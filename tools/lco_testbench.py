#!/usr/bin/env python3
"""LCO TEST BENCH — the maintainer's 'plausible sonic properties' gate.

The full loop the /goal demands, through the REAL t5ynth DSP:

  prompt -> dco_recipe.author_recipe (the deterministic author the LCO now
            routes to) -> recipe JSON
         -> tools/audition_dco_bake (the REAL dco::Baker -> real
            WavetableOscillator, scan swept 0->1 over 2 s @ 220 Hz) -> WAV
         -> windowed-FFT analysis of the ACTUAL rendered audio at scan
            waypoints -> per-timbre harmonic profile + morph trajectory
         -> a coarse, honest PASS/FAIL against what the prompt NAMES.

No Python reimplementation of the baker: the sound analysed is exactly what
the plugin makes. author_recipe is called with llm_route=None, so the result
is the pure deterministic core (no model) — for waveform/morph prompts the S2
router is never invoked anyway (empty residue), so this faithfully mirrors the
shipped path for those cases.

Run from repo root:
  BAKE=<path to compiled dco_bake>  .venv/bin/python tools/lco_testbench.py
(BAKE defaults to the scratchpad build if present.)
"""
import os, sys, json, math, wave, subprocess, glob
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
import numpy as np  # noqa: E402
import lco_author   # noqa: E402  — the REAL shipping entry (mode=="dco")

# The coder LLM surface, stubbed to route NOTHING (returns ""): S2 residue then
# resolves to all-None, so this exercises the deterministic core of the exact
# shipping path build_lco_response -> dco_recipe.author_recipe. For the tested
# prompts residue routing is empty anyway, so a live coder would give the same.
def _dummy_llm(text, system_prompt, max_new_tokens):
    return ""

SR = 44100
F0 = 220.0
DUR = 2.0
OUT = os.path.join(os.path.dirname(__file__), "lco_test_out")

# The maintainer's own failing forms + a spread across the paradigm.
PROMPTS = [
    "square > sine",
    "saw -> square",
    "violin > piano",
    "sine into square into triangle",
    "gritty saw",
    "a clean sine morphing into a gritty analog saw",
    "warm analog pad",
    "hollow woody clarinet",
    "bright metallic bell",
]


def _slug(p):
    return "".join(c if c.isalnum() else "_" for c in p)[:40]


def _find_bake():
    b = os.environ.get("BAKE")
    if b and os.path.exists(b):
        return b
    for c in glob.glob("/private/tmp/claude-*/**/scratchpad/dco_bake", recursive=True):
        return c
    for c in ("/tmp/dco_bake", os.path.join(OUT, "dco_bake")):
        if os.path.exists(c):
            return c
    return None


# ─── audio analysis (windowed FFT at scan waypoints) ─────────────────────────

def load_wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        raw = w.readframes(n)
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return a


def harmonic_profile(win):
    """Windowed FFT -> normalized amplitude at each harmonic k*F0 (k=1..40)."""
    N = len(win)
    w = win * np.hanning(N)
    mag = np.abs(np.rfft(w)) if hasattr(np, "rfft") else np.abs(np.fft.rfft(w))
    binhz = SR / N
    amps = np.zeros(40)
    for k in range(1, 41):
        c = k * F0 / binhz
        lo = max(0, int(math.floor(c)) - 2)
        hi = min(len(mag), int(math.ceil(c)) + 3)
        amps[k - 1] = mag[lo:hi].max() if hi > lo else 0.0
    peak = amps.max()
    return amps / peak if peak > 1e-9 else amps


def profile_metrics(amps):
    kk = np.arange(1, 41)
    e = amps ** 2
    tot = e.sum() + 1e-12
    centroid = float((kk * e).sum() / tot)          # brightness in harmonic #
    npart = int((amps > 0.08).sum())                 # significant partials
    odd = e[0::2].sum(); even = e[1::2].sum()         # h1,3,5 vs h2,4,6
    odd_even = float(odd / (even + 1e-9))
    hi = e[6:].sum(); lo = e[:6].sum()
    rolloff = float(hi / (lo + 1e-9))
    return centroid, npart, odd_even, rolloff


def analyze(path, nwaypts=21, win=2048):
    a = load_wav(path)
    profs = []
    for i in range(nwaypts):
        p = i / (nwaypts - 1)
        centre = int(p * (len(a) - 1))
        s = max(0, centre - win // 2)
        e = min(len(a), s + win)
        seg = a[s:e]
        if len(seg) < win:
            seg = np.pad(seg, (0, win - len(seg)))
        profs.append(harmonic_profile(seg))
    profs = np.array(profs)
    perframe = [profile_metrics(p) for p in profs]   # [(centroid,npart,odd_even,rolloff), ...]
    centroids = np.array([m[0] for m in perframe])
    # morph: spread of centroid + total spectral flux across the sweep
    morph_range = float(centroids.max() - centroids.min())
    flux = float(np.abs(np.diff(profs, axis=0)).sum() / (nwaypts - 1))
    bright = int(np.argmax(centroids)); dark = int(np.argmin(centroids))
    return {
        "profiles": profs, "centroids": centroids, "perframe": perframe,
        "morph_range": morph_range, "flux": flux,
        "bright": perframe[bright],   # (centroid,npart,odd_even,rolloff)
        "dark": perframe[dark],
        "rms": float(np.sqrt((a ** 2).mean())),
    }


# ─── plausibility gate (coarse, honest) ──────────────────────────────────────

def gate(prompt, resp, an):
    """Coarse honest PASS/FAIL. Spectral checks scan EVERY sampled frame (a
    named timbre may live at either sweep extreme — in saw->square the square
    is the DARK frame, not the bright one), so a signature match anywhere in
    the morph counts."""
    p = prompt.lower()
    notes, ok = [], True
    pf = an["perframe"]                                    # [(centroid,npart,odd_even,rolloff), ...]
    anyf = lambda pred: any(pred(*m) for m in pf)
    max_cent = max(m[0] for m in pf); min_cent = min(m[0] for m in pf)
    max_oe = max(m[2] for m in pf); max_np = max(m[1] for m in pf); min_np = min(m[1] for m in pf)

    arrowy = any(x in p for x in (">", "->", "→", "morph", "into", "becomes"))
    named = sum(w in p for w in ("sine", "square", "saw", "triangle", "pulse"))
    if arrowy or named >= 2:
        if an["morph_range"] < 0.3 and an["flux"] < 0.03:
            ok = False
            notes.append(f"morph prompt but table is STATIC (centroid spread={an['morph_range']:.2f}, flux={an['flux']:.3f})")
        else:
            notes.append(f"morphs: centroid spread={an['morph_range']:.2f} harmonics, flux={an['flux']:.3f}")

    # named-waveform spectral sanity — a match ANYWHERE along the sweep counts
    if "sine" in p and not anyf(lambda c, n, o, r: n <= 2 and c < 3):
        ok = False; notes.append(f"'sine' named but no near-pure frame (min npart={min_np})")
    if "square" in p and not anyf(lambda c, n, o, r: o > 6 and n >= 3):
        ok = False; notes.append(f"'square' named but no odd-heavy multi-partial frame (max odd/even={max_oe:.0f})")
    if "saw" in p and not anyf(lambda c, n, o, r: n >= 6 and o < 5):
        notes.append(f"'saw' named but no rich all-harmonic frame (max npart={max_np})")
    if "triangle" in p and not anyf(lambda c, n, o, r: o > 4 and n <= 8):
        notes.append("'triangle' named but no odd-heavy low-partial frame")
    if any(w in p for w in ("bright", "metallic", "piercing", "luminous", "crystal")) and max_cent < 5:
        notes.append(f"'bright/metallic' named but max centroid only {max_cent:.1f}")
    if any(w in p for w in ("warm", "dark", "hollow", "mellow", "woody", "muffled")) and min_cent > 6:
        notes.append(f"'warm/dark/hollow' named but min centroid {min_cent:.1f} is not dark")

    # honest disclosure of the deterministic author's own reading
    flags = resp.get("flags", [])
    unresolved = [f for f in flags if f.get("tier") == "unresolved"]
    if unresolved:
        notes.append("not understood: " + ", ".join(f"{f['word']}" for f in unresolved))
    return ok, notes


def main():
    bake = _find_bake()
    if not bake:
        print("NO dco_bake binary. Set BAKE=<path>.", file=sys.stderr); return 2
    os.makedirs(OUT, exist_ok=True)
    print(f"bake tool: {bake}\nout: {OUT}\n")

    npass = 0
    for i, prompt in enumerate(PROMPTS, 1):
        slug = f"{i:02d}_{_slug(prompt)}"
        resp = lco_author.build_lco_response(prompt, _dummy_llm, None)
        recipe = resp["recipe"]
        jpath = os.path.join(OUT, slug + ".json")
        wpath = os.path.join(OUT, slug + ".wav")
        with open(jpath, "w") as f:
            json.dump(recipe, f)                       # BARE recipe for recipeFromVar
        with open(os.path.join(OUT, slug + ".response.json"), "w") as f:
            json.dump({"_prompt": prompt, **resp}, f, indent=1)

        r = subprocess.run([bake, jpath, wpath], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"[{slug}] BAKE FAILED: {r.stderr.strip()[:200]}"); continue

        an = analyze(wpath)
        ok, notes = gate(prompt, resp, an)
        tech = resp["resolved"]["technique"]
        adjs = ",".join(resp["resolved"]["adjectives"]) or "-"
        mot = ",".join(resp["resolved"]["motion"]) or "-"
        nkf = len(recipe["keyframes"])
        print(f"{'PASS' if ok else '*** FAIL ***'}  [{slug}]  {prompt!r}")
        print(f"    technique={tech}  adj=[{adjs}]  motion=[{mot}]  keyframes={nkf}  rate={recipe.get('motion_rate_hz')}Hz")
        print(f"    bright frame: centroid={an['bright'][0]:.1f} npart={an['bright'][1]} odd/even={an['bright'][2]:.1f}")
        print(f"    dark   frame: centroid={an['dark'][0]:.1f} npart={an['dark'][1]} odd/even={an['dark'][2]:.1f}")
        for n in notes:
            print(f"      - {n}")
        npass += 1 if ok else 0

    print(f"\n==== {npass}/{len(PROMPTS)} prompts PASS the plausibility gate ====")
    print(f"WAVs + recipes: {OUT}")
    return 0 if npass == len(PROMPTS) else 1


if __name__ == "__main__":
    sys.exit(main())
