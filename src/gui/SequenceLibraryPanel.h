#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include "GuiHelpers.h"
#include "../presets/PresetFormat.h"

/**
 * SequenceLibraryPanel — compact in-app browser for .t5seq sequencer
 * patterns. Deliberately a fraction of PresetManagerPanel: one list, a
 * save row, right-click rename/delete. No tags / banks / search / GitHub /
 * detail pane. MainPanel shows it as a centered overlay over a dimmed scrim,
 * mirroring how it hosts the preset manager.
 *
 * It is a (mostly) self-contained view:
 *   - Load + Save are emitted as callbacks — they need the processor, which
 *     lives elsewhere; the owner (MainPanel) routes them to SequencerPanel.
 *   - Delete + Rename are plain file operations the panel performs itself,
 *     then refreshes the list. No processor involvement, so no callback.
 * The callbacks return bool so the panel can report success and close/refresh.
 */
class SequenceLibraryPanel : public juce::Component,
                             public juce::ListBoxModel
{
public:
    SequenceLibraryPanel()
    {
        title.setText("Pattern Library", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(title);

        closeBtn.setButtonText(juce::String::fromUTF8("\xc3\x97")); // ×
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeBtn.setColour(juce::TextButton::textColourOffId, kDim);
        closeBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        closeBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible(closeBtn);

        patternList.setModel(this);
        patternList.setColour(juce::ListBox::backgroundColourId, kSurface);
        patternList.setColour(juce::ListBox::outlineColourId, kBorder);
        patternList.setOutlineThickness(1);
        patternList.setRowHeight(34);
        addAndMakeVisible(patternList);

        hintLabel.setText(juce::String::fromUTF8(
            "Double-click to load \xc2\xb7 right-click to rename or delete"),
            juce::dontSendNotification);
        hintLabel.setColour(juce::Label::textColourId, kDimmer);
        hintLabel.setFont(juce::FontOptions(11.0f));
        hintLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(hintLabel);

        saveAsLabel.setText("Save as", juce::dontSendNotification);
        saveAsLabel.setColour(juce::Label::textColourId, kDim);
        saveAsLabel.setFont(juce::FontOptions(12.0f));
        saveAsLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(saveAsLabel);

        nameEditor.setTextToShowWhenEmpty("pattern name", kDimmer);
        nameEditor.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.04f));
        nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        nameEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
        nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
        nameEditor.setReturnKeyStartsNewLine(false);
        // Don't swallow Escape — let it bubble to keyPressed() so the overlay
        // closes even while the name field has focus (Return still saves via
        // onReturnKey). Matches the editors in PresetManagerPanel.
        nameEditor.setEscapeAndReturnKeysConsumed(false);
        nameEditor.onReturnKey = [this] { doSave(); };
        addAndMakeVisible(nameEditor);

        saveBtn.setButtonText("Save");
        saveBtn.setColour(juce::TextButton::buttonColourId, kAccent);
        saveBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        saveBtn.onClick = [this] { doSave(); };
        addAndMakeVisible(saveBtn);

        statusLabel.setColour(juce::Label::textColourId, kDim);
        statusLabel.setFont(juce::FontOptions(11.5f));
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(statusLabel);
    }

    // ── Owner contract ──────────────────────────────────────────────────
    // Both return success so the panel can close (load) or refresh (save).
    std::function<bool(const juce::File&)> onLoadRequested;
    std::function<bool(const juce::File&)> onSaveRequested; // arg = full target path
    std::function<void()>                  onCloseRequested;

    /** Re-scan the sequences directory (most-recent first) and rebuild the list. */
    void refreshList()
    {
        files.clear();
        auto dir = PresetFormat::getUserSequencesDirectory();
        if (dir.isDirectory())
            for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.t5seq"))
                files.add(f);

        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        patternList.updateContent();
        patternList.repaint();
    }

    void setStatusText(const juce::String& s, bool isError = false)
    {
        statusLabel.setText(s, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId,
                              isError ? juce::Colour(0xffff8a80) : kDim);
    }

    /** Owner calls this when opening the overlay: rescan, clear transient
     *  state, and focus the name field (Save entry) or the list (Load entry). */
    void prepareForShow(bool focusSave)
    {
        refreshList();
        nameEditor.setText({}, juce::dontSendNotification);
        setStatusText({});
        if (focusSave) nameEditor.grabKeyboardFocus();
        else           patternList.grabKeyboardFocus();
    }

    // ── juce::Component ─────────────────────────────────────────────────
    void paint(juce::Graphics& g) override { paintCard(g, getLocalBounds()); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onCloseRequested) onCloseRequested();
            return true;
        }
        return false;
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(14);

        auto top = b.removeFromTop(28);
        closeBtn.setBounds(top.removeFromRight(26));
        title.setBounds(top);
        b.removeFromTop(8);

        // Bottom-up: status, save row, hint — then the list fills the rest.
        statusLabel.setBounds(b.removeFromBottom(18));
        b.removeFromBottom(4);

        auto saveRow = b.removeFromBottom(28);
        saveAsLabel.setBounds(saveRow.removeFromLeft(56));
        saveBtn.setBounds(saveRow.removeFromRight(64));
        saveRow.removeFromRight(6);
        nameEditor.setBounds(saveRow);
        b.removeFromBottom(6);

        hintLabel.setBounds(b.removeFromBottom(16));
        b.removeFromBottom(6);

        patternList.setBounds(b);
    }

    // ── juce::ListBoxModel ──────────────────────────────────────────────
    int getNumRows() override { return files.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override
    {
        if (row < 0 || row >= files.size())
            return;

        if (rowIsSelected)
        {
            g.setColour(kAccent.withAlpha(0.22f));
            g.fillRect(0, 0, width, height);
            g.setColour(kAccent);
            g.fillRect(0, 0, 3, height); // left-edge marker
        }

        auto r = juce::Rectangle<int>(0, 0, width, height).reduced(10, 0);
        auto dateArea = r.removeFromRight(84);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(files[row].getFileNameWithoutExtension(), r,
                   juce::Justification::centredLeft, true);

        g.setColour(kDimmer);
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(files[row].getLastModificationTime().toString(true, false),
                   dateArea, juce::Justification::centredRight, true);

        g.setColour(kBorder.withAlpha(0.35f));
        g.fillRect(0, height - 1, width, 1);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { loadRow(row); }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showRowMenu(row, e.getScreenPosition());
    }

    void returnKeyPressed(int lastRowSelected) override { loadRow(lastRowSelected); }
    void deleteKeyPressed(int lastRowSelected) override { deleteRow(lastRowSelected); }

private:
    void loadRow(int row)
    {
        if (row < 0 || row >= files.size() || ! onLoadRequested)
            return;

        if (onLoadRequested(files[row]))
        {
            if (onCloseRequested)
                onCloseRequested();
        }
        else
        {
            setStatusText("Load failed", true);
        }
    }

    void doSave()
    {
        const auto name = juce::File::createLegalFileName(nameEditor.getText().trim());
        if (name.isEmpty())
        {
            setStatusText("Enter a name to save", true);
            return;
        }

        // String-concat (not withFileExtension): a typed name may contain a
        // dot ("My Beat 0.5"); withFileExtension would truncate at the dot.
        auto target = PresetFormat::getUserSequencesDirectory().getChildFile(name + ".t5seq");
        const bool replacing = target.existsAsFile();

        if (onSaveRequested && onSaveRequested(target))
        {
            refreshList();
            selectFile(target);
            setStatusText((replacing ? "Replaced " : "Saved ") + name);
        }
        else
        {
            setStatusText("Save failed", true);
        }
    }

    void showRowMenu(int row, juce::Point<int> screenPos)
    {
        if (row < 0 || row >= files.size())
            return;

        patternList.selectRow(row);
        juce::PopupMenu m;
        m.addItem(1, juce::String::fromUTF8("Rename\xe2\x80\xa6")); // Rename…
        m.addItem(2, "Delete");
        // Anchor at the cursor — withTargetComponent snaps the menu to the
        // list's bottom edge and looks broken (see PresetManagerPanel).
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
            [this, row](int result)
            {
                if (result == 1)      renameRow(row);
                else if (result == 2) deleteRow(row);
            });
    }

    void deleteRow(int row)
    {
        if (row < 0 || row >= files.size())
            return;

        const auto f = files[row];
        // showAsync (MessageBoxOptions) — the legacy showOkCancelBox returns
        // false on Linux without showing the dialog (see MainPanel delete).
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Delete Pattern")
                .withMessage("Delete \"" + f.getFileNameWithoutExtension() + "\"?")
                .withButton("Delete")
                .withButton("Cancel")
                .withParentComponent(this),
            [this, f](int result)
            {
                if (result != 1) return; // 1 == first button (Delete)
                if (f.deleteFile())
                {
                    refreshList();
                    setStatusText("Deleted " + f.getFileNameWithoutExtension());
                }
                else
                {
                    setStatusText("Delete failed", true);
                }
            });
    }

    void renameRow(int row)
    {
        if (row < 0 || row >= files.size())
            return;

        const auto f = files[row];
        // Modal AlertWindow with one text editor, leak-safe via the unique_ptr
        // deleter in the callback (mirrors MainPanel's preset rename).
        auto* alert = new juce::AlertWindow(
            "Rename Pattern",
            "New name for \"" + f.getFileNameWithoutExtension() + "\":",
            juce::MessageBoxIconType::QuestionIcon, this);
        alert->addTextEditor("name", f.getFileNameWithoutExtension());
        alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        alert->enterModalState(true,
            juce::ModalCallbackFunction::create([this, alert, f](int result)
            {
                std::unique_ptr<juce::AlertWindow> deleter(alert);
                if (result != 1) return;

                const auto requested = juce::File::createLegalFileName(
                    alert->getTextEditorContents("name").trim());
                if (requested.isEmpty() || requested == f.getFileNameWithoutExtension())
                    return;

                auto target = f.getParentDirectory().getChildFile(requested + ".t5seq");
                if (target.existsAsFile())
                {
                    setStatusText("Rename failed: name already exists", true);
                    return;
                }
                if (! f.moveFileTo(target))
                {
                    setStatusText("Rename failed: could not move file", true);
                    return;
                }

                refreshList();
                selectFile(target);
                setStatusText("Renamed to " + requested);
            }), false);
    }

    void selectFile(const juce::File& f)
    {
        for (int i = 0; i < files.size(); ++i)
            if (files[i] == f)
            {
                patternList.selectRow(i);
                return;
            }
    }

    juce::Label      title, saveAsLabel, hintLabel, statusLabel;
    juce::TextButton closeBtn, saveBtn;
    juce::TextEditor nameEditor;
    juce::ListBox    patternList;
    juce::Array<juce::File> files;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequenceLibraryPanel)
};
