cmake_minimum_required(VERSION 3.22)

# Version is derived from T5YNTH_VERSION env (CI passes github.ref_name); dev
# builds fall back to `git describe` so the title bar shows a real version
# instead of 0.0.0.
if(DEFINED ENV{T5YNTH_VERSION})
    set(_T5YNTH_VERSION "$ENV{T5YNTH_VERSION}")
else()
    execute_process(
        COMMAND git describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _T5YNTH_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _T5YNTH_VERSION)
        set(_T5YNTH_VERSION "0.0.0")
    endif()
endif()
# Keep the FULL tag (e.g. "v2.5.3-beta.1") for the update check — every T5ynth
# release is a GitHub pre-release, so the checker must compare full semver
# (incl. the -beta.N precedence), not just the X.Y.Z triplet JUCE requires.
set(_T5YNTH_FULL_VERSION "${_T5YNTH_VERSION}")
string(REGEX REPLACE "^v" "" _T5YNTH_VERSION "${_T5YNTH_VERSION}")
string(REGEX REPLACE "-.*$" "" _T5YNTH_VERSION "${_T5YNTH_VERSION}")
if(NOT _T5YNTH_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    set(_T5YNTH_VERSION "0.0.0")
    set(_T5YNTH_FULL_VERSION "0.0.0")
endif()

project(T5ynth VERSION ${_T5YNTH_VERSION})

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
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "T5ynth captures live audio input to seed Resynth (init_audio)."
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
    src/UpdateChecker.cpp

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
    src/dsp/FreezeTextureEngine.cpp
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
    src/inference/RepromptStances.cpp

    # Event Log (.t5evt session log)
    src/eventlog/EventLog.cpp
    src/eventlog/EventLogWriterThread.cpp
    src/eventlog/EventLogReader.cpp

    # GUI
    src/gui/MainPanel.cpp
    src/gui/PhysicalKeyState.cpp
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
    src/presets/PresetUpdater.cpp
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
    resources/T5ynth_Guide.html
    # SA3 metadata that the ungated Comfy-Org weight repos omit (~60 KB of JSON,
    # no model weights). Written into the model dir at install time by the
    # reassembly finalize step so SA3 can install login-free. model_config.json
    # is the Stability SA3 config for the SMALL checkpoints (music+sfx — modality
    # comes from the dir name, not the config); the four sa3_t5gemma_* files are
    # the Gemma tokenizer/encoder configs (shared by ALL SA3 tiers). The 34 MB
    # tokenizer.json is NOT bundled — the SentencePiece model is extracted from
    # the downloaded t5gemma weights.
    resources/sa3_configs/sa3_model_config.json
    # SA3 Medium DiT config (depth-24 differential-attn DiT + SAME-L autoencoder
    # with sliding_window attention). DISTINCT from the small config above; the
    # medium reassembly writes THIS one (finalizeSa3Reassembly branches on id).
    resources/sa3_configs/sa3_medium_model_config.json
    resources/sa3_configs/sa3_t5gemma_config.json
    resources/sa3_configs/sa3_t5gemma_generation_config.json
    resources/sa3_configs/sa3_t5gemma_tokenizer_config.json
    resources/sa3_configs/sa3_t5gemma_special_tokens_map.json
    # Stable Audio Open 1.0 metadata omitted by the ungated Comfy-Org repackaged
    # repo (which ships only the checkpoint). model_config.json is the canonical
    # Stability SAO 1.0 config (verified byte-identical across three independent
    # ungated mirrors); written into the model dir at install by the SAO reassembly
    # finalize step so SAO 1.0 can install login-free. No separate text encoder is
    # bundled here — SAO 1.0 conditions on t5-base, already available ungated.
    resources/sao_configs/sao_open_1_0_model_config.json
    # Verbatim third-party license texts written into each SA3 install dir. The
    # Stability AI Community License IV(a)(i) requires distributing a copy of the
    # Agreement; the Gemma Terms + Prohibited Use Policy are the encoder's terms.
    resources/sa3_configs/licenses/STABILITY_AI_COMMUNITY_LICENSE.txt
    resources/sa3_configs/licenses/GEMMA_TERMS_OF_USE.txt
    resources/sa3_configs/licenses/GEMMA_PROHIBITED_USE_POLICY.txt
)

target_link_libraries(T5ynth
    PRIVATE
        T5ynthData
        juce::juce_audio_utils
        juce::juce_cryptography
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
        T5YNTH_FULL_VERSION="${_T5YNTH_FULL_VERSION}"
)
