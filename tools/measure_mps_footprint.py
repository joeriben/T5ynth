#!/usr/bin/env python3
"""True MPS/unified-memory footprint of a native model (ps rss undercounts Metal).
Loads ONE model (argv[1] = model dir) via the real load path and prints the Metal
driver allocation after the weights are resident on MPS. Run once per model so each
measurement is from a clean process."""
import sys, json
from pathlib import Path
import torch
from stable_audio_tools.models.factory import create_model_from_config
from safetensors.torch import load_file

mdir = Path(sys.argv[1])
cfg = json.load(open(mdir / "model_config.json"))
for c in cfg["model"]["conditioning"]["configs"]:
    if c.get("type") == "t5gemma":
        cc = c.setdefault("config", {})
        cc.pop("repo_id", None)
        cc["model_path"] = str(mdir)
        cc["subfolder"] = cc.get("subfolder", "t5gemma-b-b-ul2")

torch.mps.empty_cache()
model = create_model_from_config(cfg)
model.load_state_dict(load_file(str(mdir / "model.safetensors")), strict=True)
model.eval().to("mps")
torch.mps.synchronize()

drv = torch.mps.driver_allocated_memory() / 1e9
cur = torch.mps.current_allocated_memory() / 1e9
nparams = sum(p.numel() for p in model.parameters()) / 1e9
print(f"{mdir.name}: params={nparams:.3f}B  MPS driver_allocated={drv:.2f}GB  current_allocated={cur:.2f}GB")
