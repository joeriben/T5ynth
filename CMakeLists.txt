cmake_minimum_required(VERSION 3.22)
project(T5ynth VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

# ── LibTorch ──────────────────────────────────────────────
# Requires a pre-built LibTorch distribution (NOT pip torch).
# Download from https://pytorch.org/get-started/locally/ → LibTorch.
#
# Configure with:
#   -DCMAKE_PREFIX_PATH=/path/to/libtorch
#
# Platform notes:
#   Linux CUDA:  libtorch-cxx11-abi-shared-with-deps (cu124 or later)
#   Linux CPU:   libtorch-cxx11-abi-shared-with-deps (cpu)
#   macOS (MPS): libtorch-macos-arm64 (MPS support automatic on Apple Silicon)
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

# ── SentencePiece (for T5 tokenizer) ─────────────────────
include(FetchContent)
FetchContent_Declare(
    sentencepiece
    GIT_REPOSITORY https://github.com/google/sentencepiece.git
    GIT_TAG        v0.2.1
)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sentencepiece)

# ── nlohmann/json (header-only, for config parsing) ──────
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

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
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(T5ynth)

target_sources(T5ynth PRIVATE
    # Core
    src/PluginProcessor.cpp
    src/PluginEditor.cpp

    # DSP
    src/dsp/WavetableOscillator.cpp
    src/dsp/WavetableBank.cpp
    src/dsp/ADSREnvelope.cpp
    src/dsp/LFO.cpp
    src/dsp/DriftLFO.cpp
    src/dsp/StateVariableFilter.cpp
    src/dsp/DelayLine.cpp
    src/dsp/ConvolutionReverb.cpp
    src/dsp/Limiter.cpp
    src/dsp/AudioLooper.cpp
    src/dsp/SynthVoice.cpp
    src/dsp/VoiceManager.cpp
    src/dsp/ModulationMatrix.cpp

    # Sequencer
    src/sequencer/StepSequencer.cpp
    src/sequencer/Arpeggiator.cpp

    # Backend (legacy HTTP — will be removed)
    src/backend/BackendManager.cpp
    src/backend/BackendConnection.cpp
    src/backend/SemanticAxes.cpp
    src/backend/GenerationRequest.cpp
    src/backend/ModelDownloader.cpp

    # Inference (native LibTorch pipeline)
    src/inference/T5ynthInference.cpp
    src/inference/DiffusionScheduler.cpp

    # GUI
    src/gui/MainPanel.cpp
    src/gui/PromptPanel.cpp
    src/gui/SynthPanel.cpp
    src/gui/FxPanel.cpp
    src/gui/SequencerPanel.cpp
    src/gui/AxesPanel.cpp
    src/gui/DimensionExplorer.cpp
    src/gui/WaveformDisplay.cpp
    src/gui/PresetPanel.cpp
    src/gui/SetupWizard.cpp
    src/gui/StatusBar.cpp
    src/gui/T5ynthLookAndFeel.cpp

    # Presets
    src/presets/PresetFormat.cpp
)

target_include_directories(T5ynth PRIVATE
    src
    ${sentencepiece_SOURCE_DIR}/src
)

juce_add_binary_data(T5ynthData SOURCES
    resources/ir/emt_140_plate_bright.wav
    resources/ir/emt_140_plate_medium.wav
    resources/ir/emt_140_plate_dark.wav
)

target_link_libraries(T5ynth
    PRIVATE
        T5ynthData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        CURL::libcurl
        ${TORCH_LIBRARIES}
        sentencepiece-static
        nlohmann_json::nlohmann_json
    PUBLIC
        juce::juce_recommended_config_flags
        # juce::juce_recommended_lto_flags  # disabled — too CPU-intensive for dev builds
        juce::juce_recommended_warning_flags
)

# LibTorch requires position-independent code
set_property(TARGET T5ynth PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(T5ynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        T5YNTH_BACKEND_DIR="${CMAKE_SOURCE_DIR}/backend"
)
