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
    if (!morphActive_)
        morphAlpha_ = 1.0f;
}

WavetableOscillator::MipDataPtr WavetableOscillator::loadPublishedMipData() const
{
    return publishedMipData_.load(std::memory_order_acquire);
}

bool WavetableOscillator::hasFrames() const
{
    if (activeMorphMipData_ != nullptr)
        return activeMorphMipData_->numFrames > 0;

    auto published = loadPublishedMipData();
    return published != nullptr && published->numFrames > 0;
}

int WavetableOscillator::getNumFrames() const
{
    if (activeMorphMipData_ != nullptr)
        return activeMorphMipData_->numFrames;

    auto published = loadPublishedMipData();
    return published != nullptr ? published->numFrames : 0;
}

void WavetableOscillator::syncSharedConfigFrom(const WavetableOscillator& source)
{
    // Copy traversal/morph configuration, but keep per-voice runtime state.
    autoScan_ = source.autoScan_;
    autoScanIncr_ = source.autoScanIncr_;
    autoScanStartPos_ = source.autoScanStartPos_;
    autoScanLoopStart_ = source.autoScanLoopStart_;
    autoScanLoopEnd_ = source.autoScanLoopEnd_;
    autoScanLoopMode_ = source.autoScanLoopMode_;
    morphTimeMs_ = source.morphTimeMs_;
}

void WavetableOscillator::adoptMipData(const MipDataPtr& mipData)
{
    if (mipData == nullptr)
        return;

    publishedMipData_.store(mipData, std::memory_order_release);
    activeMorphMipData_ = mipData;
    targetMorphMipData_.reset();
    morphAlpha_ = 1.0f;
    morphIncrement_ = 0.0f;
    morphActive_ = false;
}

void WavetableOscillator::beginMorphToMipData(const MipDataPtr& mipData)
{
    if (mipData == nullptr)
        return;

    publishedMipData_.store(mipData, std::memory_order_release);

    if (activeMorphMipData_ == nullptr || activeMorphMipData_->numFrames == 0)
    {
        adoptMipData(mipData);
        return;
    }

    if (activeMorphMipData_->generation == mipData->generation
        || (targetMorphMipData_ != nullptr && targetMorphMipData_->generation == mipData->generation))
    {
        if (!morphActive_ && activeMorphMipData_->generation == mipData->generation)
            adoptMipData(mipData);
        return;
    }

    if (morphTimeMs_ <= 0.0f)
    {
        adoptMipData(mipData);
        return;
    }

    if (morphActive_ && targetMorphMipData_ != nullptr)
    {
        const bool targetDominant = morphAlpha_ >= 0.5f;
        activeMorphMipData_ = targetDominant ? targetMorphMipData_ : activeMorphMipData_;
    }

    if (activeMorphMipData_ == nullptr
        || activeMorphMipData_->generation == mipData->generation)
    {
        adoptMipData(mipData);
        return;
    }

    targetMorphMipData_ = mipData;
    morphAlpha_ = 0.0f;
    const int morphSamples = std::max(1, static_cast<int>(std::round(
        static_cast<double>(morphTimeMs_) * 0.001 * sampleRate)));
    morphIncrement_ = 1.0f / static_cast<float>(morphSamples);
    morphActive_ = true;
}

void WavetableOscillator::shareFramesFrom(const WavetableOscillator& source)
{
    sharedSource_ = &source;
    syncSharedConfigFrom(source);

    auto mipData = source.loadPublishedMipData();
    if (mipData == nullptr)
        return;

    const bool sameActive = activeMorphMipData_ != nullptr
        && activeMorphMipData_->generation == mipData->generation;
    if (sameActive && !morphActive_)
    {
        publishedMipData_.store(mipData, std::memory_order_release);
        return;
    }

    adoptMipData(mipData);
}

void WavetableOscillator::morphToFramesFrom(const WavetableOscillator& source)
{
    sharedSource_ = &source;
    syncSharedConfigFrom(source);

    auto mipData = source.loadPublishedMipData();
    if (mipData == nullptr)
        return;

    publishedMipData_.store(mipData, std::memory_order_release);

    const bool sameActive = activeMorphMipData_ != nullptr
        && activeMorphMipData_->generation == mipData->generation;
    const bool sameTarget = targetMorphMipData_ != nullptr
        && targetMorphMipData_->generation == mipData->generation;

    if ((sameActive && !morphActive_) || sameTarget)
        return;

    beginMorphToMipData(mipData);
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
    auto dest = std::make_shared<MipData>();
    dest->frames.resize(NUM_MIP_LEVELS);

    // Level 0 = original frames
    dest->frames[0].resize(nFrames);
    for (int f = 0; f < nFrames; f++)
        dest->frames[0][f] = srcFrames[f];

    // FFT buffers (reused)
    std::vector<double> re(FRAME_SIZE), im(FRAME_SIZE);

    for (int level = 1; level < NUM_MIP_LEVELS; level++)
    {
        const int maxHarmonic = HALF_FRAME >> level;
        dest->frames[level].resize(nFrames);

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

            dest->frames[level][f].resize(FRAME_SIZE);
            for (int i = 0; i < FRAME_SIZE; i++)
                dest->frames[level][f][i] = static_cast<float>(re[i]);
        }
    }

    dest->numFrames = nFrames;
    dest->numLevels = NUM_MIP_LEVELS;
    dest->generation = ++nextPublishedGeneration_;
    MipDataPtr published = dest;
    publishedMipData_.store(std::move(published), std::memory_order_release);
}

// ─── Pitch detection (simplified YIN autocorrelation) ───

WavetableOscillator::PitchEstimate WavetableOscillator::analyzePitchWindow(const float* data,
                                                                           int length, double sr)
{
    if (length < 256) return {};

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

    if (tauEstimate < 0) return {};

    // Parabolic interpolation
    float s0 = diff[std::max(0, tauEstimate - 1)];
    float s1 = diff[tauEstimate];
    float s2 = diff[std::min(halfLen - 1, tauEstimate + 1)];
    float refinedTau = tauEstimate + 0.5f * (s0 - s2) / (s0 - 2.0f * s1 + s2 + 1e-10f);

    PitchEstimate result;
    result.hz = static_cast<float>(sr / refinedTau);
    result.confidence = juce::jlimit(0.0f, 1.0f, 1.0f - s1);
    return result;
}

float WavetableOscillator::detectPitch(const float* data, int length, double sr)
{
    return analyzePitchWindow(data, length, sr).hz;
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

int WavetableOscillator::nearestZeroCrossing(const float* data, int length, int pos, int maxSearch)
{
    if (length < 2) return juce::jlimit(0, juce::jmax(0, length - 1), pos);

    pos = juce::jlimit(0, length - 2, pos);
    maxSearch = juce::jlimit(0, length - 2, maxSearch);

    for (int d = 0; d <= maxSearch; ++d)
    {
        const int fwd = pos + d;
        if (fwd < length - 1 && data[fwd] * data[fwd + 1] <= 0.0f)
            return fwd;

        const int bwd = pos - d;
        if (bwd >= 0 && bwd < length - 1 && data[bwd] * data[bwd + 1] <= 0.0f)
            return bwd;
    }

    return pos;
}

std::vector<float> WavetableOscillator::extractResampledPeriod(const float* data, int totalSamples,
                                                               double start, double periodSamples)
{
    std::vector<float> frame(FRAME_SIZE);
    for (int i = 0; i < FRAME_SIZE; ++i)
    {
        const double srcPos = start + (static_cast<double>(i) / FRAME_SIZE) * periodSamples;
        frame[i] = lanczosSample(data, totalSamples, srcPos);
    }
    return frame;
}

double WavetableOscillator::computeLoopBoundaryError(const std::vector<float>& frame)
{
    if (frame.size() < 4)
        return 0.0;

    const float boundary = std::abs(frame.front() - frame.back());
    const float startSlope = frame[1] - frame[0];
    const float endSlope = frame[static_cast<int>(frame.size()) - 1]
                         - frame[static_cast<int>(frame.size()) - 2];
    const float slopeMismatch = std::abs(startSlope - endSlope);
    return static_cast<double>(boundary) + 0.5 * static_cast<double>(slopeMismatch);
}

// ─── Frame extraction from audio buffer ───

void WavetableOscillator::extractFramesFromBuffer(const juce::AudioBuffer<float>& buffer, double bufferSr,
                                                   float startFrac, float endFrac, int maxFrames)
{
    maxFrames = juce::jlimit(8, 256, maxFrames);
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

    std::vector<std::vector<float>> frames;
    constexpr int analysisWindow = 4096;
    constexpr int analysisHop = analysisWindow / 2;
    constexpr float pitchConfidenceThreshold = 0.9f;
    std::vector<float> pitchCandidates;

    if (totalSamples >= 256)
    {
        if (totalSamples >= analysisWindow)
        {
            for (int windowStart = 0; windowStart + analysisWindow <= totalSamples; windowStart += analysisHop)
            {
                PitchEstimate estimate = analyzePitchWindow(data + windowStart, analysisWindow, bufferSr);
                if (estimate.confidence >= pitchConfidenceThreshold
                    && estimate.hz > 20.0f && estimate.hz < 5000.0f)
                {
                    pitchCandidates.push_back(estimate.hz);
                }
            }
        }
        else
        {
            PitchEstimate estimate = analyzePitchWindow(data, totalSamples, bufferSr);
            if (estimate.confidence >= pitchConfidenceThreshold
                && estimate.hz > 20.0f && estimate.hz < 5000.0f)
            {
                pitchCandidates.push_back(estimate.hz);
            }
        }
    }

    const bool allowPitchSync = !pitchCandidates.empty()
        && (pitchCandidates.size() >= 2 || totalSamples < analysisWindow * 2);

    if (allowPitchSync)
    {
        std::sort(pitchCandidates.begin(), pitchCandidates.end());
        const float detectedPitch = pitchCandidates[pitchCandidates.size() / 2];

        // Pitch-synchronous extraction with adaptive pitch tracking
        double periodSamples = bufferSr / detectedPitch;
        double pos = 0.0;
        int framesSinceDetect = 0;
        constexpr int REDETECT_INTERVAL = 8;

        while (static_cast<int>(pos + periodSamples * 1.5) < totalSamples
               && static_cast<int>(frames.size()) < maxFrames)
        {
            const double baseStart = pos;
            std::vector<float> frame = extractResampledPeriod(data, totalSamples, baseStart, periodSamples);
            double bestError = computeLoopBoundaryError(frame);
            double selectedStart = baseStart;

            const int searchRadius = juce::jlimit(0, totalSamples - 1,
                static_cast<int>(std::floor(periodSamples * 0.125)));
            if (searchRadius > 0)
            {
                const int baseIndex = juce::jlimit(0, totalSamples - 1,
                    static_cast<int>(std::round(baseStart)));
                const int candidateStart = nearestZeroCrossing(data, totalSamples, baseIndex, searchRadius);

                if (candidateStart != baseIndex
                    && static_cast<double>(candidateStart) + periodSamples <= totalSamples)
                {
                    std::vector<float> snapped = extractResampledPeriod(
                        data, totalSamples, static_cast<double>(candidateStart), periodSamples);
                    const double snappedError = computeLoopBoundaryError(snapped);

                    if (snappedError + 1.0e-6 < bestError)
                    {
                        frame = std::move(snapped);
                        bestError = snappedError;
                        selectedStart = static_cast<double>(candidateStart);
                    }
                }
            }

            // Seamless loop: linear ramp correction at boundary
            float diff = frame[0] - frame[FRAME_SIZE - 1];
            for (int i = 0; i < FRAME_SIZE; i++)
                frame[i] += diff * static_cast<float>(i) / FRAME_SIZE;

            frames.push_back(std::move(frame));
            pos = selectedStart + periodSamples;

            // Re-detect pitch periodically to track evolving content
            if (++framesSinceDetect >= REDETECT_INTERVAL)
            {
                int intPos = static_cast<int>(pos);
                int remaining = totalSamples - intPos;
                int localAnalysisLen = std::min(analysisWindow, remaining);
                if (localAnalysisLen >= 256)
                {
                    PitchEstimate estimate = analyzePitchWindow(data + intPos, localAnalysisLen, bufferSr);
                    if (estimate.confidence >= pitchConfidenceThreshold
                        && estimate.hz > 20.0f && estimate.hz < 5000.0f)
                    {
                        periodSamples = bufferSr / estimate.hz;
                    }
                }
                framesSinceDetect = 0;
            }
        }
    }
    else
    {
        // Unpitched: overlapping windowed extraction
        const int hop = FRAME_SIZE / 2;
        int pos = 0;

        while (pos + FRAME_SIZE <= totalSamples && static_cast<int>(frames.size()) < maxFrames)
        {
            std::vector<float> frame(FRAME_SIZE);
            constexpr float tukeyAlpha = 0.1f;
            const int taperLen = static_cast<int>(tukeyAlpha * FRAME_SIZE * 0.5f);
            for (int i = 0; i < FRAME_SIZE; i++)
            {
                // Tukey window: flat center (90%), cosine taper at edges (5% each side)
                float window = 1.0f;
                if (i < taperLen)
                    window = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * static_cast<float>(i) / taperLen));
                else if (i >= FRAME_SIZE - taperLen)
                    window = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * static_cast<float>(FRAME_SIZE - 1 - i) / taperLen));
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

void WavetableOscillator::setAutoScanStartPos(float frac)
{
    autoScanStartPos_ = juce::jlimit(0.0f, 1.0f, frac);
}

void WavetableOscillator::retriggerAutoScan()
{
    // Determine direction from P1 vs P3
    bool reversed = (autoScanStartPos_ > autoScanLoopEnd_);
    autoScanDirection_ = reversed ? -1 : 1;
    autoScanInFirstPass_ = true;

    autoScanPos_ = static_cast<double>(autoScanStartPos_);
    // Jump scan immediately (no smoothing lag on retrigger)
    const float scanTarget = autoScan_
        ? juce::jlimit(0.0f, 1.0f, static_cast<float>(autoScanPos_) + scanControl_)
        : static_cast<float>(autoScanPos_);
    smoothedScan = scanTarget;
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
    if (activeMorphMipData_ == nullptr)
        activeMorphMipData_ = loadPublishedMipData();
    if (activeMorphMipData_ == nullptr || activeMorphMipData_->numFrames == 0)
        return 0.0f;

    // Apply frequency glide (per-sample linear ramp)
    if (glideFreqSamplesLeft > 0)
    {
        targetFrequency += glideFreqIncr;
        glideFreqSamplesLeft--;
        if (glideFreqSamplesLeft == 0)
            targetFrequency = glideFreqTarget;
    }

    float scanTarget = scanControl_;

    // Auto-scan: advance scan position per sample (3-point logic)
    if (autoScan_)
    {
        const double pEnd   = static_cast<double>(autoScanLoopEnd_);
        const double pStart = static_cast<double>(autoScanLoopStart_);

        autoScanPos_ += autoScanIncr_ * autoScanDirection_;

        if (autoScanInFirstPass_)
        {
            // During first pass, only check the boundary we're moving toward
            if (autoScanDirection_ > 0 && autoScanPos_ >= pEnd)
            {
                autoScanInFirstPass_ = false;
                if (autoScanLoopMode_ == LoopMode::OneShot)
                    autoScanPos_ = pEnd - 0.0001;
                else if (autoScanLoopMode_ == LoopMode::PingPong)
                {
                    autoScanPos_ = pEnd - (autoScanPos_ - pEnd);
                    autoScanDirection_ = -1;
                }
                else // Loop
                    autoScanPos_ = pStart + (autoScanPos_ - pEnd);
            }
            else if (autoScanDirection_ < 0 && autoScanPos_ < pStart)
            {
                autoScanInFirstPass_ = false;
                if (autoScanLoopMode_ == LoopMode::OneShot)
                    autoScanPos_ = pStart;
                else if (autoScanLoopMode_ == LoopMode::PingPong)
                {
                    autoScanPos_ = pStart + (pStart - autoScanPos_);
                    autoScanDirection_ = 1;
                }
                else // Loop
                    autoScanPos_ = pEnd - (pStart - autoScanPos_);
            }
        }
        else
        {
            // Standard looping between P2-P3
            if (autoScanPos_ >= pEnd)
            {
                if (autoScanLoopMode_ == LoopMode::PingPong)
                {
                    autoScanPos_ = pEnd - (autoScanPos_ - pEnd);
                    autoScanDirection_ = -1;
                }
                else
                    autoScanPos_ = pStart + (autoScanPos_ - pEnd);
            }
            else if (autoScanPos_ < pStart)
            {
                if (autoScanLoopMode_ == LoopMode::PingPong)
                {
                    autoScanPos_ = pStart + (pStart - autoScanPos_);
                    autoScanDirection_ = 1;
                }
                else
                    autoScanPos_ = pEnd - (pStart - autoScanPos_);
            }
        }

        scanTarget = juce::jlimit(0.0f, 1.0f,
            static_cast<float>(autoScanPos_) + scanControl_);
    }

    // Smooth scan position
    smoothedScan += (scanTarget - smoothedScan) * scanSmoothCoeff;
    float output = readMipSample(*activeMorphMipData_, phase, smoothedScan, targetFrequency,
                                 sampleRate, doInterpolate);

    if (morphActive_ && targetMorphMipData_ != nullptr && targetMorphMipData_->numFrames > 0)
    {
        const float alpha = juce::jlimit(0.0f, 1.0f, morphAlpha_);
        const float targetSample = readMipSample(*targetMorphMipData_, phase, smoothedScan,
                                                 targetFrequency, sampleRate, doInterpolate);
        const float dryGain = std::cos(alpha * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin(alpha * juce::MathConstants<float>::halfPi);
        output = output * dryGain + targetSample * wetGain;

        morphAlpha_ += morphIncrement_;
        if (morphAlpha_ >= 1.0f)
            adoptMipData(targetMorphMipData_);
    }

    // Advance phase
    phase += targetFrequency * FRAME_SIZE / sampleRate;
    if (phase >= FRAME_SIZE)
        phase -= FRAME_SIZE * std::floor(phase / FRAME_SIZE);

    return output;
}

float WavetableOscillator::readMipSample(const MipData& mipData, double phase, float scanPosition,
                                         float frequency, double sampleRate, bool interpolate)
{
    if (mipData.numFrames <= 0 || mipData.numLevels <= 0)
        return 0.0f;

    const float invBaseFreq = FRAME_SIZE / static_cast<float>(sampleRate);
    const float rawLevel = std::log2(juce::jmax(1.0e-6f, frequency * invBaseFreq));
    const int mipLevel = juce::jlimit(0, mipData.numLevels - 1,
                                      static_cast<int>(std::ceil(rawLevel)));
    const auto& frames = mipData.frames[static_cast<size_t>(mipLevel)];
    const int nf = mipData.numFrames;

    const float framePos = juce::jlimit(0.0f, 1.0f, scanPosition) * static_cast<float>(nf - 1);
    const int frameA = static_cast<int>(std::floor(framePos));
    const int idx0 = static_cast<int>(std::floor(phase)) % FRAME_SIZE;
    const int idx1 = (idx0 + 1) % FRAME_SIZE;
    const float phaseFrac = static_cast<float>(phase - std::floor(phase));

    if (!interpolate)
    {
        const auto& frame = frames[static_cast<size_t>(juce::jlimit(0, nf - 1, frameA))];
        return frame[static_cast<size_t>(idx0)]
             + (frame[static_cast<size_t>(idx1)] - frame[static_cast<size_t>(idx0)]) * phaseFrac;
    }

    const float frameMix = framePos - static_cast<float>(frameA);
    const int i0 = juce::jlimit(0, nf - 1, frameA - 1);
    const int i1 = juce::jlimit(0, nf - 1, frameA);
    const int i2 = juce::jlimit(0, nf - 1, frameA + 1);
    const int i3 = juce::jlimit(0, nf - 1, frameA + 2);

    const auto& f0 = frames[static_cast<size_t>(i0)];
    const auto& f1 = frames[static_cast<size_t>(i1)];
    const auto& f2 = frames[static_cast<size_t>(i2)];
    const auto& f3 = frames[static_cast<size_t>(i3)];

    const float s0 = f0[static_cast<size_t>(idx0)]
                   + (f0[static_cast<size_t>(idx1)] - f0[static_cast<size_t>(idx0)]) * phaseFrac;
    const float s1 = f1[static_cast<size_t>(idx0)]
                   + (f1[static_cast<size_t>(idx1)] - f1[static_cast<size_t>(idx0)]) * phaseFrac;
    const float s2 = f2[static_cast<size_t>(idx0)]
                   + (f2[static_cast<size_t>(idx1)] - f2[static_cast<size_t>(idx0)]) * phaseFrac;
    const float s3 = f3[static_cast<size_t>(idx0)]
                   + (f3[static_cast<size_t>(idx1)] - f3[static_cast<size_t>(idx0)]) * phaseFrac;

    const float t = frameMix;
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * s1)
        + (-s0 + s2) * t
        + (2.0f * s0 - 5.0f * s1 + 4.0f * s2 - s3) * t2
        + (-s0 + 3.0f * s1 - 3.0f * s2 + s3) * t3);
}
