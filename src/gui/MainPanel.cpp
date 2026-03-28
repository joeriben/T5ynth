#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "GuiHelpers.h"

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      fxPanel(processor.getValueTreeState()),
      presetPanel(processor),
      sequencerPanel(processor)
{
    addAndMakeVisible(promptPanel);
    addAndMakeVisible(axesPanel);
    addAndMakeVisible(synthPanel);
    addAndMakeVisible(fxPanel);
    addAndMakeVisible(presetPanel);
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);

    // Wire preset import callback to update GUI-only prompt/seed state
    presetPanel.onPresetLoaded = [this](const juce::String& pA, const juce::String& pB,
                                        int seed, bool randomSeed) {
        promptPanel.loadPresetData(pA, pB, seed, randomSeed);
    };

    // Master volume (rotary knob)
    masterVolKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterVolKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
    masterVolKnob.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    masterVolKnob.setColour(juce::Slider::rotarySliderOutlineColourId, kSurface);
    masterVolKnob.setColour(juce::Slider::textBoxTextColourId, kDim);
    masterVolKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    masterVolKnob.setTextValueSuffix(" dB");
    addAndMakeVisible(masterVolKnob);

    masterVolLabel.setText("Vol", juce::dontSendNotification);
    masterVolLabel.setColour(juce::Label::textColourId, kDim);
    masterVolLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolLabel);

    masterVolA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "master_vol", masterVolKnob);

    // DimExplorer overlay — initially hidden
    dimensionExplorer.setVisible(false);
    addChildComponent(dimensionExplorer);

    dimExplorerClose.setColour(juce::TextButton::buttonColourId, kSurface);
    dimExplorerClose.setColour(juce::TextButton::textColourOffId, kAccent);
    dimExplorerClose.onClick = [this] { hideDimExplorer(); };
    dimExplorerClose.setVisible(false);
    addChildComponent(dimExplorerClose);

    dimExplorerReset.setColour(juce::TextButton::buttonColourId, kSurface);
    dimExplorerReset.setColour(juce::TextButton::textColourOffId, kDim);
    dimExplorerReset.onClick = [this] { dimensionExplorer.clear(); dimensionExplorer.repaint(); };
    dimExplorerReset.setVisible(false);
    addChildComponent(dimExplorerReset);

    // Wire up Explore button in SynthPanel
    synthPanel.onExploreClicked = [this] { showDimExplorer(); };

    // Settings button — will be injected into JUCE standalone header
    settingsButton.setColour(juce::TextButton::buttonColourId, kSurface);
    settingsButton.setColour(juce::TextButton::textColourOffId, kAccent);
    settingsButton.onClick = [this] { showSettings(); };

    // Settings overlay
    settingsPage.setVisible(false);
    addChildComponent(settingsPage);
    settingsPage.onClose = [this] { hideSettings(); };

    // Backend status
    processorRef.getBackendManager().setStatusCallback(
        [this](BackendManager::Status s)
        {
            juce::MessageManager::callAsync([this, s]()
            {
                bool running = (s == BackendManager::Status::Running);
                statusBar.setConnected(running);
                settingsPage.setBackendConnected(running);

                // Sync BackendConnection's connected state when manager reports Running
                if (running)
                    processorRef.getBackendConnection().checkHealth();

                switch (s)
                {
                    case BackendManager::Status::Stopped:  statusBar.setStatusText("Backend stopped"); break;
                    case BackendManager::Status::Starting:  statusBar.setStatusText("Starting..."); break;
                    case BackendManager::Status::Running:   statusBar.setStatusText("Ready"); break;
                    case BackendManager::Status::Failed:    statusBar.setStatusText("Backend failed"); break;
                }
            });
        });

    processorRef.getBackendConnection().setConnectionCallback(
        [this](bool connected) {
            statusBar.setConnected(connected);
            settingsPage.setBackendConnected(connected);
        });
}

void MainPanel::showDimExplorer()
{
    dimExplorerVisible = true;
    dimensionExplorer.setVisible(true);
    dimExplorerClose.setVisible(true);
    dimExplorerReset.setVisible(true);
    dimensionExplorer.toFront(false);
    dimExplorerClose.toFront(false);
    dimExplorerReset.toFront(false);
    resized();
    repaint();
}

void MainPanel::hideDimExplorer()
{
    dimExplorerVisible = false;
    dimensionExplorer.setVisible(false);
    dimExplorerClose.setVisible(false);
    dimExplorerReset.setVisible(false);
    repaint();
}

void MainPanel::showSettings()
{
    settingsVisible = true;
    settingsPage.setVisible(true);
    settingsPage.toFront(false);
    resized();
    repaint();
}

void MainPanel::hideSettings()
{
    settingsVisible = false;
    settingsPage.setVisible(false);
    repaint();
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float bottomH = h * 0.16f;
    float topH = h - bottomH;

    g.setColour(kBorder);

    // Column separator
    float x1 = w * 0.25f;
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, topH);

    // Bottom strip separator
    g.drawHorizontalLine(juce::roundToInt(topH), 0.0f, w);

    // Overlay backgrounds
    if (dimExplorerVisible || settingsVisible)
    {
        g.setColour(juce::Colour(0xdd101016));
        g.fillRect(getLocalBounds());
    }
}

void MainPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());

    int statusH = juce::roundToInt(h * 0.03f);
    int footerH = juce::roundToInt(h * 0.10f);
    statusBar.setBounds(b.removeFromBottom(statusH));

    // Footer: Sequencer (left) | Preset (center) | FX (right) | Master Vol knob (far right)
    auto footer = b.removeFromBottom(footerH);
    int volW = juce::roundToInt(w * 0.06f);
    int fxW = juce::roundToInt(w * 0.30f);
    int presetH = 36;
    auto volArea = footer.removeFromRight(volW);
    int knobSize = juce::jmin(volArea.getWidth(), volArea.getHeight() - 16);
    masterVolKnob.setBounds(volArea.getCentreX() - knobSize / 2, volArea.getY() + 2,
                            knobSize, knobSize);
    masterVolLabel.setFont(juce::FontOptions(10.0f));
    masterVolLabel.setBounds(volArea.getX(), masterVolKnob.getBottom() - 2,
                             volArea.getWidth(), 14);
    fxPanel.setBounds(footer.removeFromRight(fxW));
    presetPanel.setBounds(footer.removeFromTop(presetH));
    sequencerPanel.setBounds(footer);

    // 2 columns: 25% | 75%
    int col1W = juce::roundToInt(w * 0.25f);

    // Col 1: GENERATION
    auto genCol = b.removeFromLeft(col1W);
    int promptH = juce::roundToInt(static_cast<float>(genCol.getHeight()) * 0.55f);
    promptPanel.setBounds(genCol.removeFromTop(promptH));
    axesPanel.setBounds(genCol);

    // Col 2: ENGINE + FILTER + MODULATION
    synthPanel.setBounds(b);

    // Settings overlay
    if (settingsVisible)
        settingsPage.setBounds(getLocalBounds());

    // DimExplorer overlay
    if (dimExplorerVisible)
    {
        auto overlayBounds = getLocalBounds().reduced(40);
        int btnH = 30;
        int btnW = 80;
        int btnGap = 10;

        auto btnArea = overlayBounds.removeFromBottom(btnH + 10);
        int totalBtnW = btnW * 2 + btnGap;
        int startX = btnArea.getCentreX() - totalBtnW / 2;

        dimExplorerReset.setBounds(startX, btnArea.getY(), btnW, btnH);
        dimExplorerClose.setBounds(startX + btnW + btnGap, btnArea.getY(), btnW, btnH);

        dimensionExplorer.setBounds(overlayBounds.reduced(20, 10));
    }
}
