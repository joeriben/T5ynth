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
                           private juce::ListBoxModel,
                           private juce::KeyListener
{
public:
    struct Entry
    {
        juce::File   file;
        juce::String name;
        juce::String model;        // shortened display name (SA Open 1.0, …)
        juce::String engineMode;   // "Sampler" / "Wavetable"
        juce::String seqMode;      // "Off" / "Step" / "Gen" / "Step + Gen"
        juce::String promptA;
        juce::String promptB;
        juce::StringArray tags;
        juce::Time modified;
        bool isFactory = false;
        /** Bank label: "Factory" for any factory file, "Default" for user
         *  presets at the user-dir root, or the subdir name (joined with
         *  "/" if nested) for presets in user-created subdirectories. */
        juce::String bank;
        bool hasAxes = false;
        std::array<std::pair<juce::String, float>, 3> axes;
        double sampleRate = 0.0;
        int    numChannels = 0;
        int    numSamples  = 0;
        float  inferenceMs = 0.0f;   // 0 → unknown / legacy preset
    };

    /** Display label for the user-presets-root pseudo-bank. */
    static const juce::String& kRootUserBank()
    {
        static const juce::String s = "Default";
        return s;
    }

    PresetManagerPanel()
    {
        titleLabel.setText("Preset Library", juce::dontSendNotification);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        addAndMakeVisible(titleLabel);

        configureEditor(searchEditor, "Search name or prompt...");
        searchEditor.setEscapeAndReturnKeysConsumed(false);
        searchEditor.addKeyListener(this);
        searchEditor.onTextChange = [this] { rebuildFiltered(); };
        addAndMakeVisible(searchEditor);

        configureButton(importBtn, kSurface);
        importBtn.onClick = [this] { if (onImportRequested) onImportRequested(); };
        addAndMakeVisible(importBtn);

        // Top-right × icon — replaces the wider "Close" text button
        closeIconBtn.setButtonText(juce::String::fromUTF8("\xc3\x97"));
        closeIconBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeIconBtn.setColour(juce::TextButton::textColourOffId, kDim);
        closeIconBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
        closeIconBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible(closeIconBtn);

        // Bottom-right Cancel button — explicit dismiss alongside the × icon
        // and Esc, matching the SavePresetDialog convention.
        configureButton(cancelBtn, kSurface);
        cancelBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible(cancelBtn);

        setWantsKeyboardFocus(true);
        addKeyListener(this);

        sidebar.onChanged = [this] { rebuildFiltered(); };
        addAndMakeVisible(sidebar);

        presetList.setModel(this);
        presetList.setColour(juce::ListBox::backgroundColourId, kSurface);
        presetList.setColour(juce::ListBox::outlineColourId, kBorder);
        presetList.setRowHeight(54);
        presetList.addKeyListener(this);
        addAndMakeVisible(presetList);

        detail.onLoadRequested = [this]
        {
            if (selectedEntryIndex < 0 || onLoadRequested == nullptr) return;
            onLoadRequested(allEntries[(size_t) selectedEntryIndex].file);
        };
        detail.onTagsCommitted = [this](const juce::StringArray& newTags)
        {
            if (selectedEntryIndex < 0 || onTagsChanged == nullptr) return;
            const auto& e = allEntries[(size_t) selectedEntryIndex];
            if (e.isFactory) return;
            onTagsChanged(e.file, newTags);
        };
        detail.installEscListener(this);
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

        // Top row: title + search + Import + × close icon
        auto topRow = area.removeFromTop(28);
        auto titleArea = topRow.removeFromLeft(150);
        titleLabel.setBounds(titleArea.withTrimmedTop(4));
        closeIconBtn.setBounds(topRow.removeFromRight(28));
        topRow.removeFromRight(8);
        importBtn.setBounds(topRow.removeFromRight(82));
        topRow.removeFromRight(8);
        searchEditor.setBounds(topRow);

        area.removeFromTop(10);

        // Bottom row: status label (stretchy left) + Cancel button (right)
        auto footer = area.removeFromBottom(28);
        cancelBtn.setBounds(footer.removeFromRight(96));
        footer.removeFromRight(8);
        statusLabel.setBounds(footer);
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
                if (a.bank != b.bank)           return a.bank.compareIgnoreCase(b.bank) < 0;
                return a.name.compareIgnoreCase(b.name) < 0;
            });

        refreshSidebarVocabulary();

        rebuildFiltered();
        setStatusText(allEntries.empty() ? "No presets found" : "");
    }

    void updateTagsForFile(const juce::File& file, const juce::StringArray& tags)
    {
        const int updatedIndex = findEntryIndexForFile(file);
        if (updatedIndex < 0) return;

        allEntries[(size_t) updatedIndex].tags = normaliseTags(tags);
        refreshSidebarVocabulary();
        rebuildFiltered();

        for (size_t row = 0; row < filteredIndices.size(); ++row)
        {
            if (filteredIndices[row] == updatedIndex)
            {
                presetList.selectRow((int) row, false, true);
                detail.setEntry(allEntries[(size_t) updatedIndex]);
                presetList.repaint();
                return;
            }
        }

        if (selectedEntryIndex == updatedIndex)
        {
            selectedEntryIndex = -1;
            detail.clear();
        }
        presetList.repaint();
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
    std::function<void(const juce::File&)>                              onRenameRequested;
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

    /** Bank label derived from a preset's location on disk:
     *    factory file       → "Factory"
     *    user dir root      → "Default"
     *    user dir subdir(s) → joined subdirectory path, e.g. "Ambient" or
     *                         "Drones/Long".
     *  This is the ONLY place that maps disk layout to bank semantics so
     *  the same logic can be reused by Save (where target dir is derived
     *  from a chosen bank name). */
    static juce::String computeBankLabel(const juce::File& file, bool isFactory)
    {
        if (isFactory) return "Factory";
        const auto userRoot = PresetFormat::getUserPresetsDirectory();
        auto parent = file.getParentDirectory();
        if (parent == userRoot) return kRootUserBank();
        const auto rel = parent.getRelativePathFrom(userRoot);
        return rel.isEmpty() ? kRootUserBank() : rel.replace("\\", "/");
    }

    /** Map full HuggingFace model IDs to short labels used in the GUI
     *  (matches PromptPanel's switchbox labels). Unknown IDs come back
     *  unchanged so future models still display, just longer. */
    static juce::String shortenModelName(const juce::String& full)
    {
        const auto lc = full.toLowerCase();
        if (lc.contains("stable-audio-open-small")) return "SA Small";
        if (lc.contains("stable-audio-open"))       return "SA Open 1.0";
        if (lc.contains("audioldm"))                return "AudioLDM2";
        return full;
    }

    static juce::StringArray normaliseTags(const juce::StringArray& input)
    {
        juce::StringArray tags = input;
        tags.trim();
        tags.removeEmptyStrings();
        tags.removeDuplicates(true);
        return tags;
    }

    int findEntryIndexForFile(const juce::File& file) const
    {
        for (size_t i = 0; i < allEntries.size(); ++i)
            if (allEntries[i].file == file)
                return (int) i;

        return -1;
    }

    void refreshSidebarVocabulary()
    {
        // Sidebar vocabulary — banks come from actual disk layout, models and
        // tags from saved preset metadata. Tags remain user-editable state; the
        // Save dialog is where heuristic suggestions are proposed.
        std::set<juce::String> banks, models, tags;
        for (auto& e : allEntries)
        {
            if (e.bank.isNotEmpty())  banks.insert(e.bank);
            if (e.model.isNotEmpty()) models.insert(e.model);
            for (auto& t : e.tags)    tags.insert(t);
        }
        sidebar.setVocabulary({ banks.begin(),  banks.end()  },
                              { models.begin(), models.end() },
                              { tags.begin(),   tags.end()   });
    }

    Entry parseEntry(const juce::File& file, bool isFactory)
    {
        Entry e;
        e.file = file;
        e.name = file.getFileNameWithoutExtension();
        e.modified = file.getLastModificationTime();
        e.isFactory = isFactory;
        e.bank = computeBankLabel(file, isFactory);

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
        if (storedName.isNotEmpty() && storedName != "T5ynth Export")
            e.name = storedName;

        if (auto* synth = root->getProperty("synth").getDynamicObject())
        {
            e.promptA = synth->getProperty("promptA").toString();
            e.promptB = synth->getProperty("promptB").toString();
            const auto rawModel = synth->getProperty("model").toString().trim();
            e.model = shortenModelName(rawModel);
            // synth.inferenceMs is optional — added to format after the
            // initial v3 release; older presets simply have 0.
            e.inferenceMs = (float) (double) synth->getProperty("inferenceMs");
        }

        if (auto* eng = root->getProperty("engine").getDynamicObject())
        {
            const auto m = eng->getProperty("mode").toString();
            if (m == "wavetable")    e.engineMode = "Wavetable";
            else if (m == "sampler") e.engineMode = "Sampler";
            else if (m.isNotEmpty()) e.engineMode = m;
        }

        // Sequencer mode = combination of step seq + generative seq enables.
        // The fields land in two different JSON blocks, so we derive a
        // single human-readable string here.
        bool stepEnabled = false, genEnabled = false;
        if (auto* seq = root->getProperty("sequencer").getDynamicObject())
            stepEnabled = (bool) seq->getProperty("enabled");
        if (auto* gseq = root->getProperty("generativeSeq").getDynamicObject())
            genEnabled  = (bool) gseq->getProperty("enabled");
        if (stepEnabled && genEnabled) e.seqMode = "Step + Gen";
        else if (stepEnabled)          e.seqMode = "Step";
        else if (genEnabled)           e.seqMode = "Gen";
        else                           e.seqMode = "Off";

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

            // Bank filter (empty active = show all)
            if (! sidebar.activeBank.isEmpty() && e.bank != sidebar.activeBank)
                continue;

            // Model filter (XOR — empty active = show all)
            if (! sidebar.activeModel.isEmpty() && e.model != sidebar.activeModel)
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
        /** Bank filter — empty string means "all banks". */
        juce::String activeBank;
        /** Model filter — empty string means "all models" (XOR with the
         *  rows in the MODEL section; clicking the active row deselects). */
        juce::String activeModel;
        std::set<juce::String> selectedTags;
        std::function<void()> onChanged;

        void setVocabulary(std::vector<juce::String> banks,
                           std::vector<juce::String> models,
                           std::vector<juce::String> tags)
        {
            bankEntries  = std::move(banks);
            modelEntries = std::move(models);
            tagEntries   = std::move(tags);

            // Drop selections that no longer exist
            if (! activeBank.isEmpty()
                && std::find(bankEntries.begin(), bankEntries.end(), activeBank) == bankEntries.end())
                activeBank.clear();
            if (! activeModel.isEmpty()
                && std::find(modelEntries.begin(), modelEntries.end(), activeModel) == modelEntries.end())
                activeModel.clear();
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
                    g.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
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
                    // Bank/Model entries with an empty label are the "show all" marker.
                    const bool isAllRow = it.label.isEmpty()
                        && (it.kind == Item::Kind::Bank || it.kind == Item::Kind::Model);
                    g.drawText(isAllRow ? juce::String("All") : it.label,
                               txt, juce::Justification::centredLeft, true);
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
            juce::String label;     // for Bank / Model / Tag this IS the value
        };
        std::vector<Item> items;
        std::vector<juce::String> bankEntries;
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
                items.push_back({ Item::Kind::Header, r, label });
                area.removeFromTop(2);
            };
            auto pushRow = [&](Item::Kind kind, const juce::String& label)
            {
                if (area.getHeight() < rowH) return;
                auto r = area.removeFromTop(rowH);
                items.push_back({ kind, r, label });
            };

            pushHeader("BANK");
            // "All" is represented by an empty `label` so activeBank.isEmpty()
            // means "no filter".
            pushRow(Item::Kind::Bank, "");
            for (auto& b : bankEntries) pushRow(Item::Kind::Bank, b);

            if (! modelEntries.empty())
            {
                area.removeFromTop(gap);
                pushHeader("MODEL");
                // "All" sentinel for the Model section too — empty label
                // means "no model filter". Same XOR semantics as Bank.
                pushRow(Item::Kind::Model, "");
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
                case Item::Kind::Bank:   return activeBank  == it.label;
                case Item::Kind::Model:  return activeModel == it.label;
                case Item::Kind::Tag:    return selectedTags.count(it.label) > 0;
            }
            return false;
        }

        void handleHit(const Item& it)
        {
            switch (it.kind)
            {
                case Item::Kind::Header: break;
                case Item::Kind::Bank:
                    // XOR with toggle-off: clicking the active row deselects.
                    activeBank  = (activeBank  == it.label) ? juce::String() : it.label;
                    break;
                case Item::Kind::Model:
                    activeModel = (activeModel == it.label) ? juce::String() : it.label;
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
        std::function<void(const juce::StringArray&)> onTagsCommitted;

        Detail()
        {
            configureBtn(loadBtn, kAccent);
            loadBtn.onClick = [this] { if (onLoadRequested) onLoadRequested(); };
            addAndMakeVisible(loadBtn);

            tagInput.setTextToShowWhenEmpty("+ tag", kDimmer);
            tagInput.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.05f));
            tagInput.setColour(juce::TextEditor::textColourId, juce::Colours::white);
            tagInput.setColour(juce::TextEditor::outlineColourId, kBorder);
            tagInput.setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
            tagInput.setEscapeAndReturnKeysConsumed(false);
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

        /** Forward Esc keystrokes from the tag editor to the panel-level
         *  KeyListener so the overlay can be dismissed even while typing. */
        void installEscListener(juce::KeyListener* l)
        {
            tagInput.addKeyListener(l);
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
            bank = e.bank.isNotEmpty() ? e.bank : (e.isFactory ? "Factory" : "User");
            modified = e.modified;
            promptA = e.promptA;
            promptB = e.promptB;
            hasAxes = e.hasAxes;
            axes = e.axes;
            sampleRate  = e.sampleRate;
            numChannels = e.numChannels;
            numSamples  = e.numSamples;
            inferenceMs = e.inferenceMs;
            model       = e.model;
            engineMode  = e.engineMode;
            seqMode     = e.seqMode;
            tags = e.tags;
            locked = e.isFactory;  // factory tags are read-only
            cachedPromptW = -1;    // force recompute on next paint
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
            area.removeFromTop(10);

            // ── META block — compact label/value rows. Always rendered so the
            //    detail card has a stable shape regardless of which fields a
            //    given preset happens to expose. ──
            const std::pair<const char*, juce::String> metaRows[] = {
                { "MODEL",  model.isNotEmpty() ? model : juce::String() },
                { "ENGINE", engineMode },
                { "SEQ",    seqMode },
                { "AUDIO",  audioInfoString() },
                { "INFER",  inferenceString() },
            };
            for (auto& row : metaRows)
                paintMetaRow(g, area.removeFromTop(16), row.first, row.second);

            area.removeFromTop(8);

            // Reserve bottom: action strip + tag-input row + chip area
            const int actionStripH = 32;
            const int tagInputH    = 24;
            area.removeFromBottom(actionStripH);
            area.removeFromBottom(8);
            const auto tagInputRow = area.removeFromBottom(tagInputH);
            area.removeFromBottom(6);

            // Compute impulse-section heights from the actual wrapped text so
            // there is no leftover gap between IMPULSE A's body and IMPULSE B's
            // header. Caps prevent extreme prompts from squeezing the tags.
            // Heights are cached and only recomputed when the available width
            // changes (TextLayout::createLayout is the most expensive call in
            // this paint and it dominates the click-to-redraw latency for
            // libraries with even a handful of presets).
            const int promptW = area.getWidth() - 6;
            const int headerH = 14;
            if (promptW != cachedPromptW)
            {
                cachedPromptHeightA = juce::jlimit(14, 80, wrappedTextHeight(promptA, promptW, 12.0f));
                cachedPromptHeightB = juce::jlimit(14, 80, wrappedTextHeight(promptB, promptW, 12.0f));
                cachedPromptW = promptW;
            }
            const int promptBodyA = cachedPromptHeightA;
            const int promptBodyB = cachedPromptHeightB;

            const int axesBlockH  = hasAxes ? (headerH + 3 * 16) : 0;
            const int promptABlock = headerH + promptBodyA + 4;
            const int promptBBlock = headerH + promptBodyB + 4;

            // Tag chip area gets whatever space is left. Reserve a sane
            // minimum so chips have somewhere to go even if a preset has
            // long prompts.
            const int needed = promptABlock + 6 + promptBBlock + (hasAxes ? 6 + axesBlockH : 0) + 8 + headerH;
            const int tagsBlockH = juce::jmax(60, area.getHeight() - needed);

            paintSection(g, area.removeFromTop(promptABlock), "IMPULSE A", promptA);
            area.removeFromTop(6);
            paintSection(g, area.removeFromTop(promptBBlock), "IMPULSE B", promptB);
            if (hasAxes)
            {
                area.removeFromTop(6);
                paintAxes(g, area.removeFromTop(axesBlockH));
            }
            area.removeFromTop(8);

            // TAGS section — header + chips fill remainder.
            auto tagsRect = area.removeFromTop(juce::jmax(headerH + 4, area.getHeight()));
            paintTagChips(g, tagsRect);

            juce::ignoreUnused(tagInputRow, tagsBlockH);
        }

        void resized() override
        {
            const int actionStripH = 32;
            const int tagInputW    = juce::jmin(120, getLocalBounds().getWidth() / 3);
            const int tagInputH    = 24;
            auto bounds = getLocalBounds().reduced(12);

            // Action strip at bottom — Load is the only primary action.
            // Delete / Rename live in the right-click context menu so that
            // the destructive action can never be hit by accident from a
            // mis-aimed Load click.
            auto strip = bounds.removeFromBottom(actionStripH);
            loadBtn.setBounds(strip);

            bounds.removeFromBottom(8);
            // The tag chips are painted by paint(); only the input editor
            // (its own row, full-width) needs Component bounds.
            auto tagInputRow = bounds.removeFromBottom(tagInputH);
            tagInput.setBounds(tagInputRow.removeFromRight(tagInputW));
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
            tagInput.setEnabled(entryValid && ! locked);
        }

        /** Compact label/value row used by the META block. */
        static void paintMetaRow(juce::Graphics& g, juce::Rectangle<int> r,
                                 const juce::String& label, const juce::String& value)
        {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(label, r.removeFromLeft(56), juce::Justification::centredLeft, false);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(11.5f));
            const auto display = value.isEmpty()
                                    ? juce::String::fromUTF8("\xe2\x80\x94")
                                    : value;
            g.drawText(display, r, juce::Justification::centredLeft, true);
        }

        juce::String audioInfoString() const
        {
            if (numSamples <= 0 || sampleRate <= 0.0) return {};
            const auto sep = juce::String::fromUTF8(" \xc2\xb7 ");
            const double secs = (double) numSamples / sampleRate;
            juce::String s;
            s << juce::String(secs, secs < 10.0 ? 2 : 1) << " s" << sep
              << juce::String((int) std::round(sampleRate / 1000.0)) << " kHz" << sep
              << (numChannels == 1 ? "mono" : (numChannels == 2 ? "stereo" : juce::String(numChannels) + " ch"));
            return s;
        }

        juce::String inferenceString() const
        {
            if (inferenceMs <= 0.0f) return {};
            const float secs = inferenceMs / 1000.0f;
            return juce::String(secs, secs < 10.0f ? 1 : 0) + " s";
        }

        /** Width-aware wrapped-text height in px, computed via JUCE TextLayout.
         *  Used to size IMPULSE A/B sections to their actual content so there
         *  is no stretched whitespace between them. */
        static int wrappedTextHeight(const juce::String& text, int width, float fontSize)
        {
            if (text.isEmpty()) return juce::roundToInt(fontSize * 1.3f);
            juce::AttributedString s;
            s.append(text, juce::Font(juce::FontOptions(fontSize)), juce::Colours::white);
            s.setWordWrap(juce::AttributedString::byWord);
            s.setJustification(juce::Justification::topLeft);
            juce::TextLayout layout;
            layout.createLayout(s, (float) juce::jmax(20, width));
            return juce::jmax(juce::roundToInt(fontSize * 1.3f),
                              juce::roundToInt(std::ceil(layout.getHeight())));
        }

        void paintSection(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& body)
        {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
            g.drawText(title, r.removeFromTop(14), juce::Justification::centredLeft, false);
            const auto displayBody = body.isEmpty()
                                        ? juce::String::fromUTF8("\xe2\x80\x94")
                                        : body;
            // AttributedString matches the metrics used by wrappedTextHeight,
            // so the section bounds we compute fit the actual rendered glyphs.
            juce::AttributedString s;
            s.append(displayBody, juce::Font(juce::FontOptions(12.0f)), juce::Colours::white);
            s.setWordWrap(juce::AttributedString::byWord);
            s.setJustification(juce::Justification::topLeft);
            s.draw(g, r.toFloat());
        }

        void paintAxes(juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
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
            g.setFont(juce::FontOptions(kUiLabelFontMin, juce::Font::bold));
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
        juce::String model, engineMode, seqMode;
        juce::Time modified;
        std::array<std::pair<juce::String, float>, 3> axes;
        double sampleRate = 0.0;
        int numChannels = 0;
        int numSamples = 0;
        float inferenceMs = 0.0f;
        juce::StringArray tags;

        // Cached prompt heights — invalidated on setEntry or when the
        // available paint width changes. Avoids running TextLayout twice
        // per paint frame.
        int cachedPromptW = -1;
        int cachedPromptHeightA = 14;
        int cachedPromptHeightB = 14;

        struct ChipRect { juce::Rectangle<int> bounds; int index; };
        std::vector<ChipRect> chipRects;

        juce::TextButton loadBtn { "Load" };
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
            g.setFont(juce::FontOptions(kUiLabelFontMin));
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

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;     // right-click only
        if (! juce::isPositiveAndBelow(row, (int) filteredIndices.size())) return;
        presetList.selectRow(row, false, true);
        showRowContextMenu(filteredIndices[(size_t) row]);
    }

    void backgroundClicked(const juce::MouseEvent&) override
    {
        // No-op; required by ListBoxModel signature compatibility.
    }

    void showRowContextMenu(int entryIndex)
    {
        if (! juce::isPositiveAndBelow(entryIndex, (int) allEntries.size())) return;
        const auto& entry = allEntries[(size_t) entryIndex];
        const auto file = entry.file;
        const bool isFactory = entry.isFactory;

        juce::PopupMenu menu;
        menu.addItem(1, juce::String::fromUTF8("Rename\xe2\x80\xa6"), ! isFactory);
        menu.addItem(2, "Delete",                                     ! isFactory);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetList),
            [this, file](int result)
            {
                switch (result)
                {
                    case 1: if (onRenameRequested) onRenameRequested(file); break;
                    case 2: if (onDeleteRequested) onDeleteRequested(file); break;
                    default: break;
                }
            });
    }

    // ─── KeyListener ─────────────────────────────────────────────────────
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onCloseRequested) onCloseRequested();
            return true;
        }
        return false;
    }

    // ─── State ───────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::TextEditor searchEditor;
    juce::TextButton importBtn { "Import" };
    juce::TextButton closeIconBtn;     // top-right × icon
    juce::TextButton cancelBtn { "Cancel" };
    juce::Label statusLabel;
    juce::ListBox presetList;

    std::vector<Entry> allEntries;
    std::vector<int>   filteredIndices;
    int selectedEntryIndex = -1;
    juce::File   currentFile;
    juce::String currentName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManagerPanel)
};
