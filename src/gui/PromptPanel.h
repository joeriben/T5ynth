#pragma once
#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include "../inference/PipeInference.h"
#include "../eventlog/EventLog.h"   // GenerationEventLogEntry (replay transport)
#include "../dsp/BlockParams.h"   // RepromptCoupling (Re-Prompt coupling enum)
#include "GuiHelpers.h"  // FlippedVerticalSlider, AlphaSliderLnF, impulse colours
// (the baked table is now drawn in the engine window — SynthPanel/WaveformDisplay
//  wtMode — so the LCO shows the machine's READING here, not the wave)
#include "RepromptStanceBar.h"    // Re-Prompt stance "symbol slider"
#include "T5ynthLookAndFeel.h"    // base LnF for the model-switch text override

class T5ynthProcessor;

/**
 * LookAndFeel for the model-switch buttons: identical to T5ynthLookAndFeel for plain
 * slots, but a slot carrying a non-empty "tierLetter" property (the two SA3 slots when
 * both tiers are installed) is drawn as a centred group [label │ letter]: the label,
 * a thin vertical divider hugging the label's right edge (no gap to its left), then the
 * s/m tier letter. Owning the whole layout here — rather than overpainting a badge —
 * lets the divider sit exactly against the measured label width while the group stays
 * balanced in the button.
 */
class ModelSwitchLnF : public T5ynthLookAndFeel
{
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto font = getTextButtonFont(b, b.getHeight());
        g.setFont(font);

        const auto ink = b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId
                                                         : juce::TextButton::textColourOffId)
                             .withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f);

        const int yIndent = juce::jmin(4, b.proportionOfHeight(0.3f));
        const int corner  = juce::jmin(b.getHeight(), b.getWidth()) / 2;
        const int fontH   = juce::roundToInt(font.getHeight() * 0.6f);
        const int left    = juce::jmin(fontH, 2 + corner / (b.isConnectedOnLeft()  ? 4 : 2));
        const int right   = juce::jmin(fontH, 2 + corner / (b.isConnectedOnRight() ? 4 : 2));
        const int textH   = b.getHeight() - yIndent * 2;

        const juce::String tier = b.getProperties().getWithDefault("tierLetter", juce::String()).toString();

        // Plain slot: stock centred label.
        if (tier.isEmpty())
        {
            g.setColour(ink);
            const int textW = b.getWidth() - left - right;
            if (textW > 0)
                g.drawFittedText(b.getButtonText(), left, yIndent, textW, textH,
                                 juce::Justification::centred, 1);
            return;
        }

        // SA3 tier slot: centre [label │ letter] as one group. The divider hugs the
        // label's measured right edge (no space to its left); the letter follows it.
        const int labelW  = font.getStringWidth(b.getButtonText());
        const int letterW = font.getStringWidth(tier);
        const int gapR    = juce::jmax(3, b.getHeight() / 5);     // divider -> letter
        const int groupW  = labelW + 1 + gapR + letterW;          // +1 = divider stroke
        const int gx      = juce::jmax(left, (b.getWidth() - groupW) / 2);
        const int dx      = gx + labelW;                          // divider, hard against the label
        const int vInset  = juce::roundToInt(static_cast<float>(b.getHeight()) * 0.26f);

        g.setColour(ink);
        g.drawText(b.getButtonText(), gx, yIndent, labelW, textH, juce::Justification::centredLeft, false);

        g.setColour(ink.withMultipliedAlpha(0.5f));
        g.drawVerticalLine(dx, static_cast<float>(vInset), static_cast<float>(b.getHeight() - vInset));

        g.setColour(ink);
        g.drawText(tier, dx + 1 + gapR, yIndent, letterW, textH, juce::Justification::centredLeft, false);
    }
};

/**
 * GENERATION column: prompts, embedding controls, compact params, generate.
 */
class PromptPanel : public juce::Component, private juce::Timer
{
public:
    explicit PromptPanel(T5ynthProcessor& processor);
    // stopTimer() first (JUCE rule). Then stop any replay: the transport's
    // generation half lives here — with the panel gone, the tape's notes would keep
    // firing while its timbre changes never arrive. Recording is unaffected (it is
    // processor-owned); only playback, which the user started from this window,
    // ends with the window.
    ~PromptPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    int getPreferredHeightForWidth(int width) const;
    void setEasyMode(bool easy);

    // Enable/disable the controls that REQUIRE the optional translation LLM (Qwen):
    // the Re-Prompt stance bar + coupling and the Translate flag. Driven by the
    // model-settings install state (MainPanel). Disabled controls dim and explain
    // themselves via tooltip; the loop also bails (runSemanticLoopStep) so a preset
    // with an engaged stance can't silently no-op when Qwen is absent. Message thread.
    void setQwenAvailable(bool available);

    // Enable/disable the base LCO generate path (triggerDcoBake), which REQUIRES
    // the optional LCO coder LLM (Qwen2.5-Coder-3B). Driven by the model-settings
    // install state (MainPanel), mirroring setQwenAvailable/qwenAvailable_ — but
    // scoped to just the one flag triggerDcoBake reads; Re-Prompt
    // (triggerDcoReprompt) is untouched, it already has its own qwenAvailable_
    // gate for the (different) translation model. Message thread.
    void setCoderAvailable(bool available);

    /** Truthful display name for the LCO interpretation LLM shown on the LCO
     *  model button (dcoModelBtn). Driven by the model-settings install state
     *  (MainPanel) — never fabricated; falls back to "LCO" if empty. */
    void setLcoModelName(juce::String name);
    bool isEasyMode() const { return easyMode_; }
    bool hasHiddenActiveState() const;

    /** Load preset data that isn't in APVTS (prompts, seed, random toggle, device, model,
     *  research-mode injection state). The injection fields are optional — when omitted,
     *  the panel keeps its current values, which lets callers decide per-feature whether
     *  to push or preserve. */
    void loadPresetData(const juce::String& promptA, const juce::String& promptB,
                        int seed, bool randomSeed,
                        const juce::String& device = {},
                        const juce::String& model = {},
                        const juce::String& injectionMode = {},
                        float lateMixAmount = std::numeric_limits<float>::quiet_NaN(),
                        float splitStart = std::numeric_limits<float>::quiet_NaN(),
                        float splitEnd = std::numeric_limits<float>::quiet_NaN());

    /** Read current prompt/seed state (for preset save). Seed state lives on the
     *  processor (setLastSeed/getLastSeed, setLastRandomSeed/getLastRandomSeed),
     *  driven exclusively by the Easy-mode Variation switchbox — the Advanced
     *  seedEditor/randomSeedToggle widgets were removed (DCO Slice 0). */
    juce::String getPromptA() const { return promptAEditor.getText().trim(); }
    juce::String getPromptB() const { return promptBEditor.getText().trim(); }

    /** LCO (Advanced) prompt text — the SNAP slots park/recall it in LCO mode. get
     *  returns the raw editor text; set replaces it without notifying (no auto-bake —
     *  recall parks the prompt; the user bakes via GENERATE when ready). */
    juce::String getLcoPrompt() const { return dcoPromptEditor.getText(); }
    void setLcoPrompt(const juce::String& t) { dcoPromptEditor.setText(t, juce::dontSendNotification); }

    /** Restore the "HEARD AS" reading box on LCO preset load — the same
     *  text triggerDcoBake's completion lambda writes into it after a
     *  bake, so a reload shows the original reading rather than an empty
     *  placeholder or a re-derived one. */
    // Preset restore writes the card directly. Bumping the bake generation here
    // is what stops an in-flight self-check — started on the PREVIOUS orchestra,
    // still running because CLAP plus an LLM turn take many seconds — from landing
    // on the freshly loaded preset and describing a sound that is no longer loaded.
    void setLcoReadingA(const juce::String& t)
    {
        ++dcoBakeSeq_;
        dcoSelfCheck_.clear();
        dcoReadingEditorA.setText(t, juce::dontSendNotification);
    }

    /** Compose the HEARD AS box's full disclosure text: the short human
     *  reading followed by its parametrisation — one "<key>: <why>" /
     *  "params: name=anchor|anchor|..." catalogue block per technique that
     *  actually resolved into the sound. The reading alone ("saw > fm_bell")
     *  is a terse gloss; BJ asked for the real parametrisation behind it to
     *  be visible, not raw Csound source — used both by triggerDcoBake's
     *  completion lambda and by MainPanel's Csound-preset restore path, so
     *  the two never drift. */
    static juce::String formatLcoDisclosure(const juce::String& reading,
                                            const juce::String& paramsText,
                                            const juce::String& selfCheck = {})
    {
        // fromUTF8: the box-drawing rule (U+2500) is non-ASCII, and a raw literal
        // mojibakes through juce::String's default narrow-string constructor.
        juce::String out = reading;
        if (paramsText.isNotEmpty())
            out += juce::String::fromUTF8("\n\n── Parametrisation ──\n") + paramsText;
        if (selfCheck.isNotEmpty())
            out += juce::String::fromUTF8("\n\n── Self-check ──\n") + selfCheck;
        return out;
    }

    /** The self-check section: how a machine listener DESCRIBED the bare
     *  oscillator, and the one sentence a second model wrote comparing that
     *  description with the prompt.
     *
     *  The description is printed in full, above the finding, and is the same
     *  string the comparison was made on (RepromptStances::composeHeardDescription
     *  builds both). That is deliberate: the finding is one model's reading of
     *  another model's words, so showing only the conclusion would present it as
     *  a fact about the sound. With the description on screen the user can see
     *  what the comparison actually had to work with — and disagree with it.
     *  No score, no percentage, no verdict on the sound. */
    static juce::String formatSelfCheck(const juce::String& description,
                                        const juce::String& finding)
    {
        juce::String out;
        if (description.isNotEmpty()) out += "heard as: " + description + "\n";
        if (finding.isNotEmpty())     out += finding;
        return out.trimEnd();
    }
    int getSeed() const;        // defined in the .cpp: T5ynthProcessor is only
    bool isRandomSeed() const;  // forward-declared here

    /** Read current injection-mode state (for preset save). */
    juce::String getInjectionMode()  const { return injectionMode_; }
    float        getLateMixAmount()  const { return lateMixForMode(injectionMode_); }
    float        getSplitStart()     const { return splitLayerStart_; }
    float        getSplitEnd()       const { return splitLayerEnd_; }

    /** Trigger generation with optional dimension offsets from DimensionExplorer. */
    void triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets);

    /** LCO (Advanced) counterpart of triggerMainGeneration: the reused GENERATE
     *  button routes here in LCO mode. Stance Off (or no prior bake yet) → author/
     *  bake from the prompt; stance engaged → one re-prompt STEP (read recipe →
     *  rewrite → bake). One entry point so button + Cmd/Return + XL CC all agree. */
    void triggerLcoGenerate();

    /** Set semantic axis values to include in the next generation request. */
    void setSemanticAxes(std::map<juce::String, float> axes) { pendingAxes_ = std::move(axes); }

    /** Re-read available devices/models after the backend was restarted. */
    void refreshInferenceChoices();

    /** Called after generation with prompt refs + explorer baseline. */
    std::function<void(const std::vector<float>&, const std::vector<float>&, const std::vector<float>&)> onEmbeddingsReady;

    /** Status callback — called with status text (e.g. "generating...", "12.3s | seed 42 | mps") */
    std::function<void(const juce::String&, bool generating)> onStatusChanged;

    /** LCO busy callback — fired true when a bake/re-prompt starts, false when it
     *  ends. MainPanel uses it to disable the reused GENERATE button during an LCO
     *  authoring pass (the neural glow/cache path never runs in LCO). */
    std::function<void(bool busy)> onLcoBusyChanged;

    /** Callback to read AxesPanel values with per-slot drift offsets (wired by MainPanel). */
    std::function<std::map<juce::String, float>(float, float, float)> getAxisValuesCallback;

    /** Fired whenever the selected model may have changed (model click, preset
     *  load, backend availability). MainPanel uses it to flip the Resynth gate
     *  (SA3-only). Semantic Axes / Dimension Explorer now run on every engine. */
    std::function<void()> onModelChanged;

    /** Fired when the user toggles the SA3 tier (small/medium) by re-clicking the
     *  active SA3 slot. MainPanel persists it to ui_settings.json (machine-local). */
    std::function<void(juce::String)> onSa3TierChanged;

    /** Paint ghost overlay for alpha slider (drift modulation indicator). */
    void paintOverChildren(juce::Graphics& g) override;

    bool isGenerating() const { return generating; }

    /** Backend-selected inference device for this machine ("mps"/"cuda"/"cpu").
     *  Empty until the subprocess reports its device list. Used by the preset
     *  manager to flag presets rendered on a different device type. */
    const juce::String& getInferenceDevice() const { return defaultInferenceDevice_; }

    /** Set the per-machine SA3 tier ("small"/"medium"). persist=true writes it back
     *  through onSa3TierChanged. Re-resolves which checkpoint backs the SA3 slots. */
    void setSa3Tier(const juce::String& tier, bool persist);
    juce::String getSa3Tier() const { return sa3Tier_; }

private:
    void timerCallback() override;
    void triggerGeneration();
    // Manual Union-Jack action: translate the A/B prompts to English IN PLACE.
    // Pauses auto-regen for the duration (frees the shared IPC pipe); it resumes
    // automatically, with its unchanged bar setting, when the translation finishes.
    void translatePromptsInPlace();
    bool playNextCachedInference();
    /** Push the realized seed from a generation result onto the processor's
     *  seed store (replaces the old seedEditor display sync — no widget to
     *  update anymore, the Easy Variation switchbox reads processor state). */
    void syncSeedState(int seed);
    /** Easy-mode entry point for an exact fixed seed: opened by double-clicking
     *  the Lock (steady) button in the Variation switchbox. Async AlertWindow —
     *  mirrors the "Rename Preset" pattern in MainPanel.cpp. On OK, writes the
     *  typed value into the processor's seed store and switches to steady mode. */
    void openSeedEntryDialog();

    /** Build a PipeInference::Request from current UI state, with optional overrides. */
    PipeInference::Request buildInferenceRequest(float alphaOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  std::map<juce::String, float> axesOverride = {},
                                                  float noiseOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float magnitudeOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float lateMixOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float splitStartOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float splitEndOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float resynthOverride = std::numeric_limits<float>::quiet_NaN());

    /** Trigger generation from drift auto-regen. */
    void triggerDriftRegeneration(float effectiveAlpha,
                                  std::map<juce::String, float> effectiveAxes,
                                  float effectiveNoise,
                                  float effectiveMagnitude,
                                  float effectiveLateMix,
                                  float effectiveSplitStart,
                                  float effectiveSplitEnd,
                                  float effectiveResynth = std::numeric_limits<float>::quiet_NaN(),
                                  bool holdForBar = false);

    /** Check if drift requires auto-regeneration (called from timerCallback). */
    void pollDriftRegen();

    /** R2c: replay transport — fire the generation the playhead just crossed.
     *  Called from timerCallback; a no-op unless a .t5evt replay is running.
     *  Mirrors pollDriftRegen's shape: the tape never stalls, the regenerated
     *  timbre crossfades in whenever the backend finishes. */
    void pollReplayRegen();
    void fireReplayGeneration(const GenerationEventLogEntry& logged);
    // Held across ticks when the pipe is busy: the processor has already handed us
    // this generation (and will not arm the next until it completes), so dropping it
    // here would silently truncate the tape's generation chain. Tagged with the tape
    // epoch it was taken from, so a stop/restart inside one timer gap discards it
    // instead of firing an old tape's generation against the new one.
    std::optional<GenerationEventLogEntry> pendingReplayGen_;
    uint32_t pendingReplayEpoch_ = 0;
    // Did the previous replayed generation actually render? An internal-resynth link
    // seeds from the parent's output sitting in the processor — if the parent failed
    // at replay time, that buffer is the GRANDparent's and the seed would be wrong.
    bool lastReplayGenSucceeded_ = false;

    /** One step of the CLAP→LLM semantic self-listening loop, mirroring one
     *  iteration body of clap_llm_loop.py run_llm_loop (minus its own generate()).
     *  Called from BOTH generation-complete callbacks (manual + Auto-Regen) on the
     *  message thread with the just-rendered result. It spawns a background thread
     *  that analyzes the audio (CLAP ear) and interprets the tags into the next
     *  prompt(s) per the active stance + coupling, then writes them back into the
     *  editor(s) on the message thread WITHOUT re-triggering onTextChange. The NEXT
     *  generation (manual Generate or the Auto-Regen standing trigger) renders the
     *  rewritten prompts. No-op (cheap early-out) unless a stance is active AND the
     *  model is SA3. */
    void runSemanticLoopStep(const PipeInference::Result& result);

    T5ynthProcessor& processorRef;

    // Prompts — colour-coded editors (purple A / yellow B) replace the old text labels
    juce::TextEditor promptAEditor, promptBEditor;
    UnionJackButton translateToggle;  // session-only: translate prompts → English before conditioning

    // Embedding controls.  alphaLnF is declared BEFORE alphaSlider so the slider
    // (a later member) is destroyed first — the LnF must outlive its only user.
    AlphaSliderLnF alphaLnF;             // A→B gradient track + position-coloured thumb
    FlippedVerticalSlider alphaSlider;   // vertical A↔B blend, A at top
    juce::Label alphaLabel, alphaValue;  // retained for callbacks but hidden (gradient is self-describing)

    // Duration — house-standard inline-bar SliderRow (mirrors MainPanel's
    // RESYNTH row): accent-band label + fill bar in kOscCol, with the "Ns"
    // read-out as the inline value. Easy view only (moved out of Advanced
    // entirely). Declared BEFORE durA (below) so the attachment tears down
    // first (reverse destruction order).
    std::unique_ptr<SliderRow> durationRow;
    // Easy-view "VAR" caption for the Variation switchbox row (the 3 seed-mode
    // icons framed by paintSwitchBoxBorder — no card, standard row height).
    juce::Label varSwitchLabel;
    // Magnitude / Chaos — house-standard inline-bar SliderRows (mirror
    // durationRow above). Easy view only (moved out of Advanced entirely).
    // Declared BEFORE magA/noiseA (below) so the attachments tear down first
    // (reverse destruction order).
    std::unique_ptr<SliderRow> magRow, noiseRow;
    // DCO surface — Advanced IS the DCO panel now (a completely different
    // paradigm from the neural Easy view, not a variant of it): a 3-line
    // prompt editor (panel-local text, NOT bound to Impulse A), the
    // BAKE/status row, the machine's READING of the prompt (dcoReadingEditorA —
    // the acoustic interpretation, "how it was heard", where the baked wave
    // used to sit; the wave itself now draws in the engine window), and the
    // flags list (the guardrail honesty channel, one "word: reason" line per
    // approximated/unmappable term). GENERATE authors a Csound orchestra from
    // the DCO prompt via the backend lexicon router (triggerDcoBake), which
    // forces the engine into Csound mode and hands the compiled orchestra off
    // to requestCsoundOrchestra().
    juce::TextEditor dcoPromptEditor;
    // Status/error channel — kept as the logical holder (many call sites write
    // it) but no longer laid out as its own visible line; its text is routed
    // into dcoReadingEditorA below (see setLcoStatus).
    juce::Label dcoStatusLabel;
    juce::Label dcoFlagsLabel;
    // The machine's reading of the prompt, shown prominently in the middle of
    // the LCO panel in place of the baked wave (which the engine window now
    // owns) — the ONE full-width "HEARD AS" surface (dcoReadingEditorA,
    // styled like promptAEditor/dcoPromptEditor): the reading of the
    // prompt-authored Csound orchestra (triggerDcoBake's completion lambda).
    // The dual A+B/harmonic-inharmonic split this used to carry (a second,
    // twin editor) is retired — BJ 2026-07-17: "this split is dead" — one
    // combined authored voice has no per-engine reading to show. Populated
    // from the bake's reading text; empty-state placeholder until the first
    // Generate. Also carries status/error text when there is no reading yet
    // (setLcoStatus).
    juce::TextEditor dcoReadingEditorA;
    bool dcoBaking_ = false;
    /** The LCO GENERATE trigger (SPEC_phase4_5_csound_llm_preset.md, Phase 4):
     *  authors a Csound orchestra from dcoPromptEditor's text via the coder/
     *  interpreter model (backend/csound_author.py — one constrained instruct
     *  call + at most one retry, LLM-first, no fallback), forces the engine
     *  into Csound mode (T5ynthProcessor::forceCsoundEngineMode, reusing
     *  dcoPrevEngineMode_), and hands the compiled orchestra text off to
     *  requestCsoundOrchestra(). */
    void triggerDcoBake();
    /** Write text into both the logical status holder and the visible HEARD AS
     *  box (which doubles as the LCO status/error channel until a reading
     *  exists). tooltip is optional context shown on the box. */
    void setLcoStatus(const juce::String& text, const juce::String& tooltip = {});

    /** Phase 5 compile-window poll (SPEC_phase4_5_csound_llm_preset.md):
     *  called from the EXISTING 10Hz timerCallback() (PromptPanel is already
     *  a juce::Timer with a correct stopTimer()-first destructor — see
     *  ~PromptPanel — so this reuses that established, already-safe Timer
     *  rather than standing up a second Timer subclass with its own
     *  lifetime to get right). A cheap no-op every tick except during the
     *  window triggerDcoBake() opens right after a successful author +
     *  requestCsoundOrchestra() call: polls csoundCompileInFlight() /
     *  csoundSwapPending() / csoundSwapFading() / csoundCompileError() until
     *  the request has resolved (covers BOTH the fade path and the "no ready
     *  active engine -> instant adopt" path, which never sets
     *  swapPending/swapFading at all), then reports compiling -> ok/error
     *  via dcoFlagsLabel. */
    void pollCsoundCompile();
    bool   csoundCompileWatching_ = false;   // a compile-window poll is active
    bool   csoundCompileSeenBusy_ = false;   // observed at least one busy tick this window
    double csoundCompileWatchStartMs_ = 0.0; // juce::Time::getMillisecondCounterHiRes() at window-open

    // DCO Re-Prompt (stance-driven self-reading loop, docs/DCO_REPROMPT_CONCEPT.md):
    // a SECOND stance bar bound to its OWN parameter (dcoRepromptStance — never
    // repromptStance; paradigm isolation, see BlockParams.h). One step = the router
    // reads its own last bake (resolved + recipe facts + flags), Qwen rewrites the
    // DCO prompt under the selected stance, then triggerDcoBake() re-bakes it. The
    // reused GENERATE button drives it (there is no separate STEP button): stance
    // Off → bake, stance engaged → one step. All state below is panel-local and
    // deliberately NOT persisted (the same seam as dcoPromptEditor itself, above):
    // the editor IS the visible chain, and v1 has no auto-restore of the human
    // original when the stance returns to Off (concept doc "Ein Feld, keine
    // Historie sichtbar").
    RepromptStanceBar dcoStanceBar;
    // "RE-PROMPT" framed card (house ModuleBox — the same template as the neural
    // repromptModuleBox), holding just the DCO stance bar (no coupling column:
    // the DCO chain has no A/B poles to couple). Sits directly above the reused
    // GENERATE button, which drives it: stance Off -> bake, stance engaged ->
    // one re-prompt step.
    ModuleBox dcoRepromptBox;
    juce::String dcoLastMachineReading_, dcoLastFlagsLine_, dcoLoopLast_;
    // Self-check section for the current bake. Message-thread only. Deliberately
    // NOT stashed on the processor and NOT persisted: it is a finding about the
    // sound THIS bake made, and a restored preset that showed one would be
    // asserting a check that never ran on this session's render.
    juce::String dcoSelfCheck_;
    // Bake generation, bumped by triggerDcoBake. The self-check runs long after
    // its bake (render + CLAP + interpret) and must prove it is still describing
    // the sound on screen. A busy FLAG cannot do that: by the time a slow check
    // returns, a second bake may have both started AND finished, clearing the
    // flag — and the stale finding would then be appended to the new bake's card,
    // asserting a measurement of a sound that is no longer loaded.
    unsigned long long dcoBakeSeq_ = 0;
    juce::StringArray dcoLoopRecent_;
    // The Re-Prompt LLM's allowed palette — the scanner's own vocabulary, sent
    // back as a sibling field on every author response (backend dco_recipe.
    // reference_vocabulary). Static per lexicon; cached from the first non-empty
    // bake and appended to every re-prompt turn so the LLM stops emitting words
    // the instrument silently drops. Message-thread only (written in the bake
    // callAsync, read in triggerDcoReprompt) — no cross-thread access.
    juce::String dcoReferenceVocab_;
    bool dcoRepromptBusy_ = false;
    void triggerDcoReprompt();

    static constexpr int kNumSeedModeBtns = 3;
    // Declared BEFORE seedModeBtns so it outlives them (LnF destruction order).
    IconButtonLnF seedBtnLnF;
    juce::TextButton seedModeBtns[kNumSeedModeBtns];
    juce::Rectangle<int> seedModeSwitchBounds;
    enum class SeedMode { base = 0, steady = 1, autoRandom = 2 };
    SeedMode seedMode_ = SeedMode::base;
    void setSeedMode(SeedMode mode, bool applyState);
    void syncSeedModeFromCurrentState();
    void syncSeedModeButtons();

    // Model selector (fixed 4-slot switchbox: SA3 Music | SA1 Open | SA1 Small | AudioLDM2).
    // Easy-only: the model choice belongs to the neural view — Advanced is
    // the DCO panel, where no neural engine is involved.
    static constexpr int kNumModelSlots = 5;
    // Declared BEFORE modelBtns so it outlives them (LnF destruction order).
    ModelSwitchLnF modelSwitchLnF;
    juce::TextButton modelBtns[kNumModelSlots];
    // LCO (Advanced) model-selector button — display-only for now (selection is
    // future work), styled EXACTLY like modelBtns[] (same modelSwitchLnF). Must
    // stay declared AFTER modelSwitchLnF above (LnF destruction-order rule: the
    // LnF must outlive every component whose setLookAndFeel points at it).
    juce::TextButton dcoModelBtn;
    juce::String lcoModelName_ { "LCO" };
    juce::String modelSlotIds[kNumModelSlots];  // resolved model directory name per slot
    juce::Rectangle<int> modelSwitchBounds;

    // SA3 tier (small | medium): a per-machine meta-choice with NO dedicated widget.
    // It is surfaced as a small "s"/"m" cell (divider + letter) that ModelSwitchLnF
    // draws on the two SA3 model slots from their "tierLetter" property, and toggled by
    // a SECOND click on the already-selected SA3 slot (see modelBtns onClick). Shown/
    // togglable only when BOTH tiers are installed (otherwise forced, cell hidden). NO
    // APVTS — machine-local, persisted to ui_settings.json by MainPanel via
    // onSa3TierChanged, never preset or automation. Default "small": installing medium
    // no longer silently overrides the lighter one.
    juce::String sa3Tier_ = "small";
    bool sa3TierChoiceAvailable_ = false;         // both tiers installed → tier cell + re-click toggle
    int  activeModelSlot_ = -1;                   // last-selected slot, for SA3 re-click detection

    // Unified switchbox frame for the injection-mode buttons (matches
    // modelSwitchBounds / seedModeSwitchBounds), and a divider line separating
    // the impulse/blend group from the params below.
    juce::Rectangle<int> injModeSwitchBounds;
    int paramsDividerY = -1;
    bool modelsPopulated = false;
    juce::String pendingModel_;  // deferred model selection until models are populated
    // Deferred split values: when a preset arrives before the backend has
    // reported its model list, we cache the preset's splitStart/splitEnd
    // here and clamp them against ditBlocks_ once populateModelSelector
    // has run refreshDitBlocksForCurrentModel for the resolved model. NaN
    // means "no pending value" so the panel falls back to current state.
    float pendingSplitStart_ = std::numeric_limits<float>::quiet_NaN();
    float pendingSplitEnd_   = std::numeric_limits<float>::quiet_NaN();
    void populateModelSelector();
    juce::String getSelectedModel() const;
    /** Index of the currently-toggled, occupied model slot, or -1 if none. The
     *  Music (0) and SFX (1) slots can hold the SAME SA3 checkpoint, so the slot
     *  index — not the model id — is what selects the SA3 track type at request time. */
    int getSelectedSlot() const;
    /** Pull the active model's DiT block count from the backend's handshake
     *  metadata into ditBlocks_, then clamp/expand the layer-split slider
     *  accordingly. Called after model selection changes and after the
     *  backend's ready frame arrives. */
    void refreshDitBlocksForCurrentModel();

    /** Re-scope the Duration slider for the active model: 120s for SA3 (music-
     *  scale / deconstructed samples), 11s otherwise. Clamps the parameter into
     *  the new ceiling. Called wherever refreshDitBlocksForCurrentModel runs. */
    void applyDurationRangeForCurrentModel();

    // Device selection is backend-controlled: GPU/Metal when available, else CPU.
    juce::String defaultInferenceDevice_;
    bool devicesPopulated = false;
    void populateDeviceChoice();

    juce::TextButton generateButton { "Generate" };

    // ── Temporary injection-mode test UI (research; not in APVTS). ──
    // Six toggle buttons + the existing alphaSlider whose label/range/state
    // shift with the active mode (linear=A↔B, step-in=step-transition, layer=split,
    // combo1/2/3=Step-in-style slider with hardcoded layer band).
    juce::TextButton injModeLinear { "Linear" };
    juce::TextButton injModeFine   { "Step-in" }; // = "late_step" — operates on sampler refinement steps
    juce::TextButton injModeLayer  { "Layer" };   // = "layer_split"
    juce::TextButton injModeKombi1 { "Combo 1" }; // = late × low-layer band [0..4]
    juce::TextButton injModeKombi2 { "Combo 2" }; // = late × broad-mid band [4..12]
    juce::TextButton injModeKombi3 { "Combo 3" }; // = late × narrow-center band [6..10]
    juce::String     injectionMode_         = "linear";  // "linear" | "late_step" | "layer_split" | "kombi1"/"kombi2"/"kombi3"
    // Step-in and the three Combo modes each remember their own slider position
    // (0–1, intensity), so a user A/B-ing the modes by clicking buttons
    // doesn't lose the last-used value of any individual mode. Layer mode
    // uses splitLayerStart_/splitLayerEnd_ instead. The backend's
    // `injection_transition_at` is the early-phase fraction; we send
    // 0.5 - 0.45·t (so t=0 → transition halfway, t=1 → almost immediate),
    // and `late_phase_alpha` = t directly (0 → 50/50 late blend, 1 → pure B).
    float            lateMixFine_           = 0.5f;
    float            lateMixKombi1_         = 0.5f;
    float            lateMixKombi2_         = 0.5f;
    float            lateMixKombi3_         = 0.5f;
    /** Returns a reference to the slot that owns the slider value for the
     *  active mode. Falls back to the Step-in slot for "linear" and "layer_split"
     *  (those modes use other state, but a non-null reference simplifies
     *  the call sites). */
    float&       lateMixForMode(const juce::String& mode);
    float        lateMixForMode(const juce::String& mode) const;
    // Layer mode: two-thumb range slider defining the B-zone [start, end]
    // along the DiT block indices. Both thumbs at extremes → full B;
    // start == end → no B (pure A); narrow range → B injected only into
    // a sub-band of layers (mid / early / late depending on position).
    // Range is 0..ditBlocks_; ditBlocks_ is refreshed from backend metadata
    // (SAO Small = 16, SA3 Small may differ) — refreshDitBlocksForCurrentModel
    // updates the slider on model change.
    float            splitLayerStart_       = 4.0f;       // 0–ditBlocks_, low thumb
    float            splitLayerEnd_         = 16.0f;      // 0–ditBlocks_, high thumb (default = top: B from layer 4 onwards)
    int              ditBlocks_             = 16;         // per-model count; updated on model change

    /** Reconfigure alphaSlider (range, label, value, attachment) for the active mode. */
    void applyModeToSlider();
    bool isAudioLDM2Model(const juce::String& model) const;
    bool selectedModelIsAudioLDM2() const;
    bool isSA3Model(const juce::String& model) const;
    /** Resolve a stored/preset model id to an installed switchbox slot: exact
     *  id match, then family fallback (patternSlotFor); -1 if none installed. */
    int  slotForModel(const juce::String& model) const;

public:
    /** True when the active model is SA3 (Stable Audio 3 Small Music). Public
     *  so MainPanel can gate the SA3-only Resynth control and the 120 s duration
     *  ceiling. (Semantic Axes / Dimension Explorer now run on SA3 too.) */
    bool selectedModelIsSA3() const;

private:
    /** Per-model defaults for the steps/CFG params. The model-click handler
     *  applies these on model switch, and hasHiddenActiveState() compares
     *  against them to decide whether easy mode's params reflect a
     *  user-modified state. Keeping the mapping in one place ensures the
     *  two stay in lockstep — drift would mean the dirty indicator
     *  contradicts the values the backend actually receives. */
    struct DefaultParams { float steps; float cfg; };
    DefaultParams defaultParamsFor(const juce::String& model) const;
    void selectInjectionMode(const juce::String& mode, bool trigger);
    void syncInjectionModeAvailability();

    bool generating = false;
    bool translatingPrompts_ = false;  // Union-Jack translate in flight (blocks IPC overlap)
    std::vector<std::pair<int, float>> pendingOffsets_;  // for DimensionExplorer
    std::map<juce::String, float> pendingAxes_;          // for SemanticAxes

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> alphaA, magA, noiseA, durA;
    bool easyMode_ = false;

    // Auto-regen state
    float lastGenAlpha_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenNoise_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenMagnitude_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenLateMix_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenSplitStart_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenSplitEnd_ = std::numeric_limits<float>::quiet_NaN();
    std::map<juce::String, float> lastGenAxes_;
    juce::String lastGenPromptA_;
    juce::String lastGenPromptB_;
    double lastRegenTimeMs_ = 0.0; // for beat-based cooldown

    // ── Semantic self-listening loop state (message-thread only) ──
    // Mirrors clap_llm_loop.py run_llm_loop's per-pole *_glieder chain + anti-stasis
    // `recent`. The chain interprets its OWN last link (glieder[-1]), which under the
    // ab_add (concat) coupling is NOT what the editor holds (editor = original + ","
    // + last), so the last link and the human original are tracked here separately
    // from the editor text. All touched only on the message thread (pollDriftRegen,
    // the generation-complete callbacks, and runSemanticLoopStep's callAsync tail).
    bool loopStepInFlight_ = false;   // re-entrancy guard (like `generating`)
    bool loopEngaged_ = false;        // false→true edge = capture the human originals
    bool qwenAvailable_ = true;       // translation LLM installed? gates Re-Prompt + Translate
    bool coderAvailable_ = true;      // LCO coder LLM installed? gates triggerDcoBake only
    juce::String loopOriginalA_, loopOriginalB_;  // glieder[0] (the human impulse, kept by concat) — FULL, incl. musical suffix (for restore)
    juce::String loopLastA_, loopLastB_;          // glieder[-1] (the chain's own last link) — CORE only (no musical suffix)
    juce::StringArray loopRecentA_, loopRecentB_; // glieder[-3:] anti-stasis memory (last 3 links) — CORE only
    // User-appended pitch/tempo control tokens (c3, 120bpm), split off the originals at
    // engage and re-appended on every editor write so the loop's LLM rewrites never
    // touch them. Empty when the originals carried none. (RepromptStances::*MusicSuffix.)
    juce::String loopSuffixA_, loopSuffixB_;
    // Pending loop prompts: held off the editor until the next generation fires,
    // so the displayed prompt only updates when it becomes wirksam (effective).
    juce::String pendingLoopPromptA_, pendingLoopPromptB_;
    // Deactivation restore: when a stance→Off transition is seen (timerCallback edge
    // detect), the human originals captured at engage are put back into the editors.
    // loopOriginalsValid_ gates it: true only once a step has captured originals AND
    // machine-modified the editors; cleared after the restore (and never set if the
    // loop never ran, so toggling a stance on/off with no render in between is a no-op).
    bool loopOriginalsValid_ = false;
    int  prevStanceForRestore_ = 0;               // RepromptStance::Off — last seen stance index
    // transcribe + AB-replace anti-collapse: transcribe reads only the shared machine
    // tags, so writing BOTH poles each step makes A==B (collapses the blend → alpha/
    // alpha-drift has nothing to interpolate). Instead write ONE pole per step,
    // alternating, so A and B stay distinct transcriptions of consecutive renders.
    // false = this step writes B, true = writes A; flips after each applied step.
    bool loopAltWriteA_ = false;
    // Resynth-loop release edge-detector: the previous loop regen's "a parameter
    // moved" state. The release (detach init so a changed prompt renders clean) is
    // edge-triggered on the false→true transition — so a continuous drift, which
    // holds the change flag true every tick, releases ONCE at onset and then locks
    // and evolves, instead of detaching every tick and disabling the loop. Reset
    // when the loop is not running. Message-thread only (pollDriftRegen).
    bool prevLoopParamsChanged_ = false;
    // Re-Prompt deactivation re-render: turning a stance Off restores the human
    // ORIGINAL prompt text, but the last render's audio (and, with Resynth on, the
    // init_audio carry) would otherwise keep the last machine B audible. On the
    // restore edge we (a) force the next render to detach init_audio so it renders
    // CLEAN from the restored prompt (forceCleanRenderOnce_, consumed in
    // buildInferenceRequest), and (b) actually fire that render once the pipe is
    // free (pendingOriginalReRender_, retried each tick in timerCallback). Both are
    // cleared together when the render's request is built. Message-thread only.
    bool forceCleanRenderOnce_ = false;
    bool pendingOriginalReRender_ = false;
    // Failure throttle: when a drift-driven auto-regen fails, gate further
    // attempts for a couple of seconds so a persistently broken backend
    // (e.g. a model the bundled pipeline can't load) doesn't get hammered
    // by the 10 Hz timer and oscillate the status label between
    // "auto regen..." and the error message.
    double lastRegenFailureMs_ = 0.0;
    float alphaGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    // Mode-specific ghosts: set when alpha-LFO offset is non-zero AND the
    // active mode targets the corresponding parameter. Painted via the same
    // drawGhost lambda in paintOverChildren.
    float lateMixGhostValue_    = std::numeric_limits<float>::quiet_NaN();
    float splitStartGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    float splitEndGhostValue_   = std::numeric_limits<float>::quiet_NaN();

    // ── Re-Prompt controls (semantic self-listening loop) ──
    // Sit directly under the prompts (above the params divider), co-located with
    // the loop logic this panel already owns (runSemanticLoopStep / pollDriftRegen).
    // A custom glyph "symbol slider" picks the interpreter STANCE (off + 6 movement
    // stances) and binds itself to repromptStance internally; a compact vertical
    // 3-way switchbox picks the COUPLING (B only / AB add / AB replace). Both are
    // message-thread-only meta-controls (read in pollDriftRegen, not in processBlock).
    // repromptCouplingA (the ComboBoxAttachment) is declared AFTER its hidden combo
    // so it tears down first (reverse-destruction order).
    // repromptModuleBox is the framed card (accent "RE-PROMPT" top-header) the
    // stance bar + coupling sit inside — it IS one control module, so it gets a
    // frame, like Duration/Variation. Decorative; sits behind them (toBack in ctor).
    ModuleBox repromptModuleBox;
    RepromptStanceBar repromptStanceBar;
    static constexpr int kNumCouplingBtns = RepromptCoupling::kCount;
    juce::TextButton repromptCouplingBtns[kNumCouplingBtns];
    juce::ComboBox repromptCouplingHidden;   // hidden; drives the visible radio buttons
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> repromptCouplingA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
