#!/usr/bin/env python3
"""Rebuild the SA3-medium model_config.json with the AUTHORITATIVE SAME-L autoencoder.

Root cause of the 'pure glitch': the derived medium config cloned small's SAME-S
autoencoder params (chunk_size=32 chunked attention), but medium's AE is SAME-L which
uses sliding_window=[1,1] attention + sinusoidal_blocks=[8]. Same weights, wrong
attention mask at decode time -> garbage audio. Shape-validation (997/997) can't catch
a masking-strategy difference.

This grafts stabilityai/SAME-L's encoder/decoder/bottleneck (ungated, authoritative)
into the medium config's pretransform, keeps the verified DiT, and re-validates that
all 997 tensor shapes still match the checkpoint header before writing.
"""
import json, copy, struct, gc, sys
import torch
from stable_audio_tools.models.factory import create_model_from_config

MED_DIR = "/Users/joerissen/Library/T5ynth/models/stable-audio-3-medium"
SAME_L = "/tmp/SAME-L_cfg.json"
HEADER = "/tmp/sa3_medium_header.json"
CUR_CFG = "/tmp/sa3_medium_model_config.json"  # my derived (taae_v2-cloned) config


def header_shapes(path):
    h = json.load(open(path))
    return {k: tuple(v["shape"]) for k, v in h.items()}


def localize_t5gemma(cfg):
    for c in cfg["model"]["conditioning"]["configs"]:
        if c.get("type") == "t5gemma":
            cc = c.setdefault("config", {})
            cc.pop("repo_id", None)
            cc["model_path"] = MED_DIR
            cc["subfolder"] = cc.get("subfolder", "t5gemma-b-b-ul2")
    return cfg


def built_shapes(cfg):
    model = create_model_from_config(cfg)
    out = {k: tuple(v.shape) for k, v in model.state_dict().items()}
    del model; gc.collect()
    return out


def diff(built, header, label):
    bset, hset = set(built), set(header)
    missing = sorted(hset - bset)
    extra = sorted(k for k in (bset - hset) if k.startswith(("model.model.", "pretransform.")))
    mism = sorted(k for k in (hset & bset) if built[k] != header[k])
    print(f"\n===== {label} =====")
    print(f"ckpt tensors={len(header)}  built(all)={len(built)}")
    print(f"MISSING (in ckpt, not built): {len(missing)}")
    for k in missing[:25]: print("   -", k, header[k])
    print(f"SHAPE-MISMATCH: {len(mism)}")
    for k in mism[:25]: print("   ~", k, "ckpt", header[k], "built", built[k])
    print(f"EXTRA (built, not in ckpt): {len(extra)}")
    for k in extra[:25]: print("   +", k, built[k])
    ok = not missing and not mism and not extra
    print("RESULT:", "EXACT MATCH" if ok else "MISMATCH")
    return ok


torch.set_grad_enabled(False)
med = json.load(open(CUR_CFG))
same_l = json.load(open(SAME_L))
med_hdr = header_shapes(HEADER)

# --- Graft SAME-L's autoencoder (authoritative) into medium pretransform.config ---
# SAME-L's model.* maps 1:1 onto medium's model.pretransform.config.*
ae_src = same_l["model"]
pt = med["model"]["pretransform"]
wrapper_keys = {k: pt[k] for k in ("type", "iterate_batch", "chunked", "enable_grad") if k in pt}
new_cfg = copy.deepcopy(ae_src)  # pretransform(patched), encoder, decoder, bottleneck, latent_dim, downsampling_ratio, io_channels
# preserve inference-time grad flags on encoder/decoder/bottleneck (SAME-L already has requires_grad False)
pt["config"] = new_cfg
for k, v in wrapper_keys.items():
    pt[k] = v

print("=== grafted AE encoder config ===")
print(json.dumps(med["model"]["pretransform"]["config"]["encoder"]["config"], indent=2))
print("=== grafted AE decoder config ===")
print(json.dumps(med["model"]["pretransform"]["config"]["decoder"]["config"], indent=2))

ok = diff(built_shapes(localize_t5gemma(copy.deepcopy(med))), med_hdr, "MEDIUM with SAME-L AE")

if ok:
    out_path = f"{MED_DIR}/model_config.json"
    json.dump(med, open(out_path, "w"), indent=2)
    json.dump(med, open("/tmp/sa3_medium_model_config_FIXED.json", "w"), indent=2)
    print(f"\nWROTE corrected config -> {out_path}")
else:
    print("\nNOT writing — shapes do not match; investigate before overwriting.")
    sys.exit(1)
