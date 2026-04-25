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
from PyInstaller.utils.hooks import collect_data_files, collect_submodules, copy_metadata

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
hidden += collect_submodules('transformers.generation')
hidden += ['transformers.generation.utils']
hidden += ['transformers.models.auto.modeling_auto']
hidden += ['transformers.models.auto.tokenization_auto']
hidden += ['transformers.utils.quantization_config']

# stable_audio_tools: keep the runtime inference/model-loading modules only.
# The package also contains training/UI/data code that drags large optional
# stacks into the frozen backend if we collect everything.
hidden += [
    'stable_audio_tools.inference.generation',
    'stable_audio_tools.models.factory',
    'stable_audio_tools.models.utils',
]
hidden += collect_submodules('dac')

# safetensors: used by diffusers/transformers for .safetensors loading
hidden += ['safetensors', 'safetensors.torch']

# requests: needed by transformers for model metadata/downloads
hidden += ['requests']

# accelerate: keep the runtime modules that are loaded by the stable-audio
# inference path, but avoid sweeping in test helpers and unrelated scripts.
hidden += [
    'accelerate',
    'accelerate.accelerator',
    'accelerate.big_modeling',
    'accelerate.checkpointing',
    'accelerate.commands',
    'accelerate.commands.config',
    'accelerate.commands.config.cluster',
    'accelerate.commands.config.config',
    'accelerate.commands.config.config_args',
    'accelerate.commands.config.config_utils',
    'accelerate.commands.config.default',
    'accelerate.commands.config.sagemaker',
    'accelerate.commands.config.update',
    'accelerate.commands.menu',
    'accelerate.commands.menu.cursor',
    'accelerate.commands.menu.helpers',
    'accelerate.commands.menu.input',
    'accelerate.commands.menu.keymap',
    'accelerate.commands.menu.selection_menu',
    'accelerate.data_loader',
    'accelerate.hooks',
    'accelerate.inference',
    'accelerate.launchers',
    'accelerate.logging',
    'accelerate.optimizer',
    'accelerate.parallelism_config',
    'accelerate.scheduler',
    'accelerate.state',
    'accelerate.tracking',
    'accelerate.utils',
    'accelerate.utils.ao',
    'accelerate.utils.bnb',
    'accelerate.utils.constants',
    'accelerate.utils.dataclasses',
    'accelerate.utils.environment',
    'accelerate.utils.fsdp_utils',
    'accelerate.utils.imports',
    'accelerate.utils.launch',
    'accelerate.utils.megatron_lm',
    'accelerate.utils.memory',
    'accelerate.utils.modeling',
    'accelerate.utils.offload',
    'accelerate.utils.operations',
    'accelerate.utils.other',
    'accelerate.utils.random',
    'accelerate.utils.torch_xla',
    'accelerate.utils.tqdm',
    'accelerate.utils.transformer_engine',
    'accelerate.utils.versions',
]

# torch backends
hidden += ['torch.backends.mps', 'torch.backends.cuda', 'torch.backends.cudnn']

# ── Data files ──────────────────────────────────────────────────────
# Some packages bundle config/JSON files that must be included.

datas = []
datas += collect_data_files('diffusers', includes=['**/*.json'])
datas += collect_data_files('transformers', includes=['**/*.json'])
datas += collect_data_files('stable_audio_tools', includes=['**/*.json', '**/*.yaml'])

# ── Package metadata ──────────────────────────────────────────────
# transformers checks dependency versions via importlib.metadata at import time
datas += copy_metadata('requests')
datas += copy_metadata('transformers')
datas += copy_metadata('torch')
datas += copy_metadata('safetensors')
datas += copy_metadata('huggingface_hub')
datas += copy_metadata('filelock')
datas += copy_metadata('pyyaml')
datas += copy_metadata('regex')
datas += copy_metadata('packaging')
datas += copy_metadata('tqdm')
datas += copy_metadata('numpy')
datas += copy_metadata('torchsde')
datas += copy_metadata('accelerate')
datas += copy_metadata('diffusers')
datas += copy_metadata('descript-audio-codec')
datas += copy_metadata('descript-audiotools')

# ── Analysis ────────────────────────────────────────────────────────

a = Analysis(
    ['pipe_inference.py'],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=hidden,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=['runtime_hook.py'],
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
        'triton',
        'tensorflow',
        'skimage',
        'numba',
        'llvmlite',
        # Torch subsystems not used by inference, but expensive to bundle
        'torch.utils.benchmark',
        'torch.utils.tensorboard',
    ],
    noarchive=False,
)

# ── Keep CUDA runtime binaries conservative for packaged builds ─────────
# Torch can import a broader CUDA userspace set eagerly on some builds. Keep
# those binaries intact for installability; only drop the Triton package/data
# because this backend does not use torch.compile.
import re as _re

a.datas    = [d for d in a.datas    if 'triton' not in d[0]]

# Torch wheels also contain build/test/tooling assets that are not needed for
# inference bundles and inflate the packaged backend substantially.
_torch_data_exclude = _re.compile(
    r'(^|.*/)torch/(testing|onnx|include|share)(/|$)'
    r'|(^|.*/)torch/bin/(ptxas|protoc)(-|$)'
)

a.datas = [d for d in a.datas if not _torch_data_exclude.search(d[0])]
a.binaries = [b for b in a.binaries if not _torch_data_exclude.search(b[0])]

# PyInstaller's torch hook can re-add NVIDIA helper packages explicitly. Keep
# CUDA runtime pieces such as CUPTI/NVTX because some torch builds import them
# eagerly, but drop the extra profiler helper binaries that are not required for
# normal inference startup.
_nvidia_runtime_exclude = _re.compile(
    r'libnvperf_'
    r'|libcheckpoint\.so'
    r'|libpcsamplingutil\.so'
)

a.datas = [d for d in a.datas if not _nvidia_runtime_exclude.search(d[0])]
a.binaries = [b for b in a.binaries if not _nvidia_runtime_exclude.search(b[0])]

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
