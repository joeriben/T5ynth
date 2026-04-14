#include "PresetPanel.h"
#include "../PluginProcessor.h"

PresetPanel::PresetPanel(T5ynthProcessor& proc) : processor(proc)
{
    addAndMakeVisible(importButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(statusLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    importButton.onClick = [this] { importPreset(); };
    exportButton.onClick = [this] { exportPreset(); };
}

void PresetPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0a0a));
}

void PresetPanel::resized()
{
    auto area = getLocalBounds().reduced(4);
    auto buttonH = std::min(28, area.getHeight());
    auto buttonW = 120;

    importButton.setBounds(area.removeFromLeft(buttonW).withHeight(buttonH));
    area.removeFromLeft(8);
    exportButton.setBounds(area.removeFromLeft(buttonW).withHeight(buttonH));
    area.removeFromLeft(8);
    statusLabel.setBounds(area.withHeight(buttonH));
}

void PresetPanel::importPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import Preset", juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.json");

    juce::Component::SafePointer<PresetPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto json = file.loadFileAsString();
            if (self->processor.importJsonPreset(json))
            {
                self->statusLabel.setText("Loaded: " + file.getFileNameWithoutExtension(), juce::dontSendNotification);

                // Extract GUI-only data (prompts, seed) and notify parent
                if (self->onPresetLoaded)
                {
                    auto parsed = juce::JSON::parse(json);
                    if (auto* root = parsed.getDynamicObject())
                    {
                        juce::String promptA, promptB, device;
                        int seed = 123456789;

                        if (auto* synth = root->getProperty("synth").getDynamicObject())
                        {
                            promptA = synth->getProperty("promptA").toString();
                            promptB = synth->getProperty("promptB").toString();
                            int s = static_cast<int>(synth->getProperty("seed"));
                            if (s > 0) seed = s;
                            device = synth->getProperty("device").toString();
                        }
                        self->onPresetLoaded(promptA, promptB, seed, device);
                    }
                }
            }
            else
                self->statusLabel.setText("Import failed", juce::dontSendNotification);
        });
}

void PresetPanel::exportPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Preset", juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.json");

    juce::Component::SafePointer<PresetPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            auto file = fc.getResult();
            if (file == juce::File()) return;

            auto jsonFile = file.withFileExtension("json");
            auto json = self->processor.exportJsonPreset();
            jsonFile.replaceWithText(json);
            self->statusLabel.setText("Saved: " + jsonFile.getFileNameWithoutExtension(), juce::dontSendNotification);
        });
}
