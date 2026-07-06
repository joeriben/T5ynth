#!/usr/bin/env python3
"""Platform-cleanliness probe for the init_audio / Resynth path.

PURPOSE (the methodological one): before any *aesthetic* claim about how the
model "hears" an external audio seed can be trusted, the PLATFORM must be shown
to behave deterministically and A/B-symmetrically. Otherwise an observed
contingency ("the pad persists, the samba doesn't come through") is not
distinguishable from a code artifact (RNG nondeterminism, backend state leak
between requests, an A-vs-B asymmetry in the embedding blend).

This runs single-shot over the REAL stdin/stdout IPC the plugin uses
(backend/pipe_inference.py) — no plugin, no auto-regen loop, no live audio. SA3
only (init_audio / generate_diffusion_cond_inpaint is SA3-gated). Three phases:

  PHASE 1 — DETERMINISM & LEAK-FREEDOM (the foundation)
    * repeatability: identical request x3 -> max sample-diff + waveform corr.
    * order-independence: render X, then a DIFFERENT cell Y, then X again ->
      are the X's identical? If X drifts after Y, the backend carries state
      between requests (the "old samples hang across generations" smell).

  PHASE 2 — A/B SYMMETRY (the "audio input only reacts to B, not A" report)
    * per (seed-WAV, init_noise): render the SAME target prompt routed via A
      (alpha=-1, decoy in B) vs via B (alpha=+1, decoy in A). A symmetric
      platform makes both sound equally like the target. Asymmetry => the bug
      is in the backend A/B blend, not the plugin loop.

  PHASE 3 — init_noise CHARACTERISATION (model dynamics; only meaningful if
    Phase 1 is green). The via-A leg's CLAP breakthrough (target vs decoy) and
    carry (timbre corr to the input WAV) across the init_noise sweep IS the
    characterisation curve: where does the prompt overtake the external seed?
    (Computed for free from the Phase-2 matrix.)

PARTITION: Phase 1 green + Phase 2 symmetric => any erratic-ness seen in the
plugin is PLUGIN-LAYER code (auto-regen / carry / edge-detector / drift
trackers), not the backend/model. Phase 1 red => the backend itself is the
problem, fix that first.

Reuses the exact IPC + metric machinery of the existing tools (IPC subprocess
path, never a direct import of backend internals).

Run:
    .venv/bin/python tools/test_resynth_platform.py [model]
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
import torchaudio

# Reuse the plugin's exact wire path + metrics.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav, corr_to  # noqa: E402
from test_resynth_loop import timbre_corr, spec_centroid, rms                  # noqa: E402
from clap_probe import CLAP_SR, embed_texts, embed_audios, assert_model_sane   # noqa: E402
from transformers import ClapModel, ClapProcessor                             # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
OUT_DIR = Path(__file__).resolve().parent / "resynth_platform_out"
SAMPLES = Path("/Users/joerissen/Music/Samples")

# ── Config ───────────────────────────────────────────────────────────
MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-3-small-music"
SEED = 12345                 # fixed (matches randomSeedToggle=false)
STEPS = 8                    # APVTS / SA3 model-card default
CFG = 1.0
DURATION_S = 3.0

TARGET_PROMPT = "a samba group playing on the streets"
DECOY_PROMPT = "a solo cello melody"

# alpha convention (backend linear blend): -1.0 = pure A, +1.0 = pure B.
ALPHA_A = -1.0
ALPHA_B = +1.0

# Three contrasting seed materials: tonal / percussive / texture.
MATERIAL = [
    ("vibraphone", SAMPLES / "Yamaha D-85 Vibraphone_.wav"),
    ("drum_tom",   SAMPLES / "Lxnn60" / "Tom Lxnn60 Clean 01.wav"),
    ("noise_sweep", SAMPLES / "SH1x1" / "Noise Sweep SH1x1.wav"),
]

# init_noise sweep: the plugin's reachable band is [0.05, 0.50] (0.50-0.45*amt);
# 0.90 added as the prompt-dominated high-regen reference.
SIGMAS = [0.05, 0.20, 0.35, 0.50, 0.90]
DET_SIGMA = 0.25             # mid-band: most sensitive to nondeterminism
DEVICE = "cpu"               # CLAP on CPU (matches the backend analyze op)


# ── Helpers ──────────────────────────────────────────────────────────

def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def load_init_wav(path: Path, gen_duration_s: float):
    """Load a WAV as [channels, frames] float32 at its native SR. Short one-shots
    are TILED up to >= the generation duration so the seed carries energy across
    the whole clip (a one-shot padded with silence is a degenerate seed); the
    backend's PadCrop(randomize=False) then deterministically takes the head."""
    wav, sr = sf.read(str(path), dtype="float32", always_2d=True)  # (frames, ch)
    audio = np.ascontiguousarray(wav.T, dtype=np.float32)          # (ch, frames)
    orig_frames = audio.shape[1]
    need = int(gen_duration_s * sr) + 1
    if orig_frames < need:
        reps = int(np.ceil(need / max(1, orig_frames)))
        audio = np.tile(audio, (1, reps))[:, :need]
    return np.ascontiguousarray(audio, dtype=np.float32), sr, orig_frames / sr


def clap_audio_emb(model, processor, audio_cs: np.ndarray, sr: int):
    """Normalized CLAP audio embedding [D] of an [channels, frames] clip."""
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, CLAP_SR).numpy()
    return embed_audios(model, processor, [mono], DEVICE)[0]


def max_abs_diff(a: np.ndarray, b: np.ndarray) -> float:
    """Max absolute sample difference over the common channels/length."""
    c = min(a.shape[0], b.shape[0])
    n = min(a.shape[1], b.shape[1])
    return float(np.max(np.abs(a[:c, :n] - b[:c, :n]))) if (c and n) else float("nan")


def gen(client, model, prompt_a, prompt_b, alpha, init_b64, init_sr, init_ch, sigma):
    req = {
        "model": model, "duration": DURATION_S, "steps": STEPS, "cfg_scale": CFG,
        "prompt_a": prompt_a, "prompt_b": prompt_b, "alpha": alpha, "seed": SEED,
        "init_audio_b64": init_b64, "init_audio_sr": init_sr,
        "init_audio_channels": init_ch, "init_noise_level": sigma,
    }
    res = client.request(req)
    return res["audio"], res["sample_rate"], res["elapsed_ms"]


# ── Main ─────────────────────────────────────────────────────────────

def main() -> int:
    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    missing = [str(p) for _, p in MATERIAL if not p.is_file()]
    if missing:
        log("ERROR: missing seed material:\n  " + "\n  ".join(missing))
        return 1
    OUT_DIR.mkdir(exist_ok=True)

    log("Loading CLAP (laion/clap-htsat-unfused, cpu) ...")
    clap = ClapModel.from_pretrained("laion/clap-htsat-unfused").to(DEVICE).eval()
    clap_proc = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")
    assert_model_sane(clap, clap_proc, DEVICE)
    txt = embed_texts(clap, clap_proc, [TARGET_PROMPT, DECOY_PROMPT], DEVICE)
    tgt_emb, decoy_emb = txt[0], txt[1]

    def breakthrough(audio_cs, sr):
        e = clap_audio_emb(clap, clap_proc, audio_cs, sr)
        ct, cd = float(e @ tgt_emb), float(e @ decoy_emb)
        return ct, cd, ct - cd

    log(f"Spawning backend: {sys.executable} {BACKEND_SCRIPT}")
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    manifest = {"model": None, "seed": SEED, "steps": STEPS, "cfg": CFG,
                "duration_s": DURATION_S, "target": TARGET_PROMPT,
                "decoy": DECOY_PROMPT, "phase1": {}, "matrix": []}
    try:
        models = client.info.get("models", [])
        model = MODEL if MODEL in models else None
        if model is None:
            log(f"ERROR: model '{MODEL}' not installed. Resynth/init_audio is "
                f"SA3-gated. Available: {models}")
            return 1
        manifest["model"] = model
        log(f"Model: {model}")

        # Pre-load + encode every seed (tiled to full duration).
        seeds = {}
        for name, path in MATERIAL:
            audio, sr, orig_dur = load_init_wav(path, DURATION_S)
            seeds[name] = {
                "b64": encode_init_audio(audio), "sr": sr, "ch": audio.shape[0],
                "orig_dur": orig_dur,
            }
            write_wav(OUT_DIR / f"seed_{name}.wav", audio, sr)
            log(f"  seed '{name}': {audio.shape} @ {sr}Hz (orig {orig_dur:.2f}s)")

        # ── PHASE 1 — determinism & leak-freedom ──────────────────────
        log("\n=== PHASE 1: determinism & leak-freedom ===")
        s = seeds[MATERIAL[0][0]]            # anchor cell = material 0, via A
        sY = seeds[MATERIAL[1][0]]           # a DIFFERENT cell to interleave

        def anchor():
            a, sr, _ = gen(client, model, TARGET_PROMPT, DECOY_PROMPT, ALPHA_A,
                           s["b64"], s["sr"], s["ch"], DET_SIGMA)
            return a, sr

        x1, sr = anchor()
        x2, _ = anchor()
        x3, _ = anchor()
        # interleave a different request, then repeat the anchor
        _y, _, _ = gen(client, model, DECOY_PROMPT, TARGET_PROMPT, ALPHA_B,
                       sY["b64"], sY["sr"], sY["ch"], DET_SIGMA)
        x4, _ = anchor()

        d12 = max_abs_diff(x1, x2)
        d13 = max_abs_diff(x1, x3)
        d14 = max_abs_diff(x1, x4)        # after the interleaved Y
        c12 = corr_to(x1, x2)
        c14 = corr_to(x1, x4)
        repeat_ok = (d12 < 1e-3 and d13 < 1e-3 and (np.isnan(c12) or c12 > 0.999))
        leak_ok = (d14 < 1e-3 and (np.isnan(c14) or c14 > 0.999))
        for nm, a in [("x1", x1), ("x4", x4)]:
            write_wav(OUT_DIR / f"phase1_{nm}.wav", a, sr)
        log(f"  repeatability  max|x1-x2|={d12:.2e}  max|x1-x3|={d13:.2e}  "
            f"corr(x1,x2)={c12:.5f}  -> {'DETERMINISTIC' if repeat_ok else 'NONDETERMINISTIC'}")
        log(f"  no-leak (X,Y,X) max|x1-x4|={d14:.2e}  corr(x1,x4)={c14:.5f}  "
            f"-> {'CLEAN' if leak_ok else 'STATE LEAK BETWEEN REQUESTS'}")
        manifest["phase1"] = {
            "det_sigma": DET_SIGMA,
            "max_abs_diff_x1x2": d12, "max_abs_diff_x1x3": d13,
            "max_abs_diff_x1x4_after_interleave": d14,
            "corr_x1x2": None if np.isnan(c12) else round(c12, 6),
            "corr_x1x4": None if np.isnan(c14) else round(c14, 6),
            "repeatable": bool(repeat_ok), "leak_free": bool(leak_ok),
        }

        # ── PHASE 2 + 3 — A/B symmetry + init_noise characterisation ──
        log("\n=== PHASE 2/3: A/B symmetry + init_noise characterisation ===")
        log(f"  metric: CLAP breakthrough = cos(out,'{TARGET_PROMPT}') "
            f"- cos(out,'{DECOY_PROMPT}');  carry = timbre_corr(out, seed)")
        asym_all = []
        for name, _ in MATERIAL:
            sd = seeds[name]
            log(f"\n  -- seed '{name}' --")
            log(f"  {'sigma':>6} | {'bt_viaA':>8} {'bt_viaB':>8} {'|asym|':>7} | "
                f"{'carryA':>7} {'carryB':>7} | {'cenA':>6} {'cenB':>6}")
            for sigma in SIGMAS:
                aA, srA, _ = gen(client, model, TARGET_PROMPT, DECOY_PROMPT, ALPHA_A,
                                 sd["b64"], sd["sr"], sd["ch"], sigma)
                aB, srB, _ = gen(client, model, DECOY_PROMPT, TARGET_PROMPT, ALPHA_B,
                                 sd["b64"], sd["sr"], sd["ch"], sigma)
                ctA, cdA, btA = breakthrough(aA, srA)
                ctB, cdB, btB = breakthrough(aB, srB)
                # carry: spectral-timbre correlation of output to the (tiled) seed.
                seed_audio, _ = sf.read(str(OUT_DIR / f"seed_{name}.wav"),
                                        dtype="float32", always_2d=True)
                seed_cs = seed_audio.T
                carryA = timbre_corr(seed_cs, aA, srA)
                carryB = timbre_corr(seed_cs, aB, srB)
                asym = abs(btA - btB)
                asym_all.append(asym)
                tag = f"{sigma:.2f}".replace(".", "")
                write_wav(OUT_DIR / f"{name}_viaA_n{tag}.wav", aA, srA)
                write_wav(OUT_DIR / f"{name}_viaB_n{tag}.wav", aB, srB)
                log(f"  {sigma:>6.2f} | {btA:>+8.3f} {btB:>+8.3f} {asym:>7.3f} | "
                    f"{carryA:>7.3f} {carryB:>7.3f} | "
                    f"{spec_centroid(aA, srA):>6.0f} {spec_centroid(aB, srB):>6.0f}")
                manifest["matrix"].append({
                    "seed": name, "sigma": sigma,
                    "viaA": {"cos_target": round(ctA, 4), "cos_decoy": round(cdA, 4),
                             "breakthrough": round(btA, 4), "carry": round(carryA, 4)},
                    "viaB": {"cos_target": round(ctB, 4), "cos_decoy": round(cdB, 4),
                             "breakthrough": round(btB, 4), "carry": round(carryB, 4)},
                    "abs_asymmetry": round(asym, 4),
                })

        # ── PHASE 4 — lone-prompt α traversal (echo-through-null) ─────
        # Verifies the mechanism behind the plugin's "audio reacts only to B"
        # report. With ONE prompt field filled the backend mirrors it through
        # null (2·null − prompt, _echo_through_null), so the linear blend at α
        # traces prompt → null → antipode. At the centred α a lone prompt sits
        # at the null point (a buzz on SA3) and the init_audio seed dominates —
        # exactly what a lone prompt rides in the drift/auto-regen path
        # (PromptPanel.cpp:2610 passes a concrete α, skipping the lone-prompt
        # pin at :2194 which needs isnan(alphaOverride)). Single-shot here
        # through the SAME backend. Prediction: loneA_bt high only near α=-1,
        # collapsing through ~0 at α=0 toward negative at α=+1; loneB mirror.
        log("\n=== PHASE 4: lone-prompt α traversal (echo-through-null) ===")
        log("  seed 'vibraphone' (pad-analog) @ init_noise 0.35; target in ONE slot, other EMPTY")
        log(f"  {'alpha':>6} | {'loneA_bt':>9} {'loneB_bt':>9} | {'loneA_carry':>11} {'loneB_carry':>11}")
        sd = seeds["vibraphone"]
        seed_vib, _ = sf.read(str(OUT_DIR / "seed_vibraphone.wav"), dtype="float32", always_2d=True)
        seed_vib_cs = seed_vib.T
        phase4 = []
        for a in [-1.0, -0.5, 0.0, 0.5, 1.0]:
            aA, srA, _ = gen(client, model, TARGET_PROMPT, "", a,
                             sd["b64"], sd["sr"], sd["ch"], 0.35)   # lone-A, B empty
            aB, srB, _ = gen(client, model, "", TARGET_PROMPT, a,
                             sd["b64"], sd["sr"], sd["ch"], 0.35)   # lone-B, A empty
            _, _, btA = breakthrough(aA, srA)
            _, _, btB = breakthrough(aB, srB)
            cA = timbre_corr(seed_vib_cs, aA, srA)
            cB = timbre_corr(seed_vib_cs, aB, srB)
            tag = f"{a:+.1f}".replace(".", "").replace("+", "p").replace("-", "m")
            write_wav(OUT_DIR / f"lone_A_a{tag}.wav", aA, srA)
            write_wav(OUT_DIR / f"lone_B_a{tag}.wav", aB, srB)
            log(f"  {a:>+6.1f} | {btA:>+9.3f} {btB:>+9.3f} | {cA:>11.3f} {cB:>11.3f}")
            phase4.append({"alpha": a, "loneA_bt": round(btA, 4), "loneB_bt": round(btB, 4),
                           "loneA_carry": round(cA, 4), "loneB_carry": round(cB, 4)})
        manifest["phase4"] = phase4
        # NOTE: Phase 4 characterises the BACKEND's lone-prompt α-traversal. It is
        # NOT the user's bug — that one appears with TWO prompts (Phase 5).

        # ── PHASE 5 — two-prompt edit-sensitivity (the "only B reacts" repro) ──
        # The actual report: with BOTH fields filled, editing A changes nothing,
        # editing B does. Operationalised in the backend at fixed α: swap A's text
        # vs swap B's text and measure how much the output moves (1 − timbre_corr
        # to the unedited baseline). Symmetric (editA_Δ ≈ editB_Δ, esp. at α=0) ⇒
        # the backend is NOT the cause; the asymmetry is plugin-side (effective α,
        # or the Re-Prompt loop which in non-dual "alpha" coupling rewrites ONLY B
        # — PromptPanel.cpp:3187/3258 — leaving A the fixed anchor).
        log("\n=== PHASE 5: two-prompt edit-sensitivity (only-B-reacts repro) ===")
        base_a, base_b, edit = TARGET_PROMPT, DECOY_PROMPT, "a distorted techno kick loop"
        sd = seeds["vibraphone"]
        log(f"  base A='{base_a}'  B='{base_b}';  edit→'{edit}'  (Δ = 1−timbre_corr to baseline)")
        log(f"  {'alpha':>6} | {'editA_Δ':>8} {'editB_Δ':>8}")
        phase5 = []
        for a in [-0.5, 0.0, 0.5]:
            o0, sr0, _ = gen(client, model, base_a, base_b, a, sd["b64"], sd["sr"], sd["ch"], 0.35)
            oA, _,   _ = gen(client, model, edit,   base_b, a, sd["b64"], sd["sr"], sd["ch"], 0.35)
            oB, _,   _ = gen(client, model, base_a, edit,   a, sd["b64"], sd["sr"], sd["ch"], 0.35)
            dA = 1.0 - timbre_corr(o0, oA, sr0)
            dB = 1.0 - timbre_corr(o0, oB, sr0)
            log(f"  {a:>+6.1f} | {dA:>8.4f} {dB:>8.4f}")
            phase5.append({"alpha": a, "editA_delta": round(dA, 4), "editB_delta": round(dB, 4)})
        manifest["phase5"] = phase5

        # ── Verdicts ──────────────────────────────────────────────────
        mean_asym = float(np.mean(asym_all)) if asym_all else float("nan")
        max_asym = float(np.max(asym_all)) if asym_all else float("nan")
        # A/B is "symmetric" if routing the target via A vs B lands within a small
        # CLAP-cosine margin. CLAP cross-modal cosines span ~0.1-0.5, so a mean
        # asymmetry under ~0.02 is platform-symmetric; a large/systematic gap is
        # the "only-B" bug surfacing in the backend blend.
        sym_ok = mean_asym < 0.02
        manifest["verdict"] = {
            "phase1_deterministic": manifest["phase1"]["repeatable"],
            "phase1_leak_free": manifest["phase1"]["leak_free"],
            "phase2_mean_abs_asymmetry": round(mean_asym, 4),
            "phase2_max_abs_asymmetry": round(max_asym, 4),
            "phase2_ab_symmetric": bool(sym_ok),
        }

        log("\n=== VERDICT ===")
        log(f"  PHASE 1 determinism : {'PASS' if manifest['phase1']['repeatable'] else 'FAIL'} "
            f"(repeat max-diff {manifest['phase1']['max_abs_diff_x1x2']:.2e})")
        log(f"  PHASE 1 no-leak     : {'PASS' if manifest['phase1']['leak_free'] else 'FAIL'} "
            f"(after-interleave max-diff {manifest['phase1']['max_abs_diff_x1x4_after_interleave']:.2e})")
        log(f"  PHASE 2 A/B symmetry: {'PASS' if sym_ok else 'FAIL'} "
            f"(mean |asym| {mean_asym:.3f}, max {max_asym:.3f})")
        if manifest['phase1']['repeatable'] and manifest['phase1']['leak_free'] and sym_ok:
            log("  => backend/model is deterministic, leak-free and A/B-symmetric.")
            log("     Any erratic behaviour seen in the plugin is PLUGIN-LAYER, not backend.")
        else:
            log("  => platform is NOT clean — fix the failing phase before trusting any")
            log("     aesthetic observation (the contingency is partly code, not the model).")

        with open(OUT_DIR / "manifest.json", "w") as f:
            json.dump(manifest, f, indent=2)
        log(f"\nWAVs + manifest.json in {OUT_DIR}")
        try:
            subprocess.run(["open", str(OUT_DIR)], check=False)
        except Exception:
            pass
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
