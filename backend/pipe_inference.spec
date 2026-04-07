# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for T5ynth inference backend.

Bundles pipe_inference.py + torch + diffusers + torchsde + stable_audio_tools
into a single-folder executable that the JUCE plugin launches as a subprocess.

Build:
    cd backend
    pyinstaller pipe_inference.spec

Output:
    dist/pipe_inference/          — folder with executable + libs
    dist/pipe_inference/pipe_inference   — the binary to launch
"""

import sys
from pathlib import Path
from PyInstaller.utils.hooks import collect_data_files, collect_submodules

# ── Hidden imports ──────────────────────────────────────────────────
# PyInstaller's static analysis misses lazy imports and plugin-style loaders.

hidden = []

# torchsde: monkey-patched at startup, imported lazily
hidden += collect_submodules('torchsde')

# diffusers: pipeline classes loaded by name, scheduler registered dynamically
hidden += collect_submodules('diffusers.pipelines.stable_audio')
hidden += collect_submodules('diffusers.schedulers')
hidden += ['diffusers.utils.outputs']

# transformers: T5 encoder loaded by diffusers pipeline
hidden += collect_submodules('transformers.models.t5')
hidden += ['transformers.models.auto.modeling_auto']
hidden += ['transformers.utils.quantization_config']

# stable_audio_tools: native pipeline (SA Small), imported lazily
hidden += collect_submodules('stable_audio_tools')

# safetensors: used by diffusers/transformers for .safetensors loading
hidden += ['safetensors', 'safetensors.torch']

# accelerate: used by diffusers for device placement
hidden += collect_submodules('accelerate')

# torch backends
hidden += ['torch.backends.mps', 'torch.backends.cuda', 'torch.backends.cudnn']

# ── Data files ──────────────────────────────────────────────────────
# Some packages bundle config/JSON files that must be included.

datas = []
datas += collect_data_files('diffusers', includes=['**/*.json'])
datas += collect_data_files('transformers', includes=['**/*.json'])
datas += collect_data_files('stable_audio_tools', includes=['**/*.json', '**/*.yaml'])

# ── Analysis ────────────────────────────────────────────────────────

a = Analysis(
    ['pipe_inference.py'],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=hidden,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        # Not needed at runtime
        'flask',
        'matplotlib',
        'tkinter',
        # 'unittest',  # required by torch.utils._config_module since torch 2.11
        'pytest',
        'IPython',
        'notebook',
        'jupyter',
    ],
    noarchive=False,
)

# ── Strip CUDA libraries not needed for inference (saves ~800 MB) ────
# Keep: cublas, cublasLt, cudnn, curand, cudart (required for GPU inference)
# Drop: training-only, multi-GPU, JIT, and profiling libs
import re as _re

_cuda_exclude = _re.compile(
    r'libnccl|nccl\d'           # multi-GPU communication
    r'|libcufft|cufft\d'        # FFT (only in training losses)
    r'|libcusparse|cusparse\d'  # sparse matrices
    r'|libcusolver|cusolver\d'  # dense/sparse solvers
    r'|libnvrtc|nvrtc\d'        # runtime compiler
    r'|libnvJitLink|nvJitLink'  # JIT linker
    r'|libcupti|cupti\d'        # profiling tools
    r'|triton'                  # Triton JIT (no torch.compile used)
)

a.binaries = [b for b in a.binaries if not _cuda_exclude.search(b[0])]
a.datas    = [d for d in a.datas    if 'triton' not in d[0]]

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='pipe_inference',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,    # UPX breaks torch .dylibs on macOS
    console=True, # subprocess, no GUI
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name='pipe_inference',
)
