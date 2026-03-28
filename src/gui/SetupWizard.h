#pragma once
#include <JuceHeader.h>

/**
 * Settings page / first-run wizard.
 *
 * Shows model status, auto-scans known paths, browse for model directory,
 * download instructions. Displayed as an overlay in MainPanel.
 */
class SettingsPage : public juce::Component
{
public:
    SettingsPage();
    ~SettingsPage() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Scan known paths for the Stable Audio model. Returns found path or empty. */
    juce::File scanForModel();

    /** Set model path and create symlink at ~/t5ynth/models/stable-audio-open-1.0 */
    void setModelPath(const juce::File& dir);

    /** Get the resolved model path (empty if not found). */
    juce::File getModelPath() const { return modelPath; }

    /** Called when user clicks Close. */
    std::function<void()> onClose;

private:
    void browseForModel();
    void updateStatus();

    juce::File modelPath;

    juce::Label titleLabel;
    juce::Label modelStatusLabel;
    juce::Label modelPathLabel;
    juce::Label instructionsLabel;

    juce::TextButton scanButton    { "Auto-Scan" };
    juce::TextButton browseButton  { "Browse..." };
    juce::TextButton closeButton   { "Close" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
