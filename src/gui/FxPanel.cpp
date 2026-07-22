#include "FxPanel.h"
#include "../dsp/BlockParams.h"
#include "../PluginProcessor.h"
#include "MidiLearnMenu.h"

static juce::String fmtMs(double v)
{
    int ms = juce::roundToInt(v);
    if (ms >= 1000) return juce::String(v / 1000.0, 2) + "s";
    return juce::String(ms) + "ms";
}
static juce::String fmtF2(double v)  { return juce::String(v, 2); }
static juce::String fmtF3(double v)  { return juce::String(v, 3); }

static juce::String fmtDampPct(double v)
{
    // Damp is a normalized 0..1 trim, NOT a single cutoff: the DSP adds a
    // per-mode intrinsic baseline rolloff (Tape darker than Digital at the same
    // value, and it compounds per feedback pass), so a kHz readout would
    // misrepresent it. Show the knob amount instead.
    return juce::String(juce::roundToInt(v * 100.0)) + "%";
}

// ── Family tables ────────────────────────────────────────────────────────────
// Each family cell in the type switchboxes owns a dropdown listing exactly its
// own variants. The tables give the menu ORDER (mildest -> wildest, which is not
// the parameter's index order) and the short cell labels.
static const int kTapeFamily[] = { DelayType::Tape, DelayType::TapeWarm,
                                   DelayType::TapeWild, DelayType::TapeOld };
static const int kBbdFamily[]  = { DelayType::BbdClean, DelayType::Bbd,
                                   DelayType::BbdDegraded };
static const int kPlateFamily[] = { ReverbType::Dark, ReverbType::Medium, ReverbType::Bright };
static const int kAlgoFamily[]  = { ReverbType::Algo, ReverbType::AlgoPlus };

// Cell labels for the active variant. Indexed by DelayType::character().
static const char* kTapeVariantLbls[] = { "Clean", "Warm", "Wild", "Old" };
static const char* kBbdVariantLbls[]  = { "Warm", "Clean", "Degraded" };

FxPanel::FxPanel(juce::AudioProcessorValueTreeState& apvts, T5ynthProcessor& processor)
    : processorRef(processor)
{
    // ══════════ DELAY section ══════════
    paintSectionHeader(delayHeader, "DELAY", kFxCol);
    addAndMakeVisible(delayHeader);

    // Delay type switchbox: 5 cells [OFF][Dig][PP][Tape][BBD].
    //
    // Tape and BBD are FAMILIES, not single voicings — each has character variants.
    // Those variants used to be reached by clicking the cell again and again
    // (Tp1→Tp2→Tp3→Tp4), which hid four states behind one button and was the only
    // place in this UI that worked that way. Each family cell now opens its OWN
    // dropdown instead: one dropdown per category, listing only that category's
    // variants. Off/Digital/Ping-Pong have no variants, so they stay plain cells.
    //
    // All 10 choices map to the flat delayType APVTS param via delayTypeHidden.
    {
        juce::StringArray delayTypeItems;
        for (const auto& e : DelayType::kEntries) delayTypeItems.add(e.label);
        delayTypeHidden.addItemList(delayTypeItems, 1);
    }

    // Update toggle states + family-cell labels whenever the APVTS selection
    // changes. A family cell shows the FAMILY name while another type is active
    // and the ACTIVE VARIANT's name while its own family is playing, so the panel
    // always states which character is running. Full name in the tooltip.
    delayTypeHidden.onChange = [this]
    {
        const int dt       = delayTypeHidden.getSelectedId() - 1;
        const int baseType = DelayType::baseMode(dt);
        for (int i = 0; i < kNumDelayBtns; ++i)
            delayTypeBtns[i].setToggleState(i == baseType, juce::dontSendNotification);

        const bool tapeOn = (baseType == DelayType::Tape);
        const bool bbdOn  = (baseType == DelayType::Bbd);
        delayTypeBtns[3].setButtonText(tapeOn ? kTapeVariantLbls[DelayType::character(dt)] : "Tape");
        delayTypeBtns[4].setButtonText(bbdOn  ? kBbdVariantLbls[DelayType::character(dt)]   : "BBD");
        delayTypeBtns[3].setTooltip(tapeOn ? juce::String("Tape echo - ") + kTapeVariantLbls[DelayType::character(dt)]
                                           : juce::String("Tape echo - pick a character"));
        delayTypeBtns[4].setTooltip(bbdOn  ? juce::String("Bucket-brigade - ") + kBbdVariantLbls[DelayType::character(dt)]
                                           : juce::String("Bucket-brigade - pick a character"));
        updateVisibility();
    };

    static const char* delayLabels[] = { "OFF", "Dig", "PP", "Tape", "BBD" };
    for (int i = 0; i < kNumDelayBtns; ++i)
    {
        delayTypeBtns[i].setButtonText(delayLabels[i]);
        styleSwitchButton(delayTypeBtns[i], kFxCol);
        // The toggle state is driven by delayTypeHidden.onChange, never by the
        // click itself: a family click only OPENS a menu, and dismissing that
        // menu must leave the switchbox exactly as it was.
        delayTypeBtns[i].setClickingTogglesState(false);
        addAndMakeVisible(delayTypeBtns[i]);
    }
    // Off / Digital / Ping-Pong: no variants, simple select.
    for (int i = 0; i < 3; ++i)
        delayTypeBtns[i].onClick = [this, i] { delayTypeHidden.setSelectedId(i + 1); };
    // Tape / BBD: the cell IS the dropdown for its family.
    delayTypeBtns[3].onClick = [this] { showDelayFamilyMenu(3, kTapeFamily, 4); };
    delayTypeBtns[4].onClick = [this] { showDelayFamilyMenu(4, kBbdFamily,  3); };

    delayTimeRow = std::make_unique<SliderRow>("Time", fmtMs, kFxCol);
    delayFbRow   = std::make_unique<SliderRow>("FB",   fmtF2, kFxCol);
    delayDampRow = std::make_unique<SliderRow>("Damp", fmtDampPct, kFxCol);
    delayMixRow  = std::make_unique<SliderRow>("Mix",  fmtF3, kFxCol);

    // Unified label-band look (accent@0.7 + light text, like the RESYNTH
    // left-title). Time/Division carry no text label — the clock band stands in.
    for (auto* r : { delayFbRow.get(), delayDampRow.get(), delayMixRow.get() })
        r->setLabelAsBand(true);

    // Division row swaps in for the Time slider when ClockMode == Sync.
    // Same screen rect, label "Time", but the value formatter returns
    // a musical-division name and the slider is stepped 0..12.
    delayDivisionRow = std::make_unique<SliderRow>("Time",
        [](double v) {
            const int idx = juce::jlimit(0, ClockDivision::kCount - 1,
                                          juce::roundToInt(v));
            return juce::String(ClockDivision::kEntries[idx].label);
        }, kFxCol);
    delayDivisionRow->getSlider().setRange(
        0.0, static_cast<double>(ClockDivision::kCount - 1), 1.0);

    // The Time/Division rows have no text label — the clock button on the
    // left IS their label. We still RESERVE label-width on the row (sized
    // dynamically in resized() to match Damp's natural label width) so
    // the slider track left-edge aligns vertically with Damp below;
    // the clock button is then positioned on top of that reserved area.
    delayTimeRow->getLabel().setText({}, juce::dontSendNotification);
    delayDivisionRow->getLabel().setText({}, juce::dontSendNotification);
    // Lock value column widths so slider track RIGHT-edges align across
    // rows within each pair-column. Hardcoded (not derived in resized()
    // from current value text) because slider value changes don't trigger
    // a relayout — the forced width must accommodate the widest possible
    // text the formatter can produce.
    //   LEFT  column (Time/Division/Damp): 56 fits "1500ms", "1/16T", "20.0k".
    //   RIGHT column (FB/Mix):             48 fits "0.95" and "0.999".
    delayTimeRow->setForcedValueWidth(56);
    delayDivisionRow->setForcedValueWidth(56);
    delayDampRow->setForcedValueWidth(56);
    delayFbRow->setForcedValueWidth(48);
    delayMixRow->setForcedValueWidth(48);

    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayDampRow.get(),
                     delayMixRow.get(), delayDivisionRow.get() })
        addAndMakeVisible(*r);

    // BPM-sync clock button — sits left of the Time row. Hidden ComboBox
    // holds the APVTS state, click cycles it Off ↔ Sync.
    juce::StringArray clockItems;
    for (const auto& e : ClockMode::kEntries) clockItems.add(e.label);
    delayClockModeHidden.addItemList(clockItems, 1);
    delayClockBtn.setLookAndFeel(&delayClockLnf);
    // The clock toggle sits in the Time row's label slot, so it doubles as that
    // row's band: cyan @0.7 when free (matches the sibling label bands below),
    // amber when tempo-synced (the shared sync indicator).
    delayClockLnf.offFill = kFxCol.withAlpha(0.7f);
    delayClockBtn.setClickingTogglesState(false);
    delayClockBtn.onClick = [this] {
        const int cur = delayClockModeHidden.getSelectedId();
        delayClockModeHidden.setSelectedId(cur == 1 ? 2 : 1);
    };
    addAndMakeVisible(delayClockBtn);

    delayTimeA      = std::make_unique<SA>(apvts, PID::delayTime,     delayTimeRow->getSlider());
    delayFbA        = std::make_unique<SA>(apvts, PID::delayFeedback, delayFbRow->getSlider());
    delayDampA      = std::make_unique<SA>(apvts, PID::delayDamp,     delayDampRow->getSlider());
    delayMixA       = std::make_unique<SA>(apvts, PID::delayMix,      delayMixRow->getSlider());
    delayDivisionA  = std::make_unique<SA>(apvts, PID::delayClockDivision,
                                           delayDivisionRow->getSlider());

    delayTimeRow->onRightClick     = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::delayTime,            p); };
    delayFbRow->onRightClick       = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::delayFeedback,        p); };
    delayDampRow->onRightClick     = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::delayDamp,            p); };
    delayMixRow->onRightClick      = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::delayMix,             p); };
    delayDivisionRow->onRightClick = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::delayClockDivision,   p); };

    delayTimeRow->updateValue();
    delayFbRow->updateValue();
    delayDampRow->updateValue();
    delayMixRow->updateValue();
    delayDivisionRow->updateValue();

    // Wire onChange BEFORE attachment so the CA's initial setSelectedId
    // call fires it and syncs the visible state.
    delayClockModeHidden.onChange = [this] {
        const bool sync = delayClockModeHidden.getSelectedId() == 2;
        delayClockBtn.setToggleState(sync, juce::dontSendNotification);
        delayClockBtn.repaint();
        if (delayTimeRow)     delayTimeRow->setVisible(!sync);
        if (delayDivisionRow) delayDivisionRow->setVisible(sync);
    };

    // Attach APVTS AFTER buttons are set up (triggers onChange → updateVisibility)
    delayTypeA       = std::make_unique<CA>(apvts, PID::delayType,      delayTypeHidden);
    delayClockModeA  = std::make_unique<CA>(apvts, PID::delayClockMode, delayClockModeHidden);

    // ══════════ REVERB section ══════════
    paintSectionHeader(reverbHeader, "REVERB", kFxCol);
    addAndMakeVisible(reverbHeader);

    // Reverb type switchbox: 3 cells [OFF][Plate][Freeverb].
    //
    // Dark/Med/Brt were never three reverbs — they are three IRs of the SAME
    // EMT-140 plate, one reverb in three tone colours, so they belong under one
    // cell with their own dropdown. Freeverb likewise now has two voicings
    // (Freeverb and Freeverb+, the latter with the early-reflection front end
    // Freeverb omits), reached from that cell's dropdown. One dropdown per
    // category, exactly like Tape and BBD above.
    juce::StringArray reverbTypeItems;
    for (const auto& e : ReverbType::kEntries) reverbTypeItems.add(e.label);
    reverbTypeHidden.addItemList(reverbTypeItems, 1);
    reverbTypeHidden.onChange = [this]
    {
        const int rt = reverbTypeHidden.getSelectedId() - 1;
        const bool plateOn = ReverbType::isPlate(rt);
        const bool algoOn  = ReverbType::isAlgorithmic(rt);
        reverbTypeBtns[0].setToggleState(rt == ReverbType::Off, juce::dontSendNotification);
        reverbTypeBtns[1].setToggleState(plateOn, juce::dontSendNotification);
        reverbTypeBtns[2].setToggleState(algoOn,  juce::dontSendNotification);
        // Same rule as the delay families: family name when idle, the active
        // variant's own name when playing.
        reverbTypeBtns[1].setButtonText(plateOn ? ReverbType::kEntries[rt].label : "Plate");
        reverbTypeBtns[2].setButtonText(algoOn  ? ReverbType::kEntries[rt].label : "Freeverb");
        reverbTypeBtns[1].setTooltip(plateOn ? juce::String("EMT-140 plate - ") + ReverbType::kEntries[rt].label
                                             : juce::String("EMT-140 plate - pick a tone colour"));
        reverbTypeBtns[2].setTooltip(algoOn  ? juce::String("Algorithmic - ") + ReverbType::kEntries[rt].label
                                             : juce::String("Algorithmic - pick a voicing"));
        updateVisibility();
    };

    static const char* reverbLabels[] = { "OFF", "Plate", "Freeverb" };
    for (int i = 0; i < kNumReverbBtns; ++i)
    {
        reverbTypeBtns[i].setButtonText(reverbLabels[i]);
        styleSwitchButton(reverbTypeBtns[i], kFxCol);
        reverbTypeBtns[i].setClickingTogglesState(false);  // see the delay cells
        addAndMakeVisible(reverbTypeBtns[i]);
    }
    reverbTypeBtns[0].onClick = [this] { reverbTypeHidden.setSelectedId(ReverbType::Off + 1); };
    reverbTypeBtns[1].onClick = [this] { showReverbFamilyMenu(1, kPlateFamily, 3); };
    reverbTypeBtns[2].onClick = [this] { showReverbFamilyMenu(2, kAlgoFamily,  2); };

    reverbMixRow  = std::make_unique<SliderRow>("Mix",   fmtF3, kFxCol);
    algoRoomRow   = std::make_unique<SliderRow>("Room",  fmtF2, kFxCol);
    algoDampRow   = std::make_unique<SliderRow>("Damp",  fmtF2, kFxCol);
    algoWidthRow  = std::make_unique<SliderRow>("Width", fmtF2, kFxCol);

    for (auto* r : { reverbMixRow.get(), algoRoomRow.get(), algoDampRow.get(), algoWidthRow.get() })
    {
        r->setLabelAsBand(true);   // unified label-band look (see Delay section)
        addAndMakeVisible(*r);
    }

    reverbMixA = std::make_unique<SA>(apvts, PID::reverbMix,   reverbMixRow->getSlider());
    algoRoomA  = std::make_unique<SA>(apvts, PID::algoRoom,    algoRoomRow->getSlider());
    algoDampA  = std::make_unique<SA>(apvts, PID::algoDamping, algoDampRow->getSlider());
    algoWidthA = std::make_unique<SA>(apvts, PID::algoWidth,   algoWidthRow->getSlider());

    reverbMixRow->onRightClick = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::reverbMix,    p); };
    algoRoomRow->onRightClick  = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::algoRoom,     p); };
    algoDampRow->onRightClick  = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::algoDamping,  p); };
    algoWidthRow->onRightClick = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::algoWidth,    p); };

    reverbMixRow->updateValue();
    algoRoomRow->updateValue();
    algoDampRow->updateValue();
    algoWidthRow->updateValue();

    addAndMakeVisible(wordmark);

    // Attach APVTS AFTER buttons are set up
    reverbTypeA = std::make_unique<CA>(apvts, PID::reverbType, reverbTypeHidden);

    startTimerHz(30); // ghost slider updates
}

void FxPanel::timerCallback()
{
    if (processorRef.audioIdle.load(std::memory_order_relaxed)) return;
    auto& mv = processorRef.modulatedValues;
    delayTimeRow->setGhostValue(mv.delayTime.load(std::memory_order_relaxed));
    delayFbRow->setGhostValue(mv.delayFeedback.load(std::memory_order_relaxed));
    delayMixRow->setGhostValue(mv.delayMix.load(std::memory_order_relaxed));
    reverbMixRow->setGhostValue(mv.reverbMix.load(std::memory_order_relaxed));

    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayMixRow.get(), reverbMixRow.get() })
        r->tickGhost();
}

// Opens a family's dropdown anchored to its switchbox cell. The menu is async,
// so the callback must survive the panel being torn down while it is open —
// hence the SafePointer. Dismissing without a pick changes nothing.
void FxPanel::showDelayFamilyMenu(int btnIndex, const int* types, int numTypes)
{
    const int cur = delayTypeHidden.getSelectedId() - 1;
    juce::PopupMenu m;
    for (int i = 0; i < numTypes; ++i)
        m.addItem(types[i] + 1, DelayType::kEntries[types[i]].label, true, types[i] == cur);

    juce::Component::SafePointer<FxPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&delayTypeBtns[btnIndex])
                        .withMinimumWidth(juce::jmax(96, delayTypeBtns[btnIndex].getWidth())),
                    [safe](int id)
                    {
                        if (safe != nullptr && id > 0)
                            safe->delayTypeHidden.setSelectedId(id);
                    });
}

void FxPanel::showReverbFamilyMenu(int btnIndex, const int* types, int numTypes)
{
    const int cur = reverbTypeHidden.getSelectedId() - 1;
    juce::PopupMenu m;
    for (int i = 0; i < numTypes; ++i)
        m.addItem(types[i] + 1, ReverbType::kEntries[types[i]].label, true, types[i] == cur);

    juce::Component::SafePointer<FxPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&reverbTypeBtns[btnIndex])
                        .withMinimumWidth(juce::jmax(96, reverbTypeBtns[btnIndex].getWidth())),
                    [safe](int id)
                    {
                        if (safe != nullptr && id > 0)
                            safe->reverbTypeHidden.setSelectedId(id);
                    });
}

void FxPanel::updateVisibility()
{
    // Guard: called by APVTS attachment before all components are created
    if (!reverbMixRow)
        return;

    constexpr float dimAlpha = 0.3f;

    // Delay: always visible, dimmed when OFF
    bool delayOn = delayTypeHidden.getSelectedId() > 1;
    float delayAlpha = delayOn ? 1.0f : dimAlpha;
    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayDampRow.get(),
                     delayMixRow.get(), delayDivisionRow.get() })
    {
        r->setAlpha(delayAlpha);
        r->setEnabled(delayOn);
    }
    delayClockBtn.setAlpha(delayAlpha);
    delayClockBtn.setEnabled(delayOn);

    // Reverb: always visible; dim params based on mode
    bool reverbOn = reverbTypeHidden.getSelectedId() > 1;
    bool algoOn   = ReverbType::isAlgorithmic(reverbTypeHidden.getSelectedId() - 1);
    float reverbAlpha = reverbOn ? 1.0f : dimAlpha;
    // Room/Damp/Width: active only for Algo, dimmed for Convolution and OFF
    float algoParamAlpha = algoOn ? 1.0f : dimAlpha;
    for (auto* r : { algoRoomRow.get(), algoDampRow.get(), algoWidthRow.get() })
    {
        r->setAlpha(algoParamAlpha);
        r->setEnabled(algoOn);
    }
    // Mix: active whenever reverb is on
    reverbMixRow->setAlpha(reverbAlpha);
    reverbMixRow->setEnabled(reverbOn);

    // NOTE: no resized() here. This only changes visual state (alpha/enabled/
    // toggle/label), none of which affects layout. Calling resized() on every
    // type click re-derived headerH from getTopLevelComponent()->getHeight(),
    // which (depending on when the window settled its size) could differ from
    // the initial layout pass by a pixel or two — a visible header jump on the
    // first click. The Time↔Division swap has its own setVisible handler.
    repaint();
}

float FxPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(12.0f, 22.0f, topH * 0.022f);
}

void FxPanel::WordmarkComponent::paint(juce::Graphics& g)
{
    const int panelW = getWidth();
    const int panelH = getHeight();
    if (panelW <= 0 || panelH <= 0)
        return;

    struct LetterColor { char ch; juce::Colour col; };
    LetterColor letters[] = {
        {'U', juce::Colour(0xff667eea)}, {'C', juce::Colour(0xffe91e63)},
        {'D', juce::Colour(0xff7C4DFF)}, {'C', juce::Colour(0xffFF6F00)},
        {'A', juce::Colour(0xff4CAF50)}, {'E', juce::Colour(0xff00BCD4)},
        {' ', {}},
        {'A', juce::Colour(0xff667eea)}, {'I', juce::Colour(0xffe91e63)},
        {' ', {}},
        {'L', juce::Colour(0xff7C4DFF)}, {'A', juce::Colour(0xffFF6F00)},
        {'B', juce::Colour(0xff4CAF50)}
    };

    auto measureBrandWidth = [&letters](float fontSize)
    {
        int total = 0;
        const int tracking = juce::roundToInt(fontSize * 0.15f);
        bool first = true;

        for (auto& lc : letters)
        {
            if (!first)
                total += tracking;

            char text[] = { lc.ch, 0 };
            total += measureTextWidth(juce::String(text), fontSize);
            first = false;
        }

        return total;
    };

    const int usableW = juce::jmax(1, panelW - 8);
    float prefixFs = juce::jlimit(9.0f, 15.0f,
                                  juce::jmin(static_cast<float>(panelW) / 11.0f,
                                             static_cast<float>(panelH) * 0.20f));
    float brandFs = juce::jlimit(12.0f, 24.0f,
                                 juce::jmin(static_cast<float>(panelW) / 11.5f,
                                            static_cast<float>(panelH) * 0.34f));

    int brandW = measureBrandWidth(brandFs);
    if (brandW > usableW)
    {
        brandFs = juce::jmax(10.0f, brandFs * static_cast<float>(usableW) / static_cast<float>(brandW));
        brandW = measureBrandWidth(brandFs);
    }

    const int prefixH = juce::roundToInt(prefixFs * 1.35f);
    const int brandH = juce::roundToInt(brandFs * 1.35f);
    const int lineGap = juce::jmax(1, juce::roundToInt(static_cast<float>(panelH) * 0.06f));
    const int totalH = prefixH + lineGap + brandH;
    int y = juce::jmax(0, (panelH - totalH) / 2);

    g.setFont(juce::FontOptions(prefixFs));
    g.setColour(kDimmer);
    g.drawText("T5ynth by", 0, y, panelW, prefixH, juce::Justification::centred);

    y += prefixH + lineGap;
    const int tracking = juce::roundToInt(brandFs * 0.15f);
    int x = juce::roundToInt((static_cast<float>(panelW) - static_cast<float>(brandW)) * 0.5f);
    x = juce::jlimit(4, juce::jmax(4, panelW - brandW - 4), x);

    g.setFont(juce::FontOptions(brandFs));
    bool first = true;
    for (auto& lc : letters)
    {
        if (!first)
            x += tracking;

        char text[] = { lc.ch, 0 };
        juce::String ch(text);
        int cw = measureTextWidth(ch, brandFs);
        if (lc.ch != ' ')
        {
            g.setColour(lc.col);
            g.drawText(ch, x, y, cw + 1, brandH, juce::Justification::centredLeft);
        }
        x += cw;
        first = false;
    }
}

void FxPanel::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    // Vertical separator on left edge (between Seq and FX)
    g.setColour(kBorder);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));

    // Framed module cards (Delay, Reverb) — same recipe as the synth easy-view
    // module blocks (paintEasyBlock): a lighter fill so the card stands out on this
    // kCard panel, plus a border. Drawn BEFORE the child controls so they sit on top;
    // the accent header band is the top strip. (No module-colour left stripe — it was
    // removed per design review.)
    auto paintFxCard = [&g](juce::Rectangle<int> b)
    {
        if (b.isEmpty()) return;
        g.setColour(kSurface.withAlpha(0.62f));
        g.fillRect(b);
        g.setColour(juce::Colour(0xaa05070d));
        g.drawRect(b.expanded(1, 1), 1);
        g.setColour(kBorder.withAlpha(0.82f));
        g.drawRect(b, 1);
    };
    paintFxCard(delayCardBounds);
    paintFxCard(reverbCardBounds);

    // SwitchBox borders
    paintSwitchBoxBorder(g, delayTypeSwitchBounds);
    paintSwitchBoxBorder(g, reverbTypeSwitchBounds);
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced(6, 0);
    float topH = getTopLevelComponent()
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    int headerH = juce::jlimit(14, 20, juce::roundToInt(topH * 0.022f));
    float f = static_cast<float>(headerH);

    int rowH = juce::jmin(juce::roundToInt(static_cast<float>(getHeight()) * 0.14f), 20);
    int gap = 2;

    // Each effect is a framed module card (same look as the synth easy-view
    // blocks): an accent header strip on top, content inset so the value
    // read-outs sit INSIDE the frame with padding — the Duration card template,
    // adapted to this kCard panel. Net vertical cost is zero: each card's bottom
    // pad is reclaimed from the old inter-section gap*2.
    const int fxPad = juce::jmax(3, juce::roundToInt(f * 0.3f));
    const int sectionBodyH = rowH * 3 + gap * 2;        // switchbox + 2 param rows

    // ── DELAY card ──
    auto delayCard = area.removeFromTop(headerH + gap + sectionBodyH + gap);
    delayCardBounds = delayCard;
    {
        auto dc = delayCard;
        delayHeader.setFont(juce::FontOptions(f * 0.85f));
        delayHeader.setBounds(dc.removeFromTop(headerH));   // full-width accent header strip
        dc.removeFromTop(gap);
        dc.removeFromBottom(gap);                            // bottom pad inside the card
        dc.reduce(fxPad, 0);                                 // side pad → value read-outs sit inside the frame

        // Delay type switchbox (5 buttons; Tape+BBD show char in label on cycle)
        {
            auto delaySwRow = dc.removeFromTop(rowH);
            int delayCellW = delaySwRow.getWidth() / kNumDelayBtns;
            for (int i = 0; i < kNumDelayBtns; ++i)
            {
                int edges = 0;
                if (i > 0)                   edges |= juce::Button::ConnectedOnLeft;
                if (i < kNumDelayBtns - 1)  edges |= juce::Button::ConnectedOnRight;
                delayTypeBtns[i].setConnectedEdges(edges);
                delayTypeBtns[i].setBounds(delaySwRow.removeFromLeft(delayCellW));
            }
            delayTypeSwitchBounds = delayTypeBtns[0].getBounds()
                .getUnion(delayTypeBtns[kNumDelayBtns - 1].getBounds());
        }
        dc.removeFromTop(gap);

        // Delay params — Time/Division/Damp share the LEFT label column; FB/Mix
        // share the RIGHT. Both forced widths derive from natural-width maxima.
        {
            const int delayPairGap = 2;
            const int delayColumnW = juce::jmax(0, (dc.getWidth() - delayPairGap) / 2);

            const int delayLeftLabelW = std::max({
                delayTimeRow->getNaturalLabelWidthForAvailableWidth(delayColumnW),
                delayDivisionRow->getNaturalLabelWidthForAvailableWidth(delayColumnW),
                delayDampRow->getNaturalLabelWidthForAvailableWidth(delayColumnW)
            });
            const int delayRightLabelW = std::max({
                delayFbRow->getNaturalLabelWidthForAvailableWidth(delayColumnW),
                delayMixRow->getNaturalLabelWidthForAvailableWidth(delayColumnW)
            });

            for (auto* r : { delayTimeRow.get(), delayDivisionRow.get(), delayDampRow.get() })
                r->setForcedLabelWidth(delayLeftLabelW);
            for (auto* r : { delayFbRow.get(), delayMixRow.get() })
                r->setForcedLabelWidth(delayRightLabelW);

            auto row1 = dc.removeFromTop(rowH);
            auto pair1 = layoutSliderRowPairBounds(row1, *delayTimeRow, *delayFbRow, delayPairGap);
            delayTimeRow->setBounds(pair1[0]);
            delayFbRow->setBounds(pair1[1]);
            if (delayDivisionRow) delayDivisionRow->setBounds(pair1[0]);
            // Overlay clock button on the (empty) reserved label slot at the
            // start of pair1[0] so it sits in the same column as "Damp" below.
            delayClockBtn.setBounds(pair1[0].withWidth(delayLeftLabelW));

            dc.removeFromTop(gap);
            auto row2 = dc.removeFromTop(rowH);
            auto pair2 = layoutSliderRowPairBounds(row2, *delayDampRow, *delayMixRow, delayPairGap);
            delayDampRow->setBounds(pair2[0]);
            delayMixRow->setBounds(pair2[1]);
        }
    }

    area.removeFromTop(gap);

    // ── REVERB card ──
    auto reverbCard = area.removeFromTop(headerH + gap + sectionBodyH + gap);
    reverbCardBounds = reverbCard;
    {
        auto rc = reverbCard;
        reverbHeader.setFont(juce::FontOptions(f * 0.85f));
        reverbHeader.setBounds(rc.removeFromTop(headerH));
        rc.removeFromTop(gap);
        rc.removeFromBottom(gap);
        rc.reduce(fxPad, 0);

        // Reverb type switchbox
        auto revSwRow = rc.removeFromTop(rowH);
        int revCellW = revSwRow.getWidth() / kNumReverbBtns;
        for (int i = 0; i < kNumReverbBtns; ++i)
        {
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumReverbBtns - 1) edges |= juce::Button::ConnectedOnRight;
            reverbTypeBtns[i].setConnectedEdges(edges);
            reverbTypeBtns[i].setBounds(revSwRow.removeFromLeft(revCellW));
        }
        reverbTypeSwitchBounds = reverbTypeBtns[0].getBounds()
            .getUnion(reverbTypeBtns[kNumReverbBtns - 1].getBounds());
        rc.removeFromTop(gap);

        // Reverb params — Room+Damp, Width+Mix. Column label widths matched like
        // Delay so the slider tracks line up (Room over Width, Damp over Mix).
        {
            const int revPairGap = 2;
            const int revColumnW = juce::jmax(0, (rc.getWidth() - revPairGap) / 2);
            const int revLeftLabelW = std::max(
                algoRoomRow->getNaturalLabelWidthForAvailableWidth(revColumnW),
                algoWidthRow->getNaturalLabelWidthForAvailableWidth(revColumnW));
            const int revRightLabelW = std::max(
                algoDampRow->getNaturalLabelWidthForAvailableWidth(revColumnW),
                reverbMixRow->getNaturalLabelWidthForAvailableWidth(revColumnW));
            algoRoomRow->setForcedLabelWidth(revLeftLabelW);
            algoWidthRow->setForcedLabelWidth(revLeftLabelW);
            algoDampRow->setForcedLabelWidth(revRightLabelW);
            reverbMixRow->setForcedLabelWidth(revRightLabelW);

            auto row1 = rc.removeFromTop(rowH);
            auto pair1 = layoutSliderRowPairBounds(row1, *algoRoomRow, *algoDampRow, revPairGap);
            algoRoomRow->setBounds(pair1[0]);
            algoDampRow->setBounds(pair1[1]);

            rc.removeFromTop(gap);
            auto row2 = rc.removeFromTop(rowH);
            auto pair2 = layoutSliderRowPairBounds(row2, *algoWidthRow, *reverbMixRow, revPairGap);
            algoWidthRow->setBounds(pair2[0]);
            reverbMixRow->setBounds(pair2[1]);
        }
    }

    area.removeFromTop(gap);
    wordmark.setBounds(area);
    wordmark.setVisible(area.getHeight() >= 24);
}
