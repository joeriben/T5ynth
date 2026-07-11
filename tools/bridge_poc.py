#!/usr/bin/env python3
"""
Bridge PoC — Stufe 1: das SUBSTRAT beweisen (noch OHNE LLM).

Frage, die dieser Prototyp am Ohr beantwortet:
  Trägt "semantische Brücke -> lebender additiver Wavetable"?
  D.h.: reicht es, jedes Konzept ("glassy", "warm", "dirty", "opens") als
  Bauvorschlag in Tool-Sprache zu schreiben, diese Vorschläge zu KOMBINIEREN,
  und daraus einen 256-Frame-Table zu bauen, der (a) lebt statt 256x dasselbe
  zu sein und (b) klingt wie das Wort?

Was hier NICHT passiert: das kleine LLM. Hier wähle ICH die Brücken pro
Konstruktion von Hand. Erst wenn das Substrat am Ohr trägt, kommt Qwen als
Kombinierer/Pruner obendrauf (Stufe 2).

Eine Brücke ist eine Funktion, die das additive Spektrum eines Frames
transformiert — abhängig von der Frame-Position p in [0,1]. Genau diese
p-Abhängigkeit ist, was den Table leben lässt: die Frames unterscheiden sich,
also erzeugt das Scannen Bewegung.

Abhängigkeitsordnung (der Punkt aus dem Gespräch): base -> character -> motion
-> analog-drift. Verzerrung (dirty) ist ein character NACH der Grundwave.

Ausnahme wie besprochen: eine nackte geometrische Basis (saw/square/tri) ohne
character/motion bekommt KEINEN Drift -> bleibt statisch. Alles andere lebt.
"""
import sys
import wave
import hashlib
from pathlib import Path
import numpy as np

FRAME_SIZE = 2048          # Serum-Standard: 2048 Samples pro Einzelzyklus
NUM_FRAMES = 256           # Serum-Standard: 256 Frames pro Table
MAX_H      = 64            # additive Partials (reicht fuer Charakter, billig)
SR         = 44100
OUT_DIR    = Path("/Users/joerissen/ai/t5ynth/tools/bridge_poc_out")

H = np.arange(MAX_H + 1)   # 0..MAX_H ; Index 0 ungenutzt (DC)

# Deterministische Per-Partial-Zufallswerte fuer den Analog-Drift und die
# "dirty"-Koernung. Fixer Seed -> gleiche Konstruktion baut denselben Table.
_rng = np.random.default_rng(1975)
DRIFT_AMT = _rng.standard_normal(MAX_H + 1) * 0.5     # wie stark jede Partial-Phase wandert
DRIFT_OFF = _rng.random(MAX_H + 1)                    # Phasen-Offset der Wanderung
GRIT      = _rng.standard_normal(MAX_H + 1)           # dirty-Koernung pro Partial
GRIT_OFF  = _rng.random(MAX_H + 1)


# ─── Die Bruecken ──────────────────────────────────────────────────────────
# Jede Bruecke mutiert (amps, phases) in place, abhaengig von p. Kategorie
# steuert die Reihenfolge. combines_with/against sind (fuer Stufe 2) die
# Affinitaeten, die das LLM zum Kombinieren nutzt — hier nur dokumentiert.

def base_saw(a, ph, p):
    a[1:] = 1.0 / H[1:]                      # volle Reihe, 1/h

def base_square(a, ph, p):
    odd = (H % 2 == 1) & (H >= 1)
    a[odd] = 1.0 / H[odd]                     # nur ungerade, 1/h

def base_sine(a, ph, p):
    a[1] = 1.0

def char_warm(a, ph, p):
    # Tiefpass-Neigung: starke Grundtoene, sanfte Hoehen. Kaum p-abhaengig
    # (warm ist ein ruhiger Charakter), nur ein leises Atmen.
    a[1:] *= 1.0 / (1.0 + (H[1:] / 5.0) ** 1.6)
    a[1] *= 1.2; a[2] *= 1.1
    breathe = 1.0 + 0.04 * np.sin(2 * np.pi * p)   # minimale Bewegung
    a[1:] *= breathe

def char_bright(a, ph, p):
    a[1:] *= (1.0 + 0.10 * H[1:])            # Spektrum nach oben kippen

def char_hollow(a, ph, p):
    even = (H % 2 == 0)
    a[even] *= 0.25                          # gerade Harmonische raus -> hohl/nasal

def char_glassy(a, ph, p):
    # hell, bruechig, leicht inharmonisch, und ueber die Frames schimmernd.
    a[2:5] *= 0.35                                        # untere Mitten ausduennen
    hi = H >= 6
    a[hi] *= (1.0 + 0.12 * (H[hi] - 5))                  # Hoehen betonen
    # inharmonischer Schimmer-Cluster h=9..13, wandert mit p -> "lebendiges Glas"
    cl = (H >= 9) & (H <= 13)
    a[cl] += 0.35 * (0.5 + 0.5 * np.sin(2 * np.pi * (3.0 * p + 0.6 * H[cl])))
    # sauber: kein drive (glassy ist das Gegenteil von dirty)

def char_dirty(a, ph, p):
    # Drive-artige Anreicherung: Energie nach oben spreizen (Saturation-Gefuehl)
    # + koernige, ueber die Frames wackelnde Hoehen.
    add = np.zeros_like(a)
    src = np.clip((H + 1) // 2, 1, MAX_H)
    add[2:49] = 0.25 * a[src[2:49]]                      # obere Harmonische aus unteren
    a += add
    hi = H >= 4
    a[hi] *= (1.0 + 0.30 * GRIT[hi] * np.sin(2 * np.pi * (5.0 * p + GRIT_OFF[hi])))
    np.abs(a, out=a)                                      # Betrag: keine negativen Amplituden

def motion_opens(a, ph, p):
    # p-abhaengiger Tiefpass, dessen Cutoff mit p aufmacht: dunkel -> offen.
    cutoff_h = 3.0 + p * 45.0
    a[1:] *= 1.0 / (1.0 + (H[1:] / cutoff_h) ** 3)

BRIDGES = {
    # name          category   fn            combines_with / against  (fuer Stufe 2)
    "saw":     ("base", base_saw,    {}),
    "square":  ("base", base_square, {}),
    "sine":    ("base", base_sine,   {}),
    "warm":    ("char", char_warm,   {"with": ["pad", "soft"],    "against": ["bright", "glassy", "dirty"]}),
    "bright":  ("char", char_bright, {"with": ["glassy", "airy"], "against": ["warm", "dark"]}),
    "hollow":  ("char", char_hollow, {"with": ["reed", "square"], "against": ["full"]}),
    "glassy":  ("char", char_glassy, {"with": ["bright", "bell"], "against": ["warm", "dirty"]}),
    "dirty":   ("char", char_dirty,  {"with": ["saw", "aggressive"], "against": ["glassy", "clean"]}),
    "opens":   ("motion", motion_opens, {"with": ["pad", "sweep"], "against": ["static"]}),
}


def _apply_drift(ph, p):
    """Analog-Leben: winzige, deterministische Per-Partial-Phasenwanderung ueber
    die Frame-Achse. Das ist die 'kleine Frequenz-/Timbre-Abweichung zwischen den
    Waves', die aus 256 Kopien einen lebenden Table macht."""
    ph[2:] += 0.15 * DRIFT_AMT[2:] * np.sin(2 * np.pi * (p + DRIFT_OFF[2:]))


def build_table(construction):
    """construction = Liste von Brueckennamen. Baut (NUM_FRAMES, FRAME_SIZE)."""
    ordered = (
        [b for b in construction if BRIDGES[b][0] == "base"]
        + [b for b in construction if BRIDGES[b][0] == "char"]
        + [b for b in construction if BRIDGES[b][0] == "motion"]
    )
    is_bare = all(BRIDGES[b][0] == "base" for b in construction)  # geom. Ausnahme

    n = np.arange(FRAME_SIZE)
    x = 2 * np.pi * n / FRAME_SIZE
    table = np.zeros((NUM_FRAMES, FRAME_SIZE), dtype=np.float64)

    for i in range(NUM_FRAMES):
        p = i / (NUM_FRAMES - 1)
        a = np.zeros(MAX_H + 1)
        ph = np.zeros(MAX_H + 1)
        for b in ordered:
            BRIDGES[b][1](a, ph, p)
        if not is_bare:
            _apply_drift(ph, p)
        angle = np.outer(H, x) + ph[:, None]             # (H+1, FRAME_SIZE)
        cyc = (a[:, None] * np.sin(angle)).sum(axis=0)
        cyc -= cyc.mean()                                # DC raus
        table[i] = cyc

    peak = np.max(np.abs(table))
    if peak > 1e-9:
        table *= 0.9 / peak
    return table


def frame_liveness(table):
    """Objektiv: mittlere Frame-zu-Frame-Differenz. ~0 => tote Kopien."""
    d = np.abs(np.diff(table, axis=0)).mean()
    return float(d)


def scan_render(table, f0=110.0, dur=3.0, scan="sweep"):
    """Table am Ohr: Oszillator bei f0, Scan-Position ueber die Note. 'sweep'
    faehrt p 0->1 (man HOERT die Entwicklung), 'static' haelt Frame 0 (Kontrast:
    genau der aktuelle 128-identische-Frames-Zustand)."""
    N = int(dur * SR)
    n = np.arange(N)
    pos = (n / (N - 1)) if scan == "sweep" else np.zeros(N)
    osc = (n * f0 / SR) % 1.0
    samp = osc * FRAME_SIZE
    fr = pos * (NUM_FRAMES - 1)

    f0i = np.floor(fr).astype(int)
    f1i = np.minimum(f0i + 1, NUM_FRAMES - 1)
    ff = fr - f0i
    s0 = np.floor(samp).astype(int) % FRAME_SIZE
    s1 = (s0 + 1) % FRAME_SIZE
    sf = samp - np.floor(samp)

    def bilerp(fi):
        return table[fi, s0] * (1 - sf) + table[fi, s1] * sf
    out = bilerp(f0i) * (1 - ff) + bilerp(f1i) * ff

    env = np.ones(N)
    fade = int(0.01 * SR)
    env[:fade] = np.linspace(0, 1, fade)
    env[-fade:] = np.linspace(1, 0, fade)
    return (out * env * 0.9).astype(np.float32)


def write_wav(path, audio):
    pcm = (np.clip(audio, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())


# ─── Was wir rendern ───────────────────────────────────────────────────────
# Von Hand kombinierte Konstruktionen (Stufe 2 macht das Qwen). Ziel: hoeren,
# ob Kombination + Leben tragen — und der Kontrolltest, dass die nackte Basis
# statisch bleibt.
CONSTRUCTIONS = [
    ("01_saw_bare",     ["saw"],                       "sweep"),   # Kontrolle: statische Ausnahme
    ("02_glassy",       ["saw", "glassy"],             "sweep"),
    ("03_warm_pad",     ["saw", "warm", "opens"],      "sweep"),
    ("04_dirty_open",   ["saw", "dirty", "opens"],     "sweep"),   # "dirty saw der aufmacht"
    ("05_glassy_warm",  ["saw", "glassy", "warm"],     "sweep"),   # Kombinations-Spannung
    ("06_hollow_bright",["square", "hollow", "bright"],"sweep"),
    ("07_dirty_open_STATIC", ["saw", "dirty", "opens"],"static"),  # Kontrast: eingefroren
]


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"{'construction':22} {'bridges':32} liveness  scan")
    print("-" * 78)
    for name, construction, scan in CONSTRUCTIONS:
        table = build_table(construction)
        live = frame_liveness(table)
        audio = scan_render(table, scan=scan)
        path = OUT_DIR / f"{name}.wav"
        write_wav(path, audio)
        print(f"{name:22} {'+'.join(construction):32} {live:7.5f}  {scan}")
    print(f"\nWAVs -> {OUT_DIR}")


if __name__ == "__main__":
    main()
