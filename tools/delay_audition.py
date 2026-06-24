#!/usr/bin/env python3
"""
Delay-mode audition mock — exact algorithm prototype for the T5ynth delay
rework, rendered to WAV so we judge the *sound* before writing C++.

Modes (the redundant 2-head Tape2 was dropped; the single Tape is the RE-201):
  Digital  : clean dual-mono, per-channel LP-damped feedback (today's baseline)
  PingPong : true ping-pong — input summed to mono into the LEFT line,
             cross-coupled feedback (L->R->L). Clean stereo bounce.
  Tape     : 3-head tape echo, heads at T, 2T, 3T (RE-201 1:2:3), spread-panned,
             wow+flutter + soft saturation + HF loss (Space-Echo character)

Run with the project venv:
  .venv/bin/python tools/delay_audition.py
Outputs WAVs + an impulse-response tap report to tools/delay_audition_out/.
"""
import math, os, struct, wave
import numpy as np

SR = 48000
OUT = os.path.join(os.path.dirname(__file__), "delay_audition_out")
os.makedirs(OUT, exist_ok=True)

# ---- tape-transport speed wobble (dimensionless, mirror the planned C++) -----
# ONE capstan/transport speed modulation m(t) ~ 0, applied MULTIPLICATIVELY to
# every head: delay_k = (k+1)*T*(1+m). Physical: the far head reads tape recorded
# (k+1)*D1 ago, so over its longer record->play gap the speed drifted further from
# "now" — BOTH the delay excursion AND the pitch wobble scale 1:2:3 with transit
# length (the error v(now)-v(now-D_k) grows with D_k for slow drift). Heads are
# NOT pitch-locked; exact for slow wow, fast flutter slightly over-scaled (tiny).
# Wow dominates (capstan, <2 Hz); flutter is faster but far shallower — a high
# freq amplifies the pitch deviation (∝ f), so even small flutter buzzes if hot.
WOW1_HZ     = 0.6          # primary wow (capstan rotation)
WOW2_HZ     = 1.3          # secondary wow, incommensurate -> organic, non-repeating
FLUT_HZ     = 6.0          # flutter shimmer (kept very shallow on purpose)
WOW1_DEPTH  = 0.0018       # fractional speed deviation
WOW2_DEPTH  = 0.0009
FLUT_DEPTH  = 0.00004
TAPE_DRIVE  = 1.6
TAPE_HP_HZ  = 100.0
PAN_WIDTH   = 0.75         # multi-head stereo spread (0..1)
TAPE_DAMP_TOP_HZ = 9000.0  # tape feedback-LP ceiling at Damp=0: an intrinsic
                           # baseline HF loss that COMPOUNDS per pass (Digital
                           # stays open at 20 kHz). Damp trims darker from here.

# ---- BBD (bucket-brigade / analog) — the dark, gritty single-tap archetype ----
BBD_RECON_HZ  = 3000.0     # steep (3-pole) reconstruction LP = the dark BBD top
BBD_HP_HZ     = 180.0      # mild HP: BBDs are mid-focused, thin the lows
BBD_DRIVE     = 2.2        # gentle bucket-brigade grit (a touch more than tape)
BBD_WOW_DEPTH = 0.0006     # subtle clock drift (single slow sine; cleaner than tape)

def damp_fc(d, mode=None):  # matches DelayLine::recomputeDamp per-mode mapping
    top = TAPE_DAMP_TOP_HZ if mode == "Tape" else 20000.0
    return top * (500.0/top) ** d

def one_pole_a(fc):
    return 1.0 - math.exp(-2.0*math.pi*fc/SR)

def frac_read(buf, wpos, delay):
    """Linear-interpolated read `delay` samples behind write pos wpos."""
    rp = (wpos - delay) % len(buf)
    i0 = int(math.floor(rp)); frac = rp - i0
    i1 = (i0 + 1) % len(buf)
    return buf[i0] * (1.0-frac) + buf[i1] * frac

def const_power_pan(p):    # p in [-1,1] -> (gL,gR)
    t = (p + 1.0) * 0.25 * math.pi
    return math.cos(t), math.sin(t)

def make_lp_biquad(fc, sr, Q=0.70710678):
    # RBJ low-pass biquad — matches juce::dsp::IIR::makeLowPass(sr, fc, Q), so the
    # mock's tape damping is the SAME 2-pole/12dB-oct the plugin ships (the old
    # one-pole here was 6dB/oct and under-sold how dark it actually gets).
    w0 = 2*math.pi*fc/sr
    cosw, sinw = math.cos(w0), math.sin(w0)
    alpha = sinw/(2*Q)
    b0 = (1-cosw)/2; b1 = 1-cosw; b2 = (1-cosw)/2
    a0 = 1+alpha;    a1 = -2*cosw; a2 = 1-alpha
    b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0
    s = {"x1":0.0,"x2":0.0,"y1":0.0,"y2":0.0}
    def proc(x):
        y = b0*x + b1*s["x1"] + b2*s["x2"] - a1*s["y1"] - a2*s["y2"]
        s["x2"]=s["x1"]; s["x1"]=x; s["y2"]=s["y1"]; s["y1"]=y
        return y
    return proc

# ---- per-mode render --------------------------------------------------------
def render(mode, dryL, dryR, T_ms=375.0, fb=0.45, mix=0.6, damp=0.45,
           playback=False, pb_floor=500.0):
    n = len(dryL)
    T = T_ms * 0.001 * SR
    cap = int(math.ceil(3.2 * T)) + 8         # holds head 3 (3T) + slack
    bL = np.zeros(cap); bR = np.zeros(cap)
    wp = 0
    aLP = one_pole_a(damp_fc(damp, mode)); lpL = lpR = 0.0
    aHP = one_pole_a(TAPE_HP_HZ);    hp_lp = 0.0
    outL = np.zeros(n); outR = np.zeros(n)
    wow1 = wow2 = flut = 0.0
    dwow1 = 2*math.pi*WOW1_HZ/SR; dwow2 = 2*math.pi*WOW2_HZ/SR; dflut = 2*math.pi*FLUT_HZ/SR
    aRecon = one_pole_a(BBD_RECON_HZ); bbd1 = bbd2 = bbd3 = 0.0   # BBD recon cascade
    aBbdHp = one_pole_a(BBD_HP_HZ);    bbd_hp = 0.0               # BBD mid-focus HP
    tapeFbBq = make_lp_biquad(damp_fc(damp, "Tape"), SR)         # 2-pole, matches C++
    # Playback rolloff tracks the knob too, but bottoms out at pb_floor (>= the
    # feedback's 500 Hz) so cranking Damp doesn't double-crush the top. Feedback
    # still reaches 500 Hz for the cumulative tail darkening.
    pb_fc = TAPE_DAMP_TOP_HZ * (pb_floor/TAPE_DAMP_TOP_HZ) ** damp
    tapePbL = make_lp_biquad(pb_fc, SR)
    tapePbR = make_lp_biquad(pb_fc, SR)

    heads = {"Tape": 3}.get(mode, 1)
    pans = []
    if heads >= 2:
        for k in range(heads):
            p = -PAN_WIDTH + (2*PAN_WIDTH)*k/(heads-1)
            pans.append(const_power_pan(p))

    for i in range(n):
        if mode == "Digital":
            dl = frac_read(bL, wp, T); dr = frac_read(bR, wp, T)
            lpL += aLP*(dl*fb - lpL); lpR += aLP*(dr*fb - lpR)
            bL[wp] = dryL[i] + lpL; bR[wp] = dryR[i] + lpR
            wl, wr = dl, dr

        elif mode == "PingPong":
            mono = 0.5*(dryL[i] + dryR[i])
            dl = frac_read(bL, wp, T); dr = frac_read(bR, wp, T)
            lpL += aLP*(dr*fb - lpL)          # left line fed by RIGHT output (cross)
            lpR += aLP*(dl*fb - lpR)          # right line fed by LEFT output (cross)
            bL[wp] = mono + lpL               # dry (mono) injected to LEFT only
            bR[wp] = lpR
            wl, wr = dl, dr

        elif mode == "BBD":   # bucket-brigade: dark steep recon LP + grit, 1 tap
            mono = 0.5*(dryL[i] + dryR[i])
            wow1 += dwow1                              # subtle clock drift
            raw = frac_read(bL, wp, T*(1.0 + math.sin(wow1)*BBD_WOW_DEPTH))
            # reconstruction filter: 3 cascaded one-poles = steep, dark BBD top
            bbd1 += aRecon*(raw  - bbd1)
            bbd2 += aRecon*(bbd1 - bbd2)
            bbd3 += aRecon*(bbd2 - bbd3)
            bbd_hp += aBbdHp*(bbd3 - bbd_hp)           # mild HP (mid-focus)
            wet = bbd3 - bbd_hp
            # feedback: gentle bucket-brigade grit on the band-limited signal
            f = math.tanh(wet*fb*BBD_DRIVE)/BBD_DRIVE
            bL[wp] = mono + f
            wl = wr = wet                              # mono -> center

        else:  # Tape  (mono tape, panned heads)
            mono = 0.5*(dryL[i] + dryR[i])
            wow1 += dwow1; wow2 += dwow2; flut += dflut
            # ONE shared transport-speed wobble, applied MULTIPLICATIVELY per head
            # so delay excursion AND pitch both scale 1:2:3 with head distance.
            m = (math.sin(wow1)*WOW1_DEPTH + math.sin(wow2)*WOW2_DEPTH
                 + math.sin(flut)*FLUT_DEPTH)
            wl = wr = 0.0
            longest = 0.0
            for k in range(heads):
                tap = frac_read(bL, wp, (k+1)*T*(1.0+m))
                gL, gR = pans[k]
                wl += tap*gL; wr += tap*gR
                if k == heads-1: longest = tap
            # PLAYBACK rolloff (optional): real tape loses HF on EVERY playback,
            # so even the first repeat is already dark — not just via feedback
            # accumulation. Kills the "darkening arrives very late" feel.
            if playback:
                wl = tapePbL(wl); wr = tapePbR(wr)
            # feedback from the long head: HP -> 2-pole LP (matches C++) -> tanh
            f = longest*fb
            hp_lp += aHP*(f - hp_lp); f = f - hp_lp
            f = tapeFbBq(f)
            f = math.tanh(f*TAPE_DRIVE)/TAPE_DRIVE
            bL[wp] = mono + f

        outL[i] = dryL[i]*(1.0-mix) + wl*mix
        outR[i] = dryR[i]*(1.0-mix) + wr*mix
        wp = (wp + 1) % cap
    return outL, outR

# ---- IO + analysis ----------------------------------------------------------
def write_wav(path, L, R):
    x = np.stack([L, R], axis=1)
    peak = np.max(np.abs(x)) or 1.0
    x = np.clip(x/peak*0.89, -1, 1)
    pcm = (x*32767).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm)

def tap_report(mode, L, R):
    """First echo taps (peaks) per channel of the impulse response, in ms."""
    def peaks(x):
        out=[]; thr=0.02
        for i in range(2, len(x)-2):
            if x[i]>thr and x[i]>=x[i-1] and x[i]>x[i+1]:
                out.append((i/SR*1000.0, x[i]))
                if len(out)>=6: break
        return out
    fmt=lambda ps: ", ".join(f"{t:.0f}ms" for t,_ in ps)
    print(f"  {mode:9s} L: {fmt(peaks(L)):40s} R: {fmt(peaks(R))}")

def main():
    dur = int(3.0*SR)
    # impulse (for tap timing) and a short 220Hz pluck (to actually hear it)
    imp = np.zeros(dur); imp[0] = 1.0
    t = np.arange(dur)/SR
    pluck = np.sin(2*math.pi*220*t) * np.exp(-t/0.12)
    pluck[int(0.18*SR):] = 0.0    # one short note, then silence -> hear the tail
    # Bright, harmonically-rich pluck (naive sawtooth, energy up to Nyquist) — the
    # SINE pluck above has no overtones, so a feedback LP above its 220Hz removes
    # nothing audible; DAMP/HF-loss is only judgeable on a spectrally rich source.
    phase = 220.0 * t
    bright = 2.0*(phase - np.floor(phase + 0.5)) * np.exp(-t/0.15)
    bright[int(0.22*SR):] = 0.0

    print("Impulse-response tap timing (T=375ms; expect Digital@375, "
          "PingPong L@375/R@750, Tape@375/750/1125, BBD@375 single):")
    for mode in ["Digital", "PingPong", "Tape", "BBD"]:
        L,R = render(mode, imp, imp.copy(), mix=1.0, fb=0.5)
        tap_report(mode, L, R)
        write_wav(os.path.join(OUT, f"{mode}_impulse.wav"), L, R)
        pl, pr = render(mode, pluck, pluck.copy())
        write_wav(os.path.join(OUT, f"{mode}_pluck.wav"), pl, pr)

    # Tape Damp audition (BRIGHT source) — judge the intrinsic baseline rolloff.
    # damp00 = knob at ZERO (should ALREADY darken across repeats via the 9 kHz
    # feedback ceiling that compounds per pass); 35/70 = the knob trimming further.
    # Digital_bright_ref keeps the top open (no compounding) for the A/B.
    dl, dr = render("Digital", bright, bright.copy(), fb=0.6)
    write_wav(os.path.join(OUT, "Digital_bright_ref.wav"), dl, dr)
    # BBD (bright source — its dark, steep, gritty tone is the whole point; the
    # sine pluck can't show it). A/B against Digital_bright_ref (open) and Tape.
    bl, br = render("BBD", bright, bright.copy(), fb=0.6)
    write_wav(os.path.join(OUT, "BBD_bright.wav"), bl, br)
    # Two tape damping structures, A/B (both now 2-pole, = what ships):
    #   Tape_damp{XX}    = feedback-only (current ship) — repeat 1 is bright, the
    #                      darkening builds up over repeats -> can feel "very late".
    #   Tape_pbdamp{XX}  = + playback rolloff — repeat 1 already dark, still
    #                      compounds. More tape-accurate, darkening arrives sooner.
    # Three tape damping structures, A/B (all 2-pole, = what ships):
    #   Tape_damp{XX}    = feedback-only (the OLD ship) — repeat 1 bright, darkening
    #                      builds up over repeats -> can feel "very late".
    #   Tape_pbdamp{XX}  = + playback rolloff, floor 500 — repeat 1 already dark but
    #                      at 70 the playback+feedback double-crush is a bit too dark.
    #   Tape_pbtame{XX}  = + playback rolloff, floor 750 (SHIPPED) — playback bottoms
    #                      higher so 70 isn't crushed; feedback still reaches 500 Hz.
    for d in [0.0, 0.35, 0.7]:
        x = int(round(d*100))
        pl, pr = render("Tape", bright, bright.copy(), fb=0.6, damp=d)
        write_wav(os.path.join(OUT, f"Tape_damp{x:02d}.wav"), pl, pr)
        pl, pr = render("Tape", bright, bright.copy(), fb=0.6, damp=d, playback=True)
        write_wav(os.path.join(OUT, f"Tape_pbdamp{x:02d}.wav"), pl, pr)
        pl, pr = render("Tape", bright, bright.copy(), fb=0.6, damp=d,
                        playback=True, pb_floor=750.0)
        write_wav(os.path.join(OUT, f"Tape_pbtame{x:02d}.wav"), pl, pr)
    print(f"\nWAVs -> {OUT}")

if __name__ == "__main__":
    main()
