#!/usr/bin/env python3
"""Pipe-based inference for T5ynth — stdin JSON requests, stdout binary audio.

Protocol:
  Request:  Single-line JSON on stdin
  Response: \x01 + header (3×int32: samples, channels, sampleRate) + float32 PCM
  Error:    \x00 + uint32 length + UTF-8 message
  Ready:    \x02 on startup when pipeline is loaded

Runs the real diffusers StableAudioPipeline with BrownianTreeNoiseSampler.
"""

import json
import math
import struct
import sys
import time
import logging
from pathlib import Path

import numpy as np
import torch

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(message)s",
                    stream=sys.stderr)
log = logging.getLogger("pipe_inference")

# ─── Model loading ──────────────────────────────────────────────────

def find_model_dir():
    """Find the Stable Audio model directory."""
    candidates = [
        Path.home() / "Library" / "T5ynth" / "models" / "stable-audio-open-1.0",  # macOS
        Path.home() / ".local" / "share" / "T5ynth" / "models" / "stable-audio-open-1.0",  # Linux
        Path.home() / "t5ynth" / "models" / "stable-audio-open-1.0",  # Legacy
    ]
    for d in candidates:
        if (d / "model_index.json").is_file():
            return d
    return None


def load_pipeline(model_dir):
    """Load diffusers pipeline and patch last step for CPU compatibility."""
    from diffusers import StableAudioPipeline

    log.info(f"Loading pipeline from {model_dir}...")
    pipe = StableAudioPipeline.from_pretrained(str(model_dir), torch_dtype=torch.float32)
    pipe = pipe.to("cpu")

    # Patch: skip BrownianTree noise at last step (sigma→0 causes torchsde crash on CPU)
    original_step = pipe.scheduler.step.__func__

    def patched_step(self, model_output, timestep, sample, **kwargs):
        step_index = self._step_index
        sigma_next = self.sigmas[step_index + 1] if step_index + 1 < len(self.sigmas) else 0
        if float(sigma_next) == 0.0:
            from diffusers.utils.outputs import BaseOutput
            model_output = self.convert_model_output(model_output, sample=sample)
            self.model_outputs.append(model_output)
            self._step_index += 1

            class Out(BaseOutput):
                prev_sample: torch.Tensor
            return Out(prev_sample=model_output)
        return original_step(self, model_output, timestep, sample, **kwargs)

    pipe.scheduler.step = patched_step.__get__(pipe.scheduler)
    log.info("Pipeline loaded and patched.")
    return pipe


# ─── Generation ─────────────────────────────────────────────────────

def generate(pipe, request):
    """Run generation from request dict. Returns (audio_np, sample_rate)."""
    prompt_a = request.get("prompt_a", "")
    prompt_b = request.get("prompt_b", "")
    alpha = request.get("alpha", 0.0)
    magnitude = request.get("magnitude", 1.0)
    noise_sigma = request.get("noise_sigma", 0.0)
    duration = request.get("duration", 3.0)
    start_pos = request.get("start_pos", 0.0)
    steps = request.get("steps", 20)
    cfg_scale = request.get("cfg_scale", 7.0)
    seed = request.get("seed", -1)

    if seed < 0:
        import random
        seed = random.randint(0, 2**32 - 1)

    sr = 44100
    generator = torch.Generator("cpu").manual_seed(seed)

    # Duration conditioning
    virtual_total = duration / (1.0 - start_pos) if start_pos < 1.0 else duration
    seconds_start = start_pos * virtual_total
    seconds_end = seconds_start + duration

    # Encode prompts via T5
    with torch.no_grad():
        tokenizer = pipe.tokenizer
        text_encoder = pipe.text_encoder

        def encode_text(text):
            inputs = tokenizer(text, return_tensors="pt", padding="max_length",
                               max_length=128, truncation=True)
            ids = inputs.input_ids.to(text_encoder.device)
            mask = inputs.attention_mask.to(text_encoder.device)
            out = text_encoder(input_ids=ids, attention_mask=mask)
            return out.last_hidden_state, mask

        emb_a, mask_a = encode_text(prompt_a)

        if prompt_b:
            emb_b, _ = encode_text(prompt_b)
            manipulated = (1.0 - alpha) * emb_a + alpha * emb_b
            # Renormalize if extrapolating
            if alpha < 0.0 or alpha > 1.0:
                mid = 0.5 * emb_a + 0.5 * emb_b
                ref_norm = mid.norm()
                res_norm = manipulated.norm()
                if res_norm > 1e-8:
                    manipulated = manipulated * (ref_norm / res_norm)
        else:
            manipulated = emb_a.clone()

        # Magnitude scaling
        if abs(magnitude - 1.0) > 1e-6:
            manipulated = manipulated * magnitude

        # Noise injection
        if noise_sigma > 0.0:
            torch.manual_seed(seed)
            noise = torch.randn_like(manipulated) * noise_sigma
            manipulated = manipulated + noise

        # Generate via pipeline with pre-computed embeddings
        neg_embeds = torch.zeros_like(manipulated)
        neg_mask = torch.ones_like(mask_a)

        log.info(f"Generating: '{prompt_a[:60]}' ({duration}s, {steps} steps, "
                 f"CFG={cfg_scale}, seed={seed})")
        t0 = time.time()

        result = pipe(
            prompt_embeds=manipulated,
            attention_mask=mask_a,
            negative_prompt_embeds=neg_embeds,
            negative_attention_mask=neg_mask,
            audio_start_in_s=seconds_start,
            audio_end_in_s=seconds_end,
            num_inference_steps=steps,
            guidance_scale=cfg_scale,
            generator=generator,
        )

        elapsed = time.time() - t0
        log.info(f"Generated in {elapsed:.1f}s")

        audio = result.audios[0]  # [channels, samples]
        if hasattr(audio, 'numpy'):
            audio = audio.cpu().float().numpy()

        # Trim to requested duration
        requested_samples = int(math.ceil(duration * sr))
        if audio.shape[-1] > requested_samples:
            audio = audio[..., :requested_samples]

        return audio, sr, seed, elapsed


# ─── Protocol ───────────────────────────────────────────────────────

def send_ready():
    """Signal ready to JUCE."""
    sys.stdout.buffer.write(b'\x02')
    sys.stdout.buffer.flush()


def send_audio(audio_np, sr, seed, elapsed_ms):
    """Send audio response: \x01 + header + PCM + seed(i32) + time_ms(f32)."""
    channels, samples = audio_np.shape
    pcm = audio_np.astype(np.float32).tobytes()
    header = struct.pack('<iiiiif', 1, samples, channels, sr, seed, elapsed_ms * 1000)
    sys.stdout.buffer.write(b'\x01')
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(pcm)
    sys.stdout.buffer.flush()


def send_error(message):
    """Send error response: \x00 + length + UTF-8 message."""
    msg_bytes = message.encode('utf-8')
    sys.stdout.buffer.write(b'\x00')
    sys.stdout.buffer.write(struct.pack('<I', len(msg_bytes)))
    sys.stdout.buffer.write(msg_bytes)
    sys.stdout.buffer.flush()


# ─── Main loop ──────────────────────────────────────────────────────

def main():
    import sys
    sys.setrecursionlimit(50000)  # torchsde workaround

    model_dir = find_model_dir()
    if model_dir is None:
        send_error("No model directory found")
        return

    try:
        pipe = load_pipeline(model_dir)
    except Exception as e:
        log.error(f"Failed to load pipeline: {e}")
        send_error(f"Pipeline load failed: {e}")
        return

    send_ready()
    log.info("Ready. Waiting for requests on stdin...")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
            audio, sr, seed, elapsed = generate(pipe, request)
            send_audio(audio, sr, seed, elapsed)
        except Exception as e:
            log.error(f"Generation failed: {e}")
            import traceback
            traceback.print_exc(file=sys.stderr)
            send_error(str(e))


if __name__ == "__main__":
    main()
