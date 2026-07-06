#!/usr/bin/env python3
"""Faithful sim of StepSequencer.processBlock event gen + mono VoiceManager.
Goal: reproduce 'bind 2 steps + short decay -> voice stuck, no more notes'."""

OFF, BIND, GLIDE = 0, 1, 2
NORMAL, A_BIND, A_GLIDE = 'Normal', 'Bind', 'Glide'
STEP_DUR = 11025.0  # 1/8 @120bpm,44100

class Step:
    def __init__(self, note=60, vel=0.8, gate=0.8, enabled=True, bind=OFF):
        self.note=note; self.vel=vel; self.gate=gate; self.enabled=enabled; self.bind=bind

class Seq:
    def __init__(self, steps, numSteps):
        self.steps=steps; self.numSteps=numSteps
        self.samplesUntilNextStep=0.0
        self.samplesUntilGateOff=-1.0
        self.scheduledStep=0
        self.lastPlayedNote=-1
        self.gpos=0  # global sample pos
    def processBlock(self, numSamples, out):
        samplePos=0
        while samplePos < numSamples:
            remaining = numSamples - samplePos
            nextEvent = self.samplesUntilNextStep
            gateOffFirst=False
            if self.samplesUntilGateOff>=0.0 and self.samplesUntilGateOff<=self.samplesUntilNextStep:
                nextEvent=self.samplesUntilGateOff; gateOffFirst=True
            if nextEvent > remaining:
                self.samplesUntilNextStep-=remaining
                if self.samplesUntilGateOff>=0.0: self.samplesUntilGateOff-=remaining
                samplePos=numSamples; break
            advance=max(0,int(nextEvent))
            samplePos+=advance
            self.samplesUntilNextStep-=advance
            if self.samplesUntilGateOff>=0.0: self.samplesUntilGateOff-=advance
            gpos=self.gpos+samplePos
            if gateOffFirst:
                if self.lastPlayedNote>=0:
                    out.append((gpos,'NoteOff',self.lastPlayedNote,NORMAL)); self.lastPlayedNote=-1
                self.samplesUntilGateOff=-1.0; continue
            stepIdx=self.scheduledStep % self.numSteps
            step=self.steps[stepIdx]
            prevIdx=(self.scheduledStep-1+self.numSteps)%self.numSteps
            prevSlid = self.scheduledStep>0 and self.steps[prevIdx].enabled and self.steps[prevIdx].bind!=OFF
            if self.lastPlayedNote>=0 and not prevSlid:
                out.append((gpos,'NoteOff',self.lastPlayedNote,NORMAL)); self.lastPlayedNote=-1
            if step.enabled:
                midiNote=max(0,min(127,step.note))
                incoming = self.steps[prevIdx].bind if prevSlid else OFF
                artic = A_GLIDE if incoming==GLIDE else A_BIND if incoming==BIND else NORMAL
                out.append((gpos,'NoteOn',midiNote,artic)); self.lastPlayedNote=midiNote
                nextIdx=(self.scheduledStep+1)%self.numSteps
                slidesToNext = step.bind!=OFF and self.steps[nextIdx].enabled
                self.samplesUntilGateOff = -1.0 if slidesToNext else step.gate*STEP_DUR
            else:
                self.samplesUntilGateOff=-1.0
            self.scheduledStep+=1
            self.samplesUntilNextStep+=STEP_DUR
        self.gpos+=numSamples

# ---- ADSR (short decay, low sustain = pluck) ----
class Env:
    def __init__(self,a,d,s,r):
        self.a=a; self.d=d; self.s=s; self.r=r
        self.stage='idle'; self.pos=0; self.level=0.0; self.relStart=0.0
    def noteOn(self):
        self.stage='attack'; self.pos=0
    def noteOff(self):
        if self.stage!='idle':
            self.relStart=self.level; self.stage='release'; self.pos=0
    def isIdle(self): return self.stage=='idle'
    def advance(self,n):
        while n>0 and self.stage!='idle':
            if self.stage=='attack':
                left=self.a-self.pos
                if n<left: self.pos+=n; self.level=self.pos/self.a; n=0
                else: n-=left; self.level=1.0; self.stage='decay'; self.pos=0
            elif self.stage=='decay':
                left=self.d-self.pos
                if n<left: self.pos+=n; self.level=1.0-(1.0-self.s)*(self.pos/self.d); n=0
                else: n-=left; self.level=self.s; self.stage='sustain'; self.pos=0
            elif self.stage=='sustain':
                # stays until noteOff
                self.level=self.s; n=0
            elif self.stage=='release':
                left=self.r-self.pos
                if n<left: self.pos+=n; self.level=self.relStart*(1.0-self.pos/self.r); n=0
                else: n-=left; self.level=0.0; self.stage='idle'; self.pos=0

class MonoVoice:
    def __init__(self,a,d,s,r):
        self.active=False; self.noteHeld=False; self.currentNote=-1
        self.env=Env(a,d,s,r)
    def releasing(self): return self.active and not self.noteHeld
    def advance_to(self,gpos,cur):
        self.env.advance(gpos-cur)
        if self.env.isIdle() and not self.noteHeld:
            self.active=False
    def hard_noteOn(self,note):
        self.currentNote=note; self.noteHeld=True; self.active=True; self.env.noteOn()
    def glide(self,note):
        self.currentNote=note  # no env retrigger
    def voice_noteOff(self):
        self.noteHeld=False; self.env.noteOff()

def run(label, steps, numSteps, env_params, blocks=400, blocksize=512, verbose_from=None):
    seq=Seq(steps,numSteps)
    v=MonoVoice(*env_params)
    cur=0
    triggers=0; legato_silent=0; total_noteon=0
    log=[]
    for b in range(blocks):
        out=[]
        seq.processBlock(blocksize, out)
        for (gpos,kind,note,artic) in out:
            v.advance_to(gpos,cur); cur=gpos
            if kind=='NoteOn':
                total_noteon+=1
                isBind = artic!=NORMAL
                legato = v.active and not v.releasing()
                took_legato = legato or (isBind and v.active)
                if took_legato:
                    if v.releasing():
                        v.hard_noteOn(v.currentNote)  # re-hold
                    fresh = False
                    v.glide(note)
                    # would this be audible? env not in attack/at-zero idle?
                    silent = v.env.isIdle() or v.env.level<0.001
                    if silent: legato_silent+=1
                    log.append((gpos,'ON',note,artic,'LEGATO/glide','silent' if silent else 'aud'))
                else:
                    v.hard_noteOn(note); triggers+=1
                    log.append((gpos,'ON',note,artic,'FRESH-TRIGGER',''))
            else: # NoteOff
                v.advance_to(gpos,cur); cur=gpos
                if v.active and not v.releasing() and v.currentNote==note:
                    v.voice_noteOff()
                    log.append((gpos,'OFF',note,'','MATCH->release',''))
                else:
                    log.append((gpos,'OFF',note,'','NO-MATCH(stale)',
                                f'vActive={v.active} vRel={v.releasing()} vNote={v.currentNote}'))
    print(f"\n=== {label} ===")
    print(f"noteOns={total_noteon} fresh-triggers={triggers} legato-glides={total_noteon-triggers} "
          f"silent-legato={legato_silent}")
    # show tail behavior
    print("  last 14 events:")
    for e in log[-14:]:
        print("   ", e)
    return triggers, total_noteon

# Pattern: 16 steps, all enabled normal pluck. Bind steps 2->3 (step2 Bind, step3 normal).
def mkpat():
    s=[Step(note=60+i%5, vel=0.8, gate=0.5, enabled=True, bind=OFF) for i in range(16)]
    return s

# Short decay, zero sustain (pluck) — the user's 'kurzer decay'
SHORT=(50,200,0.0,300)
MEDSUS=(50,200,0.3,300)

# Case A: one bind (step2 Bind -> step3)
s=mkpat(); s[2].bind=BIND
run("A: single Bind step2->3, pluck(sust0)", s, 16, SHORT)

# Case B: two adjacent bind steps (step2 Bind, step3 Bind -> chain into step4)
s=mkpat(); s[2].bind=BIND; s[3].bind=BIND
run("B: two Bind steps 2,3 (chain), pluck(sust0)", s, 16, SHORT)

# Case C: bind across loop boundary (last step bound)
s=mkpat(); s[15].bind=BIND
run("C: Bind last step15->0 (loop wrap), pluck(sust0)", s, 16, SHORT)

# Case D: two bind with moderate sustain
s=mkpat(); s[2].bind=BIND; s[3].bind=BIND
run("D: two Bind steps 2,3, sustain0.3", s, 16, MEDSUS)

# Case E: gate=1.0 full (bound note held full step, then gate-off coincides)
s=mkpat();
for st in s: st.gate=1.0
s[2].bind=BIND
run("E: single Bind, gate=1.0, pluck", s, 16, SHORT)
