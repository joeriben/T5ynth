#!/usr/bin/env python3
"""Validate TorchScript models by running the same pipeline as C++ T5ynthInference.

Loads the exported .pt models and runs inference with identical parameters,
saving the result as WAV. Compare this WAV with t5ynth_debug_output.wav
to isolate whether the issue is in the models or the C++ pipeline.

Usage:
    python tools/validate_torchscript.py \
        --model-dir ~/Library/T5ynth/exported_models \
        --prompt "a steady clean saw wave, c3" \
        --seed 123456789
"""

import argparse
import json
import math
import numpy as np
import soundfile as sf
import torch
import sentencepiece as spm
from pathlib import Path


def load_models(model_dir: Path, device="cpu"):
    """Load all TorchScript models."""
    print("Loading models...")
    tokenizer = spm.SentencePieceProcessor()
    tokenizer.Load(str(model_dir / "spiece.model"))

    t5 = torch.jit.load(str(model_dir / "t5_encoder.pt"), map_location=device)
    t5.eval()
    proj = torch.jit.load(str(model_dir / "projection_model.pt"), map_location=device)
    proj.eval()
    dit = torch.jit.load(str(model_dir / "dit.pt"), map_location=device)
    dit.eval()
    vae = torch.jit.load(str(model_dir / "vae_decoder.pt"), map_location=device)
    vae.eval()

    with open(model_dir / "scheduler_config.json") as f:
        sched_config = json.load(f)

    with open(model_dir / "vae_meta.json") as f:
        vae_meta = json.load(f)

    print("All models loaded.")
    return tokenizer, t5, proj, dit, vae, sched_config, vae_meta


def tokenize(tokenizer, text, max_len=128):
    """Tokenize and pad to max_len."""
    ids = tokenizer.Encode(text)
    ids = ids[:max_len]
    ids += [0] * (max_len - len(ids))
    return torch.tensor([ids], dtype=torch.long)


def set_timesteps(config, num_steps):
    """Compute exponential sigma schedule (matching C++ DiffusionScheduler)."""
    sigma_min = config.get("sigma_min", 0.3)
    sigma_max = config.get("sigma_max", 500.0)

    sigmas = np.exp(np.linspace(np.log(sigma_max), np.log(sigma_min), num_steps))
    sigmas = np.append(sigmas, 0.0)  # final sigma = 0

    timesteps = np.arctan(sigmas[:-1]) / (np.pi / 2)
    return sigmas, timesteps


def scale_model_input(sample, sigma, sigma_data=1.0):
    c_in = 1.0 / math.sqrt(sigma**2 + sigma_data**2)
    return sample * c_in


def convert_model_output(model_output, sample, sigma, sigma_data=1.0):
    """v_prediction → denoised estimate."""
    c_skip = sigma_data**2 / (sigma**2 + sigma_data**2)
    c_out = -(sigma * sigma_data) / math.sqrt(sigma**2 + sigma_data**2)
    return c_skip * sample + c_out * model_output


def first_order_update(denoised, sample, sigma_s, sigma_t, noise):
    """SDE DPM-Solver++ first order (with noise injection)."""
    if sigma_s == 0:
        return denoised
    lambda_s = -math.log(sigma_s)
    lambda_t = -math.log(sigma_t) if sigma_t > 0 else 100.0
    h = lambda_t - lambda_s

    ratio = (sigma_t / sigma_s) * math.exp(-h) if sigma_t > 0 else 0.0
    exp_neg2h = math.exp(-2.0 * h)
    d0_coeff = 1.0 - exp_neg2h
    noise_coeff = sigma_t * math.sqrt(max(0.0, 1.0 - exp_neg2h))

    return ratio * sample + d0_coeff * denoised + noise_coeff * noise


def second_order_update(m0, m1, sample, sigma_s0, sigma_s1, sigma_t, noise):
    """SDE DPM-Solver++ second order midpoint (with noise injection)."""
    if sigma_s0 == 0:
        return m0
    lambda_t = -math.log(sigma_t) if sigma_t > 0 else 100.0
    lambda_s0 = -math.log(sigma_s0)
    lambda_s1 = -math.log(sigma_s1)

    h = lambda_t - lambda_s0
    h_0 = lambda_s0 - lambda_s1
    r0 = h_0 / h

    D0 = m0
    D1 = (1.0 / r0) * (m0 - m1)

    ratio = (sigma_t / sigma_s0) * math.exp(-h) if sigma_t > 0 else 0.0
    exp_neg2h = math.exp(-2.0 * h)
    d0_coeff = 1.0 - exp_neg2h
    d1_coeff = 0.5 * (1.0 - exp_neg2h)
    noise_coeff = sigma_t * math.sqrt(max(0.0, 1.0 - exp_neg2h))

    return ratio * sample + d0_coeff * D0 + d1_coeff * D1 + noise_coeff * noise


@torch.no_grad()
def generate(tokenizer, t5, proj, dit, vae, sched_config, vae_meta,
             prompt_a, prompt_b="", alpha=0.5, magnitude=1.0, noise_sigma=0.0,
             duration=3.0, start_pos=0.0, steps=20, cfg_scale=7.0, seed=42,
             device="cpu"):
    """Full inference pipeline matching C++ T5ynthInference::generate()."""

    # 1. Tokenize
    token_ids_a = tokenize(tokenizer, prompt_a).to(device)
    mask_a = (token_ids_a != 0).long().to(device)

    # 2. T5 encode
    out_a = t5(token_ids_a, mask_a)
    if isinstance(out_a, dict):
        emb_a = out_a["last_hidden_state"]
    elif isinstance(out_a, tuple):
        emb_a = out_a[0]
    else:
        emb_a = out_a

    emb_b = None
    if prompt_b:
        token_ids_b = tokenize(tokenizer, prompt_b).to(device)
        mask_b = (token_ids_b != 0).long().to(device)
        out_b = t5(token_ids_b, mask_b)
        if isinstance(out_b, dict):
            emb_b = out_b["last_hidden_state"]
        elif isinstance(out_b, tuple):
            emb_b = out_b[0]
        else:
            emb_b = out_b

    # 3. Embedding manipulation
    if emb_b is not None:
        manipulated = (1.0 - alpha) * emb_a + alpha * emb_b
    else:
        manipulated = emb_a.clone()
    if abs(magnitude - 1.0) > 1e-6:
        manipulated = manipulated * magnitude

    # 4. Duration conditioning
    virtual_total = duration / (1.0 - start_pos) if start_pos < 1.0 else duration
    seconds_start = start_pos * virtual_total
    seconds_end = seconds_start + duration

    projected = proj(
        manipulated,
        torch.tensor([seconds_start], dtype=torch.float32, device=device),
        torch.tensor([seconds_end], dtype=torch.float32, device=device),
    )

    if isinstance(projected, dict):
        text_hidden = projected["text_hidden_states"]
        start_hidden = projected["seconds_start_hidden_states"]
        end_hidden = projected["seconds_end_hidden_states"]
    else:
        text_hidden = projected[0]
        start_hidden = projected[1]
        end_hidden = projected[2]

    encoder_hidden = torch.cat([text_hidden, start_hidden, end_hidden], dim=1)
    global_hidden = torch.cat([start_hidden, end_hidden], dim=2).squeeze(1)

    # Attention mask: text padding masked, time tokens always valid
    text_mask = mask_a.bool()  # [1, 128]
    time_mask = torch.ones(1, 2, dtype=torch.bool, device=device)
    attention_mask = torch.cat([text_mask, time_mask], dim=1)  # [1, 130]

    print(f"  encoder_hidden: {encoder_hidden.shape}")
    print(f"  global_hidden: {global_hidden.shape}")
    print(f"  attention_mask: {attention_mask.shape}, sum={attention_mask.sum().item()}")

    # 5. Diffusion loop
    sigmas, timesteps_arr = set_timesteps(sched_config, steps)
    latent_seq_len = 1024  # full size, like C++

    torch.manual_seed(seed)
    latent = torch.randn(1, 64, latent_seq_len, device=device) * sigmas[0]

    neg_encoder = torch.zeros_like(encoder_hidden)
    neg_global = torch.zeros_like(global_hidden)

    prev_denoised = None
    lower_order_nums = 0

    for i in range(steps):
        sigma = sigmas[i]
        sigma_next = sigmas[i + 1]

        # Scale input
        scaled = scale_model_input(latent, sigma)
        t = torch.tensor([timesteps_arr[i]], dtype=torch.float32, device=device)

        # DiT forward — conditional
        cond_out = dit(scaled, t, encoder_hidden, global_hidden, attention_mask)

        if cfg_scale > 1.0:
            uncond_out = dit(scaled, t, neg_encoder, neg_global, attention_mask)
            model_output = uncond_out + cfg_scale * (cond_out - uncond_out)
        else:
            model_output = cond_out

        denoised = convert_model_output(model_output, latent, sigma)
        noise = torch.randn_like(latent)

        is_last = (i == steps - 1)
        use_first_order = (i < 1 or lower_order_nums < 1 or is_last)

        if use_first_order:
            latent = first_order_update(denoised, latent, sigma, sigma_next, noise)
        else:
            latent = second_order_update(denoised, prev_denoised, latent,
                                          sigma, sigmas[i - 1], sigma_next, noise)

        prev_denoised = denoised
        if lower_order_nums < 2:
            lower_order_nums += 1

        rms = latent.float().pow(2).mean().sqrt().item()
        print(f"  step {i:2d}/{steps}: sigma={sigma:.3f} → {sigma_next:.3f}, latent RMS={rms:.4f}")

    # 6. VAE decode (wrapper already divides by scaling_factor)
    audio = vae(latent)
    audio = audio.squeeze(0).cpu().numpy()  # [2, N]
    print(f"  VAE output: {audio.shape}, range=[{audio.min():.3f}, {audio.max():.3f}]")

    # 7. Trim to duration
    sample_rate = vae_meta.get("sample_rate", 44100)
    requested_samples = int(math.ceil(duration * sample_rate))
    if audio.shape[1] > requested_samples:
        audio = audio[:, :requested_samples]

    return audio, sample_rate


def main():
    parser = argparse.ArgumentParser(description="Validate TorchScript inference")
    parser.add_argument("--model-dir", type=str, default="~/Library/T5ynth/exported_models")
    parser.add_argument("--prompt", type=str, default="a steady clean saw wave, c3")
    parser.add_argument("--prompt-b", type=str, default="")
    parser.add_argument("--alpha", type=float, default=0.0)
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--steps", type=int, default=20)
    parser.add_argument("--cfg", type=float, default=7.0)
    parser.add_argument("--seed", type=int, default=123456789)
    parser.add_argument("--output", type=str, default="~/Desktop/ts_validate.wav")
    args = parser.parse_args()

    model_dir = Path(args.model_dir).expanduser()
    output_path = Path(args.output).expanduser()

    tokenizer, t5, proj, dit, vae, sched_config, vae_meta = load_models(model_dir)

    print(f"\nGenerating: '{args.prompt}' ({args.duration}s, {args.steps} steps, CFG={args.cfg}, seed={args.seed})")
    audio, sr = generate(
        tokenizer, t5, proj, dit, vae, sched_config, vae_meta,
        prompt_a=args.prompt, prompt_b=args.prompt_b,
        alpha=args.alpha, duration=args.duration,
        steps=args.steps, cfg_scale=args.cfg, seed=args.seed,
    )

    # Save as WAV
    sf.write(str(output_path), audio.T, sr)  # soundfile expects [N, channels]
    print(f"\nSaved: {output_path} ({audio.shape[1]} samples, {audio.shape[0]}ch, {sr}Hz)")
    print(f"Duration: {audio.shape[1]/sr:.2f}s")


if __name__ == "__main__":
    main()
