#include "MainPanel.h"
#include "../PluginProcessor.h"

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      modulationPanel(processor.getValueTreeState()),
      fxPanel(processor.getValueTreeState()),
      sequencerPanel(processor)
{
    addAndMakeVisible(promptPanel);
    addAndMakeVisible(axesPanel);
    addAndMakeVisible(synthPanel);
    addAndMakeVisible(dimensionExplorer);
    addAndMakeVisible(modulationPanel);
    addAndMakeVisible(fxPanel);
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);

    processorRef.getBackendManager().setStatusCallback(
        [this](BackendManager::Status s)
        {
            juce::MessageManager::callAsync([this, s]()
            {
                statusBar.setConnected(s == BackendManager::Status::Running);
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
        [this](bool connected) { statusBar.setConnected(connected); });
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0a0a));

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float bottomH = h * 0.16f;
    float topH = h - bottomH;

    g.setColour(juce::Colour(0xff1a1a1a));

    // 4 column separators
    float x1 = w * 0.25f;
    float x2 = x1 + w * 0.35f;
    float x3 = x2 + w * 0.25f;
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, topH);
    g.drawVerticalLine(juce::roundToInt(x2), 0.0f, topH);
    g.drawVerticalLine(juce::roundToInt(x3), 0.0f, topH);

    g.drawHorizontalLine(juce::roundToInt(topH), 0.0f, w);
    g.drawHorizontalLine(juce::roundToInt(h - h * 0.03f), 0.0f, w);
}

void MainPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());

    int statusH = juce::roundToInt(h * 0.03f);
    int seqH = juce::roundToInt(h * 0.13f);
    statusBar.setBounds(b.removeFromBottom(statusH));
    sequencerPanel.setBounds(b.removeFromBottom(seqH));

    // 4 columns: 25% | 35% | 25% | 15%
    int col1W = juce::roundToInt(w * 0.25f);
    int col2W = juce::roundToInt(w * 0.35f);
    int col3W = juce::roundToInt(w * 0.25f);
    int col4W = b.getWidth() - col1W - col2W - col3W;

    // Col 1: GENERATION
    auto genCol = b.removeFromLeft(col1W);
    int promptH = juce::roundToInt(static_cast<float>(genCol.getHeight()) * 0.60f);
    promptPanel.setBounds(genCol.removeFromTop(promptH));
    axesPanel.setBounds(genCol);

    // Col 2: MODE + FILTER + EXPLORE
    auto modeCol = b.removeFromLeft(col2W);
    int dimExpH = juce::jmax(160, juce::roundToInt(static_cast<float>(modeCol.getHeight()) * 0.22f));
    dimensionExplorer.setBounds(modeCol.removeFromBottom(dimExpH));
    synthPanel.setBounds(modeCol);

    // Col 3: MODULATION
    modulationPanel.setBounds(b.removeFromLeft(col3W));

    // Col 4: FX + VOLUME
    fxPanel.setBounds(b);
}
