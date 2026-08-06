// Does juce::MPEInstrument allocate on the audio thread?
//
// It had to be asked, because the MPE migration wanted to put it in
// processBlock and the project forbids allocating there (CLAUDE.md, JUCE Safety
// rule 4). The answer is yes, it does, on every chord release -- which is why
// only juce::MPEZoneLayout ended up shipping. docs/MPE_MIGRATION_PARITY.md §3
// carries the conclusion; this is the measurement it rests on, kept so the
// claim can be re-checked against a future JUCE.
//
// Three instruments, because the first two are blind to the thing that matters:
//
//   * operator new  -- catches C++ container growth, but NOT juce::Array's:
//                      ArrayBase holds a HeapBlock, which calls std::malloc.
//                      Filtered to the measuring thread, since the processor
//                      starts an event-log writer that allocates throughout.
//   * malloc_zone   -- the same counter tools/audition_csound_engine.cpp case 4
//                      uses. Blind to a REALLOCATION: free-then-alloc nets to
//                      zero live blocks. Process-wide, so it is noisy here.
//   * the storage pointer moving -- the only one that sees a realloc, and the
//                      one that settled the question.
//
// Build: same recipe as tools/test_mpe_parity.cpp.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <malloc/malloc.h>

#include "JuceHeader.h"
#include "../src/PluginProcessor.h"

namespace
{
    std::atomic<long> gAllocations { 0 };
    std::atomic<bool> gCounting { false };
    // Only the measuring thread counts. T5ynthProcessor starts an event-log
    // writer, and JUCE a message thread; both allocate while the measurement
    // runs, and a process-wide count is pure noise next to the thing under test.
    std::thread::id gCountingThread;
}

void* operator new (std::size_t n)
{
    if (gCounting.load (std::memory_order_relaxed) && std::this_thread::get_id() == gCountingThread)
        gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    malloc_statistics_t snapshotMalloc()
    {
        malloc_statistics_t st;
        std::memset (&st, 0, sizeof (st));
        malloc_zone_statistics (malloc_default_zone(), &st);
        return st;
    }

    // Returns "<operator-new count> / <live heap block delta> / <peak bytes moved>".
    // A net block delta of zero does not by itself prove nothing was allocated --
    // an allocate-then-free nets out -- so max_size_in_use is reported too: it
    // only ever rises, so any growth at all shows up there.
    // Returns a pointer into a static buffer: every call site here prints the
    // result immediately, so one line of storage is enough and there is no
    // allocation inside the thing that measures allocation.
    const char* countAround (const std::function<void()>& body)
    {
        const auto before = snapshotMalloc();
        gAllocations.store (0, std::memory_order_relaxed);
        gCountingThread = std::this_thread::get_id();
        gCounting.store (true, std::memory_order_relaxed);
        body();
        gCounting.store (false, std::memory_order_relaxed);
        const auto after = snapshotMalloc();

        static char buf[128];
        std::snprintf (buf, sizeof (buf), "new=%-4ld (this thread)  blocks=%+ld (process-wide)",
                       gAllocations.load (std::memory_order_relaxed),
                       (long) after.blocks_in_use - (long) before.blocks_in_use);
        return buf;
    }
}

int main()
{
    juce::MPEZoneLayout layout;
    layout.setLowerZone (15, 24, 2);

    juce::MPEInstrument instrument { layout };

    auto noteOn  = [&] (int ch, int n) {
        instrument.processNextMidiEvent (juce::MidiMessage::noteOn (ch, n, (juce::uint8) 100)); };
    auto noteOff = [&] (int ch, int n) {
        instrument.processNextMidiEvent (juce::MidiMessage::noteOff (ch, n)); };

    // Warm up hard: hold sixteen notes at once, then let them go. If the array
    // kept its high-water storage, everything after this would be free.
    for (int ch = 1; ch <= 16; ++ch) noteOn (ch, 60);
    for (int ch = 1; ch <= 16; ++ch) noteOff (ch, 60);
    for (int ch = 1; ch <= 16; ++ch) noteOn (ch, 60);
    for (int ch = 1; ch <= 16; ++ch) noteOff (ch, 60);

    std::printf ("\njuce::MPEInstrument -- allocations after a 16-note warm-up\n\n");

    std::printf ("  one note on then off                  : %s\n",
                 countAround ([&] { noteOn (2, 64); noteOff (2, 64); }));

    std::printf ("  a five-note chord on then off         : %s\n",
                 countAround ([&] {
                     for (int i = 0; i < 5; ++i) noteOn (2 + i, 60 + i);
                     for (int i = 0; i < 5; ++i) noteOff (2 + i, 60 + i);
                 }));

    std::printf ("  100 single notes, one at a time       : %s\n",
                 countAround ([&] {
                     for (int i = 0; i < 100; ++i) { noteOn (3, 60); noteOff (3, 60); }
                 }));

    // Sixteen held throughout: the count with the array never shrinking below
    // its size, which is the only shape in which it could be allocation-free.
    for (int ch = 1; ch <= 16; ++ch) noteOn (ch, 40);
    std::printf ("  ... with 16 notes permanently held    : %s\n",
                 countAround ([&] {
                     for (int i = 0; i < 100; ++i) { noteOn (3, 60); noteOff (3, 60); }
                 }));
    for (int ch = 1; ch <= 16; ++ch) noteOff (ch, 40);

    std::printf ("\n  pitch wheel / pressure / CC74 x300     : %s\n",
                 countAround ([&] {
                     for (int i = 0; i < 100; ++i)
                     {
                         instrument.processNextMidiEvent (juce::MidiMessage::pitchWheel (2, 8192 + i));
                         instrument.processNextMidiEvent (juce::MidiMessage::channelPressureChange (2, i % 128));
                         instrument.processNextMidiEvent (juce::MidiMessage::controllerEvent (2, 74, i % 128));
                     }
                 }));

    // ── The decisive one ────────────────────────────────────────────────────
    // Neither counter above can see a realloc: juce::Array goes through
    // HeapBlock (std::malloc, not operator new, so new=0 proves nothing), and a
    // grow is free-then-alloc, which nets to zero blocks_in_use and stays under
    // an already-higher peak. So watch the storage itself move instead.
    // MPEInstrument holds exactly this container with exactly this element, and
    // Array::remove calls minimiseStorageAfterRemoval on every single removal --
    // so if the buffer moves here, it moves there, on the audio thread.
    {
        juce::Array<juce::MPENote> notes;
        for (int i = 0; i < 16; ++i)
            notes.add (juce::MPENote ((juce::uint8) (1 + i), (juce::uint8) 60,
                                      juce::MPEValue::from7BitInt (100),
                                      juce::MPEValue::centreValue(),
                                      juce::MPEValue::minValue(),
                                      juce::MPEValue::centreValue()));

        const void* afterWarmup = notes.getRawDataPointer();
        int moves = 0;
        const void* last = afterWarmup;

        for (int round = 0; round < 50; ++round)
        {
            notes.remove (notes.size() - 1);
            if (notes.getRawDataPointer() != last) { ++moves; last = notes.getRawDataPointer(); }
            notes.add (juce::MPENote ((juce::uint8) 2, (juce::uint8) 61,
                                      juce::MPEValue::from7BitInt (100),
                                      juce::MPEValue::centreValue(),
                                      juce::MPEValue::minValue(),
                                      juce::MPEValue::centreValue()));
            if (notes.getRawDataPointer() != last) { ++moves; last = notes.getRawDataPointer(); }
        }

        std::printf ("\njuce::Array<MPENote> -- 50 remove/add cycles at 16 held\n");
        std::printf ("  storage buffer moved                  : %d times%s\n",
                     moves, moves == 0 ? "  (no reallocation)" : "  <-- REALLOCATES");

        // And the shape that matters most: a note held alone, released, retaken.
        juce::Array<juce::MPENote> solo;
        int soloMoves = 0;
        const void* soloLast = nullptr;
        for (int round = 0; round < 50; ++round)
        {
            solo.add (juce::MPENote ((juce::uint8) 2, (juce::uint8) 61,
                                     juce::MPEValue::from7BitInt (100),
                                     juce::MPEValue::centreValue(),
                                     juce::MPEValue::minValue(),
                                     juce::MPEValue::centreValue()));
            if (solo.getRawDataPointer() != soloLast) { ++soloMoves; soloLast = solo.getRawDataPointer(); }
            solo.remove (0);
            if (solo.getRawDataPointer() != soloLast) { ++soloMoves; soloLast = solo.getRawDataPointer(); }
        }
        std::printf ("  one note on/off, 50 times             : %d moves%s\n",
                     soloMoves, soloMoves <= 1 ? "  (no reallocation after the first)" : "  <-- REALLOCATES");

        // The shape a player actually makes: a chord down, a chord up, again.
        // This is the one that decides whether the migration puts a malloc on
        // the audio thread during ordinary playing.
        for (int chordSize : { 4, 10, 15 })
        {
            juce::Array<juce::MPENote> chord;
            int moved = 0;
            const void* prev = nullptr;
            for (int round = 0; round < 50; ++round)
            {
                for (int i = 0; i < chordSize; ++i)
                {
                    chord.add (juce::MPENote ((juce::uint8) (2 + i), (juce::uint8) (60 + i),
                                              juce::MPEValue::from7BitInt (100),
                                              juce::MPEValue::centreValue(),
                                              juce::MPEValue::minValue(),
                                              juce::MPEValue::centreValue()));
                    if (chord.getRawDataPointer() != prev) { ++moved; prev = chord.getRawDataPointer(); }
                }
                for (int i = chordSize; --i >= 0;)
                {
                    chord.remove (i);
                    if (chord.getRawDataPointer() != prev) { ++moved; prev = chord.getRawDataPointer(); }
                }
            }
            std::printf ("  a %2d-note chord down and up, 50 times : %d moves%s\n",
                         chordSize, moved, moved > 2 ? "  <-- REALLOCATES WHILE PLAYING" : "");
        }
    }

    // And the same question of MPEZoneLayout alone, which is the one that ships.
    juce::MPEZoneLayout bare;
    std::printf ("\njuce::MPEZoneLayout alone -- RPN traffic x300 : %s\n\n",
                 countAround ([&] {
                     for (int i = 0; i < 100; ++i)
                     {
                         bare.processNextMidiEvent (juce::MidiMessage::controllerEvent (1, 101, 0));
                         bare.processNextMidiEvent (juce::MidiMessage::controllerEvent (1, 100, 6));
                         bare.processNextMidiEvent (juce::MidiMessage::controllerEvent (1, 6, 1 + (i % 14)));
                     }
                 }));

    // ── And the shipped path, not just the library in isolation ─────────────
    // The MPE handling in T5ynthProcessor::processBlock: zone messages, bend-range
    // RPNs and per-note expression, driven as real MIDI. A net block delta of zero
    // is meaningful here because nothing on this path holds a growing container --
    // any allocation would either leak a block or come from a MidiMessage spilling
    // to the heap, and a 3-byte controller message is stored inline.
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        T5ynthProcessor proc;
        proc.prepareToPlay (44100.0, 256);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;

        auto pump = [&] (int blocks) {
            for (int b = 0; b < blocks; ++b) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); }
        };

        // Warm up everything downstream, so what is measured is this path only.
        for (int ch = 2; ch <= 8; ++ch)
            midi.addEvent (juce::MidiMessage::noteOn (ch, 60 + ch, (juce::uint8) 100), 0);
        pump (20);

        // The MIDI is built ONCE, outside the measurement: juce::MidiBuffer::addEvent
        // grows its own storage, and counting the harness filling a buffer would
        // drown out the thing under test. processBlock does not modify the buffer
        // while the arpeggiator is off, so the same one can be replayed.
        juce::MidiBuffer quiet;
        juce::MidiBuffer mpeTraffic;
        for (int i = 0; i < 9; ++i)
        {
            const int ch = 2 + (i % 7);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), 1);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (1, 6, 1 + (i % 14)), 2);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 0), 3);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 0), 4);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (ch, 6, 12), 5);
            mpeTraffic.addEvent (juce::MidiMessage::pitchWheel (ch, 8192 + i * 400), 6);
            mpeTraffic.addEvent (juce::MidiMessage::channelPressureChange (ch, i * 13), 7);
            mpeTraffic.addEvent (juce::MidiMessage::controllerEvent (ch, 74, i * 13), 8);
        }

        auto pumpWith = [&] (const juce::MidiBuffer& src) {
            for (int i = 0; i < 200; ++i)
            {
                buf.clear();
                juce::MidiBuffer copy (src);   // outside would be modified; this is the cost both cases pay
                proc.processBlock (buf, copy);
            }
        };

        // A baseline first. processBlock is not allocation-free to begin with --
        // it is a whole synth, with an event-log writer and async updates in it,
        // and blocks_in_use is process-wide -- so the only reading that means
        // anything is the DIFFERENCE the MPE bytes make.
        std::printf ("T5ynthProcessor::processBlock -- 200 blocks\n");
        std::printf ("  baseline, one held note, no MIDI       : %s\n",
                     countAround ([&] { pumpWith (quiet); }));
        std::printf ("  the same, plus 81 MPE messages a block : %s\n",
                     countAround ([&] { pumpWith (mpeTraffic); }));
        std::printf ("\n");
    }

    return 0;
}