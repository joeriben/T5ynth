#include "EarlyReflections.h"

constexpr float EarlyReflections::kTapMsL[];
constexpr float EarlyReflections::kTapMsR[];

void EarlyReflections::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels      = 2;
    line.prepare(spec);

    // Longest tap at the largest Room, plus a block and a couple of samples of
    // slack for the linear interpolator's read-behind.
    const int capacity = static_cast<int>(std::ceil(kMaxTapMs * 0.001f * roomScale(1.0f)
                                                    * static_cast<float>(sampleRate)))
                       + samplesPerBlock + 4;
    line.setMaximumDelayInSamples(capacity);
    maxTapSamples = static_cast<float>(capacity - samplesPerBlock - 2);

    // Gains follow spherical spreading (1/t), normalised so the bank as a whole
    // returns roughly the energy it was fed — the per-type wet trim in FxMixLaw
    // then only has to correct what the late field adds.
    float sumSq = 0.0f;
    for (int k = 0; k < kNumTaps; ++k)
    {
        tapGain[k] = kTapMsL[0] / kTapMsL[k];
        sumSq += tapGain[k] * tapGain[k];
    }
    const float norm = 1.0f / std::sqrt(sumSq);
    for (int k = 0; k < kNumTaps; ++k)
        tapGain[k] *= norm;

    resolveTapBases();
    currentScale = targetScale;
    line.reset();
    prepared = true;
}

void EarlyReflections::reset()
{
    line.reset();
    currentScale = targetScale;
}

void EarlyReflections::setRoomSize(float size)
{
    targetScale = roomScale(juce::jlimit(0.0f, 1.0f, size));
}

void EarlyReflections::setWidth(float w)
{
    width = juce::jlimit(0.0f, 1.0f, w);
}

void EarlyReflections::resolveTapBases()
{
    const float toSamples = 0.001f * static_cast<float>(sr);
    for (int k = 0; k < kNumTaps; ++k)
    {
        tapBaseL[k] = kTapMsL[k] * toSamples;
        tapBaseR[k] = kTapMsR[k] * toSamples;
    }
}

float EarlyReflections::longestTapSamples() const
{
    return kMaxTapMs * 0.001f * static_cast<float>(sr) * juce::jmax(currentScale, targetScale);
}

void EarlyReflections::process(juce::AudioBuffer<float>& buffer)
{
    if (! prepared)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() < 1)
        return;

    // The scale moves toward its target one step per block, and the tap positions
    // are then interpolated from the previous scale to the new one ACROSS the block.
    // Every read position is therefore continuous both within a block and at its
    // boundaries — the step-per-block form spliced the signal at 86 Hz throughout a
    // Room drag. The one-pole only bounds how fast a jumped-to target is reached.
    const float startScale = currentScale;
    currentScale += (targetScale - currentScale) * 0.25f;
    if (std::abs(targetScale - currentScale) < 1.0e-5f)
        currentScale = targetScale;
    const float scaleStep = (currentScale - startScale) / static_cast<float>(numSamples);

    const bool stereo = buffer.getNumChannels() >= 2;
    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = stereo ? buffer.getWritePointer(1) : nullptr;

    float scale = startScale;
    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = dataL[i];
        const float inR = stereo ? dataR[i] : inL;

        line.pushSample(0, inL);
        line.pushSample(1, inR);

        float erL = 0.0f, erR = 0.0f;
        for (int k = 0; k < kNumTaps; ++k)
        {
            // Only the LAST read advances the line's read pointer.
            const bool last = (k == kNumTaps - 1);
            erL += line.popSample(0, tapBaseL[k] * scale, last) * tapGain[k];
            erR += line.popSample(1, tapBaseR[k] * scale, last) * tapGain[k];
        }

        // Width collapses the pattern toward its own mono sum. At width 1 the two
        // channels keep entirely separate tap times, which is where the image
        // comes from — Freeverb's own 0.52 ms offset cannot produce one.
        const float mid = 0.5f * (erL + erR);
        dataL[i] = mid + width * (erL - mid);
        if (stereo)
            dataR[i] = mid + width * (erR - mid);

        scale += scaleStep;
    }
}
