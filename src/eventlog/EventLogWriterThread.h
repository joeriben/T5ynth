#pragma once
#include <JuceHeader.h>
#include <array>
#include <functional>
#include <mutex>
#include <vector>
#include "EventLog.h"

/**
 * Sole writer of the open .t5evt file, and the sole owner of the recorder's
 * clock. Mirrors UpdateChecker (the codebase's one existing juce::Thread
 * subclass): run() loops on wait(), drains, exits on stopThread(). This thread
 * belongs entirely to the processor domain — it must NEVER depend on the editor
 * being open, since the note/param events it records are produced by the
 * audio thread whether or not any GUI exists.
 *
 * Each cycle it (1) invokes the pull callback the processor installs, which
 * drains the processor's lock-free audio-thread FIFOs into this thread's pending
 * queues via enqueue*(), then (2) serializes everything to disk. Message-thread-
 * origin events (generations, preset markers) also arrive via enqueue*() from
 * their own call sites. The audio thread never calls enqueue*() — it only writes
 * the lock-free FIFOs the pull callback reads.
 *
 * paramId resolution (APVTS index -> human-readable string) happens here, on
 * this thread, at serialization time — never inside the audio-thread-adjacent
 * hot path that produced the ParamEventLogEntry.
 */
class EventLogWriterThread : public juce::Thread
{
public:
    EventLogWriterThread(juce::File directory, EventLogHeader header,
                        std::vector<juce::String> paramIdByIndex);
    ~EventLogWriterThread() override;

    void run() override;

    void enqueue(const NoteEventLogEntry& e);
    void enqueue(const ParamEventLogEntry& e);
    void enqueue(const GenerationEventLogEntry& e);
    void enqueue(const PresetLoadedLogEntry& e);

    /** The real sample rate isn't known at construction (prepareToPlay runs
     *  later) — update it before the first event opens the file. Safe any time;
     *  a no-op once the file is already open (its header line is already written). */
    void setSampleRate(double sr);

    /** Install the callback this thread invokes once per drain cycle to pull the
     *  processor's lock-free audio-thread FIFOs into this thread's queues. Set
     *  ONCE, before startThread() — read only on this thread thereafter. This is
     *  what makes recording processor-owned rather than editor-driven. */
    void setPullCallback(std::function<void()> cb) { pullFromSource_ = std::move(cb); }

private:
    void drainAndWrite();
    void writeLine(const juce::var& obj);
    void openFileIfNeeded();

    juce::File   directory_;
    EventLogHeader header_;
    std::vector<juce::String> paramIdByIndex_;
    std::unique_ptr<juce::FileOutputStream> out_;

    std::function<void()> pullFromSource_;   // writer-thread-only after startThread()

    std::mutex pendingMutex_;
    std::vector<NoteEventLogEntry>       pendingNotes_;
    std::vector<ParamEventLogEntry>      pendingParams_;
    std::vector<GenerationEventLogEntry> pendingGenerations_;
    std::vector<PresetLoadedLogEntry>    pendingPresetLoads_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventLogWriterThread)
};
