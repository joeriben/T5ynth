#!/usr/bin/env python3
"""Phase-3 Csound lexicon validation suite (SPEC_phase3_csound_lexicon.md).

Runnable plain:      python -m backend.test_csound_lexicon
Also pytest-collectible (every test_* function below is self-contained), but
this suite is DEV-MACHINE-ONLY (it shells out to tools/csound_orch_check,
which needs a local Csound install) and must never be wired into CI —
csound_orch_check.py's caller loop below prints a LOUD message and exits
cleanly (never fails) if the binary hasn't been built yet.

Covers (spec's exact checklist):
  1. EVERY tool/character/motion key: a minimal one-layer spec ->
     assemble -> `csound_orch_check check` -> must pass.
  2. A ~25-composition sweep (BJ's dark-bell-16'+metallic-bell-8' stack, two
     4-layer stacks, every character on two families, every motion, solo
     custom/sparse families, register-stacked combos) -> check + bench each,
     render every sweep WAV into tools/csound_lexicon_out/ for the ear gate.
  3. Determinism: assemble() twice on the same spec -> byte-identical text.
  4. The exact spec-JSON schema example parses and passes check.
  5. Validation strictness: an unknown key raises ValueError, never a silent
     default.
"""
import re
import subprocess
import sys
from pathlib import Path

_BACKEND_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _BACKEND_DIR.parent
if str(_BACKEND_DIR) not in sys.path:
    sys.path.insert(0, str(_BACKEND_DIR))

import csound_assembler as asm  # noqa: E402
import csound_lexicon as lex  # noqa: E402

CHECK_TOOL = _REPO_ROOT / "tools" / "csound_orch_check"
OUT_DIR = _REPO_ROOT / "tools" / "csound_lexicon_out"

_SR_MARKER = "%SR%"


def _tool_missing_message():
    return (
        "\n"
        "=" * 78 + "\n"
        "LOUD NOTICE: tools/csound_orch_check binary not found at "
        f"{CHECK_TOOL}\n"
        "This suite is DEV-MACHINE-ONLY (needs a local Csound install) and is\n"
        "deliberately NOT wired into CI. Build it first:\n"
        "  CSOUND_PREFIX=$(brew --prefix csound)\n"
        "  clang++ -std=c++17 -O2 -I\"$CSOUND_PREFIX/include\" \\\n"
        "    tools/csound_orch_check.cpp \\\n"
        "    -F\"$CSOUND_PREFIX/Frameworks\" -framework CsoundLib64 \\\n"
        "    -Wl,-rpath,\"$CSOUND_PREFIX/Frameworks\" \\\n"
        "    -o tools/csound_orch_check\n"
        "Skipping the whole suite (exit 0 — this is a skip, not a failure).\n"
        "=" * 78 + "\n"
    )


def _write_orc(name, orchestra_text):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"{name}.orc"
    path.write_text(orchestra_text, encoding="utf-8")
    return path


_PEAK_RE = re.compile(r"peak=([0-9.]+)")
_BENCH_RE = re.compile(r"BENCH (PASS|FAIL).*median=([0-9.]+)us p99=([0-9.]+)us max=([0-9.]+)us")


def _run_check(name, orchestra_text):
    path = _write_orc(name, orchestra_text)
    r = subprocess.run([str(CHECK_TOOL), "check", str(path)], capture_output=True, text=True)
    ok = r.returncode == 0
    peak = None
    m = _PEAK_RE.search(r.stdout)
    if m:
        peak = float(m.group(1))
    return ok, peak, (r.stdout + r.stderr)


def _run_bench(name, orchestra_text):
    path = _write_orc(name, orchestra_text)
    r = subprocess.run([str(CHECK_TOOL), "bench", str(path)], capture_output=True, text=True)
    ok = r.returncode == 0
    m = _BENCH_RE.search(r.stdout)
    stats = None
    if m:
        stats = {"pass": m.group(1) == "PASS", "median_us": float(m.group(2)),
                 "p99_us": float(m.group(3)), "max_us": float(m.group(4))}
    return ok, stats, (r.stdout + r.stderr)


def _run_render(name, orchestra_text, freq=220.0):
    path = _write_orc(name, orchestra_text)
    wav_path = OUT_DIR / f"{name}.wav"
    r = subprocess.run([str(CHECK_TOOL), "render", str(path), str(wav_path), str(freq)],
                        capture_output=True, text=True)
    ok = r.returncode == 0
    return ok, wav_path, (r.stdout + r.stderr)


# ─── per-key minimal specs (stage 1: every key gets its own check) ─────────

def _minimal_family_spec(key):
    return {"layers": [{"tool": key}]}


def _minimal_character_spec(key):
    return {"layers": [{"tool": "bell", "characters": [{"key": key, "amount": 0.8}]}]}


def _minimal_motion_spec(key):
    return {"layers": [{"tool": "pad", "motion": [{"key": key, "amount": 0.7}]}]}


# ─── the ~25-composition sweep (stage 2: check + bench + render) ──────────
# Names double as the .orc/.wav basenames landing in tools/csound_lexicon_out/.

def _build_sweep():
    sweep = {}

    # 1. BJ's own words, literally: "make one dark bell 16' and above that a
    #    metallic bell 8'" -- SAME tool key ("bell") used twice, proving the
    #    assembler's per-layer namespacing (kL1_.../kL2_...) never collides.
    sweep["bj_dark_bell16_metallic_bell8"] = {
        "layers": [
            {"tool": "bell", "register": "16'", "level": 0.8,
             "characters": [{"key": "dark", "amount": 0.7}],
             "motion": [{"key": "vibrato", "amount": 0.3}]},
            {"tool": "bell", "register": "8'", "level": 0.6,
             "characters": [{"key": "metallic", "amount": 0.6}]},
        ],
        "motion": [{"key": "evolve", "amount": 0.5}],
        "space": None,
    }

    # 2-3. 4-layer stacks (the CPU/level-budget worst cases).
    sweep["stack4_max_level_decay"] = {
        "layers": [
            {"tool": "bell", "level": 1.0}, {"tool": "metal", "level": 1.0},
            {"tool": "cymbal", "level": 1.0}, {"tool": "glass", "level": 1.0},
        ],
    }
    sweep["stack4_continuous_registers"] = {
        "layers": [
            {"tool": "pad", "level": 0.9, "register": "8'"},
            {"tool": "organ_tone", "level": 0.7, "register": "4'"},
            {"tool": "sub", "level": 0.9, "register": "16'"},
            {"tool": "square_stack", "level": 0.6, "register": "8'"},
        ],
    }

    # 4-15. every character on two families (bell, saw_stack).
    for fam in ("bell", "saw_stack"):
        for ch in lex.CHARACTER_KEYS:
            sweep[f"char_{ch}_{fam}"] = {
                "layers": [{"tool": fam, "characters": [{"key": ch, "amount": 0.8}]}]
            }

    # 16-19. every motion, on pad — plus the motionless plain pad, which
    # tools/csound_lexicon_fidelity.py uses as the motion baseline.
    for mo in lex.MOTION_KEYS:
        sweep[f"motion_{mo}_pad"] = {
            "layers": [{"tool": "pad", "motion": [{"key": mo, "amount": 0.7}]}]
        }
    sweep["solo_pad"] = {
        "layers": [{"tool": "pad", "register": "8'", "level": 0.8,
                    "characters": [], "motion": []}]
    }

    # 20-23. solo showcase for the sparser/custom families.
    sweep["solo_fm_bell"] = {"layers": [{"tool": "fm_bell", "level": 0.9}]}
    sweep["solo_pluck"] = {"layers": [{"tool": "pluck", "level": 0.9}]}
    sweep["solo_glass"] = {"layers": [{"tool": "glass", "level": 0.9}]}
    sweep["solo_noise_wash"] = {"layers": [{"tool": "noise_wash", "level": 0.8}]}

    # 24-25. register-stacked combos beyond BJ's own example.
    sweep["combo_cymbal_metal"] = {
        "layers": [
            {"tool": "cymbal", "register": "8'", "level": 0.7},
            {"tool": "metal", "register": "4'", "level": 0.7},
        ],
    }
    sweep["combo_organ_registers"] = {
        "layers": [
            {"tool": "organ_tone", "register": "16'", "level": 0.7},
            {"tool": "organ_tone", "register": "8'", "level": 0.6},
            {"tool": "organ_tone", "register": "4'", "level": 0.5},
        ],
    }
    return sweep


SWEEP = _build_sweep()

# The exact spec-JSON schema example (SPEC_phase3_csound_lexicon.md, verbatim).
EXACT_SPEC_JSON_EXAMPLE = {
    "layers": [
        {"tool": "bell", "register": "16'", "level": 0.8,
         "characters": [{"key": "dark", "amount": 0.7}],
         "motion": [{"key": "vibrato", "amount": 0.3}]}
    ],
    "motion": [{"key": "evolve", "amount": 0.5}],
    "space": None,
}


# ─── test functions ─────────────────────────────────────────────────────────

def test_exact_spec_json_example_parses():
    text, reading = asm.assemble(EXACT_SPEC_JSON_EXAMPLE)
    assert "%SR%" in text
    assert reading.strip()


def test_determinism_assemble_twice_byte_identical():
    text1, reading1 = asm.assemble(SWEEP["bj_dark_bell16_metallic_bell8"])
    text2, reading2 = asm.assemble(SWEEP["bj_dark_bell16_metallic_bell8"])
    assert text1 == text2, "assemble() is not byte-identical across two calls"
    assert reading1 == reading2


def test_unknown_key_raises_never_silent_default():
    try:
        asm.assemble({"layers": [{"tool": "not_a_real_family_key"}]})
        raise AssertionError("assemble() did not raise on an unknown tool key")
    except ValueError:
        pass
    try:
        asm.assemble({"layers": [{"tool": "bell", "characters": [{"key": "not_a_real_character"}]}]})
        raise AssertionError("assemble() did not raise on an unknown character key")
    except ValueError:
        pass
    try:
        asm.assemble({"layers": [{"tool": "bell", "envelope": "strike"}]})
        raise AssertionError("assemble() did not raise on a rejected 'envelope' field "
                              "(envelopes were removed entirely -- amplitude shape is "
                              "the synth's own ADSR, never the oscillator's)")
    except ValueError:
        pass
    try:
        asm.assemble({"layers": [{"tool": "bell", "motion": [{"key": "not_a_real_motion"}]}]})
        raise AssertionError("assemble() did not raise on an unknown motion key")
    except ValueError:
        pass
    try:
        asm.assemble({"layers": [{"tool": "bell", "register": "not_a_register"}]})
        raise AssertionError("assemble() did not raise on an unknown register")
    except ValueError:
        pass


def test_max_layers_enforced():
    try:
        asm.assemble({"layers": [{"tool": "bell"}] * 5})
        raise AssertionError("assemble() accepted 5 layers (cap is 4)")
    except ValueError:
        pass


# ─── the runner (this is what actually shells out to the check tool) ──────

def run_all():
    if not CHECK_TOOL.exists():
        print(_tool_missing_message())
        return 0  # skip, not fail

    results = {"per_key_pass": 0, "per_key_fail": 0, "per_key_fail_names": [],
               "sweep_check_pass": 0, "sweep_check_fail": 0,
               "sweep_bench_pass": 0, "sweep_bench_fail": 0,
               "sweep_render_pass": 0, "sweep_render_fail": 0,
               "bench_table": [], "wav_list": [], "fail_names": []}

    # Plain assertions first (no subprocess needed).
    plain_tests = [
        test_exact_spec_json_example_parses,
        test_determinism_assemble_twice_byte_identical,
        test_unknown_key_raises_never_silent_default,
        test_max_layers_enforced,
    ]
    for t in plain_tests:
        try:
            t()
            print(f"PASS  {t.__name__}")
        except Exception as e:
            print(f"FAIL  {t.__name__}: {e}")
            results["fail_names"].append(t.__name__)

    # Stage 1: every key, individually.
    print("\n--- stage 1: every tool/character/motion key ---")
    per_key_specs = []
    for k in lex.FAMILY_KEYS:
        per_key_specs.append((f"key_family_{k}", _minimal_family_spec(k)))
    for k in lex.CHARACTER_KEYS:
        per_key_specs.append((f"key_character_{k}", _minimal_character_spec(k)))
    for k in lex.MOTION_KEYS:
        per_key_specs.append((f"key_motion_{k}", _minimal_motion_spec(k)))

    for name, spec in per_key_specs:
        text, _ = asm.assemble(spec)
        ok, peak, out = _run_check(name, text)
        if ok:
            results["per_key_pass"] += 1
            print(f"PASS  {name}  peak={peak}")
        else:
            results["per_key_fail"] += 1
            results["per_key_fail_names"].append(name)
            print(f"FAIL  {name}\n{out}")

    # Stage 2: the ~25-composition sweep -- check + bench + render.
    print(f"\n--- stage 2: {len(SWEEP)}-composition sweep (check + bench + render) ---")
    for name, spec in sorted(SWEEP.items()):
        text, reading = asm.assemble(spec)

        ok, peak, out = _run_check(name, text)
        if ok:
            results["sweep_check_pass"] += 1
        else:
            results["sweep_check_fail"] += 1
            results["fail_names"].append(f"check:{name}")
            print(f"FAIL check {name}\n{out}")

        bok, stats, bout = _run_bench(name, text)
        if bok and stats and stats["pass"]:
            results["sweep_bench_pass"] += 1
        else:
            results["sweep_bench_fail"] += 1
            results["fail_names"].append(f"bench:{name}")
            print(f"FAIL bench {name}\n{bout}")
        if stats:
            results["bench_table"].append((name, stats["median_us"], stats["p99_us"],
                                            stats["max_us"], stats["pass"]))

        rok, wav_path, rout = _run_render(name, text)
        if rok:
            results["sweep_render_pass"] += 1
            results["wav_list"].append(str(wav_path))
        else:
            results["sweep_render_fail"] += 1
            results["fail_names"].append(f"render:{name}")
            print(f"FAIL render {name}\n{rout}")

        print(f"{'PASS' if ok and bok and rok else 'FAIL'}  {name}  "
              f"peak={peak}  bench_median_us={stats['median_us'] if stats else '?'}  "
              f"reading={reading!r}")

    # ─── summary ───
    print("\n" + "=" * 78)
    print("SUMMARY")
    print(f"  plain tests:        {len(plain_tests) - len([f for f in results['fail_names'] if f in [t.__name__ for t in plain_tests]])}/{len(plain_tests)} passed")
    print(f"  per-key checks:     {results['per_key_pass']}/{results['per_key_pass']+results['per_key_fail']} passed"
          f"  (keys: {len(lex.FAMILY_KEYS)} family + {len(lex.CHARACTER_KEYS)} character + "
          f"{len(lex.MOTION_KEYS)} motion = "
          f"{len(lex.FAMILY_KEYS)+len(lex.CHARACTER_KEYS)+len(lex.MOTION_KEYS)})")
    print(f"  sweep compositions: {len(SWEEP)}")
    print(f"    check:  {results['sweep_check_pass']}/{len(SWEEP)} passed")
    print(f"    bench:  {results['sweep_bench_pass']}/{len(SWEEP)} passed (gate: 133us median)")
    print(f"    render: {results['sweep_render_pass']}/{len(SWEEP)} passed -> {OUT_DIR}")

    print("\nBENCH TABLE (name, median_us, p99_us, max_us, pass)")
    for row in sorted(results["bench_table"], key=lambda r: -r[1]):
        print(f"  {row[0]:38s} median={row[1]:8.3f}us p99={row[2]:8.3f}us max={row[3]:8.3f}us {'PASS' if row[4] else 'FAIL'}")

    total_fail = (results["per_key_fail"] + results["sweep_check_fail"]
                  + results["sweep_bench_fail"] + results["sweep_render_fail"]
                  + len(results["fail_names"]) - len(results["bench_table"]) * 0)
    n_hard_fails = len(results["fail_names"])
    print(f"\n{'ALL GREEN' if n_hard_fails == 0 else f'{n_hard_fails} FAILURES'}")
    if n_hard_fails:
        print("Failures:", results["fail_names"])
    print("=" * 78)
    return 0 if n_hard_fails == 0 else 1


if __name__ == "__main__":
    sys.exit(run_all())
