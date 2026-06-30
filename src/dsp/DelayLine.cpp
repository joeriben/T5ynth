#include "DelayLine.h"

namespace
{
    // Tape character (Tape mode) — all fixed/auto, no user parameter.
    // ONE capstan/transport speed wobble m(t) ~ 0, applied MULTIPLICATIVELY to
    // every head (delay_k = (k+1)*base*(1+m)): the far head reads tape recorded
    // (k+1)*D1 ago, so over its longer record->play gap the speed drifted further
    // from "now" — BOTH the delay excursion AND the pitch wobble scale 1:2:3 with
    // transit length (pitch dev = d/dt[delay] = (k+1)*base*m'; timing error
    // v(now)-v(now-D_k) likewise grows with D_k for slow drift). Heads are NOT
    // pitch-locked. Exact for the dominant slow wow; fast flutter is slightly
    // over-scaled on the far heads but negligible at these depths. Wow dominates
    // (capstan, <2 Hz); flutter is faster but far shallower (a high freq amplifies
    // the pitch deviation ∝ f, so even small flutter buzzes if hot).
    constexpr float  kWow1Hz     = 0.6f;      // primary wow (capstan rotation)
    constexpr float  kWow2Hz     = 1.3f;      // secondary wow, incommensurate -> organic
    constexpr float  kFlutHz     = 6.0f;      // flutter shimmer (kept very shallow)
    constexpr float  kWow1Depth  = 0.0018f;   // fractional speed deviation
    constexpr float  kWow2Depth  = 0.0009f;
    constexpr float  kFlutDepth  = 0.00004f;
    constexpr float  kTapeDrive  = 1.6f;     // soft-sat pre-gain (auto-limiting)
    constexpr float  kTapeHpHz   = 100.0f;   // high-pass in the feedback loop
    constexpr float  kPanWidth   = 0.75f;    // multi-head stereo spread (0..1)
    constexpr float  kTapeDampTopHz = 9000.0f; // Damp=0 feedback-LP ceiling: an
                                               // intrinsic baseline HF loss that
                                               // compounds per pass (vs Digital's
                                               // fully-open 20 kHz). Auditioned.
    constexpr float  kTapePbFloorHz = 750.0f;  // tape PLAYBACK rolloff floor (Damp=1):
                                               // higher than the feedback's 500 Hz so
                                               // cranking Damp doesn't double-crush the
                                               // top (playback + feedback both rolling
                                               // to 500 was too dark). Auditioned.

    // BBD (bucket-brigade) character — fixed/auto, no extra user parameter.
    constexpr float  kBbdReconTopHz = 4500.0f; // OUTPUT recon-LP top at Damp=0: dark,
                                               // steep 3-pole reconstruction (the BBD
                                               // voice). 3000 was too dark at Damp 0.
    constexpr float  kBbdLoopHz     = 6000.0f; // FEEDBACK loop LP (fixed, open-ish):
                                               // a SEPARATE gentle 1-pole, NOT the dark
                                               // recon — keeps loop gain so the echo
                                               // rings/blooms toward self-oscillation.
    constexpr float  kBbdHpHz       = 180.0f;  // mid-focus high-pass — OUTPUT ONLY. In
                                               // the short feedback loop a 180Hz HP
                                               // compounds (~13 passes) and strips the
                                               // fundamental, killing sustain.
    constexpr float  kBbdDrive      = 2.2f;    // gentle bucket-brigade grit
    constexpr float  kBbdClockHz    = 0.6f;    // subtle clock-drift rate
    constexpr float  kBbdClockDepth = 0.0006f; // clock-drift depth (cleaner than tape)

    // ── Tape character presets ──────────────────────────────────────────────────
    // Both flutter LFOs (6.0 Hz = kFlutHz, 9.7 Hz = kFlut2Hz) are non-zero in
    // every preset — like a real machine where capstan flutter and tape-guide
    // friction both vibrate at all times. Natural < Warm < Wild.
    struct TapeChar { float wow1D, wow2D, flut1D, flut2D; };
    static constexpr float kFlut2Hz = 9.7f;
    static constexpr TapeChar kTapeChars[3] = {
        { 0.0006f, 0.0003f, 0.00004f,  0.00003f  },  // Natural: wow /3 — 6988cdf1's per-head
        { 0.0016f, 0.0008f, 0.00005f,  0.00005f  },  // Warm: warmth via WOW, flutter near-Natural
        { 0.002f,  0.001f,  0.0002f,   0.00015f  },  // Wild: heavy + 6.0 + 9.7 Hz flutter
    };
    // Depths tuned by ear in real play. Natural's wow was /3'd because 6988cdf1
    // (06-24) made wow scale MULTIPLICATIVELY per head (1:2:3): the long head
    // then leiert at ~0.0054 vs the old additive ~0.0015 flat — audibly "broken
    // tape". /3 brings the long head back to ~0.0018 ≈ the old clean level.
    // Warm -20% / Wild -50% from the bench set (Wild still above Warm + Natural
    // on every component). Hz unchanged (kWow*/kFlut*/kFlut2Hz).

    // ── Tape Old (character 3) — the pre-6988cdf1 ADDITIVE wobble ────────────────
    // Restored on request. Physically wrong on purpose: one modulation offset is
    // ADDED to every head identically (the near head wobbles as much as the far
    // one), and it scales with the delay-sample count rather than being a fractional
    // speed deviation. Exact constants from 6988cdf1^ (the old kTape voicing, which
    // applied a 0.7*0.7 = 0.49 wobble scale). Uses its own LFO set: wow 0.7 Hz,
    // flutter 6.3 + 9.7 Hz, summed for an organic non-repeating drift.
    constexpr float  kOldWowHz    = 0.7f;
    constexpr float  kOldFlut1Hz  = 6.3f;
    constexpr float  kOldWowDepth  = 0.0030f;  // * delay-samples
    constexpr float  kOldFlutDepth = 0.0015f;  // * delay-samples, per flutter sine
    constexpr float  kOldWobbleScale = 0.49f;  // old kTape (Tape 3) restraint

    // ── BBD character presets ───────────────────────────────────────────────────
    // Vintage = current shipped voicing (index 0). Clean lifts the recon top and
    // tames grit/warble (DM-2 style). Degraded darkens + adds grit and warble.
    struct BbdChar { float reconHz, loopHz, drive, clockDepth; };
    static constexpr BbdChar kBbdChars[3] = {
        { 4500.0f, 6000.0f, 2.2f, 0.0006f },  // Vintage (current kBbdReconTopHz etc.)
        { 6000.0f, 7500.0f, 1.6f, 0.0003f },  // Clean
        { 3200.0f, 4500.0f, 3.0f, 0.0014f },  // Degraded
    };

    // Buffer capacity headroom: holds the longest single tap (the 5 s processor
    // clamp) and the tape 3rd head; the tape base is capped to keep 3·base inside.
    constexpr double kBufferSeconds = 6.2;

    inline float onePoleCoeff(double fc, double sr)
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                               * static_cast<float>(fc / sr));
    }
}

void T5ynthDelayLine::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    delayLine.prepare(spec);

    // SR-aware capacity. The old fixed 96000 samples (= 2 s @48k) overflowed at
    // high sample rates and under modulation toward the processor's 5 s clamp.
    // setMaximumDelayInSamples reallocates using the channel count from prepare().
    const int capacity = static_cast<int>(std::ceil(kBufferSeconds * sampleRate))
                       + samplesPerBlock + 4;
    delayLine.setMaximumDelayInSamples(capacity);
    maxDelaySamples = static_cast<float>(capacity - samplesPerBlock - 2);

    currentDelaySamples = juce::jlimit(1.0f, maxDelaySamples,
                                       static_cast<float>(delayTimeMs * 0.001 * sr));
    targetDelaySamples = currentDelaySamples;
    delayLine.setDelay(currentDelaySamples);
    targetFeedback = feedback;

    updateDampCoeffs();
    updateTapePbCoeffs();
    // Settle each filter's internal order off-thread to match the assigned
    // 2nd-order coefficients. Assigning .coefficients is only a pointer swap and
    // does NOT update Filter::order, so without this the first audio-thread
    // processSample() would trip Filter::check() → reset() → malloc (the #1
    // BLOCKING bug class). prepareToPlay calls prepare() but never reset().
    dampFilterL.reset();
    dampFilterR.reset();
    tapePbFilterL.reset();
    tapePbFilterR.reset();

    tapeHpState = 0.0f;
    wow1Phase = wow2Phase = flutPhase = flut2Phase = 0.0f;

    // BBD: the recon cascade tracks Damp (rebuilt in recomputeDamp); seed its
    // coeff at the current baseline plus the fixed mid-focus HP and the fixed
    // feedback loop LP here. One-pole coeffs only — no allocation, audio-safe.
    bbdReconCoeff = onePoleCoeff(bbdReconFreq, sr);
    bbdHpCoeff    = onePoleCoeff(kBbdHpHz, sr);
    bbdLoopCoeff  = onePoleCoeff(kBbdChars[juce::jmin(delayCharacter, 2)].loopHz, sr);
    bbdRecon1 = bbdRecon2 = bbdRecon3 = 0.0f;
    bbdHpState = 0.0f;
    bbdLoopState = 0.0f;
    bbdClockPhase = 0.0f;

    prepared = true;
}

void T5ynthDelayLine::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() < 1)
        return;

    // Silence detection: skip only after the output has TRULY decayed.
    // A delay's output is legitimately silent for up to one whole delay period
    // BEFORE the first echo returns — and again in the gaps between sparse taps
    // at low feedback. Gating on a fixed ~4-block window (~46 ms) killed any echo
    // whose delay time exceeded that: a single short hit died before its first
    // repeat ever came back, while a held note (input stays non-silent past the
    // first echo) sounded fine. Require the output to stay silent for at least
    // one full delay period + the confirm margin before concluding the tail is
    // done. (The host-side idle gate still bounds the absolute tail length.)
    const float inMag = buffer.getMagnitude(0, numSamples);
    const bool inputSilent = inMag < 1e-6f;
    const int delayPeriodBlocks = (numSamples > 0)
        ? static_cast<int>(std::ceil(targetDelaySamples / static_cast<float>(numSamples)))
        : 0;
    if (inputSilent && silentOutputBlocks > delayPeriodBlocks + SILENCE_CONFIRM_BLOCKS)
        return;

    const bool stereo = buffer.getNumChannels() >= 2;
    const float dryGain = 1.0f - wetMix;
    const float smoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sr * 0.005));

    const int  heads = (mode == kTape) ? 3 : 1;
    const bool isTape = (mode == kTape);
    const bool isBbd  = (mode == kBbd);
    const bool isPingPong = (mode == kPingPong) && stereo;

    // Constant-power pan gains for tape heads, spread evenly across [-w, +w].
    // On a mono buffer the defaults (panL=1, panR=0) leave the head sum flat so
    // mono fold-down keeps full level — the spread only applies in true stereo.
    float panL[3] = { 1.0f, 1.0f, 1.0f };
    float panR[3] = { 0.0f, 0.0f, 0.0f };
    if (isTape && stereo)
    {
        for (int k = 0; k < heads; ++k)
        {
            const float p = (heads == 1) ? 0.0f
                          : -kPanWidth + (2.0f * kPanWidth) * static_cast<float>(k)
                            / static_cast<float>(heads - 1);
            const float t = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL[k] = std::cos(t);
            panR[k] = std::sin(t);
        }
    }

    const float twoPi = juce::MathConstants<float>::twoPi;
    // Character 3 = Tape Old (additive wobble). It reuses three of the phase
    // accumulators at the OLD rates (wow 0.7, flutter 6.3 + 9.7 Hz); the modern
    // presets use four LFOs (wow 0.6/1.3, flutter 6.0 + 9.7). tc is unused when
    // tapeOld, but the reference is taken with a clamped index to stay in bounds.
    const bool tapeOld = (delayCharacter == 3);
    const TapeChar& tc = kTapeChars[juce::jlimit(0, 2, delayCharacter)];
    const float dWow1  = twoPi * (tapeOld ? kOldWowHz   : kWow1Hz) / static_cast<float>(sr);
    const float dWow2  = twoPi * kWow2Hz  / static_cast<float>(sr);
    const float dFlut  = twoPi * (tapeOld ? kOldFlut1Hz : kFlutHz) / static_cast<float>(sr);
    const float dFlut2 = twoPi * kFlut2Hz / static_cast<float>(sr);
    const float aHP   = onePoleCoeff(kTapeHpHz, sr);
    const float dBbdClock = twoPi * kBbdClockHz / static_cast<float>(sr);

    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Smooth delay time and feedback per-sample to avoid clicks.
        currentDelaySamples += (targetDelaySamples - currentDelaySamples) * smoothCoeff;
        feedback += (targetFeedback - feedback) * smoothCoeff;

        const float dryL = dataL[i];
        const float dryR = stereo ? dataR[i] : 0.0f;
        const float clampedT = juce::jlimit(1.0f, maxDelaySamples, currentDelaySamples);

        if (isTape)
        {
            const float mono = stereo ? 0.5f * (dryL + dryR) : dryL;

            wow1Phase += dWow1; wow2Phase += dWow2; flutPhase += dFlut; flut2Phase += dFlut2;
            if (wow1Phase  > twoPi) wow1Phase  -= twoPi;
            if (wow2Phase  > twoPi) wow2Phase  -= twoPi;
            if (flutPhase  > twoPi) flutPhase  -= twoPi;
            if (flut2Phase > twoPi) flut2Phase -= twoPi;
            // Modern presets (0..2): ONE shared transport-speed wobble, applied
            // MULTIPLICATIVELY per head (below) so delay excursion AND pitch both
            // scale 1:2:3 with distance. Two flutter LFOs (6.0 + 9.7 Hz): capstan
            // flutter + tape-guide friction.
            const float m = std::sin(wow1Phase)  * tc.wow1D
                          + std::sin(wow2Phase)  * tc.wow2D
                          + std::sin(flutPhase)  * tc.flut1D
                          + std::sin(flut2Phase) * tc.flut2D;
            // Tape Old (3): the legacy ADDITIVE offset — same modw added to EVERY
            // head (wow1Phase=0.7Hz wow, flutPhase/flut2Phase=6.3+9.7Hz flutter),
            // scaled by the delay-sample count. Physically wrong, kept on request.
            const float modw = (std::sin(wow1Phase) * kOldWowDepth
                              + (std::sin(flutPhase) + std::sin(flut2Phase)) * kOldFlutDepth)
                              * currentDelaySamples * kOldWobbleScale;

            // Cap the head spacing so the longest head (heads·base) stays inside
            // the buffer even at long times.
            const float base = juce::jmin(currentDelaySamples,
                                          maxDelaySamples / static_cast<float>(heads));

            float wetL = 0.0f, wetR = 0.0f, longest = 0.0f;
            for (int k = 0; k < heads; ++k)
            {
                const float d = juce::jlimit(1.0f, maxDelaySamples,
                                             tapeOld ? (static_cast<float>(k + 1) * base + modw)
                                                     : (static_cast<float>(k + 1) * base * (1.0f + m)));
                const bool last = (k == heads - 1);
                // Advance the read pointer exactly once per sample, on the last tap.
                const float tap = delayLine.popSample(0, d, last);
                wetL += tap * panL[k];
                wetR += tap * panR[k];
                if (last) longest = tap;
            }

            // Playback rolloff: a real playback head loses HF on EVERY repeat, so
            // roll off what we HEAR (incl. the first repeat) — the darkening no
            // longer arrives only via feedback accumulation ("very late"). The
            // feedback path below uses the raw long head, so its own cumulative
            // damping is unchanged; this only shapes the output taps.
            wetL = tapePbFilterL.processSample(wetL);
            wetR = tapePbFilterR.processSample(wetR);

            // Feedback from the long head: high-pass → damping low-pass → soft
            // saturation. tanh(x·drive)/drive keeps small-signal gain ≈ 1 so the
            // feedback feel is unchanged, while hot signals self-limit (tape).
            float f = longest * feedback;
            tapeHpState += aHP * (f - tapeHpState);
            f = f - tapeHpState;
            f = dampFilterL.processSample(f);
            f = std::tanh(f * kTapeDrive) / kTapeDrive;
            delayLine.pushSample(0, mono + f);

            dataL[i] = dryL * dryGain + wetL * wetMix;
            if (stereo)
                dataR[i] = dryR * dryGain + wetR * wetMix;
        }
        else if (isBbd)
        {
            // Bucket-brigade: single tap, a subtle clock-drift warble on the read,
            // then a steep 3-pole reconstruction LP + mid-focus HP (the dark BBD
            // voice), with gentle companding grit in the feedback. Mono → centred.
            const float mono = stereo ? 0.5f * (dryL + dryR) : dryL;

            bbdClockPhase += dBbdClock;
            if (bbdClockPhase > twoPi) bbdClockPhase -= twoPi;
            const BbdChar& bc = kBbdChars[delayCharacter];
            const float dd = juce::jlimit(1.0f, maxDelaySamples,
                                          clampedT * (1.0f + std::sin(bbdClockPhase) * bc.clockDepth));
            const float raw = delayLine.popSample(0, dd, true);

            // OUTPUT: 3 cascaded one-poles = steep, dark reconstruction + a mild HP.
            // This shapes only what we HEAR; it never enters the feedback loop.
            bbdRecon1 += bbdReconCoeff * (raw       - bbdRecon1);
            bbdRecon2 += bbdReconCoeff * (bbdRecon1 - bbdRecon2);
            bbdRecon3 += bbdReconCoeff * (bbdRecon2 - bbdRecon3);
            bbdHpState += bbdHpCoeff   * (bbdRecon3 - bbdHpState);
            const float wet = bbdRecon3 - bbdHpState;

            // FEEDBACK: a SEPARATE, more-open loop LP on the RAW tap (NOT the dark
            // recon, and no mid HP — a 180Hz HP in this short loop compounds over
            // ~13 passes and strips the fundamental, killing sustain). This keeps
            // loop gain so the echo rings/blooms toward self-oscillation. Grit only.
            bbdLoopState += bbdLoopCoeff * (raw - bbdLoopState);
            const float f = std::tanh(bbdLoopState * feedback * bc.drive) / bc.drive;
            delayLine.pushSample(0, mono + f);

            dataL[i] = dryL * dryGain + wet * wetMix;
            if (stereo)
                dataR[i] = dryR * dryGain + wet * wetMix;
        }
        else if (isPingPong)
        {
            const float mono = 0.5f * (dryL + dryR);
            const float dl = delayLine.popSample(0, clampedT, true);
            const float dr = delayLine.popSample(1, clampedT, true);

            // Cross-coupled feedback: each line is fed from the OTHER line's
            // output; the (mono) dry is injected into the left line only.
            const float fbToL = dampFilterL.processSample(dr * feedback);
            const float fbToR = dampFilterR.processSample(dl * feedback);
            delayLine.pushSample(0, mono + fbToL);
            delayLine.pushSample(1, fbToR);

            dataL[i] = dryL * dryGain + dl * wetMix;
            dataR[i] = dryR * dryGain + dr * wetMix;
        }
        else // Digital (dual-mono); also the mono fallback for PingPong
        {
            const float dl = delayLine.popSample(0, clampedT, true);
            const float fbL = dampFilterL.processSample(dl * feedback);
            delayLine.pushSample(0, dryL + fbL);
            dataL[i] = dryL * dryGain + dl * wetMix;

            if (stereo)
            {
                const float dr = delayLine.popSample(1, clampedT, true);
                const float fbR = dampFilterR.processSample(dr * feedback);
                delayLine.pushSample(1, dryR + fbR);
                dataR[i] = dryR * dryGain + dr * wetMix;
            }
        }
    }

    // Count as silent only when input is also silent.
    const float outMag = buffer.getMagnitude(0, numSamples);
    if (outMag < 1e-6f && inputSilent)
        ++silentOutputBlocks;
    else
        silentOutputBlocks = 0;
}

void T5ynthDelayLine::reset()
{
    delayLine.reset();
    dampFilterL.reset();
    dampFilterR.reset();
    tapePbFilterL.reset();
    tapePbFilterR.reset();
    tapeHpState = 0.0f;
    wow1Phase = wow2Phase = flutPhase = flut2Phase = 0.0f;
    bbdRecon1 = bbdRecon2 = bbdRecon3 = 0.0f;
    bbdHpState = 0.0f;
    bbdLoopState = 0.0f;
    bbdClockPhase = 0.0f;
    silentOutputBlocks = 0;
}

void T5ynthDelayLine::setTime(float ms)
{
    delayTimeMs = ms;
    if (prepared)
        targetDelaySamples = juce::jlimit(1.0f, maxDelaySamples,
                                          static_cast<float>(ms * 0.001 * sr));
}

void T5ynthDelayLine::setFeedback(float fb)
{
    targetFeedback = juce::jlimit(0.0f, 0.95f, fb);
}

void T5ynthDelayLine::setMix(float mix)
{
    wetMix = juce::jlimit(0.0f, 1.0f, mix);
}

void T5ynthDelayLine::setDamp(float d)
{
    d = juce::jlimit(0.0f, 1.0f, d);
    if (d == dampAmount)   // steady param → nothing to resolve (no idle work)
        return;
    dampAmount = d;
    recomputeDamp();
}

void T5ynthDelayLine::setMode(int delayType)
{
    if (delayType == mode)
        return;
    mode = delayType;
    // The per-mode damp top changes with the mode (Tape starts darker than
    // Digital at the same knob value), so re-resolve the feedback cutoff.
    recomputeDamp();
}

void T5ynthDelayLine::setCharacter(int c)
{
    delayCharacter = juce::jlimit(0, 3, c);   // 0..2 modern, 3 = Tape Old (Tape only)
    // BBD loop LP cutoff changes with character — rebuild the one-pole coeff.
    // This is a trivial float computation (no allocation), safe on the audio thread.
    // (character 3 only ever pairs with Tape mode; clamp the BBD index defensively.)
    if (mode == kBbd && prepared)
        bbdLoopCoeff = onePoleCoeff(kBbdChars[juce::jmin(delayCharacter, 2)].loopHz, sr);
    // Tape: no coefficients to rebuild (depths read per-sample from kTapeChars).
    // Trigger a damp recompute so the BBD recon top updates for the new character.
    if (mode == kBbd)
        recomputeDamp();
}

void T5ynthDelayLine::recomputeDamp()
{
    // Per-mode TOP of the exponential damp range. Tape starts at an intrinsic
    // baseline rolloff (kTapeDampTopHz) rather than fully open: a gentle HF loss
    // that COMPOUNDS through the feedback loop, so tape repeats darken across
    // generations even at Damp=0 — the defining tape behaviour. Digital/PingPong
    // stay open (20 kHz) at Damp=0, where Damp is the only darkening source. Damp
    // then trims down toward 500 Hz from whichever top.
    //
    // The processor calls setDamp()/setMode() every block; recomputeDamp() runs
    // only when dampAmount or mode actually changed (both setters gate first), and
    // the equality gates below are exact for a steady value — so an idle block
    // rebuilds no filter and never allocates.

    // BBD: the reconstruction cascade tracks Damp from its ~3kHz top toward 500Hz.
    // One-pole coeff only — no makeLowPass alloc, so this branch is allocation-free.
    if (mode == kBbd)
    {
        const float top = kBbdChars[juce::jmin(delayCharacter, 2)].reconHz;
        const float f = top * std::pow(500.0f / top, dampAmount);
        if (f == bbdReconFreq)
            return;
        bbdReconFreq = f;
        if (prepared)
            bbdReconCoeff = onePoleCoeff(f, sr);
        return;
    }

    // Feedback damping low-pass (Digital/PingPong/Tape). Per-mode top: Tape starts
    // at an intrinsic baseline rolloff (kTapeDampTopHz) that COMPOUNDS through the
    // feedback loop, so tape repeats darken across generations even at Damp=0 — the
    // defining tape behaviour. Digital/PingPong stay open (20kHz) at Damp=0, where
    // Damp is the only darkening source. Both trim toward 500 Hz.
    const float topHz = (mode == kTape) ? kTapeDampTopHz : 20000.0f;
    const float newFreq = topHz * std::pow(500.0f / topHz, dampAmount);
    if (newFreq != dampFreq)
    {
        dampFreq = newFreq;
        if (prepared)
            updateDampCoeffs();
    }

    // Tape ALSO rolls off the playback (every repeat, incl. the first). Tracks Damp
    // but bottoms at kTapePbFloorHz (> the feedback's 500 Hz) so cranking Damp keeps
    // the per-repeat playback from double-crushing the top with the feedback.
    if (mode == kTape)
    {
        const float pbFreq = kTapeDampTopHz * std::pow(kTapePbFloorHz / kTapeDampTopHz, dampAmount);
        if (pbFreq != tapePbFreq)
        {
            tapePbFreq = pbFreq;
            if (prepared)
                updateTapePbCoeffs();
        }
    }
}

void T5ynthDelayLine::updateDampCoeffs()
{
    // Butterworth LP (Q ≈ 0.707) at dampFreq
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, dampFreq, 0.707f);
    dampFilterL.coefficients = coeffs;
    dampFilterR.coefficients = coeffs;
}

void T5ynthDelayLine::updateTapePbCoeffs()
{
    // Same 2nd-order shape as the feedback damp, at the playback floor cutoff.
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, tapePbFreq, 0.707f);
    tapePbFilterL.coefficients = coeffs;
    tapePbFilterR.coefficients = coeffs;
}
