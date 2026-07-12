#!/usr/bin/env python3
"""DCO AUDITION LOOP -- a small model COMPOSES synthesis methods; the synth HEARS.

The idea: a small LLM does not need to KNOW that "fm2 ratio 6 -> metallic"; it
proposes ORDERED combinations of named palette idioms (its INTERPRETATION), and
the synth's own ear judges the bakes. Reuses the REAL baker (dco_bake), the REAL
CLAP ear (clap_probe), and the REAL lexicon idioms -- nothing reimplemented.

What this harness MEASURED (evidence in tools/dco_audition_out), and why it is
built the way it is:

  * The small model (qwen3:8b) INTERPRETS well on every axis -- clarinet ->
    clarinet+square, warm pad -> sine+triangle+flute, harsh -> pwm, bell ->
    fm_bell+ring_mod. The model is the reliable core.
  * The CLAP AUDIO ear is reliable on the BRIGHT/HARSH/METALLIC axis (it rescued a
    lead the model got wrong: 0.35 -> 0.56 cosine, heard harsh/fuzzy/buzzing) but
    INVERTED on the DARK/WARM axis: it rated metallic ring_mod ABOVE the model's
    correct sine+triangle for "soft warm pad", and brassy fm+brass ABOVE
    clarinet+square for "dark hollow clarinet". This bright-bias on bare
    single-cycle tones is itself a machine-listening bias worth exposing.
  * Two fitness reweightings to fix the dark axis (an additive brightness term, a
    DSP-centroid gate) BOTH failed -- they Goodharted the bright winner or could
    not overcome CLAP's dark bias. TEST, do not theorize: they were tried and cut.

So the loop ROUTES BY AXIS (target brightness read from CLAP TEXT polarity, which
IS reliable):

  DARK/WARM target -> TRUST the model's interpretation (the ear is inverted here);
                      bake only to report honest diagnostics.
  BRIGHT target    -> the ear REFINES: survey each idiom solo, best-of-N over the
                      model's proposals + an ear seed, then hill-climb by single-
                      idiom edits (monotonic on CLAP cosine, with a richness floor).

  BAKE=<dco_bake> .venv/bin/python tools/dco_audition_loop.py "metallic bell" qwen3:8b
"""
import os
import sys
import re
import json
import urllib.request
import subprocess
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
sys.path.insert(0, os.path.dirname(__file__))
import lco_testbench as TB          # analyze() + _find_bake()
import clap_probe as CP             # the CLAP ear
import dco_recipe as DR             # validate_recipe (clamp/repair -> bakeable)

LEXICON = os.path.join(os.path.dirname(__file__), "..", "backend", "dco_lexicon.json")
OUT = os.path.join(os.path.dirname(__file__), "dco_audition_out")
CLAP_MODEL = "laion/clap-htsat-unfused"

FLUX_FLOOR = 0.02        # below this a bake is a near-static (dead) tone -> reject
N_PROPOSALS = 5          # diverse candidate combinations the model offers
PROPOSE_TEMP = 0.8       # sampling temperature for proposal diversity
TOP_AFFINITY = 8         # idioms (by solo ear-affinity) the hill-climb may swap in
REFINE_ROUNDS = 3        # hill-climb rounds (stops early when no edit improves)

# Axis routing. MEASURED (tools/dco_audition_out): CLAP's AUDIO ranking is reliable
# on the BRIGHT/HARSH/METALLIC axis (it rescued a lead the model got wrong, 0.35 ->
# 0.56) but INVERTED on the DARK/WARM axis -- for "soft warm pad" it rated metallic
# ring_mod ABOVE the model's correct sine+triangle; for "dark hollow clarinet" it
# rated brassy fm+brass ABOVE the model's correct clarinet+square. Two fitness
# reweightings (an additive brightness term, a DSP-centroid gate) both failed:
# either they Goodharted the bright winner or could not overcome CLAP's dark bias.
# The reliable signals are the MODEL's interpretation (it picked right on every
# target) and DSP centroid. So route by axis: on a dark target TRUST the model, on a
# bright target let the ear REFINE. The target's axis is read from CLAP TEXT polarity
# (text-text cosine IS reliable; only the audio tower is biased).
POL_WARM = -0.15         # target text polarity below this = a dark/warm target (trust model)
POL_BRIGHT = 0.15        # target text polarity above this = a bright target (ear refines)

SYS = (
    "You are a synthesis programmer for a wavetable oscillator. You COMPOSE timbres "
    "from a fixed palette of named synthesis METHODS -- ready, parametrized building "
    "blocks. Given a target sound, choose an ORDERED sequence of 2 to 4 method keys "
    "the timbre MORPHS through from start to end. Combine DIFFERENT methods for a "
    "richer, moving result, but pick methods whose CHARACTER fits the target: a warm "
    "or mellow sound needs soft/dark methods (sine, triangle, fm_ep, flute), a harsh "
    "or metallic sound needs bright/inharmonic ones (metallic_fm, ring_mod, cheby, "
    "pwm). Reply ONLY the method keys, one per line, in order.\n\nPALETTE:\n{palette}"
)


def load_idioms():
    lex = json.load(open(LEXICON))
    return {t["key"]: {"why": t.get("why", ""), "template": t["template"]}
            for t in lex["techniques"]}


def ollama(model):
    def call(system, user, max_new=160, temperature=0.0):
        body = json.dumps({
            "model": model,
            "messages": [{"role": "system", "content": system},
                         {"role": "user", "content": user}],
            "stream": False, "think": False,
            "options": {"temperature": float(temperature), "num_predict": int(max_new)},
        }).encode()
        req = urllib.request.Request("http://localhost:11434/api/chat", body,
                                     {"Content-Type": "application/json"})
        return json.load(urllib.request.urlopen(req, timeout=300))["message"]["content"]
    return call


def extract_keys(raw, idioms):
    """Idiom keys as whole words anywhere in the reply, ordered by first appearance
    -- robust to the model wrapping them in prose."""
    low = raw.lower()
    hits = []
    for k in idioms:
        m = re.search(r"(?<![a-z0-9_])" + re.escape(k) + r"(?![a-z0-9_])", low)
        if m:
            hits.append((m.start(), k))
    hits.sort()
    keys = []
    for _, k in hits:
        if k not in keys:
            keys.append(k)
    return keys[:4]


def propose(call, target, idioms, n=N_PROPOSALS):
    """The model's INTERPRETATION: n diverse ordered idiom combinations."""
    palette = "\n".join(f"  {k}: {v['why']}" for k, v in idioms.items())
    combos = []
    for i in range(n):
        # first proposal deterministic (the model's confident pick), rest sampled
        temp = 0.0 if i == 0 else PROPOSE_TEMP
        raw = call(SYS.format(palette=palette), f"Target sound: {target}", 160, temp)
        keys = extract_keys(raw, idioms)
        if len(keys) >= 2 and keys not in combos:
            combos.append(keys)
    return combos


def build_recipe(keys, idioms, frames=256):
    kfs = []
    for k in keys:
        for kf in idioms[k]["template"]["keyframes"]:
            kfs.append(dict(kf))
    kfs = kfs[:8] or [{"kind": "additive", "partials": [{"h": 1, "a": 1.0, "phase": 0.0}]}]
    motion = [{"to": 0, "dur_frac": 0.0, "curve": "lin"}]
    for i in range(1, len(kfs)):
        motion.append({"to": i, "dur_frac": 1.0, "curve": "lin"})
    recipe = {"keyframes": kfs, "motion": motion, "loop": False,
              "frames": frames, "motion_rate_hz": 0.25}
    recipe, _ = DR.validate_recipe(recipe)
    return recipe


class Ear:
    """The synth's own hearing: bake a recipe, embed the WAV with the SAME CLAP the
    backend uses, score its cosine to the target text. Batches CLAP embedding so a
    whole candidate generation is heard in one forward pass."""

    def __init__(self, bake, outdir, target):
        from transformers import ClapModel, ClapProcessor
        self.bake, self.outdir = bake, outdir
        self.clap = ClapModel.from_pretrained(CLAP_MODEL).eval()
        self.proc = ClapProcessor.from_pretrained(CLAP_MODEL)
        CP.assert_model_sane(self.clap, self.proc, "cpu")
        self.target_emb = CP.embed_texts(self.clap, self.proc, [target], "cpu")
        self.vocab = list(dict.fromkeys(CP.NAIVE_VOCAB))
        self.vocab_emb = CP.embed_texts(self.clap, self.proc, self.vocab, "cpu")
        # Desired brightness from CLAP TEXT polarity (text-text cosine is reliable;
        # only CLAP's AUDIO tower is bright-biased on these tones). > POL_BRIGHT =>
        # target wants a bright bake; < POL_WARM => wants a dark one. The bake's
        # ACTUAL brightness is measured by DSP centroid in hear(), not by CLAP.
        poles = CP.embed_texts(self.clap, self.proc,
                               ["a bright sharp piercing harsh metallic sound",
                                "a dark warm mellow round muffled soft sound"], "cpu")
        self.target_pol = float((self.target_emb @ poles[0:1].T)[0, 0]
                                - (self.target_emb @ poles[1:2].T)[0, 0])
        self._n = 0

    def hear(self, recipes):
        """recipes: list of (tag, recipe). Bake all, embed all in one CLAP pass,
        return a scored dict per baked candidate (skips bakes that fail)."""
        baked = []
        for tag, rec in recipes:
            self._n += 1
            wav = os.path.join(self.outdir, f"cand{self._n:04d}.wav")
            jp = wav[:-4] + ".json"
            json.dump(rec, open(jp, "w"))
            r = subprocess.run([self.bake, jp, wav], capture_output=True, text=True)
            if r.returncode == 0 and os.path.exists(wav):
                baked.append((tag, rec, wav))
        if not baked:
            return []
        audios = [CP.load_audio_48k_mono(Path(w)) for _, _, w in baked]
        emb = CP.embed_audios(self.clap, self.proc, audios, "cpu")
        sem = (emb @ self.target_emb.T)[:, 0]
        _, tidx = CP.rank(emb, self.vocab_emb)
        out = []
        for i, (tag, rec, wav) in enumerate(baked):
            an = TB.analyze(wav)
            flux = an["flux"]
            parts = max(m[1] for m in an["perframe"])
            mean_cent = float(sum(m[0] for m in an["perframe"]) / len(an["perframe"]))
            tags = [self.vocab[tidx[i, j].item()] for j in range(5)]
            out.append({"tag": tag, "recipe": rec, "wav": wav,
                        "sem": float(sem[i]), "fit": float(sem[i]),  # fit == semantic cosine
                        "cent": mean_cent, "flux": flux, "parts": parts,
                        "tags": tags, "alive": flux >= FLUX_FLOOR})
        return out


def rank(cands):
    """Fitness-first (semantic cosine + brightness-axis match) with a richness floor:
    live candidates (flux >= floor) rank by fit; only if EVERY candidate is
    near-static do dead ones compete."""
    live = [c for c in cands if c["alive"]]
    pool = live if live else cands
    return sorted(pool, key=lambda c: c["fit"], reverse=True)


def survey(ear, idioms):
    """Bake each palette idiom ALONE, CLAP-rank its affinity to the target. The ear
    surveys its own methods -> a target-specific idiom ranking the climb draws from."""
    recs = [(k, build_recipe([k], idioms)) for k in idioms]
    heard = ear.hear(recs)
    heard.sort(key=lambda c: c["fit"], reverse=True)
    return [c["tag"] for c in heard]


def neighbors(keys, affinity):
    """Single-idiom edits of `keys` drawn from the ear's top-affinity idioms: swap
    each position, drop one, append one. The bounded neighbourhood a hill-climb walks."""
    pool = affinity[:TOP_AFFINITY]
    seen, out = {tuple(keys)}, []
    for i in range(len(keys)):                      # swap position i
        for q in pool:
            if q == keys[i]:
                continue
            cand = keys[:i] + [q] + keys[i + 1:]
            if tuple(cand) not in seen:
                seen.add(tuple(cand)); out.append(cand)
    if len(keys) > 2:                               # drop one (keep >= 2)
        for i in range(len(keys)):
            cand = keys[:i] + keys[i + 1:]
            if tuple(cand) not in seen:
                seen.add(tuple(cand)); out.append(cand)
    if len(keys) < 4:                               # append one
        for q in pool:
            if q not in keys:
                cand = keys + [q]
                if tuple(cand) not in seen:
                    seen.add(tuple(cand)); out.append(cand)
    return out


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "a bright metallic bell"
    model = sys.argv[2] if len(sys.argv) > 2 else "qwen3:8b"
    bake = TB._find_bake()
    if not bake:
        print("NO dco_bake. Set BAKE=<path>.", file=sys.stderr); return 2
    slug = re.sub(r"[^a-z0-9]+", "_", target.lower()).strip("_")[:32] or "target"
    outdir = os.path.join(OUT, slug)
    os.makedirs(outdir, exist_ok=True)
    idioms = load_idioms()
    call = ollama(model)

    print(f"target: {target!r}   model: {model}   idioms: {len(idioms)}\nloading CLAP ...")
    ear = Ear(bake, outdir, target)

    # ROUTE BY AXIS (see the module note above): the CLAP audio ear is reliable on the
    # bright axis and inverted on the dark axis, so pick the trustworthy signal per axis.
    dark = ear.target_pol < POL_WARM
    want = "warm/dark" if dark else ("bright" if ear.target_pol > POL_BRIGHT else "neutral")
    print(f"[axis]    target wants {want} (pol={ear.target_pol:+.3f}) -> "
          f"{'TRUST MODEL, ear is inverted here' if dark else 'ear refines'}")

    # PROPOSE -- the model's interpretation (its strength: it picked right on every axis)
    combos = propose(call, target, idioms)
    print(f"[propose] model combos: {combos}")

    if dark:
        # DARK/WARM: do NOT let the inverted CLAP ear overrule the model. Take the
        # model's confident first (temp 0) interpretation; bake only to report honest
        # diagnostics. Fall through proposals until one clears the richness floor.
        best = None
        for combo in (combos or [["sine", "additive"]]):
            c = ear.hear([("+".join(combo), build_recipe(combo, idioms))])
            if not c:
                continue
            if best is None:
                best = c[0]
            if c[0]["alive"]:
                best = c[0]; break
        if best is None:
            print("[trust] all model picks failed to bake; aborting.", file=sys.stderr); return 3
        model_first = best
        print(f"[trust]   model pick: {best['tag']}  cos={best['sem']:.3f} cent={best['cent']:.1f}  "
              f"heard={best['tags']}  flux={best['flux']:.2f}")
    else:
        # BRIGHT/HARSH: the ear is reliable -- survey the palette, best-of-N, hill-climb.
        affinity = survey(ear, idioms)
        print(f"[survey]  ear's top idioms: {affinity[:TOP_AFFINITY]}")
        ear_seed = affinity[:3]
        seeds = combos + ([ear_seed] if ear_seed not in combos else [])
        heard = ear.hear([("+".join(k), build_recipe(k, idioms)) for k in seeds])
        if not heard:
            print("[select] every seed bake failed; aborting.", file=sys.stderr); return 3
        best = rank(heard)[0]
        model_first = next((c for c in heard if c["tag"] == "+".join(combos[0])), None) if combos else None
        print(f"[select]  best seed: {best['tag']}  cos={best['sem']:.3f} cent={best['cent']:.1f}  "
              f"heard={best['tags']}  flux={best['flux']:.2f}")
        best_keys = best["tag"].split("+")
        for rnd in range(1, REFINE_ROUNDS + 1):
            nbrs = neighbors(best_keys, affinity)
            if not nbrs:
                break
            heard = ear.hear([("+".join(k), build_recipe(k, idioms)) for k in nbrs])
            cand = rank(heard)[0] if heard else None
            if cand and cand["fit"] > best["fit"] + 1e-4:
                best = cand; best_keys = best["tag"].split("+")
                print(f"[refine {rnd}] -> {best['tag']}  cos={best['sem']:.3f} cent={best['cent']:.1f}  "
                      f"heard={best['tags']}  flux={best['flux']:.2f}")
            else:
                print(f"[refine {rnd}] no edit beats cos {best['fit']:.3f} -> converged"); break

    # keep the winner as best.wav for the ear-check; leave candNNN.wav for forensics
    import shutil
    shutil.copy(best["wav"], os.path.join(outdir, "best.wav"))
    json.dump(best["recipe"], open(os.path.join(outdir, "best.json"), "w"))
    print(f"\nRESULT '{target}'  model={model}  (target wants {want}, pol={ear.target_pol:+.3f})")
    if not dark and model_first and model_first is not best:
        print(f"  model first pick     : {model_first['tag']:34}  cos={model_first['sem']:.3f} cent={model_first['cent']:.1f}  {model_first['tags']}")
    label = "model (ear deferred)" if dark else "ear-refined"
    print(f"  {label:21}: {best['tag']:34}  cos={best['sem']:.3f} cent={best['cent']:.1f}  {best['tags']}")
    print(f"  best.wav: {os.path.join(outdir, 'best.wav')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
