// PROCESSOR-LEVEL proof of the R0 fix: does the REAL T5ynthProcessor actually
// capture and hand the replay start-state to its writer? The writer/reader chain
// already works (replay_roundtrip_test); what was silently broken is that the
// processor's sticky-restore path never CALLED setStartState — so every real
// session_*.t5evt had startState ABSENT and startReplay() refused it.
//
// Here we construct the actual processor, enable recording, load a "patch"
// (endBulkParamLoad — the same hook standalone's _buffer.t5p load and a DAW's
// setStateInformation both pass through), then let a preset-load marker open the
// file. We then parse the file with EventLogReader and assert the header carries a
// non-empty startState that decodes to the processor's own getStateInformation().
//
// Build: same recipe as the other tools/*.cpp. Exits non-zero on failure.
#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "eventlog/EventLogReader.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++failures; } \
                              else { std::printf("  ok:   %s\n", msg); } } while(0)

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    T5ynthProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    // setEventLogEnabled persists to the MACHINE-WIDE settings store — this harness
    // must not clobber the user's real recording preference. The ctor restored that
    // preference into the processor; snapshot it now and put it back at the end.
    const bool userHadRecordingOn = proc.getEventLogEnabled();

    // Turn recording on at runtime (also covers the sticky-restore semantics: the
    // capture is gated identically). This alone captures the current patch.
    proc.setEventLogEnabled(true);
    CHECK(proc.getEventLogEnabled(), "recording enabled");

    // Simulate the session's initial patch settling AFTER construction — the case
    // the ctor snapshot would have missed. This re-captures the start-state.
    proc.beginBulkParamLoad();
    // (a real load would replaceState here; the marker + capture are what we test)
    proc.endBulkParamLoad("startup_patch");

    // The file opens lazily on the first enqueued event; endBulkParamLoad enqueued a
    // preset_loaded marker, so give the 20 ms drain loop time to write the header.
    for (int i = 0; i < 20 && ! proc.getEventLogCurrentFile().existsAsFile(); ++i)
        juce::Thread::sleep(50);

    const auto file = proc.getEventLogCurrentFile();
    std::printf("Event log file: %s\n", file.getFullPathName().toRawUTF8());
    CHECK(file.existsAsFile(), "processor opened a .t5evt file");

    // What the processor believes its own state is — the start-state must equal this.
    juce::MemoryBlock live;
    proc.getStateInformation(live);
    const auto liveBase64 = juce::Base64::toBase64(live.getData(), live.getSize());

    EventLogReader reader;
    const bool ok = reader.loadFile(file);
    CHECK(ok, "EventLogReader parsed the processor's file");
    const auto& h = reader.getHeader();

    CHECK(h.formatVersion == 2, "formatVersion == 2");
    CHECK(h.startStateBase64.isNotEmpty(),
          "header carries a NON-EMPTY startState (the bug that broke replay)");
    CHECK(h.startStateBase64 == liveBase64,
          "startState equals the processor's own getStateInformation()");

    // And startReplay() must now ACCEPT this tape (it refused every real log before).
    const bool accepted = proc.startReplay(reader);
    CHECK(accepted, "startReplay() accepts the recorded tape");
    CHECK(proc.isReplayActive(), "replay is active after startReplay()");
    proc.stopReplay();
    CHECK(! proc.isReplayActive(), "replay stops cleanly");

    // ── Exercise the audio-thread injection path ────────────────────────────────
    // Build a synthetic tape (this processor's own start-state + a couple of notes
    // near t=0) and drive processBlock during replay. Proves the note-injection +
    // merge-walk path runs without UB and the playhead advances — the transport is
    // live, not just "accepted". (No audio assertion: at init the engines have no
    // generated buffer, so a played note is silent — the dispatch itself is what we
    // exercise here; audibility depends on the tape's own generations.)
    {
        // Serialize through the REAL EventLog serializers so the wire format can't
        // drift from what the reader expects (string keys for source/type/artic).
        auto lineOf = [](juce::DynamicObject::Ptr o)
        { return juce::JSON::toString(juce::var(o.get()), true) + "\n"; };

        EventLogHeader th; th.sampleRate = 48000.0; th.startStateBase64 = liveBase64;
        NoteEventLogEntry on;  on.timestamp = 100;   on.source = NoteEventLogEntry::Source::StepSequencer;
        on.type = VoiceEvent::Type::NoteOn;  on.note = 60; on.velocity = 0.8f;
        on.artic = VoiceEvent::Articulation::Normal; on.strandId = -1; on.pan = 0.0f; on.midiChannel = 0;
        NoteEventLogEntry off = on; off.timestamp = 20000; off.type = VoiceEvent::Type::NoteOff; off.velocity = 0.0f;

        juce::String tape;
        tape << lineOf(eventLogHeaderToDynamicObject(th))
             << lineOf(eventLogEntryToDynamicObject(on))
             << lineOf(eventLogEntryToDynamicObject(off));

        EventLogReader tapeReader;
        const bool tapeOk = tapeReader.loadFromString(tape);
        CHECK(tapeOk, "synthetic tape (start-state + 2 notes) parsed");
        CHECK(tapeReader.getNoteEvents().size() == 2, "synthetic tape has 2 notes");

        CHECK(proc.startReplay(tapeReader), "startReplay() accepts the note tape");

        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const uint64_t p0 = proc.getReplayPlayhead();
        for (int blk = 0; blk < 60; ++blk)   // ~0.64 s — crosses both note events
        {
            buffer.clear();
            midi.clear();
            proc.processBlock(buffer, midi);   // audio-thread injection path runs here
        }
        const uint64_t p1 = proc.getReplayPlayhead();
        CHECK(p1 > p0, "playhead advanced while replaying (transport ran, no crash)");
        CHECK(p1 >= 512ull * 60, "playhead advanced by ~the rendered sample count");

        // Speed control: at ×2 the playhead must cover ~2 tape samples per device
        // sample (fractional carry keeps it exact over whole blocks).
        proc.setReplayRate(2.0f);
        const uint64_t q0 = proc.getReplayPlayhead();
        for (int blk = 0; blk < 20; ++blk)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock(buffer, midi);
        }
        const uint64_t q1 = proc.getReplayPlayhead();
        CHECK(q1 - q0 == 2ull * 512 * 20, "playhead advances at exactly 2x under Speed x2");

        proc.stopReplay();
        CHECK(! proc.isReplayActive(), "note tape stops cleanly");
    }

    // ── Fix #2: a replay start/stop must not leak into the LIVE recording ────────
    // Recording is still on and its file is open. The two startReplay/stopReplay
    // cycles above each ran setStateInformation to restore a patch — those restores
    // must NOT enqueue replay_start/replay_end preset markers into the live log, nor
    // re-stamp its start-state. Drain, re-read the live file, assert it's clean.
    for (int i = 0; i < 10; ++i) juce::Thread::sleep(50);
    {
        EventLogReader live;
        CHECK(live.loadFile(file), "live log re-parsed after the replays");
        int replayMarkers = 0;
        for (const auto& pl : live.getPresetLoadedEvents())
            if (pl.presetName == "replay_start" || pl.presetName == "replay_end")
                ++replayMarkers;
        CHECK(replayMarkers == 0, "no replay_start/replay_end markers leaked into the live log");
        CHECK(live.getHeader().startStateBase64 == liveBase64,
              "live log start-state still the user's patch, not a tape's");
    }

    proc.setEventLogEnabled(userHadRecordingOn);   // restore the user's preference
    file.deleteFile();

    std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
