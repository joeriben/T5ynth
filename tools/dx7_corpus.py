#!/usr/bin/env python3
"""What thousands of human DX7 programmers did when they named a sound.

BJ, 2026-07-31: "es sind 1000ende DX7-Presets online. baue ein skript das die
retrieved, passende 3op-konstellationen ausliest und von den Bezeichnungen auf
sonische qualitäten schließt."

THE QUESTION IT ANSWERS. `fm3` has six axes and twenty-five anchor words, and
nothing but one published study grounds any of them: Hayes & Saitis 2020 measured
`bright`, `rough` and `thick` on exactly this topology, and that is three words.
For everything else the library has prose. A patch bank is the same kind of datum
as that study and there are a hundred thousand of them: a human chose the operator
ratios and the drive, then wrote a NAME on it. Group the settings by the words in
the names and the words acquire a measured place in this oscillator's own space.

WHAT IT IS NOT. It is not a listening test and does not replace one -- these are
other people's ears on another instrument, reported through a 10-character label.
It says where a word SITS in the parameter space, not that a given rendering of
`fm3` deserves the word. `tools/lco_listening_test.py` and BJ's ear stay the gate
(docs/LCO_TEST_POLICY.md). What this can do is stop the library from guessing.

SOURCES, both fetched and checked on 2026-07-31, neither recalled:

  * The corpus: github.com/visualizersdotnl/Yamaha-DX7-patch-library, licensed
    CC0-1.0 (public domain dedication, confirmed through the GitHub API). 3728
    standard 32-voice bulk dumps came down, 28 MB, 119,296 voices. Its directory
    names are a second, independent semantic signal (`!Instruments/Brass/`,
    `!Instruments/Strings/`) that does not come from the 10-character voice name.
  * The packed bulk-dump layout: homepages.abdn.ac.uk/d.j.benson/pages/dx7/
    sysex-format.txt -- 6-byte header `F0 43 0n 09 20 00`, 4096 data bytes, 32
    voices of 128, each voice six 17-byte operator blocks in the order OP6..OP1,
    then the globals, then a 10-character name at 118..127. Validated against a
    real bank before this file was written: every field lands in its declared
    range and the names are readable.
  * The 32 algorithms: the `FmAlgorithm` table of Dexed (github.com/asb2m10/dexed,
    Source/msfa/fm_core.cc), reproduced below with its flag encoding from
    fm_core.h. Dexed is GPLv3, as this project is.

THE ONE THING DERIVED RATHER THAN COPIED, and why it is checked and not asserted:
Dexed's own `isCarrier` tests the `OUT_BUS_ADD` bit, which is also set on operators
that merely ADD into a modulation bus (algorithm 16's OP3 is `0x25`: reads bus 2,
adds into bus 1, and is not a carrier). This file uses `out_bus == 0` instead, and
the rule is checked against the algorithms whose carrier count is not in dispute:
1 and 2 give 2 carriers, 5 and 6 give 3, 16 gives 1, 31 gives 5, 32 gives 6. The
check runs in `--selftest` so the rule cannot rot.

An earlier version of this session asserted "the left branch of the DX7's
algorithm 2" from memory and it was unbacked. Nothing here is from memory.

WHAT CAME OUT, measured 2026-07-31 over all 119,296 voices (`--check`, `--axes`).
BJ's own reading of `fm3` was the falsifiable prediction this was pointed at: on
this entry `under` (ratio 3 = 2.5) together with `slow` (onset 0.6) goes "sehr
klar richtung trombone". The corpus was not tuned to it and it holds on both axes
at once, in the DX7 programmers' own settings:

    Wort         n     Verhaeltnis >= 2      Attack (99 = sofort)
    trombone   174     12.6 %   (Korpus 41)   57   (Korpus 80)
    horn       807      8.7 %                 61
    sax        395      7.3 %                 64
    cello      381     14.2 %                 52
    marimba    330     98.5 %                 99
    vibe       195     89.2 %                 80
    glass      128     59.4 %                 95

Two things fell out that no one asked for and that the entry should know. First,
`trumpet` (67.5 %) and `tuba` (52 %) break the brass family the other way on the
ratio while staying slow -- their brightness comes from a far family, trombone's
and horn's does not, so "brass" is not one direction here. Second, `fm3` runs its
ratios to 8.0 and the electric pianos of this corpus sit at 11, 12 and 14, the
acoustic ones at 20 and 21: the entry cannot reach the tine ratio as it stands.
That is a measurement about the entry's range, not a proposal to widen it.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

REPO = "visualizersdotnl/Yamaha-DX7-patch-library"
CORPUS = Path.home() / "Library" / "T5ynth" / "dx7_corpus"

# --- Dexed, Source/msfa/fm_core.cc, verbatim. Entry i is algorithm i+1; within an
#     entry the six bytes are OP6, OP5, OP4, OP3, OP2, OP1 -- the same order the
#     packed voice uses.
ALGORITHMS = [
    (0xc1, 0x11, 0x11, 0x14, 0x01, 0x14), (0x01, 0x11, 0x11, 0x14, 0xc1, 0x14),
    (0xc1, 0x11, 0x14, 0x01, 0x11, 0x14), (0xc1, 0x11, 0x94, 0x01, 0x11, 0x14),
    (0xc1, 0x14, 0x01, 0x14, 0x01, 0x14), (0xc1, 0x94, 0x01, 0x14, 0x01, 0x14),
    (0xc1, 0x11, 0x05, 0x14, 0x01, 0x14), (0x01, 0x11, 0xc5, 0x14, 0x01, 0x14),
    (0x01, 0x11, 0x05, 0x14, 0xc1, 0x14), (0x01, 0x05, 0x14, 0xc1, 0x11, 0x14),
    (0xc1, 0x05, 0x14, 0x01, 0x11, 0x14), (0x01, 0x05, 0x05, 0x14, 0xc1, 0x14),
    (0xc1, 0x05, 0x05, 0x14, 0x01, 0x14), (0xc1, 0x05, 0x11, 0x14, 0x01, 0x14),
    (0x01, 0x05, 0x11, 0x14, 0xc1, 0x14), (0xc1, 0x11, 0x02, 0x25, 0x05, 0x14),
    (0x01, 0x11, 0x02, 0x25, 0xc5, 0x14), (0x01, 0x11, 0x11, 0xc5, 0x05, 0x14),
    (0xc1, 0x14, 0x14, 0x01, 0x11, 0x14), (0x01, 0x05, 0x14, 0xc1, 0x14, 0x14),
    (0x01, 0x14, 0x14, 0xc1, 0x14, 0x14), (0xc1, 0x14, 0x14, 0x14, 0x01, 0x14),
    (0xc1, 0x14, 0x14, 0x01, 0x14, 0x04), (0xc1, 0x14, 0x14, 0x14, 0x04, 0x04),
    (0xc1, 0x14, 0x14, 0x04, 0x04, 0x04), (0xc1, 0x05, 0x14, 0x01, 0x14, 0x04),
    (0x01, 0x05, 0x14, 0xc1, 0x14, 0x04), (0x04, 0xc1, 0x11, 0x14, 0x01, 0x14),
    (0xc1, 0x14, 0x01, 0x14, 0x04, 0x04), (0x04, 0xc1, 0x11, 0x14, 0x04, 0x04),
    (0xc1, 0x14, 0x04, 0x04, 0x04, 0x04), (0xc4, 0x04, 0x04, 0x04, 0x04, 0x04),
]
OUT_BUS, ADD, IN_BUS = 0x03, 0x04, 0x30       # fm_core.h, bits 0-1, 2, 4-5


def routing(alg):
    """Who modulates whom, for algorithm `alg` (0-based).

    Returns (carriers, mods) with slot indices 0..5 = OP6..OP1: `carriers` is the
    set of slots that reach the output, `mods[s]` the slots whose signal arrives at
    slot s. The buses are simulated in processing order, which is the order the
    bytes are in: an operator with the ADD bit joins whatever is already on its
    output bus, one without it replaces it.
    """
    bus = {1: [], 2: []}
    carriers, mods = [], {}
    for slot, byte in enumerate(ALGORITHMS[alg]):
        src = (byte & IN_BUS) >> 4
        mods[slot] = list(bus[src]) if src else []
        dst = byte & OUT_BUS
        if dst == 0:
            carriers.append(slot)
        elif byte & ADD:
            bus[dst].append(slot)
        else:
            bus[dst] = [slot]
    return carriers, mods


def voices(data):
    """Every voice in a bulk dump, as raw fields. Accepts the 4104-byte SysEx and
    the bare 4096-byte body some collections store."""
    if len(data) >= 4104 and data[0] == 0xF0:
        body = data[6:6 + 4096]
    elif len(data) == 4096:
        body = data
    else:
        return
    for v in range(32):
        b = body[v * 128:(v + 1) * 128]
        if len(b) < 128:
            return
        ops = []
        for i in range(6):                    # i = 0 is OP6, matching ALGORITHMS
            o = b[i * 17:(i + 1) * 17]
            ops.append({
                "level": o[14],
                "coarse": (o[15] >> 1) & 0x1f,
                "fine": o[16],
                "fixed": bool(o[15] & 1),
                "detune": (o[12] >> 3) & 0x0f,
                "attack": o[0],               # EG rate 1: higher is faster
                "eg_rates": list(o[0:4]),
                "eg_levels": list(o[4:8]),
            })
        name = "".join(chr(c) if 32 <= c < 127 else " " for c in b[118:128]).strip()
        yield {"name": name, "alg": b[110] & 0x1f, "feedback": b[111] & 0x07, "ops": ops}


def ratio(op):
    """The operator's frequency as a multiple of the note. `None` in fixed mode,
    where the operator ignores the key. Ratio mode, per the format document:
    coarse 0 means 0.5, coarse n means n, and fine is hundredths on top."""
    if op["fixed"]:
        return None
    base = 0.5 if op["coarse"] == 0 else float(op["coarse"])
    return base * (1.0 + op["fine"] / 100.0)


def sounds(op, floor=1):
    """Whether an operator contributes at all: output above `floor`, and an
    envelope that reaches somewhere. Most DX7 patches use fewer than six operators
    without changing algorithm, by silencing the rest one of these two ways."""
    return op["level"] > floor and max(op["eg_levels"]) > 0


def principal(voice, floor=1):
    """The voice projected onto `fm3`'s axes: its LOUDEST carrier and that
    carrier's two strongest direct modulators.

    WHY A PROJECTION AND NOT A FILTER. `fm3` is exactly one carrier driven by two
    modulators in linear combination. Measured over this corpus that shape is 0.1 %
    of 119,296 voices -- 45 % of DX7 patches have two live carriers, 19 % have
    three, 91 % keep all six operators sounding. A filter for the exact shape
    yields 153 voices and no word reaches a countable group. So each voice is
    reduced to the part of it `fm3` can express, and what the reduction DISCARDS is
    carried alongside every row rather than hidden: `carriers` counts the live
    carriers (everything above 1 is a layer `fm3` does not have), `mods` the direct
    modulators on the principal one (above 2 is a modulator this drops), `chain`
    says a chosen modulator is itself modulated. `strict` marks the rows where
    nothing was discarded -- the 0.1 % -- so any reading can be re-run on them and
    compared, which is what `--words --strict` does.

    Returns None only when there is no live carrier, when the principal carrier has
    no live modulator (then it is not FM at all), or when the carrier or a chosen
    modulator runs at fixed frequency and therefore has no ratio to the note.
    """
    carriers, mods = routing(voice["alg"])
    ops = voice["ops"]
    live = {s for s in range(6) if sounds(ops[s], floor)}
    car = [s for s in carriers if s in live]
    if not car:
        return None
    c = max(car, key=lambda s: (ops[s]["level"], s))       # ties: OP1 before OP6
    m_all = [s for s in mods[c] if s in live]
    rc = ratio(ops[c])
    if not m_all or not rc:
        return None
    # The two slots are chosen by RATIO, not by level, because that is what defines
    # them in the entry: `ratio 2` is the near family, `ratio 3` the far one. Where
    # a DX7 carrier has three modulators the fm3-comparable pair is the lowest and
    # the highest, since those two span what the entry's two slots span. A carrier
    # with a single modulator is a two-operator voice, and on fm3's axes that is
    # both slots on one ratio -- not a missing value.
    live_r = sorted(r for r in (ratio(ops[s]) for s in m_all) if r is not None)
    if not live_r:
        return None
    by_ratio = {ratio(ops[s]): ops[s] for s in m_all if ratio(ops[s]) is not None}
    r2, r3 = live_r[0] / rc, live_r[-1] / rc
    o2, o3 = by_ratio[live_r[0]], by_ratio[live_r[-1]]
    return {
        "name": voice["name"], "alg": voice["alg"] + 1, "carrier_ratio": rc,
        "ratio_2": round(r2, 4), "ratio_3": round(r3, 4),
        "far": 1.0 if r3 >= 2.0 else 0.0,     # reaches a family off the body's grid
        "level_2": o2["level"], "level_3": o3["level"],
        "carrier_attack": ops[c]["attack"], "carrier_level": ops[c]["level"],
        "feedback": voice["feedback"],
        "carriers": len(car), "mods": len(m_all),
        "chain": any(any(x in live for x in mods[s]) for s in m_all),
        "strict": (len(car) == 1 and len(m_all) == 2
                   and not any(any(x in live for x in mods[s]) for s in m_all)
                   and not live - {c, *m_all}),
    }


# --- fetching ---------------------------------------------------------------

def _get(url, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": "t5ynth-dx7-corpus"})
    with urllib.request.urlopen(req, timeout=60) as r:      # connect only; no read cap
        raw = r.read()
    return raw if binary else json.loads(raw)


def fetch():
    CORPUS.mkdir(parents=True, exist_ok=True)
    tree = _get(f"https://api.github.com/repos/{REPO}/git/trees/master?recursive=1")
    if tree.get("truncated"):
        sys.exit("the tree came back truncated; fetch it in parts before trusting it")
    want = [b for b in tree["tree"]
            if b["type"] == "blob" and b["path"].lower().endswith(".syx")
            and b["size"] in (4096, 4104)]
    print(f"{len(want)} Bänke im Verzeichnis, {CORPUS}")
    got = 0
    for i, b in enumerate(want, 1):
        dest = CORPUS / b["path"]
        if dest.exists() and dest.stat().st_size == b["size"]:
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        url = (f"https://raw.githubusercontent.com/{REPO}/master/"
               + urllib.parse.quote(b["path"]))
        try:
            dest.write_bytes(_get(url, binary=True))
            got += 1
        except Exception as e:                              # noqa: BLE001
            print(f"  {b['path']}: {e}")
        if i % 250 == 0:
            print(f"  {i}/{len(want)}")
    print(f"{got} neu geholt")


def read_corpus():
    for p in sorted(CORPUS.rglob("*.[sS][yY][xX]")):
        try:
            data = p.read_bytes()
        except OSError:
            continue
        cat = p.relative_to(CORPUS).parts
        category = cat[1] if len(cat) > 2 and cat[0].startswith("!") else ""
        for v in voices(data):
            v["category"] = category
            yield v


# --- reading the words ------------------------------------------------------

WORD = re.compile(r"[A-Za-z]{3,}")
NOISE = {"the", "and", "for", "new", "old", "bank", "voice", "patch", "dx", "syx",
         "yamaha", "rom", "cart", "init", "usr", "user",
         # labels a programmer put on a slot, not on a sound. Words that merely
         # LOOK generic stay in -- `block` is a wood block and `super` is in
         # `superbass`; only what carries no timbre at all is dropped.
         "bse", "slx", "synt", "take", "image", "beamer", "vol", "off", "music",
         "set", "seq", "prog", "copy", "edit", "test", "empty", "blank", "name",
         "none", "mine", "his", "her", "mix", "num", "one", "two", "six"}


# The columns, in the entry's own vocabulary. `axis` names the `fm3` parameter the
# column corresponds to, or "" where the corpus has a quantity the entry does not
# and vice versa -- `trade` has no DX7 counterpart at all, and the DX7's output
# level is monotone in modulation index but is not a count of cycles, so `index 2/3`
# can be read for DIRECTION and never transcribed as a value.
COLUMNS = [
    ("ratio_3", "ratio 3", "ratio 3"),
    ("far", "fern %", "ratio 3 >= 2"),
    ("ratio_2", "ratio 2", "ratio 2"),
    ("level_3", "Antrieb 3", "index 3"),
    ("level_2", "Antrieb 2", "index 2"),
    ("carrier_attack", "Attack", "onset (umgekehrt)"),
    ("feedback", "Rückk.", ""),
]


def collect(strict=False):
    rows, total = [], 0
    for v in read_corpus():
        total += 1
        m = principal(v)
        if not m or (strict and not m["strict"]):
            continue
        m["category"] = v["category"]
        m["words"] = {w.lower() for w in WORD.findall(m["name"])} - NOISE
        rows.append(m)
    return rows, total


def _stats(rows, field):
    """Middle and spread of one column. A 0/1 indicator like `far` has no useful
    median -- it is reported as a percentage against the corpus percentage, in
    points, so the column reads the same way as the others: how far from ordinary."""
    vals = [r[field] for r in rows if r.get(field) is not None]
    if len(vals) < 4:
        return None
    if field == "far":
        rate = 100 * statistics.fmean(vals)
        return rate, max(rate, 1e-9), len(vals)   # a proportion's scale is its base rate
    q = statistics.quantiles(vals, n=4)
    return statistics.median(vals), max(q[2] - q[0], 1e-9), len(vals)


def words(min_n, top, strict=False):
    rows, total = collect(strict)
    hard = sum(r["strict"] for r in rows)
    print(f"{total} Stimmen gelesen, {len(rows)} auf fm3s Achsen abbildbar "
          f"({100 * len(rows) / max(total, 1):.1f} %), davon {hard} ohne jeden "
          f"Verlust ({100 * hard / max(len(rows), 1):.1f} %)")
    if not rows:
        return
    print(f"   verworfen beim Abbilden: im Median {statistics.median([r['carriers'] for r in rows]):.0f} "
          f"Träger, {statistics.median([r['mods'] for r in rows]):.0f} Modulatoren am Haupttäger; "
          f"{100 * sum(r['chain'] for r in rows) / len(rows):.0f} % haben einen modulierten Modulator\n")

    base = {f: _stats(rows, f) for f, _, _ in COLUMNS}
    by = defaultdict(list)
    for r in rows:
        for w in r["words"]:
            by[w].append(r)
        if r["category"]:
            by["/" + r["category"].lower()].append(r)

    scored = []
    for w, g in by.items():
        if len(g) < min_n:
            continue
        cells, worst = [], 0.0
        for f, _, _ in COLUMNS:
            s = _stats(g, f)
            if not s or not base[f]:
                cells.append((None, 0.0))
                continue
            d = (s[0] - base[f][0]) / base[f][1]
            cells.append((s[0], d))
            worst = max(worst, abs(d))
        scored.append((worst, w, len(g), cells))
    scored.sort(reverse=True)

    print("   Median des Wortes, dahinter der Abstand zum Korpusmedian in "
          "Interquartilsabständen (>|1| ist eine Richtung).")
    print(f"   Korpusmedian: " + "  ".join(
        f"{lab} {base[f][0]:.2f}" for f, lab, _ in COLUMNS if base[f]))
    print(f"   fm3-Achse:    " + "  ".join(
        f"{lab}->{ax or '--'}" for _, lab, ax in COLUMNS) + "\n")
    head = "Wort              n  " + "".join(f"{lab:>16}" for _, lab, _ in COLUMNS)
    print(head)
    for _, w, n, cells in scored[:top]:
        line = f"{w:16}{n:5}  "
        for med, d in cells:
            line += "               -" if med is None else f"{med:8.2f}{d:+6.1f}  "
        print(line)


def profile(want, top=12):
    """Everything about the voices whose name carries one of `want` -- the WHOLE
    voice and not the projection, because when an entry is about to be built from
    a word, the projection is exactly what must not be trusted.

    The DX7 envelope is four rates and four levels: the output runs to L1 at rate
    R1, then to L2 at R2, then to L3 at R3, and on key-off to L4 at R4 (rates are
    0-99 and FASTER when larger). What makes a struck FM body struck is therefore
    visible here and nowhere in `--axes`: a modulator whose L1 stands far above its
    L2 with a quick R2 is a transient that dies back, and one whose L1 and L2 are
    level is a family that stays. That is the distinction `fm3` does not have.
    """
    want = {w.lower() for w in want}
    hits, algs, pairs = [], Counter(), Counter()
    for v in read_corpus():
        if not want & {w.lower() for w in WORD.findall(v["name"])}:
            continue
        carriers, mods = routing(v["alg"])
        ops = v["ops"]
        live = {s for s in range(6) if sounds(ops[s])}
        car = [s for s in carriers if s in live]
        if not car:
            continue
        c = max(car, key=lambda s: (ops[s]["level"], s))
        rc = ratio(ops[c])
        m = [s for s in mods[c] if s in live and ratio(ops[s]) is not None]
        if not rc or not m:
            continue
        algs[v["alg"] + 1] += 1
        rs = sorted(round(ratio(ops[s]) / rc, 2) for s in m)
        pairs[tuple(rs)] += 1
        hits.append({"name": v["name"], "carriers": len(car), "c": ops[c], "rc": rc,
                     "mods": [(round(ratio(ops[s]) / rc, 2), ops[s]) for s in m]})
    if not hits:
        print(f"nichts zu {sorted(want)}")
        return
    print(f"{len(hits)} Stimmen zu {sorted(want)}")
    print(f"  Algorithmen: {algs.most_common(6)}")
    print(f"  Träger im Median: {statistics.median(h['carriers'] for h in hits):.0f}")
    print(f"  häufigste Modulator-Verhältnisse am Haupttäger: {pairs.most_common(top)}\n")

    band = defaultdict(list)
    for h in hits:
        for r, o in h["mods"]:
            band["fern (>= 8)" if r >= 8 else "mittel (2-8)" if r >= 2
                 else "nah (< 2)"].append(o)
    print(f"{'Modulator':16}{'n':>7}{'Pegel':>8}{'R1':>6}{'L1':>6}{'R2':>6}{'L2':>6}"
          f"{'L1-L2':>8}   was das heißt")
    for k in ("fern (>= 8)", "mittel (2-8)", "nah (< 2)"):
        g = band.get(k)
        if not g:
            continue
        med = {f: statistics.median(o[f] if isinstance(o[f], int) else o[f][i]
                                    for o in g)
               for f, i in (("level", 0),)}
        r1 = statistics.median(o["eg_rates"][0] for o in g)
        l1 = statistics.median(o["eg_levels"][0] for o in g)
        r2 = statistics.median(o["eg_rates"][1] for o in g)
        l2 = statistics.median(o["eg_levels"][1] for o in g)
        drop = statistics.median(o["eg_levels"][0] - o["eg_levels"][1] for o in g)
        says = ("stirbt zurueck" if drop >= 15 else "steht" if drop <= 4 else "sinkt leicht")
        print(f"{k:16}{len(g):7}{med['level']:8.0f}{r1:6.0f}{l1:6.0f}{r2:6.0f}"
              f"{l2:6.0f}{drop:8.0f}   {says}")
    cg = [h["c"] for h in hits]
    print(f"\n{'Träger':16}{len(cg):7}"
          f"{statistics.median(o['level'] for o in cg):8.0f}"
          f"{statistics.median(o['eg_rates'][0] for o in cg):6.0f}"
          f"{statistics.median(o['eg_levels'][0] for o in cg):6.0f}"
          f"{statistics.median(o['eg_rates'][1] for o in cg):6.0f}"
          f"{statistics.median(o['eg_levels'][1] for o in cg):6.0f}"
          f"{statistics.median(o['eg_levels'][0] - o['eg_levels'][1] for o in cg):8.0f}")
    print("\nzehn Stimmen im Klartext (Verhältnis: Pegel, L1->L2 bei R2):")
    for h in hits[:10]:
        parts = [f"{r}: {o['level']}, {o['eg_levels'][0]}->{o['eg_levels'][1]}"
                 f"@{o['eg_rates'][1]}" for r, o in h["mods"]]
        cc = h["c"]
        print(f"  {h['name']:12} Träger {cc['level']}, "
              f"{cc['eg_levels'][0]}->{cc['eg_levels'][1]}@{cc['eg_rates'][1]}"
              f"  |  " + "  |  ".join(parts))


def axes(min_n, top, strict=False):
    """The main directions, per `fm3` axis: which words sit at the top of an axis
    and which at the bottom, over every word the corpus counts often enough.

    This is the shape the entry can actually use. A word's place is stated in the
    axis's own unit where the two instruments share one -- `ratio 2` and `ratio 3`
    are a multiple of the note on both -- and as a direction only where they do
    not: the DX7's output level rises with modulation index but does not count
    cycles, and its EG rate 1 runs the opposite way to `onset`, so both are read
    for sign and never transcribed. `fm3` runs its ratios to 8.0, so a word whose
    corpus median lies above that is marked: the entry cannot reach it as it stands.
    """
    rows, total = collect(strict)
    by = defaultdict(list)
    for r in rows:
        for w in r["words"]:
            by[w].append(r)
        if r["category"]:
            by["/" + r["category"].lower()].append(r)
    groups = {w: g for w, g in by.items() if len(g) >= min_n}
    base = {f: _stats(rows, f) for f, _, _ in COLUMNS}
    print(f"{total} Stimmen, {len(rows)} abbildbar, {len(groups)} Wörter mit "
          f"mindestens {min_n} Stimmen.\n")

    for f, lab, ax in COLUMNS:
        if not ax:
            continue
        scored = []
        for w, g in groups.items():
            s = _stats(g, f)
            if s:
                scored.append((s[0], w, len(g)))
        scored.sort(reverse=True)
        unit = ("Vielfaches der Note, fm3 reicht bis 8.0" if f.startswith("ratio")
                else "Anteil der Stimmen" if f == "far"
                else "DX7-Ausgangspegel 0-99, nur die Richtung zählt" if f.startswith("level")
                else "DX7-EG-Rate 1, 99 = sofort, also LÄUFT UMGEKEHRT zu onset")
        print(f"=== {lab}  ->  fm3: {ax}   ({unit}; Korpus {base[f][0]:.2f})")
        for tag, part in (("hoch", scored[:top]), ("tief", scored[-top:][::-1])):
            out = []
            for med, w, n in part:
                mark = "!" if f.startswith("ratio") and med > 8.0 else ""
                out.append(f"{w} {med:.2f}{mark} ({n})")
            print(f"   {tag}: " + ", ".join(out))
        print()
    print("!  = über fm3s Bereich; das Verhältnis ist mit dem Eintrag, wie er ist, "
          "nicht erreichbar.")


def check(min_n):
    """BJ's own datum, 2026-07-31, as a falsifiable prediction: on `fm3`, `under`
    (ratio 3 = 2.5, the far family pulled down near the body) together with `slow`
    (onset 0.6) goes "sehr klar richtung trombone". If the corpus knows anything,
    the voices human programmers NAMED for brass must sit low on `ratio 3` and slow
    on the carrier attack relative to the corpus, and the words for the opposite
    family -- bell, glass, chime -- must sit high and fast. Nothing here tunes to
    that expectation; it is printed and either holds or does not."""
    rows, total = collect()
    by = defaultdict(list)
    for r in rows:
        for w in r["words"]:
            by[w].append(r)
    base = {f: _stats(rows, f) for f in ("far", "carrier_attack", "ratio_3", "ratio_2")}
    print(f"{total} Stimmen, {len(rows)} abbildbar. Im Korpus erreichen "
          f"{base['far'][0]:.0f} % ein Verhältnis >= 2, der Attack-Median ist "
          f"{base['carrier_attack'][0]:.0f} (99 = sofort, KLEINER ist langsamer).\n")
    print(f"{'Wort':14}{'n':>6}{'fern %':>12}{'Abstand':>9}{'Attack':>10}{'Abstand':>9}")
    for group, ws in (("BJs Richtung: tief + langsam",
                       ["trombone", "tromb", "brass", "horn", "tuba", "trumpet", "sax", "cello"]),
                      ("Gegenprobe: hoch + schnell",
                       ["bell", "glass", "chime", "tubular", "vibe", "harpsi", "clav", "marimba"])):
        print(f"\n{group}")
        for w in ws:
            g = by.get(w, [])
            if len(g) < min_n:
                print(f"  {w:12}{len(g):6}   (unter {min_n})")
                continue
            out = f"  {w:12}{len(g):6}"
            for f in ("far", "carrier_attack"):
                s = _stats(g, f)
                out += ("           -        -" if not s else
                        f"{s[0]:12.1f}{(s[0] - base[f][0]) / base[f][1]:+9.2f}")
            print(out)


def selftest():
    known = {0: 2, 1: 2, 4: 3, 5: 3, 15: 1, 30: 5, 31: 6}   # 0-based algorithm index
    for alg, want in known.items():
        car, _ = routing(alg)
        assert len(car) == want, f"Algorithmus {alg + 1}: {len(car)} Träger, erwartet {want}"
    car, mods = routing(0)                                   # 6->5->4->3, 2->1
    assert mods[3] == [2] and mods[5] == [4], mods
    assert ratio({"fixed": False, "coarse": 0, "fine": 0}) == 0.5
    assert ratio({"fixed": False, "coarse": 2, "fine": 50}) == 3.0

    # The projection on a made-up algorithm-1 voice: OP1 (slot 5) and OP3 (slot 3)
    # are the carriers, OP1 is louder, so OP2 (slot 4) is its only modulator and
    # the row must declare the second carrier it dropped.
    def op(level, coarse=1):
        return {"level": level, "coarse": coarse, "fine": 0, "fixed": False,
                "detune": 7, "attack": 99, "eg_rates": [99] * 4, "eg_levels": [99, 99, 99, 0]}
    v = {"name": "T", "alg": 0, "feedback": 0,
         "ops": [op(0), op(0), op(0), op(70), op(80, 3), op(99)]}
    p = principal(v)
    assert p["carriers"] == 2 and p["mods"] == 1 and not p["strict"], p
    assert p["ratio_2"] == p["ratio_3"] == 3.0 and p["far"] == 1.0, p   # 2-Op: ein Verhältnis
    # Three modulators on the carrier: the pair is the LOWEST and the HIGHEST ratio,
    # and the loud middle one is what the projection drops.
    v["ops"] = [op(0), op(0), op(60, 7), op(90, 4), op(80, 2), op(99)]
    v["alg"] = 17                                    # OP1 alone, driven by OP2,3,4
    car, mods = routing(17)
    assert car == [5] and sorted(mods[5]) == [2, 3, 4], (car, mods)
    p = principal(v)
    assert p["mods"] == 3 and p["ratio_2"] == 2.0 and p["ratio_3"] == 7.0, p
    assert not sounds({"level": 99, "eg_levels": [0, 0, 0, 0]}), "stumme Hüllkurve"
    print("selftest ok: Trägerzahl von 7 Algorithmen, Kette von Algorithmus 1, "
          "Verhältnisse, Projektion auf den lauteren Träger und auf die "
          "äußeren zwei von drei Modulatoren")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fetch", action="store_true", help="Bänke holen (CC0, s. Docstring)")
    ap.add_argument("--words", action="store_true", help="Wörter gegen fm3s Achsen")
    ap.add_argument("--axes", action="store_true",
                    help="Hauptrichtungen: was oben und was unten auf jeder fm3-Achse steht")
    ap.add_argument("--profile", nargs="+", metavar="WORT",
                    help="die GANZEN Stimmen zu diesen Wörtern, mit Hüllkurven")
    ap.add_argument("--check", action="store_true",
                    help="BJs Trombone-Datum gegen den Korpus prüfen")
    ap.add_argument("--strict", action="store_true",
                    help="nur Stimmen, die ohne Verlust auf fm3 passen (0,1 %%)")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--min-n", type=int, default=25, help="ab wie vielen Stimmen ein Wort zählt")
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args()
    if a.selftest or not (a.fetch or a.words or a.check or a.axes or a.profile):
        selftest()
    if a.profile:
        profile(a.profile)
    if a.fetch:
        fetch()
    if a.words:
        words(a.min_n, a.top, a.strict)
    if a.axes:
        axes(a.min_n, a.top, a.strict)
    if a.check:
        check(a.min_n)
