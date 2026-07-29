#!/usr/bin/env python3
"""HONEST timbre audit: does a 'metallic' key actually sound metallic, or is it a
thin high whistle?

The behavioural gate (tools/csound_keys_gate.py) certifies peak/RMS/centroid-travel/
pitchedness. NONE of those can tell a rich inharmonic bell from a single high sine:
both are bounded, both stand, both are 'pitched'. That is exactly the blind spot BJ
named ("ein pfeifender hoher Oszillator statt echter metallischer Sounds"), so this
tool measures the timbre itself and writes WAVs to listen to.

Per sound it reports:
  f0_peak    strongest partial (Hz) -- a whistle puts it far above the played note
  n_part     significant partials within 20-12000 Hz (>= -30 dB of the loudest).
             A whistle has 1-3; a real bell/metal has many.
  inharm     mean |ratio - nearest integer| of the partials vs the played note.
             ~0.00 = harmonic (organ/saw), high = inharmonic (bell, cymbal, glass)
  centroid   spectral centre of mass (Hz)
  body%      energy below 800 Hz (fundamental weight / warmth)
  high%      energy above 4 kHz (thin/whistly/hissy content)
  travel     centroid late/early ratio (does the spectrum MOVE over the note)

Usage:  .venv/bin/python tools/csound_timbre_audit.py [outdir]
"""
import os
import sys
import subprocess
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import csound_orch as co  # noqa
from csound_keys_gate import CHECK, _load  # noqa

NOTE_HZ = 220.0      # A3 -- a musical note, not an extreme
NOTE_DUR = 3.0
TOTAL = 3.4

# The families BJ questioned (metal/bell/glass) plus references that BOUND the
# reading: sine is the literal "thin whistle" baseline, saw/organ are known-rich.
CASES = [
    ("REF sine (whistle baseline)", ["sine"], []),
    ("REF saw (known rich)",        ["saw"], []),
    ("REF organ",                   ["organ"], []),
    ("fm_bell",                     ["fm_bell"], []),
    ("metallic_fm",                 ["metallic_fm"], []),
    ("struck_bar",                  ["struck_bar"], []),
    ("cymbal",                      ["cymbal"], []),
    ("glass",                       ["glass"], []),
    ("bell + metallic adj",         ["fm_bell"], ["metallic"]),
    ("bell + bright + metallic",    ["fm_bell"], ["bright", "metallic"]),
    ("saw + metallic adj",          ["saw"], ["metallic"]),
    ("saw + clangorous",            ["saw"], ["clangorous"]),
]


def render(orc, wav):
    with tempfile.NamedTemporaryFile("w", suffix=".orc", delete=False) as f:
        f.write(orc)
        orp = f.name
    try:
        r = subprocess.run(
            [CHECK, "probe", orp, wav, str(NOTE_HZ), "0.85", "0.0", "0.5",
             str(NOTE_DUR), str(TOTAL)],
            capture_output=True, text=True, timeout=60)
        return r.returncode == 0, (r.stdout + r.stderr)[-200:]
    finally:
        os.unlink(orp)


def spectrum(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    if len(seg) < 1024:
        return None, None
    seg = seg * np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg))
    freq = np.fft.rfftfreq(len(seg), 1.0 / sr)
    return freq, mag


def analyse(x, sr):
    freq, mag = spectrum(x, sr, 0.4, 1.4)
    if freq is None or mag.max() <= 0:
        return None
    band = (freq >= 20) & (freq <= 12000)
    f, m = freq[band], mag[band]
    mn = m / m.max()

    # significant partials: local maxima >= -30 dB of the loudest
    thr = 10 ** (-30 / 20.0)
    peaks = []
    for i in range(2, len(mn) - 2):
        if mn[i] >= thr and mn[i] > mn[i - 1] and mn[i] >= mn[i + 1]:
            if not peaks or f[i] - peaks[-1][0] > 25:   # 25 Hz separation
                peaks.append((float(f[i]), float(mn[i])))
    peaks.sort(key=lambda p: -p[1])
    top = peaks[:24]

    energy = float((m ** 2).sum())
    body = float((m[f < 800] ** 2).sum()) / energy * 100 if energy else 0.0
    high = float((m[f > 4000] ** 2).sum()) / energy * 100 if energy else 0.0
    centroid = float((f * m).sum() / m.sum()) if m.sum() else 0.0

    ratios = [p[0] / NOTE_HZ for p in top if p[0] > 20]
    inharm = float(np.mean([abs(r - round(r)) for r in ratios])) if ratios else 0.0

    # movement: centroid early vs late in the held note
    c = []
    for (a, b) in ((0.3, 0.9), (2.0, 2.6)):
        fr, mg = spectrum(x, sr, a, b)
        c.append(float((fr * mg).sum() / mg.sum()) if mg is not None and mg.sum() else 0.0)
    travel = (max(c) / min(c)) if min(c) > 1 else 1.0

    return {
        "f0_peak": top[0][0] if top else 0.0,
        "n_part": len(peaks),
        "inharm": inharm,
        "centroid": centroid,
        "body": body,
        "high": high,
        "travel": travel,
        "top": top[:8],
    }


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tools", "timbre_audit_out")
    os.makedirs(outdir, exist_ok=True)
    print(f"note = {NOTE_HZ:.0f} Hz (A3), {NOTE_DUR:.0f}s held\n")
    print(f"{'sound':30} {'f0pk':>7} {'npart':>5} {'inharm':>6} {'centr':>7} "
          f"{'body%':>6} {'high%':>6} {'travel':>6}")
    print("-" * 82)
    rows = []
    for label, keys, adjs in CASES:
        orc, _ = co.build_orchestra(keys, adjs, None)
        wav = os.path.join(outdir, label.replace(" ", "_").replace("+", "").replace("(", "").replace(")", "") + ".wav")
        ok, err = render(orc, wav)
        if not ok:
            print(f"{label:30} RENDER FAILED: {err.strip()[:40]}")
            continue
        x, sr = _load(wav)
        a = analyse(x, sr)
        if a is None:
            print(f"{label:30} (silent)")
            continue
        rows.append((label, a))
        print(f"{label:30} {a['f0_peak']:7.0f} {a['n_part']:5d} {a['inharm']:6.3f} "
              f"{a['centroid']:7.0f} {a['body']:6.1f} {a['high']:6.1f} {a['travel']:6.2f}")

    print("\npartials (Hz, strongest first) — ratio to the 220 Hz note in brackets:")
    for label, a in rows:
        parts = ", ".join(f"{fq:.0f}[{fq/NOTE_HZ:.2f}]" for fq, _ in a["top"])
        print(f"  {label:28} {parts}")
    print(f"\nWAVs: {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
