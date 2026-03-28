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

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto json = file.loadFileAsString();
            if (processor.importJsonPreset(json))
            {
                statusLabel.setText("Loaded: " + file.getFileNameWithoutExtension(), juce::dontSendNotification);

                // Extract GUI-only data (prompts, seed) and notify parent
                if (onPresetLoaded)
                {
                    auto parsed = juce::JSON::parse(json);
                    if (auto* root = parsed.getDynamicObject())
                    {
                        juce::String promptA, promptB;
                        int seed = -1;
                        bool randomSeed = true;

                        if (auto* synth = root->getProperty("synth").getDynamicObject())
                        {
                            promptA = synth->getProperty("promptA").toString();
                            promptB = synth->getProperty("promptB").toString();
                            seed = static_cast<int>(synth->getProperty("seed"));
                            // seed > 0 means fixed seed, -1 or absent means random
                            randomSeed = (seed <= 0);
                        }
                        onPresetLoaded(promptA, promptB, seed > 0 ? seed : 123456789, randomSeed);
                    }
                }
            }
            else
                statusLabel.setText("Import failed", juce::dontSendNotification);
        });
}

void PresetPanel::exportPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Preset", juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.json");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;

            auto jsonFile = file.withFileExtension("json");
            auto json = processor.exportJsonPreset();
            jsonFile.replaceWithText(json);
            statusLabel.setText("Saved: " + jsonFile.getFileNameWithoutExtension(), juce::dontSendNotification);
        });
}
