// Find the IGNORED note-off: StepSeq sends NoteOff after a bind, VoiceManager
// drops it (no matching voice) -> voice stuck held -> note-on stops re-attacking.
// Drives the REAL VoiceManager + StepSequencer, logs every note-off and whether
// it matched a voice. Sweeps decay so the bind lands inside the decay phase.
//
// Build: same flags as repro_bind_stuck.cpp (see that file's header).
#include "JuceHeader.h"
#include "dsp/VoiceManager.h"
#include "dsp/BlockParams.h"
#include "sequencer/StepSequencer.h"
#include <cstdio>
#include <vector>
#include <cmath>

using Bind = T5ynthStepSequencer::BindMode;
static float g_tuning[128];

// Returns true if note-off would match voice 0 (mono) before applying it.
static bool wouldMatchMono(VoiceManager& vm, int note) {
    auto& v = vm.getVoice(0);
    return v.isActive() && !v.isReleasing() && v.getCurrentNote() == note;
}

static void run(const char* label, int numSteps, uint64_t bindMask,
                float decayMs, float sustain, float gate, int blocks, bool trace,
                int voiceLimit = 1, float releaseMs = 40.0f)
{
    const double SR = 44100.0; const int BS = 512;
    VoiceManager vm; vm.prepare(SR, BS);
    vm.setTuningTable(g_tuning);
    vm.setEngineMode(SynthVoice::EngineMode::Wavetable);
    vm.setVoiceLimit(voiceLimit);
    BlockParams bp; bp.ampAttack=2.0f; bp.ampDecay=decayMs; bp.ampSustain=sustain; bp.ampRelease=releaseMs;
    vm.setBlockParams(bp);

    T5ynthStepSequencer seq; seq.prepare(SR,BS);
    seq.setNumSteps(numSteps); seq.setBpm(120.0); seq.setDivision(3);
    for (int i=0;i<numSteps;++i){ seq.setStepNote(i,60+(i%5)); seq.setStepEnabled(i,true);
        seq.setStepGate(i,gate); seq.setStepVelocity(i,0.8f);
        if ((bindMask>>i)&1ull) seq.setStepBindMode(i,Bind::Bind); }
    seq.start();

    juce::AudioBuffer<float> buf(2,BS); std::vector<float> lfo((size_t)BS,0.0f);
    std::vector<VoiceEvent> events;
    int offsSent=0, offsIgnored=0, audibleAttacks=0, onsTotal=0;
    int ssAudible=0, ssOn=0; const int ssStart=blocks*6/10;
    // per-voice continuous-active streak (in blocks) over the whole run
    int vStreak[16]={0}, vMaxStreak[16]={0};

    for (int b=0;b<blocks;++b){
        bool steady=b>=ssStart;
        buf.clear(); events.clear();
        seq.processBlock(buf,events);
        int pos=0;
        for (auto& ev: events){
            int off=juce::jlimit(0,BS,ev.sampleOffset);
            if (off>pos){ vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,off-pos); pos=off; }
            if (ev.type==VoiceEvent::Type::NoteOn){
                bool isBind = ev.artic!=VoiceEvent::Articulation::Normal;
                float glideMs=(ev.artic==VoiceEvent::Articulation::Bind)?0.0f:seq.getGlideTime();
                bool wasActive=vm.getVoice(0).isActive(), wasRel=vm.getVoice(0).isReleasing();
                bool fresh = !((wasActive&&!wasRel)||(isBind&&wasActive));
                vm.noteOn(ev.note,ev.velocity,isBind,glideMs,false,false,false,ev.strandId,ev.pan,0);
                ++onsTotal; if (fresh) ++audibleAttacks;
                if (steady){ ++ssOn; if (fresh) ++ssAudible; }
                if (trace) printf("   ON  note=%d %-6s curNote=%d active=%d rel=%d %s\n",
                    ev.note, isBind?"BIND":"normal", vm.getVoice(0).getCurrentNote(),
                    vm.getVoice(0).isActive(), vm.getVoice(0).isReleasing(), fresh?"(reattack)":"(legato/silent)");
            } else {
                // poly-aware: does ANY active, non-releasing voice hold this note?
                bool match=false;
                for (int vI=0; vI<vm.getVoiceLimit(); ++vI){ auto& v=vm.getVoice(vI);
                    if (v.isActive() && !v.isReleasing() && v.getCurrentNote()==ev.note){ match=true; break; } }
                ++offsSent; if (!match) ++offsIgnored;
                if (trace) printf("   OFF note=%d -> %s\n", ev.note, match?"RELEASE":"!!! IGNORED (no match) !!!");
                vm.noteOff(ev.note, ev.strandId);
            }
        }
        if (pos<BS) vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,BS-pos);
        for (int vI=0; vI<vm.getVoiceLimit(); ++vI){
            if (vm.getVoice(vI).isActive()){ ++vStreak[vI]; if (vStreak[vI]>vMaxStreak[vI]) vMaxStreak[vI]=vStreak[vI]; }
            else vStreak[vI]=0;
        }
    }
    bool stuck = (ssOn>=3 && ssAudible==0);
    // a voice active for ~the whole run (no idle) = permanently stuck
    int worstStreak=0; for (int vI=0;vI<vm.getVoiceLimit();++vI) worstStreak=std::max(worstStreak,vMaxStreak[vI]);
    bool permStuck = worstStreak >= blocks-2;
    printf("%-34s vL=%d nSteps=%d bind=0x%llx dec=%4.0f sus=%.2f gate=%.2f rel=%4.0f => offsSent=%d offsIGN=%d ssAud=%d/%d maxStreak=%d %s\n",
        label, vm.getVoiceLimit(), numSteps, (unsigned long long)bindMask, decayMs, sustain, gate, releaseMs,
        offsSent, offsIgnored, ssAudible, ssOn, worstStreak,
        permStuck?" <<< PERM-STUCK VOICE":(stuck?" <<< silent":(offsIgnored>0?" (offs ignored)":"")));
}

int main(){
    for (int i=0;i<128;++i) g_tuning[i]=440.0f*std::pow(2.0f,(i-69)/12.0f);

    printf("=== DECAY x GATE sweep, single bind (step2) in a 16-step pattern ===\n");
    for (float dec : {20.f,50.f,100.f,150.f,200.f,250.f,300.f,400.f,600.f})
        for (float g : {0.3f,0.5f,0.8f,1.0f})
            run("single-bind", 16, 0x4, dec, 0.0f, g, 240, false);

    printf("\n=== DECAY x GATE sweep, two binds (steps 2,3) ===\n");
    for (float dec : {20.f,50.f,100.f,150.f,200.f,250.f,300.f,400.f,600.f})
        for (float g : {0.3f,0.5f,0.8f,1.0f})
            run("two-bind", 16, 0xC, dec, 0.0f, g, 240, false);

    printf("\n=== POLY 4-voice x RELEASE sweep (overlapping tails), single bind ===\n");
    for (float rel : {40.f,150.f,300.f,500.f,800.f,1500.f})
        for (float g : {0.3f,0.5f,0.8f,1.0f})
            run("poly single-bind", 16, 0x4, 30.0f, 0.0f, g, 300, false, 4, rel);

    printf("\n=== POLY 4-voice x RELEASE sweep, two binds ===\n");
    for (float rel : {40.f,150.f,300.f,500.f,800.f,1500.f})
        for (float g : {0.3f,0.5f,0.8f,1.0f})
            run("poly two-bind", 16, 0xC, 30.0f, 0.0f, g, 300, false, 4, rel);

    printf("\n=== POLY traces for the worst offenders ===\n");
    run("poly trace rel=800 gate=0.8", 16, 0x4, 30.0f, 0.0f, 0.8f, 80, true, 4, 800.f);

    return 0;
}
