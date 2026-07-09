#pragma once
#include <JuceHeader.h>
#include "EventLog.h"

/**
 * R1: Reads a .t5evt session log file and parses it into typed, sorted event
 * vectors plus the header (including the base64-encoded start-state for replay).
 *
 * Usage:
 *   EventLogReader reader;
 *   if (reader.loadFile(file))
 *   {
 *       const auto& header = reader.getHeader();
 *       const auto& notes  = reader.getNoteEvents();
 *       // ... replay or dump
 *   }
 *
 * The reader is message-thread-only; it does not touch the audio thread.
 * Events are sorted by timestamp after parsing.
 */
class EventLogReader
{
public:
    EventLogReader() = default;

    /** Parse a .t5evt file. Returns true on success (header + at least zero events).
     *  On failure, getErrorMessage() returns a human-readable description. */
    bool loadFile(const juce::File& file);

    /** Parse from a string (for testing or when the file content is already in memory). */
    bool loadFromString(const juce::String& content);

    const EventLogHeader& getHeader() const { return header_; }
    const juce::String&   getErrorMessage() const { return errorMessage_; }

    /** All note events, sorted by timestamp. */
    const std::vector<NoteEventLogEntry>& getNoteEvents() const { return noteEvents_; }

    /** All param events, sorted by timestamp. paramId is stored as a string
     *  (the reader doesn't have access to the APVTS index map). */
    struct ParsedParamEvent
    {
        uint64_t     timestamp = 0;
        juce::String paramId;
        float        value  = 0.0f;
        ParamOrigin  origin = ParamOrigin::HostAutomation;
    };
    const std::vector<ParsedParamEvent>& getParamEvents() const { return paramEvents_; }

    /** All generation events, sorted by timestamp. */
    const std::vector<GenerationEventLogEntry>& getGenerationEvents() const { return generationEvents_; }

    /** All preset-loaded markers, sorted by timestamp. */
    const std::vector<PresetLoadedLogEntry>& getPresetLoadedEvents() const { return presetLoadedEvents_; }

    /** Total duration in samples (timestamp of last event, or 0 if empty). */
    uint64_t getTotalDurationSamples() const { return totalDurationSamples_; }

    /** Count of events that could not be parsed (malformed JSON, unknown kind). */
    int getSkippedEventCount() const { return skippedCount_; }

private:
    bool parseLines(const juce::StringArray& lines);
    bool parseHeader(const juce::var& obj);
    bool parseNoteEvent(const juce::var& obj);
    bool parseParamEvent(const juce::var& obj);
    bool parseGenerationEvent(const juce::var& obj);
    bool parsePresetLoadedEvent(const juce::var& obj);
    void sortByTimestamp();

    EventLogHeader header_;
    juce::String   errorMessage_;

    std::vector<NoteEventLogEntry>       noteEvents_;
    std::vector<ParsedParamEvent>        paramEvents_;
    std::vector<GenerationEventLogEntry> generationEvents_;
    std::vector<PresetLoadedLogEntry>    presetLoadedEvents_;

    uint64_t totalDurationSamples_ = 0;
    int      skippedCount_         = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventLogReader)
};