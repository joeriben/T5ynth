#!/usr/bin/env python3
"""Compare original model outputs vs TorchScript exports, layer by layer."""

import torch
import json
from pathlib import Path
from diffusers import StableAudioPipeline

MODEL_DIR = Path("~/Library/T5ynth/models/stable-audio-open-1.0").expanduser()
EXPORT_DIR = Path("~/Library/T5ynth/exported_models").expanduser()

print("Loading original pipeline...")
pipe = StableAudioPipeline.from_pretrained(str(MODEL_DIR), torch_dtype=torch.float32).to("cpu")

print("Loading TorchScript models...")
ts_t5 = torch.jit.load(str(EXPORT_DIR / "t5_encoder.pt"), map_location="cpu")
ts_proj = torch.jit.load(str(EXPORT_DIR / "projection_model.pt"), map_location="cpu")
ts_dit = torch.jit.load(str(EXPORT_DIR / "dit.pt"), map_location="cpu")
ts_vae = torch.jit.load(str(EXPORT_DIR / "vae_decoder.pt"), map_location="cpu")

torch.manual_seed(42)

# ═══ Test 1: T5 Encoder ═══
print("\n═══ T5 Encoder ═══")
tokenizer = pipe.tokenizer
tokens = tokenizer("a steady clean saw wave", max_length=128, padding="max_length",
                    truncation=True, return_tensors="pt")
input_ids = tokens["input_ids"]
attention_mask = tokens["attention_mask"]

with torch.no_grad():
    orig_t5 = pipe.text_encoder(input_ids, attention_mask)
    orig_hidden = orig_t5.last_hidden_state if hasattr(orig_t5, 'last_hidden_state') else orig_t5[0]

    ts_t5_out = ts_t5(input_ids, attention_mask)
    if isinstance(ts_t5_out, dict):
        ts_hidden = ts_t5_out["last_hidden_state"]
    elif isinstance(ts_t5_out, tuple):
        ts_hidden = ts_t5_out[0]
    else:
        ts_hidden = ts_t5_out

diff = (orig_hidden - ts_hidden).abs().max().item()
print(f"  Max diff: {diff:.2e}  (shape: {orig_hidden.shape})")
print(f"  Orig RMS: {orig_hidden.pow(2).mean().sqrt().item():.4f}")
print(f"  TS RMS:   {ts_hidden.pow(2).mean().sqrt().item():.4f}")

# ═══ Test 2: Projection Model ═══
print("\n═══ Projection Model ═══")
text_hidden = torch.randn(1, 128, 768)
seconds_start = torch.tensor([0.0])
seconds_end = torch.tensor([3.0])

with torch.no_grad():
    orig_proj = pipe.projection_model(text_hidden, seconds_start, seconds_end)
    ts_proj_out = ts_proj(text_hidden, seconds_start, seconds_end)

for name in ["text_hidden_states", "seconds_start_hidden_states", "seconds_end_hidden_states"]:
    orig_t = getattr(orig_proj, name)
    if isinstance(ts_proj_out, dict):
        ts_t = ts_proj_out[name]
    else:
        idx = ["text_hidden_states", "seconds_start_hidden_states", "seconds_end_hidden_states"].index(name)
        ts_t = ts_proj_out[idx]
    d = (orig_t - ts_t).abs().max().item()
    print(f"  {name}: max_diff={d:.2e}, shape={orig_t.shape}")

# ═══ Test 3: DiT (single step) ═══
print("\n═══ DiT (single forward pass) ═══")

# Use TraceSafeAttnProcessor like the export script
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_to_torchscript import TraceSafeAttnProcessor

hidden_states = torch.randn(1, 64, 1024)
timestep = torch.tensor([0.5])
encoder_hidden = torch.randn(1, 130, 768)
global_hidden = torch.randn(1, 1536)
attention_mask = torch.ones(1, 130, dtype=torch.bool)

# Original DiT with TraceSafe processor (same as what was traced)
pipe.transformer.set_attn_processor(TraceSafeAttnProcessor())
with torch.no_grad():
    orig_dit = pipe.transformer(
        hidden_states=hidden_states, timestep=timestep,
        encoder_hidden_states=encoder_hidden,
        global_hidden_states=global_hidden,
        encoder_attention_mask=attention_mask,
    ).sample

    ts_dit_out = ts_dit(hidden_states, timestep, encoder_hidden, global_hidden, attention_mask)

diff = (orig_dit - ts_dit_out).abs().max().item()
rms_diff = (orig_dit - ts_dit_out).pow(2).mean().sqrt().item()
print(f"  Max diff: {diff:.2e}")
print(f"  RMS diff: {rms_diff:.2e}")
print(f"  Orig RMS: {orig_dit.pow(2).mean().sqrt().item():.4f}")
print(f"  TS RMS:   {ts_dit_out.pow(2).mean().sqrt().item():.4f}")
print(f"  Orig range: [{orig_dit.min():.4f}, {orig_dit.max():.4f}]")
print(f"  TS range:   [{ts_dit_out.min():.4f}, {ts_dit_out.max():.4f}]")

# ═══ Test 4: VAE Decoder ═══
print("\n═══ VAE Decoder ═══")
latent = torch.randn(1, 64, 1024) * 0.1  # small values like post-diffusion

scaling_factor = getattr(pipe.vae.config, 'scaling_factor', 1.0)
with torch.no_grad():
    orig_audio = pipe.vae.decode(latent / scaling_factor).sample
    ts_audio = ts_vae(latent)  # wrapper already divides by scaling_factor

diff = (orig_audio - ts_audio).abs().max().item()
print(f"  Max diff: {diff:.2e}")
print(f"  Orig range: [{orig_audio.min():.4f}, {orig_audio.max():.4f}]")
print(f"  TS range:   [{ts_audio.min():.4f}, {ts_audio.max():.4f}]")
print(f"  Orig shape: {orig_audio.shape}")
print(f"  TS shape:   {ts_audio.shape}")

print("\n═══ Summary ═══")
print("If DiT max_diff is large (>0.01), the TorchScript export of DiT is broken.")
print("Check if TraceSafeAttnProcessor matches the runtime processor.")
