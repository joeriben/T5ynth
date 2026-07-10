#pragma once
#include <JuceHeader.h>
#include <deque>
#include "../eventlog/EventLog.h"
#include "../eventlog/EventLogReader.h"

class T5ynthProcessor;

/**
 * Full-surface replay transport: covers the whole main area (everything above the
 * StatusBar) while a .t5evt tape plays. The tape drives the engine underneath —
 * params move, generations swap the sound — but the user's visible workspace
 * (prompt editors, seed, explorer) is never touched: this overlay IS the replay's
 * display. Content:
 *
 *   REPLAY  <filename>                                   [Stop Replay]
 *   1:23 / 6:40                              Speed ×1.00  ────○────
 *   [timeline strip: note scatter, generation marks, playhead]
 *   now: "prompt A"  ⇄  "prompt B"   seed 123456   (generating…)
 *   <recent-event feed>
 *
 * No own Timer: MainPanel's existing timer calls refresh(), which repaints only
 * when something visible changed (playhead pixel, feed line, position second) —
 * see docs/PERFORMANCE_GUIDE.md.
 *
 * Data access: the processor's getReplay*ForUi() views — message-thread-only,
 * valid while the replay is active (refresh() self-guards on isReplayActive()).
 */
class ReplayOverlay : public juce::Component
{
public:
    explicit ReplayOverlay(T5ynthProcessor& p);

    /** Show for the tape that just started (message thread, replay already active). */
    void begin(const juce::String& tapeName);
    /** Hide and drop cached tape data. Idempotent. */
    void end();
    /** Timer tick from MainPanel — updates readouts, repaints only on change. */
    void refresh();

    /** Wired by MainPanel to stopReplay() + status text. */
    std::function<void()> onStop;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildTimelineImage(int w, int h);
    juce::String formatTapeTime(uint64_t samples) const;

    T5ynthProcessor& processorRef;

    juce::TextButton stopBtn { "Stop Replay" };
    juce::Slider     speedSlider;

    juce::String tapeName_;

    // Timeline strip: event positions burned into an Image once per size/tape,
    // playhead line painted over it. Thousands of events, zero per-frame cost.
    juce::Image          timeline_;
    juce::Rectangle<int> timelineBounds_;

    // refresh() change-detection cache
    juce::String posText_;
    juce::String nowPromptA_, nowPromptB_, nowMetaText_;
    bool         genBusy_    = false;
    int          playheadPx_ = -1;

    // Recent-event feed (append-only cursors over the tape's sorted vectors)
    size_t feedNoteIdx_  = 0;
    size_t feedParamIdx_ = 0;
    size_t nowGenIdx_    = static_cast<size_t>(-1);   // -1 = before the first generation
    std::deque<juce::String> feed_;
    static constexpr size_t kFeedLines = 7;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplayOverlay)
};
