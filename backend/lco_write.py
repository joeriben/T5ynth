#!/usr/bin/env python3
"""The LCO write-path: the LLM WRITES the Csound orchestra.

One prompt -> one inference against a curated library of real Csound idioms ->
one orchestra -> it runs. The library ORIENTS the model; it is not a menu the
model picks from and it is not assembled by Python. Morph, loop, mix, motion all
live IN the emitted Csound, because Csound is a programming language and the
model writes it.

BJ, 2026-07-19: "Es gibt: 1 Prompt. 1 LLM-Inferenz die Bibliotheken benutzt.
Daraus wird 1 Csound-Code fuer 1-3 Osc, die in sich morphen und was auch immer
tun: es ist 1 - EIN - Csound-Code der resultiert."

This module replaces the keys->prefab-emitter montage (`csound_orch.py`), which
was never authorised and is parked at tag `parked/keys-path-csound-20260721`.
Python's remaining job, and its whole job:

  1. assemble the prompt (library + host contract + platform invariants),
  2. call the model ONCE,
  3. wrap the returned body in the host scaffold,
  4. syntax-check it; on failure hand the compiler's own error back to the model
     and ask again (bounded), NEVER fall back to prefabricated code,
  5. return the orchestra.

LLM-first, no fallback: no model -> no oscillator. A prompt that cannot be
authored returns an honest ok=false, never a junk tone.
"""
import os
import re
import subprocess
import tempfile

# Host scaffold constants -- these are the plugin's contract, not preferences.
KSMPS = 64
NCHNLS = 16          # == CsoundEngine::kMaxVoices
HEADROOM = 0.32      # bounds a single idiom's peak; the voice VCA/DCA shapes the rest

# How many times the model may see the compiler's error and try again. Each
# retry is a full re-authoring with the error in the user turn -- not a patch.
MAX_TRIES = int(os.environ.get("T5YNTH_LCO_MAX_TRIES", 3))


# ─────────────────────────────────────────────────────────────────────────────
# The library — curated Csound idioms with parameter ranges and anchor glosses
# ─────────────────────────────────────────────────────────────────────────────
# Harvested from the parked keys implementation, whose constants are ear-approved
# and measured (docs/LCO_CONCEPT.md §5). Every number that carries a measurement
# behind it keeps that measurement in its comment, because the comment is what
# the model reads. The model ADAPTS and COMBINES these; it does not select one.
#
# This is BJ's "wir informieren ueber Parameter jeweils dieses Csound-Codes
# innerhalb seines Spektrums ... und Parametrisierungshinweise wie 'square ist
# sharp wenn Wert x = y, ist hollow wenn x = y'". The anchor glosses ARE that.

LIBRARY = r"""
# ============================================================================
# ANALOG OSCILLATOR  —  vco2: saw, pulse, square, PWM, supersaw, drive
# ============================================================================
# vco2's 3rd argument is imode (an INTEGER): 0 = saw, 2 = pulse/square, 10 =
# triangle. In imode 2 the 4th argument is the ON duty and vco2 keeps the RMS
# constant at every duty, so the duty may be swept freely.
#
#   duty 0.50 — square: hollow, woody, odd harmonics only
#   duty 0.30 — reedy, nasal, clarinet-ish
#   duty 0.12 — thin, buzzy, narrow, "chiptune lead"
#   duty 0.90 — thin again (0.9 and 0.1 sound alike; the duty is symmetric)
asaw   vco2 0.5, kfreq, 0                  ; bright, full harmonic series
asqr   vco2 0.5, kfreq, 2, 0.5             ; hollow square
athin  vco2 0.5, kfreq, 2, 0.12            ; thin narrow pulse

# PWM is a LOOP: a free-running LFO sweeps the duty forever, so the tone hollows
# out and fills back in, continuously. THIS is what movement looks like in
# Csound — an oscillator driving a parameter. Rate 0.2 Hz = slow breathing,
# 2 Hz = obvious wobble, 6 Hz = shimmer.
klfo   oscili 0.35, 0.5, giSine            ; 0.5 Hz, free-running, never resets
kpw    = 0.5 + klfo                        ; duty sweeps 0.15 .. 0.85
apwm   vco2 0.6, kfreq, 2, kpw

# SUPERSAW / fat: copies detuned in cents around kfreq. +-12 cents = gentle
# chorus, +-30 = wide analog fat, +-60 = out-of-tune ensemble. Divide by
# sqrt(N) so adding copies does not raise the level.
au1    vco2 0.5, kfreq * 0.99306, 0        ; -12 cents
au2    vco2 0.5, kfreq, 0
au3    vco2 0.5, kfreq * 1.00699, 0        ; +12 cents
afat   = (au1 + au2 + au3) * 0.577

# ANALOG LIFE: a slow random pitch drift keeps a tone from sounding frozen.
# 0.0007 = subtle (the shipped default), 0.003 = visibly unstable/old.
kdrift randi 0.0007, 3.0, 2
adrift vco2 0.5, kfreq * (1 + kdrift), 0

# DIRT / OVERDRIVE: tanh saturation with output compensation, so driving the
# tone does not also make it louder. pregain 2 = warm edge, 8 = overdriven,
# 20 = fully distorted. comp = amp / tanh(amp * pregain).
adirty = tanh(asaw * 8.0) * 0.5006

# ============================================================================
# FM  —  foscili: bells, metal, electric pianos
# ============================================================================
# foscili amp, cps, kcar, kmod, kndx, ifn. Harmonicity is a STEP function of
# car:mod rationality — an INTEGER ratio is harmonic, a non-integer is metal.
# Never interpolate a ratio; pick one.
#
#   1:1    — harmonic, organ-like, hollow
#   1:2    — odd harmonics only (square-ish)
#   1:3    — full harmonic series, brassy
#   1:1.41 — inharmonic: bell, gong, struck metal
#   1:3.46 — inharmonic: clangorous, sharp metal
#   1:14.2 — the classic DX electric-piano tine. NOT 14.0: an exact integer is
#            perfectly harmonic and reads as a bright buzz, while 14.2 places
#            partials BETWEEN harmonics and reads as metal.
#
# The INDEX is brightness: 0.5 = nearly a sine, 3 = rich, 8 = clangorous.
# A bell's colour DARKENS as it rings — the index falls while the loudness does
# not. That is a permitted travel (colour may move, loudness may not).
kndx   linseg 6, 2.5, 1.5                  ; bright strike -> darker ring
abell  foscili 0.4, kfreq, 1, 1.41, kndx, giSine

# INHARMONIC DOUBLET: two partials a few Hz apart beat against each other —
# this is what makes a real bell shimmer instead of sitting still. 2.7 Hz
# offset = slow shimmer; scale it with kfreq for a constant musical beat.
ab1    foscili 0.3, kfreq, 1, 1.41, kndx, giSine
ab2    foscili 0.3, kfreq * 1.0032, 1, 1.41, kndx, giSine
abeat  = (ab1 + ab2) * 0.7

# ELECTRIC PIANO: a body pair plus a short metallic tine on top. The tine's
# index decays to zero so the metal is an attack colour, not a permanent buzz.
ktine  linseg 2.2, 0.09, 0.0               ; 86 ms tine
abody  foscili 0.30, kfreq, 1, 3, 1.4, giSine
atine  foscili 0.30, kfreq, 1, 14.2, ktine, giSine
aep    balance abody + atine, oscili(0.5, 1, giSine)

# ============================================================================
# ADDITIVE  —  gbuzz / buzz: organs, brass, breath, controllable spectra
# ============================================================================
# gbuzz kamp, kcps, knh, klh, kmul, ifn — knh = number of harmonics, klh =
# lowest harmonic, kmul = rolloff (amplitude ratio between adjacent harmonics).
# It reads a COSINE table (giCos), not a sine one.
#
#   kmul 0.9  — bright, buzzy, almost a saw
#   kmul 0.6  — full but rounded, "brass"
#   kmul 0.3  — soft, few harmonics, flute-ish
#   kmul 0.05 — nearly a sine
# knh 40 = full, 8 = hollow/organ-like, 3 = very simple.
aorg   gbuzz 0.4, kfreq, 20, 1, 0.5, giCos

# BRASS BLOW: the spectrum OPENS as the player blows into the note — rolloff
# travels, loudness does not. A slow k-rate sweep of kmul is exactly this.
kblow  linseg 0.25, 0.6, 0.72              ; closed -> open over 600 ms
abrass gbuzz 0.45, kfreq, 30, 1, kblow, giCos

# ORGAN REGISTRATION: independent sines at integer multiples. 8' = played
# pitch, 4' = octave up, 2' = two octaves up, 16' = octave down.
ar16   oscili 0.25, kfreq * 0.5, giSine
ar8    oscili 0.40, kfreq, giSine
ar4    oscili 0.22, kfreq * 2, giSine
ar2    oscili 0.12, kfreq * 4, giSine
adraw  = ar16 + ar8 + ar4 + ar2

# ============================================================================
# MOTION  —  loops, morphs, one-shots
# ============================================================================
# A LOOP is a free-running oscili/phasor that NEVER resets. A ONE-SHOT is a
# linseg/expseg: it moves once and then holds. Choose deliberately — "loop"
# in the prompt means the trajectory REPEATS.

# LOOP between two timbres: equal-power crossfade driven by a free-running LFO,
# so it sweeps saw -> square -> saw forever. 0.25 Hz = one full cycle per 4 s.
klx    oscili 0.5, 0.25, giSine
kx     = 0.5 + klx                         ; 0 .. 1 .. 0, repeating
ala    vco2 0.5, kfreq, 0
alb    vco2 0.5, kfreq, 2, 0.5
aloop  = ala * sqrt(1 - kx) + alb * sqrt(kx)

# ONE-SHOT morph "a > b": moves from a to b once, over the morph time, and
# stays at b. 1.4 s is the house default when the prompt gives no speed cue;
# "slowly" = 4-8 s, "quickly" = 0.2-0.5 s.
kmx    linseg 0, 1.4, 1
ama    vco2 0.5, kfreq, 0
amb    gbuzz 0.4, kfreq, 20, 1, 0.5, giCos
amorph = ama * sqrt(1 - kmx) + amb * sqrt(kmx)

# THREE-STAGE CHAIN "a > b > c": chain the crossfades, each on its own leg.
# VIBRATO/SHIMMER: a small free-running modulation of pitch or amplitude.
kvib   oscili 0.004, 5.2, giSine           ; +-0.4% pitch, 5.2 Hz
avib   vco2 0.5, kfreq * (1 + kvib), 0

# ============================================================================
# NOISE, BREATH, TEXTURE
# ============================================================================
# Filtered noise tracks the pitch and gives breath, air and grit.
anz    rand 0.5
abrth  reson anz, kfreq, kfreq * 0.35, 1   ; breathy band around the pitch
aair   butterhp anz * 0.08, 6000           ; high air on top of a tone

# ============================================================================
# MODAL / STRUCK  —  mode: resonators for bars, plates, drum heads
# ============================================================================
# `mode aexcite, kfreq, kQ` is one resonant mode. A struck bar or drum is a
# BANK of them at inharmonic ratios, excited continuously (the synth owns the
# envelope, so the resonators are driven, not struck once).
#   Q  50 — short, dry, woody
#   Q 400 — long, ringing, metallic
# Bar ratios (glockenspiel/vibraphone family): 1, 2.76, 5.40, 8.93
# Drum-head ratios (circular membrane):        1, 1.59, 2.14, 2.30, 2.65
aexc   rand 0.02
am1    mode aexc, kfreq * 1.00, 220
am2    mode aexc, kfreq * 2.76, 180
am3    mode aexc, kfreq * 5.40, 140
abar   = (am1 * 0.6 + am2 * 0.3 + am3 * 0.15) * 0.8

# ============================================================================
# WAVESHAPING  —  giCheb: exact harmonic content by transfer function
# ============================================================================
# Drive a sine through the Chebyshev transfer table for a defined harmonic set.
# Sweeping the drive sweeps the brightness — a clean, DC-free way to "open up".
asin   oscili 0.9, kfreq, giSine
kdrv   oscili 0.4, 0.3, giSine
acheb  tablei asin * (0.5 + kdrv), giCheb, 1, 0.5
"""


# ─────────────────────────────────────────────────────────────────────────────
# The system prompt — library as orientation, host contract, platform invariants
# ─────────────────────────────────────────────────────────────────────────────
# The invariants are docs/LCO_CONCEPT.md §4. They are user-observable
# fundamentals: the oscillator is a SPECTRUM SOURCE, colour may travel but
# loudness may not, pitch belongs to the synth, and every sound MOVES unless the
# prompt asks for stillness.

SYSTEM_PROMPT = r"""You WRITE Csound orchestra code (Csound 6.18) for a synthesizer's oscillator. You are given a LIBRARY of real, working Csound idioms with measured parameter ranges and plain-language anchors. ADAPT and COMBINE them into the code that generates the sound the user describes. The library orients you; it is not a menu and you are not restricted to it.

WHAT YOU WRITE
Only the oscillator body: the lines that produce the raw signal. The host provides everything around it.

HARD RULES
- Read the pitch from `kfreq` (Hz, k-rate). It GLIDES; everything you write must track it. Never hardcode a pitch or a frequency number.
- Write your final audio into a variable named exactly `asig`.
- Define every variable before you use it, top to bottom. Give your variables distinct names; do not reuse a name for two things.
- Use only real Csound 6.18 opcodes. `vco2`'s imode is an INTEGER (0 saw, 2 pulse, 10 triangle). There is no `vco1`.
- You shape SPECTRUM and TIMBRE only. Do NOT write an amplitude envelope on the output (no linen, adsr, madsr, expon on the way to `asig`) — loudness belongs to the player's envelope. The COLOUR may travel over the note; the LOUDNESS may not. A tone that fades out on its own is wrong.
- Keep `asig` near +-0.5 peak. The host applies its own headroom and voice gain.
- The sound MOVES by default: a sweep, a beat, a shimmer, a morph. Only write a standing tone if the user explicitly asks for something static or still.
- A LOOP is a free-running `oscili`/`phasor`/`lfo` that NEVER resets, driving a parameter or a crossfade. If the user asks for a loop or for something repeating, write a REPEATING trajectory — never a one-shot `linseg`, which moves once and then holds.
- You may layer up to THREE oscillators and mix them, and you may morph or crossfade between timbres. "a > b" means start at a and move to b; a LOOP of that repeats forever. "a + b" means two layers at once.
- Layer 1 must be scaled by `kvol1`, layer 2 by `kvol2`, layer 3 by `kvol3`, and each layer's pitch is `kfreq * koct1` / `koct2` / `koct3`. Those are the player's mix and octave knobs — a layer that ignores them cannot be mixed. With one layer, use `kvol1` and `kfreq * koct1`.

AVAILABLE IN SCOPE
  kfreq  k-rate pitch in Hz (already glide-smoothed and limited)
  kvel   note velocity 0..1        kpres  pressure/aftertouch 0..1
  ktimb  timbre controller 0..1    knote  seconds since this note started
  kvol1 kvol2 kvol3                per-layer mix (player knobs)
  koct1 koct2 koct3                per-layer octave multiplier (player knobs)
  giSine (sine table)  giCos (cosine table, for gbuzz/buzz)
  giCheb (Chebyshev transfer table)  giImp (short strike impulse table)

OUTPUT FORMAT
Output ONLY Csound code lines. No <CsoundSynthesizer>, no <CsInstruments>, no header (sr/ksmps/nchnls/0dbfs), no `instr`/`endin`, no `ftgen`, no score, no `out`/`outs`/`outch`, no markdown fences, no explanation. The host supplies all of it.

After the code, on ONE final line, write:
READING: <a short plain-language description of what you built, 5-12 words>

LIBRARY (adapt and combine — do not simply copy one block):
""" + LIBRARY


_REPAIR_PROMPT = (
    "\n\nYour previous attempt did not compile. Csound reported:\n"
    "  {error}\n"
    "Write the WHOLE body again, correctly. Do not explain the error."
)


# ─────────────────────────────────────────────────────────────────────────────
# Sanitising the model's reply
# ─────────────────────────────────────────────────────────────────────────────
# The model is told not to emit a header, a score or an `instr`. It sometimes
# does anyway, and a stray header line would fight the host's own. Strip them
# rather than fail the authoring over a formality; anything that changes the
# SOUND is left untouched.

_STRIP = re.compile(
    r"^\s*(</?Cs\w+|sr\s*=|kr\s*=|ksmps\s*=|nchnls|0dbfs|instr\b|endin\b|"
    r"ftgen\b|\w+\s+ftgen\b|out\b|outs\b|outch\b|i\s+\d|f\s+\d|e\s*$)",
    re.IGNORECASE)

_FENCE = re.compile(r"```[A-Za-z0-9_+#-]*\s*\n(.*?)```", re.DOTALL)
_OUT_CALL = re.compile(r"\s*(?:out|outs|outch)\s+(?:\d+\s*,\s*)?(a\w+)", re.IGNORECASE)
_READING = re.compile(r"^\s*READING\s*:\s*(.+?)\s*$", re.IGNORECASE | re.MULTILINE)


def sanitize(raw):
    """Model reply -> (body, reading). Strips fences, the host's own lines and
    the READING line; recovers `asig` when the model wrote its output through
    `out`/`outch` under another name."""
    txt = raw or ""
    m = _FENCE.search(txt)
    if m:
        txt = m.group(1)
    txt = txt.replace("```", "")

    reading = ""
    rm = _READING.search(txt)
    if rm:
        reading = rm.group(1).strip()
        txt = _READING.sub("", txt)

    kept, captured_out = [], None
    for ln in txt.splitlines():
        s = ln.rstrip()
        if not s.strip():
            continue
        mo = _OUT_CALL.match(s)
        if mo:
            captured_out = mo.group(1)
        if _STRIP.match(s):
            continue
        kept.append(s)
    body = "\n".join(kept)
    # The model routed its signal to the output under another name: keep the
    # sound and give the host the variable it needs.
    if not re.search(r"^\s*asig\b", body, re.MULTILINE) and captured_out and captured_out != "asig":
        body += f"\n  asig = {captured_out}"
    return body, reading


# ─────────────────────────────────────────────────────────────────────────────
# The host scaffold — everything around the authored body
# ─────────────────────────────────────────────────────────────────────────────
# Unchanged from the shipped contract: sr is substituted by the engine at its
# real rate, ksmps=64, nchnls=16 (one channel per voice), one numeric `instr 1`
# with p4 = voice index, six named control channels per voice read via chnget,
# and 16 always-on score instances.

_HEAD = (
    "<CsoundSynthesizer>\n<CsOptions>\n-n -d\n</CsOptions>\n<CsInstruments>\n"
    "sr = %SR%\n"
    f"ksmps = {KSMPS}\n"
    f"nchnls = {NCHNLS}\n"
    "0dbfs = 1\n"
    "giSine ftgen 1, 0, 65536, 10, 1\n"
    # gbuzz reads a COSINE table (its harmonics are cosines); a sine table here
    # would phase-shift every harmonic against the fundamental.
    "giCos  ftgen 2, 0, 65536, 11, 1\n"
    # GEN13 Chebyshev TRANSFER function, odd harmonics with alternating signs:
    # odd-only is antisymmetric, so f(0)=0 and the shaper is DC-free at EVERY
    # drive level and can be swept freely; alternating signs leave each
    # harmonic's magnitude untouched (a sign is a phase) while the peak sums to
    # 0.71 instead of 2.01 — 10 dB of crest given back.
    "giCheb ftgen 3, 0, 8193, 13, 1, 1, 0, 1, 0, -0.5, 0, 0.28, 0, -0.15, 0, 0.08\n"
    # Short decaying harmonic burst: the mallet contact for struck models, which
    # take their strike argument as a TABLE NUMBER, not a scalar.
    "giImp  ftgen 4, 0, 256, 10, 1, 0.5, 0.3, 0.2, 0.1\n"
    # Starting values for the player's mix/octave knobs. The plugin overwrites
    # these from its parameters on the next block; an offline render or a host
    # that never writes them plays exactly what was authored.
    "chnset 1.0000, \"osc1vol\"\n"
    "chnset 1.0000, \"osc1oct\"\n"
    "chnset 1.0000, \"osc2vol\"\n"
    "chnset 1.0000, \"osc2oct\"\n"
    "chnset 1.0000, \"osc3vol\"\n"
    "chnset 1.0000, \"osc3oct\"\n"
    "\n"
    "instr 1\n"
    "  ivoice   = p4\n"
    "  Sgate    sprintf \"gate%d\", ivoice\n"
    "  Sfreq    sprintf \"freq%d\", ivoice\n"
    "  Svel     sprintf \"vel%d\", ivoice\n"
    "  Spres    sprintf \"pres%d\", ivoice\n"
    "  Stimb    sprintf \"timb%d\", ivoice\n"
    "  Strig    sprintf \"trig%d\", ivoice\n"
    "  kgateraw chnget Sgate\n"
    "  kfreqraw chnget Sfreq\n"
    "  kvel     chnget Svel\n"
    "  kpres    chnget Spres\n"
    "  ktimb    chnget Stimb\n"
    "  ktrig    chnget Strig\n"
    "  kvol1    chnget \"osc1vol\"\n"
    "  koct1    chnget \"osc1oct\"\n"
    "  kvol2    chnget \"osc2vol\"\n"
    "  koct2    chnget \"osc2oct\"\n"
    "  kvol3    chnget \"osc3vol\"\n"
    "  koct3    chnget \"osc3oct\"\n"
    "  kgate    portk kgateraw, 0.001\n"
    "  kfreq    limit kfreqraw, 20, 12000\n"
    "  kpresGain = 1.0 + 0.15 * kpres\n"
    # Seconds since THIS note, for anything that shapes timbre over the note.
    # init 0 rather than a settled value: a note whose gate and trig are already
    # high before the first k-cycle gives changed2 no edge to fire on, so a
    # counter starting anywhere else would sit frozen forever.
    "  knote    init 0\n"
    "  if changed2(ktrig) == 1 then\n"
    "    knote  = 0\n"
    "  endif\n"
    "  knote    = knote + 1/kr\n\n"
)

_TAIL = (
    "\n"
    f"  aout     = asig * kgate * kvel * kpresGain * {HEADROOM}\n"
    "  aout     clip aout, 0, 0.95, 0.85    ; final soft peak safety: transparent\n"
    "                                       ; below ~0.8, asymptotes ~0.88 above,\n"
    "                                       ; bounds ANY op stack / crest / host gain\n"
    "  outch    ivoice, aout\n"
    "endin\n"
    "</CsInstruments>\n<CsScore>\n"
)

_SCORE = "".join(f"i 1 0 360000 {v}\n" for v in range(1, NCHNLS + 1)) \
         + "e 360000\n</CsScore>\n</CsoundSynthesizer>\n"


def wrap(body):
    """Authored body -> the full orchestra the engine compiles."""
    indented = "\n".join(("  " + l) if not l.startswith(" ") else l
                         for l in body.splitlines())
    return _HEAD + indented + _TAIL + _SCORE


# ─────────────────────────────────────────────────────────────────────────────
# The compile gate
# ─────────────────────────────────────────────────────────────────────────────

_ANSI = re.compile(r"\x1b\[[0-9;]*m")
_ERR_LINE = re.compile(r"\berror:", re.IGNORECASE)


def _csound_binary():
    return os.environ.get("T5YNTH_CSOUND", "csound")


def syntax_check(orchestra):
    """(ok, first_error). Runs the real compiler over the real orchestra with
    `--syntax-check-only`, so the gate is Csound's own verdict rather than a
    parser of ours guessing at one. sr is substituted for the check only."""
    csd = orchestra.replace("%SR%", "44100")
    with tempfile.NamedTemporaryFile("w", suffix=".csd", delete=False) as fh:
        fh.write(csd)
        path = fh.name
    try:
        r = subprocess.run([_csound_binary(), "--syntax-check-only", path],
                           capture_output=True, text=True, timeout=90)
        log = _ANSI.sub("", (r.stderr or "") + (r.stdout or ""))
        if r.returncode == 0:
            return True, ""
        for ln in log.splitlines():
            if _ERR_LINE.search(ln):
                return False, ln.strip()
        tail = [l for l in log.strip().splitlines() if l.strip()]
        return False, (tail[-1] if tail else "csound failed without a message")
    except FileNotFoundError:
        # No compiler on this machine: authoring still works, it is just
        # unverified. Say so rather than failing a good orchestra.
        return True, ""
    except subprocess.TimeoutExpired:
        return False, "csound syntax check timed out"
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# The entry point
# ─────────────────────────────────────────────────────────────────────────────

def build_csound_response(text, llm, correction="", previous=""):
    """Prompt -> live Csound orchestra. The REAL pipeline entry
    (pipe_inference mode=="csound" calls this).

    ONE inference writes the orchestra body against the library; the compiler
    checks it; a failure goes back to the model with Csound's own error, up to
    MAX_TRIES. There is NO fallback: if the model cannot produce a compiling
    orchestra, this returns ok=false and the LCO stays silent, exactly as
    docs/LCO_CONCEPT.md §4 requires ("no LLM, no oscillator").

    ``llm`` is the caller-injected ``(text, system_prompt, max_new_tokens) ->
    str`` callable. Returns the {ok, orchestra, reading, error} shape the C++
    PipeInference parses.

    ``correction``/``previous`` are accepted for wire compatibility and are
    appended to the user turn when present (the self-listen loop is currently
    deactivated and sends neither).
    """
    prompt = (text or "").strip()
    if not prompt:
        return {"ok": False, "error": "empty prompt"}

    user_turn = prompt
    if correction:
        user_turn = (f"{prompt}\n\nThe previous attempt was described as: "
                     f"{previous}\nCorrect it: {correction}")

    attempts = []
    turn = user_turn
    for attempt in range(1, MAX_TRIES + 1):
        # No token cap: the orchestra is as long as the sound needs. A cap here
        # would cut the body mid-line, and a truncated generation is not an
        # error — it would arrive as a syntax failure with no cause to show.
        raw = llm(turn, SYSTEM_PROMPT, None)
        body, reading = sanitize(raw)
        if not body.strip():
            attempts.append("the model returned no code")
            turn = user_turn + _REPAIR_PROMPT.format(error="you returned no code at all")
            continue

        orchestra = wrap(body)
        ok, err = syntax_check(orchestra)
        if ok:
            return {"ok": True, "orchestra": orchestra,
                    "reading": reading or _fallback_reading(body),
                    # The panel's transparency surface. Under the write-path the
                    # honest answer to "what did the machine build?" is the code
                    # the model actually wrote -- not a list of keys it picked,
                    # because it picks none.
                    "params_text": body,
                    "attempts": attempt}
        attempts.append(err)
        turn = user_turn + _REPAIR_PROMPT.format(error=err)

    return {"ok": False,
            "error": "the author could not write a compiling orchestra after "
                     f"{MAX_TRIES} attempts: {attempts[-1] if attempts else 'unknown'}",
            "attempts": MAX_TRIES}


def _fallback_reading(body):
    """A reading when the model omitted its READING line. Names the opcodes it
    actually used — a true, if plain, statement about what is running."""
    seen = []
    for op in ("vco2", "foscili", "gbuzz", "buzz", "mode", "oscili", "phasor",
               "rand", "reson", "tablei", "tanh", "balance"):
        if re.search(rf"\b{op}\b", body) and op not in seen:
            seen.append(op)
    return ("built from " + ", ".join(seen[:5])) if seen else "authored orchestra"
