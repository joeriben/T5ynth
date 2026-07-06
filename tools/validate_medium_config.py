import json, copy, re, gc, struct, traceback
import torch
from stable_audio_tools.models.factory import create_model_from_config

SMALL_DIR = "/Users/joerissen/Library/T5ynth/models/stable-audio-3-small-music"

def header_local(path):
    with open(path, 'rb') as f:
        n = struct.unpack('<Q', f.read(8))[0]
        h = json.loads(f.read(n))
    h.pop('__metadata__', None)
    return {k: tuple(v['shape']) for k, v in h.items()}

def header_json(path):
    h = json.load(open(path))
    return {k: tuple(v['shape']) for k, v in h.items()}

def localize_t5gemma(cfg):
    for c in cfg["model"]["conditioning"]["configs"]:
        if c.get("type") == "t5gemma":
            cc = c.setdefault("config", {})
            cc.pop("repo_id", None)
            cc["model_path"] = SMALL_DIR
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
    for k in missing[:20]: print("   -", k, header[k])
    print(f"SHAPE-MISMATCH: {len(mism)}")
    for k in mism[:30]: print("   ~", k, "ckpt", header[k], "built", built[k])
    print(f"EXTRA (built model/pretransform not in ckpt): {len(extra)}")
    for k in extra[:20]: print("   +", k, built[k])
    ok = not missing and not mism and not extra
    print("RESULT:", "EXACT MATCH" if ok else "MISMATCH")
    return ok

torch.set_grad_enabled(False)
small_cfg = json.load(open(f"{SMALL_DIR}/model_config.json"))
small_hdr = header_local(f"{SMALL_DIR}/model.safetensors")
med_hdr = header_json("/tmp/sa3_medium_header.json")

# --- SANITY: small config must reproduce small header exactly ---
if False:
    try:
        diff(built_shapes(localize_t5gemma(copy.deepcopy(small_cfg))), small_hdr, "SMALL sanity")
    except Exception:
        print("SMALL build FAILED:"); traceback.print_exc()

# --- MEDIUM guess ---
mc = copy.deepcopy(small_cfg)
dit = mc["model"]["diffusion"]["config"]
dit["embed_dim"] = 1536
dit["depth"] = 24
dit["num_heads"] = 24
dit.setdefault("attn_kwargs", {})["differential"] = True
ae = mc["model"]["pretransform"]["config"]
for side in ("encoder", "decoder"):
    acfg = ae[side]["config"]
    acfg["channels"] = 256  # 128->256  => width 768->1536  (GUESS)
    if "transformer_depths" in acfg:
        acfg["transformer_depths"] = [d * 2 for d in acfg["transformer_depths"]]  # 6->12
ae["decoder"]["config"]["conv_mapping"] = False  # medium decoder output conv kernel 3->1
json.dump(mc, open("/tmp/sa3_medium_model_config.json", "w"), indent=2)
try:
    diff(built_shapes(localize_t5gemma(copy.deepcopy(mc))), med_hdr, "MEDIUM guess(ch=256, tdepth*2, diff=True)")
except Exception:
    print("MEDIUM build FAILED:"); traceback.print_exc()
