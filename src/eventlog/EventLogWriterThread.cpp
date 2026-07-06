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

void EventLogWriterThread::setSampleRate(double sr)
{
    // header_ is only otherwise touched by openFileIfNeeded() (writer thread, same
    // lock) — out_ itself stays writer-thread-only/unguarded, so this never races it.
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    header_.sampleRate = sr;
}

void EventLogWriterThread::enqueue(const NoteEventLogEntry& e)
{
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingNotes_.push_back(e);
}

void EventLogWriterThread::enqueue(const ParamEventLogEntry& e)
{
    const std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingParams_.push_back(e);
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

void EventLogWriterThread::drainAndWrite()
{
    std::vector<NoteEventLogEntry>       notes;
    std::vector<ParamEventLogEntry>      params;
    std::vector<GenerationEventLogEntry> generations;
    std::vector<PresetLoadedLogEntry>    presetLoads;
    {
        const std::lock_guard<std::mutex> lock(pendingMutex_);
        notes.swap(pendingNotes_);
        params.swap(pendingParams_);
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
    for (const auto& g : generations) writeLine(juce::var(eventLogEntryToDynamicObject(g).get()));
    for (const auto& pl : presetLoads) writeLine(juce::var(eventLogEntryToDynamicObject(pl).get()));

    if (out_ != nullptr)
        out_->flush();
}

void EventLogWriterThread::run()
{
    while (! threadShouldExit())
    {
        if (pullFromSource_)
            pullFromSource_();   // drain the processor's lock-free FIFOs into our queues
        drainAndWrite();
        wait(kDrainWaitMs);
    }
    // Final flush: pull once more so events produced right before stopThread()
    // (e.g. a note-off at transport stop) aren't lost, then write.
    if (pullFromSource_)
        pullFromSource_();
    drainAndWrite();
}
