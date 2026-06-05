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


def check_presets_not_bundled() -> None:
    """Factory presets are no longer compiled into the binary.

    Commit 4e970b77 ("build(presets): stop bundling factory presets into the
    binary", shipped in v2.2.0-beta.0) dropped the ~120 MB offline bundle:
    ``resources/presets/*.t5p``, the ``T5YNTH_FACTORY_PRESETS`` glob in
    ``juce_add_binary_data``, and MainPanel's ``ensureBundledPresetsExist()``
    launch-time extraction (which kept resurrecting stale preset versions).
    Distribution moved to the on-demand public mirror
    (joeriben/T5ynth-Presets), fetched via the Preset Manager's "Update
    Library" button. This check guards that inversion so a refactor can't
    silently re-bundle the collection — the predecessor check asserted the
    opposite and has been failing on every clean checkout since the de-bundle.
    """
    # No .t5p bundle source may live in the tree any more.
    preset_files = sorted((ROOT / "resources/presets").glob("*.t5p"))
    require(
        not preset_files,
        f"factory presets must not be bundled; found {len(preset_files)} .t5p under resources/presets",
    )

    # CMake must not compile presets into BinaryData.
    cmake = read("CMakeLists.txt")
    for token in ("T5YNTH_FACTORY_PRESETS", "resources/presets"):
        require(token not in cmake, f"CMakeLists.txt must not bundle presets (found '{token}')")

    # The on-demand replacement path must stay wired: an "Update Library" fetch
    # control in the Preset Manager, plus a first-launch hint when the library
    # is empty so users aren't left staring at an empty browser.
    preset_manager = read("src/gui/PresetManagerPanel.h")
    contains_all(
        preset_manager,
        ['"Update Library"'],
        "Preset Manager Update Library fetch button",
    )

    main = read("src/gui/MainPanel.cpp")
    contains_all(
        main,
        [
            "PresetFormat::getAllPresetFiles().isEmpty()",
            "No Presets Found",
            "Update Library",
            "PresetUpdater",
        ],
        "MainPanel empty-library hint and on-demand updater",
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
            "Update Library",
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
        check_presets_not_bundled,
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
