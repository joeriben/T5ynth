#!/usr/bin/env python3
"""
Delay-mode audition mock — exact algorithm prototype for the T5ynth delay
rework, rendered to WAV so we judge the *sound* before writing C++.

Modes (final candidate set, mediocre St/saturation-Tape dropped):
  Digital  : clean dual-mono, per-channel LP-damped feedback (today's baseline)
  PingPong : true ping-pong — input summed to mono into the LEFT line,
             cross-coupled feedback (L->R->L). Clean stereo bounce.
  Tape2    : 2-head tape echo, heads at T, 2T (RE-201 1:2 spacing), panned,
             wow+flutter + soft saturation + HF loss (Space-Echo character)
  Tape3    : 3-head tape echo, heads at T, 2T, 3T (RE-201 1:2:3), spread-panned

Run with the project venv:
  .venv/bin/python tools/delay_audition.py
Outputs WAVs + an impulse-response tap report to tools/delay_audition_out/.
"""
import math, os, struct, wave
import numpy as np

SR = 48000
OUT = os.path.join(os.path.dirname(__file__), "delay_audition_out")
os.makedirs(OUT, exist_ok=True)

# ---- voicing constants (mirror the planned C++) -----------------------------
WOW_HZ      = 0.7          # slow pitch drift
FLUT1_HZ    = 6.3          # flutter (two incommensurate sines = organic)
FLUT2_HZ    = 9.7
WOW_DEPTH   = 0.0030       # * delay-samples
FLUT_DEPTH  = 0.0015       # * delay-samples, per flutter sine
TAPE_DRIVE  = 1.6
TAPE_HP_HZ  = 100.0
PAN_WIDTH   = 0.75         # multi-head stereo spread (0..1)

def damp_fc(d):            # matches DelayLine::setDamp mapping
    return 20000.0 * (500.0/20000.0) ** d

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

# ---- per-mode render --------------------------------------------------------
def render(mode, dryL, dryR, T_ms=375.0, fb=0.45, mix=0.6, damp=0.45):
    n = len(dryL)
    T = T_ms * 0.001 * SR
    cap = int(math.ceil(3.2 * T)) + 8         # holds head 3 (3T) + slack
    bL = np.zeros(cap); bR = np.zeros(cap)
    wp = 0
    aLP = one_pole_a(damp_fc(damp)); lpL = lpR = 0.0
    aHP = one_pole_a(TAPE_HP_HZ);    hp_lp = 0.0
    outL = np.zeros(n); outR = np.zeros(n)
    wow = fl1 = fl2 = 0.0
    dwow = 2*math.pi*WOW_HZ/SR; df1 = 2*math.pi*FLUT1_HZ/SR; df2 = 2*math.pi*FLUT2_HZ/SR

    heads = {"Tape2": 2, "Tape3": 3}.get(mode, 1)
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

        else:  # Tape2 / Tape3  (mono tape, panned heads)
            mono = 0.5*(dryL[i] + dryR[i])
            wow += dwow; fl1 += df1; fl2 += df2
            wobble = 0.5 if mode == "Tape2" else 0.7  # Tape2 50% / Tape3 30% subtler
            modw = (math.sin(wow)*WOW_DEPTH + (math.sin(fl1)+math.sin(fl2))*FLUT_DEPTH) * T * wobble
            wl = wr = 0.0
            longest = 0.0
            for k in range(heads):
                tap = frac_read(bL, wp, (k+1)*T + modw)
                gL, gR = pans[k]
                wl += tap*gL; wr += tap*gR
                if k == heads-1: longest = tap
            # feedback from the long head: HP -> LP -> soft saturation
            f = longest*fb
            hp_lp += aHP*(f - hp_lp); f = f - hp_lp
            lpL += aLP*(f - lpL); f = lpL
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

    print("Impulse-response tap timing (T=375ms; expect Digital@375, "
          "PingPong L@375/R@750, Tape3@375/750/1125):")
    for mode in ["Digital", "PingPong", "Tape2", "Tape3"]:
        L,R = render(mode, imp, imp.copy(), mix=1.0, fb=0.5)
        tap_report(mode, L, R)
        write_wav(os.path.join(OUT, f"{mode}_impulse.wav"), L, R)
        pl, pr = render(mode, pluck, pluck.copy())
        write_wav(os.path.join(OUT, f"{mode}_pluck.wav"), pl, pr)
    print(f"\nWAVs -> {OUT}")

if __name__ == "__main__":
    main()
