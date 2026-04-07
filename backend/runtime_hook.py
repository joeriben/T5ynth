"""PyInstaller runtime hook — pre-import packages that diffusers checks via find_spec.

diffusers uses importlib.util.find_spec() to detect optional dependencies.
In a PYZ archive, find_spec() can return None even though the module is bundled.
Pre-importing patches sys.modules so subsequent find_spec() calls succeed.
"""
import importlib

for _pkg in ('torchsde', 'accelerate', 'safetensors'):
    try:
        importlib.import_module(_pkg)
    except Exception:
        pass
