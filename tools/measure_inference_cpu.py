#!/usr/bin/env python3
"""Empirical cooldown + minimum-interval for throttling ASAP regen.

Goal: produce the hard numbers needed to rate-limit auto-regen so continuous
("ASAP") inference doesn't sit at peak load. Measured over the REAL stdin/stdout
IPC backend (backend/pipe_inference.py), per SA3 inference:

  duration       backend-reported compute time (response elapsed_ms)
  active_cpu     CPU% of the pipe_inference process WHILE one inference runs,
                 measured from a COLD start (a real idle gap precedes it — the
                 methodology bug that contaminated the earlier version was
                 measuring back-to-back windows and calling them "single")
  cooldown       wall time AFTER the response until the process CPU falls back
                 to idle (async/teardown tail). 0 ⇒ it blocks on the next read
                 immediately; >0 ⇒ a real settling tail the next inference would
                 collide with under ASAP.
  contention     does duration GROW when fired back-to-back vs spaced? (probes
                 whether inferences actually overlap — constant duration ⇒ they
                 don't, the load is duty cycle; growing duration ⇒ real contention)

Derived:
  min_interval_settled = duration + cooldown  (next inference only after the
                          previous fully settled — zero tail overlap)
  duty table          = for a target AVERAGE load ceiling, the inter-request
                          interval keeping avg under it: interval = active·dur/ceil

CPU = pipe_inference process, psutil cpu_times deltas (100% = one core, matches
Activity Monitor's per-process column). Per-process %GPU can't be sampled without
sudo/powermetrics; the GPU rows use the ACTIVE %GPU you read in Activity Monitor
(--gpu-active, default 75 from the ASAP screenshots).

  .venv/bin/python tools/measure_inference_cpu.py [model] [--gpu-active 75]
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient  # noqa: E402

try:
    import psutil
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "psutil"])
    import psutil

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"

DURATION_S = 3.0
STEPS = 8
CFG_SCALE = 4.0
PROMPT = "warm analog bass, sustained"
N = 5             # repetitions per measurement
COLD_GAP = 4.0    # idle gap before each cold-start active measurement
IDLE_PCT = 15.0   # CPU% below which the process counts as idle
WIN = 0.10        # cooldown sampling window
NEED = 3          # consecutive idle windows to declare "settled"
COOLDOWN_CAP = 6.0


def cpu_over(proc, fn):
    t0 = proc.cpu_times(); w0 = time.perf_counter()
    out = fn()
    w1 = time.perf_counter(); t1 = proc.cpu_times()
    cpu_s = (t1.user - t0.user) + (t1.system - t0.system)
    return out, 100.0 * cpu_s / max(w1 - w0, 1e-6)


def cooldown_after(proc):
    """Sample CPU in WIN windows; return seconds from now until NEED consecutive
    windows read < IDLE_PCT (capped)."""
    t_resp = time.perf_counter()
    last = proc.cpu_times(); lastw = time.perf_counter()
    below = 0
    while time.perf_counter() - t_resp < COOLDOWN_CAP:
        time.sleep(WIN)
        cur = proc.cpu_times(); now = time.perf_counter()
        cpu = 100.0 * ((cur.user - last.user) + (cur.system - last.system)) / (now - lastw)
        last, lastw = cur, now
        if cpu < IDLE_PCT:
            below += 1
            if below >= NEED:
                return max((now - WIN * NEED) - t_resp, 0.0)
        else:
            below = 0
    return COOLDOWN_CAP


def main():
    args = [a for a in sys.argv[1:]]
    gpu_active = 75.0
    if "--gpu-active" in args:
        i = args.index("--gpu-active"); gpu_active = float(args[i + 1]); del args[i:i + 2]
    pref = args[0] if args else None

    client = PipeClient([sys.executable, str(BACKEND)])
    try:
        info = client.info
        models = info.get("models", [])
        sa3 = [m for m in models if "stable-audio-3" in m]
        model = (pref if pref in models else (sa3[0] if sa3 else (models[0] if models else None)))
        if not model:
            print("no models", file=sys.stderr); sys.exit(1)
        proc = psutil.Process(client.process.pid)
        base = {"model": model, "duration": DURATION_S, "steps": STEPS, "cfg_scale": CFG_SCALE}
        print(f"pid={proc.pid} device={info.get('default')} model={model} "
              f"({DURATION_S}s/{STEPS} steps)\n", file=sys.stderr)

        def gen(seed):
            return client.request({**base, "prompt_a": PROMPT, "seed": seed})

        gen(1)  # warm-up (model load), discarded

        # (1) COLD-START active CPU + duration: real idle gap before each.
        act, durs = [], []
        for i in range(N):
            time.sleep(COLD_GAP)                       # let it go fully idle first
            res, cpu = cpu_over(proc, lambda: gen(1000 + i))
            act.append(cpu); durs.append(res["elapsed_ms"] / 1000.0)
            print(f"cold #{i}: active_cpu={cpu:5.1f}%  duration={durs[-1]:.2f}s", file=sys.stderr)
        active_cpu = sum(act) / len(act)
        duration = sum(durs) / len(durs)

        # (2) COOLDOWN: tail after the response returns.
        cools = []
        for i in range(N):
            time.sleep(COLD_GAP)
            gen(2000 + i)
            c = cooldown_after(proc)
            cools.append(c)
            print(f"cooldown #{i}: {c:.2f}s", file=sys.stderr)
        cooldown = sum(cools) / len(cools)

        # (3) CONTENTION: duration spaced vs back-to-back (does it overlap?).
        time.sleep(COLD_GAP)
        spaced_dur = []
        for i in range(N):
            time.sleep(COLD_GAP); spaced_dur.append(gen(3000 + i)["elapsed_ms"] / 1000.0)
        b2b_dur = [gen(4000 + i)["elapsed_ms"] / 1000.0 for i in range(N)]  # no gaps
        sd = sum(spaced_dur) / len(spaced_dur); bd = sum(b2b_dur) / len(b2b_dur)

        settled = duration + cooldown
        print("\n──────────────────── results ────────────────────")
        print(f"duration / inference        : {duration:.2f}s")
        print(f"active CPU (cold start)      : {active_cpu:.1f}%  (1 core = 100%)")
        print(f"cooldown (CPU→idle tail)     : {cooldown:.2f}s")
        print(f"duration spaced vs back2back : {sd:.2f}s vs {bd:.2f}s  "
              f"(ratio {bd / sd:.2f} → {'overlap/contention' if bd > sd * 1.15 else 'NO contention; constant'})")
        print(f"\nmin interval (fully settled) : {settled:.2f}s  (duration + cooldown)")
        print(f"\nduty-based min interval for an AVERAGE-load ceiling")
        print(f"  (interval = active · duration / ceiling):")
        print(f"  {'ceiling':>8} | {'CPU-based':>10} | {'GPU-based':>10}   (GPU active={gpu_active:.0f}%)")
        for ceil in (20, 30, 40, 50):
            ci = active_cpu * duration / ceil
            gi = gpu_active * duration / ceil
            print(f"  {ceil:>6}%  | {ci:>9.2f}s | {gi:>9.2f}s")
        print(f"\nNOTE: numbers scale with your real duration/steps. GPU rows assume")
        print(f"the ~{gpu_active:.0f}% active GPU from your ASAP screenshots; per-process %GPU")
        print(f"can't be sampled here without sudo powermetrics.")
    finally:
        client.close()


if __name__ == "__main__":
    main()
