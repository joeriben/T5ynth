"""Does the real 7B map natural-language / metaphor prompts onto the discrete
anchor words of the parametrised LCO instruments -- drum_head
(pitched/spot/tension/damping) -- and in the right DIRECTION?

It probed `fm_ep` (ting/ring/reed/strike) too until 2026-07-31, when BJ struck that
entry; those seven cases went with it rather than being retargeted, because the
prompts name an electric piano and no entry claims one.

This is the empirical answer to "how is a 7B supposed to map metaphors onto
spot=centre etc.": it exercises the ACTUAL wire the plugin drives (mode=csound
over the pipe_inference IPC subprocess, project rule) and dumps the structured
`oscillators` field, so the per-key params the model REALLY set are visible --
not the lossy display gloss, and not a fabricated in-process demo.

Reading the output: an oscillator's `params` is non-empty only when the model
emitted the `key(name=anchor)` syntax and it parsed. A bare `params:{}` on a
parametrised key means the descriptive words did NOT become parameters (they
landed as adjectives or as a prepended oscillator instead), or the prompt routed
to a non-parametrised key entirely (rhodes / clarinet / pulse have no params).

Run:  .venv/bin/python tools/lco_param_map_probe.py
Writes tools/lco_param_map_out/pmap_results.json (untracked, like every *_out).
"""
import json, struct, subprocess, os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "tools", "lco_param_map_out")
os.makedirs(OUT, exist_ok=True)


def read_exact(f, n):
    buf = b""
    while len(buf) < n:
        c = f.read(n - len(buf))
        if not c:
            raise RuntimeError("EOF")
        buf += c
    return buf


p = subprocess.Popen([os.path.join(REPO, ".venv/bin/python"), "-u",
                      os.path.join(REPO, "backend/pipe_inference.py")],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=open(os.path.join(OUT, "stderr.log"), "wb"),
                     cwd=os.path.join(REPO, "backend"))
assert read_exact(p.stdout, 1) == b"\x02"
n = struct.unpack("<H", read_exact(p.stdout, 2))[0]
read_exact(p.stdout, n)
print("READY", flush=True)


def call(payload):
    p.stdin.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
    p.stdin.flush()
    h = read_exact(p.stdout, 1)
    ln = struct.unpack("<I", read_exact(p.stdout, 4))[0]
    body = read_exact(p.stdout, ln).decode("utf-8", "replace")
    if h == b"\x00":
        return {"ok": False, "error": "FRAME " + body}
    return json.loads(body)


# (prompt, instrument we HOPE it routes to, difficulty tier, the axis under test)
CASES = [
    # ---- drum_head : pitched / spot / tension / damping ---------------------
    ("a dead muffled tom, no ring",                    "drum_head","direct",  "damping->muffled pitched->tom"),
    ("a bright tight singing timpani",                 "drum_head","direct",  "tension->tight pitched->timpani"),
    ("a thin hard edgy rimshot drum",                  "drum_head","spot!",   "spot->rim"),
    ("a deep round drum hit dead in the centre",       "drum_head","spot!",   "spot->centre"),
    ("a boomy slack floor tom ringing wide open",      "drum_head","metaphor","tension->slack damping->open"),
    ("a drum that thuds like a heartbeat",             "drum_head","metaphor","tom, dull, damped"),
    ("a drum like knocking on a cardboard box",        "drum_head","far",     "muffled tom"),
    ("a sharp cracking snare-like crack near the edge","drum_head","far",     "spot->rim tension->tight"),
]

results = []
for prompt, want, tier, axis in CASES:
    r = call({"mode": "csound", "text": prompt})
    oscs = r.get("oscillators")
    rec = {"prompt": prompt, "want": want, "tier": tier, "axis": axis,
           "ok": r.get("ok"), "reading": r.get("reading"), "oscillators": oscs}
    results.append(rec)
    print(f"\n[{tier:8}] {prompt}", flush=True)
    print(f"   want   : {want}  |  {axis}", flush=True)
    print(f"   reading: {rec['reading']}", flush=True)
    print(f"   oscs   : {json.dumps(oscs, separators=(',',':')) if oscs is not None else None}", flush=True)

with open(os.path.join(OUT, "pmap_results.json"), "w") as f:
    json.dump(results, f, indent=2)
set_count = sum(1 for r in results
                if any((o.get("params") or {}) for o in (r["oscillators"] or [])))
print(f"\nparams set on {set_count}/{len(results)} prompts. WROTE {OUT}/pmap_results.json", flush=True)

p.stdin.close()
p.terminate()
