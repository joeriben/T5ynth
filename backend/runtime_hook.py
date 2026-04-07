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

# Completely disable the resource_tracker — it re-executes the frozen binary.
# We don't use shared memory objects, so the tracker is unnecessary.
import multiprocessing.resource_tracker as _rt
_rt.ensure_running = lambda: None          # prevent tracker from starting
_rt._resource_tracker.ensure_running = lambda: None
_rt.register = lambda *a, **kw: None       # no-op registration
_rt.unregister = lambda *a, **kw: None     # no-op unregistration

import importlib

for _pkg in ('torchsde', 'safetensors'):
    try:
        importlib.import_module(_pkg)
    except Exception:
        pass
