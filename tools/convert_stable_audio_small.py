#!/usr/bin/env python3
"""Convert stable-audio-open-small from native format to diffusers StableAudioPipeline.

Adapted from huggingface/diffusers/scripts/convert_stable_audio.py
for the small model which only has seconds_total (no seconds_start).
"""
import json
import os
import sys

import torch
from safetensors.torch import load_file
from transformers import AutoTokenizer, T5EncoderModel

from diffusers import (
    AutoencoderOobleck,
    CosineDPMSolverMultistepScheduler,
    StableAudioDiTModel,
    StableAudioPipeline,
    StableAudioProjectionModel,
)


def convert_state_dict(state_dict, num_autoencoder_layers=5):
    """Convert native stable-audio-tools state dict to diffusers format."""

    # ── Projection model (conditioners) ──
    projection_model_state_dict = {
        k.replace("conditioner.conditioners.", "").replace("embedder.embedding", "time_positional_embedding"): v
        for (k, v) in state_dict.items()
        if "conditioner.conditioners" in k
    }
    for key in list(projection_model_state_dict.keys()):
        new_key = key.replace("seconds_start", "start_number_conditioner").replace(
            "seconds_total", "end_number_conditioner"
        )
        projection_model_state_dict[new_key] = projection_model_state_dict.pop(key)

    # ── Transformer (DiT) ──
    model_state_dict = {k.replace("model.model.", ""): v for (k, v) in state_dict.items() if "model.model." in k}
    for key in list(model_state_dict.keys()):
        new_key = (
            key.replace("transformer.", "")
            .replace("layers", "transformer_blocks")
            .replace("self_attn", "attn1")
            .replace("cross_attn", "attn2")
            .replace("ff.ff", "ff.net")
        )
        new_key = (
            new_key.replace("pre_norm", "norm1")
            .replace("cross_attend_norm", "norm2")
            .replace("ff_norm", "norm3")
            .replace("to_out", "to_out.0")
        )
        new_key = new_key.replace("gamma", "weight").replace("beta", "bias")

        new_key = (
            new_key.replace("project", "proj")
            .replace("to_timestep_embed", "timestep_proj")
            .replace("timestep_features", "time_proj")
            .replace("to_global_embed", "global_proj")
            .replace("to_cond_embed", "cross_attention_proj")
        )

        if new_key == "time_proj.weight":
            model_state_dict[key] = model_state_dict[key].squeeze(1)

        if "to_qkv" in new_key:
            q, k_val, v = torch.chunk(model_state_dict.pop(key), 3, dim=0)
            model_state_dict[new_key.replace("qkv", "q")] = q
            model_state_dict[new_key.replace("qkv", "k")] = k_val
            model_state_dict[new_key.replace("qkv", "v")] = v
        elif "to_kv" in new_key:
            k_val, v = torch.chunk(model_state_dict.pop(key), 2, dim=0)
            model_state_dict[new_key.replace("kv", "k")] = k_val
            model_state_dict[new_key.replace("kv", "v")] = v
        else:
            model_state_dict[new_key] = model_state_dict.pop(key)

    # ── Autoencoder (Oobleck VAE) ──
    autoencoder_state_dict = {
        k.replace("pretransform.model.", "").replace("coder.layers.0", "coder.conv1"): v
        for (k, v) in state_dict.items()
        if "pretransform.model." in k
    }
    for key in list(autoencoder_state_dict.keys()):
        new_key = key
        if "coder.layers" in new_key:
            idx = int(new_key.split("coder.layers.")[1].split(".")[0])
            new_key = new_key.replace(f"coder.layers.{idx}", f"coder.block.{idx - 1}")

            if "encoder" in new_key:
                for i in range(3):
                    new_key = new_key.replace(f"block.{idx - 1}.layers.{i}", f"block.{idx - 1}.res_unit{i + 1}")
                new_key = new_key.replace(f"block.{idx - 1}.layers.3", f"block.{idx - 1}.snake1")
                new_key = new_key.replace(f"block.{idx - 1}.layers.4", f"block.{idx - 1}.conv1")
            else:
                for i in range(2, 5):
                    new_key = new_key.replace(f"block.{idx - 1}.layers.{i}", f"block.{idx - 1}.res_unit{i - 1}")
                new_key = new_key.replace(f"block.{idx - 1}.layers.0", f"block.{idx - 1}.snake1")
                new_key = new_key.replace(f"block.{idx - 1}.layers.1", f"block.{idx - 1}.conv_t1")

            new_key = new_key.replace("layers.0.beta", "snake1.beta")
            new_key = new_key.replace("layers.0.alpha", "snake1.alpha")
            new_key = new_key.replace("layers.2.beta", "snake2.beta")
            new_key = new_key.replace("layers.2.alpha", "snake2.alpha")
            new_key = new_key.replace("layers.1.bias", "conv1.bias")
            new_key = new_key.replace("layers.1.weight_", "conv1.weight_")
            new_key = new_key.replace("layers.3.bias", "conv2.bias")
            new_key = new_key.replace("layers.3.weight_", "conv2.weight_")

            if idx == num_autoencoder_layers + 1:
                new_key = new_key.replace(f"block.{idx - 1}", "snake1")
            elif idx == num_autoencoder_layers + 2:
                new_key = new_key.replace(f"block.{idx - 1}", "conv2")

        value = autoencoder_state_dict.pop(key)
        if "snake" in new_key:
            value = value.unsqueeze(0).unsqueeze(-1)
        autoencoder_state_dict[new_key] = value

    return model_state_dict, projection_model_state_dict, autoencoder_state_dict


def main():
    model_folder = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.expanduser("~/Library/T5ynth/models/stable-audio-open-small")
    save_dir = sys.argv[2] if len(sys.argv) > 2 else model_folder + "-diffusers"

    checkpoint_path = os.path.join(model_folder, "model.safetensors")
    config_path = os.path.join(model_folder, "model_config.json")

    print(f"Converting {model_folder} → {save_dir}")

    with open(config_path) as f:
        config_dict = json.load(f)

    conditioning_dict = {
        c["id"]: c["config"] for c in config_dict["model"]["conditioning"]["configs"]
    }
    t5_config = conditioning_dict["prompt"]

    # Load T5 text encoder
    print(f"Loading T5 encoder: {t5_config['t5_model_name']}...")
    text_encoder = T5EncoderModel.from_pretrained(t5_config["t5_model_name"])
    tokenizer = AutoTokenizer.from_pretrained(
        t5_config["t5_model_name"], truncation=True,
        model_max_length=t5_config["max_length"]
    )

    # Scheduler — cosine DPM-Solver for diffusers compatibility
    scheduler = CosineDPMSolverMultistepScheduler(
        sigma_min=0.3, sigma_max=500, solver_order=2,
        prediction_type="v_prediction", sigma_data=1.0,
        sigma_schedule="exponential",
    )

    # Load and convert state dict
    print("Loading checkpoint...")
    orig_sd = load_file(checkpoint_path, device="cpu")
    model_sd, proj_sd, vae_sd = convert_state_dict(orig_sd)

    # Projection model — small model only has seconds_total (no seconds_start)
    # Use seconds_total config for both min/max
    time_config = conditioning_dict["seconds_total"]
    projection_model = StableAudioProjectionModel(
        text_encoder_dim=text_encoder.config.d_model,
        conditioning_dim=config_dict["model"]["conditioning"]["cond_dim"],
        min_value=time_config["min_val"],
        max_value=time_config["max_val"],
    )
    # Load what we have; missing start_number_conditioner keys will stay at init
    missing, unexpected = projection_model.load_state_dict(proj_sd, strict=False)
    if missing:
        print(f"  Projection model — missing keys (expected for small model): {missing}")

    # DiT transformer
    dit_config = config_dict["model"]["diffusion"]["config"]
    attention_head_dim = dit_config["embed_dim"] // dit_config["num_heads"]
    model = StableAudioDiTModel(
        sample_size=int(config_dict["sample_size"]) / int(config_dict["model"]["pretransform"]["config"]["downsampling_ratio"]),
        in_channels=dit_config["io_channels"],
        num_layers=dit_config["depth"],
        attention_head_dim=attention_head_dim,
        num_key_value_attention_heads=dit_config["cond_token_dim"] // attention_head_dim,
        num_attention_heads=dit_config["num_heads"],
        out_channels=dit_config["io_channels"],
        cross_attention_dim=dit_config["cond_token_dim"],
        time_proj_dim=256,
        global_states_input_dim=dit_config["global_cond_dim"],
        cross_attention_input_dim=dit_config["cond_token_dim"],
    )
    model.load_state_dict(model_sd)
    print("  DiT loaded OK")

    # Oobleck VAE
    vae_config = config_dict["model"]["pretransform"]["config"]
    autoencoder = AutoencoderOobleck(
        encoder_hidden_size=vae_config["encoder"]["config"]["channels"],
        downsampling_ratios=vae_config["encoder"]["config"]["strides"],
        decoder_channels=vae_config["decoder"]["config"]["channels"],
        decoder_input_channels=vae_config["decoder"]["config"]["latent_dim"],
        audio_channels=vae_config["io_channels"],
        channel_multiples=vae_config["encoder"]["config"]["c_mults"],
        sampling_rate=config_dict["sample_rate"],
    )
    autoencoder.load_state_dict(vae_sd)
    print("  VAE loaded OK")

    # Assemble pipeline
    pipeline = StableAudioPipeline(
        transformer=model,
        tokenizer=tokenizer,
        text_encoder=text_encoder,
        scheduler=scheduler,
        vae=autoencoder,
        projection_model=projection_model,
    )

    print(f"Saving to {save_dir}...")
    pipeline.save_pretrained(save_dir)
    print("Done!")


if __name__ == "__main__":
    main()
