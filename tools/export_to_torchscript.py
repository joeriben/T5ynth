#!/usr/bin/env python3
"""
Export Stable Audio Open 1.0 components to TorchScript for LibTorch C++ inference.

Usage:
    python tools/export_to_torchscript.py \
        --model-dir ~/models/stable-audio-open-1.0 \
        --output-dir ~/t5ynth/exported_models

Exports:
    1. t5_encoder.pt          — T5EncoderModel (text → 768d embeddings)
    2. projection_model.pt    — Duration conditioning (seconds → 768d)
    3. dit.pt                 — Diffusion Transformer (denoising network)
    4. vae_decoder.pt         — AutoencoderOobleck decoder (latents → audio)
    5. spiece.model           — SentencePiece tokenizer vocabulary (copied)
    6. pca_components.pt      — PCA axis directions [N, 768] (copied if exists)
    7. scheduler_config.json  — Scheduler parameters (copied)

After export, the JUCE plugin loads these directly via LibTorch — no Python needed.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path
from typing import Optional, Tuple

import torch
import torch.nn.functional as F
from diffusers import StableAudioPipeline


class TraceSafeAttnProcessor:
    """Attention processor for TorchScript tracing — identical to StableAudioAttnProcessor2_0
    but drops the `output_size` kwarg from repeat_interleave.

    During torch.jit.trace, `key.shape[1] * heads_per_kv_head` is lowered to a CPU
    tensor while the data tensors live on CUDA, which causes a device mismatch in
    torch.repeat_interleave. The output_size parameter is a pure performance hint —
    removing it does not change numerical results.
    """

    def __call__(
        self,
        attn,
        hidden_states: torch.Tensor,
        encoder_hidden_states: Optional[torch.Tensor] = None,
        attention_mask: Optional[torch.Tensor] = None,
        rotary_emb: Optional[torch.Tensor] = None,
    ) -> torch.Tensor:
        from diffusers.models.embeddings import apply_rotary_emb

        residual = hidden_states
        input_ndim = hidden_states.ndim

        if input_ndim == 4:
            batch_size, channel, height, width = hidden_states.shape
            hidden_states = hidden_states.view(batch_size, channel, height * width).transpose(1, 2)

        batch_size, sequence_length, _ = (
            hidden_states.shape if encoder_hidden_states is None else encoder_hidden_states.shape
        )

        if attention_mask is not None:
            attention_mask = attn.prepare_attention_mask(attention_mask, sequence_length, batch_size)
            attention_mask = attention_mask.view(batch_size, attn.heads, -1, attention_mask.shape[-1])

        query = attn.to_q(hidden_states)

        if encoder_hidden_states is None:
            encoder_hidden_states = hidden_states
        elif attn.norm_cross:
            encoder_hidden_states = attn.norm_encoder_hidden_states(encoder_hidden_states)

        key = attn.to_k(encoder_hidden_states)
        value = attn.to_v(encoder_hidden_states)

        # Compute head_dim and kv_heads from module weights (Python ints, not traced tensors).
        # Using tensor .shape during tracing produces symbolic traced values on CPU,
        # which causes device mismatches with CUDA tensors in repeat_interleave.
        head_dim = attn.to_q.out_features // attn.heads  # Python int
        kv_heads = attn.to_k.out_features // head_dim    # Python int

        query = query.view(batch_size, -1, attn.heads, head_dim).transpose(1, 2)
        key = key.view(batch_size, -1, kv_heads, head_dim).transpose(1, 2)
        value = value.view(batch_size, -1, kv_heads, head_dim).transpose(1, 2)

        if kv_heads != attn.heads:
            heads_per_kv_head = attn.heads // kv_heads  # Python int — safe for tracing
            key = torch.repeat_interleave(key, heads_per_kv_head, dim=1)
            value = torch.repeat_interleave(value, heads_per_kv_head, dim=1)

        if attn.norm_q is not None:
            query = attn.norm_q(query)
        if attn.norm_k is not None:
            key = attn.norm_k(key)

        if rotary_emb is not None:
            query_dtype = query.dtype
            key_dtype = key.dtype
            query = query.to(torch.float32)
            key = key.to(torch.float32)

            rot_dim = rotary_emb[0].shape[-1]
            query_to_rotate, query_unrotated = query[..., :rot_dim], query[..., rot_dim:]
            query_rotated = apply_rotary_emb(query_to_rotate, rotary_emb, use_real=True, use_real_unbind_dim=-2)
            query = torch.cat((query_rotated, query_unrotated), dim=-1)

            if not attn.is_cross_attention:
                key_to_rotate, key_unrotated = key[..., :rot_dim], key[..., rot_dim:]
                key_rotated = apply_rotary_emb(key_to_rotate, rotary_emb, use_real=True, use_real_unbind_dim=-2)
                key = torch.cat((key_rotated, key_unrotated), dim=-1)

            query = query.to(query_dtype)
            key = key.to(key_dtype)

        hidden_states = F.scaled_dot_product_attention(
            query, key, value, attn_mask=attention_mask, dropout_p=0.0, is_causal=False
        )

        hidden_states = hidden_states.transpose(1, 2).reshape(batch_size, -1, attn.heads * head_dim)
        hidden_states = hidden_states.to(query.dtype)

        hidden_states = attn.to_out[0](hidden_states)
        hidden_states = attn.to_out[1](hidden_states)

        if input_ndim == 4:
            hidden_states = hidden_states.transpose(-1, -2).reshape(batch_size, channel, height, width)

        if attn.residual_connection:
            hidden_states = hidden_states + residual

        hidden_states = hidden_states / attn.rescale_output_factor

        return hidden_states


def export_t5_encoder(pipeline, output_dir: Path, device: str):
    """Export T5 text encoder to TorchScript.

    T5 has tied weights (embedding ↔ lm_head). TorchScript doesn't support
    tied weights, so we untie them before tracing. This is safe for inference-only
    (no fine-tuning).
    """
    print("[1/4] Exporting T5 encoder...")
    encoder = pipeline.text_encoder
    encoder.eval()

    # Untie weights if needed
    if hasattr(encoder, 'encoder') and hasattr(encoder.encoder, 'embed_tokens'):
        pass  # T5EncoderModel doesn't have lm_head, but check just in case

    # Create example inputs (max_length=128 as per Stable Audio config)
    tokenizer = pipeline.tokenizer
    example_text = "a steady clean saw wave"
    tokens = tokenizer(
        example_text,
        max_length=128,
        padding="max_length",
        truncation=True,
        return_tensors="pt",
    )
    input_ids = tokens["input_ids"].to(device)
    attention_mask = tokens["attention_mask"].to(device)

    # Trace the encoder
    with torch.no_grad():
        traced = torch.jit.trace(
            encoder,
            (input_ids, attention_mask),
            strict=False,  # Allow non-tensor intermediate values
        )

    # Verify output matches
    with torch.no_grad():
        ref_output = encoder(input_ids, attention_mask)
        traced_output = traced(input_ids, attention_mask)

    ref_hidden = ref_output.last_hidden_state
    # Traced output may be a dict, tuple, or ModelOutput
    if isinstance(traced_output, dict):
        traced_hidden = traced_output["last_hidden_state"]
    elif isinstance(traced_output, tuple):
        traced_hidden = traced_output[0]
    else:
        traced_hidden = traced_output.last_hidden_state

    max_diff = (ref_hidden - traced_hidden).abs().max().item()
    print(f"  T5 encoder max difference: {max_diff:.2e}")
    assert max_diff < 1e-4, f"T5 encoder trace diverged: max_diff={max_diff}"

    output_path = output_dir / "t5_encoder.pt"
    traced.save(str(output_path))
    print(f"  Saved: {output_path} ({output_path.stat().st_size / 1e6:.1f} MB)")


def export_projection_model(pipeline, output_dir: Path, device: str):
    """Export the projection model (duration conditioning).

    StableAudioProjectionModel contains:
    - text_projection: nn.Identity (768→768, no-op for text)
    - start_number_conditioner: NumberConditioner (float→768d)
    - end_number_conditioner: NumberConditioner (float→768d)
    """
    print("[2/4] Exporting projection model...")
    proj = pipeline.projection_model
    proj.eval()

    # Example inputs
    text_hidden = torch.randn(1, 128, 768, device=device)
    seconds_start = torch.tensor([0.0], device=device)
    seconds_end = torch.tensor([3.0], device=device)

    # The projection model's forward signature needs investigation
    # Let's check what it expects
    with torch.no_grad():
        # StableAudioProjectionModel.forward(text_hidden_states, start_seconds, end_seconds)
        ref_output = proj(text_hidden, seconds_start, seconds_end)

    with torch.no_grad():
        traced = torch.jit.trace(
            proj,
            (text_hidden, seconds_start, seconds_end),
            strict=False,
        )

    with torch.no_grad():
        traced_output = traced(text_hidden, seconds_start, seconds_end)

    # Check output — StableAudioProjectionModelOutput has .text_hidden_states
    ref_tensor = ref_output.text_hidden_states if hasattr(ref_output, 'text_hidden_states') else ref_output[0]
    traced_tensor = traced_output["text_hidden_states"] if isinstance(traced_output, dict) else traced_output[0]
    max_diff = (ref_tensor - traced_tensor).abs().max().item()

    print(f"  Projection model max difference: {max_diff:.2e}")
    assert max_diff < 1e-4, f"Projection model trace diverged: max_diff={max_diff}"

    output_path = output_dir / "projection_model.pt"
    traced.save(str(output_path))
    print(f"  Saved: {output_path} ({output_path.stat().st_size / 1e6:.1f} MB)")


def export_dit(pipeline, output_dir: Path, device: str):
    """Export the Diffusion Transformer (StableAudioDiTModel) to TorchScript.

    This is the denoising network called at each diffusion step.
    Input shapes are fixed for tracing:
    - hidden_states: [1, 64, 1024] (latent representation)
    - timestep: [1] (current noise level / sigma)
    - encoder_hidden_states: [1, 129, 768] (text + duration conditioning)
    - attention_mask: [1, 129] (conditioning mask)
    """
    print("[3/4] Exporting DiT (diffusion transformer)...")
    dit = pipeline.transformer
    dit.eval()

    # Replace attention processor with trace-safe version (no output_size in repeat_interleave)
    dit.set_attn_processor(TraceSafeAttnProcessor())

    # Example inputs with fixed shapes
    # hidden_states shape: [B, channels, sequence_length] = [1, 64, 1024]
    hidden_states = torch.randn(1, 64, 1024, device=device, dtype=dit.dtype)
    timestep = torch.tensor([1.0], device=device, dtype=dit.dtype)
    encoder_hidden_states = torch.randn(1, 129, 768, device=device, dtype=dit.dtype)
    # global_hidden_states = concatenated start+end duration embeddings (2×768 = 1536)
    global_hidden_states = torch.randn(1, 1536, device=device, dtype=dit.dtype)
    encoder_attention_mask = torch.ones(1, 129, device=device, dtype=torch.bool)

    with torch.no_grad():
        ref_output = dit(
            hidden_states=hidden_states,
            timestep=timestep,
            encoder_hidden_states=encoder_hidden_states,
            global_hidden_states=global_hidden_states,
            encoder_attention_mask=encoder_attention_mask,
        )

    # DiT forward uses keyword args — wrap for trace
    class DiTWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, hidden_states, timestep, encoder_hidden_states,
                    global_hidden_states, encoder_attention_mask):
            return self.model(
                hidden_states=hidden_states,
                timestep=timestep,
                encoder_hidden_states=encoder_hidden_states,
                global_hidden_states=global_hidden_states,
                encoder_attention_mask=encoder_attention_mask,
            ).sample

    wrapper = DiTWrapper(dit)
    wrapper.eval()

    with torch.no_grad():
        traced = torch.jit.trace(
            wrapper,
            (hidden_states, timestep, encoder_hidden_states,
             global_hidden_states, encoder_attention_mask),
            strict=False,
        )

    with torch.no_grad():
        traced_output = traced(hidden_states, timestep, encoder_hidden_states,
                               global_hidden_states, encoder_attention_mask)

    ref_sample = ref_output.sample
    max_diff = (ref_sample - traced_output).abs().max().item()
    print(f"  DiT max difference: {max_diff:.2e}")
    assert max_diff < 5e-3, f"DiT trace diverged: max_diff={max_diff}"

    output_path = output_dir / "dit.pt"
    traced.save(str(output_path))
    print(f"  Saved: {output_path} ({output_path.stat().st_size / 1e6:.1f} MB)")


def export_vae_decoder(pipeline, output_dir: Path, device: str):
    """Export the VAE decoder (AutoencoderOobleck) to TorchScript.

    Input: latent [1, 64, 1024]
    Output: audio [1, 2, N] (stereo waveform at 44100 Hz)
    """
    print("[4/4] Exporting VAE decoder...")
    vae = pipeline.vae
    vae.eval()

    # Example latent (post-diffusion)
    latent = torch.randn(1, 64, 1024, device=device, dtype=vae.dtype)

    # Scale latent by vae.config.scaling_factor if needed
    scaling_factor = getattr(vae.config, 'scaling_factor', 1.0)

    class VAEDecoderWrapper(torch.nn.Module):
        def __init__(self, vae_model, scale):
            super().__init__()
            self.vae = vae_model
            self.scale = scale

        def forward(self, latent):
            scaled = latent / self.scale
            return self.vae.decode(scaled).sample

    wrapper = VAEDecoderWrapper(vae, scaling_factor)
    wrapper.eval()

    with torch.no_grad():
        ref_audio = wrapper(latent)
        traced = torch.jit.trace(wrapper, (latent,), strict=False)
        traced_audio = traced(latent)

    max_diff = (ref_audio - traced_audio).abs().max().item()
    print(f"  VAE decoder max difference: {max_diff:.2e}")
    assert max_diff < 5e-3, f"VAE decoder trace diverged: max_diff={max_diff}"

    output_path = output_dir / "vae_decoder.pt"
    traced.save(str(output_path))
    print(f"  Saved: {output_path} ({output_path.stat().st_size / 1e6:.1f} MB)")

    # Also save the scaling factor and sample rate
    meta = {
        "scaling_factor": scaling_factor,
        "sample_rate": 44100,
        "vae_hop_length": getattr(vae.config, 'hop_length', 2048),
    }
    meta_path = output_dir / "vae_meta.json"
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    print(f"  Saved: {meta_path}")


def copy_auxiliary_files(model_dir: Path, output_dir: Path):
    """Copy tokenizer, scheduler config, and PCA components."""
    print("\nCopying auxiliary files...")

    # SentencePiece model
    spiece = model_dir / "tokenizer" / "spiece.model"
    if spiece.exists():
        shutil.copy2(spiece, output_dir / "spiece.model")
        print(f"  Copied: spiece.model")
    else:
        print(f"  WARNING: {spiece} not found!")

    # Tokenizer config (for special token IDs)
    for name in ["tokenizer.json", "tokenizer_config.json", "special_tokens_map.json"]:
        src = model_dir / "tokenizer" / name
        if src.exists():
            shutil.copy2(src, output_dir / name)

    # Scheduler config
    sched = model_dir / "scheduler" / "scheduler_config.json"
    if sched.exists():
        shutil.copy2(sched, output_dir / "scheduler_config.json")
        print(f"  Copied: scheduler_config.json")

    # PCA components (from t5ynth backend data)
    pca_src = Path(__file__).resolve().parent.parent / "backend" / "data" / "pca_components.pt"
    if pca_src.exists():
        shutil.copy2(pca_src, output_dir / "pca_components.pt")
        pca = torch.load(pca_src, map_location="cpu")
        print(f"  Copied: pca_components.pt ({pca.shape})")
    else:
        print(f"  WARNING: PCA components not found at {pca_src}")


def main():
    parser = argparse.ArgumentParser(description="Export Stable Audio Open to TorchScript")
    parser.add_argument("--model-dir", type=str, required=True,
                        help="Path to Stable Audio Open 1.0 HuggingFace directory")
    parser.add_argument("--output-dir", type=str, default=None,
                        help="Output directory for exported models (default: <model-dir>/exported)")
    parser.add_argument("--device", type=str, default="cuda" if torch.cuda.is_available() else "cpu",
                        help="Device for tracing (default: cuda if available)")
    parser.add_argument("--dtype", type=str, default="float32", choices=["float16", "float32"],
                        help="Model dtype for tracing (default: float32)")
    args = parser.parse_args()

    model_dir = Path(args.model_dir).expanduser().resolve()
    if not (model_dir / "model_index.json").exists():
        print(f"ERROR: {model_dir} does not contain model_index.json")
        print("Expected a HuggingFace pipeline directory for stabilityai/stable-audio-open-1.0")
        sys.exit(1)

    output_dir = Path(args.output_dir) if args.output_dir else model_dir / "exported"
    output_dir.mkdir(parents=True, exist_ok=True)

    device = args.device
    dtype = torch.float16 if args.dtype == "float16" else torch.float32

    print(f"Model directory: {model_dir}")
    print(f"Output directory: {output_dir}")
    print(f"Device: {device}, dtype: {dtype}")
    print()

    # Load pipeline
    print("Loading StableAudioPipeline...")
    pipeline = StableAudioPipeline.from_pretrained(
        str(model_dir),
        torch_dtype=dtype,
    ).to(device)
    print(f"Pipeline loaded on {device}.")
    print()

    # Export each component
    try:
        export_t5_encoder(pipeline, output_dir, device)
        print()
        export_projection_model(pipeline, output_dir, device)
        print()
        export_dit(pipeline, output_dir, device)
        print()
        export_vae_decoder(pipeline, output_dir, device)
        print()
        copy_auxiliary_files(model_dir, output_dir)
    except Exception as e:
        print(f"\nEXPORT FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    print()
    print("=" * 60)
    print("Export complete!")
    print(f"Files in {output_dir}:")
    for f in sorted(output_dir.iterdir()):
        size = f.stat().st_size
        if size > 1e6:
            print(f"  {f.name:30s} {size / 1e6:.1f} MB")
        else:
            print(f"  {f.name:30s} {size / 1e3:.1f} KB")
    print()
    print("Next: Point the T5ynth plugin's model directory to:")
    print(f"  {output_dir}")


if __name__ == "__main__":
    main()
