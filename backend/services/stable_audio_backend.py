"""
Stable Audio Backend - Audio generation via Stable Audio Open (Diffusers)

Cross-aesthetic generation support: accepts pre-computed embeddings
for direct T5 embedding manipulation.

Features:
- StableAudioPipeline from diffusers
- Text + duration conditioning (T5-Base, 768d)
- 44100Hz stereo output, max 47.55s
- On-demand lazy loading
- Embedding injection for cross-aesthetic use

Usage:
    backend = get_stable_audio_backend()
    if await backend.is_available():
        audio_bytes = await backend.generate_audio(
            prompt="ocean waves crashing on rocks",
            duration_seconds=10.0
        )
"""

import logging
import time
from typing import Optional, Dict, Any
import asyncio
import numpy as np

logger = logging.getLogger(__name__)


# ─── Cross-platform deterministic noise ─────────────────────────────
# torch.Generator("cpu") and torch.Generator("cuda") are different PRNGs —
# same seed produces different sequences. torchsde's BrownianTree uses
# torch.Generator(device) internally (brownian_interval.py:31).
#
# Fix: monkey-patch torchsde._randn to use numpy PCG64 which is identical
# on all platforms (ARM, x86, any OS).

def _patch_torchsde_for_determinism():
    """Replace torchsde's device-dependent _randn with numpy PCG64."""
    try:
        import torch
        import torchsde._brownian.brownian_interval as _bi

        def _deterministic_randn(size, dtype, device, seed):
            rng = np.random.Generator(np.random.PCG64(int(seed)))
            arr = rng.standard_normal(size).astype(np.float32)
            return torch.from_numpy(arr).to(dtype=dtype, device=device)

        _bi._randn = _deterministic_randn
        logger.info("[STABLE-AUDIO] torchsde patched for deterministic noise")
    except ImportError:
        pass  # torchsde not installed

_patch_torchsde_for_determinism()


class StableAudioGenerator:
    """
    Audio generation using Stable Audio Open (stabilityai/stable-audio-open-1.0).

    Supports:
    - Text-to-audio generation via StableAudioPipeline
    - Pre-computed embedding injection (for cross-aesthetic strategies)
    - Lazy model loading for VRAM efficiency
    """

    def __init__(self):
        from config import (
            STABLE_AUDIO_MODEL_ID,
            STABLE_AUDIO_DEVICE,
            STABLE_AUDIO_DTYPE,
            STABLE_AUDIO_LAZY_LOAD,
            STABLE_AUDIO_MAX_DURATION,
            STABLE_AUDIO_SAMPLE_RATE,
            MODEL_DIR,
        )

        # Check for local model directory first (users without HF account)
        local_model_path = MODEL_DIR / "stable-audio-open-1.0"
        if local_model_path.is_dir() and any(local_model_path.iterdir()):
            self.model_id = str(local_model_path)
            logger.info(f"[STABLE-AUDIO] Using local model: {local_model_path}")
        else:
            self.model_id = STABLE_AUDIO_MODEL_ID
            logger.info(f"[STABLE-AUDIO] No local model at {local_model_path}, "
                        f"will download from HF: {STABLE_AUDIO_MODEL_ID}")

        self.device = STABLE_AUDIO_DEVICE
        self.dtype_str = STABLE_AUDIO_DTYPE
        self.lazy_load = STABLE_AUDIO_LAZY_LOAD
        self.max_duration = STABLE_AUDIO_MAX_DURATION
        self.sample_rate = STABLE_AUDIO_SAMPLE_RATE

        # Pipeline (lazy-loaded)
        self._pipeline = None
        self._is_loaded = False
        self._vram_mb: float = 0
        self._last_used: float = 0
        self._in_use: int = 0

        logger.info(
            f"[STABLE-AUDIO] Initialized: model={self.model_id}, "
            f"device={self.device}, lazy_load={self.lazy_load}"
        )

    # =========================================================================
    # Lifecycle
    # =========================================================================

    async def is_available(self) -> bool:
        try:
            from diffusers import StableAudioPipeline  # noqa: F401
            return True
        except ImportError:
            logger.error("[STABLE-AUDIO] diffusers not installed or StableAudioPipeline not available")
            return False

    async def _load_pipeline(self) -> bool:
        if self._is_loaded:
            return True

        try:
            import torch
            from diffusers import StableAudioPipeline

            logger.info(f"[STABLE-AUDIO] Loading pipeline: {self.model_id}...")

            dtype_map = {"float16": torch.float16, "bfloat16": torch.bfloat16, "float32": torch.float32}
            torch_dtype = dtype_map.get(self.dtype_str, torch.float16)

            vram_before = torch.cuda.memory_allocated(0) if torch.cuda.is_available() else 0

            def _load():
                pipe = StableAudioPipeline.from_pretrained(
                    self.model_id,
                    torch_dtype=torch_dtype,
                )
                pipe = pipe.to(self.device)
                return pipe

            self._pipeline = await asyncio.to_thread(_load)
            self._is_loaded = True
            self._last_used = time.time()

            vram_after = torch.cuda.memory_allocated(0) if torch.cuda.is_available() else 0
            self._vram_mb = (vram_after - vram_before) / (1024 * 1024)

            logger.info(f"[STABLE-AUDIO] Pipeline loaded (VRAM: {self._vram_mb:.0f}MB)")
            return True

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Failed to load pipeline: {e}")
            import traceback
            traceback.print_exc()
            return False

    async def unload_pipeline(self) -> bool:
        if not self._is_loaded:
            return False

        try:
            import torch

            del self._pipeline
            self._pipeline = None
            self._is_loaded = False
            self._vram_mb = 0
            self._in_use = 0

            if torch.cuda.is_available():
                torch.cuda.empty_cache()
                torch.cuda.synchronize()

            logger.info("[STABLE-AUDIO] Pipeline unloaded")
            return True

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Error unloading pipeline: {e}")
            return False

    # =========================================================================
    # Generation
    # =========================================================================

    async def generate_audio(
        self,
        prompt: str,
        duration_seconds: float = 10.0,
        negative_prompt: str = "",
        steps: int = 20,
        cfg_scale: float = 7.0,
        seed: int = -1,
        output_format: str = "wav",
    ) -> Optional[bytes]:
        """
        Generate audio from text prompt.

        Args:
            prompt: Text description of desired audio
            duration_seconds: Duration in seconds (max 47.55)
            negative_prompt: Negative conditioning text
            steps: Number of inference steps (default 20)
            cfg_scale: Classifier-free guidance scale
            seed: Seed for reproducibility (-1 = random)
            output_format: 'wav' or 'mp3'

        Returns:
            Audio bytes or None on failure
        """
        try:
            import torch

            if not self._is_loaded:
                if not await self._load_pipeline():
                    return None

            self._in_use += 1
            self._last_used = time.time()

            try:
                duration_seconds = min(duration_seconds, self.max_duration)

                if seed == -1:
                    import random
                    seed = random.randint(0, 2**32 - 1)

                # CPU generator for cross-platform determinism (BrownianTree
                # noise is already patched via _patch_torchsde_for_determinism)
                generator = torch.Generator(device="cpu").manual_seed(seed)

                logger.info(
                    f"[STABLE-AUDIO] Generating: prompt='{prompt[:80]}...', "
                    f"duration={duration_seconds}s, steps={steps}, cfg={cfg_scale}, seed={seed}"
                )

                def _generate():
                    with torch.no_grad():
                        result = self._pipeline(
                            prompt=prompt,
                            negative_prompt=negative_prompt if negative_prompt else None,
                            audio_end_in_s=duration_seconds,
                            num_inference_steps=steps,
                            guidance_scale=cfg_scale,
                            generator=generator,
                        )
                    return result.audios[0]  # [channels, samples]

                audio = await asyncio.to_thread(_generate)
                return self._encode_audio(audio, output_format, seed)

            finally:
                self._in_use -= 1

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Generation error: {e}")
            import traceback
            traceback.print_exc()
            return None

    async def generate_from_embeddings(
        self,
        prompt_embeds,  # torch.Tensor [B, seq, 768]
        attention_mask=None,  # torch.Tensor [B, seq] or None
        seconds_start: float = 0.0,
        seconds_end: float = 10.0,
        negative_prompt: str = "",
        steps: int = 20,
        cfg_scale: float = 7.0,
        seed: int = -1,
    ) -> Optional[bytes]:
        """
        Generate audio from pre-computed embeddings (for cross-aesthetic use).

        The pipeline accepts prompt_embeds to bypass T5 text encoding.
        This enables manipulated T5 embeddings to be injected directly
        as conditioning.

        CRITICAL: When using pre-computed embeddings with CFG (guidance_scale > 1),
        we must also provide negative_prompt_embeds as zeros. Otherwise the pipeline
        encodes negative_prompt text via T5, and the CFG computation becomes:
            output = T5("") + cfg * (CLIP_features - T5(""))
        Since the spaces are different, the subtraction is meaningless.
        With zeros: output = cfg * embeddings (correct).

        Args:
            prompt_embeds: Pre-computed conditioning tensor [B, seq, 768]
            attention_mask: Attention mask for embeddings [B, seq]
            seconds_start: Audio start time
            seconds_end: Audio end time (max 47.55)
            negative_prompt: Ignored when prompt_embeds is provided (kept for API compat)
            steps: Number of inference steps (default 20)
            cfg_scale: Classifier-free guidance scale
            seed: Seed for reproducibility (-1 = random)

        Returns:
            Audio bytes (WAV) or None on failure
        """
        try:
            import torch

            if not self._is_loaded:
                if not await self._load_pipeline():
                    return None

            self._in_use += 1
            self._last_used = time.time()

            try:
                seconds_end = min(seconds_end, self.max_duration)

                if seed == -1:
                    import random
                    seed = random.randint(0, 2**32 - 1)

                # CPU generator for cross-platform determinism
                generator = torch.Generator(device="cpu").manual_seed(seed)

                # Move embeddings to device
                xf_dtype = self._pipeline.transformer.dtype
                prompt_embeds = prompt_embeds.to(device=self.device, dtype=xf_dtype)

                if attention_mask is not None:
                    attention_mask = attention_mask.to(device=self.device)

                # Zero negative embeddings: keeps CFG in the same feature space
                negative_prompt_embeds = torch.zeros_like(prompt_embeds)
                negative_attention_mask = torch.ones_like(attention_mask)

                logger.info(
                    f"[STABLE-AUDIO] Generating from embeddings: "
                    f"shape={list(prompt_embeds.shape)}, "
                    f"duration={seconds_end - seconds_start}s, steps={steps}, seed={seed}"
                )

                def _generate():
                    with torch.no_grad():
                        result = self._pipeline(
                            prompt_embeds=prompt_embeds,
                            attention_mask=attention_mask,
                            negative_prompt_embeds=negative_prompt_embeds,
                            negative_attention_mask=negative_attention_mask,
                            audio_start_in_s=seconds_start,
                            audio_end_in_s=seconds_end,
                            num_inference_steps=steps,
                            guidance_scale=cfg_scale,
                            generator=generator,
                        )
                    return result.audios[0]

                audio = await asyncio.to_thread(_generate)
                return self._encode_audio(audio, "wav", seed)

            finally:
                self._in_use -= 1

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Embedding generation error: {e}")
            import traceback
            traceback.print_exc()
            return None

    async def encode_prompt(self, text: str):
        """
        Encode text via Stable Audio's T5 tokenizer + T5EncoderModel.

        Exposes the T5 conditioning step so callers can manipulate
        the embedding before passing it to generate_from_embeddings().

        Args:
            text: Text prompt to encode

        Returns:
            Tuple of (prompt_embeds, attention_mask) or (None, None) on failure.
            prompt_embeds: Tensor [1, seq_len, 768]
            attention_mask: Tensor [1, seq_len]
        """
        try:
            import torch

            if not self._is_loaded:
                if not await self._load_pipeline():
                    return None, None

            self._last_used = time.time()

            pipe = self._pipeline
            tokenizer = pipe.tokenizer
            text_encoder = pipe.text_encoder

            def _encode():
                with torch.no_grad():
                    inputs = tokenizer(
                        text,
                        return_tensors="pt",
                        padding="max_length",
                        max_length=tokenizer.model_max_length,
                        truncation=True,
                    )
                    input_ids = inputs.input_ids.to(text_encoder.device)
                    attention_mask = inputs.attention_mask.to(text_encoder.device)

                    encoder_output = text_encoder(
                        input_ids=input_ids,
                        attention_mask=attention_mask,
                    )
                    # T5 hidden states: [1, seq_len, 768]
                    hidden_states = encoder_output.last_hidden_state

                    # Apply the projection model (StableAudioProjectionModel)
                    # In Stable Audio, text_projection is nn.Identity() (768->768)
                    projection = pipe.projection_model.text_projection
                    projected = projection(hidden_states)

                    return projected, attention_mask

            prompt_embeds, attention_mask = await asyncio.to_thread(_encode)
            logger.info(
                f"[STABLE-AUDIO] Encoded prompt: shape={list(prompt_embeds.shape)}, "
                f"text='{text[:60]}...'"
            )
            return prompt_embeds, attention_mask

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Prompt encoding error: {e}")
            import traceback
            traceback.print_exc()
            return None, None

    def get_vae(self):
        """Get the VAE (AutoencoderOobleck) for cross-aesthetic latent decoding."""
        if not self._is_loaded or self._pipeline is None:
            return None
        return self._pipeline.vae

    def _encode_audio(self, audio, output_format: str, seed: int) -> Optional[bytes]:
        """Encode audio tensor to bytes."""
        import io
        import numpy as np

        try:
            # audio shape: [channels, samples] as numpy or tensor
            # Pipeline outputs float16 when torch_dtype=float16 -- soundfile needs float32
            if hasattr(audio, 'numpy'):
                audio_np = audio.cpu().float().numpy()
            elif hasattr(audio, 'cpu'):
                audio_np = audio.cpu().float().numpy()
            else:
                audio_np = np.array(audio)

            # Transpose for soundfile: [samples, channels]
            if audio_np.ndim == 2:
                audio_np = audio_np.T

            if output_format == "mp3":
                return self._encode_mp3(audio_np)
            else:
                return self._encode_wav(audio_np)

        except Exception as e:
            logger.error(f"[STABLE-AUDIO] Audio encoding error: {e}")
            return None

    def _encode_wav(self, audio_np) -> bytes:
        import io
        import soundfile as sf

        buffer = io.BytesIO()
        sf.write(buffer, audio_np, self.sample_rate, format="WAV")
        buffer.seek(0)
        return buffer.getvalue()

    def _encode_mp3(self, audio_np) -> bytes:
        import io

        # Write WAV first, then convert (pydub or direct ffmpeg)
        try:
            from pydub import AudioSegment
            wav_bytes = self._encode_wav(audio_np)
            audio_segment = AudioSegment.from_wav(io.BytesIO(wav_bytes))
            mp3_buffer = io.BytesIO()
            audio_segment.export(mp3_buffer, format="mp3", bitrate="192k")
            mp3_buffer.seek(0)
            return mp3_buffer.getvalue()
        except ImportError:
            logger.warning("[STABLE-AUDIO] pydub not available, falling back to WAV")
            return self._encode_wav(audio_np)


# =============================================================================
# Singleton
# =============================================================================

_backend: Optional[StableAudioGenerator] = None


def get_stable_audio_backend() -> StableAudioGenerator:
    global _backend
    if _backend is None:
        _backend = StableAudioGenerator()
    return _backend


def reset_stable_audio_backend():
    global _backend
    _backend = None
