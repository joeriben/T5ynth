#!/usr/bin/env python3
"""Pipe-based inference for T5ynth — stdin JSON requests, stdout binary audio.

Protocol:
  Request:  Single-line JSON on stdin (includes optional "device" field)
  Response: \x01 + header (6 fields: flag,samples,channels,sr,seed,timeMs) + float32 PCM
  Error:    \x00 + uint32 length + UTF-8 message
  Ready:    \x02 + uint16 length + JSON {"devices": [...], "default": "..."}

Loads one pipeline per available device (MPS, CUDA, CPU) for instant switching.
Noise generation is patched to use numpy PCG64 for cross-platform determinism
(same seed → same audio on CPU, CUDA, ARM, x86).
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
        import torchsde._brownian.brownian_interval as _bi

        def _deterministic_randn(size, dtype, device, seed):
            rng = np.random.Generator(np.random.PCG64(int(seed)))
            arr = rng.standard_normal(size).astype(np.float32)
            return torch.from_numpy(arr).to(dtype=dtype, device=device)

        _bi._randn = _deterministic_randn
        logging.getLogger("pipe_inference").info("torchsde patched for deterministic noise")
    except ImportError:
        pass  # torchsde not installed — patch not needed

_patch_torchsde_for_determinism()

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


def available_devices():
    """Return list of available inference devices, best first."""
    devices = []
    if torch.backends.mps.is_available():
        devices.append("mps")
    if torch.cuda.is_available():
        devices.append("cuda")
    devices.append("cpu")
    return devices


def _patch_scheduler(pipe):
    """Patch scheduler to skip BrownianTree noise at last step (sigma→0 crash)."""
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


def load_pipeline(model_dir, device):
    """Load diffusers pipeline on a specific device."""
    from diffusers import StableAudioPipeline

    log.info(f"Loading pipeline from {model_dir} on {device}...")
    pipe = StableAudioPipeline.from_pretrained(str(model_dir), torch_dtype=torch.float32)
    pipe = pipe.to(device)

    _patch_scheduler(pipe)

    # Attention slicing reduces peak memory and improves MPS throughput ~20%
    if device in ("mps", "cpu"):
        pipe.enable_attention_slicing()
        log.info(f"Attention slicing enabled for {device}")

    log.info(f"Pipeline loaded on {device}.")
    return pipe


def load_all_pipelines(model_dir):
    """Load a pipeline on each available device. Returns (dict, device_list)."""
    devices = available_devices()
    pipelines = {}
    for dev in devices:
        try:
            pipelines[dev] = load_pipeline(model_dir, dev)
        except Exception as e:
            log.warning(f"Failed to load pipeline on {dev}: {e}")

    if not pipelines:
        raise RuntimeError("Could not load pipeline on any device")

    loaded = list(pipelines.keys())
    return pipelines, loaded


# ─── Latent cache ────────────────────────────────────────────────────
# Stores pre-VAE latents keyed by name. Enables fast interpolation +
# VAE-only decode (~2s) instead of full diffusion (~15-50s).
# Latents are stored on CPU for device-agnostic access.

_latent_cache = {}   # {name: torch.Tensor on CPU}


def vae_decode(pipe, latent, duration):
    """Decode a latent tensor to audio via VAE. Returns (audio_np, sr)."""
    sr = 44100
    device = next(pipe.vae.parameters()).device
    latent = latent.to(device)
    with torch.no_grad():
        audio = pipe.vae.decode(latent).sample
    audio_np = audio.squeeze(0).cpu().float().numpy()
    requested_samples = int(math.ceil(duration * sr))
    if audio_np.shape[-1] > requested_samples:
        audio_np = audio_np[..., :requested_samples]
    return audio_np, sr


def interpolate_and_decode(pipe, request):
    """Interpolate between cached latents and VAE-decode. ~2s instead of ~15-50s."""
    name_a = request["latent_a"]
    name_b = request["latent_b"]
    alpha = request.get("lerp_alpha", 0.5)
    duration = request.get("duration", 3.0)

    if name_a not in _latent_cache:
        raise ValueError(f"Latent '{name_a}' not in cache (have: {list(_latent_cache.keys())})")
    if name_b not in _latent_cache:
        raise ValueError(f"Latent '{name_b}' not in cache (have: {list(_latent_cache.keys())})")

    lat_a = _latent_cache[name_a]
    lat_b = _latent_cache[name_b]
    interpolated = (1.0 - alpha) * lat_a + alpha * lat_b

    # Optionally cache the interpolated result
    cache_as = request.get("cache_as")
    if cache_as:
        _latent_cache[cache_as] = interpolated.clone()

    t0 = time.time()
    audio_np, sr = vae_decode(pipe, interpolated, duration)
    elapsed = time.time() - t0
    log.info(f"Interpolated {name_a}↔{name_b} (α={alpha:.2f}), decoded in {elapsed:.1f}s")
    return audio_np, sr, -1, elapsed


def decode_cached(pipe, request):
    """Decode a single cached latent. For quick re-listen without diffusion."""
    name = request["latent_name"]
    duration = request.get("duration", 3.0)

    if name not in _latent_cache:
        raise ValueError(f"Latent '{name}' not in cache (have: {list(_latent_cache.keys())})")

    t0 = time.time()
    audio_np, sr = vae_decode(pipe, _latent_cache[name], duration)
    elapsed = time.time() - t0
    log.info(f"Decoded cached '{name}' in {elapsed:.1f}s")
    return audio_np, sr, -1, elapsed


# ─── Generation ─────────────────────────────────────────────────────

def generate(pipe, request):
    """Run generation from request dict. Returns (audio_np, sample_rate, seed, elapsed).

    If "cache_as" is set in the request, the pre-VAE latent is cached for
    fast interpolation via interpolate_and_decode().
    """
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
    cache_as = request.get("cache_as")  # optional: cache latent under this name

    if seed < 0:
        import random
        seed = random.randint(0, 2**31 - 1)

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

        # Noise injection (numpy PCG64 for cross-platform determinism)
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(manipulated.shape).astype(np.float32)
            noise = torch.from_numpy(noise_np).to(manipulated.device) * noise_sigma
            manipulated = manipulated + noise

        # Generate via pipeline with pre-computed embeddings
        neg_embeds = torch.zeros_like(manipulated)
        neg_mask = torch.ones_like(mask_a)

        device = next(pipe.transformer.parameters()).device
        cache_str = f", cache_as='{cache_as}'" if cache_as else ""
        log.info(f"Generating on {device}: '{prompt_a[:60]}' ({duration}s, {steps} steps, "
                 f"CFG={cfg_scale}, seed={seed}{cache_str})")
        t0 = time.time()

        # If caching requested: get latent first, then decode separately
        output_type = "latent" if cache_as else "pt"

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
            output_type=output_type,
        )

        if cache_as:
            # result.audios[0] is actually the raw latent when output_type="latent"
            latent = result.audios[0]
            if latent.dim() == 2:
                latent = latent.unsqueeze(0)  # ensure [1, C, T]
            _latent_cache[cache_as] = latent.cpu().clone()
            log.info(f"Cached latent '{cache_as}': {list(latent.shape)}")
            # Now VAE-decode for the audio response
            audio_np, _ = vae_decode(pipe, latent, duration)
        else:
            audio = result.audios[0]  # [channels, samples]
            if hasattr(audio, 'numpy'):
                audio_np = audio.cpu().float().numpy()
            else:
                audio_np = np.array(audio)
            # Trim to requested duration
            requested_samples = int(math.ceil(duration * sr))
            if audio_np.shape[-1] > requested_samples:
                audio_np = audio_np[..., :requested_samples]

        elapsed = time.time() - t0
        log.info(f"Generated in {elapsed:.1f}s")

        return audio_np, sr, seed, elapsed


# ─── Protocol ───────────────────────────────────────────────────────

def send_ready(devices, default_device):
    """Signal ready to JUCE with device info."""
    info = json.dumps({"devices": devices, "default": default_device}).encode("utf-8")
    sys.stdout.buffer.write(b'\x02')
    sys.stdout.buffer.write(struct.pack('<H', len(info)))
    sys.stdout.buffer.write(info)
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
        pipelines, devices = load_all_pipelines(model_dir)
    except Exception as e:
        log.error(f"Failed to load pipelines: {e}")
        send_error(f"Pipeline load failed: {e}")
        return

    default_device = devices[0]
    send_ready(devices, default_device)
    log.info(f"Ready. Devices: {devices}, default: {default_device}")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)

            # Route to correct device pipeline
            device = request.get("device", default_device)
            if device == "auto" or device not in pipelines:
                device = default_device
            pipe = pipelines[device]

            mode = request.get("mode", "generate")

            if mode == "interpolate":
                audio, sr, seed, elapsed = interpolate_and_decode(pipe, request)
            elif mode == "decode_cached":
                audio, sr, seed, elapsed = decode_cached(pipe, request)
            else:
                audio, sr, seed, elapsed = generate(pipe, request)

            send_audio(audio, sr, seed, elapsed)
        except Exception as e:
            log.error(f"Request failed (mode={request.get('mode', 'generate')}): {e}")
            import traceback
            traceback.print_exc(file=sys.stderr)
            send_error(str(e))


if __name__ == "__main__":
    main()
