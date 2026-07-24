#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>

// One required file of a model, with its CURRENT published byte size from
// HuggingFace's tree API. A model's "manifest" is the vector of these — the
// authoritative packing list (root weights/config plus, for SA3, every engine
// file in the t5gemma text-encoder subfolder). relPath is the canonical path
// inside the installed model dir (may contain a "subfolder/" prefix).
struct ManifestEntry { juce::String relPath; juce::int64 size = 0; };

/**
 * Model settings panel.
 *
 * Shows model status, auto-scans known paths, browses for model directory,
 * downloads ungated models directly from HuggingFace (no token).
 * Embedded in the JUCE Audio/MIDI Settings dialog.
 */
class SettingsPage : public juce::Component,
                     private juce::Timer
{
public:
    SettingsPage();
    ~SettingsPage() override
    {
        stopTimer();
        if (downloadCancelFlag_)
            downloadCancelFlag_->store(true);
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::File scanForModel();
    bool hasAnyInstalledModel();
    void importDiscoveredModels();
    void setModelPath(const juce::File& dir);
    juce::File getModelPath() const { return modelPath; }
    void setBackendConnected(bool connected);
    void setBackendStarting();
    void setBackendFailed(const juce::String& reason);

    std::function<void()> onClose;

    /** Called when a model becomes available (after download or browse). */
    std::function<void()> onModelReady;

    /** Fired when the language model's install state TRANSITIONS (download /
     *  import / removal), with the new installed flag. One model serves all three
     *  LLM-dependent features, so this single callback lets the editor bring the
     *  LCO bake, Translate and Re-Prompt to life live, without a reopen. */
    std::function<void(bool)> onCoderModelChanged;

    /** True iff the language model is installed on disk, at either the in-app
     *  Download slot or the lco-coder/ dev-drop location. Public wrapper over the
     *  internal install gate for the editor's UI gating. */
    bool isCoderModelInstalled() const { return coderModelInstalled(); }

    /** Directory name of the language model the backend's resolver will author
     *  with (the same precedence walk as the install gate), or empty when none is
     *  loadable. MainPanel feeds it to the LCO model tab, which names the model
     *  instead of showing a role placeholder. */
    juce::String resolvedCoderDirName() const;

    static juce::File getAppSupportModelDir();
    static juce::File getAppSupportModelDir(const juce::String& modelId);

private:
    class ModelRow;   // defined below; forward-declared for activeRow()'s return type

    void browseForModel();
    void startDownload();
    // The two halves of the Download action. scanDownloadsThenDownload() is the
    // silent ~/Downloads pre-scan native-Stability models run first (import the
    // exact files if the user already hand-fetched them; otherwise fall through);
    // startRepoDownload() is the actual repo/mirror fetch — also the direct path
    // for every other model and the target the pre-scan falls through to.
    void scanDownloadsThenDownload();
    void startRepoDownload();
    void updateStatus();
    // Recompute every engine row's installed-light / status / action button from
    // the on-disk scan. Cheap and idempotent: each ModelRow caches its visual
    // state and only repaints on an actual change (idle-CPU safe).
    void refreshAllRows();
    // Refresh the language-model ROW (installed light / status / button, or
    // download mode if its download is active) from the on-disk install check.
    // Called at the end of refreshAllRows(), including its transition-latch fire
    // of onCoderModelChanged so an install refreshes the gate live.
    void refreshCoderRow();
    // True iff the backend's resolver will find a loadable language model: the
    // emptiness check of resolvedCoderDirName(), which mirrors the backend's
    // _resolve_coder_model_dir (pipe_inference.py) step for step.
    bool coderModelInstalled() const;
    // Transition latch for onCoderModelChanged: refreshCoderRow() is called on
    // every refresh (construction, download/import, backend connect), so it
    // notifies only when `installed` actually changes. `known` stays false until
    // the first refresh; MainPanel also pushes an explicit initial state, so a
    // missed ctor-time fire (callback not wired yet) is harmless.
    bool coderInstalledLast_  = false;
    bool coderInstalledKnown_ = false;
    void timerCallback() override;
    void setModelInstallBusy(bool busy, const juce::String& statusText = {});
    juce::Result importModelDirectoryForId(const juce::String& modelId,
                                           const juce::File& sourceDir,
                                           juce::File& activeDir,
                                           bool replaceExistingTarget);

    // Smart Auto-Scan entry point: checks known install paths, and for
    // native Stability models walks the user's Downloads folder looking for
    // the two files they were told to fetch manually from HuggingFace.
    void performAutoScan();

    // Tri-state result from the install helpers. The Auto-Scan dispatcher
    // needs to distinguish "tried something silently and nothing was there"
    // (let the next fallback try) from "we already surfaced a dialog to the
    // user, do NOT chain into the next fallback".
    //  - Installed:         success path completed; success dialog shown.
    //  - AbortedWithDialog: refused to install (e.g. duplicate of an
    //                       already-installed model); error/warning dialog
    //                       shown. Caller must NOT chain further fallbacks.
    //  - NotInstalled:      nothing actionable in this source; caller may
    //                       try the next fallback (e.g. folder picker).
    enum class InstallOutcome { Installed, AbortedWithDialog, NotInstalled };

    // Install a gated native Stability model from the given source folder by
    // matching every entry of its live HF manifest to a file in the folder
    // (canonical name + browser-rename tolerance; the two equally-named
    // model.safetensors are told apart by their manifest sizes). Reports
    // missing / wrong-size / success, reconstructs the t5gemma subfolder, then
    // copies into the target app-support dir on success. Used for both the
    // Downloads folder (primary) and any folder chosen via the picker fallback.
    // The manifest is fetched ONCE up front and threaded through both attempts.
    InstallOutcome installFromManifestFolder(const juce::File& sourceFolder,
                                             const juce::String& modelId,
                                             const juce::String& modelDisplayName,
                                             const std::vector<ManifestEntry>& manifest,
                                             bool reportIfMissing);

    void downloadAllFilesInThread();
    void downloadGhReleaseInThread();
    void downloadReassemblyInThread();
    // Stand-alone t5-base fetch for the manual SAO import paths (no reassembly
    // thread to piggyback on); the in-app SAO download chains t5-base inline.
    void startT5BaseChainDownload();
    void onDownloadFinished(bool success, const juce::String& error);
    // Abort the in-flight download from the active row's Cancel button: signal the
    // worker thread, stop the timer, and restore the rows (partial files are kept
    // on disk so the next Download resumes). The thread exits silently.
    void cancelDownload();
    // The row whose download is in flight (matched by activeOpModelId_), across
    // both the engine rows and the translation row. nullptr if none matches.
    ModelRow* activeRow();
    static bool isLfsPointer(const juce::File& file);
    void cleanupBadFiles(const juce::File& dir);

    juce::String selectedModelId();
    juce::String selectedHfRepo();
    juce::String selectedGhRelease();
    bool selectedDownloadable();
    bool selectedIsGenerationEngine();
    juce::String selectedModelDisplay();
    // GhAsset list returned as opaque pointer to avoid header pollution;
    // implementation in SetupWizard.cpp pulls from the kKnownModels catalog.
    const void* selectedGhFiles();
    int         selectedGhFileCount();
    // ReassemblyAsset list (SA3 multi-source install), opaque for the same reason.
    const void* selectedAssets();
    int         selectedAssetCount();

    juce::File modelPath;

    // One row of the Model Manager: a generation engine's name + a one-line
    // "what encoder ships with it" sublabel, a status string, a single
    // contextual action button, and a right-edge "installed" light. Secondary
    // actions (open page / browse / reveal) live on a right-click menu so the
    // visible row stays Ableton-clean. The text encoders (t5-base, t5gemma)
    // never get a row of their own — they install with their engine.
    class ModelRow : public juce::Component
    {
    public:
        enum class Action { Download, GetFiles, Installed };

        ModelRow(juce::String id, juce::String displayName, juce::String sublabel);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;

        // Update visual state; repaints only when something actually changed.
        // Also exits download mode (restores the action button) if it was active.
        void setState(bool installed, const juce::String& statusText,
                      Action action, bool actionEnabled);

        // Put the row into active-download mode: the action button turns into a
        // Cancel button (fires onCancel) and paint() draws an inline progress
        // meter along the row's foot in place of the separator. Idempotent.
        void enterDownloadingState();
        // Update the inline meter + live readout (e.g. "0.5/2.3 GB  ·  14 MB/s")
        // while in download mode. Repaints only when the quantized fill or the
        // readout text actually changes (idle-CPU safe even at 4 Hz).
        void setDownloadProgress(double fraction, const juce::String& detail);

        const juce::String& modelId() const { return modelId_; }

        std::function<void(juce::String)> onAction;     // primary action button
        std::function<void(juce::String)> onCancel;     // Cancel button (download mode)
        std::function<void(juce::String)> onOpenPage;   // right-click menu
        std::function<void(juce::String)> onBrowse;     // right-click menu
        std::function<void(juce::String)> onReveal;     // right-click menu (installed)

    private:
        // Style the single action button as either the green primary action or
        // the neutral Cancel affordance shown while this row is downloading.
        void applyActionButtonStyle(bool downloadingStyle);

        juce::String modelId_, displayName_, sublabel_, statusText_;
        bool installed_ = false;
        Action action_ = Action::Download;
        // Active-download overlay state (independent of the idle installed/status
        // fields above so a finished/cancelled download cleanly restores them).
        bool downloading_ = false;
        double progress_ = 0.0;       // 0..1 fill fraction
        int progressPermille_ = -1;   // last painted fill, in 0.1% steps (repaint gate)
        juce::String detail_;         // live readout shown in the status slot
        juce::TextButton actionBtn_;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModelRow)
    };

    // UI elements
    juce::Label titleLabel;
    juce::Label backendStatusLabel;        // separate footer line (backend state)
    juce::TextEditor instructionsLabel;    // shared detail strip: focused-model
                                           // info, manual-install steps, errors
    juce::Label downloadStatusLabel;       // phase / terminal status line (footer)
    bool backendConnected = false;
    juce::String backendFailReason;

    // The five generation-engine rows (built in the constructor) plus the
    // family-header rects painted above each family's first row. Declared AFTER
    // the shared members so they (and their child buttons) destruct first.
    std::vector<std::unique_ptr<ModelRow>> rows_;
    struct FamilyHeader { juce::String text; juce::Rectangle<int> bounds; };
    std::vector<FamilyHeader> familyHeaders_;

    // The language-model row — visually identical to the engine rows (its own
    // "LANGUAGE MODEL" family header is drawn in paint()), but the model is an
    // auxiliary asset: it never activates as a generation engine, so its
    // installed-check uses coderModelInstalled() and onDownloadFinished routes
    // around the engine-activation glue. Kept out of rows_ so the engine-row loop
    // stays purely about generation engines.
    std::unique_ptr<ModelRow> coderRow_;

    std::unique_ptr<juce::FileChooser> fileChooser;

    struct DownloadFile {
        juce::String remotePath;
        int64_t size = 0;
    };
    std::vector<DownloadFile> filesToDownload;
    int64_t totalBytes = 0;
    std::atomic<int64_t> downloadedBytes { 0 };
    std::atomic<bool> downloading { false };
    std::atomic<bool> modelInstallBusy_ { false };
    bool licenseAccepted_ = false;
    // The model an in-flight action targets — the single source selectedXxx()
    // reads (decoupled from any UI widget). Set from a ModelRow's button/menu
    // callback the moment an action starts; defaults to the first row.
    juce::String activeOpModelId_;
    // The model whose DOWNLOAD is in flight, captured at start. Distinct from
    // activeOpModelId_ (which the right-click menu can re-point at another row to
    // re-describe it in the detail strip): the inline progress meter must keep
    // tracking the row that is actually downloading, never the focused one.
    juce::String downloadModelId_;
    std::shared_ptr<std::atomic<int64_t>> downloadCounter_;
    std::shared_ptr<std::atomic<bool>> downloadCancelFlag_;

    // Download-speed estimate for the active row's live readout, sampled in
    // timerCallback (EMA-smoothed). lastTickMs_ == 0 marks "no baseline yet" so
    // the first tick after a (re)start seeds without emitting a bogus spike.
    int64_t lastTickBytes_ = 0;
    double  lastTickMs_ = 0.0;
    double  speedBytesPerSec_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};


/**
 * General (machine-wide) settings — the "Settings" tab beside the model manager.
 * Currently exposes the nonlinear-filter oversampling quality (Off / 2x / 4x).
 * Processor-agnostic: emits onOsQualityChanged(index 0/1/2); the host wires it to
 * the processor and seeds the current value via setOsQuality().
 */
class GeneralSettingsPage : public juce::Component
{
public:
    GeneralSettingsPage();

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Seed the dropdown to the stored value without firing onOsQualityChanged. */
    void setOsQuality(int qualityIndex);

    /** Fired when the user picks a new quality (index 0=Off, 1=2x, 2=4x). */
    std::function<void(int)> onOsQualityChanged;

    /** Seed the toggle to the stored value without firing onCheckForUpdatesChanged. */
    void setCheckForUpdatesEnabled(bool enabled);

    /** Fired when the user (un)ticks "Check for updates on startup". */
    std::function<void(bool)> onCheckForUpdatesChanged;

    /** Seed the toggle to the stored value without firing onEventLogEnabledChanged. */
    void setEventLogEnabled(bool enabled);

    /** Fired when the user (un)ticks "Record Event Log". */
    std::function<void(bool)> onEventLogEnabledChanged;

    /** Reveal (or clear) the "Update available: vX — Download" row at the top of
     *  this page. Called by MainPanel when the background UpdateChecker finds a
     *  newer release; the Download button opens `url` in the browser. */
    void setUpdateAvailable(const juce::String& version, const juce::String& url);

private:
    void layoutRows();

    // Update banner (top of page; hidden until an update is found).
    juce::Label          updateLabel_;
    juce::TextButton     updateDownloadBtn_ { "Download" };
    juce::String         updateUrl_;
    bool                 updateAvailable_ = false;

    juce::Label          osTitle_;
    juce::ComboBox       osCombo_;
    juce::Rectangle<int> helpBounds_;
    juce::ToggleButton   updateCheckToggle_ { "Check for updates on startup" };
    juce::ToggleButton   eventLogToggle_ { "Record Event Log (.t5evt)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsPage)
};
