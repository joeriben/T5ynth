#!/usr/bin/env python3
"""T5ynth — Stable Audio 3 backend (second inference process).

Standalone IPC server for the Stable Audio 3 family (currently
``stable-audio-3-small-music``). Speaks the EXACT same binary stdin/stdout
protocol as ``pipe_inference.py`` (see docs/IPC_PROTOCOL.md), but runs on a
DIFFERENT, mutually-incompatible dependency set:

    torch/torchaudio 2.7.1 · transformers >=4.53 (t5gemma) ·
    stable-audio-tools (git main: T5GemmaConditioner, diffusion_cond_inpaint,
    native pingpong, APG) · pytorch-lightning · PyWavelets>=1.6

Because that stack cannot coexist in one interpreter with the lean
``pipe_inference.py`` (transformers/stable-audio-tools 0.0.19) stack, SA3 is a
SECOND frozen backend. The C++ client picks the process by model id.

This file intentionally re-implements the small protocol-framing helpers
(send_ready / send_audio / send_error) rather than importing them, so the two
backends stay independent and either can be built without the other.
"""
import os
import sys
import json
import time
import math
import struct
import random
import logging
from pathlib import Path

# ─── Logging → stderr ONLY. stdout is the binary pipe. ───────────────
logging.basicConfig(level=logging.INFO, stream=sys.stderr,
                    format="%(asctime)s [pipe_inference_sa3] %(message)s")
log = logging.getLogger("pipe_inference_sa3")

_SA3_CONDITIONER_TYPE = "t5gemma"   # marker that a model dir is an SA3 model


# ─── Model discovery (mirrors pipe_inference.py) ─────────────────────

def _model_search_base_dirs():
    configured = os.environ.get("T5YNTH_MODEL_DIR")
    if configured:
        return [Path(configured)]
    base = [
        Path.home() / "Library" / "Application Support" / "T5ynth" / "models",
        Path.home() / "Library" / "T5ynth" / "models",
        Path.home() / ".config" / "share" / "T5ynth" / "models",
        Path.home() / ".local" / "share" / "T5ynth" / "models",
        Path.home() / "t5ynth" / "models",
    ]
    if sys.platform == "darwin":
        base.insert(1, Path("/Library/Application Support/T5ynth/models"))
    elif sys.platform == "win32":
        base.insert(0, Path(os.environ.get("APPDATA", "")) / "T5ynth" / "models")
        base.insert(1, Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "T5ynth" / "models")
    return base


def _read_model_config(model_dir: Path):
    cfg_path = model_dir / "model_config.json"
    if not cfg_path.is_file():
        return None
    try:
        with open(cfg_path) as f:
            return json.load(f)
    except Exception:
        return None


def _conditioning_configs(model_config):
    cond = (model_config.get("model", {}).get("conditioning")
            or model_config.get("conditioning") or {})
    return cond.get("configs", [])


def _is_sa3_model_dir(model_dir: Path):
    """True iff dir holds a stable-audio-tools model whose text conditioner is t5gemma."""
    if not (model_dir / "model.safetensors").is_file():
        return False
    cfg = _read_model_config(model_dir)
    if not cfg:
        return False
    return any(c.get("type") == _SA3_CONDITIONER_TYPE for c in _conditioning_configs(cfg))


def find_models():
    """Discover SA3 model directories only. Returns {name: Path}."""
    models = {}
    for base in _model_search_base_dirs():
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            if child.is_dir() and child.name not in models and _is_sa3_model_dir(child):
                models[child.name] = child
    return models


# ─── Devices ─────────────────────────────────────────────────────────

def available_devices():
    import torch
    devices = []
    if torch.backends.mps.is_available():
        devices.append("mps")
    if torch.cuda.is_available():
        devices.append("cuda")
    devices.append("cpu")
    return devices


# ─── APG float32-on-MPS patch (MPS has no float64) ───────────────────

def _patch_apg_for_mps():
    """SA3's DiT apg_project() casts to float64, which MPS cannot do. Replace it
    with a version that uses float32 on MPS (math identical, negligible precision
    loss for guidance) and float64 everywhere else."""
    import torch
    from stable_audio_tools.models.dit import DiffusionTransformer

    def apg_project(self, v0, v1, padding_mask=None):
        dtype = v0.dtype
        work = torch.float32 if v0.device.type == "mps" else torch.float64
        v0, v1 = v0.to(work), v1.to(work)
        if padding_mask is not None:
            mask = padding_mask.unsqueeze(1).to(work)
            v0m, v1m = v0 * mask, v1 * mask
            v1n = v1m.norm(dim=[-1, -2], keepdim=True).clamp(min=1e-8)
            v1u = v1m / v1n
            v0_parallel = (v0m * v1u).sum(dim=[-1, -2], keepdim=True) * v1u
            v0_orthogonal = (v0 - (v0 * v1u).sum(dim=[-1, -2], keepdim=True) * v1u) * mask
        else:
            v1 = torch.nn.functional.normalize(v1, dim=[-1, -2])
            v0_parallel = (v0 * v1).sum(dim=[-1, -2], keepdim=True) * v1
            v0_orthogonal = v0 - v0_parallel
        return v0_parallel.to(dtype), v0_orthogonal.to(dtype)

    DiffusionTransformer.apg_project = apg_project
    log.info("Patched DiT.apg_project for MPS (float32).")


# ─── Pipeline (lazy, cached per (model, device)) ─────────────────────

class SA3Pipeline:
    def __init__(self, model, model_config, device, model_name):
        self.model = model
        self.model_config = model_config
        self.device = device
        self.model_name = model_name
        self.sample_rate = int(model_config.get("sample_rate", 44100))
        self.audio_channels = int(model_config.get("audio_channels", 2))


_pipelines = {}   # {(name, device): SA3Pipeline}
_models = {}      # {name: Path}


def _prepare_config_for_local_load(model_dir: Path, model_config):
    """Rewrite the t5gemma conditioner to load its encoder from the local
    subfolder inside the model dir, never from the network."""
    for c in _conditioning_configs(model_config):
        if c.get("type") == _SA3_CONDITIONER_TYPE:
            cc = c.setdefault("config", {})
            cc.pop("repo_id", None)
            cc["model_path"] = str(model_dir)         # parent; subfolder stays
            cc.setdefault("subfolder", "t5gemma-b-b-ul2")
    return model_config


def _load_pipeline(model_dir: Path, device):
    import torch
    from stable_audio_tools.models.factory import create_model_from_config
    from safetensors.torch import load_file

    name = model_dir.name
    log.info(f"Loading SA3 pipeline from {model_dir} on {device}...")

    _patch_apg_for_mps()

    raw = _read_model_config(model_dir)
    if raw is None:
        raise RuntimeError(f"model_config.json missing or invalid in {model_dir}")
    model_config = _prepare_config_for_local_load(model_dir, raw)

    # Force HF offline so the t5gemma encoder is read from the local subfolder.
    prev = {k: os.environ.get(k) for k in ("HF_HUB_OFFLINE", "TRANSFORMERS_OFFLINE")}
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["TRANSFORMERS_OFFLINE"] = "1"
    real_stdout = sys.stdout
    sys.stdout = sys.stderr   # libraries print during load → protect the pipe
    try:
        model = create_model_from_config(model_config)
        sd = load_file(str(model_dir / "model.safetensors"))
        # strict=False: the t5gemma encoder weights are loaded by the conditioner
        # from the subfolder, NOT present in this checkpoint. Verified: 0 missing
        # / 0 unexpected outside conditioner.* (truth-test stage 3).
        missing, unexpected = model.load_state_dict(sd, strict=False)
        leftover = [k for k in missing if not k.startswith("conditioner.")]
        if leftover or unexpected:
            log.warning(f"state_dict mismatch: missing(non-cond)={leftover[:4]} "
                        f"unexpected={list(unexpected)[:4]}")
        model = model.to(device).eval()
    finally:
        sys.stdout = real_stdout
        for k, v in prev.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    log.info(f"SA3 pipeline loaded on {device}.")
    return SA3Pipeline(model, model_config, device, name)


def get_pipeline(model_name, device):
    key = (model_name, device)
    if key not in _pipelines:
        if model_name not in _models:
            raise ValueError(f"Unknown model: {model_name} (have: {list(_models.keys())})")
        _pipelines[key] = _load_pipeline(_models[model_name], device)
    return _pipelines[key]


# ─── Generation ──────────────────────────────────────────────────────

def generate(pipe: SA3Pipeline, request):
    import torch
    import numpy as np
    from stable_audio_tools.inference.generation import generate_diffusion_cond_inpaint

    prompt = request.get("prompt_a", "")
    duration = float(request.get("duration", 3.0))
    steps = int(request.get("steps", 8))
    cfg_scale = float(request.get("cfg_scale", 6.0))
    seed = int(request.get("seed", -1))
    if seed < 0:
        seed = random.randint(0, 2**31 - 1)
    # Determinism is DEVICE-SCOPED: pingpong is a flow sampler using on-device
    # torch.randn (seeded below via the lib's seed= arg), so same seed reproduces
    # bit-for-bit on the SAME device type, but not across mps/cuda/cpu. T5ynth
    # presets are already device-type-specific, so this matches the product model.

    sr = pipe.sample_rate
    sample_size = int(math.ceil(duration * sr))

    log.info(f"Generating on {pipe.device}: '{prompt}' ({duration:.1f}s, {steps} steps, "
             f"CFG={cfg_scale}, seed={seed})")

    conditioning = [{"prompt": prompt, "seconds_total": duration}]

    real_stdout = sys.stdout
    sys.stdout = sys.stderr   # sampler prints a tqdm bar → protect the pipe
    t0 = time.time()
    try:
        audio = generate_diffusion_cond_inpaint(
            pipe.model,
            steps=steps,
            cfg_scale=cfg_scale,
            conditioning=conditioning,
            sample_size=sample_size,
            sampler_type="pingpong",
            seed=seed,
            device=pipe.device,
        )
    finally:
        sys.stdout = real_stdout
    elapsed = time.time() - t0

    # [B, C, N] → [C, N]; peak-normalize to [-1, 1] (stable-audio convention)
    audio = audio.detach().float().cpu()
    if audio.dim() == 3:
        audio = audio[0]
    peak = audio.abs().max()
    if float(peak) > 0:
        audio = (audio / peak).clamp(-1.0, 1.0)
    audio_np = audio.numpy()

    # Truncate to the requested length (sampler rounds up to a latent boundary).
    want = int(math.ceil(duration * sr))
    if audio_np.shape[-1] > want:
        audio_np = audio_np[..., :want]
    audio_np = np.ascontiguousarray(audio_np.astype(np.float32))

    log.info(f"Generated in {elapsed:.1f}s ({audio_np.shape[1]} samples)")
    # emb_stats=None for now → num_dims=0 footer (valid; embedding UI deferred).
    return audio_np, sr, seed, elapsed, None


# ─── IPC framing (byte-identical to pipe_inference.py) ───────────────

def send_ready(devices, default_device, models, default_model):
    payload = {
        "devices": devices,
        "default": default_device,
        "models": list(models.keys()),
        "default_model": default_model,
    }
    info = json.dumps(payload).encode("utf-8")
    sys.stdout.buffer.write(b'\x02')
    sys.stdout.buffer.write(struct.pack('<H', len(info)))
    sys.stdout.buffer.write(info)
    sys.stdout.buffer.flush()


def send_audio(audio_np, sr, seed, elapsed_seconds, emb_stats=None):
    import numpy as np
    channels, samples = audio_np.shape
    pcm = audio_np.astype(np.float32).tobytes()
    # NOTE: wire float32 = elapsed_seconds * 1000; C++ divides by 1000 → seconds.
    header = struct.pack('<iiiiif', 1, samples, channels, sr, seed, elapsed_seconds * 1000)
    sys.stdout.buffer.write(b'\x01')
    sys.stdout.buffer.write(header)
    sys.stdout.buffer.write(pcm)
    if emb_stats is not None:
        emb_a, emb_b, baseline = emb_stats
        sys.stdout.buffer.write(struct.pack('<H', len(emb_a)))
        sys.stdout.buffer.write(emb_a.astype(np.float32).tobytes())
        sys.stdout.buffer.write(emb_b.astype(np.float32).tobytes())
        sys.stdout.buffer.write(baseline.astype(np.float32).tobytes())
    else:
        sys.stdout.buffer.write(struct.pack('<H', 0))
    sys.stdout.buffer.flush()


def send_error(message):
    msg_bytes = str(message).encode('utf-8')
    sys.stdout.buffer.write(b'\x00')
    sys.stdout.buffer.write(struct.pack('<I', len(msg_bytes)))
    sys.stdout.buffer.write(msg_bytes)
    sys.stdout.buffer.flush()


# ─── Main loop ───────────────────────────────────────────────────────

def _choose_startup(models, devices):
    """Load the first model that succeeds on any device. Returns (name, device)."""
    failures = []
    for name in models:
        for dev in devices:
            try:
                get_pipeline(name, dev)
                return name, dev, failures
            except Exception as e:
                failures.append(f"{name}/{dev}: {e}")
                log.warning(f"Startup load failed: {name} on {dev}: {e}")
    raise RuntimeError("No SA3 model loaded (" + "; ".join(failures) + ")")


def main():
    global _models
    import numpy as np  # noqa: ensure available for send_audio
    sys.setrecursionlimit(50000)

    _models = find_models()
    if not _models:
        send_error("No Stable Audio 3 model directories found")
        return

    try:
        devices = available_devices()
        default_model, default_device, failures = _choose_startup(_models, devices)
    except Exception as e:
        log.error(f"Startup failed: {e}")
        send_error(f"Pipeline load failed: {e}")
        return

    send_ready(devices, default_device, _models, default_model)
    log.info(f"Ready. Models: {list(_models.keys())}, default: {default_model}, "
             f"devices: {devices}, default device: {default_device}")
    if failures:
        log.warning(f"Skipped: {failures}")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request = {}
        try:
            request = json.loads(line)
            model = request.get("model", default_model)
            device = request.get("device", default_device)
            if model not in _models:
                model = default_model
            if device == "auto" or device not in devices:
                device = default_device

            mode = request.get("mode", "generate")
            pipe = get_pipeline(model, device)

            if mode == "preload":
                empty = np.zeros((2, 0), dtype=np.float32)
                send_audio(empty, 44100, 0, 0.0)
                continue
            if mode not in ("generate", ""):
                raise ValueError(f"SA3 backend does not support mode '{mode}'")

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
