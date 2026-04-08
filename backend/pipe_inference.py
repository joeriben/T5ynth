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

def _model_format(model_dir):
    """Detect model format. Returns 'diffusers', 'audioldm2', 'native', or None."""
    model_index = model_dir / "model_index.json"
    if model_index.is_file():
        try:
            with open(model_index) as f:
                idx = json.load(f)
            if "AudioLDM2" in idx.get("_class_name", ""):
                return "audioldm2"
        except (json.JSONDecodeError, OSError):
            pass
        return "diffusers"
    if (model_dir / "model_config.json").is_file():
        return "native"
    return None

# {name: "diffusers"|"native"} — format of each discovered model
_model_formats = {}

def find_models():
    """Discover all model directories (diffusers or native). Returns {name: Path}."""
    base_dirs = [
        Path.home() / "Library" / "T5ynth" / "models",       # macOS
        Path.home() / ".local" / "share" / "T5ynth" / "models",  # Linux
        Path.home() / "t5ynth" / "models",                    # Legacy
    ]
    models = {}
    for base in base_dirs:
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            fmt = _model_format(child) if child.is_dir() else None
            if fmt and child.name not in models:
                models[child.name] = child
                _model_formats[child.name] = fmt
    return models


# ─── Lazy pipeline cache ──────────────────────────────────────────────
_available_models = {}   # {name: Path}  — set in main()
_loaded_pipelines = {}   # {(model, device): pipeline}


def get_pipeline(model_name, device):
    """Get or lazily load a pipeline for model+device."""
    key = (model_name, device)
    if key not in _loaded_pipelines:
        if model_name not in _available_models:
            raise ValueError(f"Unknown model: {model_name} (have: {list(_available_models.keys())})")
        model_dir = _available_models[model_name]
        log.info(f"Lazy-loading {model_name} on {device}...")
        _loaded_pipelines[key] = load_pipeline(model_dir, device)
    return _loaded_pipelines[key]


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
    if not hasattr(pipe.scheduler.step, '__func__'):
        log.info("Scheduler patch not applicable (no __func__)")
        return
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
    """Load pipeline on a specific device. Dispatches by format."""
    model_name = model_dir.name
    fmt = _model_formats.get(model_name, "diffusers")
    if fmt == "audioldm2":
        return _load_audioldm2_pipeline(model_dir, device)
    if fmt == "native":
        return _load_native_pipeline(model_dir, device)
    return _load_diffusers_pipeline(model_dir, device)


def _load_diffusers_pipeline(model_dir, device):
    """Load diffusers StableAudioPipeline."""
    from diffusers import StableAudioPipeline

    log.info(f"Loading diffusers pipeline from {model_dir} on {device}...")
    pipe = StableAudioPipeline.from_pretrained(str(model_dir), torch_dtype=torch.float32)
    pipe = pipe.to(device)

    _patch_scheduler(pipe)

    # Attention slicing reduces peak memory and improves MPS throughput ~20%
    if device in ("mps", "cpu"):
        pipe.enable_attention_slicing()
        log.info(f"Attention slicing enabled for {device}")

    log.info(f"Diffusers pipeline loaded on {device}.")
    return pipe


def _mock_optional_deps():
    """Mock non-essential stable-audio-tools dependencies for minimal import."""
    import types
    mocks = ['skimage', 'skimage.transform', 'dac', 'encodec', 'laion_clap',
             'pedalboard', 'pedalboard.io', 'pytorch_lightning', 'wandb',
             'v_diffusion_pytorch', 'gradio', 'jsonmerge', 'clean_fid', 'kornia']
    k_diff = types.ModuleType('k_diffusion')
    for sub in ['augmentation', 'config', 'evaluation', 'external', 'gns',
                'layers', 'models', 'sampling', 'utils']:
        mod = types.ModuleType(f'k_diffusion.{sub}')
        setattr(k_diff, sub, mod)
        sys.modules[f'k_diffusion.{sub}'] = mod
    sys.modules['k_diffusion'] = k_diff
    for mod in mocks:
        if mod not in sys.modules:
            sys.modules[mod] = types.ModuleType(mod)


class NativePipeline:
    """Wrapper for stable-audio-tools native format models."""

    def __init__(self, model, model_config, device):
        self.model = model
        self.model_config = model_config
        self.device = device
        self.sample_size = model_config["sample_size"]
        self.sample_rate = model_config["sample_rate"]
        # Extract T5 encoder reference for embedding access
        self._t5_conditioner = None
        for key, cond in model.conditioner.conditioners.items():
            if hasattr(cond, 'tokenizer') and hasattr(cond, 'model'):
                self._t5_conditioner = cond
                break

    @property
    def tokenizer(self):
        return self._t5_conditioner.tokenizer if self._t5_conditioner else None

    @property
    def text_encoder(self):
        return self._t5_conditioner.model if self._t5_conditioner else None


class AudioLDM2Wrapper:
    """Wrapper for AudioLDM2Pipeline with metadata for routing."""

    def __init__(self, pipe, device):
        self.pipe = pipe
        self.device = device
        self.sample_rate = 16000
        self.target_sample_rate = 44100

    @property
    def tokenizer(self):
        return self.pipe.tokenizer_2

    @property
    def text_encoder(self):
        return self.pipe.text_encoder_2


def _load_audioldm2_pipeline(model_dir, device):
    """Load AudioLDM2Pipeline from diffusers format."""
    from diffusers import AudioLDM2Pipeline

    log.info(f"Loading AudioLDM2 pipeline from {model_dir} on {device}...")
    pipe = AudioLDM2Pipeline.from_pretrained(str(model_dir), torch_dtype=torch.float32)
    pipe = pipe.to(device)

    if device in ("mps", "cpu"):
        pipe.enable_attention_slicing()
        log.info(f"Attention slicing enabled for AudioLDM2 on {device}")

    log.info(f"AudioLDM2 pipeline loaded on {device}.")
    return AudioLDM2Wrapper(pipe, device)


def _load_native_pipeline(model_dir, device):
    """Load native stable-audio-tools model."""
    _mock_optional_deps()

    from stable_audio_tools.models.factory import create_model_from_config
    from stable_audio_tools.models.utils import load_ckpt_state_dict

    config_path = model_dir / "model_config.json"
    weights_path = model_dir / "model.safetensors"
    if not weights_path.is_file():
        weights_path = model_dir / "model.ckpt"

    log.info(f"Loading native pipeline from {model_dir} on {device}...")

    # Suppress any stdout output during model loading (protects IPC pipe)
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        with open(config_path) as f:
            model_config = json.load(f)

        model = create_model_from_config(model_config)
        model.load_state_dict(load_ckpt_state_dict(str(weights_path)))
        model.eval()
        model = model.to(device)
    finally:
        sys.stdout = real_stdout

    log.info(f"Native pipeline loaded on {device}.")
    return NativePipeline(model, model_config, device)


def load_default_model(model_name, devices):
    """Eagerly load the default model on all devices. Returns list of loaded devices."""
    loaded = []
    for dev in devices:
        try:
            get_pipeline(model_name, dev)
            loaded.append(dev)
        except Exception as e:
            log.warning(f"Failed to load {model_name} on {dev}: {e}")
    if not loaded:
        raise RuntimeError(f"Could not load {model_name} on any device")
    return loaded


# ─── Audio resampling ────────────────────────────────────────────────

def _resample_audio(audio_np, from_sr, to_sr):
    """Resample audio array [channels, samples] from from_sr to to_sr."""
    if from_sr == to_sr:
        return audio_np
    from scipy.signal import resample_poly
    gcd = math.gcd(to_sr, from_sr)
    up, down = to_sr // gcd, from_sr // gcd
    return resample_poly(audio_np, up, down, axis=-1).astype(np.float32)


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


# ─── Semantic axes ───────────────────────────────────────────────────
# Pole prompts matching AxesPanel.cpp kEffectiveAxes (display → key mapping)

SEMANTIC_AXIS_POLES = {
    "music_noise":          ("noise",       "music"),
    "acoustic_electronic":  ("electronic",  "acoustic"),
    "improvised_composed":  ("improvised",  "composed"),
    "refined_raw":          ("raw",         "refined"),
    "solo_ensemble":        ("ensemble",    "solo"),
    "sacred_secular":       ("secular",     "sacred"),
    "tonal_noisy":          ("noisy",       "tonal"),
    "rhythmic_sustained":   ("sustained",   "rhythmic"),
}

_axis_emb_cache = {}  # {(axis_key, model_name): (dir_tensor, neutral_emb)}


def _apply_semantic_axes(manipulated, axes_dict, encode_fn, model_name):
    """Apply semantic axis deltas to embedding tensor [1, seq, 768].

    axes_dict: {"music_noise": 0.5, "tonal_noisy": -0.3, ...}
    encode_fn: function(text) → (emb[1,seq,768], mask[1,seq])

    For each axis, computes direction = pole_emb - neutral_emb,
    then adds direction * value to the manipulated embedding.
    """
    if not axes_dict:
        return manipulated

    # Encode neutral once (cached per model)
    cache_key = ("__neutral__", model_name)
    if cache_key not in _axis_emb_cache:
        neutral_emb, _ = encode_fn("")
        _axis_emb_cache[cache_key] = neutral_emb.detach()
    neutral_emb = _axis_emb_cache[cache_key].to(manipulated.device)

    for axis_key, value in axes_dict.items():
        if abs(value) < 0.001:
            continue
        poles = SEMANTIC_AXIS_POLES.get(axis_key)
        if not poles:
            continue
        pole_a_text, pole_b_text = poles

        # Cache pole embeddings per model
        for pole_text in (pole_a_text, pole_b_text):
            ck = (axis_key + "_" + pole_text, model_name)
            if ck not in _axis_emb_cache:
                emb, _ = encode_fn(pole_text)
                _axis_emb_cache[ck] = emb.detach()

        emb_a = _axis_emb_cache[(axis_key + "_" + pole_a_text, model_name)].to(manipulated.device)
        emb_b = _axis_emb_cache[(axis_key + "_" + pole_b_text, model_name)].to(manipulated.device)

        # Direction: positive value → pole_b, negative → pole_a
        if value >= 0:
            direction = emb_b - neutral_emb
        else:
            direction = emb_a - neutral_emb

        # Mean-pool direction to [1, 1, dim] for models with variable seq_len
        # (AudioLDM2 uses dynamic T5 padding; SA uses fixed 128-token padding)
        if direction.shape[1] != manipulated.shape[1]:
            direction = direction.mean(dim=1, keepdim=True)

        manipulated = manipulated + direction * abs(value)

    return manipulated


# ─── Generation ─────────────────────────────────────────────────────

def _mean_pool(emb, mask):
    """Mean-pool embedding [1, seq, 768] using attention mask → [768]."""
    # mask: [1, seq] → [1, seq, 1]
    m = mask.unsqueeze(-1).float()
    pooled = (emb * m).sum(dim=1) / m.sum(dim=1).clamp(min=1.0)
    return pooled.squeeze(0).cpu().float().numpy()  # [768]


def _generate_audioldm2(wrapper, request):
    """Generate audio using AudioLDM2Pipeline with full embedding manipulation.

    AudioLDM2 uses dual embeddings: prompt_embeds (T5 raw, [B,seq,1024]) for
    secondary cross-attention and generated_prompt_embeds (GPT2, [B,8,768]) for
    primary cross-attention. We encode with CFG=True to get properly shaped
    negative+positive pairs, then manipulate the positive halves.
    Output is resampled from 16kHz to 44.1kHz for JUCE compatibility.
    """
    pipe = wrapper.pipe
    prompt_a = request.get("prompt_a", "")
    prompt_b = request.get("prompt_b", "")
    alpha = request.get("alpha", 0.0)
    magnitude = request.get("magnitude", 1.0)
    noise_sigma = request.get("noise_sigma", 0.0)
    duration = request.get("duration", 3.0)
    steps = request.get("steps", 50)
    cfg_scale = request.get("cfg_scale", 3.5)
    seed = request.get("seed", -1)
    dim_offsets = request.get("dimension_offsets")

    if seed < 0:
        import random
        seed = random.randint(0, 2**31 - 1)

    device = wrapper.device
    generator = torch.Generator("cpu").manual_seed(seed)

    with torch.no_grad():
        # Encode A with CFG — returns [2, seq, dim] (neg @ 0, pos @ 1)
        pe_a, mask_a, gpe_a = pipe.encode_prompt(
            prompt=prompt_a, device=device,
            num_waveforms_per_prompt=1,
            do_classifier_free_guidance=True,
            negative_prompt="",
        )
        neg_pe   = pe_a[0:1]
        pos_pe_a = pe_a[1:2]
        neg_mask  = mask_a[0:1]
        pos_mask_a = mask_a[1:2]
        neg_gpe  = gpe_a[0:1]
        pos_gpe_a = gpe_a[1:2]

        if prompt_b:
            pe_b, mask_b, gpe_b = pipe.encode_prompt(
                prompt=prompt_b, device=device,
                num_waveforms_per_prompt=1,
                do_classifier_free_guidance=True,
                negative_prompt="",
            )
            pos_pe_b   = pe_b[1:2]
            pos_mask_b = mask_b[1:2]
            pos_gpe_b  = gpe_b[1:2]

            # Pad prompt_embeds to same seq_len (T5 uses dynamic padding)
            s_a, s_b = pos_pe_a.shape[1], pos_pe_b.shape[1]
            if s_a != s_b:
                pad_a = max(0, s_b - s_a)
                pad_b = max(0, s_a - s_b)
                if pad_a:
                    pos_pe_a   = torch.nn.functional.pad(pos_pe_a, (0, 0, 0, pad_a))
                    pos_mask_a = torch.nn.functional.pad(pos_mask_a, (0, pad_a))
                    neg_pe     = torch.nn.functional.pad(neg_pe, (0, 0, 0, pad_a))
                    neg_mask   = torch.nn.functional.pad(neg_mask, (0, pad_a))
                if pad_b:
                    pos_pe_b   = torch.nn.functional.pad(pos_pe_b, (0, 0, 0, pad_b))
                    pos_mask_b = torch.nn.functional.pad(pos_mask_b, (0, pad_b))

            # Alpha interpolation: 0 = midpoint, -1 = pure A, +1 = pure B
            w_a = 0.5 - 0.5 * alpha
            w_b = 0.5 + 0.5 * alpha
            manipulated_pe  = w_a * pos_pe_a  + w_b * pos_pe_b
            manipulated_gpe = w_a * pos_gpe_a + w_b * pos_gpe_b
            pos_mask = (pos_mask_a | pos_mask_b)

            # Renormalize if extrapolating (|alpha| > 1)
            if alpha < -1.0 or alpha > 1.0:
                for manip, ref_a, ref_b in [(manipulated_pe, pos_pe_a, pos_pe_b),
                                             (manipulated_gpe, pos_gpe_a, pos_gpe_b)]:
                    ref_norm = ref_a.norm() if alpha < 0.0 else ref_b.norm()
                    res_norm = manip.norm()
                    if res_norm > 1e-8:
                        manip.mul_(ref_norm / res_norm)
        else:
            manipulated_pe  = pos_pe_a.clone()
            manipulated_gpe = pos_gpe_a.clone()
            pos_mask = pos_mask_a

        # Magnitude scaling
        if abs(magnitude - 1.0) > 1e-6:
            manipulated_pe = manipulated_pe * magnitude

        # Noise injection (numpy PCG64 for cross-platform determinism)
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(manipulated_pe.shape).astype(np.float32)
            noise = torch.from_numpy(noise_np).to(manipulated_pe.device) * noise_sigma
            manipulated_pe = manipulated_pe + noise

        # Dimension offsets from DimensionExplorer
        if dim_offsets:
            for idx, val in dim_offsets:
                if 0 <= idx < manipulated_pe.shape[-1]:
                    manipulated_pe[:, :, idx] += val

        # Semantic axes
        sem_axes = request.get("semantic_axes")
        if sem_axes:
            def audioldm2_encode(text):
                pe, m, _ = pipe.encode_prompt(
                    prompt=text, device=device,
                    num_waveforms_per_prompt=1,
                    do_classifier_free_guidance=False,
                )
                return pe, m
            manipulated_pe = _apply_semantic_axes(
                manipulated_pe, sem_axes, audioldm2_encode, "audioldm2"
            )

    offsets_str = f", {len(dim_offsets)} offsets" if dim_offsets else ""
    log.info(f"Generating (AudioLDM2) on {device}: '{prompt_a[:60]}' ({duration}s, {steps} steps, "
             f"CFG={cfg_scale}, mag={magnitude}, noise={noise_sigma}, seed={seed}{offsets_str})")
    t0 = time.time()

    result = pipe(
        prompt_embeds=manipulated_pe,
        attention_mask=pos_mask,
        negative_prompt_embeds=neg_pe,
        negative_attention_mask=neg_mask,
        generated_prompt_embeds=manipulated_gpe,
        negative_generated_prompt_embeds=neg_gpe,
        audio_length_in_s=duration,
        num_inference_steps=steps,
        guidance_scale=cfg_scale,
        generator=generator,
        output_type="np",
    )

    elapsed = time.time() - t0

    audio_np = result.audios[0]
    if audio_np.ndim == 1:
        audio_np = audio_np[np.newaxis, :]

    audio_np = _resample_audio(audio_np, wrapper.sample_rate, wrapper.target_sample_rate)

    if audio_np.shape[0] == 1:
        audio_np = np.vstack([audio_np, audio_np])

    log.info(f"Generated (AudioLDM2) in {elapsed:.1f}s, {audio_np.shape[1]} samples @ 44.1kHz")
    return audio_np, wrapper.target_sample_rate, seed, elapsed, None


def _generate_native(pipe, request):
    """Generate audio using native stable-audio-tools pipeline (e.g. small model).

    Supports the same embedding manipulations as the diffusers path:
    alpha interpolation, magnitude, noise injection, dimension offsets.
    """
    from stable_audio_tools.inference.generation import generate_diffusion_cond

    prompt_a = request.get("prompt_a", "")
    prompt_b = request.get("prompt_b", "")
    alpha = request.get("alpha", 0.0)
    magnitude = request.get("magnitude", 1.0)
    noise_sigma = request.get("noise_sigma", 0.0)
    duration = request.get("duration", 3.0)
    steps = request.get("steps", 8)
    cfg_scale = request.get("cfg_scale", 4.0)
    seed = request.get("seed", -1)
    dim_offsets = request.get("dimension_offsets")

    if seed < 0:
        import random
        seed = random.randint(0, 2**31 - 1)

    sr = pipe.sample_rate
    device = pipe.device

    # ── Encode prompts via the model's built-in T5 conditioner ──
    # Then manipulate embeddings (alpha, magnitude, noise, dim offsets)
    # and pass as pre-computed conditioning_tensors.
    with torch.no_grad():
        # Run full conditioner to get base conditioning tensors
        cond_a = pipe.model.conditioner([{"prompt": prompt_a, "seconds_total": duration}], device)

        # Extract T5 embedding for manipulation: {"prompt": [emb_tensor, None], ...}
        prompt_emb_a = cond_a["prompt"][0]   # [1, seq, 768]

        # Mean-pool for DimensionExplorer stats (before manipulation)
        # Use attention-style pooling (non-zero tokens)
        emb_a_pooled = prompt_emb_a.squeeze(0).mean(dim=0).cpu().float().numpy()

        if prompt_b:
            cond_b = pipe.model.conditioner([{"prompt": prompt_b, "seconds_total": duration}], device)
            prompt_emb_b = cond_b["prompt"][0]
            emb_b_pooled = prompt_emb_b.squeeze(0).mean(dim=0).cpu().float().numpy()
            # Alpha interpolation: 0 = midpoint, -1 = pure A, +1 = pure B
            manipulated = (0.5 - 0.5 * alpha) * prompt_emb_a + (0.5 + 0.5 * alpha) * prompt_emb_b
            if alpha < -1.0 or alpha > 1.0:
                ref_norm = prompt_emb_a.norm() if alpha < 0.0 else prompt_emb_b.norm()
                res_norm = manipulated.norm()
                if res_norm > 1e-8:
                    manipulated = manipulated * (ref_norm / res_norm)
        else:
            emb_b_pooled = np.zeros(768, dtype=np.float32)
            manipulated = prompt_emb_a.clone()

        # Magnitude scaling
        if abs(magnitude - 1.0) > 1e-6:
            manipulated = manipulated * magnitude

        # Noise injection (numpy PCG64 for cross-platform determinism)
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(manipulated.shape).astype(np.float32)
            noise = torch.from_numpy(noise_np).to(manipulated.device) * noise_sigma
            manipulated = manipulated + noise

        # Dimension offsets from DimensionExplorer
        if dim_offsets:
            for idx, val in dim_offsets:
                if 0 <= idx < manipulated.shape[-1]:
                    manipulated[:, :, idx] += val

        # Apply semantic axes
        sem_axes = request.get("semantic_axes")
        if sem_axes:
            def native_encode(text):
                cond = pipe.model.conditioner([{"prompt": text, "seconds_total": 1}], device)
                return cond["prompt"][0], None
            manipulated = _apply_semantic_axes(manipulated, sem_axes, native_encode, "native")

        # Re-apply attention mask to zero out padding (native conditioner zeros
        # padding, but manipulations like noise/offsets/axes can pollute it;
        # the DiT disables cross_attn_cond_mask so padding must stay zero)
        mask = cond_a["prompt"][1]
        if mask is not None:
            manipulated = manipulated * mask.unsqueeze(-1).float()

        # Replace prompt embedding in conditioning tensors
        cond_a["prompt"] = [manipulated, cond_a["prompt"][1]]

    offsets_str = f", {len(dim_offsets)} offsets" if dim_offsets else ""
    log.info(f"Generating (native) on {device}: '{prompt_a[:60]}' ({duration}s, {steps} steps, "
             f"CFG={cfg_scale}, mag={magnitude}, noise={noise_sigma}, seed={seed}{offsets_str})")
    t0 = time.time()

    # stable_audio_tools prints seed + tqdm progress to stdout, which would
    # corrupt the binary IPC pipe to JUCE. Redirect stdout → stderr temporarily.
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        output = generate_diffusion_cond(
            pipe.model,
            steps=steps,
            cfg_scale=cfg_scale,
            conditioning_tensors=cond_a,
            sample_size=pipe.sample_size,
            device=device,
            seed=seed,
        )
    finally:
        sys.stdout = real_stdout

    elapsed = time.time() - t0
    audio_np = output.squeeze(0).cpu().float().numpy()  # [channels, samples]
    requested_samples = int(math.ceil(duration * sr))
    if audio_np.shape[-1] > requested_samples:
        audio_np = audio_np[..., :requested_samples]

    rms = float(np.sqrt(np.mean(audio_np ** 2)))
    peak = float(np.max(np.abs(audio_np)))
    log.info(f"Generated (native) in {elapsed:.1f}s, RMS={rms:.4f}, peak={peak:.4f}, shape={audio_np.shape}")
    emb_stats = (emb_a_pooled, emb_b_pooled)
    return audio_np, sr, seed, elapsed, emb_stats


def generate(pipe, request):
    """Run generation from request dict. Returns (audio_np, sample_rate, seed, elapsed, emb_stats).

    emb_stats = (emb_a_pooled[768], emb_b_pooled[768] or zeros)
    If "cache_as" is set in the request, the pre-VAE latent is cached for
    fast interpolation via interpolate_and_decode().
    """
    if isinstance(pipe, AudioLDM2Wrapper):
        return _generate_audioldm2(pipe, request)
    if isinstance(pipe, NativePipeline):
        return _generate_native(pipe, request)
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
    dim_offsets = request.get("dimension_offsets")  # optional: [[idx, val], ...]

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

        # Mean-pool for DimensionExplorer stats (before manipulation)
        emb_a_pooled = _mean_pool(emb_a, mask_a)

        if prompt_b:
            emb_b, mask_b = encode_text(prompt_b)
            emb_b_pooled = _mean_pool(emb_b, mask_b)
            # alpha: 0 = midpoint, -1 = pure A, +1 = pure B
            manipulated = (0.5 - 0.5 * alpha) * emb_a + (0.5 + 0.5 * alpha) * emb_b
            # Renormalize if extrapolating (|alpha| > 1)
            if alpha < -1.0 or alpha > 1.0:
                ref_norm = emb_a.norm() if alpha < 0.0 else emb_b.norm()
                res_norm = manipulated.norm()
                if res_norm > 1e-8:
                    manipulated = manipulated * (ref_norm / res_norm)
        else:
            emb_b_pooled = np.zeros(768, dtype=np.float32)
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

        # Apply dimension offsets from DimensionExplorer
        if dim_offsets:
            for idx, val in dim_offsets:
                if 0 <= idx < manipulated.shape[-1]:
                    manipulated[:, :, idx] += val

        # Apply semantic axes
        sem_axes = request.get("semantic_axes")
        if sem_axes:
            manipulated = _apply_semantic_axes(manipulated, sem_axes, encode_text, "diffusers")

        # Generate via pipeline with pre-computed embeddings
        neg_embeds = torch.zeros_like(manipulated)
        neg_mask = torch.ones_like(mask_a)

        device = next(pipe.transformer.parameters()).device
        cache_str = f", cache_as='{cache_as}'" if cache_as else ""
        offsets_str = f", {len(dim_offsets)} offsets" if dim_offsets else ""
        log.info(f"Generating on {device}: '{prompt_a[:60]}' ({duration}s, {steps} steps, "
                 f"CFG={cfg_scale}, seed={seed}{cache_str}{offsets_str})")
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

        emb_stats = (emb_a_pooled, emb_b_pooled)
        return audio_np, sr, seed, elapsed, emb_stats


# ─── Protocol ───────────────────────────────────────────────────────

def send_ready(devices, default_device, models, default_model):
    """Signal ready to JUCE with device and model info."""
    info = json.dumps({
        "devices": devices,
        "default": default_device,
        "models": list(models.keys()),
        "default_model": default_model,
    }).encode("utf-8")
    sys.stdout.buffer.write(b'\x02')
    sys.stdout.buffer.write(struct.pack('<H', len(info)))
    sys.stdout.buffer.write(info)
    sys.stdout.buffer.flush()


def send_audio(audio_np, sr, seed, elapsed_ms, emb_stats=None):
    """Send audio response: \x01 + header + PCM [+ embedding stats].

    If emb_stats is (emb_a[768], emb_b[768]), appends:
        uint16(num_dims) + float32[num_dims] emb_a + float32[num_dims] emb_b
    """
    channels, samples = audio_np.shape
    pcm = audio_np.astype(np.float32).tobytes()
    header = struct.pack('<iiiiif', 1, samples, channels, sr, seed, elapsed_ms * 1000)
    sys.stdout.buffer.write(b'\x01')
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(pcm)
    if emb_stats is not None:
        emb_a, emb_b = emb_stats
        num_dims = len(emb_a)
        sys.stdout.buffer.write(struct.pack('<H', num_dims))
        sys.stdout.buffer.write(emb_a.astype(np.float32).tobytes())
        sys.stdout.buffer.write(emb_b.astype(np.float32).tobytes())
    else:
        sys.stdout.buffer.write(struct.pack('<H', 0))
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
    global _available_models
    import sys
    sys.setrecursionlimit(50000)  # torchsde workaround

    _available_models = find_models()
    if not _available_models:
        send_error("No model directories found")
        return

    devices = available_devices()

    # Default model: prefer stable-audio-open-1.0 for backward compat
    if "stable-audio-open-1.0" in _available_models:
        default_model = "stable-audio-open-1.0"
    else:
        default_model = next(iter(_available_models))

    try:
        loaded_devices = load_default_model(default_model, devices)
    except Exception as e:
        log.error(f"Failed to load default model: {e}")
        send_error(f"Pipeline load failed: {e}")
        return

    default_device = loaded_devices[0]
    send_ready(loaded_devices, default_device, _available_models, default_model)
    log.info(f"Ready. Models: {list(_available_models.keys())}, default: {default_model}, "
             f"devices: {loaded_devices}, default device: {default_device}")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)

            # Route to correct model + device
            model = request.get("model", default_model)
            device = request.get("device", default_device)
            if model not in _available_models:
                model = default_model
            if device == "auto" or device not in devices:
                device = default_device

            mode = request.get("mode", "generate")

            if mode == "preload":
                # Just load the pipeline (lazy-load cache), respond with empty audio
                log.info(f"Preloading {model} on {device}...")
                pipe = get_pipeline(model, device)
                log.info(f"Preload complete: {model} on {device}")
                # Send minimal success response (0 samples)
                empty = np.zeros((2, 0), dtype=np.float32)
                send_audio(empty, 44100, 0, 0.0)
                continue

            pipe = get_pipeline(model, device)
            emb_stats = None

            if mode == "interpolate":
                if isinstance(pipe, AudioLDM2Wrapper):
                    raise ValueError("Latent interpolation not yet supported for AudioLDM2")
                audio, sr, seed, elapsed = interpolate_and_decode(pipe, request)
            elif mode == "decode_cached":
                if isinstance(pipe, AudioLDM2Wrapper):
                    raise ValueError("Latent cache decode not yet supported for AudioLDM2")
                audio, sr, seed, elapsed = decode_cached(pipe, request)
            else:
                audio, sr, seed, elapsed, emb_stats = generate(pipe, request)

            send_audio(audio, sr, seed, elapsed, emb_stats)
        except Exception as e:
            log.error(f"Request failed (model={request.get('model', '?')}, "
                      f"mode={request.get('mode', 'generate')}): {e}")
            import traceback
            traceback.print_exc(file=sys.stderr)
            send_error(str(e))


if __name__ == "__main__":
    main()
