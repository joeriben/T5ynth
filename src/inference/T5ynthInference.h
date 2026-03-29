#pragma once
#include <JuceHeader.h>
#include <torch/torch.h>
#include <torch/script.h>
#include <sentencepiece_processor.h>
#include "DiffusionScheduler.h"
#include <map>
#include <string>

/**
 * Native C++ inference pipeline for Stable Audio Open 1.0.
 *
 * Replaces the Python HTTP backend entirely.
 * Pipeline: Tokenize → T5 Encode → Embed Manipulation → Diffusion Loop → VAE Decode → Audio
 *
 * All models are loaded from TorchScript .pt files exported via tools/export_to_torchscript.py.
 * Thread-safe: generate() is blocking and should be called from a background thread.
 */
class T5ynthInference
{
public:
    T5ynthInference();
    ~T5ynthInference();

    /** Load all models from a directory containing the exported .pt files. */
    bool loadModels(const juce::File& modelDir);

    /** Unload all models and free VRAM. */
    void unloadModels();

    bool isLoaded() const { return loaded_.load(); }

    struct Request
    {
        juce::String promptA;
        juce::String promptB;         // empty = single prompt
        float alpha = 0.5f;
        float magnitude = 1.0f;
        float noiseSigma = 0.0f;
        float durationSeconds = 1.0f;
        float startPosition = 0.0f;
        int steps = 20;
        float cfgScale = 7.0f;
        int seed = -1;                // -1 = random
        std::map<juce::String, float> axes;
        std::map<int, float> dimensionOffsets;
    };

    struct Result
    {
        bool success = false;
        juce::AudioBuffer<float> audio;
        float generationTimeMs = 0.0f;
        int seed = -1;
        juce::String errorMessage;
    };

    /** Blocking generation — call from background thread. */
    Result generate(const Request& request);

private:
    // Pipeline stages
    torch::Tensor tokenize(const std::string& text);
    torch::Tensor encodeText(const torch::Tensor& tokenIds, const torch::Tensor& attentionMask);
    torch::Tensor projectConditioning(const torch::Tensor& textHidden,
                                      float secondsStart, float secondsEnd);
    torch::Tensor manipulateEmbedding(const torch::Tensor& embA,
                                      const torch::Tensor& embB,
                                      float alpha, float magnitude, float noiseSigma,
                                      const std::map<int, float>& dimOffsets,
                                      int seed);
    torch::Tensor diffusionLoop(const torch::Tensor& encoderHidden,
                                const torch::Tensor& globalHidden,
                                const torch::Tensor& attentionMask,
                                int steps, float cfgScale, int seed);
    juce::AudioBuffer<float> decodeLatent(const torch::Tensor& latent);

    // Models
    torch::jit::Module t5Encoder_;
    torch::jit::Module projectionModel_;
    torch::jit::Module dit_;
    torch::jit::Module vaeDecoder_;
    sentencepiece::SentencePieceProcessor tokenizer_;

    // Data
    torch::Tensor pcaComponents_;       // [N, 768]
    DiffusionScheduler scheduler_;

    // State
    std::atomic<bool> loaded_ { false };
    torch::Device device_ { torch::kCPU };
    int sampleRate_ = 44100;
    int vaeHopLength_ = 2048;
    float vaeScalingFactor_ = 1.0f;

    static constexpr int kMaxTokenLength = 128;
    static constexpr int kLatentChannels = 64;
    static constexpr int kLatentSeqLen = 1024;  // ~47s at 44100/2048
    static constexpr int kTextDim = 768;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthInference)
};
