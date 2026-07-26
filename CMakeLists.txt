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

# ── Csound engine ─────────────────────────────────────────────────────────
# The LRO IS Csound, so Csound travels with the app: third_party/csound/<platform>
# holds the exact payload a bundle ships — the library, its support libraries and
# the plugin opcode modules, with every load command already pointing inside the
# bundle. A build therefore needs no Homebrew, no dylibbundler and no system
# Csound anywhere, and dev and CI compile against the same bytes.
#
# STILL NEVER REQUIRED (CMakeLists' oldest rule): delete third_party/csound, or
# build on a platform that has no payload yet, and everything builds exactly as
# before — T5YNTH_CSOUND_FOUND goes false, CsoundEngine.cpp is not compiled, and
# the always-defined T5YNTH_HAS_CSOUND lets every call site branch at runtime.
#
# T5YNTH_CSOUND_PREFIX (CMake var or environment) overrides the vendored payload
# with an installation of one's own — on macOS its Frameworks/ dir, as Homebrew
# lays it out. Everything downstream follows from what was found, not from the
# prefix.
set(T5YNTH_CSOUND_FOUND FALSE)
set(T5YNTH_CSOUND_VENDORED "")
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_csound_vendor "${CMAKE_SOURCE_DIR}/third_party/csound/macos-arm64")
elseif(WIN32)
    set(_csound_vendor "${CMAKE_SOURCE_DIR}/third_party/csound/windows-x64")
else()
    set(_csound_vendor "")
endif()

if(NOT T5YNTH_CSOUND_PREFIX AND DEFINED ENV{T5YNTH_CSOUND_PREFIX})
    set(T5YNTH_CSOUND_PREFIX "$ENV{T5YNTH_CSOUND_PREFIX}")
endif()

if(_csound_vendor AND NOT T5YNTH_CSOUND_PREFIX
   AND EXISTS "${_csound_vendor}/include/csound/csound.h")
    if(APPLE AND EXISTS "${_csound_vendor}/lib/CsoundLib64")
        set(T5YNTH_CSOUND_VENDORED "${_csound_vendor}")
        set(T5YNTH_CSOUND_FOUND TRUE)
    elseif(WIN32 AND EXISTS "${_csound_vendor}/lib/csound64.lib")
        set(T5YNTH_CSOUND_VENDORED "${_csound_vendor}")
        set(T5YNTH_CSOUND_FOUND TRUE)
    endif()
endif()

if(APPLE AND NOT T5YNTH_CSOUND_FOUND)
    # No payload (or an explicit prefix): the framework, as Homebrew ships it.
    find_library(T5YNTH_CSOUND_FRAMEWORK NAMES CsoundLib64
                 PATHS "${T5YNTH_CSOUND_PREFIX}/Frameworks"
                       /opt/homebrew/opt/csound/Frameworks /Library/Frameworks
                 NO_DEFAULT_PATH)
    if(T5YNTH_CSOUND_FRAMEWORK)
        set(T5YNTH_CSOUND_FOUND TRUE)
    endif()
endif()

if(WIN32 AND NOT T5YNTH_CSOUND_FOUND AND T5YNTH_CSOUND_PREFIX)
    if(EXISTS "${T5YNTH_CSOUND_PREFIX}/lib/csound64.lib")
        set(T5YNTH_CSOUND_VENDORED "${T5YNTH_CSOUND_PREFIX}")
        set(T5YNTH_CSOUND_FOUND TRUE)
    endif()
endif()

if(T5YNTH_CSOUND_VENDORED)
    message(STATUS "Csound: vendored payload at ${T5YNTH_CSOUND_VENDORED}")
elseif(T5YNTH_CSOUND_FOUND)
    message(STATUS "Csound: system framework at ${T5YNTH_CSOUND_FRAMEWORK}")
else()
    message(STATUS "Csound: none — the LRO will be silent in this build")
endif()

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

# The instrument is called akróasys (ASCII: akroasys). T5ynth was its name up to
# and including 2.5.3 — the last version without the language-resonant
# oscillator — and the identity below is a DELIBERATE clean break, taken while it
# is still cheap (BJ, 2026-07-25: "noch kennt kaum jemand diesen Synth").
#
# What each field does, because three of them look interchangeable and are not:
#   PLUGIN_NAME   what the host DISPLAYS. Carries the accent.
#   PRODUCT_NAME  the artefact FILENAME (akroasys.vst3 / .component / .app), so
#                 ASCII — a non-ASCII bundle name is a portability accident
#                 waiting on the first Windows or Linux path.
#   PLUGIN_CODE   the four-character IDENTITY a saved DAW project matches on.
#                 Changing it away from T5yn is what makes this a break: a
#                 project that had loaded T5ynth no longer finds the plugin.
# PLUGIN_MANUFACTURER_CODE stays A4Ae — it identifies the lab, not the product.
#
# The CMake TARGET is still T5ynth, so `build_clean/T5ynth_artefacts/…` and every
# script pointing at it keep working. Renaming the target, the source files and
# the symbols is a separate mechanical pass; doing it here would mean one commit
# that changes identity AND moves every path.
juce_add_plugin(T5ynth
    COMPANY_NAME "UCDCAE AI Lab"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE A4Ae
    PLUGIN_CODE Akro
    FORMATS ${T5YNTH_PLUGIN_FORMATS}
    PLUGIN_NAME "akróasys"
    PRODUCT_NAME "akroasys"
    BUNDLE_ID "org.ai4artsed.akroasys"
    DESCRIPTION "Text-to-Sound Synthesizer — embedding-space navigation and a language-resonant oscillator"
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/logos/t5ynth_icon.png"
    COPY_PLUGIN_AFTER_BUILD FALSE
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "akróasys uses Bluetooth to connect to MIDI controllers."
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "akróasys captures live audio input to seed Resynth (init_audio)."
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

# Standalone has issue 2 as well, and it is the one a developer hits first:
# the .app that comes out of a local build fails `codesign --verify` with the
# same "code has no resources but signature indicates they must be present",
# and LaunchServices then refuses to open it. Running the Mach-O directly still
# works, which makes it read as "the app won't start" with no error anywhere —
# the CI already re-signs the .app for exactly this reason
# (.github/workflows/build.yml), so only the local build was left out.
if(APPLE AND TARGET T5ynth_Standalone)
    add_custom_command(TARGET T5ynth_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "ad-hoc signing Standalone app bundle"
        COMMAND codesign --force --sign - --deep
            "$<TARGET_BUNDLE_DIR:T5ynth_Standalone>"
        VERBATIM)
endif()

# ── macOS: make each bundle self-contained w.r.t. Csound ──────────────────
# Without this the LRO is silent on every machine that does not have Csound
# installed — which is every machine but this one, and the reason the published
# build compiles green and makes no sound (docs/plans/HANDOVER_LCO.md §8.1).
# tools/bundle_csound_macos.sh copies CsoundLib64, its plugin opcodes and its
# dependency tree into Contents/libs, rewrites every load command to
# @loader_path, ships the LGPL texts, and re-signs — for the .app, the .vst3 AND
# the .component, because all three link the library and all three need the copy.
#
# Runs LAST on each target: POST_BUILD commands execute in the order they are
# added, so this follows JUCE's own signing steps and the re-signs above, and
# re-seals the bundle itself afterwards.
#
# Never required, exactly like the library itself: no Csound -> no bundling step,
# and a machine with Csound but without dylibbundler builds as it did before,
# linking the system framework directly. The bundling is what an INSTALLER needs;
# a dev build that only ever runs on this machine works without it.
option(T5YNTH_BUNDLE_CSOUND
    "Copy Csound into the macOS bundles so they need no system Csound"
    ON)
if(APPLE AND T5YNTH_CSOUND_VENDORED AND T5YNTH_BUNDLE_CSOUND)
    # Vendored: the payload is already linked for this layout, so bundling is a
    # copy plus signatures. Not optional here and deliberately so — the binary
    # names @loader_path/../libs/CsoundLib64, and a bundle without it would not
    # launch at all. No dylibbundler, no Homebrew, nothing to install.
    foreach(_fmt Standalone VST3 AU)
        if(TARGET T5ynth_${_fmt})
            add_custom_command(TARGET T5ynth_${_fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env
                    "CSOUND_VENDORED_LIBS=${T5YNTH_CSOUND_VENDORED}/lib"
                    bash "${CMAKE_SOURCE_DIR}/tools/bundle_csound_macos.sh"
                    "$<TARGET_BUNDLE_DIR:T5ynth_${_fmt}>"
                VERBATIM)
        endif()
    endforeach()
elseif(APPLE AND T5YNTH_CSOUND_FOUND AND T5YNTH_BUNDLE_CSOUND)
    # A system framework instead of the payload: the old route, which has to
    # flatten the framework and pull its dependency tree in with dylibbundler.
    find_program(T5YNTH_DYLIBBUNDLER dylibbundler)
    if(NOT T5YNTH_DYLIBBUNDLER)
        message(WARNING
            "Csound found but dylibbundler is not installed — the bundles will "
            "keep linking ${T5YNTH_CSOUND_FRAMEWORK} and will only run on a "
            "machine that has it. `brew install dylibbundler` to bundle Csound.")
    else()
        foreach(_fmt Standalone VST3 AU)
            if(TARGET T5ynth_${_fmt})
                add_custom_command(TARGET T5ynth_${_fmt} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E env
                        "CSOUND_FRAMEWORK_BINARY=${T5YNTH_CSOUND_FRAMEWORK}/CsoundLib64"
                        bash "${CMAKE_SOURCE_DIR}/tools/bundle_csound_macos.sh"
                        "$<TARGET_BUNDLE_DIR:T5ynth_${_fmt}>"
                    VERBATIM)
            endif()
        endforeach()
    endif()
endif()

# Windows: the DLL and its plugin opcodes go next to the executable and into the
# VST3 bundle, where the loader looks for them (src/dsp/CsoundEngine.cpp points
# Csound at the plugins64 dir beside the module it was loaded from).
if(WIN32 AND T5YNTH_CSOUND_VENDORED AND T5YNTH_BUNDLE_CSOUND)
    foreach(_fmt Standalone VST3)
        if(TARGET T5ynth_${_fmt})
            add_custom_command(TARGET T5ynth_${_fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${T5YNTH_CSOUND_VENDORED}/bin"
                    "$<TARGET_FILE_DIR:T5ynth_${_fmt}>"
                VERBATIM)
        endif()
    endforeach()
endif()

# The standalone brings its own JUCEApplication (src/StandaloneAppLauncher.cpp)
# so that launching does not open an audio INPUT device — JUCE's default app
# does, before any window exists, and a stored setup naming a different input
# than output device makes that an aggregate-device registration that can block
# on coreaudiod with no window, no crash and no log. Resynth ext opens the input
# on demand instead; the measurement and the mechanism are in
# src/StandaloneAudioInput.h. Standalone-only: the plugin formats are untouched,
# and so is the processor's stereo input bus.
if(TARGET T5ynth_Standalone)
    target_sources(T5ynth_Standalone PRIVATE src/StandaloneAppLauncher.cpp)
    target_compile_definitions(T5ynth_Standalone PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

juce_generate_juce_header(T5ynth)

target_sources(T5ynth PRIVATE
    # Core
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/UpdateChecker.cpp

    # DSP
    src/dsp/DcoBaker.cpp
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
    src/dsp/EarlyReflections.cpp
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
    src/gui/ReplayOverlay.cpp
    src/gui/T5ynthLookAndFeel.cpp

    # Presets
    src/presets/PresetFormat.cpp
    src/presets/PresetUpdater.cpp
)

# target_sources/link additions must stay inside this if(): CsoundEngine.cpp
# #includes <csound/csound.h> at file scope and cannot even preprocess without
# the framework's headers on the include path.
if(T5YNTH_CSOUND_VENDORED)
    target_sources(T5ynth PRIVATE src/dsp/CsoundEngine.cpp)
    target_include_directories(T5ynth PRIVATE "${T5YNTH_CSOUND_VENDORED}/include")
    if(APPLE)
        # The vendored library's own install name is @loader_path/../libs/CsoundLib64,
        # so linking it records exactly the load command the bundled layout needs —
        # no install_name_tool, no rpath, nothing machine-specific in the binary.
        target_link_libraries(T5ynth PRIVATE "${T5YNTH_CSOUND_VENDORED}/lib/CsoundLib64")
    else()
        target_link_libraries(T5ynth PRIVATE "${T5YNTH_CSOUND_VENDORED}/lib/csound64.lib")
    endif()
elseif(T5YNTH_CSOUND_FOUND)
    target_sources(T5ynth PRIVATE src/dsp/CsoundEngine.cpp)
    get_filename_component(_csound_fw_dir "${T5YNTH_CSOUND_FRAMEWORK}" DIRECTORY)
    # Homebrew's opt-prefix ships an "include/csound" symlink pointing into the
    # framework's own Headers/ dir (verified on this machine:
    # /opt/homebrew/opt/csound/include/csound -> ../Frameworks/CsoundLib64.framework/Headers).
    # That's the SAME include path tools/csound_poc.cpp's proven build recipe
    # uses ($CSOUND_PREFIX/include) and is what makes `#include <csound/csound.h>`
    # resolve. _csound_fw_dir is ".../Frameworks"; its parent is the Homebrew prefix.
    # A /Library/Frameworks install has no such prefix layout -- there the
    # framework's own Headers/ dir provides csound.h, so map <csound/...> onto it.
    get_filename_component(_csound_prefix "${_csound_fw_dir}" DIRECTORY)
    if(EXISTS "${_csound_prefix}/include/csound")
        target_include_directories(T5ynth PRIVATE "${_csound_prefix}/include")
    else()
        # Recreate the same shim in the build tree: <builddir>/csound_include/csound -> Headers
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/csound_include")
        file(CREATE_LINK "${T5YNTH_CSOUND_FRAMEWORK}/Headers"
             "${CMAKE_BINARY_DIR}/csound_include/csound" SYMBOLIC)
        target_include_directories(T5ynth PRIVATE "${CMAKE_BINARY_DIR}/csound_include")
    endif()
    target_link_libraries(T5ynth PRIVATE "${T5YNTH_CSOUND_FRAMEWORK}")
    target_link_options(T5ynth PRIVATE "-Wl,-rpath,${_csound_fw_dir}")
endif()

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
        T5YNTH_HAS_CSOUND=$<BOOL:${T5YNTH_CSOUND_FOUND}>
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        T5YNTH_BACKEND_DIR="${CMAKE_SOURCE_DIR}/backend"
        T5YNTH_FULL_VERSION="${_T5YNTH_FULL_VERSION}"
)
