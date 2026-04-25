#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"

/**
 * Small modal: name + comma-separated tags + Save/Cancel.
 *
 * Used directly by the StatusBar `Save` button (no library browser in
 * the way). The owner supplies a default name and a suggested tag list
 * via configure(), then receives `onSave(name, tags)` or `onCancel()`.
 *
 * Pure presentation — no file I/O, no processor reach-through.
 */
class SavePresetDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name, const juce::StringArray& tags)> onSave;
    std::function<void()> onCancel;

    SavePresetDialog()
    {
        title.setText("Save Preset", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(title);

        nameLabel.setText("Name", juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, kDim);
        nameLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        addAndMakeVisible(nameLabel);

        tagsLabel.setText("Tags (comma-separated; suggestions pre-filled)",
                          juce::dontSendNotification);
        tagsLabel.setColour(juce::Label::textColourId, kDim);
        tagsLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        addAndMakeVisible(tagsLabel);

        configureEditor(nameEdit);
        nameEdit.onReturnKey = [this] { confirm(); };
        addAndMakeVisible(nameEdit);

        configureEditor(tagsEdit);
        tagsEdit.setMultiLine(false);
        tagsEdit.onReturnKey = [this] { confirm(); };
        addAndMakeVisible(tagsEdit);

        configureButton(saveBtn,   kAccent);
        configureButton(cancelBtn, kSurface);
        saveBtn.onClick   = [this] { confirm(); };
        cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible(saveBtn);
        addAndMakeVisible(cancelBtn);
    }

    /** Pre-fill name + suggested tags before showing. */
    void configure(const juce::String& defaultName,
                   const juce::StringArray& suggestedTags)
    {
        nameEdit.setText(defaultName, juce::dontSendNotification);
        tagsEdit.setText(suggestedTags.joinIntoString(", "), juce::dontSendNotification);
        nameEdit.grabKeyboardFocus();
    }

    void paint(juce::Graphics& g) override
    {
        paintCard(g, getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        title.setBounds(area.removeFromTop(20));
        area.removeFromTop(10);

        nameLabel.setBounds(area.removeFromTop(14));
        nameEdit.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        tagsLabel.setBounds(area.removeFromTop(14));
        tagsEdit.setBounds(area.removeFromTop(28));

        auto strip = area.removeFromBottom(30);
        saveBtn.setBounds(strip.removeFromRight(96));
        strip.removeFromRight(8);
        cancelBtn.setBounds(strip.removeFromRight(80));
    }

private:
    static void configureEditor(juce::TextEditor& e)
    {
        e.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.04f));
        e.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        e.setColour(juce::TextEditor::outlineColourId, kBorder);
        e.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
        e.setSelectAllWhenFocused(true);
    }
    static void configureButton(juce::TextButton& b, juce::Colour c)
    {
        b.setColour(juce::TextButton::buttonColourId, c);
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }

    void confirm()
    {
        const auto name = juce::File::createLegalFileName(nameEdit.getText().trim());
        if (name.isEmpty()) return;
        juce::StringArray tags;
        tags.addTokens(tagsEdit.getText(), ",", "");
        tags.trim();
        tags.removeEmptyStrings();
        tags.removeDuplicates(true);
        if (onSave) onSave(name, tags);
    }

    juce::Label title, nameLabel, tagsLabel;
    juce::TextEditor nameEdit, tagsEdit;
    juce::TextButton saveBtn   { "Save" };
    juce::TextButton cancelBtn { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SavePresetDialog)
};
