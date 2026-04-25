#pragma once
#include <JuceHeader.h>
#include <vector>
#include "GuiHelpers.h"

/**
 * Save-preset modal: name input + tag chips + library context.
 *
 * Triggered from the StatusBar Save button. Independent of the library
 * browser. Shows a scrollable read-only list of existing user preset
 * names so the user can see what already exists; if the typed name
 * collides with one, the matching row is highlighted and a "will
 * overwrite" warning appears inline.
 *
 * Closes via Cancel button, top-right × icon, Esc key, or scrim click
 * (handled by the owner via `onCancel`).
 */
class SavePresetDialog : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::KeyListener
{
public:
    /** Magic bank name that means "save to user root, not a subdirectory". */
    static constexpr const char* kRootBankLabel = "Default";

    std::function<void(const juce::String& name,
                       const juce::StringArray& tags,
                       const juce::String& bank)> onSave;
    std::function<void()> onCancel;

    SavePresetDialog()
    {
        title.setText("Save Preset", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(title);

        configureIconButton(closeIconBtn);
        closeIconBtn.setButtonText(juce::String::fromUTF8("\xc3\x97"));
        closeIconBtn.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible(closeIconBtn);

        nameLabel.setText("Name", juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, kDim);
        nameLabel.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
        addAndMakeVisible(nameLabel);

        tagsLabel.setText("Tags (comma-separated; suggestions pre-filled)",
                          juce::dontSendNotification);
        tagsLabel.setColour(juce::Label::textColourId, kDim);
        tagsLabel.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
        addAndMakeVisible(tagsLabel);

        bankLabel.setText("Bank (subdirectory; type to create new)", juce::dontSendNotification);
        bankLabel.setColour(juce::Label::textColourId, kDim);
        bankLabel.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
        addAndMakeVisible(bankLabel);

        bankBox.setEditableText(true);
        bankBox.setColour(juce::ComboBox::backgroundColourId, kSurface.brighter(0.04f));
        bankBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
        bankBox.setColour(juce::ComboBox::outlineColourId, kBorder);
        bankBox.setColour(juce::ComboBox::focusedOutlineColourId, kAccent);
        bankBox.onChange = [this] { refreshConflictUi(); };
        addAndMakeVisible(bankBox);

        libraryLabel.setText("User library", juce::dontSendNotification);
        libraryLabel.setColour(juce::Label::textColourId, kDim);
        libraryLabel.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
        addAndMakeVisible(libraryLabel);

        warningLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffaa55));
        warningLabel.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
        warningLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(warningLabel);

        configureEditor(nameEdit);
        nameEdit.setEscapeAndReturnKeysConsumed(false);
        nameEdit.addKeyListener(this);
        nameEdit.onReturnKey  = [this] { confirm(); };
        nameEdit.onTextChange = [this] { refreshConflictUi(); };
        addAndMakeVisible(nameEdit);

        configureEditor(tagsEdit);
        tagsEdit.setMultiLine(false);
        tagsEdit.setEscapeAndReturnKeysConsumed(false);
        tagsEdit.addKeyListener(this);
        tagsEdit.onReturnKey = [this] { confirm(); };
        addAndMakeVisible(tagsEdit);

        bankBox.onChange = [this] { refreshConflictUi(); };

        existingList.setModel(this);
        existingList.setColour(juce::ListBox::backgroundColourId, kSurface);
        existingList.setColour(juce::ListBox::outlineColourId, kBorder);
        existingList.setRowHeight(20);
        addAndMakeVisible(existingList);

        configureButton(saveBtn,   kAccent);
        configureButton(cancelBtn, kSurface);
        saveBtn.onClick   = [this] { confirm(); };
        cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible(saveBtn);
        addAndMakeVisible(cancelBtn);

        setWantsKeyboardFocus(true);
        addKeyListener(this);
    }

    /** Pre-fill name + suggested tags + library names + bank picker.
     *  `existingPathKeys` is the set of paths-relative-to-user-dir of every
     *  existing user preset in lowercase form ("foo.t5p" or "ambient/bar.t5p").
     *  This drives the bank-aware conflict highlight. */
    void configure(const juce::String& defaultName,
                   const juce::StringArray& suggestedTags,
                   juce::StringArray existingUserPresetNames,
                   juce::StringArray existingBanks,
                   std::set<juce::String> existingPathKeys,
                   const juce::String& currentBank)
    {
        nameEdit.setText(defaultName, juce::dontSendNotification);
        tagsEdit.setText(suggestedTags.joinIntoString(", "), juce::dontSendNotification);
        existingNames = std::move(existingUserPresetNames);
        existingNames.sortNatural();
        existingList.updateContent();

        existingPaths = std::move(existingPathKeys);

        bankBox.clear(juce::dontSendNotification);
        bankBox.addItem(kRootBankLabel, 1);
        int nextId = 2;
        for (auto& b : existingBanks)
        {
            if (b.isEmpty() || b == kRootBankLabel) continue;
            bankBox.addItem(b, nextId++);
        }
        const auto initial = currentBank.isEmpty() ? juce::String(kRootBankLabel) : currentBank;
        bankBox.setText(initial, juce::dontSendNotification);

        refreshConflictUi();
        nameEdit.grabKeyboardFocus();
    }

    void paint(juce::Graphics& g) override
    {
        paintCard(g, getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);

        // Top row: title left, × icon right
        auto top = area.removeFromTop(22);
        closeIconBtn.setBounds(top.removeFromRight(22));
        title.setBounds(top);
        area.removeFromTop(10);

        // Name
        nameLabel.setBounds(area.removeFromTop(14));
        nameEdit.setBounds(area.removeFromTop(28));

        // Inline warning right under the name field (consumes 16 px even
        // when empty so the layout doesn't jump on first conflict).
        warningLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(6);

        // Tags
        tagsLabel.setBounds(area.removeFromTop(14));
        tagsEdit.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);

        // Bank picker
        bankLabel.setBounds(area.removeFromTop(14));
        bankBox.setBounds(area.removeFromTop(26));
        area.removeFromTop(12);

        // Buttons at bottom
        auto strip = area.removeFromBottom(30);
        saveBtn.setBounds(strip.removeFromRight(96));
        strip.removeFromRight(8);
        cancelBtn.setBounds(strip.removeFromRight(80));
        area.removeFromBottom(10);

        // Library context list (fills remainder)
        libraryLabel.setBounds(area.removeFromTop(14));
        existingList.setBounds(area);
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
    static void configureIconButton(juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, kDim);
        b.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
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

        auto bank = bankBox.getText().trim();
        if (bank.equalsIgnoreCase(kRootBankLabel))
            bank = {};   // empty = save to user-dir root
        else
            bank = juce::File::createLegalPathName(bank);

        if (onSave) onSave(name, tags, bank);
    }

    /** Rebuilds the conflict warning + Save button label/colour. The Save
     *  button is the affirmation: when no conflict it reads "Save Preset"
     *  in accent colour, when the typed (bank,name) combination would
     *  overwrite an existing file it switches to "Replace \"NAME\"" in red.
     *  The label itself spells out the action so there is no second popup
     *  with the usual ambiguous "OK"/"Cancel" wording. */
    void refreshConflictUi()
    {
        const auto cleanedName = juce::File::createLegalFileName(nameEdit.getText().trim());
        const auto bankRaw     = bankBox.getText().trim();
        const auto bank = bankRaw.equalsIgnoreCase(kRootBankLabel)
                              ? juce::String()
                              : juce::File::createLegalPathName(bankRaw);

        const auto pathKey = makePathKey(bank, cleanedName);
        const bool conflict = (! cleanedName.isEmpty())
                              && existingPaths.count(pathKey) > 0;

        if (conflict)
        {
            const auto bankLabel = bank.isEmpty() ? juce::String(kRootBankLabel) : bank;
            warningLabel.setText(juce::String::fromUTF8("\xe2\x9a\xa0  Will replace \"") + cleanedName
                                 + "\" in bank \"" + bankLabel + "\"",
                                 juce::dontSendNotification);
            warningLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff5050));
            saveBtn.setButtonText("Replace \"" + cleanedName + "\"");
            saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffaa3333));
        }
        else
        {
            warningLabel.setText({}, juce::dontSendNotification);
            saveBtn.setButtonText("Save Preset");
            saveBtn.setColour(juce::TextButton::buttonColourId, kAccent);
        }

        // Highlight matching row in the library list (display-only).
        const int rowHit = findRowByName(cleanedName);
        if (rowHit >= 0) existingList.selectRow(rowHit, false, true);
        else             existingList.deselectAllRows();
        existingList.repaint();
    }

    static juce::String makePathKey(const juce::String& bank, const juce::String& name)
    {
        if (name.isEmpty()) return {};
        const auto file = name + ".t5p";
        const auto key = bank.isEmpty() ? file : (bank + "/" + file);
        return key.toLowerCase();
    }

    int findRowByName(const juce::String& cleanedName) const
    {
        if (cleanedName.isEmpty()) return -1;
        for (int i = 0; i < existingNames.size(); ++i)
            if (existingNames[i].equalsIgnoreCase(cleanedName))
                return i;
        return -1;
    }

    // ─── ListBoxModel ────────────────────────────────────────────────────
    int getNumRows() override { return existingNames.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow(row, existingNames.size())) return;
        if (selected)
        {
            g.setColour(juce::Colour(0xffffaa55).withAlpha(0.18f));
            g.fillRect(0, 0, width, height);
            g.setColour(juce::Colour(0xffffaa55));
            g.fillRect(0, 0, 3, height);
        }
        g.setColour(selected ? juce::Colours::white : juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::FontOptions(11.5f));
        g.drawText(existingNames[row],
                   juce::Rectangle<int>(0, 0, width, height).reduced(8, 0),
                   juce::Justification::centredLeft, true);
    }

    // ─── KeyListener ─────────────────────────────────────────────────────
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onCancel) onCancel();
            return true;
        }
        return false;
    }

    juce::Label title, nameLabel, tagsLabel, bankLabel, libraryLabel, warningLabel;
    juce::TextEditor nameEdit, tagsEdit;
    juce::ComboBox bankBox;
    juce::ListBox existingList;
    juce::StringArray existingNames;
    std::set<juce::String> existingPaths;
    juce::TextButton saveBtn       { "Save" };
    juce::TextButton cancelBtn     { "Cancel" };
    juce::TextButton closeIconBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SavePresetDialog)
};
