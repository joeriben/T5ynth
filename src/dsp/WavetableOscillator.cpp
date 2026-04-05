#include "WavetableOscillator.h"
#include <algorithm>

void WavetableOscillator::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    phase = 0.0;
    smoothedScan = 0.0f;
    // ~5ms time constant for scan smoothing
    scanSmoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sr * 0.005));
}

void WavetableOscillator::reset()
{
    phase = 0.0;
    smoothedScan = 0.0f;
}

void WavetableOscillator::shareFramesFrom(const WavetableOscillator& source)
{
    sharedSource_ = &source;
}

// ─── FFT (Radix-2 Cooley-Tukey, in-place) ───

void WavetableOscillator::fft(std::vector<double>& re, std::vector<double>& im)
{
    const int n = static_cast<int>(re.size());
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    // Butterfly stages
    for (int len = 2; len <= n; len *= 2)
    {
        const int half = len >> 1;
        const double angle = -2.0 * juce::MathConstants<double>::pi / len;
        const double wRe = std::cos(angle), wIm = std::sin(angle);
        for (int i = 0; i < n; i += len)
        {
            double curRe = 1.0, curIm = 0.0;
            for (int j = 0; j < half; j++)
            {
                const int a = i + j, b = a + half;
                const double tRe = re[b] * curRe - im[b] * curIm;
                const double tIm = re[b] * curIm + im[b] * curRe;
                re[b] = re[a] - tRe; im[b] = im[a] - tIm;
                re[a] = re[a] + tRe; im[a] = im[a] + tIm;
                const double tmp = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = tmp;
            }
        }
    }
}

void WavetableOscillator::ifft(std::vector<double>& re, std::vector<double>& im)
{
    const int n = static_cast<int>(re.size());
    for (int i = 0; i < n; i++) im[i] = -im[i];
    fft(re, im);
    const double invN = 1.0 / n;
    for (int i = 0; i < n; i++)
    {
        re[i] *= invN;
        im[i] = -im[i] * invN;
    }
}

// ─── Mip-level generation ───

void WavetableOscillator::generateMipLevels(const std::vector<std::vector<float>>& srcFrames)
{
    const int nFrames = static_cast<int>(srcFrames.size());

    // Write into the INACTIVE slot (audio thread reads from active slot)
    int writeSlot = 1 - activeSlot_.load(std::memory_order_relaxed);
    auto& dest = mipSlots_[writeSlot];

    dest.frames.clear();
    dest.frames.resize(NUM_MIP_LEVELS);

    // Level 0 = original frames
    dest.frames[0].resize(nFrames);
    for (int f = 0; f < nFrames; f++)
        dest.frames[0][f] = srcFrames[f];

    // FFT buffers (reused)
    std::vector<double> re(FRAME_SIZE), im(FRAME_SIZE);

    for (int level = 1; level < NUM_MIP_LEVELS; level++)
    {
        const int maxHarmonic = HALF_FRAME >> level;
        dest.frames[level].resize(nFrames);

        for (int f = 0; f < nFrames; f++)
        {
            const auto& src = srcFrames[f];
            for (int i = 0; i < FRAME_SIZE; i++) { re[i] = src[i]; im[i] = 0.0; }

            fft(re, im);

            // Zero harmonics above maxHarmonic
            for (int k = maxHarmonic + 1; k <= FRAME_SIZE - maxHarmonic - 1; k++)
            {
                re[k] = 0.0;
                im[k] = 0.0;
            }

            ifft(re, im);

            dest.frames[level][f].resize(FRAME_SIZE);
            for (int i = 0; i < FRAME_SIZE; i++)
                dest.frames[level][f][i] = static_cast<float>(re[i]);
        }
    }

    dest.numFrames = nFrames;
    dest.numLevels = NUM_MIP_LEVELS;

    // Atomically swap — audio thread will pick up the new data on next sample
    activeSlot_.store(writeSlot, std::memory_order_release);
}

// ─── Pitch detection (simplified YIN autocorrelation) ───

float WavetableOscillator::detectPitch(const float* data, int length, double sr)
{
    if (length < 256) return -1.0f;

    const int halfLen = length / 2;
    std::vector<float> diff(halfLen, 0.0f);

    // Difference function
    for (int tau = 1; tau < halfLen; tau++)
    {
        float sum = 0.0f;
        for (int i = 0; i < halfLen; i++)
        {
            float d = data[i] - data[i + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }

    // Cumulative mean normalized difference
    diff[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau < halfLen; tau++)
    {
        running += diff[tau];
        diff[tau] = diff[tau] * tau / running;
    }

    // Absolute threshold (0.1)
    constexpr float threshold = 0.1f;
    int tauEstimate = -1;
    for (int tau = 2; tau < halfLen; tau++)
    {
        if (diff[tau] < threshold)
        {
            while (tau + 1 < halfLen && diff[tau + 1] < diff[tau])
                tau++;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0) return -1.0f;

    // Parabolic interpolation
    float s0 = diff[std::max(0, tauEstimate - 1)];
    float s1 = diff[tauEstimate];
    float s2 = diff[std::min(halfLen - 1, tauEstimate + 1)];
    float refinedTau = tauEstimate + 0.5f * (s0 - s2) / (s0 - 2.0f * s1 + s2 + 1e-10f);

    return static_cast<float>(sr / refinedTau);
}

// ─── Lanczos sinc interpolation ───

float WavetableOscillator::lanczosSample(const float* src, int srcLen, double pos)
{
    const int center = static_cast<int>(std::floor(pos));
    const double frac = pos - center;
    double sum = 0.0, weightSum = 0.0;

    for (int i = -SINC_KERNEL_A + 1; i <= SINC_KERNEL_A; i++)
    {
        int idx = center + i;
        if (idx < 0 || idx >= srcLen) continue;

        double x = frac - i;
        double w;
        if (std::abs(x) < 1e-6)
            w = 1.0;
        else if (std::abs(x) >= SINC_KERNEL_A)
            w = 0.0;
        else
        {
            double piX = juce::MathConstants<double>::pi * x;
            double piXA = piX / SINC_KERNEL_A;
            w = (std::sin(piX) / piX) * (std::sin(piXA) / piXA);
        }

        sum += src[idx] * w;
        weightSum += w;
    }

    return weightSum > 0.0 ? static_cast<float>(sum / weightSum) : 0.0f;
}

// ─── Frame extraction from audio buffer ───

void WavetableOscillator::extractFramesFromBuffer(const juce::AudioBuffer<float>& buffer, double bufferSr,
                                                   float startFrac, float endFrac)
{
    if (sharedSource_ != nullptr) return; // shared-mode oscillators don't own frame data
    const int bufferLen = buffer.getNumSamples();

    // Apply extraction region (brackets)
    startFrac = juce::jlimit(0.0f, 1.0f, startFrac);
    endFrac   = juce::jlimit(0.0f, 1.0f, endFrac);
    if (endFrac <= startFrac) endFrac = 1.0f;

    const int regionStart = static_cast<int>(startFrac * bufferLen);
    const int regionEnd   = static_cast<int>(endFrac * bufferLen);
    const float* data = buffer.getReadPointer(0) + regionStart;
    const int totalSamples = regionEnd - regionStart;

    if (totalSamples < FRAME_SIZE) return;

    // Detect pitch within the extraction region
    const int analysisLen = std::min(4096, totalSamples);
    float detectedPitch = detectPitch(data, analysisLen, bufferSr);

    std::vector<std::vector<float>> frames;

    if (detectedPitch > 20.0f && detectedPitch < 5000.0f)
    {
        // Pitch-synchronous extraction
        double periodSamples = bufferSr / detectedPitch;
        int pos = 0;

        while (pos + static_cast<int>(periodSamples * 1.5) < totalSamples && static_cast<int>(frames.size()) < 256)
        {
            // Extract one period and resample to FRAME_SIZE via Lanczos
            std::vector<float> frame(FRAME_SIZE);
            for (int i = 0; i < FRAME_SIZE; i++)
            {
                double srcPos = pos + (static_cast<double>(i) / FRAME_SIZE) * periodSamples;
                frame[i] = lanczosSample(data, totalSamples, srcPos);
            }

            // Seamless loop: linear ramp correction at boundary
            float diff = frame[0] - frame[FRAME_SIZE - 1];
            for (int i = 0; i < FRAME_SIZE; i++)
                frame[i] += diff * static_cast<float>(i) / FRAME_SIZE;

            frames.push_back(std::move(frame));
            pos += static_cast<int>(periodSamples);
        }
    }
    else
    {
        // Unpitched: overlapping windowed extraction
        const int hop = FRAME_SIZE / 2;
        int pos = 0;

        while (pos + FRAME_SIZE <= totalSamples && static_cast<int>(frames.size()) < 256)
        {
            std::vector<float> frame(FRAME_SIZE);
            for (int i = 0; i < FRAME_SIZE; i++)
            {
                // Hann window
                float window = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / FRAME_SIZE));
                frame[i] = data[pos + i] * window;
            }

            // Seamless loop correction
            float diff = frame[0] - frame[FRAME_SIZE - 1];
            for (int i = 0; i < FRAME_SIZE; i++)
                frame[i] += diff * static_cast<float>(i) / FRAME_SIZE;

            frames.push_back(std::move(frame));
            pos += hop;
        }
    }

    // Remove near-silent frames (peak below -40dB ≈ 0.01)
    frames.erase(std::remove_if(frames.begin(), frames.end(), [](const std::vector<float>& f) {
        float peak = 0.0f;
        for (float s : f) peak = std::max(peak, std::abs(s));
        return peak < 0.01f;
    }), frames.end());

    // Normalize each frame to peak 0.95 (essential for wavetable playback)
    for (auto& frame : frames)
    {
        float peak = 0.0f;
        for (float s : frame) peak = std::max(peak, std::abs(s));
        if (peak > 0.001f)
        {
            float gain = 0.95f / peak;
            for (float& s : frame) s *= gain;
        }
    }

    // Ensure minimum frame count
    while (static_cast<int>(frames.size()) < MIN_FRAMES && !frames.empty())
    {
        frames.push_back(frames.back());
    }

    if (!frames.empty())
        generateMipLevels(frames);
}

// ─── Auto-scan (sampler-style temporal progression) ───

void WavetableOscillator::setAutoScanRate(double bufferSR, int bufferLen)
{
    // Scan goes from 0 to 1 in (bufferLen / bufferSR) seconds
    double durationSeconds = static_cast<double>(bufferLen) / bufferSR;
    if (durationSeconds > 0.0)
        autoScanIncr_ = 1.0 / (durationSeconds * sampleRate);
    else
        autoScanIncr_ = 0.0;
}

void WavetableOscillator::setAutoScanLoop(float startFrac, float endFrac, LoopMode mode)
{
    autoScanLoopStart_ = juce::jlimit(0.0f, 1.0f, startFrac);
    autoScanLoopEnd_   = juce::jlimit(0.0f, 1.0f, endFrac);
    if (autoScanLoopEnd_ <= autoScanLoopStart_)
        autoScanLoopEnd_ = 1.0f;
    autoScanLoopMode_ = mode;
}

void WavetableOscillator::retriggerAutoScan()
{
    autoScanPos_ = static_cast<double>(autoScanLoopStart_);
    autoScanReverse_ = false;
    // Jump scan immediately (no smoothing lag on retrigger)
    targetScanPosition = static_cast<float>(autoScanPos_);
    smoothedScan = targetScanPosition;
}

// ─── Contiguous frame extraction (sampler-style) ───

void WavetableOscillator::extractContiguousFrames(const juce::AudioBuffer<float>& buffer, double bufferSR,
                                                    float startFrac, float endFrac)
{
    if (sharedSource_ != nullptr) return;

    const int bufferLen = buffer.getNumSamples();
    startFrac = juce::jlimit(0.0f, 1.0f, startFrac);
    endFrac   = juce::jlimit(0.0f, 1.0f, endFrac);
    if (endFrac <= startFrac) endFrac = 1.0f;

    const int regionStart = static_cast<int>(startFrac * bufferLen);
    const int regionEnd   = static_cast<int>(endFrac * bufferLen);
    const float* data = buffer.getReadPointer(0) + regionStart;
    const int totalSamples = regionEnd - regionStart;

    if (totalSamples < FRAME_SIZE) return;

    std::vector<std::vector<float>> frames;

    // Extract contiguous windows — no pitch detection, no resampling
    int pos = 0;
    while (pos + FRAME_SIZE <= totalSamples)
    {
        std::vector<float> frame(FRAME_SIZE);
        for (int i = 0; i < FRAME_SIZE; i++)
            frame[i] = data[pos + i];

        // Seamless loop correction (linear ramp to close boundary gap)
        float diff = frame[0] - frame[FRAME_SIZE - 1];
        for (int i = 0; i < FRAME_SIZE; i++)
            frame[i] += diff * static_cast<float>(i) / FRAME_SIZE;

        frames.push_back(std::move(frame));
        pos += FRAME_SIZE;  // non-overlapping
    }

    // Normalize each frame
    for (auto& frame : frames)
    {
        float peak = 0.0f;
        for (float s : frame) peak = std::max(peak, std::abs(s));
        if (peak > 0.001f)
        {
            float gain = 0.95f / peak;
            for (float& s : frame) s *= gain;
        }
    }

    if (static_cast<int>(frames.size()) < MIN_FRAMES && !frames.empty())
        while (static_cast<int>(frames.size()) < MIN_FRAMES)
            frames.push_back(frames.back());

    if (!frames.empty())
        generateMipLevels(frames);
}

// ─── Per-sample processing ───

void WavetableOscillator::glideToFrequency(float hz, float durationMs)
{
    hz = juce::jlimit(20.0f, 20000.0f, hz);
    float durationSamples = (durationMs / 1000.0f) * static_cast<float>(sampleRate);
    int samples = std::max(1, static_cast<int>(durationSamples));

    glideFreqTarget = hz;
    glideFreqIncr = (hz - targetFrequency) / static_cast<float>(samples);
    glideFreqSamplesLeft = samples;
}

float WavetableOscillator::processSample()
{
    // Snapshot the active mip data — lock-free, consistent for this sample
    const auto& mip = getActiveMipData();
    if (mip.numFrames == 0) return 0.0f;

    // Apply frequency glide (per-sample linear ramp)
    if (glideFreqSamplesLeft > 0)
    {
        targetFrequency += glideFreqIncr;
        glideFreqSamplesLeft--;
        if (glideFreqSamplesLeft == 0)
            targetFrequency = glideFreqTarget;
    }

    // Auto-scan: advance scan position per sample
    if (autoScan_)
    {
        const double loopLen = static_cast<double>(autoScanLoopEnd_ - autoScanLoopStart_);

        if (autoScanReverse_)
            autoScanPos_ -= autoScanIncr_;
        else
            autoScanPos_ += autoScanIncr_;

        // Loop wrapping
        if (autoScanPos_ >= static_cast<double>(autoScanLoopEnd_))
        {
            if (autoScanLoopMode_ == LoopMode::OneShot)
                autoScanPos_ = static_cast<double>(autoScanLoopEnd_) - 0.0001;
            else if (autoScanLoopMode_ == LoopMode::PingPong)
            {
                autoScanPos_ = static_cast<double>(autoScanLoopEnd_) - (autoScanPos_ - static_cast<double>(autoScanLoopEnd_));
                autoScanReverse_ = true;
            }
            else // Loop
                autoScanPos_ -= loopLen;
        }
        else if (autoScanPos_ < static_cast<double>(autoScanLoopStart_))
        {
            if (autoScanLoopMode_ == LoopMode::PingPong)
            {
                autoScanPos_ = static_cast<double>(autoScanLoopStart_) + (static_cast<double>(autoScanLoopStart_) - autoScanPos_);
                autoScanReverse_ = false;
            }
            else
                autoScanPos_ += loopLen;
        }

        targetScanPosition = static_cast<float>(autoScanPos_);
    }

    // Smooth scan position
    smoothedScan += (targetScanPosition - smoothedScan) * scanSmoothCoeff;

    // Select mip level based on frequency
    const float invBaseFreq = FRAME_SIZE / static_cast<float>(sampleRate);
    const float rawLevel = std::log2(targetFrequency * invBaseFreq);
    const int mipLevel = juce::jlimit(0, mip.numLevels - 1, static_cast<int>(std::ceil(rawLevel)));
    const auto& frames = mip.frames[mipLevel];
    const int nf = mip.numFrames;

    // Frame selection
    const float framePos = smoothedScan * (nf - 1);
    const int frameA = static_cast<int>(std::floor(framePos));

    // Sample position via phase accumulator
    const int idx0 = static_cast<int>(std::floor(phase)) % FRAME_SIZE;
    const int idx1 = (idx0 + 1) % FRAME_SIZE;
    const float phaseFrac = static_cast<float>(phase - std::floor(phase));

    const auto& fA = frames[juce::jlimit(0, nf - 1, frameA)];
    const float sampleA = fA[idx0] + (fA[idx1] - fA[idx0]) * phaseFrac;

    float output;

    if (doInterpolate)
    {
        // Catmull-Rom cubic interpolation across 4 frames
        const float frameMix = framePos - frameA;
        const int i0 = juce::jlimit(0, nf - 1, frameA - 1);
        const int i1 = juce::jlimit(0, nf - 1, frameA);
        const int i2 = juce::jlimit(0, nf - 1, frameA + 1);
        const int i3 = juce::jlimit(0, nf - 1, frameA + 2);

        const auto& f0 = frames[i0];
        const auto& f1 = frames[i1];
        const auto& f2 = frames[i2];
        const auto& f3 = frames[i3];

        const float s0 = f0[idx0] + (f0[idx1] - f0[idx0]) * phaseFrac;
        const float s1 = f1[idx0] + (f1[idx1] - f1[idx0]) * phaseFrac;
        const float s2 = f2[idx0] + (f2[idx1] - f2[idx0]) * phaseFrac;
        const float s3 = f3[idx0] + (f3[idx1] - f3[idx0]) * phaseFrac;

        // Catmull-Rom spline
        const float t = frameMix;
        const float t2 = t * t;
        const float t3 = t2 * t;
        output = 0.5f * ((2.0f * s1)
            + (-s0 + s2) * t
            + (2.0f * s0 - 5.0f * s1 + 4.0f * s2 - s3) * t2
            + (-s0 + 3.0f * s1 - 3.0f * s2 + s3) * t3);
    }
    else
    {
        output = sampleA;
    }

    // Advance phase
    phase += targetFrequency * FRAME_SIZE / sampleRate;
    if (phase >= FRAME_SIZE)
        phase -= FRAME_SIZE * std::floor(phase / FRAME_SIZE);

    return output;
}
