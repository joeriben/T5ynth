// Answers one question empirically: does the StepSequencer emit a NoteOff in a
// fully-closed bind loop? Print every event the REAL object produces over a few
// passes, for (A) 2 steps both Bind (closed loop) and (B) 2 steps, only step0
// Bind (partial). No VoiceManager — just the raw seq event stream.
//
// Build: same flags as the other repro_*.cpp (see /tmp/h.rsp recipe).
#include "JuceHeader.h"
#include "sequencer/StepSequencer.h"
#include <cstdio>
#include <vector>

using Bind = T5ynthStepSequencer::BindMode;

static const char* artStr(VoiceEvent::Articulation a){
    switch(a){ case VoiceEvent::Articulation::Normal: return "Normal";
               case VoiceEvent::Articulation::Glide:  return "Glide";
               case VoiceEvent::Articulation::Bind:   return "Bind"; }
    return "?";
}

static void run(const char* tag, bool step1Bound)
{
    const double SR=44100.0; const int BS=512;
    T5ynthStepSequencer seq; seq.prepare(SR,BS);
    seq.setNumSteps(2); seq.setBpm(120.0); seq.setDivision(3);   // 0.5x -> 11025 smp/step
    for(int i=0;i<2;++i){ seq.setStepNote(i,60+i); seq.setStepEnabled(i,true);
        seq.setStepGate(i,0.5f); seq.setStepVelocity(i,0.8f); }
    seq.setStepBindMode(0, Bind::Bind);
    seq.setStepBindMode(1, step1Bound ? Bind::Bind : Bind::Off);
    seq.start();

    printf("\n==== %s ====\n", tag);
    juce::AudioBuffer<float> buf(2,BS); std::vector<VoiceEvent> evs;
    int blocksPerStep = 11025/BS + 1;       // ~22 blocks/step
    int totalBlocks = blocksPerStep*2*4;    // ~4 full passes of the 2-step loop
    int nOn=0, nOff=0, step=0;
    for(int b=0; b<totalBlocks; ++b){
        buf.clear(); evs.clear();
        seq.processBlock(buf, evs);
        for(auto& e: evs){
            if(e.type==VoiceEvent::Type::NoteOn){
                ++nOn;
                printf("  blk%4d  NoteOn  note=%d  artic=%-6s\n", b, e.note, artStr(e.artic));
            } else {
                ++nOff;
                printf("  blk%4d  NoteOff note=%d   <-- release\n", b, e.note);
            }
        }
        (void)step;
    }
    printf("  ---- over ~4 passes: NoteOn=%d  NoteOff=%d ----\n", nOn, nOff);
}

int main(){
    run("A) 2 steps, BOTH Bind (fully-closed loop)", true);
    run("B) 2 steps, only step0 Bind (partial)",     false);
    return 0;
}
