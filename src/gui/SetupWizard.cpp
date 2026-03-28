#include "SetupWizard.h"
#include "GuiHelpers.h"

static const juce::String kModelFileName = "model_index.json";
static const juce::String kModelName     = "stabilityai/stable-audio-open-1.0";

SettingsPage::SettingsPage()
{
    titleLabel.setText("T5ynth Settings", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, kAccent);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(modelStatusLabel);

    modelPathLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(modelPathLabel);

    instructionsLabel.setColour(juce::Label::textColourId, kDim);
    instructionsLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(instructionsLabel);

    scanButton.setColour(juce::TextButton::buttonColourId, kSurface);
    scanButton.setColour(juce::TextButton::textColourOffId, kAccent);
    scanButton.onClick = [this] {
        auto found = scanForModel();
        if (found.exists())
            setModelPath(found);
        updateStatus();
    };
    addAndMakeVisible(scanButton);

    browseButton.setColour(juce::TextButton::buttonColourId, kSurface);
    browseButton.setColour(juce::TextButton::textColourOffId, kAccent);
    browseButton.onClick = [this] { browseForModel(); };
    addAndMakeVisible(browseButton);

    closeButton.setColour(juce::TextButton::buttonColourId, kSurface);
    closeButton.setColour(juce::TextButton::textColourOffId, kDim);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeButton);

    // Auto-scan on construction
    auto found = scanForModel();
    if (found.exists())
        modelPath = found;
    updateStatus();
}

juce::File SettingsPage::scanForModel()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    // Known locations to check (most specific first)
    std::vector<juce::File> candidates = {
        // Explicit T5ynth model dir (may be symlink)
        home.getChildFile("t5ynth/models/stable-audio-open-1.0"),

        // HuggingFace cache (scan snapshot dirs)
        home.getChildFile(".cache/huggingface/hub/models--stabilityai--stable-audio-open-1.0"),

        // ComfyUI common location
        home.getChildFile("ai/ai4artsed_development/dlbackend/ComfyUI/models/checkpoints/"
                          "OfficialStableDiffusion/stableaudio/stable-audio-open-1.0"),
    };

    for (auto& dir : candidates)
    {
        if (!dir.isDirectory()) continue;

        // Direct match
        if (dir.getChildFile(kModelFileName).existsAsFile())
            return dir;

        // HuggingFace cache: check inside snapshots/
        auto snapshotsDir = dir.getChildFile("snapshots");
        if (snapshotsDir.isDirectory())
        {
            for (auto& snapshot : snapshotsDir.findChildFiles(
                     juce::File::findDirectories, false))
            {
                if (snapshot.getChildFile(kModelFileName).existsAsFile())
                    return snapshot;
            }
        }
    }

    return {};
}

void SettingsPage::setModelPath(const juce::File& dir)
{
    modelPath = dir;

    // Create symlink at ~/t5ynth/models/stable-audio-open-1.0 for the backend
    auto targetDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                         .getChildFile("t5ynth/models/stable-audio-open-1.0");

    if (targetDir.exists() && targetDir.isSymbolicLink())
        targetDir.deleteFile();

    if (!targetDir.exists())
    {
        targetDir.getParentDirectory().createDirectory();
        targetDir.createSymbolicLink(dir, false);
    }

    updateStatus();
}

void SettingsPage::browseForModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select the Stable Audio model directory (contains model_index.json)",
        modelPath.exists() ? modelPath : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "", true);

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File()) return;

            if (!result.getChildFile(kModelFileName).existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Wrong Directory",
                    "This directory does not contain " + kModelFileName + ".\n\n"
                    "Select the directory named 'stable-audio-open-1.0' that contains "
                    "model_index.json, text_encoder/, transformer/, vae/, etc.");
                return;
            }

            setModelPath(result);
        });
}

void SettingsPage::updateStatus()
{
    if (modelPath.exists() && modelPath.getChildFile(kModelFileName).existsAsFile())
    {
        modelStatusLabel.setText("Model: Found", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        modelPathLabel.setText(modelPath.getFullPathName(), juce::dontSendNotification);
        instructionsLabel.setText("Model is ready. The backend will use this path on next start.",
                                 juce::dontSendNotification);
    }
    else
    {
        modelStatusLabel.setText("Model: Not found", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        modelPathLabel.setText("", juce::dontSendNotification);
        instructionsLabel.setText(
            "Required: " + kModelName + " (HuggingFace pipeline format)\n\n"
            "Option 1 — Auto-Scan: Click 'Auto-Scan' to search common locations.\n\n"
            "Option 2 — Download:\n"
            "  huggingface-cli download stabilityai/stable-audio-open-1.0\n"
            "    --local-dir ~/t5ynth/models/stable-audio-open-1.0\n\n"
            "Option 3 — Browse: Select the directory containing model_index.json,\n"
            "  text_encoder/, transformer/, vae/, etc.",
            juce::dontSendNotification);
    }
}

void SettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xf0101018));

    auto area = getLocalBounds().reduced(30);
    g.setColour(kCard);
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.0f);
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced(50, 40);
    float f = juce::jlimit(14.0f, 22.0f, static_cast<float>(getHeight()) * 0.025f);
    int rowH = juce::roundToInt(f * 1.6f);
    int gap = juce::roundToInt(f * 0.5f);

    titleLabel.setFont(juce::FontOptions(f * 1.4f).withStyle("Bold"));
    titleLabel.setBounds(area.removeFromTop(juce::roundToInt(f * 2.0f)));
    area.removeFromTop(gap * 2);

    // Model section
    modelStatusLabel.setFont(juce::FontOptions(f));
    modelStatusLabel.setBounds(area.removeFromTop(rowH));

    modelPathLabel.setFont(juce::FontOptions(f * 0.8f));
    modelPathLabel.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(gap);

    // Buttons row
    auto btnRow = area.removeFromTop(juce::roundToInt(f * 2.0f));
    int btnW = juce::roundToInt(static_cast<float>(btnRow.getWidth()) * 0.2f);
    int btnGap = 10;
    scanButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(btnGap);
    browseButton.setBounds(btnRow.removeFromLeft(btnW));
    area.removeFromTop(gap * 2);

    // Instructions
    instructionsLabel.setFont(juce::FontOptions(f * 0.85f));
    instructionsLabel.setBounds(area.removeFromTop(area.getHeight() - rowH - gap));

    // Close button at bottom
    area.removeFromTop(gap);
    int closeBtnW = juce::roundToInt(static_cast<float>(area.getWidth()) * 0.15f);
    closeButton.setBounds(area.removeFromBottom(rowH).withWidth(closeBtnW)
                              .withX(area.getCentreX() - closeBtnW / 2));
}
