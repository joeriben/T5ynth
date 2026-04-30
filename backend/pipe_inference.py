#!/usr/bin/env python3
"""Pipe-based inference for T5ynth — stdin JSON requests, stdout binary audio.

Protocol:
  Request:  Single-line JSON on stdin (includes optional "device" field)
  Response: \x01 + header (6 fields: flag,samples,channels,sr,seed,timeMs) + float32 PCM
  Error:    \x00 + uint32 length + UTF-8 message
  Ready:    \x02 + uint16 length + JSON {"devices": [...], "default": "..."}

Loads one startup pipeline on the preferred device and lazy-loads additional
device/model combinations on demand.
Noise generation is patched to use numpy PCG64 for cross-platform determinism
(same seed → same audio on CPU, CUDA, ARM, x86).
"""

import json
import math
import os
import struct
import sys
import time
import logging
from pathlib import Path


def _configure_cpu_budget_env():
    """Keep inference helper threads from starving the real-time audio process."""
    worker_threads = os.environ.get("T5YNTH_INFERENCE_CPU_THREADS", "2").strip()
    interop_threads = os.environ.get("T5YNTH_INFERENCE_INTEROP_THREADS", "1").strip()

    def positive_int_or_default(raw_value, default):
        try:
            value = int(raw_value)
        except (TypeError, ValueError):
            return default
        return max(1, value)

    worker_threads = str(positive_int_or_default(worker_threads, 2))
    interop_threads = str(positive_int_or_default(interop_threads, 1))

    def cap_thread_env(key, limit):
        raw_value = os.environ.get(key)
        try:
            current = int(raw_value) if raw_value is not None else None
        except ValueError:
            current = None

        if current is None or current < 1 or current > int(limit):
            os.environ[key] = limit

    for key in (
        "OMP_NUM_THREADS",
        "MKL_NUM_THREADS",
        "OPENBLAS_NUM_THREADS",
        "NUMEXPR_NUM_THREADS",
        "VECLIB_MAXIMUM_THREADS",
        "BLIS_NUM_THREADS",
        "TORCH_NUM_THREADS",
    ):
        cap_thread_env(key, worker_threads)

    cap_thread_env("TORCH_NUM_INTEROP_THREADS", interop_threads)
    os.environ.setdefault("OMP_WAIT_POLICY", "PASSIVE")
    os.environ.setdefault("KMP_BLOCKTIME", "0")
    os.environ.setdefault("MKL_DYNAMIC", "TRUE")


_configure_cpu_budget_env()

import numpy as np
import torch


def _apply_torch_cpu_budget():
    def env_int(name, default):
        try:
            return max(1, int(os.environ.get(name, str(default))))
        except ValueError:
            return default

    worker_threads = env_int("TORCH_NUM_THREADS", 2)
    interop_threads = env_int("TORCH_NUM_INTEROP_THREADS", 1)

    try:
        torch.set_num_threads(worker_threads)
        torch.set_num_interop_threads(interop_threads)
    except RuntimeError as exc:
        logging.getLogger("pipe_inference").warning(
            "Could not apply torch CPU budget after startup: %s", exc
        )


_apply_torch_cpu_budget()

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

MAX_DIMENSION_OFFSET = 4.0
BLACKWELL_MIN_CUDA = (12, 8)


def _sanitize_dimension_offsets(raw_offsets):
    """Validate and clamp sparse per-dimension offsets from the UI."""
    if not raw_offsets:
        return []

    cleaned = []
    dropped = 0
    clamped = 0

    for item in raw_offsets:
        if not isinstance(item, (list, tuple)) or len(item) != 2:
            dropped += 1
            continue

        try:
            idx = int(item[0])
            value = float(item[1])
        except (TypeError, ValueError):
            dropped += 1
            continue

        if not math.isfinite(value):
            dropped += 1
            continue

        bounded = max(-MAX_DIMENSION_OFFSET, min(MAX_DIMENSION_OFFSET, value))
        if bounded != value:
            clamped += 1

        if abs(bounded) <= 1e-8:
            continue

        cleaned.append((idx, bounded))

    if dropped or clamped:
        log.warning(
            "Sanitized dimension offsets: kept=%d, clamped=%d, dropped=%d, limit=%.2f",
            len(cleaned), clamped, dropped, MAX_DIMENSION_OFFSET,
        )

    return cleaned


def _apply_dimension_offsets(tensor, dim_offsets):
    """Apply sparse additive offsets to the last tensor dimension."""
    if not dim_offsets:
        return

    last_dim = tensor.shape[-1]
    for idx, value in dim_offsets:
        if 0 <= idx < last_dim:
            tensor[:, :, idx] += value


def _parse_version_tuple(raw_value):
    """Parse '12.8' or '2.11.0.dev...' into a comparable integer tuple."""
    if not raw_value:
        return None

    parts = []
    for chunk in str(raw_value).split("."):
        digits = ""
        for char in chunk:
            if char.isdigit():
                digits += char
            else:
                break
        if not digits:
            break
        parts.append(int(digits))

    if not parts:
        return None
    if len(parts) == 1:
        parts.append(0)
    return tuple(parts[:2])


def _is_blackwell_capability(capability):
    """Treat sm_120+ GPUs as Blackwell-class devices."""
    return bool(capability) and capability[0] >= 12


def _validate_cuda_runtime_or_raise():
    """Fail closed when the bundled torch runtime is too old for the host GPU."""
    if os.environ.get("T5YNTH_ALLOW_INCOMPATIBLE_CUDA") == "1":
        return

    if not torch.cuda.is_available():
        return

    bundle_cuda_version = _parse_version_tuple(torch.version.cuda)
    incompatible_devices = []

    for device_index in range(torch.cuda.device_count()):
        capability = torch.cuda.get_device_capability(device_index)
        if not _is_blackwell_capability(capability):
            continue

        if bundle_cuda_version is None or bundle_cuda_version < BLACKWELL_MIN_CUDA:
            incompatible_devices.append(
                (
                    device_index,
                    torch.cuda.get_device_name(device_index),
                    f"sm_{capability[0]}{capability[1]}",
                )
            )

    if not incompatible_devices:
        return

    device_summary = ", ".join(
        f"GPU{device_index} {name} ({sm_name})"
        for device_index, name, sm_name in incompatible_devices
    )
    required_cuda = ".".join(str(part) for part in BLACKWELL_MIN_CUDA)
    raise RuntimeError(
        "Incompatible CUDA backend bundle: detected Blackwell GPU(s) "
        f"{device_summary}, but this backend was built with torch "
        f"{torch.__version__} / CUDA {torch.version.cuda or 'unknown'}. "
        f"Blackwell requires a bundle built with CUDA {required_cuda}+ "
        "(use the pinned Blackwell torch stack, not cu124)."
    )

# ─── Model loading ──────────────────────────────────────────────────

def _model_format(model_dir):
    """Detect model format. Returns 'diffusers', 'audioldm2', 'native', or None."""
    native_weights = (model_dir / "model.safetensors").is_file()
    model_index = model_dir / "model_index.json"
    if model_index.is_file():
        try:
            with open(model_index) as f:
                idx = json.load(f)
            if "AudioLDM2" in idx.get("_class_name", ""):
                return "audioldm2"
        except (json.JSONDecodeError, OSError):
            pass
        if native_weights and (model_dir / "model_config.json").is_file():
            return "native"
        return "diffusers"
    if native_weights and (model_dir / "model_config.json").is_file():
        return "native"
    return None

# {name: "diffusers"|"native"} — format of each discovered model
_model_formats = {}

def find_models():
    """Discover all model directories (diffusers or native). Returns {name: Path}."""
    import sys, os
    configured_dir = os.environ.get("T5YNTH_MODEL_DIR")
    if configured_dir:
        base_dirs = [Path(configured_dir)]
    else:
        base_dirs = [
            Path.home() / "Library" / "Application Support" / "T5ynth" / "models",  # per-user macOS (primary)
            Path.home() / "Library" / "T5ynth" / "models",       # legacy macOS
            Path.home() / ".config" / "share" / "T5ynth" / "models",  # Linux current app data path
            Path.home() / ".local" / "share" / "T5ynth" / "models",  # Linux
            Path.home() / "t5ynth" / "models",                    # legacy
        ]
        if sys.platform == "darwin":
            base_dirs.insert(1, Path("/Library/Application Support/T5ynth/models"))  # system-wide (.pkg, scan-only)
        elif sys.platform == "win32":
            base_dirs.insert(0, Path(os.environ.get("APPDATA", "")) / "T5ynth" / "models")  # per-user Windows (primary)
            base_dirs.insert(1, Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "T5ynth" / "models")  # system-wide (scan-only)
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
        _validate_cuda_runtime_or_raise()
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
    import importlib.machinery
    try:
        import pkg_resources  # noqa: F401
    except ImportError:
        import packaging

        pkg_resources = types.ModuleType('pkg_resources')
        pkg_resources.__spec__ = importlib.machinery.ModuleSpec('pkg_resources', None)
        pkg_resources.packaging = packaging
        sys.modules['pkg_resources'] = pkg_resources

    # Do not mock `dac`: the native stable-audio-open-small path imports
    # dac.nn / dac.model during model construction.
    if 'soundfile' not in sys.modules:
        soundfile = types.ModuleType('soundfile')
        soundfile.__spec__ = importlib.machinery.ModuleSpec('soundfile', None)

        def _soundfile_unavailable(*_args, **_kwargs):
            raise RuntimeError("soundfile I/O is not available in the inference subprocess")

        class _UnavailableSoundFile:
            def __init__(self, *_args, **_kwargs):
                _soundfile_unavailable()

        soundfile.read = _soundfile_unavailable
        soundfile.write = _soundfile_unavailable
        soundfile.SoundFile = _UnavailableSoundFile
        sys.modules['soundfile'] = soundfile

    mocks = ['skimage', 'skimage.transform', 'encodec', 'laion_clap',
             'pedalboard', 'pedalboard.io', 'pytorch_lightning', 'wandb',
             'v_diffusion_pytorch', 'gradio', 'jsonmerge', 'clean_fid', 'kornia']
    for name in mocks:
        if name not in sys.modules:
            m = types.ModuleType(name)
            m.__spec__ = importlib.machinery.ModuleSpec(name, None)
            if name == "jsonmerge":
                m.merge = lambda base, override, *args, **kwargs: override if override is not None else base
            sys.modules[name] = m


class NativePipeline:
    """Wrapper for stable-audio-tools native format models."""

    def __init__(self, model, model_config, device, model_name):
        self.model = model
        self.model_config = model_config
        self.device = device
        self.model_name = model_name
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

    # stable-audio-tools imports these via the top-level transformers package.
    # In the frozen bundle, pre-import them explicitly so the lazy export table
    # is populated before the conditioner constructs the T5 stack.
    import transformers
    from transformers.generation.utils import GenerationMixin
    from transformers.models.auto.tokenization_auto import AutoTokenizer
    from transformers.models.t5.modeling_t5 import T5EncoderModel

    # The frozen transformers package can fail to resolve some top-level lazy
    # exports even though the concrete implementation modules are present.
    # stable-audio-tools expects these names on the package root.
    transformers.GenerationMixin = GenerationMixin
    transformers.AutoTokenizer = AutoTokenizer
    transformers.T5EncoderModel = T5EncoderModel

    from stable_audio_tools.models.factory import create_model_from_config
    from stable_audio_tools.models.utils import load_ckpt_state_dict

    config_path = model_dir / "model_config.json"
    weights_path = model_dir / "model.safetensors"
    if not weights_path.is_file():
        raise RuntimeError(
            f"Native model at {model_dir} requires model.safetensors; "
            "checkpoint formats such as model.ckpt are not accepted."
        )

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
    return NativePipeline(model, model_config, device, model_dir.name)


def _native_conditioning_ids(pipe):
    """Return configured native conditioner ids for a stable-audio-tools model."""
    configs = pipe.model_config.get("model", {}).get("conditioning", {}).get("configs", [])
    return {cfg.get("id") for cfg in configs if cfg.get("id")}


def _build_native_conditioning_input(pipe, prompt, duration, start_pos=0.0):
    """Build one metadata dict matching the native model's configured conditioners."""
    payload = {"prompt": prompt}
    cond_ids = _native_conditioning_ids(pipe)

    if "seconds_start" in cond_ids or "seconds_total" in cond_ids:
        start_pos = max(0.0, min(1.0, float(start_pos)))
        virtual_total = duration / (1.0 - start_pos) if start_pos < 1.0 else duration
        if "seconds_start" in cond_ids:
            payload["seconds_start"] = start_pos * virtual_total
        if "seconds_total" in cond_ids:
            payload["seconds_total"] = virtual_total

    return payload


def load_default_model(model_name, devices):
    """Load the default model on the first working device. Returns that device."""
    failures = []
    for dev in devices:
        try:
            get_pipeline(model_name, dev)
            return dev
        except Exception as e:
            msg = f"{dev}: {e}"
            failures.append(msg)
            log.warning(f"Failed to load {model_name} on {msg}")
    failure_summary = "; ".join(failures) if failures else "no devices"
    raise RuntimeError(f"Could not load {model_name} on any device ({failure_summary})")


def startup_model_candidates(models):
    """Return startup candidates in preferred order."""
    preferred = [
        "stable-audio-open-1.0",
        "stable-audio-open-small",
    ]
    ordered = [name for name in preferred if name in models]
    ordered.extend(name for name in models.keys() if name not in ordered)
    return ordered


def choose_startup_model(models, devices):
    """Pick the first model that actually loads on at least one device."""
    failures = []
    for model_name in startup_model_candidates(models):
        try:
            default_device = load_default_model(model_name, devices)
            return model_name, default_device, failures
        except Exception as e:
            msg = f"{model_name}: {e}"
            failures.append(msg)
            log.warning(f"Startup model rejected: {msg}")
    failure_summary = "; ".join(failures) if failures else "no startup candidates"
    raise RuntimeError(f"No usable model could be loaded ({failure_summary})")


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


def _echo_through_null(other_emb, null_emb):
    """Spiegelung am Modell-Null: 2·null − other.

    When one prompt field is empty, the empty side's embedding becomes
    the reflection of the present side through the null/unconditional
    encoding (the model's encoding of ""). Standard interpolation
    (1-α)/2·A + (1+α)/2·B then traces a path from the present prompt
    through the model-neutral point to its tonal antipode, instead of
    collapsing to the present prompt for all α.
    """
    return 2.0 * null_emb - other_emb


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
    dim_offsets = _sanitize_dimension_offsets(request.get("dimension_offsets"))
    injection_mode = request.get("injection_mode", "linear")
    if injection_mode != "linear":
        raise ValueError(
            f"injection_mode='{injection_mode}' is only implemented on the "
            "native pipeline; AudioLDM2 only supports 'linear'."
        )

    if seed < 0:
        import random
        seed = random.randint(0, 2**31 - 1)

    device = wrapper.device
    generator = torch.Generator("cpu").manual_seed(seed)

    with torch.no_grad():
        # Symmetric prompt handling: encode whichever prompts are present.
        # encode_prompt with CFG=True returns [neg, pos] — the negative half
        # is always the encoding of "" and serves as both the CFG anchor and
        # the Spiegel-Punkt for echo derivation.
        a_present = bool(prompt_a)
        b_present = bool(prompt_b)

        if a_present:
            pe_a, mask_a, gpe_a = pipe.encode_prompt(
                prompt=prompt_a, device=device,
                num_waveforms_per_prompt=1,
                do_classifier_free_guidance=True,
                negative_prompt="",
            )
            neg_pe     = pe_a[0:1]
            neg_mask   = mask_a[0:1]
            neg_gpe    = gpe_a[0:1]
            pos_pe_a   = pe_a[1:2]
            pos_mask_a = mask_a[1:2]
            pos_gpe_a  = gpe_a[1:2]

        if b_present:
            pe_b, mask_b, gpe_b = pipe.encode_prompt(
                prompt=prompt_b, device=device,
                num_waveforms_per_prompt=1,
                do_classifier_free_guidance=True,
                negative_prompt="",
            )
            if not a_present:
                neg_pe   = pe_b[0:1]
                neg_mask = mask_b[0:1]
                neg_gpe  = gpe_b[0:1]
            pos_pe_b   = pe_b[1:2]
            pos_mask_b = mask_b[1:2]
            pos_gpe_b  = gpe_b[1:2]

        if not a_present and not b_present:
            pe_e, mask_e, gpe_e = pipe.encode_prompt(
                prompt="", device=device,
                num_waveforms_per_prompt=1,
                do_classifier_free_guidance=True,
                negative_prompt="",
            )
            neg_pe     = pe_e[0:1]
            neg_mask   = mask_e[0:1]
            neg_gpe    = gpe_e[0:1]
            pos_pe_a   = neg_pe.clone()
            pos_pe_b   = neg_pe.clone()
            pos_mask_a = neg_mask
            pos_mask_b = neg_mask
            pos_gpe_a  = neg_gpe.clone()
            pos_gpe_b  = neg_gpe.clone()

        # Echoraum: a leeres Feld is the reflection of the other prompt
        # through neg_pe (the empty-string encoding).
        if a_present and not b_present:
            pos_pe_b   = _echo_through_null(pos_pe_a, neg_pe)
            pos_mask_b = pos_mask_a
            pos_gpe_b  = _echo_through_null(pos_gpe_a, neg_gpe)
        elif b_present and not a_present:
            pos_pe_a   = _echo_through_null(pos_pe_b, neg_pe)
            pos_mask_a = pos_mask_b
            pos_gpe_a  = _echo_through_null(pos_gpe_b, neg_gpe)

        # Pad prompt_embeds to same seq_len (only needed when both prompts
        # were independently encoded — echoes inherit the source's shape).
        if a_present and b_present:
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

        # Magnitude scaling
        if abs(magnitude - 1.0) > 1e-6:
            manipulated_pe = manipulated_pe * magnitude

        # Noise injection (numpy PCG64 for cross-platform determinism)
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(manipulated_pe.shape).astype(np.float32)
            noise = torch.from_numpy(noise_np).to(manipulated_pe.device) * noise_sigma
            manipulated_pe = manipulated_pe + noise

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

        # Dimension offsets from DimensionExplorer are always applied last.
        _apply_dimension_offsets(manipulated_pe, dim_offsets)

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

    Optional research-mode field "injection_mode" selects how prompt B is mixed
    into prompt A. Default "linear" reproduces the historical crossfade exactly.
    """
    from stable_audio_tools.inference.generation import generate_diffusion_cond

    prompt_a = request.get("prompt_a", "")
    prompt_b = request.get("prompt_b", "")
    alpha = request.get("alpha", 0.0)
    magnitude = request.get("magnitude", 1.0)
    noise_sigma = request.get("noise_sigma", 0.0)
    duration = request.get("duration", 3.0)
    start_pos = request.get("start_pos", 0.0)
    steps = max(1, int(request.get("steps", 8)))
    cfg_scale = request.get("cfg_scale", 4.0)
    seed = request.get("seed", -1)
    dim_offsets = _sanitize_dimension_offsets(request.get("dimension_offsets"))
    injection_mode = request.get("injection_mode", "linear")
    injection_transition_at = max(
        0.05, min(0.95, float(request.get("injection_transition_at", 0.6)))
    )
    # late_phase_alpha drives the late-phase blend formula:
    #   late_blend = (0.5 - 0.5α)·A + (0.5 + 0.5α)·B
    # 0  → 50/50 mix  (subtle texture mixing)
    # +1 → pure B      (full A→B at the transition step)
    # The plugin couples this with transition_at so the Fine slider's right
    # end produces effectively pure B (very early transition + pure-B late).
    late_phase_alpha = max(-1.0, min(1.0, float(request.get("late_phase_alpha", 0.0))))
    # Layer mode: two-thumb range slider on the plugin defines the B-zone
    # [split_start, split_end] across the 16 DiT blocks. Per-block weight is
    # a sigmoid product (smooth top-hat) so both edges have ±2-layer ramps.
    split_start = max(0.0, min(16.0, float(request.get("split_start", 4.0))))
    split_end   = max(0.0, min(16.0, float(request.get("split_end",  16.0))))
    layer_split_smoothness = 2.0  # sigmoid scale; ±2 layers ramp at each edge
    if injection_mode not in ("linear", "delta", "late_step", "layer_split",
                              "kombi1", "kombi2", "kombi3"):
        raise ValueError(
            f"Unknown injection_mode '{injection_mode}'. "
            "Expected one of: linear, delta, late_step, layer_split, kombi1, kombi2, kombi3."
        )

    # Kombi 1/2/3: late-step blend × layer-split, with hardcoded layer range.
    # Frontend sends the same range; backend re-asserts it as a safety net so
    # the per-mode geometry can't drift via stale presets or drift overrides.
    if injection_mode == "kombi1":
        split_start, split_end = 0.0, 4.0   # surface (low layers)
    elif injection_mode == "kombi2":
        split_start, split_end = 4.0, 12.0  # broad mid
    elif injection_mode == "kombi3":
        split_start, split_end = 6.0, 10.0  # narrow center

    if seed < 0:
        import random
        seed = random.randint(0, 2**31 - 1)

    sr = pipe.sample_rate
    device = pipe.device
    effective_steps = steps
    if getattr(pipe.model, "diffusion_objective", None) == "v" and effective_steps < 2:
        log.warning(
            "Native %s requested with %d step; clamping to 2 because the k-diffusion sampler "
            "is unstable below 2 steps",
            pipe.model_name,
            effective_steps,
        )
        effective_steps = 2

    # ── Encode prompts via the model's built-in T5 conditioner ──
    # Then manipulate embeddings (alpha, magnitude, noise, dim offsets)
    # and pass as pre-computed conditioning_tensors.
    with torch.no_grad():
        a_present = bool(prompt_a)
        b_present = bool(prompt_b)

        # Always encode "" for the Spiegel-Punkt and as conditioning carrier
        # (for timing fields like seconds_start/seconds_total when neither
        # prompt is present).
        cond_null = pipe.model.conditioner(
            [_build_native_conditioning_input(pipe, "", duration, start_pos)],
            device,
        )
        null_pe = cond_null["prompt"][0]
        null_mask = cond_null["prompt"][1]

        if a_present:
            cond_a = pipe.model.conditioner(
                [_build_native_conditioning_input(pipe, prompt_a, duration, start_pos)],
                device,
            )
            prompt_emb_a = cond_a["prompt"][0]   # [1, seq, 768]
            mask_a = cond_a["prompt"][1]
        else:
            # Carry timing/duration conditioning from cond_null. Deep-copy so
            # the later cond_a["prompt"] = [...] assignment doesn't corrupt
            # cond_null in case anything else holds a reference.
            cond_a = copy.deepcopy(cond_null)
            prompt_emb_a = null_pe.clone()
            mask_a = null_mask if null_mask is None else null_mask.clone()

        if b_present:
            cond_b = pipe.model.conditioner(
                [_build_native_conditioning_input(pipe, prompt_b, duration, start_pos)],
                device,
            )
            prompt_emb_b = cond_b["prompt"][0]
            mask_b = cond_b["prompt"][1]
        else:
            prompt_emb_b = null_pe.clone()
            mask_b = null_mask

        # Echoraum: a leeres Feld is the reflection of the other prompt
        # through null_pe (encoding of "").
        if a_present and not b_present:
            prompt_emb_b = _echo_through_null(prompt_emb_a, null_pe)
            mask_b = mask_a
        elif b_present and not a_present:
            prompt_emb_a = _echo_through_null(prompt_emb_b, null_pe)
            mask_a = mask_b

        # Mean-pool for DimensionExplorer stats (post-echo, pre-manipulation)
        emb_a_pooled = prompt_emb_a.squeeze(0).mean(dim=0).cpu().float().numpy()
        emb_b_pooled = prompt_emb_b.squeeze(0).mean(dim=0).cpu().float().numpy()

        combined_mask = None
        if mask_a is not None and mask_b is not None:
            combined_mask = (mask_a | mask_b)
        elif mask_a is not None:
            combined_mask = mask_a
        else:
            combined_mask = mask_b

        # ── Embedding-level manipulators ──
        # Magnitude / Noise / Semantic Axes / Dimension Offsets all act
        # additively or multiplicatively on a 768-D embedding tensor. To make
        # them work uniformly across all injection modes (Linear feeds a
        # single blend tensor into the DiT; the non-linear modes feed two
        # tensors — early/late or A/B per block — that the per-block hooks
        # combine), we apply the SAME shifts to every tensor that flows
        # into the model. Because the manipulations are linear, applying
        # them independently to A and B and then blending is equivalent to
        # blending first and then manipulating (modulo the renorm step that
        # only fires when |alpha| > 1, which is Linear-specific).
        sem_axes = request.get("semantic_axes")

        def native_encode(text):
            cond = pipe.model.conditioner(
                [_build_native_conditioning_input(pipe, text, 1.0, 0.0)],
                device,
            )
            return cond["prompt"][0], None

        # Noise tensor is drawn ONCE from the seed-derived PCG64 RNG and
        # reused across every embedding path so the per-mode geometry
        # (e.g. blends, layer mixes) keeps the Linear-mode invariant of
        # "noise added once, not per term".
        _noise_tensor = None
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(prompt_emb_a.shape).astype(np.float32)
            _noise_tensor = torch.from_numpy(noise_np).to(prompt_emb_a.device) * noise_sigma

        def apply_pre_offsets(emb):
            """Magnitude + noise + semantic axes — i.e. all manipulations
            EXCEPT DimensionExplorer offsets. Returns a new tensor; the
            input is not mutated. Mask is NOT applied here."""
            out = emb
            if abs(magnitude - 1.0) > 1e-6:
                out = out * magnitude
            if _noise_tensor is not None:
                out = out + _noise_tensor
            if sem_axes:
                out = _apply_semantic_axes(out, sem_axes, native_encode, pipe.model_name)
            return out

        def apply_dim_offsets(emb):
            """DimensionExplorer offsets, applied in-place on a clone so the
            caller's tensor is preserved (multiple paths may share storage
            with the raw prompt embedding)."""
            if not dim_offsets:
                return emb
            out = emb.clone()
            _apply_dimension_offsets(out, dim_offsets)
            return out

        def apply_all(emb):
            return apply_dim_offsets(apply_pre_offsets(emb))

        # ── Injection mode dispatch ──
        # "linear"     : historical crossfade (default; byte-identical to v1.5).
        # "delta"      : A + alpha · (B − null), preserves A more strongly.
        # "late_step"  : early sampler steps see A only; after the transition
        #                point the DiTWrapper.forward swap injects late_blend.
        # "layer_split": every DiT block sees a different A/B blend, sigmoid-
        #                ramped over the [split_start, split_end] B-zone.
        # "kombi1/2/3" : step phase × layer band, hard-mask layer geometry.
        # Manipulations are applied to whichever tensor(s) actually flow
        # into the cross-attention layer for the active mode.
        late_step_payload = None
        layer_split_payload = None  # (proj_a, proj_b, proj_null, blocks)
        kombi_payload = None        # (proj_a, proj_late, proj_null, blocks)
        baseline_pooled = None      # set per mode for the GUI explorer feedback
        if injection_mode == "linear":
            manipulated = (0.5 - 0.5 * alpha) * prompt_emb_a + (0.5 + 0.5 * alpha) * prompt_emb_b
            if alpha < -1.0 or alpha > 1.0:
                ref_norm = prompt_emb_a.norm() if alpha < 0.0 else prompt_emb_b.norm()
                res_norm = manipulated.norm()
                if res_norm > 1e-8:
                    manipulated = manipulated * (ref_norm / res_norm)
            # Inline manipulation chain so baseline_pooled snapshots the
            # post-axes-pre-DimExplorer state (preserves v1.5.x byte-identity
            # AND the GUI explorer's "this is what the model sees before
            # your dim offsets" reference contract).
            manipulated = apply_pre_offsets(manipulated)
            baseline = manipulated if combined_mask is None \
                else manipulated * combined_mask.unsqueeze(-1).float()
            baseline_pooled = baseline.squeeze(0).mean(dim=0).cpu().float().numpy()
            manipulated = apply_dim_offsets(manipulated)
        elif injection_mode == "delta":
            manipulated = prompt_emb_a + alpha * (prompt_emb_b - null_pe)
            manipulated = apply_pre_offsets(manipulated)
            baseline = manipulated if combined_mask is None \
                else manipulated * combined_mask.unsqueeze(-1).float()
            baseline_pooled = baseline.squeeze(0).mean(dim=0).cpu().float().numpy()
            manipulated = apply_dim_offsets(manipulated)
        elif injection_mode == "late_step":
            # Late-phase blend driven by late_phase_alpha (set by the Fine slider).
            # α=0 → 50/50 mix (subtle); α=+1 → pure B (slider's right end).
            # Both the early A-only path AND the late blend get the same
            # manipulations so semantic axes / noise / magnitude / dim offsets
            # shape both phases consistently.
            la = late_phase_alpha
            late_blend = (0.5 - 0.5 * la) * prompt_emb_a + (0.5 + 0.5 * la) * prompt_emb_b
            manipulated = apply_all(prompt_emb_a)         # early-phase conditioning
            late_emb_manip = apply_all(late_blend)        # late-phase conditioning
            if combined_mask is not None:
                late_emb_zeroed = late_emb_manip * combined_mask.unsqueeze(-1).float()
            else:
                late_emb_zeroed = late_emb_manip
            late_step_payload = (late_emb_zeroed, combined_mask)
            baseline_pooled = manipulated.squeeze(0).mean(dim=0).cpu().float().numpy()
        elif injection_mode == "layer_split":
            # Per-block context override via forward_pre_hook on each
            # block.cross_attn. Pre-project A, B, null through to_cond_embed
            # so the hook bypasses the DiT's projection step cleanly.
            # Manipulate A and B independently (with the same shifts) BEFORE
            # projection so the per-block sigmoid blend (1−w)·A + w·B inherits
            # magnitude / noise / axes / dim offsets uniformly.
            emb_a_manip = apply_all(prompt_emb_a)
            emb_b_manip = apply_all(prompt_emb_b)
            if combined_mask is not None:
                emb_a_masked = emb_a_manip * combined_mask.unsqueeze(-1).float()
                emb_b_masked = emb_b_manip * combined_mask.unsqueeze(-1).float()
            else:
                emb_a_masked = emb_a_manip
                emb_b_masked = emb_b_manip
            dit = pipe.model.model.model  # DiffusionTransformer
            param_dtype = next(dit.to_cond_embed.parameters()).dtype
            with torch.no_grad():
                proj_a    = dit.to_cond_embed(emb_a_masked.to(param_dtype))
                proj_b    = dit.to_cond_embed(emb_b_masked.to(param_dtype))
                # CFG-uncond half: the model uses `torch.zeros_like(cross_attn_cond)`
                # AFTER `to_cond_embed` (dit.py:141 projects, then dit.py:346 builds
                # null_embed at the projected level). Match that exactly — projecting
                # an encoded "" string would NOT match what the unhooked path does.
                proj_null = torch.zeros_like(proj_a)
            blocks = dit.transformer.layers
            layer_split_payload = (proj_a, proj_b, proj_null, blocks)
            manipulated = emb_a_manip  # un-hooked fallback / global cond path
            baseline_pooled = manipulated.squeeze(0).mean(dim=0).cpu().float().numpy()
        else:  # kombi1 / kombi2 / kombi3 — late-step × layer-split.
            # Kombi semantics: early sampler steps see pure A on every block.
            # After the transition step, each block in the per-Kombi
            # hardcoded layer band sees the late_blend; blocks outside the
            # band still see A (hard mask). Same manipulations applied to
            # both the A path and the late_blend path before projection.
            la = late_phase_alpha
            late_blend = (0.5 - 0.5 * la) * prompt_emb_a + (0.5 + 0.5 * la) * prompt_emb_b
            emb_a_manip = apply_all(prompt_emb_a)
            late_blend_manip = apply_all(late_blend)
            if combined_mask is not None:
                emb_a_masked = emb_a_manip * combined_mask.unsqueeze(-1).float()
                late_blend_masked = late_blend_manip * combined_mask.unsqueeze(-1).float()
            else:
                emb_a_masked = emb_a_manip
                late_blend_masked = late_blend_manip
            dit = pipe.model.model.model
            param_dtype = next(dit.to_cond_embed.parameters()).dtype
            with torch.no_grad():
                proj_a    = dit.to_cond_embed(emb_a_masked.to(param_dtype))
                proj_late = dit.to_cond_embed(late_blend_masked.to(param_dtype))
                proj_null = torch.zeros_like(proj_a)
            blocks = dit.transformer.layers
            kombi_payload = (proj_a, proj_late, proj_null, blocks)
            manipulated = emb_a_manip  # un-hooked fallback / global cond path
            baseline_pooled = manipulated.squeeze(0).mean(dim=0).cpu().float().numpy()

        # Re-apply attention mask to zero out padding (native conditioner zeros
        # padding, but manipulations like noise/offsets/axes can pollute it;
        # the DiT disables cross_attn_cond_mask so padding must stay zero).
        if combined_mask is not None:
            manipulated = manipulated * combined_mask.unsqueeze(-1).float()

        # Replace prompt embedding in conditioning tensors
        cond_a["prompt"] = [manipulated, combined_mask]

    offsets_str = f", {len(dim_offsets)} offsets" if dim_offsets else ""
    mode_str = f", mode={injection_mode}" if injection_mode != "linear" else ""
    log.info(f"Generating (native) on {device}: '{prompt_a[:60]}' ({duration}s, {effective_steps} steps, "
             f"CFG={cfg_scale}, mag={magnitude}, noise={noise_sigma}, seed={seed}{offsets_str}{mode_str})")
    t0 = time.time()

    # late_step: install a forward swap on DiTWrapper so the cross-attn
    # conditioning flips from "early" (= manipulated stored in cond_a) to
    # "late" (= late_step_payload[0]) after a fraction of the sampler steps.
    # ConditionedDiffusionModelWrapper.forward (diffusion.py:216) passes
    # cross_attn_cond / cross_attn_mask as kwargs to DiTWrapper, so kwargs
    # swap suffices. DiTWrapper.forward asserts batch_cfg=True, so the hook
    # fires exactly once per sampler step.
    restore_forward = None
    if injection_mode == "late_step" and late_step_payload is not None:
        late_emb_zeroed, late_mask = late_step_payload
        transition_step = max(1, int(round(effective_steps * injection_transition_at)))
        dit_wrapper = pipe.model.model
        original_forward = dit_wrapper.forward
        state = {"calls": 0}

        def patched_forward(x, t, **kwargs):
            if state["calls"] >= transition_step:
                kwargs["cross_attn_cond"] = late_emb_zeroed
                # Use DiTWrapper's parameter name `cross_attn_mask` (NOT the
                # inner DiffusionTransformer's `cross_attn_cond_mask`).
                # DiTWrapper.forward takes `cross_attn_mask=...` and internally
                # forwards it as `cross_attn_cond_mask` to the inner DiT
                # (diffusion.py:547). Setting `cross_attn_cond_mask` here would
                # collide with that forwarding and TypeError-out.
                kwargs["cross_attn_mask"] = late_mask
            state["calls"] += 1
            return original_forward(x, t, **kwargs)

        dit_wrapper.forward = patched_forward

        def restore_forward():
            if dit_wrapper.forward is patched_forward:
                del dit_wrapper.forward

    elif injection_mode == "layer_split" and layer_split_payload is not None:
        # Register a forward_pre_hook on every block.cross_attn that overrides
        # the `context` kwarg with a per-block A↔B blend. Per-layer weight is
        # the product of two sigmoids → smooth top-hat over [split_start, split_end].
        proj_a, proj_b, proj_null, blocks = layer_split_payload
        num_layers = len(blocks)
        # Special cases for clean limits at the slider extremes:
        # • Both thumbs at the outer edges → full B (avoids the 0.5-edge artifact
        #   of the sigmoid product).
        # • Empty range (start ≥ end) → no B at all (pure A pass-through).
        full_b = (split_start <= 0.5 and split_end >= 15.5)
        empty  = (split_start >= split_end)
        per_block_ctx = []  # one tensor per block; CFG-doubling at hook fire time
        for i in range(num_layers):
            if full_b:
                w = 1.0
            elif empty:
                w = 0.0
            else:
                w_left  = 1.0 / (1.0 + math.exp(-(i - split_start) / layer_split_smoothness))
                w_right = 1.0 / (1.0 + math.exp(-(split_end - i)   / layer_split_smoothness))
                w = w_left * w_right
            blended = (1.0 - w) * proj_a + w * proj_b
            per_block_ctx.append(blended)

        hook_handles = []
        for i, block in enumerate(blocks):
            ctx_cond = per_block_ctx[i]            # [B, seq, embed]
            ctx_uncond = proj_null                  # [B_null, seq, embed]

            def make_hook(ctx_cond_local, ctx_uncond_local):
                def pre_hook(module, args, kwargs):
                    # The DiT calls block.cross_attn(x, context=context, ...).
                    # When CFG is active the runtime context has batch=2*B
                    # (cond half + uncond half). We reconstruct the same
                    # batching from our pre-projected per-block tensors,
                    # repeating to match the runtime context's batch dim if
                    # needed (covers cfg=1 and cfg>1 uniformly).
                    runtime_ctx = kwargs.get("context")
                    if runtime_ctx is None and len(args) >= 2:
                        runtime_ctx = args[1]
                    if runtime_ctx is None:
                        return None  # nothing to override
                    b_runtime = runtime_ctx.shape[0]
                    b_cond = ctx_cond_local.shape[0]
                    if b_runtime == b_cond:
                        new_ctx = ctx_cond_local
                    elif b_runtime == 2 * b_cond:
                        new_ctx = torch.cat([ctx_cond_local, ctx_uncond_local], dim=0)
                    else:
                        # Unexpected batching — let the original through.
                        return None
                    new_ctx = new_ctx.to(runtime_ctx.dtype).to(runtime_ctx.device)
                    new_kwargs = dict(kwargs)
                    new_kwargs["context"] = new_ctx
                    return args, new_kwargs
                return pre_hook

            handle = block.cross_attn.register_forward_pre_hook(
                make_hook(ctx_cond, ctx_uncond), with_kwargs=True
            )
            hook_handles.append(handle)

        def restore_forward():
            for h in hook_handles:
                h.remove()

    elif injection_mode in ("kombi1", "kombi2", "kombi3") and kombi_payload is not None:
        # Kombi: late-step × layer-split. Per-block hooks override cross_attn
        # context every step, but switch between an "early" and "late" tensor
        # based on a sampler-step counter ticked by a DiTWrapper.forward patch.
        #   early: pure A on every block (uniform across layers).
        #   late : per-layer (1-w)·A + w·late_blend, sigmoid top-hat over the
        #          per-Kombi hardcoded [split_start, split_end] range.
        proj_a, proj_late, proj_null, blocks = kombi_payload
        num_layers = len(blocks)
        # Hard layer mask (no sigmoid softening): w = 1 inside [start, end),
        # 0 outside. Different from layer_split's sigmoid top-hat, which
        # softens edges to ~0.5 at the boundary. The Kombis are preset
        # geometry — the user's intensity slider controls the late-phase
        # blend, NOT the band shape — so a hard mask makes "slider=1" mean
        # "100% late_blend on every block in the band", matching the user's
        # mental model of a preset's full strength.
        late_per_block = []
        for i in range(num_layers):
            w = 1.0 if (i >= split_start and i < split_end) else 0.0
            late_per_block.append((1.0 - w) * proj_a + w * proj_late)

        transition_step = max(1, int(round(effective_steps * injection_transition_at)))
        state = {"calls": 0}

        # Patch DiTWrapper.forward to tick the step counter once per sampler
        # step (it's the unique entry-point per step; the inner DiT calls all
        # 16 blocks within a single forward). Increment AFTER original_forward
        # so all hooks within one step see the same `calls` value.
        dit_wrapper = pipe.model.model
        original_forward = dit_wrapper.forward

        def patched_forward(x, t, **kwargs):
            result = original_forward(x, t, **kwargs)
            state["calls"] += 1
            return result

        dit_wrapper.forward = patched_forward

        hook_handles = []
        for i, block in enumerate(blocks):
            ctx_early_local  = proj_a
            ctx_late_local   = late_per_block[i]
            ctx_uncond_local = proj_null

            def make_hook(early, late, uncond):
                def pre_hook(module, args, kwargs):
                    runtime_ctx = kwargs.get("context")
                    if runtime_ctx is None and len(args) >= 2:
                        runtime_ctx = args[1]
                    if runtime_ctx is None:
                        return None
                    ctx_cond = late if state["calls"] >= transition_step else early
                    b_runtime = runtime_ctx.shape[0]
                    b_cond = ctx_cond.shape[0]
                    if b_runtime == b_cond:
                        new_ctx = ctx_cond
                    elif b_runtime == 2 * b_cond:
                        new_ctx = torch.cat([ctx_cond, uncond], dim=0)
                    else:
                        return None
                    new_ctx = new_ctx.to(runtime_ctx.dtype).to(runtime_ctx.device)
                    new_kwargs = dict(kwargs)
                    new_kwargs["context"] = new_ctx
                    return args, new_kwargs
                return pre_hook

            handle = block.cross_attn.register_forward_pre_hook(
                make_hook(ctx_early_local, ctx_late_local, ctx_uncond_local),
                with_kwargs=True,
            )
            hook_handles.append(handle)

        def restore_forward():
            for h in hook_handles:
                h.remove()
            if dit_wrapper.forward is patched_forward:
                del dit_wrapper.forward

    # stable_audio_tools prints seed + tqdm progress to stdout, which would
    # corrupt the binary IPC pipe to JUCE. Redirect stdout → stderr temporarily.
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        output = generate_diffusion_cond(
            pipe.model,
            steps=effective_steps,
            cfg_scale=cfg_scale,
            conditioning_tensors=cond_a,
            sample_size=pipe.sample_size,
            device=device,
            seed=seed,
        )
    finally:
        sys.stdout = real_stdout
        if restore_forward is not None:
            restore_forward()

    elapsed = time.time() - t0
    audio_np = output.squeeze(0).cpu().float().numpy()  # [channels, samples]
    requested_samples = int(math.ceil(duration * sr))
    if audio_np.shape[-1] > requested_samples:
        audio_np = audio_np[..., :requested_samples]

    rms = float(np.sqrt(np.mean(audio_np ** 2)))
    peak = float(np.max(np.abs(audio_np)))
    log.info(f"Generated (native) in {elapsed:.1f}s, RMS={rms:.4f}, peak={peak:.4f}, shape={audio_np.shape}")
    emb_stats = (emb_a_pooled, emb_b_pooled, baseline_pooled)
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
    dim_offsets = _sanitize_dimension_offsets(request.get("dimension_offsets"))  # optional: [[idx, val], ...]
    injection_mode = request.get("injection_mode", "linear")
    if injection_mode != "linear":
        raise ValueError(
            f"injection_mode='{injection_mode}' is only implemented on the "
            "native pipeline (e.g. stable-audio-open-small); diffusers SAO "
            "currently only supports 'linear'."
        )

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

        a_present = bool(prompt_a)
        b_present = bool(prompt_b)

        # Always encode "" for the Spiegel-Punkt (used only for echo
        # derivation; CFG neg_embeds stays as torch.zeros_like below,
        # unchanged from the original behavior to keep CFG-driven sound
        # identical when both prompts are present).
        null_pe, null_mask = encode_text("")

        if a_present:
            emb_a, mask_a = encode_text(prompt_a)
        else:
            emb_a = null_pe.clone()
            mask_a = null_mask

        if b_present:
            emb_b, mask_b = encode_text(prompt_b)
        else:
            emb_b = null_pe.clone()
            mask_b = null_mask

        # Echoraum: a leeres Feld is the reflection of the other prompt
        # through null_pe (encoding of "").
        if a_present and not b_present:
            emb_b = _echo_through_null(emb_a, null_pe)
            mask_b = mask_a
        elif b_present and not a_present:
            emb_a = _echo_through_null(emb_b, null_pe)
            mask_a = mask_b

        # Mean-pool for DimensionExplorer stats (post-echo, pre-manipulation)
        emb_a_pooled = _mean_pool(emb_a, mask_a)
        emb_b_pooled = _mean_pool(emb_b, mask_b)

        combined_mask = (mask_a | mask_b)
        # alpha: 0 = midpoint, -1 = pure A, +1 = pure B
        manipulated = (0.5 - 0.5 * alpha) * emb_a + (0.5 + 0.5 * alpha) * emb_b
        # Renormalize if extrapolating (|alpha| > 1)
        if alpha < -1.0 or alpha > 1.0:
            ref_norm = emb_a.norm() if alpha < 0.0 else emb_b.norm()
            res_norm = manipulated.norm()
            if res_norm > 1e-8:
                manipulated = manipulated * (ref_norm / res_norm)

        # Magnitude scaling
        if abs(magnitude - 1.0) > 1e-6:
            manipulated = manipulated * magnitude

        # Noise injection (numpy PCG64 for cross-platform determinism)
        if noise_sigma > 0.0:
            rng = np.random.Generator(np.random.PCG64(seed))
            noise_np = rng.standard_normal(manipulated.shape).astype(np.float32)
            noise = torch.from_numpy(noise_np).to(manipulated.device) * noise_sigma
            manipulated = manipulated + noise

        # Apply semantic axes
        sem_axes = request.get("semantic_axes")
        if sem_axes:
            manipulated = _apply_semantic_axes(manipulated, sem_axes, encode_text, "diffusers")

        baseline_pooled = _mean_pool(manipulated, combined_mask)

        # Apply dimension offsets from DimensionExplorer last so they cannot be
        # overwritten by later conditioning operations.
        _apply_dimension_offsets(manipulated, dim_offsets)

        # Generate via pipeline with pre-computed embeddings
        neg_embeds = torch.zeros_like(manipulated)
        neg_mask = torch.ones_like(combined_mask)

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
            attention_mask=combined_mask,
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

        emb_stats = (emb_a_pooled, emb_b_pooled, baseline_pooled)
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

    If emb_stats is (emb_a[num_dims], emb_b[num_dims], baseline[num_dims]), appends:
        uint16(num_dims) + float32[num_dims] emb_a
                         + float32[num_dims] emb_b
                         + float32[num_dims] baseline
    """
    channels, samples = audio_np.shape
    pcm = audio_np.astype(np.float32).tobytes()
    header = struct.pack('<iiiiif', 1, samples, channels, sr, seed, elapsed_ms * 1000)
    sys.stdout.buffer.write(b'\x01')
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(pcm)
    if emb_stats is not None:
        emb_a, emb_b, baseline = emb_stats
        num_dims = len(emb_a)
        sys.stdout.buffer.write(struct.pack('<H', num_dims))
        sys.stdout.buffer.write(emb_a.astype(np.float32).tobytes())
        sys.stdout.buffer.write(emb_b.astype(np.float32).tobytes())
        sys.stdout.buffer.write(baseline.astype(np.float32).tobytes())
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
        if os.environ.get("T5YNTH_ALLOW_NO_MODELS") == "1":
            devices = available_devices()
            default_device = devices[0] if devices else "cpu"
            default_model = ""
            send_ready(devices, default_device, _available_models, "")
            log.warning("No model directories found; continuing in smoke-test mode")
        else:
            send_error("No model directories found")
            return
    else:
        try:
            devices = available_devices()
            default_model, default_device, startup_failures = choose_startup_model(_available_models, devices)
        except Exception as e:
            log.error(f"Failed to load default model: {e}")
            send_error(f"Pipeline load failed: {e}")
            return

        send_ready(devices, default_device, _available_models, default_model)
        log.info(f"Ready. Models: {list(_available_models.keys())}, default: {default_model}, "
                 f"devices: {devices}, default device: {default_device}")
        if startup_failures:
            log.warning(f"Skipped unusable startup models: {startup_failures}")

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
                if isinstance(pipe, AudioLDM2Wrapper) or not hasattr(pipe, "vae"):
                    raise ValueError("Latent interpolation is only supported for diffusers Stable Audio pipelines")
                audio, sr, seed, elapsed = interpolate_and_decode(pipe, request)
            elif mode == "decode_cached":
                if isinstance(pipe, AudioLDM2Wrapper) or not hasattr(pipe, "vae"):
                    raise ValueError("Latent cache decode is only supported for diffusers Stable Audio pipelines")
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
