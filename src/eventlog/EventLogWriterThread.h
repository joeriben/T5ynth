#pragma once
#include <JuceHeader.h>
#include <array>
#include <mutex>
#include <vector>
#include "EventLog.h"

/**
 * Sole owner of the .t5evt recorder: the lock-free ingress FIFOs, the drain
 * loop, and the output file. Deliberately self-contained — it NEVER dereferences
 * the processor. Two consequences:
 *   - recording is independent of the editor (the drain runs on this thread, not
 *     a GUI timer);
 *   - an abandoned thread (if stopThread() ever times out on stalled disk I/O)
 *     can only ever touch its OWN members, never freed processor memory — the
 *     same worst case every other juce::Thread in the app already accepts, and no
 *     worse. (The processor additionally release()es rather than delete()s this
 *     object on a join timeout, so even those own members stay alive.)
 *
 * Ingress:
 *   - pushNote(): lock-free, audio-thread-safe. SINGLE producer — the note taps
 *     run only inside processBlock (the audio thread).
 *   - pushParam(): MULTI producer — parameterChanged() fires on the audio thread
 *     (CC-Learn) AND the message thread (GUI / host automation). Serialised by a
 *     try-lock so the audio thread never blocks; a lost race drops one event
 *     (acceptable for a log — the ring already drops on overflow).
 *   - enqueue(): message-thread-only, mutex-guarded, for generation / preset
 *     events that originate off the audio thread.
 * The single consumer of every ingress path is this thread's run() loop.
 *
 * paramId resolution (APVTS index -> string) happens here at serialization time,
 * never in the audio-thread-adjacent producer.
 */
class EventLogWriterThread : public juce::Thread
{
public:
    EventLogWriterThread(juce::File directory, EventLogHeader header,
                        std::vector<juce::String> paramIdByIndex);
    ~EventLogWriterThread() override;

    void run() override;

    /** Lock-free, audio-thread-safe. No allocation, no blocking. */
    void pushNote(const NoteEventLogEntry& e) noexcept;
    /** Multi-producer (audio + message thread) — try-locks, never blocks; drops
     *  the event on a lost race. Audio-thread-safe. */
    void pushParam(const ParamEventLogEntry& e) noexcept;

    /** Message-thread ingress for off-audio-thread events. */
    void enqueue(const GenerationEventLogEntry& e);
    void enqueue(const PresetLoadedLogEntry& e);

    /** The real sample rate isn't known at construction (prepareToPlay runs
     *  later) — set it before the first event opens the file. */
    void setSampleRate(double sr);

    /** The .t5evt file currently being written this session, or an empty File if
     *  nothing has been recorded yet (the file is created lazily on the first
     *  event). Thread-safe; message thread calls it to offer "Save Session Log". */
    juce::File getCurrentFile() const;

private:
    void drainFifos();       // lock-free ingress rings -> writer-thread staging
    void drainAndWrite();    // staging + message-thread queues -> file
    void writeLine(const juce::var& obj);
    void openFileIfNeeded();

    juce::File   directory_;
    EventLogHeader header_;
    std::vector<juce::String> paramIdByIndex_;
    std::unique_ptr<juce::FileOutputStream> out_;
    juce::File   currentFile_;   // guarded by pendingMutex_; published in openFileIfNeeded()

    // Lock-free ingress rings (producer/consumer threads per the class doc).
    static constexpr int kNoteRingSize  = 256;
    static constexpr int kParamRingSize = 256;
    std::array<NoteEventLogEntry, kNoteRingSize>   noteRing_;
    juce::AbstractFifo                             noteFifo_ { kNoteRingSize };
    std::array<ParamEventLogEntry, kParamRingSize> paramRing_;
    juce::AbstractFifo                             paramFifo_ { kParamRingSize };
    juce::SpinLock                                 paramProducerLock_;   // serialises pushParam's multi-thread producers

    // Writer-thread-only staging (drained from the lock-free rings).
    std::vector<NoteEventLogEntry>       stagedNotes_;
    std::vector<ParamEventLogEntry>      stagedParams_;
    // Message-thread-origin queues (mutex-guarded: producer = message thread,
    // consumer = this thread).
    mutable std::mutex pendingMutex_;   // mutable: getCurrentFile() is const
    std::vector<GenerationEventLogEntry> pendingGenerations_;
    std::vector<PresetLoadedLogEntry>    pendingPresetLoads_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventLogWriterThread)
};
