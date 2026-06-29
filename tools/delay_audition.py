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
           playback=False, pb_floor=500.0, bbd_recon_hz=4500.0, bbd_loop_hz=6000.0,
           wow1_depth=WOW1_DEPTH, wow2_depth=WOW2_DEPTH,
           flut_depth=FLUT_DEPTH, flut2_depth=0.0, flut2_hz=9.7,
           bbd_drive=BBD_DRIVE, bbd_clock_depth=BBD_WOW_DEPTH):
    n = len(dryL)
    T = T_ms * 0.001 * SR
    cap = int(math.ceil(3.2 * T)) + 8         # holds head 3 (3T) + slack
    bL = np.zeros(cap); bR = np.zeros(cap)
    wp = 0
    aLP = one_pole_a(damp_fc(damp, mode)); lpL = lpR = 0.0
    aHP = one_pole_a(TAPE_HP_HZ);    hp_lp = 0.0
    outL = np.zeros(n); outR = np.zeros(n)
    wow1 = wow2 = flut = flut2 = 0.0
    dwow1 = 2*math.pi*WOW1_HZ/SR; dwow2 = 2*math.pi*WOW2_HZ/SR
    dflut = 2*math.pi*FLUT_HZ/SR; dflut2 = 2*math.pi*flut2_hz/SR
    aRecon = one_pole_a(bbd_recon_hz); bbd1 = bbd2 = bbd3 = 0.0   # BBD recon cascade (OUT)
    aFbLp  = one_pole_a(bbd_loop_hz);  bbd_fblp = 0.0             # BBD feedback loop LP
    aBbdHp = one_pole_a(BBD_HP_HZ);    bbd_hp = bbd_fbhp = 0.0    # BBD HP (out + fb)
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
            raw = frac_read(bL, wp, T*(1.0 + math.sin(wow1)*bbd_clock_depth))
            # OUTPUT path: 3 cascaded one-poles = steep, dark BBD top + mid HP.
            # This is ONLY on what you hear; it never touches the feedback loop.
            bbd1 += aRecon*(raw  - bbd1)
            bbd2 += aRecon*(bbd1 - bbd2)
            bbd3 += aRecon*(bbd2 - bbd3)
            bbd_hp += aBbdHp*(bbd3 - bbd_hp)           # mild HP (mid-focus)
            wet = bbd3 - bbd_hp                        # what you HEAR (full dark + HP)
            # FEEDBACK path: a SEPARATE, more-open loop LP (bbd_loop_hz) on the RAW
            # tap so the loop keeps gain and BLOOMS toward self-osc — the dark 3-pole
            # recon is NOT in the loop. NO mid HP here: a 180Hz HP in a 375ms loop
            # compounds over ~13 passes and strips the fundamental, killing sustain;
            # the mid-focus HP lives on the OUTPUT (applied once). + grit only.
            bbd_fblp += aFbLp*(raw - bbd_fblp)
            f = math.tanh(bbd_fblp*fb*bbd_drive)/bbd_drive
            bL[wp] = mono + f
            wl = wr = wet                              # mono -> center

        else:  # Tape  (mono tape, panned heads)
            mono = 0.5*(dryL[i] + dryR[i])
            wow1 += dwow1; wow2 += dwow2; flut += dflut; flut2 += dflut2
            # ONE shared transport-speed wobble, applied MULTIPLICATIVELY per head
            # so delay excursion AND pitch both scale 1:2:3 with head distance.
            # flut2 is a second incommensurate flutter LFO (depth=0 unless Wild).
            m = (math.sin(wow1)*wow1_depth + math.sin(wow2)*wow2_depth
                 + math.sin(flut)*flut_depth + math.sin(flut2)*flut2_depth)
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
    # BBD brightness at the OPEN end — recon top sweep (fb_tap=1 = bright loop).
    # The shipped 3000 felt too dark at Damp 0; 4500/6000 lift the top while
    # keeping the steep 3-pole BBD character. A/B against Digital_bright_ref (open).
    for top in [3000, 4500, 6000]:
        bl, br = render("BBD", bright, bright.copy(), fb=0.6, bbd_recon_hz=float(top))
        write_wav(os.path.join(OUT, f"BBD_top{top}.wav"), bl, br)
    # SELF-OSCILLATION at near-max feedback (0.9). 6 s source: one note then long
    # silence so the TAIL is the whole story. The dark recon is OUT of the loop now;
    # the loop is a separate gentle LP — sweep its cutoff to dial how much it blooms.
    # Higher loop cutoff = brighter loop = blooms more (closer to the TAPE target).
    durSO = int(6.0*SR); tSO = np.arange(durSO)/SR
    phSO = 220.0*tSO
    brightSO = 2.0*(phSO - np.floor(phSO + 0.5)) * np.exp(-tSO/0.15)
    brightSO[int(0.22*SR):] = 0.0
    # fb 0.95 = the user's "maximal". Loop cutoff now shapes the REPEAT tone (how
    # dark the tail melts), not the sustain length — pick by ear.
    for loop in [3000, 6000, 9000]:
        bl, br = render("BBD", brightSO, brightSO.copy(), fb=0.95,
                        bbd_recon_hz=4500.0, bbd_loop_hz=float(loop))
        write_wav(os.path.join(OUT, f"BBD_selfosc_loop{loop}.wav"), bl, br)
    bl, br = render("Tape", brightSO, brightSO.copy(), fb=0.95, damp=0.0)
    write_wav(os.path.join(OUT, "BBD_selfosc_TAPEref.wav"), bl, br)
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
    # ── Tape character-preset audition (Natural / Warm / Wild) ──────────────────
    # Three candidate presets rendered side-by-side on a bright pluck + long tail:
    #   Natural = current shipped values (baseline)
    #   Warm    = 2x wow, 20x flutter  (moderate movement)
    #   Wild    = old Tp3 feel (heavy flutter at ~0.003 fractional depth)
    # Judge on the fluttery tail: Warm should feel "analog warmth", Wild should
    # flutter visibly like a wobbly tape machine.
    # Wild uses two incommensurate flutter LFOs (6.0 + 9.7 Hz, multiplicative)
    # — physically: capstan flutter + tape-guide friction at a different rate.
    # Warm: slow/stronger wow for organic warmth, fast/subtle flutter for light shimmer.
    # Both flutter LFOs (6.0 + 9.7 Hz) are ALWAYS present in every preset — like a
    # real machine where capstan flutter and tape-guide friction both vibrate at
    # all times. Even unhearably subtle beats a hard zero. Staffelung Natural<Warm<Wild.
    TAPE_CHARS = {
        "Natural": dict(wow1_depth=0.0018, wow2_depth=0.0009, flut_depth=0.00004, flut2_depth=0.00003, flut2_hz=9.7),
        "Warm":    dict(wow1_depth=0.004,  wow2_depth=0.002,  flut_depth=0.0002,  flut2_depth=0.0003,  flut2_hz=9.7),
        "Wild":    dict(wow1_depth=0.008,  wow2_depth=0.004,  flut_depth=0.0008,  flut2_depth=0.0006,  flut2_hz=9.7),
    }
    dur2 = int(4.0*SR); t2 = np.arange(dur2)/SR
    ph2 = 220.0*t2
    pluck2 = 2.0*(ph2 - np.floor(ph2 + 0.5)) * np.exp(-t2/0.15)
    pluck2[int(0.22*SR):] = 0.0   # short note, then silence — hear the whole tail
    print("\nTape character presets (Natural / Warm / Wild):")
    for name, kw in TAPE_CHARS.items():
        pl, pr = render("Tape", pluck2.copy(), pluck2.copy(),
                        T_ms=375.0, fb=0.55, mix=0.65, damp=0.3, playback=True, pb_floor=750.0,
                        **kw)
        path = os.path.join(OUT, f"Tape_{name}.wav")
        write_wav(path, pl, pr)
        f2 = kw.get('flut2_depth', 0.0); hz2 = kw.get('flut2_hz', 9.7)
        print(f"  {name:8s}: wow={kw['wow1_depth']:.4f}/{kw['wow2_depth']:.4f}  "
              f"flut={kw['flut_depth']:.5f}@6Hz + {f2:.4f}@{hz2}Hz -> {path}")

    print("\nWarm zitter sweep (9.7Hz, wow fixed at 0.004/0.002, flut@6Hz=0):")
    for fd in [0.0004, 0.0006, 0.001, 0.002]:
        pl, pr = render("Tape", pluck2.copy(), pluck2.copy(),
                        T_ms=375.0, fb=0.55, mix=0.65, damp=0.3, playback=True, pb_floor=750.0,
                        wow1_depth=0.004, wow2_depth=0.002,
                        flut_depth=0.0, flut2_depth=fd, flut2_hz=9.7)
        label = f"{fd:.4f}".replace("0.", "").rstrip("0")
        path = os.path.join(OUT, f"Tape_Warm_z{label}.wav")
        write_wav(path, pl, pr)
        print(f"  9.7Hz depth={fd:.4f} -> {path}")

    # ── BBD character-preset audition (Clean / Vintage / Degraded) ──────────────
    # Real bucket-brigade units scatter from bright/short chorus-echo (Boss DM-2,
    # early stage) to dark/gritty/degraded (EHX Memory Man cranked, or a worn unit).
    # The character axis varies four constants together:
    #   recon_hz  — output reconstruction LP top: darkness (lower = darker)
    #   loop_hz   — feedback-loop LP: how brightly the tail blooms
    #   drive     — bucket-brigade grit (companding distortion)
    #   clock     — clock-drift warble depth (BBDs warble as the clock ages/drifts)
    # Vintage = the current shipped voicing (baseline). Clean lifts the top + tames
    # grit/warble; Degraded darkens, adds grit and a heavier clock warble.
    BBD_CHARS = {
        "Clean":    dict(bbd_recon_hz=6000.0, bbd_loop_hz=7500.0, bbd_drive=1.6, bbd_clock_depth=0.0003),
        "Vintage":  dict(bbd_recon_hz=4500.0, bbd_loop_hz=6000.0, bbd_drive=2.2, bbd_clock_depth=0.0006),
        "Degraded": dict(bbd_recon_hz=3200.0, bbd_loop_hz=4500.0, bbd_drive=3.0, bbd_clock_depth=0.0014),
    }
    print("\nBBD character presets (Clean / Vintage / Degraded):")
    for name, kw in BBD_CHARS.items():
        pl, pr = render("BBD", pluck2.copy(), pluck2.copy(),
                        T_ms=375.0, fb=0.55, mix=0.65, damp=0.3, **kw)
        path = os.path.join(OUT, f"BBD_{name}.wav")
        write_wav(path, pl, pr)
        print(f"  {name:9s}: recon={kw['bbd_recon_hz']:.0f}Hz loop={kw['bbd_loop_hz']:.0f}Hz "
              f"drive={kw['bbd_drive']:.1f} clock={kw['bbd_clock_depth']:.4f} -> {path}")

    # BBD grit A/B — the tanh companding only bites when the loop signal runs HOT,
    # so a quick pluck at moderate fb hides it. Drive the loop hard: a hotter,
    # longer note at fb=0.85 so the feedback accumulates into the nonlinear region.
    # Fix recon/loop/clock at Vintage; vary ONLY drive so the grit is isolated.
    durG = int(5.0*SR); tG = np.arange(durG)/SR
    phG = 110.0*tG
    gritSrc = 1.4 * 2.0*(phG - np.floor(phG + 0.5)) * np.exp(-tG/0.5)  # hot, 0.5s decay
    gritSrc[int(0.7*SR):] = 0.0     # ~0.7s note, then the loop sustains on its own
    print("\nBBD grit isolation (fb=0.85, hot source, recon/loop/clock=Vintage):")
    for drv in [1.6, 2.2, 3.0, 4.0]:
        bl, br = render("BBD", gritSrc.copy(), gritSrc.copy(),
                        T_ms=300.0, fb=0.85, mix=0.7, damp=0.3,
                        bbd_recon_hz=4500.0, bbd_loop_hz=6000.0,
                        bbd_drive=drv, bbd_clock_depth=0.0006)
        path = os.path.join(OUT, f"BBD_grit_drive{drv:.1f}.wav")
        write_wav(path, bl, br)
        print(f"  drive={drv:.1f} -> {path}")

    print(f"\nWAVs -> {OUT}")

if __name__ == "__main__":
    main()
