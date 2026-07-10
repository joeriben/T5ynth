#!/usr/bin/env python3
"""Empirical calibration harness for the DCO Re-Prompt loop
(docs/DCO_REPROMPT_CONCEPT.md, src/inference/RepromptStances.cpp,
src/gui/PromptPanel.cpp triggerDcoBake / triggerDcoReprompt).

The DCO Re-Prompt loop feeds Qwen a stance system prompt + a user turn
carrying the DCO router's own machine-readable reading of its last bake
(technique/adjectives/motion/values/flags), then re-authors Qwen's reply
through the DCO lexicon (docs/DCO_LLM_GUARDRAILS.md S0-S4). Whether the six
shipped stances (transcribe/entkitscher/verniedlicher/variation/abduction/
opposite) produce lexicon-VIABLE prompts -- words the router can actually
map -- is an EMPIRICAL question. This tool answers it over the REAL IPC
backend, REAL Qwen2.5-1.5B-Instruct, REAL author pipeline (never a direct
torch/model import -- project rule: backend test tools stay on the IPC
subprocess path, mirroring tools/test_dco_author.py).

This is a REPORT tool, not a PASS/FAIL assertion suite: a stance writing
unmappable poetry is the FINDING, not a bug. The only hard fails are
MECHANICAL (the machinery breaking): an IPC/transport error, an author()
call that doesn't come back ok:true, or an interpret() reply that cleans
down to nothing.

One full chain, per (stance, seed prompt):
    author(seed)                                   -- priming bake, like a
                                                       user's manual BAKE click
    for i in 1..iters:                              -- one STEP click each
        interpret(stanceSysp, buildDcoStanceUserTurn(...)) -> raw
        cleaned = _clean_prompt(raw)                -- mechanical check: non-empty
        author(cleaned)                             -- triggerDcoReprompt's
                                                       trailing triggerDcoBake()
This exactly mirrors triggerDcoBake() + 3x triggerDcoReprompt() in
src/gui/PromptPanel.cpp: each STEP interprets using the PREVIOUS bake's own
machine reading, then immediately re-bakes the cleaned reply so the NEXT
STEP reads THAT bake's reading -- never a stale one.

Run (dev venv -- the backend needs torch/transformers):
    .venv/bin/python tools/test_dco_reprompt.py            # full 6x3x3
    .venv/bin/python tools/test_dco_reprompt.py --quick     # 2x2x2 smoke run
"""
from __future__ import annotations

import argparse
import json
import statistics
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_DIR = REPO_ROOT / "backend"
BACKEND_SCRIPT = BACKEND_DIR / "pipe_inference.py"
TOOLS_DIR = Path(__file__).resolve().parent

# ─────────────────────────────────────────────────────────────────────────
# Stance system prompts: Python source of truth
# ─────────────────────────────────────────────────────────────────────────
# The MODES dict (and _clean_prompt, the function RepromptStances::cleanPrompt
# is a port of) lives in tools/clap_llm_loop.py -- NOT backend/. Importing the
# originals instead of pasting copies keeps this harness drift-proof against
# the C++ side, which is ported from the same file ("Keep in sync").
sys.path.insert(0, str(TOOLS_DIR))
import clap_llm_loop as cll  # noqa: E402
from clap_llm_loop import _clean_prompt  # noqa: E402  (RepromptStances::cleanPrompt is its C++ port)

REQUIRED_STANCES = ["transcribe", "entkitscher", "verniedlicher", "variation", "abduction", "opposite"]
_missing = [k for k in REQUIRED_STANCES if k not in cll.MODES]
if _missing:
    print(f"FATAL: tools/clap_llm_loop.py MODES is missing required stance key(s): {_missing}. "
          f"Present keys: {sorted(cll.MODES.keys())}. Cannot proceed without a Python source of "
          f"truth for these stances' system prompts.", file=sys.stderr)
    sys.exit(2)

# 5 of the 6 stances' system prompts are FACTORY-CONSTANT in clap_llm_loop.py
# (the outer _mode_X(header_a, header_b, target) closure never interpolates
# those three args into `sysp` -- only the per-turn `build()` reads its own
# args). Verified by direct reading of tools/clap_llm_loop.py AND cross-
# checked byte-for-byte against src/inference/RepromptStances.cpp's syspXxx()
# functions before writing this file. Self-checked again below at runtime.
_STATIC_STANCE_KEYS = ["transcribe", "entkitscher", "verniedlicher", "abduction", "opposite"]


def _extract_sysp(key: str, header_a="", header_b="", target=""):
    """MODES[key](header_a, header_b, target) returns a build(tags, prev_b,
    recent, spectral="") closure that returns (sysp, user_text); we only
    want the sysp half, using harmless empty per-turn args."""
    build = cll.MODES[key](header_a, header_b, target)
    sysp, _user_text = build("", "", [])
    return sysp


def _assert_header_invariant(key: str) -> str:
    """Safety net for the "5 static stances" assumption above: prove sysp is
    IDENTICAL regardless of header_a/header_b/target before trusting it as a
    faithful stand-in for the DCO C++ port (which never receives an A/B-pole
    header at all -- docs/DCO_REPROMPT_CONCEPT.md, "kein Gegenstück")."""
    a = _extract_sysp(key, "", "", "")
    b = _extract_sysp(key, "SOME OTHER PROMPT A", "SOME OTHER PROMPT B", "SOME TARGET")
    if a != b:
        raise RuntimeError(
            f"tools/clap_llm_loop.py MODES[{key!r}] sysp is NOT header-invariant "
            f"(changed when header_a/header_b/target changed) -- the 'import the Python "
            f"sysp verbatim' assumption used for this stance is WRONG. Re-derive from "
            f"src/inference/RepromptStances.cpp instead, the way 'variation' is handled below."
        )
    return a


# "variation" is the ONE documented, intentional exception (RepromptStances.cpp
# syspVariation()'s own comment): tools/clap_llm_loop.py._mode_variation bakes a
# FIXED Prompt-A header into its system prompt ('The fixed identity (Prompt A)
# is: "..."'), because the neural loop has two poles (A/B). The DCO paradigm has
# ONE prompt field -- there is no "Prompt A" to pin -- so RepromptStances.cpp
# ships a DIFFERENT, header-free system prompt for "variation". This is the C++
# author's own documented divergence, not a bug this harness should paper over.
# Transliterated VERBATIM from RepromptStances.cpp::syspVariation().
# Keep in sync with src/inference/RepromptStances.cpp::syspVariation() -- the
# C++ side is authoritative.
CPP_VARIATION_SYSP = (
    "You are the Prompt-B variation engine of a text-to-audio synthesizer. "
    "Each turn you receive the current Prompt B and the timbres a machine ear hears "
    "in the latest rendered sound. Write ONE new short Prompt B (3 to 8 words) that "
    "VARIES the current one: keep its spirit and the family of the sound, but shift "
    "the imagery in a fresh musical direction suggested by what is heard. "
    "Reply with ONLY the new prompt - no quotes, no label, no explanation."
)

SYSTEM_PROMPTS: dict[str, str] = {}
_SPEC_MISMATCHES: list[str] = []
for _k in _STATIC_STANCE_KEYS:
    SYSTEM_PROMPTS[_k] = _assert_header_invariant(_k)

_py_variation_sysp = _extract_sysp("variation", "SOME PROMPT A", "SOME PROMPT B", "target")
if _py_variation_sysp == CPP_VARIATION_SYSP:
    _SPEC_MISMATCHES.append(
        "UNEXPECTED: tools/clap_llm_loop.py._mode_variation now produces the SAME sysp as "
        "RepromptStances.cpp::syspVariation() -- the documented divergence (fixed Prompt-A "
        "header vs. header-free) may have been resolved upstream; re-check whether this "
        "harness can now just import 'variation' like the other five."
    )
else:
    _SPEC_MISMATCHES.append(
        "CONFIRMED (documented, intentional): tools/clap_llm_loop.py._mode_variation's sysp "
        "interpolates a fixed Prompt-A header ('The fixed identity (Prompt A) is: \"...\"') "
        "that has no DCO equivalent (DCO has one prompt field, not A/B poles). "
        "src/inference/RepromptStances.cpp::syspVariation() ships a header-free variant "
        "instead, with a code comment explaining exactly this. This harness uses the "
        "C++-authoritative text (CPP_VARIATION_SYSP above) for 'variation', NOT an import."
    )
SYSTEM_PROMPTS["variation"] = CPP_VARIATION_SYSP

assert set(SYSTEM_PROMPTS) == set(REQUIRED_STANCES), (set(SYSTEM_PROMPTS), REQUIRED_STANCES)


# ─────────────────────────────────────────────────────────────────────────
# DCO user-turn builder -- transliterated from C++, no Python equivalent
# exists (buildDcoStanceUserTurn is a DCO-only function; the C++ file's own
# comment calls it "the self-READING twin of buildStanceUserTurn").
# Keep in sync with src/inference/RepromptStances.cpp::buildDcoStanceUserTurn
# -- the C++ side is authoritative.
# ─────────────────────────────────────────────────────────────────────────

def build_dco_stance_user_turn(stance_key: str, machine_reading: str, flags_line: str,
                                prev_prompt: str, recent: list[str]) -> str:
    def tried_clause() -> str:
        if not recent:
            return ""
        return "\nAlready tried (do not reuse): " + " / ".join(recent)

    if stance_key == "transcribe":
        not_understood = f"\nNot understood: {flags_line}" if flags_line else ""
        return f"Machine reading: {machine_reading}{not_understood}"
    if stance_key == "abduction":
        return f"The oscillator does: {machine_reading}{tried_clause()}"
    if stance_key == "opposite":
        return (f'Current prompt: "{prev_prompt}"\nThe machine read it as: '
                f'{machine_reading}{tried_clause()}')
    if stance_key == "entkitscher":
        return f'Current prompt: "{prev_prompt}"\nMachine reading: {machine_reading}'
    if stance_key == "verniedlicher":
        return f'Current prompt: "{prev_prompt}"\nMachine reading: {machine_reading}'
    if stance_key == "variation":
        return f'Current prompt: "{prev_prompt}"\nThe machine read it as: {machine_reading}'
    return ""   # "off" or unknown


# ─────────────────────────────────────────────────────────────────────────
# machineReading / flagsLine -- transliterated from the C++ GUI, not a
# separate Python module (this block lives inline in triggerDcoBake).
# Keep in sync with src/gui/PromptPanel.cpp::triggerDcoBake (search
# "machineReading") -- the C++ side is authoritative.
# ─────────────────────────────────────────────────────────────────────────

def build_machine_reading(resp: dict) -> tuple[str, str, str, list[dict]]:
    """Returns (reading, flagsLine, technique, flags_list)."""
    resolved = resp.get("resolved") or {}
    recipe = resp.get("recipe") or {}
    flags = resp.get("flags") or []

    technique = resolved.get("technique") or ""
    if not technique:
        technique = "?"   # PromptPanel.cpp: `if (technique.isEmpty()) technique = "?";`

    parts = []
    if technique and technique != "?":
        parts.append(f"technique: {technique}")
    adjectives = resolved.get("adjectives") or []
    if adjectives:
        parts.append("adjectives: " + ", ".join(str(a) for a in adjectives))
    motion = resolved.get("motion") or []
    if motion:
        parts.append("motion: " + ", ".join(str(m) for m in motion))
    values = resolved.get("values") or {}
    if values:
        parts.append("values: " + ", ".join(f"{k}={v}" for k, v in values.items()))
    motion_rate_hz = recipe.get("motion_rate_hz") or 0.0
    if isinstance(motion_rate_hz, (int, float)) and motion_rate_hz > 0.0:
        parts.append(f"motion rate {motion_rate_hz:.2f} Hz")
    parts.append(f"frames {recipe.get('frames')}")   # ALWAYS added, unconditional in the C++
    keyframes = recipe.get("keyframes") or []
    if keyframes:
        kinds = [kf.get("kind", "saw") for kf in keyframes]
        parts.append("shapes: " + ", ".join(str(k) for k in kinds))
    reading = "; ".join(parts)

    flag_parts = [f"{f.get('word')} ({f.get('reason')})" for f in flags]
    flags_line = "; ".join(flag_parts)

    return reading, flags_line, technique, flags


def no_mapping_words(flags: list[dict]) -> list[str]:
    """Words flagged with the exact S2 'could not map at all' reason
    (backend/dco_recipe.py: 'no mapping — ignored', _run_s2). Distinct from
    other flag reasons (also-mentioned priority collisions, capped chains,
    spectral-adjective inapplicability, etc.) which are not "no mapping"."""
    return [f.get("word") for f in flags if "no mapping" in (f.get("reason") or "")]


def is_fallback(resp: dict) -> bool:
    """"fallback" = the router had no technique to route: resolved.technique
    empty/"?" (defensive; per backend/dco_recipe.py _resolve_technique S1
    ALWAYS defaults to 'saw' or 'square', so this arm never fires in practice)
    OR the response carries a 'no technique named — defaulted/inferred' flag
    (the signal that actually fires on a total vocabulary miss)."""
    technique = (resp.get("resolved") or {}).get("technique") or ""
    if technique in ("", "?"):
        return True
    flags = resp.get("flags") or []
    return any("no technique named" in (f.get("reason") or "") for f in flags)


# ─────────────────────────────────────────────────────────────────────────
# IPC transport -- mirrors tools/test_dco_author.py's PipeClient verbatim
# (project rule: backend test tools stay on the IPC subprocess path, never a
# direct model import). \x03 = text frame (byte 0x03 + uint32 LE length +
# UTF-8 JSON/text), the same frame interpret/dco/translate/analyze all use.
# ─────────────────────────────────────────────────────────────────────────

class PipeProtocolError(RuntimeError):
    pass


class PipeClient:
    def __init__(self, command):
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=None, text=False,
        )
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.info = self._read_ready()

    def close(self):
        try:
            if self.stdin and not self.stdin.closed:
                self.stdin.close()
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)

    def _read_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.stdout.read(n - len(buf))
            if not chunk:
                raise PipeProtocolError(
                    f"Backend closed pipe while reading {n} bytes (got {len(buf)})"
                )
            buf.extend(chunk)
        return bytes(buf)

    def _read_ready(self):
        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x02":
            raise PipeProtocolError(f"Unexpected ready byte: {head!r}")
        n = struct.unpack("<H", self._read_exact(2))[0]
        return json.loads(self._read_exact(n).decode("utf-8"))

    def request_text(self, payload):
        """Send a request, read a TEXT frame (\\x03 + uint32 len + UTF-8)."""
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.stdin.write(data)
        self.stdin.flush()
        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x03":
            raise PipeProtocolError(f"Unexpected response byte: {head!r}")
        n = struct.unpack("<I", self._read_exact(4))[0]
        return self._read_exact(n).decode("utf-8", "replace")


def dco(client, text, frames=None):
    """Mirrors PipeInference::authorDcoRecipe's wire request EXACTLY: {"mode":
    "dco", "text": text} plus "frames" ONLY if > 0 -- no "device" field ever
    (src/inference/PipeInference.cpp::authorDcoRecipe)."""
    payload = {"mode": "dco", "text": text}
    if frames is not None and frames > 0:
        payload["frames"] = frames
    raw = client.request_text(payload)
    return json.loads(raw), raw


def interpret(client, system_prompt, user_text, max_new_tokens, device):
    """Mirrors PipeInference::interpret's wire request EXACTLY: mode/
    system_prompt/prompt_a/max_new_tokens always; "device" ONLY if non-empty
    (src/inference/PipeInference.cpp::interpret)."""
    payload = {
        "mode": "interpret",
        "system_prompt": system_prompt,
        "prompt_a": user_text,
        "max_new_tokens": int(max_new_tokens),
    }
    if device:
        payload["device"] = device
    return client.request_text(payload)


# ─────────────────────────────────────────────────────────────────────────
# The three seed prompts (task spec) and stance order (matches
# RepromptStance::kEntries in src/dsp/BlockParams.h, off excluded).
# ─────────────────────────────────────────────────────────────────────────

SEEDS = [
    "a warm saw wave, slow wobble",
    "a sine wave morphing into a square wave",
    "glassy bell shimmering in the distance",   # deliberately flag-heavy
]
ALL_STANCES = ["transcribe", "entkitscher", "verniedlicher", "variation", "abduction", "opposite"]
MAX_NEW_TOKENS = 64   # same as triggerDcoReprompt's interpret() call


def truncate(s, n=64):
    s = s or ""
    return s if len(s) <= n else s[: n - 1] + "…"


def author_step(client, text, failures, where):
    """One author() call + mechanical checks (IPC error / author op error).
    Returns (resp, reading, flags_line, technique, flags) or None on a
    mechanical failure (already appended to `failures`)."""
    try:
        resp, raw = dco(client, text)
    except PipeProtocolError as e:
        failures.append(f"{where}: IPC error calling author(dco) on {text!r}: {e}")
        return None
    except json.JSONDecodeError as e:
        failures.append(f"{where}: author(dco) returned non-JSON for {text!r}: {e}")
        return None
    if not isinstance(resp, dict) or resp.get("ok") is not True:
        failures.append(f"{where}: author(dco) did not return ok:true for {text!r}: {resp!r}")
        return None
    for required_key in ("recipe", "resolved", "flags"):
        if required_key not in resp:
            failures.append(f"{where}: author(dco) response missing {required_key!r} for {text!r}")
            return None
    reading, flags_line, technique, flags = build_machine_reading(resp)
    return resp, reading, flags_line, technique, flags


def run_chain(client, stance_key, seed_prompt, iters, device, failures, report):
    """One full (stance, seed) chain: 1 priming author() + `iters` x
    (interpret -> clean -> author). Returns the list of per-iteration
    records (dicts) actually completed (may be short if a mechanical
    failure cut the chain off)."""
    where0 = f"[{stance_key} / seed={truncate(seed_prompt, 40)!r}]"
    report(f"\n{'=' * 78}")
    report(f"STANCE: {stance_key}   SEED: {seed_prompt!r}")
    report("=" * 78)

    step0 = author_step(client, seed_prompt, failures, f"{where0} priming author(seed)")
    if step0 is None:
        report("  MECHANICAL FAILURE priming this chain -- skipping (see MECHANICS below).")
        return []
    resp, reading, flags_line, technique, flags = step0
    fb = is_fallback(resp)
    words = no_mapping_words(flags)
    report(f"  SEED   {seed_prompt!r}")
    report(f"    author -> technique={technique}  fallback={fb}  "
           f"flags: {len(flags)} total, no-mapping: {words or 'none'}")
    report(f"    reading: {reading}")
    if flags_line:
        report(f"    all flags: {flags_line}")

    prev = seed_prompt
    recent = [seed_prompt]   # seeded with the start prompt (triggerDcoBake's dcoLoopRecent_)
    records = []

    for i in range(1, iters + 1):
        where = f"{where0} iter {i}"
        sysp = SYSTEM_PROMPTS[stance_key]
        user_turn = build_dco_stance_user_turn(stance_key, reading, flags_line, prev, recent)
        try:
            raw_reply = interpret(client, sysp, user_turn, MAX_NEW_TOKENS, device)
        except PipeProtocolError as e:
            failures.append(f"{where}: IPC error calling interpret(): {e}")
            report(f"  [{i}] MECHANICAL FAILURE calling interpret() -- chain stops here.")
            break

        cleaned = _clean_prompt(raw_reply)
        if not cleaned:
            failures.append(
                f"{where}: cleaned interpret() reply is EMPTY (raw={raw_reply!r}, "
                f"user_turn={user_turn!r})"
            )
            report(f"  [{i}] MECHANICAL FAILURE -- cleaned reply is empty "
                   f"(raw={raw_reply!r}) -- chain stops here.")
            break

        step_i = author_step(client, cleaned, failures, f"{where} author(cleaned)")
        if step_i is None:
            report(f"  [{i}] MECHANICAL FAILURE re-baking the cleaned reply -- chain stops here.")
            break
        resp_i, reading_i, flags_line_i, technique_i, flags_i = step_i
        fb_i = is_fallback(resp_i)
        words_i = no_mapping_words(flags_i)

        report(f"  [{i}] {prev!r}")
        report(f"      Qwen raw -> {raw_reply!r}")
        report(f"      cleaned  -> {cleaned!r}")
        report(f"      author   -> technique={technique_i}  fallback={fb_i}  "
               f"flags: {len(flags_i)} total, no-mapping: {words_i or 'none'}")
        report(f"      reading  -> {reading_i}")
        if flags_line_i:
            report(f"      all flags -> {flags_line_i}")

        records.append({
            "iter": i, "prompt_in": prev, "raw_reply": raw_reply, "cleaned": cleaned,
            "technique": technique_i, "fallback": fb_i, "flags": flags_i,
            "no_mapping_words": words_i, "reading": reading_i,
        })

        # advance state exactly like triggerDcoReprompt + the trailing triggerDcoBake:
        # dcoLoopLast_ = cleaned; dcoLoopRecent_.add(cleaned) capped at 3.
        recent.append(cleaned)
        while len(recent) > 3:
            recent.pop(0)
        prev = cleaned
        reading, flags_line, technique, flags = reading_i, flags_line_i, technique_i, flags_i

    return records


def build_summary(all_records, iters, report):
    report(f"\n{'=' * 78}")
    report("SUMMARY")
    report("=" * 78)
    report(f"{'stance':<14} {'rewrites':>10} {'non-fallback':>13} "
           f"{'avg flags':>10} {'avg no-map':>11}")
    for stance_key in ALL_STANCES:
        recs = all_records.get(stance_key, [])
        n = len(recs)
        non_fb = sum(1 for r in recs if not r["fallback"] and r["technique"] not in ("", "?"))
        avg_flags = statistics.mean(len(r["flags"]) for r in recs) if recs else float("nan")
        avg_nomap = statistics.mean(len(r["no_mapping_words"]) for r in recs) if recs else float("nan")
        report(f"{stance_key:<14} {n:>7}/{iters * 3:<2} {non_fb:>13} "
               f"{avg_flags:>10.2f} {avg_nomap:>11.2f}")

    report("\ntranscribe fixpoint check (per seed):")
    report('  Fixpoint = iteration i where either the prompt stopped changing '
           '(cleaned == prompt fed in) or the technique stopped changing vs. the '
           'previous iteration.')
    tr_recs = all_records.get("transcribe", [])
    by_seed: dict[str, list[dict]] = {}
    for r in tr_recs:
        by_seed.setdefault(r["seed"], []).append(r)
    for seed in SEEDS:
        recs = sorted(by_seed.get(seed, []), key=lambda r: r["iter"])
        if not recs:
            report(f"  {seed!r}: (no data -- chain did not complete)")
            continue
        found = None
        prev_technique = None
        for r in recs:
            prompt_fixed = (r["cleaned"] == r["prompt_in"])
            technique_fixed = (prev_technique is not None and r["technique"] == prev_technique)
            if prompt_fixed or technique_fixed:
                reason = []
                if prompt_fixed:
                    reason.append("prompt unchanged")
                if technique_fixed:
                    reason.append(f"technique stable ({r['technique']})")
                found = (r["iter"], " & ".join(reason))
                break
            prev_technique = r["technique"]
        if found:
            report(f"  {seed!r}: fixpoint at iter {found[0]} ({found[1]})")
        else:
            techs = [r["technique"] for r in recs]
            report(f"  {seed!r}: no fixpoint within {len(recs)} iterations "
                   f"(techniques: {' -> '.join(techs)})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true",
                    help="fast smoke run: 2 stances (transcribe, abduction) x 2 seeds x "
                         "2 iters, instead of the full 6 x 3 x 3")
    args = ap.parse_args()

    stances = ["transcribe", "abduction"] if args.quick else ALL_STANCES
    seeds = SEEDS[:2] if args.quick else SEEDS
    iters = 2 if args.quick else 3

    lines: list[str] = []

    def report(s=""):
        lines.append(s)

    report("DCO Re-Prompt loop -- empirical stance calibration")
    report(f"mode: {'QUICK' if args.quick else 'FULL'}  "
           f"stances={stances}  seeds={len(seeds)}  iters={iters}")
    for m in _SPEC_MISMATCHES:
        report(f"NOTE: {m}")

    if not BACKEND_SCRIPT.is_file():
        report(f"\nFATAL: backend not found at {BACKEND_SCRIPT}")
        print("\n".join(lines))
        sys.exit(2)

    command = [sys.executable, str(BACKEND_SCRIPT)]
    print(f"Spawning backend: {' '.join(command)}", file=sys.stderr)
    client = PipeClient(command)
    failures: list[str] = []
    try:
        info = client.info
        device = info.get("default") or ""
        report(f"\nBackend ready. devices={info.get('devices')} default={info.get('default')!r} "
               f"models={info.get('models')}")
        print(f"Backend ready. devices={info.get('devices')} default={device!r}", file=sys.stderr)

        # ── preflight: is the translation/instruct model even installed? ──
        # dco mode degrades gracefully without it (S2 just gets skipped, every
        # residue word flagged); interpret mode HARD-REQUIRES it. Check
        # explicitly and report plainly rather than burning the whole matrix
        # on a doomed run.
        print("Preflight: checking the instruct/translation model is installed...", file=sys.stderr)
        try:
            _ = interpret(client, SYSTEM_PROMPTS["transcribe"], "Machine reading: technique: saw",
                          8, device)
        except PipeProtocolError as e:
            report(f"\nFATAL: backend is missing a required model for interpret(): {e}")
            report("Cannot run the DCO Re-Prompt loop without the instruct/translation model "
                   "(expected under <model root>/translation/). Install it and re-run.")
            print("\n".join(lines))
            sys.exit(3)
        print("Preflight OK: instruct model responds.", file=sys.stderr)

        all_records: dict[str, list[dict]] = {s: [] for s in stances}
        for stance_key in stances:
            for seed in seeds:
                print(f"-- running {stance_key} / {truncate(seed, 40)} --", file=sys.stderr)
                recs = run_chain(client, stance_key, seed, iters, device, failures, report)
                for r in recs:
                    r["seed"] = seed
                all_records[stance_key].extend(recs)

        build_summary(all_records, iters, report)

        report(f"\n{'=' * 78}")
        if failures:
            report(f"MECHANICS: {len(failures)} FAILURE(S):")
            for f in failures:
                report(f"  - {f}")
        else:
            report("MECHANICS: ALL PASS")
        report("=" * 78)

        print("\n".join(lines))
        sys.exit(1 if failures else 0)
    finally:
        client.close()


if __name__ == "__main__":
    main()
