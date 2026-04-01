#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "GuiHelpers.h"
#include <thread>

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
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);
    // PresetPanel is kept as logic handler but not shown — buttons are in StatusBar

    // Wire preset import callback
    presetPanel.onPresetLoaded = [this](const juce::String& pA, const juce::String& pB,
                                        int seed, bool randomSeed,
                                        const juce::String& device) {
        promptPanel.loadPresetData(pA, pB, seed, randomSeed, device);
    };

    // Master volume
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

    // DimExplorer — always visible (mini-view in left column, overlay on click)
    addAndMakeVisible(dimensionExplorer);
    dimensionExplorer.onClicked = [this] {
        if (!dimExplorerVisible) showDimExplorer();
    };

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

    // Load native inference models
    tryLoadInferenceModels();
}

void MainPanel::showDimExplorer()
{
    dimExplorerVisible = true;
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
    dimExplorerClose.setVisible(false);
    dimExplorerReset.setVisible(false);
    resized();  // repositions DimExplorer back to mini-view
    repaint();
}

void MainPanel::toggleSettings() {}

void MainPanel::tryLoadInferenceModels()
{
    // Try pipe inference first (Python subprocess — correct audio)
    statusBar.setStatusText("Loading inference...");

    auto* processor = &processorRef;

    // Find backend directory (contains pipe_inference.py)
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File backendDir;
    auto search = exe.getParentDirectory();
    for (int i = 0; i < 8; ++i)
    {
        if (search.getChildFile("backend/pipe_inference.py").existsAsFile())
        {
            backendDir = search.getChildFile("backend");
            break;
        }
        search = search.getParentDirectory();
    }

    if (backendDir.exists())
    {
        std::thread([this, processor, backendDir]()
        {
            bool ok = processor->launchPipeInference(backendDir);
            juce::MessageManager::callAsync([this, ok]()
            {
                if (ok)
                {
                    statusBar.setConnected(true);
                    statusBar.setStatusText("Ready (Python)");
                    settingsPage.setBackendConnected(true);
                }
                else
                {
                    statusBar.setStatusText("Python inference failed — trying native...");
                    tryLoadNativeInference();
                }
            });
        }).detach();
    }
    else
    {
        tryLoadNativeInference();
    }
}

void MainPanel::tryLoadNativeInference()
{
    // Fallback: try native LibTorch inference (deprecated, produces garbage)
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);

    std::vector<juce::File> candidates = {
        appData.getChildFile("T5ynth/exported_models"),
        juce::File::getCurrentWorkingDirectory().getChildFile("exported_models"),
        home.getChildFile("t5ynth/exported_models"),
        home.getChildFile("ai/t5ynth/exported_models"),
    };

    for (auto& dir : candidates)
    {
        if (dir.getChildFile("dit.pt").existsAsFile())
        {
            statusBar.setStatusText("Loading native models...");
            auto* processor = &processorRef;
            auto modelDir = dir;
            std::thread([this, processor, modelDir]()
            {
                bool ok = processor->loadInferenceModels(modelDir);
                juce::MessageManager::callAsync([this, ok]()
                {
                    if (ok)
                    {
                        statusBar.setConnected(true);
                        statusBar.setStatusText("Ready (native)");
                        settingsPage.setBackendConnected(true);
                    }
                    else
                        statusBar.setStatusText("Model load failed");
                });
            }).detach();
            return;
        }
    }
    statusBar.setStatusText("No model — open Settings to download");
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float bottomH = h * 0.26f;
    float topH = h - bottomH;

    g.setColour(kBorder);
    float x1 = w * 0.25f;
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, topH);
    g.drawHorizontalLine(juce::roundToInt(topH), 0.0f, w);

    if (dimExplorerVisible)
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

    int statusH = juce::jmax(22, juce::roundToInt(h * 0.03f));
    int footerH = juce::roundToInt(h * 0.22f);
    statusBar.setBounds(b.removeFromBottom(statusH));

    // Footer
    auto footer = b.removeFromBottom(footerH);
    int volW = juce::roundToInt(w * 0.06f);
    int fxW = juce::roundToInt(w * 0.30f);
    auto volArea = footer.removeFromRight(volW);
    int knobSize = juce::jmin(volArea.getWidth(), volArea.getHeight() - 16);
    masterVolKnob.setBounds(volArea.getCentreX() - knobSize / 2, volArea.getY() + 2,
                            knobSize, knobSize);
    masterVolLabel.setFont(juce::FontOptions(10.0f));
    masterVolLabel.setBounds(volArea.getX(), masterVolKnob.getBottom() - 2,
                             volArea.getWidth(), 14);
    fxPanel.setBounds(footer.removeFromRight(fxW));
    sequencerPanel.setBounds(footer);

    // Col 1: GENERATION + AXES + DIM EXPLORER
    int col1W = juce::roundToInt(w * 0.25f);
    auto genCol = b.removeFromLeft(col1W);

    int promptH = juce::roundToInt(static_cast<float>(genCol.getHeight()) * 0.55f);
    int axesH = juce::roundToInt(static_cast<float>(genCol.getHeight()) * 0.25f);
    promptPanel.setBounds(genCol.removeFromTop(promptH));
    axesPanel.setBounds(genCol.removeFromTop(axesH));
    if (!dimExplorerVisible)
        dimensionExplorer.setBounds(genCol);

    // Col 2: ENGINE
    synthPanel.setBounds(b);



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
