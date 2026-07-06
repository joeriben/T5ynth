// "Erst im 2. Durchlauf": run the REAL VoiceManager + StepSequencer for several
// FULL passes (mono, Freeze/granular) and account, PER PASS, for audible
// re-attacks and ignored note-offs. The earlier harness ran <1 pass, so it
// never reached the loop wrap / pass-2 state where the user sees the voice die.
//
// Build: same flags as repro_bind_stuck.cpp (see /tmp/h.rsp recipe in its header).
#include "JuceHeader.h"
#include "dsp/VoiceManager.h"
#include "dsp/BlockParams.h"
#include "sequencer/StepSequencer.h"
#include <cstdio>
#include <vector>
#include <cmath>

using Bind = T5ynthStepSequencer::BindMode;
static float g_tuning[128];

// Exact mono predicate: would this note-on produce an AUDIBLE re-attack
// (fresh trigger or re-hold of a releasing voice), or just a silent glide?
static bool audibleAttack(bool wasActive, bool wasRel, bool isBind) {
    bool bindPath = (wasActive && !wasRel) || (isBind && wasActive);
    if (!bindPath) return true;   // else-branch: v.noteOn(note) fresh
    return wasRel;                // bind path: re-hold if releasing, else silent glide
}

struct PassStat { int noteOns=0, audible=0, offsSent=0, offsIgnored=0; };

static int run(const char* label, int numSteps, uint64_t bindMask,
                float decayMs, float sustain, float gate, int passes,
                SynthVoice::EngineMode eng, bool trace, uint64_t enableMask = ~0ull)
{
    const double SR = 44100.0; const int BS = 512;
    VoiceManager vm; vm.prepare(SR, BS);
    vm.setTuningTable(g_tuning);
    vm.setEngineMode(eng);
    vm.setVoiceLimit(1);
    BlockParams bp; bp.ampAttack=2.0f; bp.ampDecay=decayMs; bp.ampSustain=sustain; bp.ampRelease=40.0f;
    vm.setBlockParams(bp);

    T5ynthStepSequencer seq; seq.prepare(SR,BS);
    seq.setNumSteps(numSteps); seq.setBpm(120.0); seq.setDivision(3); // 1/8 = 11025 smp/step
    for (int i=0;i<numSteps;++i){ bool en=(enableMask>>i)&1ull;
        seq.setStepNote(i,60+(i%5)); seq.setStepEnabled(i,en);
        seq.setStepGate(i,gate); seq.setStepVelocity(i,0.8f);
        if ((bindMask>>i)&1ull) seq.setStepBindMode(i,Bind::Bind); }
    seq.start();

    const double smpPerStep = SR*60.0/120.0*0.5;          // 11025
    const int blocksPerPass = (int)std::ceil(smpPerStep*numSteps/BS) + 1;
    const int blocks = blocksPerPass*passes + 4;

    juce::AudioBuffer<float> buf(2,BS); std::vector<float> lfo((size_t)BS,0.0f);
    std::vector<VoiceEvent> events;
    std::vector<PassStat> ps((size_t)passes+2);

    for (int b=0;b<blocks;++b){
        int pass = b / blocksPerPass; if (pass >= (int)ps.size()) pass=(int)ps.size()-1;
        buf.clear(); events.clear();
        seq.processBlock(buf,events);
        int pos=0;
        for (auto& ev: events){
            int off=juce::jlimit(0,BS,ev.sampleOffset);
            if (off>pos){ vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,off-pos); pos=off; }
            auto& v0=vm.getVoice(0);
            if (ev.type==VoiceEvent::Type::NoteOn){
                bool isBind = ev.artic!=VoiceEvent::Articulation::Normal;
                float glideMs=(ev.artic==VoiceEvent::Articulation::Bind)?0.0f:seq.getGlideTime();
                bool wasActive=v0.isActive(), wasRel=v0.isReleasing();
                bool aud=audibleAttack(wasActive,wasRel,isBind);
                vm.noteOn(ev.note,ev.velocity,isBind,glideMs,false,false,false,ev.strandId,ev.pan,0);
                ps[pass].noteOns++; if (aud) ps[pass].audible++;
                if (trace) printf("  p%d ON  n=%d %-4s wasAct=%d wasRel=%d -> %s  (curNote now %d)\n",
                    pass, ev.note, isBind?"BIND":"norm", wasActive, wasRel,
                    aud?"AUDIBLE":"silent-glide", v0.getCurrentNote());
            } else {
                bool match = v0.isActive() && !v0.isReleasing() && v0.getCurrentNote()==ev.note;
                ps[pass].offsSent++; if (!match) ps[pass].offsIgnored++;
                if (trace) printf("  p%d OFF n=%d -> %s (vAct=%d vRel=%d vNote=%d)\n",
                    pass, ev.note, match?"release":"!!! IGNORED !!!",
                    v0.isActive(), v0.isReleasing(), v0.getCurrentNote());
                vm.noteOff(ev.note, ev.strandId);
            }
        }
        if (pos<BS) vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,BS-pos);
    }

    // A pass is DEAD if it has note-ons but zero audible attacks (all silent glides).
    int firstDead=-1, totIgn=0;
    for (int p=0;p<passes;++p) totIgn+=ps[p].offsIgnored;
    for (int p=1;p<passes;++p) if (ps[p].noteOns>=1 && ps[p].audible==0){ firstDead=p; break; }
    if (trace || firstDead>=0 || totIgn>0){
        printf("%-26s eng=%d n=%d en=0x%llx bind=0x%llx dec=%4.0f g=%.2f | aud/ons(ign): ",
            label,(int)eng,numSteps,(unsigned long long)(enableMask&((1ull<<numSteps)-1)),
            (unsigned long long)bindMask,decayMs,gate);
        for (int p=0;p<passes;++p) printf("%d/%d(%d) ", ps[p].audible, ps[p].noteOns, ps[p].offsIgnored);
        if (firstDead>=0) printf(" <<< DEAD from pass %d", firstDead);
        if (totIgn>0)    printf(" <<< %d note-off(s) IGNORED", totIgn);
        printf("\n");
    }
    return (firstDead>=0?1:0) | (totIgn>0?2:0);
}

// Reproduce the user's 3-step sequence: closed bind loop, mono N passes ->
// switch to 4 voices M passes -> back to mono. Dump per-voice state each pass.
static void switchTest(int numSteps, uint64_t bindMask, uint64_t enableMask,
                       float decayMs, float gate, SynthVoice::EngineMode eng)
{
    const double SR=44100.0; const int BS=512;
    VoiceManager vm; vm.prepare(SR,BS); vm.setTuningTable(g_tuning);
    vm.setEngineMode(eng); vm.setVoiceLimit(1);
    BlockParams bp; bp.ampAttack=2.0f; bp.ampDecay=decayMs; bp.ampSustain=0.0f; bp.ampRelease=40.0f;
    vm.setBlockParams(bp);
    T5ynthStepSequencer seq; seq.prepare(SR,BS);
    seq.setNumSteps(numSteps); seq.setBpm(120.0); seq.setDivision(3);
    for (int i=0;i<numSteps;++i){ bool en=(enableMask>>i)&1ull;
        seq.setStepNote(i,60+(i%5)); seq.setStepEnabled(i,en);
        seq.setStepGate(i,gate); seq.setStepVelocity(i,0.8f);
        if ((bindMask>>i)&1ull) seq.setStepBindMode(i,Bind::Bind); }
    seq.start();
    const int bpp=(int)std::ceil(SR*60.0/120.0*0.5*numSteps/BS)+1;
    juce::AudioBuffer<float> buf(2,BS); std::vector<float> lfo((size_t)BS,0.0f);
    std::vector<VoiceEvent> events;
    // schedule: passes 0,1 mono ; 2,3 four-voice ; 4,5 mono again
    auto phaseFor=[&](int p){ return p<2?"MONO":p<4?"4-VOICE":"MONO-again"; };
    int totalPasses=6;
    printf("\n=== SWITCH TEST: n=%d en=0x%llx bind=0x%llx dec=%.0f gate=%.2f ===\n",
        numSteps,(unsigned long long)(enableMask&((1ull<<numSteps)-1)),
        (unsigned long long)bindMask,decayMs,gate);
    for (int b=0, prevPass=-1; b<bpp*totalPasses+4; ++b){
        int pass=b/bpp; if (pass>=totalPasses) break;
        if (pass!=prevPass){
            if (pass==2) vm.setVoiceLimit(4);
            if (pass==4) vm.setVoiceLimit(1);
            // dump voice states at the START of each pass
            printf(" pass %d [%-10s vL=%d] voices: ", pass, phaseFor(pass), vm.getVoiceLimit());
            for (int v=0; v<4; ++v){ auto& vc=vm.getVoice(v);
                printf("v%d{a=%d r=%d n=%d} ", v, vc.isActive(), vc.isReleasing(), vc.getCurrentNote()); }
            printf("\n");
            prevPass=pass;
        }
        buf.clear(); events.clear(); seq.processBlock(buf,events);
        int pos=0;
        for (auto& ev: events){ int off=juce::jlimit(0,BS,ev.sampleOffset);
            if (off>pos){ vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,off-pos); pos=off; }
            if (ev.type==VoiceEvent::Type::NoteOn){
                bool isBind=ev.artic!=VoiceEvent::Articulation::Normal;
                float gms=(ev.artic==VoiceEvent::Articulation::Bind)?0.0f:seq.getGlideTime();
                vm.noteOn(ev.note,ev.velocity,isBind,gms,false,false,false,ev.strandId,ev.pan,0);
            } else vm.noteOff(ev.note,ev.strandId);
        }
        if (pos<BS) vm.renderBlock(buf,bp,lfo.data(),lfo.data(),lfo.data(),pos,BS-pos);
    }
}

int main(){
    for (int i=0;i<128;++i) g_tuning[i]=440.0f*std::pow(2.0f,(i-69)/12.0f);
    auto FZ = SynthVoice::EngineMode::Freeze;
    auto WT = SynthVoice::EngineMode::Wavetable;

    switchTest(2, 0x3, 0x3, 80.f, 0.5f, FZ);    // closed 2-step loop
    switchTest(4, 0xF, 0xF, 80.f, 0.5f, FZ);    // closed 4-step loop (varying notes)

    printf("=== EXHAUSTIVE enable x bind sweep, FREEZE, mono, 4 passes ===\n");
    printf("(only DEAD passes or IGNORED note-offs are printed)\n");
    int hits=0;
    for (int n : {2,3,4,5,6}){
        uint64_t full=(1ull<<n)-1;
        for (uint64_t en=1; en<=full; ++en){
            for (uint64_t bm=0; bm<=full; ++bm){
                if (bm & ~en) continue;          // only bind enabled steps
                if (bm==0) continue;             // need at least one bind
                for (float dec : {30.f,80.f,150.f})
                    for (float g : {0.5f,1.0f}){
                        char lab[32]; snprintf(lab,sizeof lab,"sweep n=%d",n);
                        hits += (run(lab, n, bm, dec, 0.0f, g, 4, FZ, false, en)?1:0);
                    }
            }
        }
    }
    printf(">>> total problem configs: %d\n\n", hits);

    printf("=== TRACE: 2-step pattern, both bound (closed loop?) ===\n");
    run("2-step both-bound", 2, 0x3, 80.f, 0.0f, 0.5f, 3, FZ, true, 0x3);

    printf("=== FREEZE (granular), mono, 4 passes, 2 adjacent binds at various positions ===\n");
    // pattern lengths the user might use; binds placed mid, at wrap, at start
    for (int n : {4,8,16}){
        // binds at steps (n-2,n-1): the LAST two -> last slides into step0 of next pass (wrap)
        uint64_t wrap = (1ull<<(n-2))|(1ull<<(n-1));
        run("FZ binds@last2 (wrap)", n, wrap, 80.f, 0.0f, 0.5f, 4, FZ, false);
        // binds at steps 2,3 (mid)
        run("FZ binds@2,3 (mid)", n, 0xC, 80.f, 0.0f, 0.5f, 4, FZ, false);
        // binds at steps 0,1 (start)
        run("FZ binds@0,1 (start)", n, 0x3, 80.f, 0.0f, 0.5f, 4, FZ, false);
    }

    printf("\n=== same on WAVETABLE (is it engine-specific?) ===\n");
    for (int n : {4,8,16}){
        uint64_t wrap = (1ull<<(n-2))|(1ull<<(n-1));
        run("WT binds@last2 (wrap)", n, wrap, 80.f, 0.0f, 0.5f, 4, WT, false);
        run("WT binds@2,3 (mid)", n, 0xC, 80.f, 0.0f, 0.5f, 4, WT, false);
    }

    printf("\n=== DECAY x GATE sweep on FREEZE, wrap binds, 8 steps, 4 passes ===\n");
    for (float dec : {30.f,80.f,150.f,300.f})
        for (float g : {0.3f,0.5f,0.8f,1.0f}){
            uint64_t wrap=(1ull<<6)|(1ull<<7);
            run("FZ wrap sweep", 8, wrap, dec, 0.0f, g, 4, FZ, false);
        }

    return 0;
}
