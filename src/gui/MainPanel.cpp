#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "../dsp/BlockParams.h"
#include "../presets/PresetTagSuggester.h"
#include "GuiHelpers.h"
#include "BinaryData.h"
#include <cmath>
#include <cstring>
#include <thread>

namespace
{
constexpr char kComputerKeyboardNoteKeys[] = { 'a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'z', 'h', 'u', 'j', 'k' };
constexpr int kComputerKeyboardBaseMidiNote = 60;
constexpr int kComputerKeyboardMinOctaveOffset = -5;
constexpr int kComputerKeyboardMaxOctaveOffset = 4;
constexpr const char* kUiSettingsFileName = "ui_settings.json";
constexpr const char* kOscEasyModeKey = "oscEasyMode";

const char* const kMainSnapshotParamIds[] = {
    PID::genAlpha, PID::genMagnitude, PID::genNoise, PID::genDuration,
    PID::genStart, PID::genCfg, PID::genSeed, PID::genHfBoost, PID::infSteps,
    PID::engineMode, PID::voiceCount, PID::tuning, PID::loopMode,
    PID::crossfadeMs, PID::normalize, PID::loopOptimize,
    PID::oscScan, PID::oscOctave, PID::noiseLevel, PID::noiseType,
    PID::wtFrames, PID::wtSmooth, PID::wtAutoScan,
    PID::ampAttack, PID::ampDecay, PID::ampSustain, PID::ampRelease,
    PID::ampAmount, PID::ampVelSens, PID::ampLoop, PID::ampTarget,
    PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve,
    PID::ampAttackVelMode, PID::ampDecayVelMode, PID::ampReleaseVelMode,
    PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
    PID::mod1Amount, PID::mod1VelSens, PID::mod1Loop, PID::mod1Target,
    PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve,
    PID::mod1AttackVelMode, PID::mod1DecayVelMode, PID::mod1ReleaseVelMode,
    PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
    PID::mod2Amount, PID::mod2VelSens, PID::mod2Loop, PID::mod2Target,
    PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve,
    PID::mod2AttackVelMode, PID::mod2DecayVelMode, PID::mod2ReleaseVelMode,
    PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, PID::lfo1Target, PID::lfo1Mode,
    PID::lfo1ClockMode, PID::lfo1ClockDivision,
    PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, PID::lfo2Target, PID::lfo2Mode,
    PID::lfo2ClockMode, PID::lfo2ClockDivision,
    PID::lfo3Rate, PID::lfo3Depth, PID::lfo3Wave, PID::lfo3Target, PID::lfo3Mode,
    PID::lfo3ClockMode, PID::lfo3ClockDivision,
    PID::aftertouchTarget, PID::aftertouchAmount,
    PID::driftEnabled, PID::driftRegen, PID::driftCrossfade,
    PID::drift1Rate, PID::drift1Depth, PID::drift1Target, PID::drift1Wave,
    PID::drift1ClockMode, PID::drift1ClockDivision,
    PID::drift2Rate, PID::drift2Depth, PID::drift2Target, PID::drift2Wave,
    PID::drift2ClockMode, PID::drift2ClockDivision,
    PID::drift3Rate, PID::drift3Depth, PID::drift3Target, PID::drift3Wave,
    PID::drift3ClockMode, PID::drift3ClockDivision,
    PID::filterEnabled, PID::filterType, PID::filterSlope, PID::filterCutoff,
    PID::filterResonance, PID::filterMix, PID::filterKbdTrack, PID::filterDrive,
    PID::filterDriveOs, PID::filterAlgorithm, PID::filterWarpStyle
};

bool findParameterValue(const juce::ValueTree& state, const juce::String& id, float& value)
{
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild(i);
        if (child.getProperty("id").toString() == id && child.hasProperty("value"))
        {
            value = static_cast<float>(static_cast<double>(child.getProperty("value")));
            return true;
        }
    }
    return false;
}

void restoreParameterFromState(juce::AudioProcessorValueTreeState& apvts,
                               const juce::ValueTree& state,
                               const char* id)
{
    float value = 0.0f;
    if (findParameterValue(state, id, value))
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
}

juce::File getUiSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("T5ynth")
        .getChildFile(kUiSettingsFileName);
}

#if JUCE_WINDOWS
juce::StringArray getWindowsCompanionBackendRoots()
{
    juce::StringArray roots;
    auto addRoot = [&roots](const juce::String& path)
    {
        auto trimmed = path.trim();
        if (trimmed.isNotEmpty())
            roots.addIfNotAlreadyThere(trimmed);
    };

    constexpr auto wow64 = juce::WindowsRegistry::WoW64_64bit;

    addRoot(juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\Software\\T5ynth\\BackendDir", {}, wow64));
    addRoot(juce::WindowsRegistry::getValue("HKEY_CURRENT_USER\\Software\\T5ynth\\BackendDir", {}, wow64));

    auto installDir = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\Software\\T5ynth\\InstallDir", {}, wow64);
    if (installDir.isNotEmpty())
        addRoot(juce::File(installDir).getChildFile("backend").getFullPathName());

    installDir = juce::WindowsRegistry::getValue("HKEY_CURRENT_USER\\Software\\T5ynth\\InstallDir", {}, wow64);
    if (installDir.isNotEmpty())
        addRoot(juce::File(installDir).getChildFile("backend").getFullPathName());

    addRoot("C:\\Program Files\\T5ynth\\backend");
    addRoot("C:\\Program Files (x86)\\T5ynth\\backend");
    return roots;
}
#endif

}

MainPanel::GenerateButton::GenerateButton(const juce::String& label)
    : juce::TextButton(label)
{
    setTooltip("Generate audio from the current impulses and latent controls");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MainPanel::GenerateButton::setAnimationState(float phase, bool isGenerating)
{
    animationPhase = phase;
    generating = isGenerating;
    repaint();
}

void MainPanel::GenerateButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
        return;

    const bool active = isEnabled() || generating;
    const auto label = getButtonText().trim().isNotEmpty() ? getButtonText().trim()
                                                           : juce::String("GENERATE");

    // Section-header style: same kOscCol fill as oscHeader/axesHeader/
    // dimHeader above this button. Flat, sharp corners, no border.
    // The button reads as the active counterpart to those headers — the
    // primary action of the prompt/oscillator section.
    auto fill = kOscCol;
    if (down)            fill = fill.darker(0.10f);
    else if (highlighted) fill = fill.brighter(0.10f);

    if (generating)
    {
        // Pulse fades the fill toward the page background (kBg, near-black)
        // and back. A clearly visible "thinking / working" signal that
        // stays inside the flat design language — no sweep, no stripe.
        const float pulse = 0.5f + 0.5f * std::sin(animationPhase);
        fill = fill.interpolatedWith(kBg, pulse * 0.70f);
    }

    if (! active)
        fill = fill.withAlpha(0.45f);

    g.setColour(fill);
    g.fillRect(bounds);

    // Label — dark on accent, exactly like paintSectionHeader. Font size
    // is bounded by BOTH height and width so the label can't grow out of
    // proportion when the button is short and wide (or tall and narrow).
    const float fromHeight = bounds.getHeight() * 0.50f;
    const float fromWidth  = bounds.getWidth() / 11.0f;  // ~"GENERATE" + chevrons
    const float fontSize = juce::jlimit(16.0f, 30.0f, juce::jmin(fromHeight, fromWidth));
    const float chevronSize = fontSize * 0.50f;

    // Three chevrons → "forward / run / advance" mark, clearer than one.
    const auto chevronStr = juce::String::charToString(0x276F)
                          + juce::String::charToString(0x276F)
                          + juce::String::charToString(0x276F);

    // Inverted button text follows the system convention used by every other
    // active/toggled button in the L&F: TextButton::textColourOnId = kTextPrimary
    // (near-white). Section *labels* (paintSectionHeader) use dark text on the
    // same fill, but a button is not a label — using the header convention
    // here was a mis-inheritance, corrected back to the button convention.
    const auto fgText = kTextPrimary;
    const float textAlpha = active ? (down ? 0.85f : 1.0f) : 0.55f;

    auto font = juce::Font(juce::FontOptions(fontSize, juce::Font::bold))
                    .withExtraKerningFactor(0.10f);
    auto chevronFont = juce::Font(juce::FontOptions(chevronSize, juce::Font::bold))
                           .withExtraKerningFactor(-0.05f);

    // Measure with GlyphArrangement — Font::getStringWidthFloat ignores
    // extraKerningFactor in this JUCE build, which previously clipped the
    // last glyph of "GENERATE". GlyphArrangement respects all font attributes.
    auto measureWith = [](const juce::Font& f, const juce::String& s) -> float
    {
        if (s.isEmpty()) return 0.0f;
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, s, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    };

    // Compose label + chevrons as a single centered unit so the chevrons
    // sit immediately next to the word rather than pinned to the right edge.
    const float labelW = measureWith(font, label);
    const float chevW  = measureWith(chevronFont, chevronStr);
    const float gap    = fontSize * 0.55f;
    const float compositeW = labelW + gap + chevW;

    const float minPad = 8.0f;
    const float subPxPad = 2.0f;  // absorbs sub-pixel rounding from .toNearestInt()
    const bool fits = compositeW + minPad * 2.0f <= bounds.getWidth();

    if (fits)
    {
        const float startX = juce::jmax(minPad, bounds.getCentreX() - compositeW * 0.5f);
        auto labelArea = juce::Rectangle<float>(startX, bounds.getY(),
                                                 labelW + subPxPad, bounds.getHeight());
        auto chevArea  = juce::Rectangle<float>(startX + labelW + gap, bounds.getY(),
                                                 chevW + subPxPad, bounds.getHeight());

        g.setFont(font);
        g.setColour(fgText.withAlpha(textAlpha));
        g.drawText(label, labelArea.toNearestInt(),
                   juce::Justification::centredLeft, false);

        g.setFont(chevronFont);
        g.setColour(fgText.withAlpha(textAlpha * 0.75f));
        g.drawText(chevronStr, chevArea.toNearestInt(),
                   juce::Justification::centredLeft, false);
    }
    else
    {
        // Extremely narrow: drop chevrons, fit label only.
        g.setFont(font);
        g.setColour(fgText.withAlpha(textAlpha));
        g.drawFittedText(label, bounds.reduced(minPad, 0.0f).toNearestInt(),
                         juce::Justification::centred, 1, 0.5f);
    }
}

// ─── SnapshotButton ─────────────────────────────────────────────────────────
void MainPanel::SnapshotButton::setSnapshotIndex(int index)
{
    snapshotIndex = index;
}

void MainPanel::SnapshotButton::setSnapshotFilled(bool filled)
{
    if (snapshotFilled != filled)
    {
        snapshotFilled = filled;
        repaint();
    }
}

void MainPanel::SnapshotButton::flashStored()
{
    flashUntilMs = juce::Time::getMillisecondCounterHiRes() + 430.0;
    startTimerHz(30);
    repaint();
}

void MainPanel::SnapshotButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    auto base = findColour(getToggleState() ? juce::TextButton::buttonOnColourId
                                            : juce::TextButton::buttonColourId);
    lf.drawButtonBackground(g, *this, base, highlighted, down);
    lf.drawButtonText(g, *this, highlighted, down);

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (flashUntilMs > now)
    {
        const float alpha = static_cast<float>((flashUntilMs - now) / 430.0);
        g.setColour(juce::Colours::white.withAlpha(0.28f * juce::jlimit(0.0f, 1.0f, alpha)));
        g.fillRect(getLocalBounds());
    }

    if (snapshotIndex > 0 && snapshotFilled)
    {
        auto dot = getLocalBounds().reduced(3, 0).removeFromBottom(2);
        g.setColour((getToggleState() ? juce::Colours::white : kOscCol).withAlpha(0.85f));
        g.fillRect(dot);
    }

    if (snapshotIndex > 0 && pressing && !longFired)
    {
        g.setColour(kOscCol.withAlpha(0.8f));
        g.drawRect(getLocalBounds().reduced(1), 1);
    }
}

void MainPanel::SnapshotButton::mouseDown(const juce::MouseEvent& e)
{
    pressing = true;
    longFired = false;
    pressStartMs = juce::Time::getMillisecondCounterHiRes();
    if (snapshotIndex > 0 && onPressStarted)
        onPressStarted(snapshotIndex);
    startTimerHz(30);
    juce::TextButton::mouseDown(e);
    repaint();
}

void MainPanel::SnapshotButton::mouseUp(const juce::MouseEvent& e)
{
    const bool wasShort = pressing && !longFired;
    pressing = false;
    if (wasShort && onShortActivate)
        onShortActivate(snapshotIndex);
    if (flashUntilMs <= juce::Time::getMillisecondCounterHiRes())
        stopTimer();
    juce::TextButton::mouseUp(e);
    repaint();
}

void MainPanel::SnapshotButton::mouseExit(const juce::MouseEvent& e)
{
    juce::TextButton::mouseExit(e);
    repaint();
}

void MainPanel::SnapshotButton::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (pressing && !longFired && snapshotIndex > 0 && now - pressStartMs >= 650.0)
    {
        longFired = true;
        if (onLongStore)
            onLongStore(snapshotIndex);
    }

    if (!pressing && flashUntilMs <= now)
        stopTimer();

    repaint();
}

// ─── CacheCapButton ──────────────────────────────────────────────────────────
// Replaces the dropped "inference cache N/M" status row: while the cache is
// filling, the *selected* capacity button pulses its fill colour subtly. When
// the cache is full, pulsing stops and the button sits at solid kOscCol.
void MainPanel::CacheCapButton::setPulsing(bool p)
{
    if (pulsing != p)
    {
        pulsing = p;
        repaint();
    }
}

void MainPanel::CacheCapButton::setPulsePhase(float phase)
{
    lastPhase = phase;
    if (pulsing && getToggleState())
        repaint();
}

void MainPanel::CacheCapButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    auto base = findColour(getToggleState() ? juce::TextButton::buttonOnColourId
                                            : juce::TextButton::buttonColourId);

    const bool isPulsing = pulsing && getToggleState();
    float p = 0.0f;
    if (isPulsing)
    {
        p = 0.5f + 0.5f * std::sin(lastPhase);
        base = base.brighter(0.45f * p);
    }

    lf.drawButtonBackground(g, *this, base, highlighted, down);

    if (isPulsing)
    {
        // White overlay tick adds a clearly-visible blink layer on top of
        // the brightness pulse — without it, the brighten-only effect
        // disappears on small buttons at typical viewing distance.
        g.setColour(juce::Colours::white.withAlpha(0.22f * p));
        g.fillRect(getLocalBounds());
    }

    lf.drawButtonText(g, *this, highlighted, down);
}

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      fxPanel(processor.getValueTreeState(), processor),
      sequencerPanel(processor)
{
    setOpaque(true);
    computerKeyboardActiveNotes.fill(-1);
    // Allow keyboard shortcuts (⌘S) to reach the panel even when no inner
    // text editor has focus.
    setWantsKeyboardFocus(true);
    addAndMakeVisible(promptPanel);
    addAndMakeVisible(axesPanel);
    addAndMakeVisible(synthPanel);
    addAndMakeVisible(fxPanel);
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);

    // Left column section headers
    paintSectionHeader(oscHeader, "T5 OSCILLATOR", kOscCol);
    addAndMakeVisible(oscHeader);

    poweredByLabel.setText("Powered by Stability AI", juce::dontSendNotification);
    poweredByLabel.setColour(juce::Label::textColourId, kBg.withAlpha(0.7f));
    poweredByLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    poweredByLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(poweredByLabel);

    oscModeToggle.setColour(juce::TextButton::buttonColourId, kSurface.darker(0.35f));
    oscModeToggle.setColour(juce::TextButton::buttonOnColourId, kSurface.darker(0.35f));
    oscModeToggle.setColour(juce::TextButton::textColourOffId, kOscCol);
    oscModeToggle.setColour(juce::TextButton::textColourOnId, kOscCol);
    oscModeToggle.setTooltip("Switch T5 OSC interface mode");
    oscModeToggle.onClick = [this] { setOscEasyMode(!oscEasyMode, true); };
    addAndMakeVisible(oscModeToggle);

    paintSectionHeader(axesHeader, "SEMANTIC AXES", kOscCol);
    addAndMakeVisible(axesHeader);
    paintSectionHeader(dimHeader, "LATENT DIMENSION EXPLORER", kOscCol);
    addAndMakeVisible(dimHeader);

    // Axes description note is inside AxesPanel

    // Wire StatusBar buttons
    statusBar.onNewPreset    = [this] { loadInitPreset(); };
    statusBar.onSavePreset   = [this] { savePreset(); };
    statusBar.onLoadPreset   = [this] { loadPreset(); };
    statusBar.onExportWav    = [this] { exportWav(); };
    statusBar.onSettings     = [this] { if (settingsVisible) hideSettings(); else showSettings(); };
    statusBar.onManual       = [this] { showManual(); };
    statusBar.onKeyboardInputChanged = [this](bool enabled) { setComputerKeyboardEnabled(enabled); };
    statusBar.onPresetNameContextMenu = [this](juce::Point<int> p) { showPresetNameContextMenu(p); };
    statusBar.setKeyboardInputEnabled(false);

    // Settings overlay (same pattern as DimExplorer)
    settingsScrim.onClick = [this] { hideSettings(); };
    settingsScrim.setVisible(false);
    addChildComponent(settingsScrim);
    addChildComponent(settingsPage);
    settingsPage.onModelReady = [this]
    {
        if (promptPanel.isGenerating())
        {
            pendingInferenceReload = true;
            statusBar.setStatusText("Model installed. Backend reload is queued.");
            return;
        }

        tryLoadInferenceModels(true);
    };

    presetScrim.onClick = [this] { hidePresetManager(); };
    presetScrim.setVisible(false);
    addChildComponent(presetScrim);

    presetManager.setVisible(false);
    presetManager.onCloseRequested = [this] { hidePresetManager(); };
    presetManager.onLoadRequested = [this](const juce::File& file)
    {
        if (loadPresetFromFile(file))
            hidePresetManager();
        else
            presetManager.setStatusText("Preset load failed", true);
    };
    presetManager.onImportRequested = [this] { importPresetFile(); };
    presetManager.onGetFromGithubRequested = [this]
    {
        const bool ok = juce::URL("https://github.com/joeriben/T5ynth-Presets")
                            .launchInDefaultBrowser();
        if (!ok)
            presetManager.setStatusText("Could not open GitHub preset repository", true);
    };
    presetManager.onSaveRequested = [this](const juce::String& presetName,
                                           const juce::StringArray& tags,
                                           const juce::String& bank,
                                           bool includeInferenceCache)
    {
        auto bankDir = PresetFormat::getUserPresetsDirectory();
        if (bank.isNotEmpty())
            bankDir = bankDir.getChildFile(bank);
        bankDir.createDirectory();

        // String-concat (not withFileExtension) — withFileExtension strips
        // at the last dot, so a user-typed name like "Pad 0.5" would be
        // truncated to "Pad 0.t5p" and could clobber an unrelated preset.
        auto target = bankDir.getChildFile(presetName + ".t5p");

        juce::Component::SafePointer<MainPanel> safeThis(this);
        auto performSave = [safeThis, target, tags, includeInferenceCache]
        {
            if (! safeThis) return;
            auto* self = safeThis.getComponent();
            self->processorRef.setLastTags(tags);
            if (self->savePresetToFile(target, includeInferenceCache))
            {
                self->presetManager.leaveSaveMode();
                self->hidePresetManager();
            }
            else
            {
                self->presetManager.setStatusText("Preset save failed", true);
            }
        };

        if (! target.existsAsFile())
        {
            performSave();
            return;
        }

        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Replace Preset")
                .withMessage("Replace \"" + target.getFileNameWithoutExtension()
                             + "\" in bank \""
                             + (bank.isEmpty() ? PresetManagerPanel::kRootUserBank() : bank)
                             + "\"?")
                .withButton("Replace")
                .withButton("Cancel")
                .withParentComponent(this),
            [performSave](int result)
            {
                if (result == 1)
                    performSave();
            });
    };
    presetManager.onTagsChanged = [this](const juce::File& file,
                                         const juce::StringArray& newTags)
    {
        // Surgical JSON-only patch — audio PCM and other fields are
        // preserved byte-for-byte. Works for ANY preset in the library, not
        // just the loaded one. If this IS the loaded preset, also keep the
        // processor's lastTags in sync so a subsequent Save Preset doesn't
        // resurrect the old tag list.
        if (! patchPresetTagsField(file, newTags))
        {
            presetManager.setStatusText("Tag save failed", true);
            return;
        }
        if (file == currentPresetFile)
            processorRef.setLastTags(newTags);
        presetManager.updateTagsForFile(file, newTags);
        presetManager.setStatusText("Tags saved");
    };
    presetManager.onRenameRequested = [this](const juce::File& file)
    {
        // Modal AlertWindow with a single text editor pre-filled with the
        // current name. Save = rename file on disk + patch the JSON `name`
        // field so the in-file metadata stays consistent with the filename.
        auto* alert = new juce::AlertWindow("Rename Preset",
                                            "New name for \"" + file.getFileNameWithoutExtension() + "\":",
                                            juce::MessageBoxIconType::QuestionIcon, this);
        alert->addTextEditor("name", file.getFileNameWithoutExtension());
        alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        alert->enterModalState(true,
            juce::ModalCallbackFunction::create([this, alert, file](int result)
            {
                std::unique_ptr<juce::AlertWindow> deleter(alert);
                if (result != 1) return;

                const auto requested = juce::File::createLegalFileName(
                    alert->getTextEditorContents("name").trim());
                if (requested.isEmpty() || requested == file.getFileNameWithoutExtension())
                    return;

                // String-concat (not withFileExtension) — see save callback above.
                auto target = file.getParentDirectory().getChildFile(requested + ".t5p");
                if (target.existsAsFile())
                {
                    presetManager.setStatusText("Rename failed: name already exists", true);
                    return;
                }
                if (! file.moveFileTo(target))
                {
                    presetManager.setStatusText("Rename failed: could not move file", true);
                    return;
                }
                // Patch the JSON `name` field inside the file so the embedded
                // metadata stays consistent with the new filename.
                patchPresetNameField(target, requested);

                if (currentPresetFile == file)
                    currentPresetFile = target;

                presetManager.refreshLibrary();
                presetManager.setCurrentPreset(currentPresetFile, getCurrentPresetDisplayName());
                presetManager.setStatusText("Renamed to " + requested);
            }), false);
    };
    presetManager.onDeleteRequested = [this](const juce::File& file)
    {
        // Async confirmation — the legacy showOkCancelBox returns false on
        // Linux without ever showing the dialog, which made Delete appear
        // permanently broken. The MessageBoxOptions/showAsync path works on
        // every platform.
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Delete Preset")
                .withMessage("Delete \"" + file.getFileNameWithoutExtension()
                             + "\" from the user library?")
                .withButton("Delete")
                .withButton("Cancel")
                .withParentComponent(this),
            [this, file](int result)
            {
                // showAsync convention: result == 1 → first button (Delete).
                if (result != 1) return;

                if (file.deleteFile())
                {
                    if (currentPresetFile == file)
                        currentPresetFile = juce::File();
                    presetManager.refreshLibrary();
                    presetManager.setCurrentPreset(currentPresetFile,
                                                   getCurrentPresetDisplayName());
                    presetManager.setStatusText("Deleted "
                                                + file.getFileNameWithoutExtension());
                }
                else
                {
                    presetManager.setStatusText("Preset delete failed (write-protected?)",
                                                true);
                }
            });
    };
    presetManager.onRevealRequested = [](const juce::File& file)
    {
        // JUCE's revealToUser() opens the platform file manager (Finder /
        // Explorer / Nautilus) with the file selected — works for both
        // user and factory presets.
        if (file.existsAsFile()) file.revealToUser();
    };
    presetManager.onDuplicateRequested = [this](const juce::File& file)
    {
        // Byte-for-byte copy in the same bank, with a "<name> copy"
        // suffix (or " copy 2", " copy 3" if that is taken). The JSON
        // `name` field inside the new file is patched so the embedded
        // metadata stays consistent with the duplicate's filename.
        const auto stem = file.getFileNameWithoutExtension();
        const auto parent = file.getParentDirectory();
        // Build the target name by string concatenation, NOT withFileExtension —
        // withFileExtension() strips at the last `.`, which destroys stems
        // containing a dot ("Bass v1.2" → "Bass v1.t5p") and can land the
        // duplicate on top of an unrelated existing file.
        auto target = parent.getChildFile(stem + " copy.t5p");
        for (int n = 2; target.existsAsFile() && n < 1000; ++n)
            target = parent.getChildFile(stem + " copy " + juce::String(n) + ".t5p");

        // Final belt-and-braces guard: never overwrite, even if the loop
        // bailed out at n=1000 against a wedged collision.
        if (target.existsAsFile())
        {
            presetManager.setStatusText("Duplicate failed: could not allocate a free name", true);
            return;
        }

        if (! file.copyFileTo(target))
        {
            presetManager.setStatusText("Duplicate failed: could not copy file", true);
            return;
        }
        patchPresetNameField(target, target.getFileNameWithoutExtension());
        presetManager.refreshLibrary();
        presetManager.setStatusText("Duplicated to " + target.getFileNameWithoutExtension());
    };
    addChildComponent(presetManager);

    // Manual overlay — hosts the native WebBrowserComponent that renders
    // the shipped HTML guide. Clicking outside the panel or the close
    // button hides the overlay without destroying the web view, so the
    // page stays loaded for subsequent opens.
    manualScrim.onClick = [this] { hideManual(); };
    manualScrim.setVisible(false);
    addChildComponent(manualScrim);

    manualPanel.setVisible(false);
    addChildComponent(manualPanel);
    manualPanel.addAndMakeVisible(manualWeb);
    manualWeb.setVisible(false);

    manualCloseBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    manualCloseBtn.setColour(juce::TextButton::textColourOffId, kAccent);
    manualCloseBtn.onClick = [this] { hideManual(); };
    manualPanel.addAndMakeVisible(manualCloseBtn);

    // Master volume — vertical slider
    masterVolKnob.setSliderStyle(juce::Slider::LinearVertical);
    masterVolKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    masterVolKnob.setColour(juce::Slider::trackColourId, kAccent);
    masterVolKnob.setColour(juce::Slider::backgroundColourId, kSurface);
    masterVolKnob.setColour(juce::Slider::textBoxTextColourId, kDim);
    masterVolKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    masterVolKnob.setTextValueSuffix(" dB");
    addAndMakeVisible(masterVolKnob);

    masterVolLabel.setText("Vol", juce::dontSendNotification);
    masterVolLabel.setColour(juce::Label::textColourId, kDim);
    masterVolLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolLabel);

    masterVolA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), PID::masterVol, masterVolKnob);

    // Main Generate button at bottom of left column. Custom paintButton
    // handles fill/border/text colours directly — no TextButton::ColourIds
    // needed.
    mainGenerateBtn.onClick = [this] { triggerMainGeneration(); };
    addAndMakeVisible(mainGenerateBtn);

    snapLabel.setText("SNAP", juce::dontSendNotification);
    snapLabel.setColour(juce::Label::textColourId, kDim);
    snapLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    snapLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(snapLabel);

    {
        static constexpr const char* labels[kNumSnapshotButtons] = { "OFF", "1", "2", "3", "4" };
        for (int i = 0; i < kNumSnapshotButtons; ++i)
        {
            auto& b = snapshotButtons[i];
            b.setButtonText(labels[i]);
            b.setSnapshotIndex(i);
            b.setColour(juce::TextButton::buttonColourId, kSurface);
            b.setColour(juce::TextButton::buttonOnColourId, kOscCol);
            b.setColour(juce::TextButton::textColourOffId, kDim);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b.setTooltip(i == 0 ? "Disable snapshot recall"
                                : "Snapshot slot " + juce::String(i));
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumSnapshotButtons - 1) edges |= juce::Button::ConnectedOnRight;
            b.setConnectedEdges(edges);
            b.onPressStarted = [this](int slot) { captureSnapshotPress(slot); };
            b.onShortActivate = [this](int slot) { activateSnapshot(slot); };
            b.onLongStore = [this](int slot) { storeSnapshotFromPress(slot); };
            addAndMakeVisible(b);
        }
    }

    cacheLabel.setText("CACHE", juce::dontSendNotification);
    cacheLabel.setColour(juce::Label::textColourId, kDim);
    cacheLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    cacheLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(cacheLabel);

    {
        static constexpr const char* labels[kNumInfCacheButtons] = {
            "OFF", "2", "4", "8", "16", "32", "64"
        };
        static constexpr int values[kNumInfCacheButtons] = {
            0, 2, 4, 8, 16, 32, 64
        };
        for (int i = 0; i < kNumInfCacheButtons; ++i)
        {
            auto& b = infCacheButtons[i];
            b.setButtonText(labels[i]);
            b.setColour(juce::TextButton::buttonColourId, kSurface);
            b.setColour(juce::TextButton::buttonOnColourId, kOscCol);
            b.setColour(juce::TextButton::textColourOffId, kDim);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b.setClickingTogglesState(true);
            b.setRadioGroupId(3017);
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumInfCacheButtons - 1) edges |= juce::Button::ConnectedOnRight;
            b.setConnectedEdges(edges);
            b.onClick = [this, value = values[i]]
            {
                processorRef.setInferenceCacheCapacity(value);
                syncInferenceCacheUi();
            };
            addAndMakeVisible(b);
        }
    }
    syncSnapshotUi();
    syncInferenceCacheUi();

    // Wire axis values callback for drift auto-regen (offsets applied per slot)
    promptPanel.getAxisValuesCallback = [this](float o1, float o2, float o3) {
        return axesPanel.getAxisValuesWithOffsets(o1, o2, o3);
    };



    // Status callback — drive Generate animation and status bar text.
    startTimerHz(20);  // lightweight glow animation + UI polling

    promptPanel.onStatusChanged = [this](const juce::String& text, bool isGenerating) {
        const bool cacheHit = (text == "From cache");

        if (cacheHit)
        {
            // Cache-hit: keep the pulse running and keep the action labelled
            // as cache playback while the cache remains full. Button stays
            // enabled so the user can fire the next cache entry immediately.
            cacheHitActive = true;
            cacheHitUntilSec = juce::Time::getMillisecondCounterHiRes() * 0.001 + 1.5;
            glowGenerating = true;
            updateGenerateButtonsForCacheState(true);
            mainGenerateBtn.setAnimationState(glowPhase, glowGenerating);
        }
        else
        {
            // Real generation status — drop any pending cache-hit display.
            if (isGenerating && activeSnapshotIndex != 0)
            {
                activeSnapshotIndex = 0;
                syncSnapshotUi();
            }
            cacheHitActive = false;
            glowGenerating = isGenerating;
            mainGenerateBtn.setAnimationState(glowPhase, glowGenerating);

            if (isGenerating && !processorRef.isInferenceCacheFull())
            {
                mainGenerateBtn.setButtonText("GENERATE");
                mainGenerateBtn.setEnabled(false);
                dimApplyBtn.setButtonText("generating...");
                dimApplyBtn.setEnabled(false);
            }
            else
            {
                updateGenerateButtonsForCacheState(false);
            }
        }

        statusBar.setStatusText(text);
        syncInferenceCacheUi();
    };

    // Scrim (click outside DimExplorer overlay to close)
    dimScrim.onClick = [this] { hideDimExplorer(); };
    dimScrim.setVisible(false);
    addChildComponent(dimScrim);

    // DimExplorer — always visible (mini-view in left column, overlay on click)
    addAndMakeVisible(dimensionExplorer);
    dimensionExplorer.onClicked = [this] {
        if (!dimExplorerVisible) showDimExplorer();
    };

    // Wire PromptPanel → DimensionExplorer (embedding stats after generation)
    promptPanel.onEmbeddingsReady = [this](const std::vector<float>& a,
                                           const std::vector<float>& b,
                                           const std::vector<float>& baseline) {
        dimensionExplorer.setEmbeddings(a, b, baseline);
    };

    // "Anwenden + generieren" — green, triggers generation with offsets
    dimApplyBtn.setColour(juce::TextButton::buttonColourId, kOscCol);
    dimApplyBtn.setColour(juce::TextButton::textColourOffId, kBg);
    dimApplyBtn.onClick = [this] {
        activeSnapshotIndex = 0;
        syncSnapshotUi();
        promptPanel.setSemanticAxes(axesPanel.getAxisValues());
        auto offsets = dimensionExplorer.getDimensionOffsets();
        promptPanel.triggerGenerationWithOffsets(std::move(offsets));
    };
    dimApplyBtn.setVisible(false);
    addChildComponent(dimApplyBtn);

    dimUndoBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    dimUndoBtn.setColour(juce::TextButton::textColourOffId, kDim);
    dimUndoBtn.onClick = [this] { dimensionExplorer.undo(); };
    dimUndoBtn.setVisible(false);
    addChildComponent(dimUndoBtn);

    dimRedoBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    dimRedoBtn.setColour(juce::TextButton::textColourOffId, kDim);
    dimRedoBtn.onClick = [this] { dimensionExplorer.redo(); };
    dimRedoBtn.setVisible(false);
    addChildComponent(dimRedoBtn);

    // "Alle zurücksetzen" — orange
    dimResetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4e2700));
    dimResetBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff9800));
    dimResetBtn.onClick = [this] { dimensionExplorer.resetOffsets(); };
    dimResetBtn.setVisible(false);
    addChildComponent(dimResetBtn);

    setOscEasyMode(loadOscEasyModeSetting(), false);

    // Ensure bundled presets exist in user presets directory
    ensureBundledPresetsExist();

    // Load default preset (if no audio loaded yet)
    loadDefaultPreset();

    // Normalize any previously discovered external model directories into
    // T5ynth's canonical model slots before the backend scans them.
    settingsPage.importDiscoveredModels();

    // Load native inference only after Settings has confirmed at least one
    // model slot contains model files the backend can scan.
    if (settingsPage.hasAnyInstalledModel())
    {
        tryLoadInferenceModels();
    }
    else
    {
        statusBar.setConnected(false);
        statusBar.setStatusText("Model setup required");
        showSettings();
    }
}

void MainPanel::showDimExplorer()
{
    dimExplorerVisible = true;
    dimensionExplorer.setOverlayMode(true);
    dimScrim.setVisible(true);
    dimScrim.toFront(false);
    dimApplyBtn.setVisible(true);
    dimUndoBtn.setVisible(true);
    dimRedoBtn.setVisible(true);
    dimResetBtn.setVisible(true);
    dimensionExplorer.toFront(false);
    dimApplyBtn.toFront(false);
    dimUndoBtn.toFront(false);
    dimRedoBtn.toFront(false);
    dimResetBtn.toFront(false);
    resized();
    repaint();
}

void MainPanel::hideDimExplorer()
{
    dimExplorerVisible = false;
    dimensionExplorer.setOverlayMode(false);
    dimScrim.setVisible(false);
    dimApplyBtn.setVisible(false);
    dimUndoBtn.setVisible(false);
    dimRedoBtn.setVisible(false);
    dimResetBtn.setVisible(false);
    resized();  // repositions DimExplorer back to mini-view
    repaint();
}

bool MainPanel::loadOscEasyModeSetting() const
{
    auto file = getUiSettingsFile();
    if (!file.existsAsFile())
        return true;

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (auto* obj = parsed.getDynamicObject())
    {
        juce::ignoreUnused(obj);
        return static_cast<bool>(parsed.getProperty(kOscEasyModeKey, true));
    }

    return true;
}

void MainPanel::saveOscEasyModeSetting() const
{
    auto file = getUiSettingsFile();
    file.getParentDirectory().createDirectory();

    juce::var parsed;
    juce::DynamicObject::Ptr root;
    if (file.existsAsFile())
    {
        parsed = juce::JSON::parse(file.loadFileAsString());
        if (auto* obj = parsed.getDynamicObject())
            root = obj;
    }

    if (root == nullptr)
        root = juce::DynamicObject::Ptr(new juce::DynamicObject());

    root->setProperty(kOscEasyModeKey, oscEasyMode);
    file.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
}

void MainPanel::setOscEasyMode(bool easy, bool persist)
{
    oscEasyMode = easy;
    if (oscEasyMode && dimExplorerVisible)
        hideDimExplorer();

    promptPanel.setEasyMode(oscEasyMode);
    dimHeader.setVisible(!oscEasyMode);
    dimensionExplorer.setVisible(!oscEasyMode);
    oscModeToggle.setButtonText(oscEasyMode ? "> adv." : "> easy");
    updateOscModeToggleVisual();

    if (persist)
        saveOscEasyModeSetting();

    resized();
    repaint();
}

bool MainPanel::hasOscHiddenActiveState() const
{
    if (!oscEasyMode)
        return false;

    return promptPanel.hasHiddenActiveState() || dimensionExplorer.hasOffsets();
}

void MainPanel::updateOscModeToggleVisual()
{
    const bool pulse = hasOscHiddenActiveState();
    auto fill = kSurface.darker(0.45f);
    auto text = kOscCol;

    if (oscEasyMode && pulse)
    {
        const float p = 0.5f + 0.5f * std::sin(oscModePulsePhase);
        fill = fill.interpolatedWith(kOscCol, 0.10f + 0.18f * p);
        text = kTextPrimary;
    }

    oscModeToggle.setColour(juce::TextButton::buttonColourId, fill);
    oscModeToggle.setColour(juce::TextButton::buttonOnColourId, fill);
    oscModeToggle.setColour(juce::TextButton::textColourOffId, text);
    oscModeToggle.setColour(juce::TextButton::textColourOnId, text);
    oscModeToggle.repaint();
}

void MainPanel::showPresetManager()
{
    presetManagerVisible = true;
    presetManager.refreshLibrary();
    presetManager.setCurrentPreset(currentPresetFile, getCurrentPresetDisplayName());
    presetScrim.setVisible(true);
    presetManager.setVisible(true);
    presetScrim.toFront(false);
    presetManager.toFront(false);
    resized();
    repaint();
}

void MainPanel::hidePresetManager()
{
    presetManagerVisible = false;
    // Defensive: if the panel was closed while still in Save mode (× icon,
    // scrim click) restore Browse so the next open is in a clean state.
    presetManager.leaveSaveMode();
    presetScrim.setVisible(false);
    presetManager.setVisible(false);
    repaint();
}

void MainPanel::enterLibrarySaveMode(SaveNameMode mode)
{
    auto defaultName = getCurrentPresetDisplayName();
    if (defaultName.isEmpty()) defaultName = "New Preset";
    if (mode == SaveNameMode::appendCopy && getCurrentPresetDisplayName().isNotEmpty())
        defaultName = defaultName + " copy";

    juce::StringArray existingBanks;
    std::set<juce::String> existingPathKeys;
    auto userDir = PresetFormat::getUserPresetsDirectory();
    if (userDir.isDirectory())
    {
        for (auto& f : userDir.findChildFiles(juce::File::findFiles, true, "*.t5p"))
        {
            const auto rel = f.getRelativePathFrom(userDir).replace("\\", "/");
            existingPathKeys.insert(rel.toLowerCase());
        }
        for (auto& d : userDir.findChildFiles(juce::File::findDirectories, false, "*"))
            existingBanks.add(d.getFileName());
    }
    existingBanks.removeEmptyStrings();
    existingBanks.removeDuplicates(true);
    existingBanks.sortNatural();

    juce::String currentBank;
    if (currentPresetFile.existsAsFile())
    {
        const auto parent = currentPresetFile.getParentDirectory();
        if (parent != userDir && parent.isAChildOf(userDir))
            currentBank = parent.getFileName();
    }

    PresetManagerPanel::SavePrefill prefill;
    prefill.defaultName      = defaultName;
    prefill.suggestedTags    = suggestTagsForCurrent();
    prefill.currentBank      = currentBank;
    prefill.existingBanks    = existingBanks;
    prefill.existingPathKeys = std::move(existingPathKeys);
    prefill.promptA          = promptPanel.getPromptA();
    prefill.promptB          = promptPanel.getPromptB();
    prefill.canIncludeInferenceCache = processorRef.getInferenceCacheCapacity() > 0;

    showPresetManager();
    presetManager.enterSaveMode(std::move(prefill));
}

namespace
{
/** Write a Serum-format wavetable .wav: standard RIFF/WAVE container with
 *  a `clm ` metadata chunk indicating frame size. Recognised by Serum,
 *  Vital, Bitwig Polysynth and most modern wavetable importers — they
 *  use the `<!>FRAME_SIZE …` text in the chunk to slice the data into
 *  single-cycle frames.
 *
 *  Layout: 32-bit IEEE-float PCM, mono, frames concatenated frame-major. */
bool writeWavetableWav(const juce::File& file,
                       const std::vector<float>& samples,
                       int frameSize,
                       int numFrames,
                       double sampleRate)
{
    if (frameSize <= 0 || numFrames <= 0) return false;
    if ((int) samples.size() != frameSize * numFrames) return false;
    if (sampleRate <= 0.0) sampleRate = 44100.0;

    auto out = file.createOutputStream();
    if (out == nullptr) return false;
    out->setPosition(0);
    out->truncate();

    const uint16_t numChannels   = 1;
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign    = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
    const auto sr32              = static_cast<uint32_t>(sampleRate);
    const uint32_t avgBytesPerSec = sr32 * blockAlign;
    const uint32_t pcmBytes       = static_cast<uint32_t>(samples.size() * sizeof(float));

    // <!> + frame size + version-id + name. Serum reads the first integer
    // after "<!>" as the per-frame sample count.
    const juce::String clmText = "<!>" + juce::String(frameSize) + " 0 t5ynth";
    const auto clmUtf8         = clmText.toRawUTF8();
    const auto clmLen          = static_cast<uint32_t>(std::strlen(clmUtf8));
    const auto clmPad          = static_cast<uint32_t>(clmLen % 2);  // RIFF chunks 2-byte aligned

    const uint32_t fmtChunkBytes  = 16;
    const uint32_t totalRiffSize  = 4                                // "WAVE"
                                  + (8 + fmtChunkBytes)              // fmt
                                  + (8 + clmLen + clmPad)            // clm
                                  + (8 + pcmBytes);                  // data

    // JUCE's writeInt / writeShort are little-endian by default — the
    // explicit BigEndian variants exist but the LE ones are the
    // unsuffixed defaults. RIFF / WAVE is little-endian.
    auto writeFourCC = [&out](const char* fcc) { out->write(fcc, 4); };
    auto writeU32LE  = [&out](uint32_t v) { out->writeInt(static_cast<int>(v)); };
    auto writeU16LE  = [&out](uint16_t v) { out->writeShort(static_cast<int16_t>(v)); };

    writeFourCC("RIFF");
    writeU32LE(totalRiffSize);
    writeFourCC("WAVE");

    writeFourCC("fmt ");
    writeU32LE(fmtChunkBytes);
    writeU16LE(3);                  // wFormatTag = 3 → IEEE float
    writeU16LE(numChannels);
    writeU32LE(sr32);
    writeU32LE(avgBytesPerSec);
    writeU16LE(blockAlign);
    writeU16LE(bitsPerSample);

    // RIFF: chunk size reports the unpadded payload length; the optional
    // pad byte exists only to 2-byte-align the *next* chunk and is NOT
    // included in this size field. (Strict importers like Serum mis-frame
    // when the size is overstated by 1.)
    writeFourCC("clm ");
    writeU32LE(clmLen);
    out->write(clmUtf8, clmLen);
    if (clmPad) out->writeByte(0);

    writeFourCC("data");
    writeU32LE(pcmBytes);
    out->write(samples.data(), pcmBytes);
    return true;
}

/** Generic in-place patcher for the JSON header of a .t5p — caller mutates
 *  the parsed DynamicObject; PCM tail is preserved byte-for-byte. */
template <typename Mutator>
bool patchPresetJson(const juce::File& file, Mutator mutate)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData(data)) return false;
    const auto* bytes = static_cast<const uint8_t*>(data.getData());
    const auto size = data.getSize();
    if (size < 12 || std::memcmp(bytes, "T5YN", 4) != 0) return false;

    const uint32_t version    = *reinterpret_cast<const uint32_t*>(bytes + 4);
    const uint32_t oldJsonLen = *reinterpret_cast<const uint32_t*>(bytes + 8);
    if (12 + (size_t) oldJsonLen > size) return false;

    const juce::String oldJson(reinterpret_cast<const char*>(bytes + 12),
                               static_cast<size_t>(oldJsonLen));
    auto parsed = juce::JSON::parse(oldJson);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr) return false;

    mutate(*root);

    const juce::String newJson = juce::JSON::toString(parsed, true);
    const uint32_t newJsonLen = static_cast<uint32_t>(newJson.getNumBytesAsUTF8());

    juce::TemporaryFile tmp(file);
    juce::FileOutputStream out(tmp.getFile());
    if (out.failedToOpen()) return false;

    out.write("T5YN", 4);
    out.writeInt(static_cast<int>(version));
    out.writeInt(static_cast<int>(newJsonLen));
    out.write(newJson.toRawUTF8(), static_cast<size_t>(newJsonLen));

    const size_t audioOffset = 12 + (size_t) oldJsonLen;
    if (audioOffset < size)
        out.write(bytes + audioOffset, size - audioOffset);

    out.flush();
    if (! out.getStatus().wasOk()) return false;
    return tmp.overwriteTargetFileWithTemporary();
}
}  // namespace

bool MainPanel::patchPresetNameField(const juce::File& file, const juce::String& newName)
{
    return patchPresetJson(file, [&](juce::DynamicObject& root)
    {
        root.setProperty("name", newName);
    });
}

bool MainPanel::patchPresetTagsField(const juce::File& file, const juce::StringArray& newTags)
{
    return patchPresetJson(file, [&](juce::DynamicObject& root)
    {
        juce::Array<juce::var> arr;
        for (auto& t : newTags) arr.add(t);
        root.setProperty("tags", arr);
    });
}

juce::StringArray MainPanel::suggestTagsForCurrent()
{
    const auto& audio = processorRef.getGeneratedAudio();
    const double sr = processorRef.getGeneratedSampleRate();
    std::optional<SamplePlayer::NormalizeAnalysis> analysis;
    if (audio.getNumSamples() > 0 && sr > 0.0)
        analysis = processorRef.getSampler().analyzeNormalizeRegion(
            audio, 0, audio.getNumSamples(), sr);
    return PresetTagSuggester::suggest(processorRef.getSampler(), analysis,
                                       promptPanel.getPromptA(),
                                       promptPanel.getPromptB());
}

juce::String MainPanel::getCurrentPresetDisplayName() const
{
    auto stored = processorRef.getLastPresetName().trim();
    if (stored.isNotEmpty())
        return stored;

    if (currentPresetFile.existsAsFile())
        return currentPresetFile.getFileNameWithoutExtension();

    return {};
}

void MainPanel::syncGuiStateForPresetSave()
{
    processorRef.setLastPrompts(promptPanel.getPromptA(), promptPanel.getPromptB());
    processorRef.setLastSeed(promptPanel.getSeed());
    processorRef.setLastInjection(promptPanel.getInjectionMode(),
                                   promptPanel.getLateMixAmount(),
                                   promptPanel.getSplitStart(),
                                   promptPanel.getSplitEnd());

    auto axStates = axesPanel.getSlotStates();
    std::array<T5ynthProcessor::AxisSlotState, 3> procAxes;
    for (int i = 0; i < 3; ++i)
    {
        procAxes[static_cast<size_t>(i)].dropdownId = axStates[static_cast<size_t>(i)].dropdownId;
        procAxes[static_cast<size_t>(i)].value = axStates[static_cast<size_t>(i)].value;
    }
    processorRef.setLastAxes(procAxes);
}

void MainPanel::applyLoadedPreset(const PresetFormat::LoadResult& result, const juce::File& sourceFile)
{
    activeSnapshotIndex = 0;
    syncSnapshotUi();

    promptPanel.loadPresetData(result.promptA, result.promptB,
                               result.seed, result.randomSeed, result.device, result.model,
                               result.injectionMode,
                               result.lateMixAmount,
                               result.splitStart,
                               result.splitEnd);

    if (result.hasAxes)
    {
        std::array<AxesPanel::SlotState, 3> states;
        for (int i = 0; i < 3; ++i)
        {
            states[static_cast<size_t>(i)].dropdownId = result.axes[static_cast<size_t>(i)].dropdownId;
            states[static_cast<size_t>(i)].value = result.axes[static_cast<size_t>(i)].value;
        }
        axesPanel.setSlotStates(states);
    }

    if (result.hasAudio)
    {
        processorRef.loadGeneratedAudio(result.audio, result.sampleRate);
        processorRef.setLastSeed(result.seed);
        processorRef.setLastPrompts(result.promptA, result.promptB);
    }

    processorRef.setInferenceCacheCapacity(0);
    if (result.inferenceCacheCapacity > 0)
    {
        processorRef.setInferenceCacheCapacity(result.inferenceCacheCapacity);
        for (const auto& entry : result.inferenceCache)
            processorRef.addInferenceCacheEntry(entry.audio, entry.sampleRate);
    }
    syncInferenceCacheUi();

    if (!result.embeddingA.empty())
    {
        processorRef.setLastEmbeddings(result.embeddingA, result.embeddingB);
        auto& apvts = processorRef.getValueTreeState();
        auto baseline = DimensionExplorer::estimateBaselineValues(
            result.embeddingA, result.embeddingB,
            apvts.getRawParameterValue(PID::genAlpha)->load(),
            apvts.getRawParameterValue(PID::genMagnitude)->load());
        dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB, baseline, false);
    }

    processorRef.setLastPresetName(result.presetName);
    processorRef.setLastTags(result.tags);
    statusBar.setPresetName(result.presetName);

    if (sourceFile.existsAsFile()
        && sourceFile.getFileName() != "_buffer.t5p"
        && (sourceFile.isAChildOf(PresetFormat::getUserPresetsDirectory())
            || sourceFile.isAChildOf(PresetFormat::getFactoryPresetsDirectory())
            || sourceFile.getParentDirectory() == PresetFormat::getUserPresetsDirectory()
            || sourceFile.getParentDirectory() == PresetFormat::getFactoryPresetsDirectory()))
    {
        currentPresetFile = sourceFile;
    }
    else
    {
        currentPresetFile = juce::File();
    }

    presetManager.setCurrentPreset(currentPresetFile, result.presetName);
}

bool MainPanel::savePresetToFile(const juce::File& file, bool includeInferenceCache)
{
    syncGuiStateForPresetSave();

    auto target = file.withFileExtension("t5p");
    processorRef.setLastPresetName(target.getFileNameWithoutExtension());

    if (!PresetFormat::saveToFile(target, processorRef, includeInferenceCache))
    {
        statusBar.setStatusText("Preset save failed");
        return false;
    }

    currentPresetFile = target;
    statusBar.setPresetName(target.getFileNameWithoutExtension());
    statusBar.setStatusText("Saved preset: " + target.getFileName());
    presetManager.setCurrentPreset(target, target.getFileNameWithoutExtension());
    return true;
}

bool MainPanel::loadPresetFromFile(const juce::File& file)
{
    auto result = PresetFormat::loadFromFile(file, processorRef);
    if (!result.success)
    {
        statusBar.setStatusText("Preset load failed");
        return false;
    }

    applyLoadedPreset(result, file);
    statusBar.setStatusText("Loaded preset: " + result.presetName);
    return true;
}

void MainPanel::importPresetFile()
{
    // Multi-select file picker — drops the per-file overwrite confirm
    // (silently broken on Linux anyway) in favour of automatic suffixing
    // " (1)", " (2)", … on filename collision. Imported files are NOT
    // auto-loaded; the user double-clicks in the library to load.
    auto presetsDir = PresetFormat::getUserPresetsDirectory();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import Presets", presetsDir, "*.t5p");

    juce::Component::SafePointer<MainPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (! safeThis) return;
            auto* self = safeThis.getComponent();

            const auto results = fc.getResults();
            if (results.isEmpty()) return;

            const auto userDir = PresetFormat::getUserPresetsDirectory();
            int imported = 0, renamed = 0, skippedNonT5p = 0, failed = 0;
            juce::File lastImported;

            for (auto& src : results)
            {
                if (! src.existsAsFile())          { ++failed; continue; }
                if (! src.hasFileExtension("t5p")) { ++skippedNonT5p; continue; }

                // Do not import a file that already lives in the user dir
                if (src.isAChildOf(userDir)) { continue; }

                auto target = userDir.getChildFile(src.getFileName());
                bool didRename = false;
                if (target.existsAsFile())
                {
                    target = target.getNonexistentSibling(true);
                    didRename = true;
                }

                if (src.copyFileTo(target))
                {
                    ++imported;
                    if (didRename) ++renamed;
                    lastImported = target;
                }
                else
                {
                    ++failed;
                }
            }

            self->presetManager.refreshLibrary();
            if (lastImported.existsAsFile())
                self->presetManager.setCurrentPreset(self->currentPresetFile,
                                                    self->getCurrentPresetDisplayName());

            juce::String msg;
            msg << "Imported " << imported << (imported == 1 ? " preset" : " presets");
            if (renamed > 0)        msg << " (" << renamed << " renamed to avoid clash)";
            if (skippedNonT5p > 0)  msg << ", skipped " << skippedNonT5p << " non-.t5p";
            if (failed > 0)         msg << ", " << failed << " failed";
            self->presetManager.setStatusText(msg, failed > 0);
        });
}

void MainPanel::mouseDown(const juce::MouseEvent& e)
{
    // Close overlays on click outside
    if (dimExplorerVisible)
    {
        auto dimBounds = dimensionExplorer.getBounds();
        if (!dimBounds.contains(e.x, e.y))
            hideDimExplorer();
    }
    if (settingsVisible)
    {
        auto settingsBounds = settingsPage.getBounds();
        if (!settingsBounds.contains(e.x, e.y))
            hideSettings();
    }
    if (manualVisible)
    {
        auto mb = manualPanel.getBounds();
        if (!mb.contains(e.x, e.y))
            hideManual();
    }
}

bool MainPanel::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if ((mods.isCommandDown() || mods.isCtrlDown()) && key.getTextCharacter() == 'k')
    {
        setComputerKeyboardEnabled(!computerKeyboardEnabled);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::returnKey
        && (mods.isShiftDown() || mods.isCommandDown() || mods.isCtrlDown()))
    {
        if (settingsVisible || manualVisible || presetManagerVisible)
            return false;
        triggerMainGeneration();
        return true;
    }

    // ⌘S / Ctrl+S opens the Library in Save mode. Skipped when another
    // modal overlay (settings, manual, dim explorer) is up so the
    // shortcut doesn't yank the user out of an unrelated workflow. Also
    // skipped if the Library is already in Save mode (a no-op re-entry
    // would just reset the typed name).
    if ((mods.isCommandDown() || mods.isCtrlDown()) && key.getTextCharacter() == 's')
    {
        if (settingsVisible || manualVisible || dimExplorerVisible) return false;
        if (presetManagerVisible
            && presetManager.getMode() == PresetManagerPanel::Mode::Save)
            return true;   // already there; consume so nothing else fires
        savePreset();
        return true;
    }

    if (!isTextEditingFocus() && !settingsVisible && !manualVisible && !presetManagerVisible)
    {
        const juce_wchar c = key.getTextCharacter();
        if (c >= '1' && c <= '4')
        {
            const int slot = static_cast<int>(c - '0');
            if (mods.isShiftDown())
            {
                snapshotPressCaptures[static_cast<size_t>(slot - 1)] = captureMainSnapshot();
                storeSnapshotFromPress(slot);
            }
            else
            {
                activateSnapshot(slot);
            }
            return true;
        }

        const juce_wchar lower = juce::CharacterFunctions::toLowerCase(c);
        const bool plainKeyboardCommand = computerKeyboardEnabled
                                       && !mods.isCommandDown()
                                       && !mods.isCtrlDown()
                                       && !mods.isAltDown();
        if (plainKeyboardCommand && (lower == 'y' || lower == 'x'))
        {
            auto& keyHeld = (lower == 'y') ? computerKeyboardOctaveDownKeyDown
                                           : computerKeyboardOctaveUpKeyDown;
            if (!keyHeld)
                shiftComputerKeyboardOctave(lower == 'y' ? -1 : 1);
            keyHeld = true;
            return true;
        }

        const int keyIndex = computerKeyboardEnabled ? computerKeyIndexFor(key) : -1;
        if (keyIndex >= 0)
        {
            if (!computerKeyboardNotesDown[static_cast<size_t>(keyIndex)])
            {
                computerKeyboardNotesDown[static_cast<size_t>(keyIndex)] = true;
                const int note = computerKeyboardNoteForIndex(keyIndex);
                computerKeyboardActiveNotes[static_cast<size_t>(keyIndex)] = note;
                processorRef.beginComputerKeyboardNote(note, 0.82f);
            }
            return true;
        }
    }
    return false;
}

void MainPanel::toggleSettings()
{
    if (settingsVisible) hideSettings(); else showSettings();
}

void MainPanel::showSettings()
{
    settingsVisible = true;
    settingsScrim.setVisible(true);
    settingsScrim.toFront(false);
    settingsPage.setVisible(true);
    settingsPage.toFront(false);
    resized();
}

void MainPanel::hideSettings()
{
    settingsVisible = false;
    settingsScrim.setVisible(false);
    settingsPage.setVisible(false);
    resized();
}

void MainPanel::tryLoadInferenceModels(bool forceRestart)
{
    statusBar.setConnected(false);
    statusBar.setStatusText(forceRestart ? "Refreshing model..." : "Loading model...");
    settingsPage.setBackendStarting();

    const auto bundledBackendMode = juce::SystemStats::getEnvironmentVariable("T5YNTH_REQUIRE_BUNDLED_BACKEND", {})
                                        .trim();
    const auto forceBundledBackend = bundledBackendMode.equalsIgnoreCase("1")
                                  || bundledBackendMode.equalsIgnoreCase("true");

    // Find backend directory — accepts either:
    //   backend/pipe_inference.py  (dev: Python script)
    //   backend/pipe_inference     (release: PyInstaller binary)
    //   backend/pipe_inference.exe (Windows release)
    //   backend/dist/pipe_inference/pipe_inference      (local PyInstaller build)
    //   backend/dist/pipe_inference/pipe_inference.exe  (local Windows PyInstaller build)
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File backendDir;

    auto hasBackend = [](const juce::File& dir) {
        return dir.getChildFile("pipe_inference.py").existsAsFile()
            || dir.getChildFile("pipe_inference").existsAsFile()
            || dir.getChildFile("pipe_inference.exe").existsAsFile()
            || dir.getChildFile("dist/pipe_inference/pipe_inference").existsAsFile()
            || dir.getChildFile("dist/pipe_inference/pipe_inference.exe").existsAsFile();
    };

    // 1. Standalone macOS app bundle: Contents/MacOS/T5ynth → Contents/Resources/backend
    //    (This branch hits when running the T5ynth Standalone.app directly.)
    auto resources = exe.getParentDirectory().getSiblingFile("Resources").getChildFile("backend");
    if (hasBackend(resources))
        backendDir = resources;

   #if JUCE_MAC
    // Test mode for validating packaged apps on a dev machine: only accept the
    // backend embedded in the current app bundle, never a repo/companion fallback.
    const bool allowSearchUpwards = !forceBundledBackend;
   #else
    const bool allowSearchUpwards = true;
   #endif

    // 2. Walk up from executable (dev builds, Linux/Windows standalone layout:
    //    T5ynth.exe next to backend/).
    if (!backendDir.exists() && allowSearchUpwards)
    {
        auto search = exe.getParentDirectory();
        for (int i = 0; i < 8; ++i)
        {
            auto candidate = search.getChildFile("backend");
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
            search = search.getParentDirectory();
        }
    }

    // 3. Plugin context (VST3/AU): the exe is the DAW, not T5ynth. Look for a
    //    companion T5ynth Standalone install and borrow its bundled backend.
    //    Release archives ship the heavy backend only with the Standalone —
    //    VST3/AU plugins piggy-back on it so the plugin downloads stay small.
    if (!backendDir.exists() && !forceBundledBackend)
    {
       #if JUCE_MAC
        juce::Array<juce::File> companionApps {
            juce::File("/Applications/T5ynth.app"),
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Applications/T5ynth.app")
        };
        for (const auto& app : companionApps)
        {
            auto candidate = app.getChildFile("Contents/Resources/backend");
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #elif JUCE_WINDOWS
        // Windows: prefer the installer-written registry path, then fall back
        // to the default install prefixes.
        auto companionRoots = getWindowsCompanionBackendRoots();
        for (const auto& p : companionRoots)
        {
            juce::File candidate (p);
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #elif JUCE_LINUX
        juce::StringArray companionRoots {
            "/opt/T5ynth/backend",
            "/usr/local/share/T5ynth/backend"
        };
        for (const auto& p : companionRoots)
        {
            juce::File candidate (p);
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #endif
    }

    // 4. Last-resort dev fallback: compile-time project backend path.
    //    Only active in dev builds where T5YNTH_BACKEND_DIR is defined.
   #ifdef T5YNTH_BACKEND_DIR
    if (!backendDir.exists() && !forceBundledBackend)
    {
        juce::File devBackend (T5YNTH_BACKEND_DIR);
        if (hasBackend(devBackend))
            backendDir = devBackend;
    }
   #endif

    if (backendDir.exists())
    {
        juce::Component::SafePointer<MainPanel> safeThis(this);
        auto pipePtr = processorRef.getPipeInferencePtr();
        std::thread([safeThis, pipePtr, backendDir, forceRestart]()
        {
            if (forceRestart)
                pipePtr->shutdown();

            bool ok = pipePtr->launch(backendDir);
            auto errorMsg = ok ? juce::String() : pipePtr->getLastError();
            juce::MessageManager::callAsync([safeThis, ok, errorMsg]()
            {
                if (auto* self = safeThis.getComponent())
                {
                    if (ok)
                    {
                        self->statusBar.setConnected(true);
                        self->statusBar.setStatusText("Ready");
                        self->settingsPage.setBackendConnected(true);
                        self->promptPanel.refreshInferenceChoices();
                    }
                    else
                    {
                        self->statusBar.setConnected(false);
                        self->statusBar.setStatusText("Backend start failed");
                        self->settingsPage.setBackendFailed(errorMsg);
                    }
                }
            });
        }).detach();
    }
    else
    {
        // Plugin context with no companion install is the most common failure —
        // give the user an actionable hint instead of the generic message.
        const auto msg = forceBundledBackend
                         ? juce::String("Bundled backend not found in app")
                         : (juce::JUCEApplicationBase::isStandaloneApp()
                            ? juce::String("Backend not found — reinstall T5ynth")
                            : juce::String("Backend not found — install the T5ynth app"));
        statusBar.setStatusText(msg);
        settingsPage.setBackendFailed(forceBundledBackend ? "Bundled backend missing" : "Not found");
    }
}

// ── Buffer preset: persist full state on Standalone quit ──
static juce::File getBufferPresetFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("T5ynth")
               .getChildFile("_buffer.t5p");
}

MainPanel::~MainPanel()
{
    releaseComputerKeyboardNotes();
    stopTimer();

    if (!juce::JUCEApplicationBase::isStandaloneApp())
        return;

    syncGuiStateForPresetSave();
    auto bufFile = getBufferPresetFile();
    bufFile.getParentDirectory().createDirectory();
    PresetFormat::saveToFile(bufFile, processorRef);
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    float w = static_cast<float>(getWidth());
    float footerTop = static_cast<float>(sequencerPanel.getY());

    g.setColour(kBorder);
    float x1 = static_cast<float>(promptPanel.getRight() + 4);
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, footerTop);
    g.drawHorizontalLine(juce::roundToInt(footerTop), 0.0f, w);

    int inset = 4;

    // Card 1: OSCILLATOR (oscHeader + promptPanel)
    {
        int top = oscHeader.getY() - inset;
        int bot = promptPanel.getBottom() + inset;
        int left = oscHeader.getX() - inset;
        int cardW = promptPanel.getWidth() + inset * 2;
        paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    // Card 2: AXES (axesHeader + axesPanel)
    {
        int top = axesHeader.getY() - inset;
        int bot = axesPanel.getBottom() + inset;
        int left = axesHeader.getX() - inset;
        int cardW = axesPanel.getWidth() + inset * 2;
        paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    // Card 3: DIM EXPLORER + Generate button
    if (!dimExplorerVisible)
    {
        int top = (oscEasyMode ? axesPanel.getBottom() + inset : dimHeader.getY() - inset);
        int bot = sequencerPanel.getY() - inset;
        int left = promptPanel.getX() - inset;
        int cardW = promptPanel.getWidth() + inset * 2;
        if (bot > top)
            paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    if (!snapshotSwitchBounds.isEmpty())
        paintSwitchBoxBorder(g, snapshotSwitchBounds);
    if (!cacheSwitchBounds.isEmpty())
        paintSwitchBoxBorder(g, cacheSwitchBounds);

    if (glowGenerating)
    {
        const float pulse = 0.5f + 0.5f * std::sin(glowPhase);
        auto gb = mainGenerateBtn.getBounds().toFloat().expanded(7.0f, 5.0f);
        g.setColour(kOscCol.withAlpha(0.10f + 0.10f * pulse));
        g.fillRect(gb);
        g.setColour(kOscCol.withAlpha(0.18f + 0.10f * pulse));
        g.drawRect(gb, 1.0f);
    }
}

void MainPanel::syncInferenceCacheUi()
{
    const int capacity = processorRef.getInferenceCacheCapacity();
    const int fill = processorRef.getInferenceCacheFillCount();
    const bool full = processorRef.isInferenceCacheFull();
    updateGenerateButtonsForCacheState(false);

    if (capacity == lastInfCacheUiCapacity
        && fill == lastInfCacheUiFill
        && full == lastInfCacheUiFull)
        return;

    lastInfCacheUiCapacity = capacity;
    lastInfCacheUiFill = fill;
    lastInfCacheUiFull = full;

    static constexpr int values[kNumInfCacheButtons] = { 0, 2, 4, 8, 16, 32, 64 };

    // Pulse the *selected* button while the cache is filling. Once full,
    // pulsing stops and the button sits at solid kOscCol — that solid state
    // is the "cache full" signal, replacing the dropped status text row.
    const bool isFilling = (capacity > 0) && (fill < capacity);
    for (int i = 0; i < kNumInfCacheButtons; ++i)
    {
        infCacheButtons[i].setToggleState(capacity == values[i], juce::dontSendNotification);
        infCacheButtons[i].setPulsing(isFilling);
    }
}

void MainPanel::updateGenerateButtonsForCacheState(bool pulseCacheHit)
{
    const bool cachePlaybackReady = processorRef.isInferenceCacheFull();
    if (cachePlaybackReady)
    {
        mainGenerateBtn.setButtonText("cache hit");
        mainGenerateBtn.setEnabled(true);
        dimApplyBtn.setButtonText("cache hit");
        dimApplyBtn.setEnabled(true);
        if (pulseCacheHit)
            glowGenerating = true;
        return;
    }

    if (processorRef.getInferenceCacheCapacity() == 0)
    {
        cacheHitActive = false;
        if (!promptPanel.isGenerating())
            glowGenerating = false;
    }

    if (!promptPanel.isGenerating() && !glowGenerating && !cacheHitActive)
    {
        mainGenerateBtn.setButtonText("GENERATE");
        mainGenerateBtn.setEnabled(true);
        dimApplyBtn.setButtonText("Apply + Generate");
        dimApplyBtn.setEnabled(true);
    }
}

MainPanel::MainSnapshot MainPanel::captureMainSnapshot()
{
    MainSnapshot snapshot;

    const auto& rawAudio = processorRef.getGeneratedAudioRaw();
    const auto& processedAudio = processorRef.getGeneratedAudio();
    const auto& sourceAudio = rawAudio.getNumSamples() > 0 ? rawAudio : processedAudio;
    if (sourceAudio.getNumChannels() <= 0 || sourceAudio.getNumSamples() <= 0)
        return snapshot;

    snapshot.audio.makeCopyOf(sourceAudio);
    snapshot.sampleRate = processorRef.getGeneratedSampleRate();
    if (snapshot.sampleRate <= 0.0)
        snapshot.sampleRate = processorRef.getSampleRate() > 0.0 ? processorRef.getSampleRate() : 44100.0;

    snapshot.parameters = processorRef.getValueTreeState().copyState();
    snapshot.promptA = promptPanel.getPromptA();
    snapshot.promptB = promptPanel.getPromptB();
    snapshot.device = processorRef.getLastDevice();
    snapshot.model = processorRef.getLastModel();
    snapshot.injectionMode = promptPanel.getInjectionMode();
    snapshot.seed = promptPanel.getSeed();
    snapshot.randomSeed = promptPanel.isRandomSeed();
    snapshot.lateMixAmount = promptPanel.getLateMixAmount();
    snapshot.splitStart = promptPanel.getSplitStart();
    snapshot.splitEnd = promptPanel.getSplitEnd();
    snapshot.axes = axesPanel.getSlotStates();
    snapshot.embeddingA = processorRef.getLastEmbeddingA();
    snapshot.embeddingB = processorRef.getLastEmbeddingB();
    snapshot.dimensionOffsets = dimensionExplorer.getDimensionOffsets();

    {
        const juce::ScopedLock sl(processorRef.getCallbackLock());
        auto& sampler = processorRef.getSampler();
        snapshot.loopStart = sampler.getLoopStart();
        snapshot.loopEnd = sampler.getLoopEnd();
        snapshot.startPos = sampler.getStartPos();
        snapshot.wtExtractStart = sampler.getWtExtractStart();
        snapshot.wtExtractEnd = sampler.getWtExtractEnd();
        snapshot.pointsLocked = sampler.getPointsLocked();
    }

    snapshot.valid = true;
    return snapshot;
}

void MainPanel::restoreMainSnapshot(const MainSnapshot& snapshot)
{
    if (!snapshot.valid)
        return;

    auto& apvts = processorRef.getValueTreeState();
    for (auto* id : kMainSnapshotParamIds)
        restoreParameterFromState(apvts, snapshot.parameters, id);

    promptPanel.loadPresetData(snapshot.promptA, snapshot.promptB,
                               snapshot.seed, snapshot.randomSeed,
                               snapshot.device, snapshot.model,
                               snapshot.injectionMode,
                               snapshot.lateMixAmount,
                               snapshot.splitStart,
                               snapshot.splitEnd);
    axesPanel.setSlotStates(snapshot.axes);
    std::array<T5ynthProcessor::AxisSlotState, 3> procAxes;
    for (int i = 0; i < 3; ++i)
    {
        procAxes[static_cast<size_t>(i)].dropdownId = snapshot.axes[static_cast<size_t>(i)].dropdownId;
        procAxes[static_cast<size_t>(i)].value = snapshot.axes[static_cast<size_t>(i)].value;
    }

    processorRef.setLastDevice(snapshot.device);
    processorRef.setLastModel(snapshot.model);
    processorRef.setLastSeed(snapshot.seed);
    processorRef.setLastPrompts(snapshot.promptA, snapshot.promptB);
    processorRef.setLastInjection(snapshot.injectionMode,
                                  snapshot.lateMixAmount,
                                  snapshot.splitStart,
                                  snapshot.splitEnd);
    processorRef.setLastAxes(procAxes);

    if (!snapshot.embeddingA.empty())
    {
        processorRef.setLastEmbeddings(snapshot.embeddingA, snapshot.embeddingB);
        const float alpha = apvts.getRawParameterValue(PID::genAlpha)->load();
        const float magnitude = apvts.getRawParameterValue(PID::genMagnitude)->load();
        auto baseline = DimensionExplorer::estimateBaselineValues(snapshot.embeddingA,
                                                                  snapshot.embeddingB,
                                                                  alpha,
                                                                  magnitude);
        dimensionExplorer.setEmbeddings(snapshot.embeddingA, snapshot.embeddingB, baseline, false);
        dimensionExplorer.setDimensionOffsets(snapshot.dimensionOffsets);
    }
    else
    {
        dimensionExplorer.clear();
    }

    auto applyMarkers = [&]()
    {
        const float loopStart = juce::jlimit(0.0f, 0.99f, snapshot.loopStart);
        float loopEnd = juce::jlimit(0.01f, 1.0f, snapshot.loopEnd);
        if (loopEnd < loopStart + 0.01f)
            loopEnd = juce::jmin(1.0f, loopStart + 0.01f);

        auto& sampler = processorRef.getSampler();
        sampler.setPointsLocked(true);
        sampler.setLoopEnd(1.0f);
        sampler.setLoopStart(loopStart);
        sampler.setLoopEnd(loopEnd);
        sampler.setStartPos(juce::jlimit(0.0f, 1.0f, snapshot.startPos));
        sampler.setWtExtractStart(juce::jlimit(0.0f, 1.0f, snapshot.wtExtractStart));
        sampler.setWtExtractEnd(juce::jlimit(0.0f, 1.0f, snapshot.wtExtractEnd));
    };

    {
        const juce::ScopedLock sl(processorRef.getCallbackLock());
        applyMarkers();
    }
    processorRef.loadGeneratedAudio(snapshot.audio, snapshot.sampleRate);
    {
        const juce::ScopedLock sl(processorRef.getCallbackLock());
        applyMarkers();
        processorRef.getSampler().setPointsLocked(snapshot.pointsLocked);
    }
}

void MainPanel::captureSnapshotPress(int slot)
{
    if (slot < 1 || slot > kNumSnapshotSlots)
        return;
    snapshotPressCaptures[static_cast<size_t>(slot - 1)] = captureMainSnapshot();
}

void MainPanel::storeSnapshotFromPress(int slot)
{
    if (slot < 1 || slot > kNumSnapshotSlots)
        return;

    auto& pending = snapshotPressCaptures[static_cast<size_t>(slot - 1)];
    if (!pending.valid)
    {
        statusBar.setStatusText("No audio to snapshot");
        return;
    }

    mainSnapshots[static_cast<size_t>(slot - 1)] = std::move(pending);
    pending = {};
    activeSnapshotIndex = slot;
    syncSnapshotUi();
    snapshotButtons[slot].flashStored();
    statusBar.setStatusText("Snapshot " + juce::String(slot) + " saved");
}

void MainPanel::activateSnapshot(int slot)
{
    if (slot <= 0)
    {
        activeSnapshotIndex = 0;
        syncSnapshotUi();
        return;
    }

    if (slot > kNumSnapshotSlots || !mainSnapshots[static_cast<size_t>(slot - 1)].valid)
    {
        statusBar.setStatusText("Snapshot " + juce::String(slot) + " empty");
        syncSnapshotUi();
        return;
    }

    restoreMainSnapshot(mainSnapshots[static_cast<size_t>(slot - 1)]);
    activeSnapshotIndex = slot;
    syncSnapshotUi();
    statusBar.setStatusText("Snapshot " + juce::String(slot) + " recalled");
}

void MainPanel::syncSnapshotUi()
{
    for (int i = 0; i < kNumSnapshotButtons; ++i)
    {
        snapshotButtons[i].setToggleState(activeSnapshotIndex == i, juce::dontSendNotification);
        snapshotButtons[i].setSnapshotFilled(i > 0 && mainSnapshots[static_cast<size_t>(i - 1)].valid);
    }
}

void MainPanel::triggerMainGeneration()
{
    if (!mainGenerateBtn.isEnabled())
        return;

    activeSnapshotIndex = 0;
    syncSnapshotUi();
    promptPanel.setSemanticAxes(axesPanel.getAxisValues());
    promptPanel.triggerGenerationWithOffsets({});
}

void MainPanel::setComputerKeyboardEnabled(bool enabled)
{
    if (computerKeyboardEnabled == enabled)
    {
        statusBar.setKeyboardInputEnabled(enabled);
        return;
    }

    computerKeyboardEnabled = enabled;
    statusBar.setKeyboardInputEnabled(enabled);
    if (!enabled)
    {
        releaseComputerKeyboardNotes();
        computerKeyboardOctaveDownKeyDown = false;
        computerKeyboardOctaveUpKeyDown = false;
    }
    statusBar.setStatusText(enabled ? computerKeyboardStatusText()
                                    : "Computer keyboard off");
}

void MainPanel::shiftComputerKeyboardOctave(int delta)
{
    const int nextOffset = juce::jlimit(kComputerKeyboardMinOctaveOffset,
                                       kComputerKeyboardMaxOctaveOffset,
                                       computerKeyboardOctaveOffset + delta);
    if (nextOffset == computerKeyboardOctaveOffset)
    {
        statusBar.setStatusText("Kbd octave limit: " + computerKeyboardBaseNoteName());
        return;
    }

    releaseComputerKeyboardNotes();
    computerKeyboardOctaveOffset = nextOffset;
    statusBar.setStatusText("Kbd octave: " + computerKeyboardBaseNoteName());
}

bool MainPanel::isTextEditingFocus() const
{
    for (auto* c = juce::Component::getCurrentlyFocusedComponent(); c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::TextEditor*>(c) != nullptr)
            return true;
    return false;
}

int MainPanel::computerKeyIndexFor(const juce::KeyPress& key) const
{
    const juce_wchar c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    for (int i = 0; i < static_cast<int>(sizeof(kComputerKeyboardNoteKeys) / sizeof(kComputerKeyboardNoteKeys[0])); ++i)
        if (c == kComputerKeyboardNoteKeys[i])
            return i;
    return -1;
}

int MainPanel::computerKeyboardNoteForIndex(int keyIndex) const
{
    return kComputerKeyboardBaseMidiNote + computerKeyboardOctaveOffset * 12 + keyIndex;
}

juce::String MainPanel::computerKeyboardBaseNoteName() const
{
    return juce::MidiMessage::getMidiNoteName(computerKeyboardNoteForIndex(0), true, true, 4);
}

juce::String MainPanel::computerKeyboardStatusText() const
{
    return "Kbd on: y/x oct, awsedftgzhujk from " + computerKeyboardBaseNoteName();
}

void MainPanel::pollComputerKeyboard()
{
    if (!computerKeyboardEnabled || isTextEditingFocus()
        || settingsVisible || manualVisible || presetManagerVisible)
    {
        releaseComputerKeyboardNotes();
        computerKeyboardOctaveDownKeyDown = false;
        computerKeyboardOctaveUpKeyDown = false;
        return;
    }

    const auto isCharDown = [] (char keyChar)
    {
        const int lower = static_cast<int>(keyChar);
        const int upper = static_cast<int>(juce::CharacterFunctions::toUpperCase(keyChar));
        return juce::KeyPress::isKeyCurrentlyDown(lower)
            || juce::KeyPress::isKeyCurrentlyDown(upper);
    };

    const bool octaveDown = isCharDown('y');
    if (octaveDown && !computerKeyboardOctaveDownKeyDown)
        shiftComputerKeyboardOctave(-1);
    computerKeyboardOctaveDownKeyDown = octaveDown;

    const bool octaveUp = isCharDown('x');
    if (octaveUp && !computerKeyboardOctaveUpKeyDown)
        shiftComputerKeyboardOctave(1);
    computerKeyboardOctaveUpKeyDown = octaveUp;

    for (int i = 0; i < static_cast<int>(sizeof(kComputerKeyboardNoteKeys) / sizeof(kComputerKeyboardNoteKeys[0])); ++i)
    {
        const int lower = static_cast<int>(kComputerKeyboardNoteKeys[i]);
        const int upper = static_cast<int>(juce::CharacterFunctions::toUpperCase(kComputerKeyboardNoteKeys[i]));
        const bool down = juce::KeyPress::isKeyCurrentlyDown(lower)
                       || juce::KeyPress::isKeyCurrentlyDown(upper);

        if (down == computerKeyboardNotesDown[static_cast<size_t>(i)])
            continue;

        computerKeyboardNotesDown[static_cast<size_t>(i)] = down;
        if (down)
        {
            const int note = computerKeyboardNoteForIndex(i);
            computerKeyboardActiveNotes[static_cast<size_t>(i)] = note;
            processorRef.beginComputerKeyboardNote(note, 0.82f);
        }
        else
        {
            const int note = computerKeyboardActiveNotes[static_cast<size_t>(i)];
            if (note >= 0)
                processorRef.endComputerKeyboardNote(note);
            computerKeyboardActiveNotes[static_cast<size_t>(i)] = -1;
        }
    }
}

void MainPanel::releaseComputerKeyboardNotes()
{
    for (int i = 0; i < static_cast<int>(computerKeyboardNotesDown.size()); ++i)
    {
        if (!computerKeyboardNotesDown[static_cast<size_t>(i)])
            continue;
        computerKeyboardNotesDown[static_cast<size_t>(i)] = false;
        const int note = computerKeyboardActiveNotes[static_cast<size_t>(i)];
        processorRef.endComputerKeyboardNote(note >= 0 ? note : computerKeyboardNoteForIndex(i));
        computerKeyboardActiveNotes[static_cast<size_t>(i)] = -1;
    }
}

void MainPanel::timerCallback()
{
    // Stop the temporary pulse after a cache hit, but keep the cache-hit
    // label as long as the cache is still full.
    if (cacheHitActive)
    {
        const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (nowSec >= cacheHitUntilSec)
        {
            cacheHitActive = false;
            if (! promptPanel.isGenerating())
                glowGenerating = false;
        }
    }

    const bool isAuto = processorRef.driftRegenMode.load() != 0;
    const bool isHover = mainGenerateBtn.isMouseOver(false);
    float speedHz;
    if (glowGenerating)               speedHz = 1.00f;
    else if (isAuto && isHover)       speedHz = 1.00f;
    else if (isAuto)                  speedHz = 0.75f;
    else if (isHover)                 speedHz = 0.50f;
    else                              speedHz = 0.05f;   // manual idle (slow drift)

    const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (glowLastTimeSec <= 0.0)
        glowLastTimeSec = nowSec;
    const float dt = static_cast<float>(juce::jmin(0.5, nowSec - glowLastTimeSec));
    glowLastTimeSec = nowSec;

    glowPhase += speedHz * juce::MathConstants<float>::twoPi * dt;
    while (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;

    oscModePulsePhase += 0.33f * juce::MathConstants<float>::twoPi * dt;
    while (oscModePulsePhase > juce::MathConstants<float>::twoPi)
        oscModePulsePhase -= juce::MathConstants<float>::twoPi;
    updateOscModeToggleVisual();

    mainGenerateBtn.setAnimationState(glowPhase, glowGenerating);
    updateGenerateButtonsForCacheState(false);
    syncInferenceCacheUi();
    pollComputerKeyboard();

    // Drive the cache-button pulse phase while the cache is still filling.
    // Uses an independent 2 Hz counter (not glowPhase, which crawls at idle)
    // so the selected button visibly blinks. Only the toggled button repaints.
    if (lastInfCacheUiCapacity > 0 && lastInfCacheUiFill < lastInfCacheUiCapacity)
    {
        cachePulsePhase += 2.0f * juce::MathConstants<float>::twoPi * dt;
        while (cachePulsePhase > juce::MathConstants<float>::twoPi)
            cachePulsePhase -= juce::MathConstants<float>::twoPi;
        for (auto& b : infCacheButtons)
            b.setPulsePhase(cachePulsePhase);
    }

    // Poll drift ghost offsets for AxesPanel (30Hz)
    auto& mv = processorRef.modulatedValues;
    axesPanel.setGhostOffsets(
        mv.driftAxis1.load(std::memory_order_relaxed),
        mv.driftAxis2.load(std::memory_order_relaxed),
        mv.driftAxis3.load(std::memory_order_relaxed));

    if (pendingInferenceReload && !promptPanel.isGenerating())
    {
        pendingInferenceReload = false;
        tryLoadInferenceModels(true);
    }
}

void MainPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());

    int statusH = 20;
    int footerH = juce::jlimit(160, 280, juce::roundToInt(h * 0.24f));
    statusBar.setBounds(b.removeFromBottom(statusH));

    // Gap between footer and main content
    b.removeFromBottom(6);

    // Footer
    auto footer = b.removeFromBottom(footerH);
    int volW = juce::jlimit(40, 60, juce::roundToInt(w * 0.05f));
    auto volArea = footer.removeFromRight(volW);
    masterVolLabel.setFont(juce::FontOptions(kUiLabelFontMin));
    masterVolLabel.setBounds(volArea.removeFromTop(14));
    masterVolKnob.setBounds(volArea);
    int footerGap = juce::jlimit(4, 8, juce::roundToInt(w * 0.005f));
    footer.removeFromRight(footerGap);  // gap Vol–FX

    const int footerContentW = footer.getWidth() - footerGap;
    const int fxPrefW = juce::jlimit(180, 400, juce::roundToInt(w * 0.28f));
    const int fxMinW = 160;
    const int seqMinW = 360;

    int fxW = juce::jmin(fxPrefW, juce::jmax(fxMinW, footerContentW - seqMinW));
    int seqW = footerContentW - fxW;
    if (seqW < seqMinW)
    {
        fxW = juce::jmax(120, footerContentW - seqMinW);
        seqW = footerContentW - fxW;
    }

    seqW = juce::jmax(280, seqW);
    fxW = juce::jmax(120, footerContentW - seqW);

    fxPanel.setBounds(footer.removeFromRight(fxW));
    footer.removeFromRight(footerGap);  // gap FX–Seq
    sequencerPanel.setBounds(footer.removeFromRight(seqW));

    // ═══ Col 1: Three cards — OSCILLATOR, AXES, DIM EXPLORER ═══
    int col1W = juce::jlimit(240, 420, juce::roundToInt(w * 0.25f));
    auto genCol = b.removeFromLeft(col1W).reduced(6, 2);

    int headerH = juce::jlimit(14, 20, juce::roundToInt(h * 0.022f));
    int kGap = juce::jlimit(3, 6, juce::roundToInt(h * 0.005f));
    constexpr int kMinDimH = 24;
    constexpr int kMaxDimH = 48;
    constexpr int kMinOscH = 220;
    constexpr int kMinAxesH = 64;
    constexpr int kMinGenerateButtonH = 38;
    const int cacheRowH = juce::jlimit(16, 20, juce::roundToInt(h * 0.022f));
    const int genCacheGap = juce::jlimit(14, 28, juce::roundToInt(h * 0.024f));

    int genBtnH = juce::jlimit(50, 72,
                               juce::roundToInt(juce::jmax(static_cast<float>(genCol.getWidth()) * 0.18f,
                                                           h * 0.060f)));

    int oscH = juce::jmax(kMinOscH, promptPanel.getPreferredHeightForWidth(genCol.getWidth()));
    int axesH = juce::jlimit(kMinAxesH, 108, juce::roundToInt(h * 0.10f));
    const int reservedGenerateBlockH = kMinGenerateButtonH + genCacheGap + cacheRowH;
    const int headerCount = oscEasyMode ? 2 : 3;
    const int minDimBudget = oscEasyMode ? 0 : kMinDimH;
    int dimBudget = genCol.getHeight() - (headerH * headerCount + kGap * headerCount
                                          + reservedGenerateBlockH + oscH + axesH);
    if (dimBudget < minDimBudget)
    {
        int shortage = minDimBudget - dimBudget;
        int trimAxes = juce::jmin(shortage, juce::jmax(0, axesH - kMinAxesH));
        axesH -= trimAxes;
        shortage -= trimAxes;

        if (shortage > 0)
            oscH = juce::jmax(kMinOscH, oscH - shortage);
    }

    // Card 1: OSCILLATOR
    oscHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    auto oscHeaderBounds = genCol.removeFromTop(headerH);
    oscHeader.setBounds(oscHeaderBounds);
    const float oscHeaderFs = static_cast<float>(headerH) * 0.85f;
    const int toggleW = juce::jlimit(58, 78,
        measureTextWidth(oscModeToggle.getButtonText(), juce::jmax(kUiControlFontMin, oscHeaderFs * 0.72f)) + 16);
    oscModeToggle.setBounds(oscHeaderBounds.removeFromRight(toggleW).reduced(2, 2));
    const int titleW = measureTextWidth(" T5 OSCILLATOR", oscHeaderFs) + 8;
    auto poweredBounds = oscHeader.getBounds();
    poweredBounds.removeFromLeft(juce::jmin(titleW, poweredBounds.getWidth()));
    poweredBounds.removeFromRight(toggleW + 4);
    poweredByLabel.setFont(juce::FontOptions(juce::jmax(kUiLabelFontMin,
                                                        static_cast<float>(headerH) * 0.6f)));
    poweredByLabel.setBounds(poweredBounds);
    promptPanel.setBounds(genCol.removeFromTop(oscH));
    genCol.removeFromTop(kGap);

    // Card 2: SEMANTIC AXES
    axesHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    axesHeader.setBounds(genCol.removeFromTop(headerH));
    axesPanel.setBounds(genCol.removeFromTop(axesH));
    genCol.removeFromTop(kGap);

    if (oscEasyMode)
    {
        dimHeader.setBounds({});
        dimensionExplorer.setBounds({});
    }
    else
    {
        // Card 3: DIM EXPLORER. This mini view is residual context; it must give
        // up space before the Generate/cache controls can collide with Sequencer.
        dimHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
        dimHeader.setBounds(genCol.removeFromTop(headerH));
        const int availableDimH = genCol.getHeight() - kGap - reservedGenerateBlockH;
        int dimH = juce::jlimit(0, kMaxDimH, availableDimH);
        if (!dimExplorerVisible)
        {
            if (dimH >= kMinDimH)
                dimensionExplorer.setBounds(genCol.removeFromTop(dimH));
            else
                dimensionExplorer.setBounds({});
        }
        else if (dimH > 0)
        {
            genCol.removeFromTop(dimH);
        }
        genCol.removeFromTop(juce::jmin(kGap, genCol.getHeight()));
    }

    // Generate + InfCache controls get all slack freed by the explorer cap,
    // centered in the remaining card area so the controls have breathing room.
    // genCacheGap is intentionally larger than kGap so Generate doesn't read
    // as glued to the cache row — separation here marks Generate as a
    // standalone primary control, not a label for the cache row.
    int remainH = genCol.getHeight();
    int effectiveGenCacheGap = genCacheGap;
    if (remainH < kMinGenerateButtonH + effectiveGenCacheGap + cacheRowH)
        effectiveGenCacheGap = juce::jmin(effectiveGenCacheGap, kGap);

    const int availableGenButtonH = juce::jmax(0, remainH - effectiveGenCacheGap - cacheRowH);
    genBtnH = juce::jlimit(0, genBtnH, availableGenButtonH);
    const int controlsH = genBtnH + effectiveGenCacheGap + cacheRowH;
    int genBtnY = genCol.getY() + juce::jmax(0, (remainH - controlsH) / 2);
    auto genBtnArea = juce::Rectangle<int>(genCol.getX(), genBtnY, genCol.getWidth(), genBtnH);
    int genW = juce::roundToInt(static_cast<float>(genBtnArea.getWidth()) * 0.66f);
    int genX = genBtnArea.getX() + (genBtnArea.getWidth() - genW) / 2;
    mainGenerateBtn.setBounds(genX, genBtnArea.getY(), genW, genBtnArea.getHeight());

    auto snapCacheRow = juce::Rectangle<int>(genCol.getX(),
                                             mainGenerateBtn.getBottom() + effectiveGenCacheGap,
                                             genCol.getWidth(),
                                             cacheRowH).reduced(1, 0);
    const float switchFs = juce::jmax(kUiLabelFontMin, static_cast<float>(cacheRowH) * 0.58f);
    snapLabel.setFont(juce::FontOptions(switchFs));
    cacheLabel.setFont(juce::FontOptions(switchFs));

    const bool veryNarrow = snapCacheRow.getWidth() < 260;
    const int gap = veryNarrow ? 3 : 4;
    const int snapLabelW = veryNarrow ? 28 : juce::jlimit(28, 31, measureTextWidth("SNAP", switchFs) + 4);
    const int cacheLabelW = veryNarrow ? 36 : juce::jlimit(36, 39, measureTextWidth("CACHE", switchFs) + 4);
    const int snapGroupW = veryNarrow ? 94 : juce::jlimit(108, 128,
                                                          juce::roundToInt(static_cast<float>(snapCacheRow.getWidth()) * 0.28f));

    snapLabel.setBounds(snapCacheRow.removeFromLeft(snapLabelW));
    auto snapGroup = snapCacheRow.removeFromLeft(juce::jmin(snapGroupW, snapCacheRow.getWidth()));
    snapCacheRow.removeFromLeft(juce::jmin(gap, snapCacheRow.getWidth()));
    cacheLabel.setBounds(snapCacheRow.removeFromLeft(juce::jmin(cacheLabelW, snapCacheRow.getWidth())));
    snapCacheRow.removeFromLeft(juce::jmin(gap, snapCacheRow.getWidth()));

    auto layoutWeightedButtons = [](auto& buttons, int count, juce::Rectangle<int> area, const float* weights)
    {
        float remainingWeight = 0.0f;
        for (int i = 0; i < count; ++i)
            remainingWeight += weights[i];

        for (int i = 0; i < count; ++i)
        {
            const int cellW = (i == count - 1)
                ? area.getWidth()
                : juce::jmax(1, juce::roundToInt(static_cast<float>(area.getWidth()) * weights[i] / remainingWeight));
            buttons[i].setBounds(area.removeFromLeft(cellW));
            remainingWeight -= weights[i];
        }
    };

    static constexpr float snapshotWeights[kNumSnapshotButtons] = {
        1.65f, 1.00f, 1.00f, 1.00f, 1.00f
    };
    layoutWeightedButtons(snapshotButtons, kNumSnapshotButtons, snapGroup, snapshotWeights);
    snapshotSwitchBounds = snapshotButtons[0].getBounds();
    for (int i = 1; i < kNumSnapshotButtons; ++i)
        snapshotSwitchBounds = snapshotSwitchBounds.getUnion(snapshotButtons[i].getBounds());

    auto cacheGroup = snapCacheRow;
    static constexpr float cacheWeights[kNumInfCacheButtons] = {
        1.35f, 1.00f, 1.00f, 1.00f, 1.12f, 1.12f, 1.12f
    };
    layoutWeightedButtons(infCacheButtons, kNumInfCacheButtons, cacheGroup, cacheWeights);
    cacheSwitchBounds = infCacheButtons[0].getBounds();
    for (int i = 1; i < kNumInfCacheButtons; ++i)
        cacheSwitchBounds = cacheSwitchBounds.getUnion(infCacheButtons[i].getBounds());

    // Col 2: ENGINE
    synthPanel.setBounds(b);



    // Scrims cover everything
    dimScrim.setBounds(getLocalBounds());
    settingsScrim.setBounds(getLocalBounds());
    presetScrim.setBounds(getLocalBounds());
    manualScrim.setBounds(getLocalBounds());

    if (presetManagerVisible)
    {
        int panelW = juce::jlimit(720, 1100, juce::roundToInt(w * 0.78f));
        int panelH = juce::jlimit(440, 720, juce::roundToInt(h * 0.78f));
        presetManager.setBounds((getWidth() - panelW) / 2,
                                (getHeight() - panelH) / 2,
                                panelW,
                                panelH);
    }
    else
    {
        presetManager.setBounds({});
    }

    // Manual overlay (centered). Leaves a strip at the bottom of the
    // panel for the close button; the WebBrowserComponent fills the rest.
    if (manualVisible)
    {
        int manW = juce::jlimit(720, 1100, juce::roundToInt(w * 0.8f));
        int manH = juce::jlimit(480, 820, juce::roundToInt(h * 0.85f));
        int mx = (getWidth() - manW) / 2;
        int my = (getHeight() - manH) / 2;
        manualPanel.setBounds(mx, my, manW, manH);

        auto inner = manualPanel.getLocalBounds().reduced(8);
        auto btnRow = inner.removeFromBottom(30);
        inner.removeFromBottom(6);
        manualWeb.setBounds(inner);
        manualCloseBtn.setBounds(btnRow.removeFromRight(90));
    }
    else
    {
        manualPanel.setBounds({});
        manualWeb.setBounds(-10000, -10000, 1, 1);
    }

    // Settings overlay (bottom-right, above StatusBar)
    if (settingsVisible)
    {
        int settingsW = juce::jlimit(400, 600, juce::roundToInt(w * 0.4f));
        int settingsH = juce::jlimit(300, 500, juce::roundToInt(h * 0.55f));
        int sx = getWidth() - settingsW - 20;
        int sy = getHeight() - statusH - settingsH - 30;
        settingsPage.setBounds(sx, sy, settingsW, settingsH);
    }

    // DimExplorer overlay
    if (dimExplorerVisible)
    {
        auto overlayBounds = getLocalBounds().reduced(40);
        int btnH = 30;
        int applyW = 180;
        int smallW = 70;
        int resetW = 140;
        int btnGap = 10;

        auto btnArea = overlayBounds.removeFromBottom(btnH + 10);
        int totalBtnW = applyW + smallW * 2 + resetW + btnGap * 3;
        int startX = btnArea.getCentreX() - totalBtnW / 2;
        int y = btnArea.getY();

        dimApplyBtn.setBounds(startX, y, applyW, btnH);
        dimUndoBtn.setBounds(startX + applyW + btnGap, y, smallW, btnH);
        dimRedoBtn.setBounds(startX + applyW + smallW + btnGap * 2, y, smallW, btnH);
        dimResetBtn.setBounds(startX + applyW + smallW * 2 + btnGap * 3, y, resetW, btnH);

        dimensionExplorer.setBounds(overlayBounds.reduced(20, 10));
    }
}

// ═══════════════════════════════════════════════════════════════════
// Default / Init state
// ═══════════════════════════════════════════════════════════════════

void MainPanel::ensureBundledPresetsExist()
{
    auto userDir = PresetFormat::getUserPresetsDirectory();

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        auto* resourceName = BinaryData::namedResourceList[i];
        auto* originalName = BinaryData::originalFilenames[i];
        if (resourceName == nullptr || originalName == nullptr)
            continue;

        const juce::String presetName = juce::String::fromUTF8(originalName);
        if (!presetName.endsWithIgnoreCase(".t5p"))
            continue;

        int size = 0;
        auto* data = BinaryData::getNamedResource(resourceName, size);
        if (data == nullptr || size <= 0)
            continue;

        auto target = userDir.getChildFile(presetName);
        if (!target.existsAsFile())
            target.replaceWithData(data, static_cast<size_t>(size));
    }
}

void MainPanel::loadDefaultPreset()
{
    // Only load if no audio present (fresh launch, not DAW session restore)
    if (processorRef.getGeneratedAudio().getNumSamples() > 0)
        return;

    // Standalone: restore previous session state if available
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        auto bufFile = getBufferPresetFile();
        if (bufFile.existsAsFile())
        {
            auto result = PresetFormat::loadFromFile(bufFile, processorRef);
            if (result.success)
            {
                applyLoadedPreset(result, bufFile);
                return;
            }
        }
    }

    // First launch or DAW: use the same clean state as the Init button.
    loadInitPreset();
}

void MainPanel::loadInitPreset()
{
    for (auto* param : processorRef.getParameters())
        if (param != nullptr)
            param->setValueNotifyingHost(param->getDefaultValue());

    juce::AudioBuffer<float> emptyAudio;
    const double sampleRate = processorRef.getSampleRate() > 0.0
        ? processorRef.getSampleRate()
        : 44100.0;
    processorRef.loadGeneratedAudio(emptyAudio, sampleRate);
    processorRef.setInferenceCacheCapacity(0);
    processorRef.setLastPrompts({}, {});
    processorRef.setLastPresetName({});
    processorRef.setLastTags({});
    processorRef.setLastEmbeddings({}, {});
    processorRef.setLastSeed(static_cast<int>(
        processorRef.getValueTreeState().getRawParameterValue(PID::genSeed)->load()));
    processorRef.setLastInjection("linear", 0.75f, 4.0f, 16.0f);
    processorRef.setLastAxes({});

    promptPanel.loadPresetData({}, {}, processorRef.getLastSeed(), false,
                               {}, {}, "linear", 0.75f, 4.0f, 16.0f);
    axesPanel.setSlotStates({});
    dimensionExplorer.clear();

    currentPresetFile = juce::File();
    statusBar.setPresetName({});
    statusBar.setStatusText("Initialized");
    presetManager.setCurrentPreset(currentPresetFile, {});
    syncInferenceCacheUi();
}

// ═══════════════════════════════════════════════════════════════════
// WAV Export
// ═══════════════════════════════════════════════════════════════════

void MainPanel::exportWav()
{
    // Single Export entry-point that branches on engine mode:
    //   Sampler   → 24-bit PCM .wav of the loaded audio buffer.
    //   Wavetable → Serum-format .wav (32-bit float, mono, frames
    //               concatenated, with `clm ` chunk for frame size).
    const bool isWavetable = processorRef.isWavetableMode();

    if (isWavetable)
    {
        if (!processorRef.getMasterOscConst().hasFrames())
        {
            statusBar.setStatusText("No wavetable to export");
            return;
        }
    }
    else
    {
        if (processorRef.getGeneratedAudio().getNumSamples() == 0)
        {
            statusBar.setStatusText("No audio to export");
            return;
        }
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        isWavetable ? "Export Wavetable" : "Export WAV",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.wav");

    juce::Component::SafePointer<MainPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, chooser, isWavetable](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            auto file = fc.getResult();
            if (file == juce::File()) return;

            // String-concat (not withFileExtension) — the user-typed name
            // may contain dots (e.g. "Pad 0.5"); withFileExtension would
            // truncate everything after the last dot.
            if (!file.hasFileExtension("wav"))
                file = file.getParentDirectory().getChildFile(file.getFileName() + ".wav");

            double sr = self->processorRef.getGeneratedSampleRate();
            if (sr <= 0.0) sr = 44100.0;

            if (isWavetable)
            {
                std::vector<float> samples;
                int frameSize = 0, numFrames = 0;
                if (! self->processorRef.getMasterOscConst()
                        .snapshotLevel0Frames(samples, frameSize, numFrames))
                {
                    self->statusBar.setStatusText("Wavetable export failed: no frames");
                    return;
                }
                if (writeWavetableWav(file, samples, frameSize, numFrames, sr))
                    self->statusBar.setStatusText("Exported wavetable: "
                                                  + file.getFileName()
                                                  + "  (" + juce::String(numFrames) + " frames)");
                else
                    self->statusBar.setStatusText("Wavetable export failed");
                return;
            }

            // Sampler mode — write the loaded audio buffer as 24-bit PCM.
            const auto& buf = self->processorRef.getGeneratedAudio();
            auto outStream = file.createOutputStream();
            if (!outStream) { self->statusBar.setStatusText("Export failed"); return; }

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(outStream.release(), sr,
                                    static_cast<unsigned int>(buf.getNumChannels()),
                                    24, {}, 0));
            if (writer)
            {
                writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
                self->statusBar.setStatusText("Exported: " + file.getFileName());
            }
            else
                self->statusBar.setStatusText("Export failed");
        });
}

// ═══════════════════════════════════════════════════════════════════
// Manual — native WebView renders the bundled HTML guide.
// The HTML is compiled into the plugin as BinaryData and extracted
// once per app-session to a temp file so the WKWebView / WebView2 /
// WebKitGTK backend has a stable file:// URL to load. Anchor links
// (#setup, #gen, …) work natively; external https:// links launch
// in the user's default browser.
// ═══════════════════════════════════════════════════════════════════

void MainPanel::showManual()
{
    manualVisible = true;
    manualScrim.setVisible(true);
    manualScrim.toFront(false);
    manualPanel.setVisible(true);
    manualPanel.toFront(false);
#if JUCE_LINUX
    if (manualWeb.getParentComponent() != &manualPanel)
        manualPanel.addAndMakeVisible(manualWeb);
#endif
    manualWeb.setVisible(true);

    if (!manualLoaded)
    {
        // Extract the bundled HTML to a temp file once per session.
        manualHtmlOnDisk = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("T5ynth_Guide.html");
        manualHtmlOnDisk.replaceWithData(BinaryData::T5ynth_Guide_html,
                                         static_cast<size_t>(BinaryData::T5ynth_Guide_htmlSize));

        manualWeb.goToURL(juce::URL(manualHtmlOnDisk).toString(false));
        manualLoaded = true;
    }

    resized();
}

void MainPanel::hideManual()
{
    manualVisible = false;
    manualScrim.setVisible(false);
    manualPanel.setVisible(false);
    manualWeb.setVisible(false);
    manualWeb.setBounds(-10000, -10000, 1, 1);
#if JUCE_LINUX
    // Linux WebKit child windows can leak through hidden parents; detach the
    // native view while the overlay is closed to avoid the white artefact.
    if (manualWeb.getParentComponent() == &manualPanel)
        manualPanel.removeChildComponent(&manualWeb);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Preset Save / Load
// ═══════════════════════════════════════════════════════════════════

void MainPanel::savePreset()
{
    // Always open the Library in Save mode. The drawer's conflict-aware
    // Save button is the only path that overwrites an existing preset, and
    // the user has to click the explicit red "Replace \"NAME\"" button to
    // confirm. There is no Undo in the synth, so silent overwrites of disk
    // state are not acceptable.
    enterLibrarySaveMode(SaveNameMode::keepName);
}

void MainPanel::loadPreset()
{
    showPresetManager();
}

void MainPanel::renameCurrentPreset()
{
    if (! currentPresetFile.existsAsFile()) return;
    if (! currentPresetFile.isAChildOf(PresetFormat::getUserPresetsDirectory()))
    {
        statusBar.setStatusText("Cannot rename a factory preset");
        return;
    }
    if (presetManager.onRenameRequested) presetManager.onRenameRequested(currentPresetFile);
}

void MainPanel::deleteCurrentPreset()
{
    if (! currentPresetFile.existsAsFile()) return;
    if (! currentPresetFile.isAChildOf(PresetFormat::getUserPresetsDirectory()))
    {
        statusBar.setStatusText("Cannot delete a factory preset");
        return;
    }
    if (presetManager.onDeleteRequested) presetManager.onDeleteRequested(currentPresetFile);
}

void MainPanel::showPresetNameContextMenu(juce::Point<int>)
{
    juce::PopupMenu menu;
    const bool haveUserPreset = currentPresetFile.existsAsFile()
        && currentPresetFile.isAChildOf(PresetFormat::getUserPresetsDirectory());
    menu.addItem(1, juce::String::fromUTF8("Rename\xe2\x80\xa6"), haveUserPreset);
    menu.addItem(2, "Delete",                                     haveUserPreset);
    menu.addSeparator();
    menu.addItem(3, "Reveal in file manager",
                 currentPresetFile.existsAsFile());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&statusBar),
        [this](int result)
        {
            switch (result)
            {
                case 1: renameCurrentPreset(); break;
                case 2: deleteCurrentPreset(); break;
                case 3: if (currentPresetFile.existsAsFile()) currentPresetFile.revealToUser(); break;
                default: break;
            }
        });
}
