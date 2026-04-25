"""PyInstaller runtime hook — fix multiprocessing + pre-import packages.

1. Disable multiprocessing resource_tracker before torch is imported.
   torch initializes multiprocessing, which starts a resource_tracker
   subprocess via sys.executable. In a PyInstaller bundle, sys.executable
   is the frozen binary, so the tracker re-executes the full application
   (import torch → new tracker → import torch → ...) = infinite fork bomb.
   pipe_inference.py uses no shared memory, so the tracker is unnecessary.

2. Pre-import packages that diffusers checks via find_spec().
   In PYZ archives, find_spec() returns None for bundled modules.
   Pre-importing patches sys.modules so find_spec() succeeds.
"""
import multiprocessing
multiprocessing.freeze_support()

import os


def _configure_cpu_budget_env():
    worker_threads = os.environ.get("T5YNTH_INFERENCE_CPU_THREADS", "2").strip()
    interop_threads = os.environ.get("T5YNTH_INFERENCE_INTEROP_THREADS", "1").strip()

    try:
        worker_threads = str(max(1, int(worker_threads)))
    except ValueError:
        worker_threads = "2"

    try:
        interop_threads = str(max(1, int(interop_threads)))
    except ValueError:
        interop_threads = "1"

    def cap_thread_env(key, limit):
        raw_value = os.environ.get(key)
        try:
            current = int(raw_value) if raw_value is not None else None
        except ValueError:
            current = None

        if current is None or current < 1 or current > int(limit):
            os.environ[key] = limit

    for key in (
        "OMP_NUM_THREADS",
        "MKL_NUM_THREADS",
        "OPENBLAS_NUM_THREADS",
        "NUMEXPR_NUM_THREADS",
        "VECLIB_MAXIMUM_THREADS",
        "BLIS_NUM_THREADS",
        "TORCH_NUM_THREADS",
    ):
        cap_thread_env(key, worker_threads)

    cap_thread_env("TORCH_NUM_INTEROP_THREADS", interop_threads)
    os.environ.setdefault("OMP_WAIT_POLICY", "PASSIVE")
    os.environ.setdefault("KMP_BLOCKTIME", "0")
    os.environ.setdefault("MKL_DYNAMIC", "TRUE")


_configure_cpu_budget_env()

# Completely disable the resource_tracker — it re-executes the frozen binary.
# We don't use shared memory objects, so the tracker is unnecessary.
import multiprocessing.resource_tracker as _rt
_rt.ensure_running = lambda: None          # prevent tracker from starting
_rt._resource_tracker.ensure_running = lambda: None
_rt.register = lambda *a, **kw: None       # no-op registration
_rt.unregister = lambda *a, **kw: None     # no-op unregistration

import importlib

for _pkg in ('safetensors',):
    try:
        importlib.import_module(_pkg)
    except Exception:
        pass
