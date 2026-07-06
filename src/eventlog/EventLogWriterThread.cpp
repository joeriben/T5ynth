#include "EventLogWriterThread.h"

namespace
{
constexpr int kDrainWaitMs = 50;
}

EventLogWriterThread::EventLogWriterThread(juce::File directory, EventLogHeader header,
                                          std::vector<juce::String> paramIdByIndex)
    : juce::Thread("T5ynth Event Log"),
      directory_(std::move(directory)),
      header_(std::move(header)),
      paramIdByIndex_(std::move(paramIdByIndex))
{
}

EventLogWriterThread::~EventLogWriterThread()
{
    stopThread(4000);
}

void EventLogWriterThread::setSampleRate(double sr)
{
    // header_ is only otherwise read by openFileIfNeeded() under the same lock.
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    header_.sampleRate = sr;
}

void EventLogWriterThread::pushNote(const NoteEventLogEntry& e) noexcept
{
    // Audio thread, single producer, lock-free.
    int s1, sz1, s2, sz2;
    noteFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)
    {
        noteRing_[static_cast<size_t>(s1)] = e;
        noteFifo_.finishedWrite(1);
    }
}

void EventLogWriterThread::pushParam(const ParamEventLogEntry& e) noexcept
{
    // Audio OR message thread. Try-lock serialises the two producers against
    // each other (AbstractFifo is SPSC); never blocks the audio thread. The
    // single consumer (run()) does NOT take this lock.
    const juce::SpinLock::ScopedTryLockType tryLock(paramProducerLock_);
    if (! tryLock.isLocked())
        return;
    int s1, sz1, s2, sz2;
    paramFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)
    {
        paramRing_[static_cast<size_t>(s1)] = e;
        paramFifo_.finishedWrite(1);
    }
}

void EventLogWriterThread::enqueue(const GenerationEventLogEntry& e)
{
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingGenerations_.push_back(e);
}

void EventLogWriterThread::enqueue(const PresetLoadedLogEntry& e)
{
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingPresetLoads_.push_back(e);
}

void EventLogWriterThread::openFileIfNeeded()
{
    if (out_ != nullptr)
        return;

    EventLogHeader headerSnapshot;
    {
        const std::lock_guard<std::mutex> lock(pendingMutex_);
        headerSnapshot = header_;
    }

    directory_.createDirectory();
    const auto filename = "session_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".t5evt";
    out_ = std::make_unique<juce::FileOutputStream>(directory_.getChildFile(filename));
    if (! out_->openedOk())
    {
        out_.reset();
        return;
    }
    writeLine(juce::var(eventLogHeaderToDynamicObject(headerSnapshot).get()));
}

void EventLogWriterThread::writeLine(const juce::var& obj)
{
    if (out_ == nullptr)
        return;
    out_->writeText(juce::JSON::toString(obj, true) + "\n", false, false, "\n");
}

void EventLogWriterThread::drainFifos()
{
    // Writer thread, sole consumer of both rings.
    const int nn = noteFifo_.getNumReady();
    if (nn > 0)
    {
        int s1, sz1, s2, sz2;
        noteFifo_.prepareToRead(nn, s1, sz1, s2, sz2);
        for (int i = 0; i < sz1; ++i) stagedNotes_.push_back(noteRing_[static_cast<size_t>(s1 + i)]);
        for (int i = 0; i < sz2; ++i) stagedNotes_.push_back(noteRing_[static_cast<size_t>(s2 + i)]);
        noteFifo_.finishedRead(sz1 + sz2);
    }

    const int np = paramFifo_.getNumReady();
    if (np > 0)
    {
        int s1, sz1, s2, sz2;
        paramFifo_.prepareToRead(np, s1, sz1, s2, sz2);
        for (int i = 0; i < sz1; ++i) stagedParams_.push_back(paramRing_[static_cast<size_t>(s1 + i)]);
        for (int i = 0; i < sz2; ++i) stagedParams_.push_back(paramRing_[static_cast<size_t>(s2 + i)]);
        paramFifo_.finishedRead(sz1 + sz2);
    }
}

void EventLogWriterThread::drainAndWrite()
{
    std::vector<NoteEventLogEntry>  notes;  notes.swap(stagedNotes_);    // writer-thread-only, no lock
    std::vector<ParamEventLogEntry> params; params.swap(stagedParams_);
    std::vector<GenerationEventLogEntry> generations;
    std::vector<PresetLoadedLogEntry>    presetLoads;
    {
        const std::lock_guard<std::mutex> lock(pendingMutex_);
        generations.swap(pendingGenerations_);
        presetLoads.swap(pendingPresetLoads_);
    }
    if (notes.empty() && params.empty() && generations.empty() && presetLoads.empty())
        return;

    openFileIfNeeded();

    for (const auto& n : notes)       writeLine(juce::var(eventLogEntryToDynamicObject(n).get()));
    for (const auto& p : params)
    {
        const juce::String paramId = (p.paramIndex >= 0 && static_cast<size_t>(p.paramIndex) < paramIdByIndex_.size())
                                    ? paramIdByIndex_[static_cast<size_t>(p.paramIndex)]
                                    : juce::String();
        writeLine(juce::var(eventLogEntryToDynamicObject(p, paramId).get()));
    }
    for (const auto& g : generations)  writeLine(juce::var(eventLogEntryToDynamicObject(g).get()));
    for (const auto& pl : presetLoads) writeLine(juce::var(eventLogEntryToDynamicObject(pl).get()));

    if (out_ != nullptr)
        out_->flush();
}

void EventLogWriterThread::run()
{
    while (! threadShouldExit())
    {
        drainFifos();
        drainAndWrite();
        wait(kDrainWaitMs);
    }
    // Final flush of anything produced right before stopThread().
    drainFifos();
    drainAndWrite();
}
