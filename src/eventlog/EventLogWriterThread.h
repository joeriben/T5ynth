#pragma once
#include <JuceHeader.h>
#include <array>
#include <mutex>
#include <vector>
#include "EventLog.h"

/**
 * Sole writer of the open .t5evt file. Mirrors UpdateChecker (the codebase's
 * one existing juce::Thread subclass): run() loops on wait(), drains whatever
 * it owns, exits on stopThread(). Everything it drains arrives via enqueue*()
 * calls made from the MESSAGE thread only (T5ynthProcessor::drainEventLogQueues(),
 * itself called from MainPanel's timer at the same cadence as
 * drainStepRecordQueue()) — the audio thread never calls enqueue*(), it only
 * ever touches the lock-free FIFOs drainEventLogQueues() reads from. enqueue*
 * takes a short mutex, never blocks on I/O, never allocates beyond an
 * occasional vector growth — both fine off the audio thread.
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

private:
    void drainAndWrite();
    void writeLine(const juce::var& obj);
    void openFileIfNeeded();

    juce::File   directory_;
    EventLogHeader header_;
    std::vector<juce::String> paramIdByIndex_;
    std::unique_ptr<juce::FileOutputStream> out_;

    std::mutex pendingMutex_;
    std::vector<NoteEventLogEntry>       pendingNotes_;
    std::vector<ParamEventLogEntry>      pendingParams_;
    std::vector<GenerationEventLogEntry> pendingGenerations_;
    std::vector<PresetLoadedLogEntry>    pendingPresetLoads_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventLogWriterThread)
};
