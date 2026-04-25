cmake_minimum_required(VERSION 3.22)
project(T5ynth VERSION 1.3.0)

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

set(T5YNTH_PLUGIN_FORMATS Standalone VST3)
if(APPLE)
    option(T5YNTH_MACOS_STANDALONE_ONLY
        "Build only the macOS standalone app"
        OFF)

    if(T5YNTH_MACOS_STANDALONE_ONLY)
        set(T5YNTH_PLUGIN_FORMATS Standalone)
    else()
        list(APPEND T5YNTH_PLUGIN_FORMATS AU)
    endif()
endif()

juce_add_plugin(T5ynth
    COMPANY_NAME "UCDCAE AI Lab"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE A4Ae
    PLUGIN_CODE T5yn
    FORMATS ${T5YNTH_PLUGIN_FORMATS}
    PRODUCT_NAME "T5ynth"
    BUNDLE_ID "org.ai4artsed.t5ynth"
    DESCRIPTION "Text-to-Sound Synthesizer — T5 Embedding Space Navigation"
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    COPY_PLUGIN_AFTER_BUILD FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "T5ynth uses Bluetooth to connect to MIDI controllers."
)

# ── macOS: ad-hoc sign VST3 / AU bundles after JUCE's build steps ──
# Two separate JUCE 8.0.6 issues manifest as "plugin does not show up as
# instrument in Bitwig/Ableton":
#
#   1. VST3: JUCE signs the bundle *before* juce_vst3_helper generates
#      Contents/Resources/moduleinfo.json. That invalidates the sealed
#      signature ("a sealed resource is missing or invalid") and DAWs on
#      modern macOS refuse to register the plugin (it appears in scan/debug
#      lists but not in the instrument browser).
#
#   2. AU: JUCE 8.0.6 does not call _juce_adhoc_sign for the AU target at
#      all (only VST3 and LV2 get it). The linker embeds a minimal ad-hoc
#      Mach-O signature that expects a sealed _CodeSignature folder, which
#      never gets created — resulting in "code has no resources but signature
#      indicates they must be present".
#
# Fix for both: re-sign ad-hoc after all JUCE POST_BUILD steps have run.
# POST_BUILD commands run in the order they are added to a target, so this
# runs *after* JUCE's own manifest / sign steps.
if(APPLE AND TARGET T5ynth_VST3)
    add_custom_command(TARGET T5ynth_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "re-signing VST3 bundle after moduleinfo.json generation"
        COMMAND codesign --force --sign - --deep
            "$<TARGET_BUNDLE_DIR:T5ynth_VST3>"
        VERBATIM)
endif()

if(APPLE AND TARGET T5ynth_AU)
    add_custom_command(TARGET T5ynth_AU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "ad-hoc signing AU component bundle"
        COMMAND codesign --force --sign - --deep
            "$<TARGET_BUNDLE_DIR:T5ynth_AU>"
        VERBATIM)
endif()

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
    resources/logos/t5ynth_icon.png
    "resources/presets/DEMO T5-Oscillator-Drift.t5p"
    "resources/presets/Evil Beauty.t5p"
    resources/presets/INIT.t5p
    "resources/presets/Samba Getdown.t5p"
    "resources/presets/Talking about aliens.t5p"
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
