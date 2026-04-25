#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <optional>
#include <set>
#include <vector>
#include "GuiHelpers.h"
#include "AxesPanel.h"
#include "../presets/PresetFormat.h"

/**
 * Three-pane preset library overlay.
 *
 *   [ search…                         ] [Import] [Close]
 *   ┌──────────┬─────────────────────────┬─────────────┐
 *   │ Sidebar  │ Preset list             │ Detail card │
 *   │ Bank     │  name / prompt snippet  │ prompts /   │
 *   │ Model    │  modified date          │ axes / tags │
 *   │ Tags     │  …                      │ buttons     │
 *   └──────────┴─────────────────────────┴─────────────┘
 *   Status text
 *
 * The panel only describes selection; all I/O (load/save/delete) is
 * delegated to MainPanel via std::function callbacks. JSON-header parsing
 * (name / model / prompts / tags) happens during refreshLibrary() — light
 * enough to scan the whole library on each open.
 */
class PresetManagerPanel : public juce::Component,
                           private juce::ListBoxModel
{
public:
    struct Entry
    {
        juce::File   file;
        juce::String name;
        juce::String model;
        juce::String promptA;
        juce::String promptB;
        juce::StringArray tags;
        juce::Time modified;
        bool isFactory = false;
        bool hasAxes = false;
        std::array<std::pair<juce::String, float>, 3> axes;
        double sampleRate = 0.0;
        int    numChannels = 0;
        int    numSamples  = 0;
    };

    PresetManagerPanel()
    {
        titleLabel.setText("Preset Library", juce::dontSendNotification);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        addAndMakeVisible(titleLabel);

        configureEditor(searchEditor, "Search name or prompt…");
        searchEditor.onTextChange = [this] { rebuildFiltered(); };
        addAndMakeVisible(searchEditor);

        configureButton(importBtn, kSurface);
        importBtn.onClick = [this] { if (onImportRequested) onImportRequested(); };
        addAndMakeVisible(importBtn);

        configureButton(closeBtn, kSurface);
        closeBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible(closeBtn);

        sidebar.onChanged = [this] { rebuildFiltered(); };
        addAndMakeVisible(sidebar);

        presetList.setModel(this);
        presetList.setColour(juce::ListBox::backgroundColourId, kSurface);
        presetList.setColour(juce::ListBox::outlineColourId, kBorder);
        presetList.setRowHeight(54);
        addAndMakeVisible(presetList);

        detail.onLoadRequested = [this]
        {
            if (selectedEntryIndex < 0 || onLoadRequested == nullptr) return;
            onLoadRequested(allEntries[(size_t) selectedEntryIndex].file);
        };
        detail.onDeleteRequested = [this]
        {
            if (selectedEntryIndex < 0 || onDeleteRequested == nullptr) return;
            const auto& e = allEntries[(size_t) selectedEntryIndex];
            if (e.isFactory) return;
            onDeleteRequested(e.file);
        };
        detail.onTagsCommitted = [this](const juce::StringArray& newTags)
        {
            if (selectedEntryIndex < 0 || onTagsChanged == nullptr) return;
            const auto& e = allEntries[(size_t) selectedEntryIndex];
            if (e.isFactory) return;
            onTagsChanged(e.file, newTags);
        };
        addAndMakeVisible(detail);

        statusLabel.setColour(juce::Label::textColourId, kDim);
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setFont(juce::FontOptions(11.5f));
        addAndMakeVisible(statusLabel);
    }

    void paint(juce::Graphics& g) override
    {
        paintCard(g, getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);

        // Top row: title + search + buttons + close
        auto topRow = area.removeFromTop(28);
        auto titleArea = topRow.removeFromLeft(150);
        titleLabel.setBounds(titleArea.withTrimmedTop(4));
        closeBtn.setBounds(topRow.removeFromRight(72));
        topRow.removeFromRight(6);
        importBtn.setBounds(topRow.removeFromRight(82));
        topRow.removeFromRight(6);
        searchEditor.setBounds(topRow);

        area.removeFromTop(10);

        // Status at bottom
        statusLabel.setBounds(area.removeFromBottom(20));
        area.removeFromBottom(6);

        // Three panes
        sidebar.setBounds(area.removeFromLeft(140));
        area.removeFromLeft(8);
        detail.setBounds(area.removeFromRight(290));
        area.removeFromRight(8);
        presetList.setBounds(area);
    }

    void refreshLibrary()
    {
        allEntries.clear();

        auto factoryDir = PresetFormat::getFactoryPresetsDirectory();
        for (auto& f : PresetFormat::getAllPresetFiles())
        {
            const bool fac = (f.getParentDirectory() == factoryDir) || f.isAChildOf(factoryDir);
            allEntries.push_back(parseEntry(f, fac));
        }

        std::stable_sort(allEntries.begin(), allEntries.end(),
            [](const Entry& a, const Entry& b)
            {
                if (a.isFactory != b.isFactory) return a.isFactory && ! b.isFactory;
                return a.name.compareIgnoreCase(b.name) < 0;
            });

        // Update sidebar's vocabulary based on what's actually in the library
        std::set<juce::String> models, tags;
        for (auto& e : allEntries)
        {
            if (e.model.isNotEmpty()) models.insert(e.model);
            for (auto& t : e.tags)    tags.insert(t);
        }
        sidebar.setVocabulary({ models.begin(), models.end() },
                              { tags.begin(), tags.end() });

        rebuildFiltered();
        setStatusText(allEntries.empty() ? "No presets found" : "");
    }

    void setCurrentPreset(const juce::File& file, const juce::String& name)
    {
        currentFile = file;
        currentName = name;
        // Try to highlight the matching row
        for (size_t i = 0; i < filteredIndices.size(); ++i)
        {
            const auto& e = allEntries[(size_t) filteredIndices[i]];
            if (e.file == file)
            {
                presetList.selectRow((int) i, false, true);
                return;
            }
        }
        // No match: leave previous selection
    }

    void setStatusText(const juce::String& s, bool isError = false)
    {
        statusLabel.setText(s, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId,
                              isError ? juce::Colour(0xffff8a80) : kDim);
    }

    // Callbacks — strictly browse / load / delete / tag-edit. Save flow
    // lives in a separate StatusBar-driven dialog (SavePresetDialog).
    std::function<void(const juce::File&)>                              onLoadRequested;
    std::function<void(const juce::File&)>                              onDeleteRequested;
    std::function<void()>                                                onImportRequested;
    std::function<void()>                                                onCloseRequested;
    std::function<void(const juce::File&, const juce::StringArray&)>    onTagsChanged;

private:
    // ─── Helpers ─────────────────────────────────────────────────────────
    static void configureEditor(juce::TextEditor& e, const juce::String& placeholder)
    {
        e.setTextToShowWhenEmpty(placeholder, kDimmer);
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

    static juce::String snippet(const juce::String& s, int maxLen = 84)
    {
        auto t = s.trim();
        if (t.length() <= maxLen) return t;
        return t.substring(0, maxLen - 1).trimEnd() + juce::String(juce::CharPointer_UTF8("…"));
    }

    Entry parseEntry(const juce::File& file, bool isFactory)
    {
        Entry e;
        e.file = file;
        e.name = file.getFileNameWithoutExtension();
        e.modified = file.getLastModificationTime();
        e.isFactory = isFactory;

        // Read header — only the JSON section, not the audio PCM
        juce::FileInputStream in(file);
        if (! in.openedOk()) return e;
        char magic[4];
        if (in.read(magic, 4) != 4) return e;
        if (juce::String(magic, 4) != "T5YN") return e;
        const auto version = (uint32_t) in.readInt();
        juce::ignoreUnused(version);
        const auto jsonLen = (uint32_t) in.readInt();
        if (jsonLen == 0 || jsonLen > 8 * 1024 * 1024) return e;
        juce::MemoryBlock jsonBlk;
        if (in.readIntoMemoryBlock(jsonBlk, (ssize_t) jsonLen) != (ssize_t) jsonLen) return e;
        const juce::String json(static_cast<const char*>(jsonBlk.getData()),
                                static_cast<size_t>(jsonBlk.getSize()));
        const auto parsed = juce::JSON::parse(json);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr) return e;

        const auto storedName = root->getProperty("name").toString().trim();
        if (storedName.isNotEmpty()) e.name = storedName;

        if (auto* synth = root->getProperty("synth").getDynamicObject())
        {
            e.promptA = synth->getProperty("promptA").toString();
            e.promptB = synth->getProperty("promptB").toString();
            e.model   = synth->getProperty("model").toString().trim();
        }

        if (auto* arr = root->getProperty("tags").getArray())
            for (auto& v : *arr)
            {
                auto t = v.toString().trim();
                if (t.isNotEmpty()) e.tags.addIfNotAlreadyThere(t);
            }

        if (auto* axesArr = root->getProperty("semanticAxes").getArray())
        {
            const auto& labels = AxesPanel::getAxisLabels();
            bool anyAssigned = false;
            for (int i = 0; i < std::min(axesArr->size(), 3); ++i)
                if (auto* ax = (*axesArr)[i].getDynamicObject())
                {
                    const int dropId = (int) ax->getProperty("dropdownId");
                    const auto label = (dropId >= 1 && dropId <= labels.size())
                                         ? labels[dropId - 1]
                                         : juce::String("---");
                    e.axes[(size_t) i] = { label, (float) (double) ax->getProperty("value") };
                    if (! label.isEmpty() && label != "---") anyAssigned = true;
                }
            // Only show the AXES section if at least one slot is actually used.
            e.hasAxes = anyAssigned;
        }

        if (auto* meta = root->getProperty("audio_meta").getDynamicObject())
        {
            e.sampleRate  = (double) meta->getProperty("sampleRate");
            e.numChannels = (int)    meta->getProperty("channels");
            e.numSamples  = (int)    meta->getProperty("numSamples");
        }

        return e;
    }

    void rebuildFiltered()
    {
        filteredIndices.clear();
        const auto needle = searchEditor.getText().trim().toLowerCase();

        for (size_t i = 0; i < allEntries.size(); ++i)
        {
            const auto& e = allEntries[i];

            // Bank filter
            switch (sidebar.activeBank)
            {
                case Sidebar::Bank::Factory: if (! e.isFactory) continue; break;
                case Sidebar::Bank::User:    if (  e.isFactory) continue; break;
                case Sidebar::Bank::All:     break;
            }

            // Model filter (empty selection → all)
            if (! sidebar.selectedModels.empty()
                && sidebar.selectedModels.find(e.model) == sidebar.selectedModels.end())
                continue;

            // Tag filter (empty → all; otherwise need any-of-match)
            if (! sidebar.selectedTags.empty())
            {
                bool any = false;
                for (auto& t : e.tags)
                    if (sidebar.selectedTags.count(t)) { any = true; break; }
                if (! any) continue;
            }

            // Search needle
            if (needle.isNotEmpty())
            {
                const bool nameHit    = e.name.toLowerCase().contains(needle);
                const bool promptHit  = (e.promptA + " " + e.promptB).toLowerCase().contains(needle);
                if (! nameHit && ! promptHit) continue;
            }

            filteredIndices.push_back((int) i);
        }

        presetList.updateContent();

        // Try to keep current selection visible
        for (size_t i = 0; i < filteredIndices.size(); ++i)
        {
            const auto& e = allEntries[(size_t) filteredIndices[i]];
            if (e.file == currentFile)
            {
                presetList.selectRow((int) i, false, true);
                return;
            }
        }
        if (selectedEntryIndex >= 0)
        {
            for (size_t i = 0; i < filteredIndices.size(); ++i)
                if (filteredIndices[i] == selectedEntryIndex)
                {
                    presetList.selectRow((int) i, false, true);
                    return;
                }
        }
        if (! filteredIndices.empty())
            presetList.selectRow(0, false, true);
        else
        {
            selectedEntryIndex = -1;
            detail.clear();
        }
    }

    // ─── Sidebar pane ────────────────────────────────────────────────────
    class Sidebar : public juce::Component
    {
    public:
        enum class Bank { All, Factory, User };
        Bank activeBank = Bank::All;
        std::set<juce::String> selectedModels;
        std::set<juce::String> selectedTags;
        std::function<void()> onChanged;

        void setVocabulary(std::vector<juce::String> models, std::vector<juce::String> tags)
        {
            modelEntries = std::move(models);
            tagEntries   = std::move(tags);

            // Drop selections that no longer exist
            for (auto it = selectedModels.begin(); it != selectedModels.end(); )
                it = std::find(modelEntries.begin(), modelEntries.end(), *it) == modelEntries.end()
                       ? selectedModels.erase(it) : std::next(it);
            for (auto it = selectedTags.begin(); it != selectedTags.end(); )
                it = std::find(tagEntries.begin(), tagEntries.end(), *it) == tagEntries.end()
                       ? selectedTags.erase(it) : std::next(it);

            rebuildLayout();
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            if (items.empty())
                rebuildLayout();   // defensive: paint before first resized()
            for (const auto& it : items)
            {
                if (it.kind == Item::Kind::Header)
                {
                    g.setColour(kDim);
                    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                    g.drawText(it.label, it.rect, juce::Justification::centredLeft, false);
                    g.setColour(kBorder.withAlpha(0.5f));
                    g.drawLine((float) it.rect.getX(), (float) it.rect.getBottom() - 1.0f,
                               (float) it.rect.getRight(), (float) it.rect.getBottom() - 1.0f, 0.5f);
                }
                else
                {
                    const bool active = isItemActive(it);
                    if (active)
                    {
                        g.setColour(kAccent.withAlpha(0.18f));
                        g.fillRect(it.rect);
                        g.setColour(kAccent);
                        g.fillRect(juce::Rectangle<int>(it.rect.getX(), it.rect.getY(), 3, it.rect.getHeight()));
                    }
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::FontOptions(12.0f));
                    auto txt = it.rect.withTrimmedLeft(8);
                    g.drawText(it.label, txt, juce::Justification::centredLeft, true);
                }
            }
        }

        void resized() override
        {
            rebuildLayout();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();
            for (const auto& it : items)
            {
                if (it.kind == Item::Kind::Header) continue;
                if (it.rect.contains(p))
                {
                    handleHit(it);
                    repaint();
                    if (onChanged) onChanged();
                    return;
                }
            }
        }

    private:
        struct Item
        {
            enum class Kind { Header, Bank, Model, Tag };
            Kind kind;
            juce::Rectangle<int> rect;
            juce::String label;
            Bank bank = Bank::All;
        };
        std::vector<Item> items;
        std::vector<juce::String> modelEntries;
        std::vector<juce::String> tagEntries;

        void rebuildLayout()
        {
            items.clear();
            auto area = getLocalBounds().reduced(8);
            const int rowH = 22;
            const int headerH = 18;
            const int gap = 12;

            auto pushHeader = [&](const char* label)
            {
                auto r = area.removeFromTop(headerH);
                items.push_back({ Item::Kind::Header, r, label, Bank::All });
                area.removeFromTop(2);
            };
            auto pushRow = [&](Item::Kind kind, const juce::String& label, Bank bank = Bank::All)
            {
                if (area.getHeight() < rowH) return;
                auto r = area.removeFromTop(rowH);
                items.push_back({ kind, r, label, bank });
            };

            pushHeader("BANK");
            pushRow(Item::Kind::Bank, "All",     Bank::All);
            pushRow(Item::Kind::Bank, "Factory", Bank::Factory);
            pushRow(Item::Kind::Bank, "User",    Bank::User);

            if (! modelEntries.empty())
            {
                area.removeFromTop(gap);
                pushHeader("MODEL");
                for (auto& m : modelEntries) pushRow(Item::Kind::Model, m);
            }

            if (! tagEntries.empty())
            {
                area.removeFromTop(gap);
                pushHeader("TAGS");
                for (auto& t : tagEntries) pushRow(Item::Kind::Tag, t);
            }
        }

        bool isItemActive(const Item& it) const
        {
            switch (it.kind)
            {
                case Item::Kind::Header: return false;
                case Item::Kind::Bank:   return activeBank == it.bank;
                case Item::Kind::Model:  return selectedModels.count(it.label) > 0;
                case Item::Kind::Tag:    return selectedTags.count(it.label) > 0;
            }
            return false;
        }

        void handleHit(const Item& it)
        {
            switch (it.kind)
            {
                case Item::Kind::Header: break;
                case Item::Kind::Bank:   activeBank = it.bank; break;
                case Item::Kind::Model:
                    if (selectedModels.erase(it.label) == 0) selectedModels.insert(it.label);
                    break;
                case Item::Kind::Tag:
                    if (selectedTags.erase(it.label) == 0) selectedTags.insert(it.label);
                    break;
            }
        }
    };
    Sidebar sidebar;

    // ─── Detail pane ─────────────────────────────────────────────────────
    class Detail : public juce::Component
    {
    public:
        std::function<void()> onLoadRequested;
        std::function<void()> onDeleteRequested;
        std::function<void(const juce::StringArray&)> onTagsCommitted;

        Detail()
        {
            configureBtn(loadBtn, kAccent);
            configureBtn(delBtn,  juce::Colour(0xff5c2432));
            loadBtn.onClick = [this] { if (onLoadRequested) onLoadRequested(); };
            delBtn.onClick  = [this] { if (onDeleteRequested) onDeleteRequested(); };
            addAndMakeVisible(loadBtn);
            addAndMakeVisible(delBtn);

            tagInput.setTextToShowWhenEmpty("+ tag", kDimmer);
            tagInput.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.05f));
            tagInput.setColour(juce::TextEditor::textColourId, juce::Colours::white);
            tagInput.setColour(juce::TextEditor::outlineColourId, kBorder);
            tagInput.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
            tagInput.onReturnKey = [this]
            {
                const auto t = tagInput.getText().trim();
                if (t.isEmpty()) return;
                if (locked) { tagInput.setText({}, false); return; }
                tags.addIfNotAlreadyThere(t);
                tagInput.setText({}, false);
                if (onTagsCommitted) onTagsCommitted(tags);
                resized();
                repaint();
            };
            addAndMakeVisible(tagInput);
        }

        void clear()
        {
            entryValid = false;
            name.clear();
            bank.clear();
            promptA.clear(); promptB.clear();
            hasAxes = false;
            sampleRate = 0.0; numChannels = 0; numSamples = 0;
            tags.clear();
            locked = false;
            updateButtonsEnabled();
            resized();
            repaint();
        }

        void setEntry(const Entry& e)
        {
            entryValid = true;
            name = e.name;
            bank = e.isFactory ? "Factory" : "User";
            modified = e.modified;
            promptA = e.promptA;
            promptB = e.promptB;
            hasAxes = e.hasAxes;
            axes = e.axes;
            sampleRate  = e.sampleRate;
            numChannels = e.numChannels;
            numSamples  = e.numSamples;
            tags = e.tags;
            locked = e.isFactory;  // factory tags are read-only
            updateButtonsEnabled();
            resized();
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            paintCard(g, getLocalBounds());
            if (! entryValid)
            {
                g.setColour(kDim);
                g.setFont(juce::FontOptions(13.0f));
                g.drawText("No preset selected", getLocalBounds(), juce::Justification::centred);
                return;
            }

            auto area = getLocalBounds().reduced(12);

            // Title
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawText(name, area.removeFromTop(20), juce::Justification::centredLeft, true);

            // Subline
            g.setColour(kDim);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(bank + (modified.toMilliseconds() > 0
                                 ? juce::String::fromUTF8(" \xc2\xb7 ") + modified.formatted("%Y-%m-%d %H:%M")
                                 : juce::String()),
                       area.removeFromTop(16), juce::Justification::centredLeft, false);
            area.removeFromTop(8);

            // Reserve bottom: action strip + tag chip area + audio row + gaps
            const int actionStripH = 28;
            const int audioRowH    = 16;
            const int tagBlockH    = 72;
            area.removeFromBottom(actionStripH);
            area.removeFromBottom(8);
            const auto tagsRect = area.removeFromBottom(tagBlockH);
            area.removeFromBottom(6);
            const auto audioRow = area.removeFromBottom(audioRowH);
            area.removeFromBottom(6);

            // If the panel is too short to host the section grid, bail
            // gracefully instead of issuing negative-height removeFromTop()s.
            if (area.getHeight() < 60)
            {
                paintTagChips(g, tagsRect);
                g.setColour(kDim);
                g.setFont(juce::FontOptions(11.0f));
                g.drawText("Resize panel for full details", audioRow,
                           juce::Justification::centredLeft, false);
                return;
            }

            // Sections are top-aligned with capped natural heights so prompt
            // sections don't bloat into a wall of whitespace when the body text
            // is short. Excess vertical space drops to the bottom (above the
            // audio/tag area).
            const int promptH = juce::jlimit(50, 90, area.getHeight() / (hasAxes ? 3 : 2) - 4);
            paintSection(g, area.removeFromTop(promptH), "PROMPT A", promptA);
            area.removeFromTop(6);
            paintSection(g, area.removeFromTop(promptH), "PROMPT B", promptB);
            if (hasAxes)
            {
                area.removeFromTop(6);
                const int axesH = juce::jlimit(40, 70, area.getHeight());
                paintAxes(g, area.removeFromTop(axesH));
            }

            // Audio info
            g.setColour(kDim);
            g.setFont(juce::FontOptions(11.0f));
            juce::String audioStr;
            if (numSamples > 0 && sampleRate > 0.0)
            {
                const auto sep = juce::String::fromUTF8(" \xc2\xb7 ");
                const double secs = (double) numSamples / sampleRate;
                audioStr << juce::String(secs, secs < 10.0 ? 2 : 1) << " s" << sep
                         << juce::String((int) std::round(sampleRate / 1000.0)) << " kHz" << sep
                         << (numChannels == 1 ? "mono" : (numChannels == 2 ? "stereo" : juce::String(numChannels) + " ch"));
            }
            else
            {
                audioStr = "no audio";
            }
            g.drawText(audioStr, audioRow, juce::Justification::centredLeft, false);

            paintTagChips(g, tagsRect);
        }

        void resized() override
        {
            const int actionStripH = 28;
            const int tagInputW    = 80;
            const int tagBlockH    = 72;
            auto bounds = getLocalBounds().reduced(12);

            // Action strip at bottom — Load (primary) wide-left, Delete right
            auto strip = bounds.removeFromBottom(actionStripH);
            const int delBtnW = juce::jmin(96, strip.getWidth() / 2);
            delBtn.setBounds(strip.removeFromRight(delBtnW));
            strip.removeFromRight(8);
            loadBtn.setBounds(strip);

            bounds.removeFromBottom(8);
            // The tag chips are painted by paint(); only the input editor
            // (bottom-right of the block) needs Component bounds.
            auto tagsRect = bounds.removeFromBottom(tagBlockH);
            tagInput.setBounds(tagsRect.removeFromBottom(24).removeFromRight(tagInputW));
        }

    private:
        static void configureBtn(juce::TextButton& b, juce::Colour c)
        {
            b.setColour(juce::TextButton::buttonColourId, c);
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }

        void updateButtonsEnabled()
        {
            loadBtn.setEnabled(entryValid);
            delBtn.setEnabled(entryValid && ! locked);
            tagInput.setEnabled(entryValid && ! locked);
        }

        void paintSection(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& body)
        {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(title, r.removeFromTop(14), juce::Justification::centredLeft, false);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText(body.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94") : body,
                             r, juce::Justification::topLeft, 6, 0.9f);
        }

        void paintAxes(juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("AXES", r.removeFromTop(14), juce::Justification::centredLeft, false);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(12.0f));
            const auto emDash = juce::String::fromUTF8("\xe2\x80\x94");
            for (int i = 0; i < 3; ++i)
            {
                auto row = r.removeFromTop(16);
                if (row.isEmpty()) break;
                const auto& label = axes[(size_t) i].first;
                // Slot is unset when label == "---" (placeholder) or empty
                const bool unset = label.isEmpty() || label == "---";
                g.drawText(unset ? emDash : label,
                           row.removeFromLeft(row.getWidth() - 60),
                           juce::Justification::centredLeft, true);
                g.drawText(unset ? juce::String() : juce::String(axes[(size_t) i].second, 2),
                           row, juce::Justification::centredRight, false);
            }
        }

        void paintTagChips(juce::Graphics& g, juce::Rectangle<int> r)
        {
            chipRects.clear();
            g.setColour(kDim);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("TAGS", r.removeFromTop(14), juce::Justification::centredLeft, false);

            r.removeFromBottom(24);  // input row
            int x = r.getX();
            int y = r.getY();
            const int rowH = 22;
            const int gapX = 4;
            const int gapY = 4;

            for (int i = 0; i < tags.size(); ++i)
            {
                const auto& t = tags[i];
                const int chipW = juce::Font(juce::FontOptions(11.0f)).getStringWidth(t) + (locked ? 14 : 28);
                if (x + chipW > r.getRight())
                {
                    x = r.getX();
                    y += rowH + gapY;
                    if (y + rowH > r.getBottom()) break;
                }
                const juce::Rectangle<int> chip(x, y, chipW, rowH - 2);
                g.setColour(kSurface.brighter(0.08f));
                g.fillRoundedRectangle(chip.toFloat(), 3.0f);
                g.setColour(kBorder);
                g.drawRoundedRectangle(chip.toFloat(), 3.0f, 1.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(11.0f));
                auto labelArea = chip.reduced(7, 2);
                if (! locked) labelArea.removeFromRight(14);
                g.drawText(t, labelArea, juce::Justification::centredLeft, false);
                if (! locked)
                {
                    const juce::Rectangle<int> closeBox(chip.getRight() - 16, chip.getY(), 16, chip.getHeight());
                    g.setColour(kDimmer);
                    g.drawText(juce::String::fromUTF8("\xc3\x97"), closeBox, juce::Justification::centred, false);
                }
                chipRects.push_back({ chip, i });
                x += chipW + gapX;
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (locked) return;
            const auto p = e.getPosition();
            for (const auto& cr : chipRects)
            {
                // The × hitbox is the right ~16 px of each chip
                const auto closeBox = juce::Rectangle<int>(
                    cr.bounds.getRight() - 16, cr.bounds.getY(), 16, cr.bounds.getHeight());
                if (closeBox.contains(p))
                {
                    if (cr.index >= 0 && cr.index < tags.size())
                    {
                        tags.remove(cr.index);
                        if (onTagsCommitted) onTagsCommitted(tags);
                        resized();
                        repaint();
                    }
                    return;
                }
            }
        }

        bool entryValid = false;
        bool hasAxes = false;
        bool locked = false;
        juce::String name, bank, promptA, promptB;
        juce::Time modified;
        std::array<std::pair<juce::String, float>, 3> axes;
        double sampleRate = 0.0;
        int numChannels = 0;
        int numSamples = 0;
        juce::StringArray tags;

        struct ChipRect { juce::Rectangle<int> bounds; int index; };
        std::vector<ChipRect> chipRects;

        juce::TextButton loadBtn { "Load" };
        juce::TextButton delBtn  { "Delete" };
        juce::TextEditor tagInput;
    };
    Detail detail;

    // ─── ListBoxModel ────────────────────────────────────────────────────
    int getNumRows() override { return (int) filteredIndices.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow(rowNumber, (int) filteredIndices.size())) return;
        const auto& e = allEntries[(size_t) filteredIndices[(size_t) rowNumber]];

        if (rowIsSelected)
        {
            g.setColour(kAccent.withAlpha(0.22f));
            g.fillRect(0, 0, width, height);
        }
        if (e.file == currentFile)
        {
            g.setColour(kSeqCol);
            g.fillRect(0, 0, 3, height);
        }
        g.setColour(kBorder.withAlpha(0.35f));
        g.drawLine(0.0f, (float) height - 1.0f, (float) width, (float) height - 1.0f, 0.5f);

        auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(10, 6);
        auto topRow = bounds.removeFromTop(20);
        // Reserve at most half the row width for the date column, and only if
        // the row has room for a meaningful name first.
        const int dateW = topRow.getWidth() > 180
                              ? juce::jmin(90, topRow.getWidth() / 2)
                              : 0;
        auto dateArea = topRow.removeFromRight(dateW);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.5f));
        g.drawText(e.name, topRow, juce::Justification::centredLeft, true);
        if (dateW > 0)
        {
            g.setColour(kDimmer);
            g.setFont(juce::FontOptions(10.5f));
            g.drawText(e.modified.formatted("%Y-%m-%d"), dateArea, juce::Justification::centredRight, false);
        }

        bounds.removeFromTop(2);
        g.setColour(kDim);
        g.setFont(juce::FontOptions(11.5f));
        const auto snip = snippet(e.promptA.isNotEmpty() ? e.promptA : e.promptB, 96);
        g.drawText(snip.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94") : snip,
                   bounds.removeFromTop(14), juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (! juce::isPositiveAndBelow(lastRowSelected, (int) filteredIndices.size()))
        {
            selectedEntryIndex = -1;
            detail.clear();
            return;
        }
        selectedEntryIndex = filteredIndices[(size_t) lastRowSelected];
        detail.setEntry(allEntries[(size_t) selectedEntryIndex]);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow(row, (int) filteredIndices.size())) return;
        if (onLoadRequested) onLoadRequested(allEntries[(size_t) filteredIndices[(size_t) row]].file);
    }

    // ─── State ───────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::TextEditor searchEditor;
    juce::TextButton importBtn { "Import" };
    juce::TextButton closeBtn  { "Close" };
    juce::Label statusLabel;
    juce::ListBox presetList;

    std::vector<Entry> allEntries;
    std::vector<int>   filteredIndices;
    int selectedEntryIndex = -1;
    juce::File   currentFile;
    juce::String currentName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManagerPanel)
};
