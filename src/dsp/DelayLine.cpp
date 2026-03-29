#include "DelayLine.h"

void T5ynthDelayLine::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    delayLine.prepare(spec);
    delayLine.setDelay(static_cast<float>(delayTimeMs * 0.001 * sr));

    // Initialize damping filters (Butterworth LP, Q ~= 0.707)
    updateDampCoeffs();

    prepared = true;
}

void T5ynthDelayLine::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Parallel send-bus: wet signal is ADDED to dry (not crossfaded).
    // Dry compensation: reduce dry by mix * 0.3 to prevent clipping.
    const float dryGain = 1.0f - wetMix * 0.3f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        auto& dampFilter = (ch == 0) ? dampFilterL : dampFilterR;

        for (int i = 0; i < numSamples; ++i)
        {
            float drySample = data[i];
            float delayed = delayLine.popSample(ch);

            // Feedback path with damping LP filter
            float fbSample = delayed * feedback;
            float dampedFb = dampFilter.processSample(fbSample);
            delayLine.pushSample(ch, drySample + dampedFb);

            // Parallel mix: dry (compensated) + wet (delayed)
            data[i] = drySample * dryGain + delayed * wetMix;
        }
    }
}

void T5ynthDelayLine::reset()
{
    delayLine.reset();
    dampFilterL.reset();
    dampFilterR.reset();
}

void T5ynthDelayLine::setTime(float ms)
{
    delayTimeMs = ms;
    if (prepared)
        delayLine.setDelay(static_cast<float>(ms * 0.001 * sr));
}

void T5ynthDelayLine::setFeedback(float fb)
{
    feedback = juce::jlimit(0.0f, 0.95f, fb);
}

void T5ynthDelayLine::setMix(float mix)
{
    wetMix = juce::jlimit(0.0f, 1.0f, mix);
}

void T5ynthDelayLine::setDamp(float d)
{
    // Exponential mapping: 0 = bright (20kHz), 1 = dark (500Hz)
    // freq = 20000 * pow(500/20000, d)
    d = juce::jlimit(0.0f, 1.0f, d);
    dampFreq = 20000.0f * std::pow(500.0f / 20000.0f, d);
    if (prepared)
        updateDampCoeffs();
}

void T5ynthDelayLine::updateDampCoeffs()
{
    // Butterworth LP (Q ≈ 0.707) at dampFreq
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, dampFreq, 0.707f);
    dampFilterL.coefficients = coeffs;
    dampFilterR.coefficients = coeffs;
}
