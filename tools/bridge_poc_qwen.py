#!/usr/bin/env python3
"""
Bridge PoC — Stufe 2: der KOMBINIERER (Qwen).

Stufe 1 (tools/bridge_poc.py) hat bewiesen: kombinierte additive Bruecken ->
lebender Table. Dort habe ICH die Bruecken von Hand gewaehlt. Hier macht es das
kleine LLM.

Die These, die das hier prueft, ist NICHT "findet Qwen das Wort 'glassy' im
Prompt" (das koennte der 1995-Scanner auch). Sondern: bildet es Woerter, die
GAR KEINE Bruecke sind — "luminous", "brittle", "aggressive", "muddy" — ueber
die BEDEUTUNG auf die richtige Bruecke ab und kombiniert sie? Das ist der ganze
Unterschied zum toten Lexikon.

Klein gehalten (wie besprochen): Qwen waehlt eine MENGE Bruecken + eine Basis,
der Executor kombiniert gleichgewichtig. Gewichte / Frame-Regionen kommen erst,
wenn das Auswaehlen-per-Bedeutung am Ohr traegt.

Transport: run_instruct wird hier DIREKT importiert (nicht ueber den IPC-Pfad).
Bewusst — die Bruecken-Auswahl ist eine Faehigkeit, die der Shipping-Backend
(mode:dco) gar nicht hat; es gibt also keinen IPC-Pfad zu testen. Gleiches
Muster wie tools/... Verifikationsproben. Wird das echt, bekommt es einen
eigenen Mode + Tests.
"""
import sys
import re
from pathlib import Path

sys.path.insert(0, "/Users/joerissen/ai/t5ynth/tools")
sys.path.insert(0, "/Users/joerissen/ai/t5ynth/backend")
import bridge_poc as BP
import pipe_inference as P

# name -> (kategorie, ein-Zeilen-Bedeutung). Die Bedeutung ist die semantische
# Bruecke: das, woran Qwen ein Prompt-Wort abgleicht. Namen muessen mit
# bridge_poc.BRIDGES uebereinstimmen.
CATALOG = {
    "saw":    ("BASE", "full bright buzzy harmonics, the classic rich analog tone"),
    "square": ("BASE", "hollow woody reedy tone, only odd harmonics"),
    "sine":   ("BASE", "pure clean fundamental, no overtones, soft"),
    "warm":   ("CHARACTER", "warm dark round mellow soft, gentle highs rolled off"),
    "bright": ("CHARACTER", "bright sharp present airy, tilted toward the highs"),
    "hollow": ("CHARACTER", "hollow nasal reedy, even harmonics removed"),
    "glassy": ("CHARACTER", "glassy bright brittle crystalline luminous bell-like shimmer"),
    "dirty":  ("CHARACTER", "dirty distorted gritty aggressive saturated screaming"),
    "opens":  ("MOTION", "the timbre opens up and evolves over time, a filter sweep"),
}
VALID = set(CATALOG)


def _catalog_text():
    lines = []
    for cat in ("BASE", "CHARACTER", "MOTION"):
        members = [f"{n} ({d})" for n, (c, d) in CATALOG.items() if c == cat]
        lines.append(f"{cat}: " + "; ".join(members))
    return "\n".join(lines)


SYS = (
    "You are a sound designer. You build a sound by combining bridges from a "
    "catalog; each bridge shapes the tone. Given a prompt, pick exactly ONE base "
    "and any number of character/motion bridges whose MEANING matches the prompt "
    "-- match by meaning, not by exact word. Reply in EXACTLY two lines:\n"
    "BASE: <name>\nBRIDGES: <name>, <name>, ...\n"
    "Use only names from the catalog. Empty BRIDGES if none fit. No other text."
)


def select(prompt, model_dir, device):
    """Qwen -> (base, [bridge names]). Only catalog names survive; unknown or
    junk tokens are dropped, never invented into the construction."""
    user = _catalog_text() + f"\n\nPrompt: {prompt}"
    raw = P.run_instruct(user, model_dir, device, SYS, max_new_tokens=64)
    base, chars = "saw", []
    for line in raw.splitlines():
        m = re.match(r"\s*BASE:\s*(.+)", line, re.I)
        if m:
            cand = m.group(1).strip().lower().split()[0].strip(",.")
            if cand in VALID and CATALOG[cand][0] == "BASE":
                base = cand
        m = re.match(r"\s*BRIDGES:\s*(.+)", line, re.I)
        if m:
            for tok in re.split(r"[,\s]+", m.group(1).strip().lower()):
                tok = tok.strip(",.")
                if tok in VALID and CATALOG[tok][0] in ("CHARACTER", "MOTION") and tok not in chars:
                    chars.append(tok)
    return base, chars, raw.strip()


# Prompts voller Woerter, die KEINE Bruecke sind -> testet Bedeutungs-Matching.
PROMPTS = [
    "warm analog pad that slowly opens up",
    "luminous crystalline bell",
    "cold aggressive metallic stab",
    "brittle glass shimmer",
    "muddy dark drone",
    "hollow woody clarinet",
    "screaming bright lead",
    "soft mellow flute",
]


def main():
    mdir = P._resolve_translation_model_dir({})
    dev = "mps"
    BP.OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"translator: {mdir}\n")
    print(f"{'prompt':38} {'base':7} {'Qwen picked (by meaning)'}")
    print("-" * 92)
    for i, prompt in enumerate(PROMPTS, 1):
        base, chars, raw = select(prompt, mdir, dev)
        construction = [base] + chars
        table = BP.build_table(construction)
        audio = BP.scan_render(table, scan="sweep")
        name = f"qwen_{i:02d}_" + re.sub(r"[^a-z]+", "_", prompt.lower())[:24].strip("_")
        BP.write_wav(BP.OUT_DIR / f"{name}.wav", audio)
        live = BP.frame_liveness(table)
        print(f"{prompt:38} {base:7} {'+'.join(chars) if chars else '(none)':32} live={live:.4f}")
        print(f"    RAW: {raw!r}")
    print(f"\nWAVs -> {BP.OUT_DIR}  (qwen_*.wav)")


if __name__ == "__main__":
    main()
