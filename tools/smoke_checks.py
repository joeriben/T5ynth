#!/usr/bin/env python3
"""Fast source-level smoke checks for T5ynth integration paths.

These checks intentionally avoid launching the app or loading models. They make
sure recent live-performance and startup invariants still exist in the C++
sources after refactors.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def contains_all(text: str, needles: list[str], label: str) -> None:
    missing = [needle for needle in needles if needle not in text]
    require(not missing, f"{label}: missing {', '.join(missing)}")


def check_fixed_midi_controllers() -> None:
    processor = read("src/PluginProcessor.cpp")
    voice_manager_h = read("src/dsp/VoiceManager.h")
    voice_manager_cpp = read("src/dsp/VoiceManager.cpp")

    contains_all(
        processor,
        [
            "msg.isPitchWheel()",
            "setPitchBendSemitones",
            "cc == 1",
            "setModWheel",
            "cc == 2",
            "setBreathController",
            "cc == 7",
            "setChannelVolume",
            "cc == 11",
            "setExpression",
            "cc == 64",
            "setSustainPedal",
            "cc == 66",
            "setSostenutoPedal",
            "cc == 67",
            "setSoftPedal",
            "cc == 120 || cc == 123",
            "cc == 121",
            "resetPerformanceControllers",
        ],
        "fixed MIDI controller handling",
    )

    contains_all(
        voice_manager_h + voice_manager_cpp,
        [
            "sostenutoVoice",
            "sostenutoReleasedVoice",
            "releaseSostenutoVoices",
            "softPedalDown",
            "performancePitchRatio",
            "performanceOutputGain",
            "modWheelPressure",
            "breathPressure",
            "expressionGain",
            "channelVolumeGain",
        ],
        "VoiceManager performance controller state",
    )


def check_audio_ldm2_linear_lock() -> None:
    prompt = read("src/gui/PromptPanel.cpp")
    contains_all(
        prompt,
        [
            "isAudioLDM2Model",
            'injectionMode_ = "linear"',
            "setEnabled(nonLinearEnabled)",
            "requestInjectionMode = isAudioLDM2Model(req.model)",
            "req.injectionMode = requestInjectionMode",
        ],
        "AudioLDM2 linear-mode lock",
    )


def check_step_preview_requires_shift() -> None:
    sequencer = read("src/gui/SequencerPanel.cpp")
    pattern = re.compile(
        r"e\.mods\.isShiftDown\(\).*?beginStepHoldPreview",
        re.DOTALL,
    )
    require(pattern.search(sequencer) is not None, "step preview must require Shift")


def check_computer_keyboard_note_off() -> None:
    main = read("src/gui/MainPanel.cpp")
    contains_all(
        main,
        [
            "computerKeyboardActiveNotes",
            "releaseComputerKeyboardNotes",
            "endComputerKeyboardNote(note >= 0 ? note : computerKeyboardNoteForIndex(i))",
            "lower == 'y' || lower == 'x'",
            "shiftComputerKeyboardOctave",
        ],
        "computer keyboard note-off and octave controls",
    )


def check_init_startup_state() -> None:
    main = read("src/gui/MainPanel.cpp")
    default_body = re.search(
        r"void MainPanel::loadDefaultPreset\(\)\s*\{(?P<body>.*?)\n\}",
        main,
        re.DOTALL,
    )
    require(default_body is not None, "loadDefaultPreset body not found")
    require(
        "loadInitPreset();" in default_body.group("body"),
        "loadDefaultPreset must fall back to loadInitPreset for fresh installs",
    )
    require(
        "loadBundledPreset(" not in default_body.group("body"),
        "loadDefaultPreset must not load a factory demo preset",
    )


def check_bundled_preset_collection() -> None:
    preset_files = sorted((ROOT / "resources/presets").glob("*.t5p"))
    require(len(preset_files) >= 30, "bundled preset collection should include the expanded preset set")

    cmake = read("CMakeLists.txt")
    contains_all(
        cmake,
        [
            "T5YNTH_FACTORY_PRESETS",
            "CONFIGURE_DEPENDS",
            "${T5YNTH_FACTORY_PRESETS}",
        ],
        "CMake bundled preset glob",
    )

    main = read("src/gui/MainPanel.cpp")
    contains_all(
        main,
        [
            "BinaryData::namedResourceListSize",
            "BinaryData::originalFilenames",
            "BinaryData::getNamedResource(resourceName, size)",
            'endsWithIgnoreCase(".t5p")',
        ],
        "BinaryData preset mirroring",
    )


def check_guide_mentions_controls() -> None:
    guide = read("resources/T5ynth_Guide.html")
    contains_all(
        guide,
        [
            "MIDI Pitch Bend",
            "CC1 / CC2",
            "CC7 / CC11",
            "CC66 / CC67",
            "AudioLDM2 is selected",
            "Shift</strong> while clicking or dragging",
            "Import Presets",
            "Get from GitHub",
        ],
        "guide coverage",
    )


def main() -> int:
    checks = [
        check_fixed_midi_controllers,
        check_audio_ldm2_linear_lock,
        check_step_preview_requires_shift,
        check_computer_keyboard_note_off,
        check_init_startup_state,
        check_bundled_preset_collection,
        check_guide_mentions_controls,
    ]

    for check in checks:
        check()
        print(f"ok: {check.__name__}")

    print(f"{len(checks)} smoke checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
