#!/usr/bin/env python3
"""LCO comprehension + Re-Prompt DIAGNOSTIC harness (NOT a pass/fail test).

Purpose: the LCO (language-controlled oscillator) is reported to understand
very little — it floods the user with flags — and Re-Prompt misbehaves (e.g.
"abduction converges on a prompt-prefix instead of abducting"). This harness
drives the REAL backend over IPC (the same stdin/stdout path the plugin uses,
so S2's live Qwen routing and the interpret() Re-Prompt calls actually fire —
NOT the llm_route=None unit path) and dumps a structured JSON report plus a
human summary, for a Sonnet agent to analyse and turn into prioritised fixes.

Two probes:
  A. AUTHORING  — a broad, systematic corpus (every synthesis type from the
     lexicon + realistic natural-language + German + should-flag edge cases) →
     mode:"dco". Captures resolved{} + flags[] and per-prompt comprehension
     metrics (flag rate, whether the technique was EXPLICIT or a flagged
     default, which words were dropped).
  B. RE-PROMPT  — for a spanning subset, bake once, build the machine reading
     the way the plugin does, then run every stance through mode:"interpret"
     with the ACTUAL C++ stance system prompts (mirrored below). Critically it
     runs each stance BOTH with and WITHOUT the #7 vocabulary constraint, so
     the agent can measure directly whether that constraint is strangling the
     leap-out stances. Each rewrite is re-baked so we can see how much of the
     LLM's own output the parser drops.

Finding (2026-07-11, commit 5c640be1): the split is stays-acoustic vs
leaves-acoustic, NOT "scene stances vs the rest". With the palette gated off,
abduction and verniedlicher leap (a phonograph; a cozy cave); the reframed
ACOUSTIC palette (not "the machine's vocabulary") stops the machine-speak on
opposite (25% -> 0%) and transcribe (50% -> 25%). So the palette ships ON for
transcribe/entkitscher/variation/opposite, OFF for abduction/verniedlicher.

Known limits of THIS harness (measurements are bounded, not gospel):
  - contains_synth_speak() is deliberately CONSERVATIVE: it does not flag bare
    "saw"/"sine"/"square" because those are legitimate BASE WAVEFORM palette
    words too — so machine-speak is UNDER-counted, never over-counted.
  - the live S2 adjective-routing is not captured raw, so silent mis-routes
    (residue "swells" forced onto adjective "airy") show only as a wrong
    resolved{}, not as an explicit S2 trace. (TODO: --dump-s2.)
  - run_reprompt passes recent=[] always, so the "already tried" diversity
    clause is never exercised; and it records the rebake FLAG COUNT but not the
    rebaked resolved{}, so a scene-leap win that is pyrrhic in SOUND is invisible.
  - n=4 seeds: strong effects (abduction 100% synth-speak) are trustworthy;
    mild ones (opposite's flag delta) are directional only.

Run (dev venv — backend needs torch/transformers; first call loads Qwen, ~10s):
  .venv/bin/python tools/lco_diagnostic.py --out /tmp/lco_report.json
  .venv/bin/python tools/lco_diagnostic.py --quick          # small corpus
  .venv/bin/python tools/lco_diagnostic.py --no-reprompt    # authoring only

The plugin uses the frozen backend, but the dev path is the same protocol and
the same dco_recipe/pipe_inference code (project rule: backend test tools stay
on the IPC subprocess path — this harness imports PipeClient from
test_dco_author, it does NOT direct-import the recipe author for the live run).
"""
import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_DIR = REPO_ROOT / "backend"
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(BACKEND_DIR))

# Reuse the real IPC client + dco helper (subprocess path). interpret() we add.
from test_dco_author import PipeClient, dco, BACKEND_SCRIPT  # noqa: E402


def interpret(client, system_prompt, user_text, max_new_tokens=64):
    """mode:interpret — the exact wire the Re-Prompt loop uses (system_prompt +
    prompt_a + max_new_tokens)."""
    raw = client.request_text({
        "mode": "interpret",
        "system_prompt": system_prompt,
        "prompt_a": user_text,
        "max_new_tokens": max_new_tokens,
    })
    return json.loads(raw) if raw.strip().startswith("{") else {"text": raw}


# ── stance system prompts — MIRRORED VERBATIM from src/inference/RepromptStances.cpp
#    (syspTranscribe/…/syspAbduction). Keep in sync; this harness exists to test
#    exactly these strings, so a paraphrase would defeat the purpose. ──────────
DASH = "—"  # em dash used in the C++ strings (fromUTF8)

SYSP = {
    "transcribe":
        "You are a machine transcription engine for a text-to-audio synthesizer. You "
        "receive ONLY machine measurements of the latest sound: the timbre words a "
        "neural ear matched, and signal-level spectral descriptors. Compose them "
        "LITERALLY into ONE short generation prompt (3 to 8 words) describing those "
        "measured qualities as a sound. Add NO scene, NO story, NO metaphor, NO place "
        "and NO human imagery " + DASH + " only the measured sonic attributes. "
        "Reply with ONLY the prompt - no quotes, no label, no explanation.",
    "abduction":
        "You are the interpreter of a text-to-audio synthesizer. You are given the bare "
        "timbre words a machine ear heard in a sound, plus the scenes already tried. Make "
        "an abductive leap: name a concrete, unexpected real-world scene or source that "
        "could plausibly PRODUCE such a sound, phrased as ONE short generation prompt "
        "(3 to 8 words). Each turn, leap to a scene CLEARLY DIFFERENT from the ones already "
        "tried - never repeat or lightly reword them. Be surprising but physically "
        "plausible. Reply with ONLY the prompt - no quotes, no label, no explanation.",
    "opposite":
        "You describe the exact diametral OPPOSITE of the sound. Invert both the things "
        "and their relations: bright becomes dark, fast becomes slow, hard becomes soft, "
        "calm becomes agitated, dense becomes sparse, near becomes far, growth becomes "
        "decay. Each turn invert the CURRENT sound into its contrary, clearly different "
        "from the opposites already tried. "
        "Reply with ONLY one short prompt (3 to 8 words) - no quotes, no label.",
    "entkitscher":
        "You are a sound engineer writing plain notes. Rewrite the current prompt as a "
        "sober, factual description of the SAME sound: name the physical sound and its "
        "source in neutral acoustic words, leaving out emotion, story and atmosphere. "
        "Example: \"the warm embrace of a mother's lullaby\" becomes \"soft low vocal "
        "hum\". Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.",
    "verniedlicher":
        "You are a narrator of gentle magical realism for sound. Rewrite the prompt so it "
        "feels emotionally safe for a child yet sonically fascinating: turn the "
        "threatening into the wondrous and the harsh into the mysterious " + DASH + " REINTERPRET, "
        "do not censor (a conflict becomes a riddle, darkness becomes shelter). Keep the "
        "core but heal its impact, an aesthetic of warmth and wonder. "
        "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.",
    "variation":
        "You are the Prompt-B variation engine of a text-to-audio synthesizer. "
        "Each turn you receive the current Prompt B and the timbres a machine ear hears "
        "in the latest rendered sound. Write ONE new short Prompt B (3 to 8 words) that "
        "VARIES the current one: keep its spirit and the family of the sound, but shift "
        "the imagery in a fresh musical direction suggested by what is heard. "
        "Reply with ONLY the new prompt - no quotes, no label, no explanation.",
}

# Whether each stance is SUPPOSED to stay inside synth vocabulary (transcribe,
# variation) or to leap OUT to real-world scene/imagery (abduction, opposite,
# verniedlicher, entkitscher). The #7 vocab constraint only makes sense for the
# first group; on the second it is a hypothesised failure cause. The harness
# runs BOTH ways regardless so the data decides.
SCENE_STANCES = {"abduction", "opposite", "verniedlicher", "entkitscher"}


def build_dco_user_turn(stance, heard_tags, heard_spectral, flags_line, prev, recent):
    """MIRROR of RepromptStances::buildDcoStanceUserTurn (the LCO variant).

    Since 2026-07-28 this builder is handed the EAR's output — CLAP tags plus the
    computed spectral words, separately labelled — not a reading of the recipe.
    The labels changed with it: nothing here may claim the code was READ.
    """
    tried = ("\nAlready tried (do not reuse): " + " / ".join(recent)) if recent else ""
    spec = ("\nSpectral: " + heard_spectral) if heard_spectral else ""
    if stance == "transcribe":
        notu = ("\nNot understood: " + flags_line) if flags_line else ""
        return "Neural ear: " + heard_tags + spec + notu
    if stance == "abduction":
        return "Heard: " + heard_tags + spec + tried
    if stance == "opposite":
        return 'Current prompt: "' + prev + '"\nHeard: ' + heard_tags + spec + tried
    if stance in ("entkitscher", "verniedlicher"):
        return 'Current prompt: "' + prev + '"\nHeard: ' + heard_tags + spec
    if stance == "variation":
        return 'Current prompt: "' + prev + '"\nHeard now: ' + heard_tags + spec
    return ""


def vocab_constraint(brief):
    """MIRROR of RepromptStances::dcoVocabularyConstraintBlock."""
    if not brief.strip():
        return ""
    return ("\n\nDescribe the sound itself - its spectral character, harmonic "
            "structure, and movement - in the acoustic qualities below. Stay "
            "within them: they are what this instrument can actually shape, so a "
            "sound described in them is one it can render. Recombine them freely.\n\n"
            + brief.strip())


def machine_reading_from(resp):
    """MIRROR of PromptPanel.cpp's machineReading construction from resolved+recipe."""
    resolved = resp.get("resolved", {})
    recipe = resp.get("recipe", {})
    parts = []
    tech = resolved.get("technique") or ""
    if tech and tech != "?":
        parts.append("technique: " + tech)
    if resolved.get("adjectives"):
        parts.append("adjectives: " + ", ".join(resolved["adjectives"]))
    if resolved.get("motion"):
        parts.append("motion: " + ", ".join(resolved["motion"]))
    vals = resolved.get("values") or {}
    if vals:
        parts.append("values: " + ", ".join(f"{k}={v}" for k, v in vals.items()))
    rate = recipe.get("motion_rate_hz")
    if isinstance(rate, (int, float)) and rate > 0:
        parts.append(f"motion rate {rate:.2f} Hz")
    parts.append("frames " + str(recipe.get("frames")))
    kinds = [kf.get("kind", "saw") for kf in recipe.get("keyframes", [])]
    if kinds:
        parts.append("shapes: " + ", ".join(kinds))
    return "; ".join(parts)


def flags_line_from(resp):
    return "; ".join(f'{f.get("word")} ({f.get("reason")})' for f in resp.get("flags", []))


# ── corpus ──────────────────────────────────────────────────────────────────
STOPWORDS_HINT = {"a", "an", "the", "with", "that", "and", "of", "very", "slightly",
                  "into", "sound", "tone", "some", "kind", "like", "it", "is"}


def build_corpus(quick=False):
    """Systematic (every technique + representative adjective/motion, from the
    lexicon) + realistic natural-language + German + should-flag edge cases."""
    import dco_recipe as dr  # direct import ONLY to enumerate the lexicon for corpus gen
    lex = dr.load_lexicon()
    tech_forms = [t["surface_forms"][0] for t in lex["techniques"]]
    adj_forms = [a["surface_forms"][0] for a in lex["adjectives"]]
    mot_forms = [m["surface_forms"][0] for m in lex["motions"]]

    corpus = {}
    # every technique, bare — a pure recognition floor
    corpus["technique_bare"] = list(tech_forms if not quick else tech_forms[:6])
    # technique + one adjective — the common real case
    corpus["technique_adjective"] = [f"{a} {t}" for t, a in
                                     zip(tech_forms, adj_forms)][: (6 if quick else 25)]
    # technique + motion
    corpus["technique_motion"] = [f"{t} {m}" for t, m in
                                  zip(tech_forms, mot_forms)][: (5 if quick else 15)]
    # the feature families shipped in C/D
    corpus["composition"] = ["only odd harmonics", "boost the 3rd harmonic",
                             "saw, attenuate every 2nd harmonic", "square, 8 harmonics"]
    corpus["fm_relative"] = ["bell deeper modulation", "electric piano higher ratio",
                             "fm bell less modulation", "metal, lower ratio"]
    corpus["drive_inharm"] = ["distorted saw", "gritty acid bass", "glassy bell",
                              "aggressive metallic lead", "dirty overdriven bass"]
    corpus["morph_chain"] = ["saw into square", "sine into square into triangle",
                             "warm saw morphing into a glassy bell"]
    corpus["typed_values"] = ["50% pulse", "2 op fm ratio 3 index 2.5", "waveshaper order 5"]
    # realistic natural-language — what a user actually types; the real gap
    corpus["natural_language"] = [
        "a warm hollow pad that slowly opens up",
        "gritty acid bassline with movement",
        "shimmering glass bell that slowly decays",
        "aggressive metallic lead stab",
        "soft breathy flute tone",
        "dark evolving drone",
        "punchy fm bass",
        "hollow wooden clarinet that swells",
        "bright detuned supersaw chord",
        "warm vintage electric piano",
        "nasal reedy oboe-like tone",
        "icy digital bell pluck",
    ]
    corpus["german"] = [
        "fetter Moog-Bass", "hohler Klarinettenton", "warmes Rechteck, sehr weich",
        "heller Sägezahn der sich langsam öffnet", "glasige Glocke, metallisch",
    ]
    corpus["should_flag_mood"] = ["warm evening nostalgia", "the sound of loneliness",
                                  "childhood summer afternoon"]
    corpus["should_flag_nonsense"] = ["quantum banana photosynthesis", "purple velvet Tuesday"]
    if quick:
        corpus = {k: v[:4] for k, v in corpus.items()
                  if k in ("technique_bare", "technique_adjective", "natural_language",
                           "drive_inharm", "should_flag_mood")}
    return corpus


def est_content_words(prompt):
    toks = [w for w in re.findall(r"[a-zA-Z0-9%äöüß]+", prompt.lower()) if w not in STOPWORDS_HINT]
    return len(toks)


def run_authoring(client, corpus):
    rows = []
    for category, prompts in corpus.items():
        for p in prompts:
            try:
                resp, _ = dco(client, p)
            except Exception as e:
                rows.append({"category": category, "prompt": p, "error": repr(e)})
                continue
            resolved = resp.get("resolved", {})
            flags = resp.get("flags", [])
            residue = [f["word"] for f in flags if "no mapping" in f.get("reason", "")]
            # technique defaulted (inferred, not explicitly named) → flagged with 'inferred'/'default'
            tech_defaulted = any(("defaul" in f.get("reason", "").lower()
                                  or "no technique" in f.get("reason", "").lower()
                                  or "inferred" in f.get("reason", "").lower())
                                 for f in flags)
            n_content = est_content_words(p)
            rows.append({
                "category": category,
                "prompt": p,
                "technique": resolved.get("technique"),
                "technique_defaulted": tech_defaulted,
                "adjectives": resolved.get("adjectives", []),
                "composition": resolved.get("composition", []),
                "fm": resolved.get("fm", []),
                "motion": resolved.get("motion", []),
                "values": resolved.get("values", {}),
                "n_flags": len(flags),
                "residue_words": residue,
                "flags": [{"word": f.get("word"), "reason": f.get("reason")} for f in flags],
                "n_content_words_est": n_content,
                "flag_rate": (len(residue) / n_content) if n_content else 0.0,
                "recipe_kinds": [kf.get("kind") for kf in resp.get("recipe", {}).get("keyframes", [])],
            })
    return rows


def looks_like_reading_echo(rewrite, machine_reading):
    """Heuristic for the reported abduction failure: the rewrite parrots the
    machine-reading jargon / its prefix instead of producing a fresh prompt."""
    rw = rewrite.lower().strip()
    jargon = ("technique:", "adjectives:", "motion:", "frames", "shapes:", "fm2",
              "motion rate", "machine reading", "the oscillator does")
    if any(j in rw for j in jargon):
        return True
    head = machine_reading.lower()[:20]
    return bool(head) and rw.startswith(head[:12])


# Synth-shop-talk terms a real-world SCENE abduction ("a phonograph on vinyl")
# would never use. Their presence in a scene-stance rewrite is the direct signal
# that the vocab constraint dragged the LLM back into describing the SYNTHESIS
# instead of leaping to a source — the reported "doesn't abduct" failure.
_SYNTH_SPEAK = ("synthesizer", "synth ", "oscillat", "waveform", "wave form", " fm ",
                "additive", "harmonic", "modulation", " hz", "sawtooth", "square wave",
                "sine wave", "timbre", "spectral", "sideband", "vco", "vca", "lfo",
                "patch", "preset", "filter cutoff", "resonance")


def contains_synth_speak(text):
    t = " " + text.lower() + " "
    return any(term in t for term in _SYNTH_SPEAK)


def run_reprompt(client, corpus, brief, max_new_tokens=64):
    # OBSOLETE SHAPE (2026-07-28). This probe fed the stance turns a *reading* of
    # the router's recipe. The product's Re-Prompt step now renders the authored
    # orchestra and hands the turn CLAP tags + spectral words (see
    # build_dco_user_turn above, and PromptPanel::triggerDcoReprompt). This harness
    # has no renderer, so it cannot produce that input, and measuring the old shape
    # would certify a turn the product no longer emits — which is worse than not
    # measuring. Reviving it means giving it an ear (a Csound render + the backend's
    # `analyze` op), not passing a reading in the tags slot.
    raise SystemExit(
        "run_reprompt is obsolete: the LCO Re-Prompt turn is built from a CLAP "
        "description of a rendered probe since 2026-07-28, and this harness has no "
        "renderer. Give it one (Csound render -> mode:'analyze') before re-enabling."
    )
    # A spanning subset: one FM, one additive/spectral, one analog, one natural.
    seeds = ["glassy bell", "warm hollow clarinet", "gritty distorted bass",
             "a warm hollow pad that slowly opens up"]
    results = []
    for seed in seeds:
        resp, _ = dco(client, seed)
        mr = machine_reading_from(resp)
        fl = flags_line_from(resp)
        for stance, sysp in SYSP.items():
            for vocab_mode in ("with_vocab", "without_vocab"):
                # The ear's two halves, which this harness cannot produce (no
                # renderer) -- see run_reprompt's SystemExit. Empty ON PURPOSE.
                user_turn = build_dco_user_turn(stance, "", "", fl, seed, [])
                if vocab_mode == "with_vocab":
                    user_turn = user_turn + vocab_constraint(brief)
                try:
                    r = interpret(client, sysp, user_turn, max_new_tokens)
                    rewrite = (r.get("text") or "").strip()
                except Exception as e:
                    rewrite = f"<error: {e!r}>"
                # re-bake the rewrite to see how much the parser keeps
                rebake_flags = None
                if rewrite and not rewrite.startswith("<error"):
                    try:
                        rb, _ = dco(client, rewrite)
                        rebake_flags = len(rb.get("flags", []))
                    except Exception:
                        pass
                results.append({
                    "seed": seed,
                    "stance": stance,
                    "is_scene_stance": stance in SCENE_STANCES,
                    "vocab_mode": vocab_mode,
                    "machine_reading": mr,
                    "user_turn": user_turn,
                    "rewrite": rewrite,
                    "reading_echo": looks_like_reading_echo(rewrite, mr),
                    "synth_speak": contains_synth_speak(rewrite),
                    "rebake_flag_count": rebake_flags,
                })
    return results


def summarize(author_rows, reprompt_rows):
    lines = ["", "=" * 72, "LCO DIAGNOSTIC SUMMARY", "=" * 72]
    # authoring: flag rate + defaulted-technique rate per category
    lines.append("\nAUTHORING — comprehension by category:")
    lines.append(f"  {'category':22s} {'n':>3s} {'avg_flags':>9s} {'defaulted%':>10s} {'residue%':>8s}")
    from collections import defaultdict
    cat = defaultdict(list)
    for r in author_rows:
        if "error" not in r:
            cat[r["category"]].append(r)
    for c, rows in cat.items():
        n = len(rows)
        avg_flags = sum(r["n_flags"] for r in rows) / n
        defaulted = 100 * sum(1 for r in rows if r["technique_defaulted"]) / n
        avg_residue = 100 * sum(r["flag_rate"] for r in rows) / n
        lines.append(f"  {c:22s} {n:3d} {avg_flags:9.1f} {defaulted:9.0f}% {avg_residue:7.0f}%")
    # worst residue words (things the system did not understand, ranked)
    from collections import Counter
    resid = Counter(w for r in author_rows for w in r.get("residue_words", []))
    lines.append("\n  Most-flagged (unmapped) words — candidate lexicon gaps:")
    lines.append("   " + ", ".join(f"{w}×{n}" for w, n in resid.most_common(30)))
    # reprompt: scene-stance echo rate with vs without the vocab constraint
    if reprompt_rows:
        lines.append("\nRE-PROMPT — scene-stance failure (leap OUT expected; synth-speak/echo = failed):")
        for mode in ("with_vocab", "without_vocab"):
            scene = [r for r in reprompt_rows if r["is_scene_stance"] and r["vocab_mode"] == mode]
            if not scene:
                continue
            echo = 100 * sum(1 for r in scene if r["reading_echo"]) / len(scene)
            synth = 100 * sum(1 for r in scene if r["synth_speak"]) / len(scene)
            lines.append(f"  {mode:15s}: synth-speak {synth:.0f}%  |  reading-echo {echo:.0f}%  (n={len(scene)})")
        lines.append("\n  Sample abduction rewrites (seed → with / without vocab):")
        for seed in sorted({r["seed"] for r in reprompt_rows}):
            w = next((r for r in reprompt_rows if r["seed"] == seed and r["stance"] == "abduction"
                      and r["vocab_mode"] == "with_vocab"), None)
            wo = next((r for r in reprompt_rows if r["seed"] == seed and r["stance"] == "abduction"
                       and r["vocab_mode"] == "without_vocab"), None)
            if w and wo:
                lines.append(f"   {seed!r}")
                lines.append(f"      with:    {w['rewrite']!r}")
                lines.append(f"      without: {wo['rewrite']!r}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(REPO_ROOT / "tools" / "lco_diagnostic_report.json"))
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--no-reprompt", action="store_true")
    ap.add_argument("--max-new-tokens", type=int, default=64)
    args = ap.parse_args()

    if not BACKEND_SCRIPT.is_file():
        print(f"backend script missing: {BACKEND_SCRIPT}", file=sys.stderr)
        sys.exit(2)

    import dco_recipe as dr
    brief = dr.reference_vocabulary()

    command = [sys.executable, str(BACKEND_SCRIPT)]
    print(f"Spawning backend: {' '.join(command)}", file=sys.stderr)
    client = PipeClient(command)
    try:
        corpus = build_corpus(quick=args.quick)
        n = sum(len(v) for v in corpus.values())
        print(f"AUTHORING: {n} prompts across {len(corpus)} categories...", file=sys.stderr)
        author_rows = run_authoring(client, corpus)
        reprompt_rows = []
        if not args.no_reprompt:
            print("RE-PROMPT: 4 seeds x 6 stances x {with,without vocab}...", file=sys.stderr)
            reprompt_rows = run_reprompt(client, corpus, brief, args.max_new_tokens)
    finally:
        client.close()

    report = {"authoring": author_rows, "reprompt": reprompt_rows,
              "reference_vocabulary": brief}
    Path(args.out).write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(summarize(author_rows, reprompt_rows))
    print(f"\nFull JSON report: {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
