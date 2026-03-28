#include "AudioLooper.h"

void AudioLooper::prepare(double sampleRate, int /*samplesPerBlock*/)
{
    playbackSampleRate = sampleRate;
}

void AudioLooper::reset()
{
    loopBuffer.setSize(0, 0);
    audioLoaded = false;
    readPosition = 0.0;
    playing = true;
}

void AudioLooper::loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate)
{
    loopBuffer.makeCopyOf(buffer);
    bufferOriginalSR = bufferSampleRate;
    readPosition = 0.0;
    audioLoaded = true;
    playing = true;
}

void AudioLooper::processBlock(juce::AudioBuffer<float>& output)
{
    if (!audioLoaded || !playing || loopBuffer.getNumSamples() == 0)
        return;

    const int numOutChannels = output.getNumChannels();
    const int numOutSamples = output.getNumSamples();
    const int loopLength = loopBuffer.getNumSamples();
    const int loopChannels = loopBuffer.getNumChannels();
    const double speedRatio = (bufferOriginalSR / playbackSampleRate) * transposeRatio;

    for (int i = 0; i < numOutSamples; ++i)
    {
        int pos0 = static_cast<int>(readPosition) % loopLength;
        int pos1 = (pos0 + 1) % loopLength;
        float frac = static_cast<float>(readPosition - std::floor(readPosition));

        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            int srcCh = ch < loopChannels ? ch : 0;
            float s0 = loopBuffer.getSample(srcCh, pos0);
            float s1 = loopBuffer.getSample(srcCh, pos1);
            output.addSample(ch, i, s0 + (s1 - s0) * frac);
        }

        readPosition += speedRatio;
        if (readPosition >= static_cast<double>(loopLength))
            readPosition -= static_cast<double>(loopLength);
    }
}
