cmake_minimum_required(VERSION 3.22)
project(T5ynth VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcurl: required on Linux (JUCE URL backend), optional elsewhere
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
else()
    find_package(CURL QUIET)
endif()

# ── nlohmann/json (header-only, for config parsing) ──────
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# ── Signalsmith Stretch (header-only, for pitch-preserving transposition) ──────
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.1.0
)
FetchContent_MakeAvailable(signalsmith_stretch)

add_subdirectory(JUCE)

juce_add_plugin(T5ynth
    COMPANY_NAME "AI4ArtsEd"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE A4Ae
    PLUGIN_CODE T5yn
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "T5ynth"
    BUNDLE_ID "org.ai4artsed.t5ynth"
    DESCRIPTION "Text-to-Sound Synthesizer — T5 Embedding Space Navigation"
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    COPY_PLUGIN_AFTER_BUILD FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "T5ynth uses Bluetooth to connect to MIDI controllers."
)

juce_generate_juce_header(T5ynth)

target_sources(T5ynth PRIVATE
    # Core
    src/PluginProcessor.cpp
    src/PluginEditor.cpp

    # DSP
    src/dsp/WavetableOscillator.cpp
    src/dsp/WavetableBank.cpp
    src/dsp/NoiseGenerator.cpp
    src/dsp/ADSREnvelope.cpp
    src/dsp/LFO.cpp
    src/dsp/DriftLFO.cpp
    src/dsp/StateVariableFilter.cpp
    src/dsp/DelayLine.cpp
    src/dsp/ConvolutionReverb.cpp
    src/dsp/AlgorithmicReverb.cpp
    src/dsp/Limiter.cpp
    src/dsp/SamplePlayer.cpp
    src/dsp/SynthVoice.cpp
    src/dsp/VoiceManager.cpp
    src/dsp/ModulationMatrix.cpp

    # Sequencer
    src/sequencer/StepSequencer.cpp
    src/sequencer/Arpeggiator.cpp
    src/sequencer/GenerativeSequencer.cpp
    src/sequencer/EuclideanRhythm.h
    src/sequencer/ScaleQuantizer.h
    src/sequencer/GenerativeSequencer.h

    # Inference (Python pipe IPC)
    src/inference/PipeInference.cpp

    # GUI
    src/gui/MainPanel.cpp
    src/gui/PromptPanel.cpp
    src/gui/SynthPanel.cpp
    src/gui/FxPanel.cpp
    src/gui/SequencerPanel.cpp
    src/gui/AxesPanel.cpp
    src/gui/DimensionExplorer.cpp
    src/gui/WaveformDisplay.cpp
    src/gui/SetupWizard.cpp
    src/gui/StatusBar.cpp
    src/gui/T5ynthLookAndFeel.cpp

    # Presets
    src/presets/PresetFormat.cpp
)

target_include_directories(T5ynth PRIVATE
    src
)
target_include_directories(T5ynth SYSTEM PRIVATE
    ${signalsmith_stretch_SOURCE_DIR}
)

juce_add_binary_data(T5ynthData SOURCES
    resources/ir/emt_140_plate_bright.wav
    resources/ir/emt_140_plate_medium.wav
    resources/ir/emt_140_plate_dark.wav
    "resources/presets/DEMO T5-Oscillator-Drift.t5p"
    resources/T5ynth_Guide.html
)

target_link_libraries(T5ynth
    PRIVATE
        T5ynthData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        $<$<BOOL:${CURL_FOUND}>:CURL::libcurl>
        nlohmann_json::nlohmann_json
        # JUCE 8's juce_gui_extra module header does not declare a
        # linuxPackages: line, so its Linux WebBrowserComponent deps
        # (webkit2gtk + gtk3) are not auto-linked. JUCEUtils.cmake creates
        # a JUCE_BROWSER_LINUX_DEPS pkgconfig target but leaves linkage
        # to the user. Without this explicit link, Linux builds fail with
        # 'gtk/gtk.h: No such file or directory' in juce_gui_extra.cpp.
        $<$<BOOL:${UNIX}>:$<$<NOT:$<BOOL:${APPLE}>>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>>
    PUBLIC
        juce::juce_recommended_config_flags
        # juce::juce_recommended_lto_flags  # disabled — too CPU-intensive for dev builds
        juce::juce_recommended_warning_flags
)

target_compile_definitions(T5ynth
    PUBLIC
        NOMINMAX              # prevent windows.h min/max macros
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=$<BOOL:${CURL_FOUND}>
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        T5YNTH_BACKEND_DIR="${CMAKE_SOURCE_DIR}/backend"
)
