#include "T5ynthInference.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <chrono>

static torch::Device detectBestDevice()
{
    if (torch::cuda::is_available())
        return torch::kCUDA;
#ifdef __APPLE__
    if (torch::mps::is_available())
        return torch::kMPS;
#endif
    return torch::kCPU;
}

T5ynthInference::T5ynthInference()
    : device_(detectBestDevice())
{
}

T5ynthInference::~T5ynthInference()
{
    unloadModels();
}

bool T5ynthInference::loadModels(const juce::File& modelDir)
{
    try
    {
        auto path = [&](const char* name) {
            return modelDir.getChildFile(name).getFullPathName().toStdString();
        };

        // Load SentencePiece tokenizer
        auto status = tokenizer_.Load(path("spiece.model"));
        if (!status.ok())
            throw std::runtime_error("Failed to load tokenizer: " + status.ToString());

        // Load TorchScript models
        t5Encoder_ = torch::jit::load(path("t5_encoder.pt"), device_);
        t5Encoder_.eval();

        projectionModel_ = torch::jit::load(path("projection_model.pt"), device_);
        projectionModel_.eval();

        dit_ = torch::jit::load(path("dit.pt"), device_);
        dit_.eval();

        vaeDecoder_ = torch::jit::load(path("vae_decoder.pt"), device_);
        vaeDecoder_.eval();

        // Load PCA components (saved with torch.save in Python)
        auto pcaPath = path("pca_components.pt");
        if (juce::File(pcaPath).existsAsFile())
        {
            // torch::pickle_load reads Python torch.save tensors
            std::ifstream f(pcaPath, std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
            pcaComponents_ = torch::pickle_load(data).toTensor().to(device_);
        }

        // Load scheduler config
        auto schedConfig = DiffusionScheduler::loadConfig(path("scheduler_config.json"));
        scheduler_ = DiffusionScheduler(schedConfig);

        // Load VAE metadata
        {
            std::ifstream f(path("vae_meta.json"));
            if (f.is_open())
            {
                auto j = nlohmann::json::parse(f);
                vaeScalingFactor_ = j.value("scaling_factor", 1.0f);
                sampleRate_ = j.value("sample_rate", 44100);
                vaeHopLength_ = j.value("vae_hop_length", 2048);
            }
        }

        loaded_ = true;
        return true;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("T5ynthInference::loadModels failed: " + juce::String(e.what()));
        unloadModels();
        return false;
    }
}

void T5ynthInference::unloadModels()
{
    loaded_ = false;
    // Releasing modules frees VRAM/device memory
    t5Encoder_ = torch::jit::Module();
    projectionModel_ = torch::jit::Module();
    dit_ = torch::jit::Module();
    vaeDecoder_ = torch::jit::Module();
    pcaComponents_ = torch::Tensor();
}

// ─── Tokenize ───────────────────────────────────────────

torch::Tensor T5ynthInference::tokenize(const std::string& text)
{
    std::vector<int> ids;
    tokenizer_.Encode(text, &ids);

    // Pad or truncate to kMaxTokenLength
    ids.resize(std::min(static_cast<int>(ids.size()), kMaxTokenLength));
    while (static_cast<int>(ids.size()) < kMaxTokenLength)
        ids.push_back(0);  // pad token = 0

    return torch::tensor(ids, torch::kInt64).unsqueeze(0);  // [1, 128]
}

// ─── T5 Encode ──────────────────────────────────────────

torch::Tensor T5ynthInference::encodeText(const torch::Tensor& tokenIds,
                                          const torch::Tensor& attentionMask)
{
    torch::NoGradGuard noGrad;
    auto input = tokenIds.to(device_);
    auto mask = attentionMask.to(device_);

    auto output = t5Encoder_.forward({input, mask});

    // Output is tuple or dict — extract last_hidden_state
    if (output.isTuple())
        return output.toTuple()->elements()[0].toTensor();
    return output.toGenericDict().at("last_hidden_state").toTensor();
}

// ─── Projection (Duration Conditioning) ─────────────────

torch::Tensor T5ynthInference::projectConditioning(const torch::Tensor& textHidden,
                                                   float secondsStart, float secondsEnd)
{
    torch::NoGradGuard noGrad;
    auto start = torch::tensor({secondsStart}, torch::kFloat32).to(device_);
    auto end = torch::tensor({secondsEnd}, torch::kFloat32).to(device_);

    auto output = projectionModel_.forward({textHidden, start, end});

    // Returns (text_hidden_states, ...) — we need the first element
    if (output.isTuple())
        return output.toTuple()->elements()[0].toTensor();
    return output.toGenericDict().at("text_hidden_states").toTensor();
}

// ─── Embedding Manipulation ─────────────────────────────

torch::Tensor T5ynthInference::manipulateEmbedding(const torch::Tensor& embA,
                                                   const torch::Tensor& embB,
                                                   float alpha, float magnitude,
                                                   float noiseSigma,
                                                   const std::map<int, float>& dimOffsets,
                                                   int seed)
{
    torch::Tensor result;

    if (embB.defined() && embB.numel() > 0)
    {
        // Interpolation / extrapolation
        result = (1.0f - alpha) * embA + alpha * embB;

        // Renormalize if extrapolating (alpha outside [0,1])
        if (alpha < 0.0f || alpha > 1.0f)
        {
            auto midpoint = 0.5f * embA + 0.5f * embB;
            auto refNorm = midpoint.norm();
            auto resultNorm = result.norm();
            if (resultNorm.item<float>() > 1e-8f)
                result = result * (refNorm / resultNorm);
        }
    }
    else
    {
        result = embA.clone();
    }

    // Magnitude scaling
    if (std::abs(magnitude - 1.0f) > 1e-6f)
        result = result * magnitude;

    // Noise injection
    if (noiseSigma > 0.0f)
    {
        torch::manual_seed(seed >= 0 ? seed : static_cast<int64_t>(std::random_device{}()));
        auto noise = torch::randn_like(result) * noiseSigma;
        result = result + noise;
    }

    // Per-dimension offsets
    for (auto& [dim, offset] : dimOffsets)
    {
        if (dim >= 0 && dim < kTextDim)
            result.index({torch::indexing::Slice(), torch::indexing::Slice(), dim}) += offset;
    }

    return result;
}

// ─── Diffusion Loop ─────────────────────────────────────

torch::Tensor T5ynthInference::diffusionLoop(const torch::Tensor& encoderHidden,
                                             const torch::Tensor& globalHidden,
                                             const torch::Tensor& attentionMask,
                                             int steps, float cfgScale, int seed)
{
    torch::NoGradGuard noGrad;
    scheduler_.setTimesteps(steps);

    // Initial noise
    torch::manual_seed(seed);
    auto latent = torch::randn({1, kLatentChannels, kLatentSeqLen},
                               torch::TensorOptions().dtype(torch::kFloat32).device(device_));

    // Scale initial latent by sigma_max
    latent = latent * scheduler_.getSigma(0);

    // For CFG: negative conditioning is zeros
    auto negEncoderHidden = torch::zeros_like(encoderHidden);
    auto negGlobalHidden = torch::zeros_like(globalHidden);

    // Model outputs buffer for 2nd order solver
    torch::Tensor prevModelOutput;
    int lowerOrderNums = 0;

    for (int i = 0; i < steps; ++i)
    {
        // Scale input
        auto scaledLatent = scheduler_.scaleModelInput(latent, i);

        // Timestep
        auto timestep = torch::tensor({scheduler_.getTimestep(i)},
                                      torch::kFloat32).to(device_);

        // DiT forward — conditional
        auto condOutput = dit_.forward({scaledLatent, timestep,
                                        encoderHidden, globalHidden,
                                        attentionMask}).toTensor();

        torch::Tensor modelOutput;
        if (cfgScale > 1.0f)
        {
            // DiT forward — unconditional
            auto uncondOutput = dit_.forward({scaledLatent, timestep,
                                              negEncoderHidden, negGlobalHidden,
                                              attentionMask}).toTensor();

            // CFG: output = uncond + cfg_scale * (cond - uncond)
            modelOutput = uncondOutput + cfgScale * (condOutput - uncondOutput);
        }
        else
        {
            modelOutput = condOutput;
        }

        // Convert v_prediction to denoised
        auto denoised = scheduler_.convertModelOutput(modelOutput, latent, i);

        // Generate step noise
        auto noise = torch::randn_like(latent);

        // Determine solver order for this step
        bool isLast = (i == steps - 1);
        bool useFirstOrder = (i < 1 || lowerOrderNums < 1 || isLast);

        if (useFirstOrder)
        {
            latent = scheduler_.firstOrderUpdate(denoised, latent, i, noise);
        }
        else
        {
            latent = scheduler_.secondOrderUpdate(denoised, prevModelOutput, latent, i, noise);
        }

        prevModelOutput = denoised;
        if (lowerOrderNums < 2)
            ++lowerOrderNums;
    }

    return latent;
}

// ─── VAE Decode ─────────────────────────────────────────

juce::AudioBuffer<float> T5ynthInference::decodeLatent(const torch::Tensor& latent)
{
    torch::NoGradGuard noGrad;

    auto scaled = latent / vaeScalingFactor_;
    auto audio = vaeDecoder_.forward({scaled}).toTensor();

    // audio shape: [1, 2, N] — stereo
    audio = audio.squeeze(0).cpu().contiguous();  // [2, N]

    int numChannels = static_cast<int>(audio.size(0));
    int numSamples = static_cast<int>(audio.size(1));

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto channelData = audio[ch].data_ptr<float>();
        std::memcpy(buffer.getWritePointer(ch), channelData, numSamples * sizeof(float));
    }

    return buffer;
}

// ─── Generate (Full Pipeline) ───────────────────────────

T5ynthInference::Result T5ynthInference::generate(const Request& request)
{
    Result result;
    auto startTime = std::chrono::steady_clock::now();

    try
    {
        if (!loaded_)
            throw std::runtime_error("Models not loaded");

        // Resolve seed
        int seed = request.seed;
        if (seed < 0)
            seed = static_cast<int>(std::random_device{}());
        result.seed = seed;

        // 1. Tokenize prompt(s)
        auto tokenIdsA = tokenize(request.promptA.toStdString());
        auto maskA = (tokenIdsA != 0).to(torch::kInt64);

        // 2. T5 encode
        auto embA = encodeText(tokenIdsA, maskA);

        torch::Tensor embB;
        if (request.promptB.isNotEmpty())
        {
            auto tokenIdsB = tokenize(request.promptB.toStdString());
            auto maskB = (tokenIdsB != 0).to(torch::kInt64);
            embB = encodeText(tokenIdsB, maskB);
        }

        // 3. Embedding manipulation (interpolation, magnitude, noise, dim offsets)
        auto manipulated = manipulateEmbedding(embA, embB, request.alpha, request.magnitude,
                                               request.noiseSigma, request.dimensionOffsets, seed);

        // 4. Duration conditioning
        float duration = request.durationSeconds;
        float startPos = request.startPosition;
        float virtualTotal = (startPos < 1.0f) ? duration / (1.0f - startPos) : duration;
        float secondsStart = startPos * virtualTotal;
        float secondsEnd = secondsStart + duration;

        // Project conditioning (text + duration → encoder_hidden + global_hidden)
        auto projected = projectionModel_.forward({
            manipulated,
            torch::tensor({secondsStart}, torch::kFloat32).to(device_),
            torch::tensor({secondsEnd}, torch::kFloat32).to(device_)
        });

        // Extract projected outputs
        torch::Tensor encoderHidden, globalHidden;
        if (projected.isTuple())
        {
            auto tuple = projected.toTuple();
            encoderHidden = tuple->elements()[0].toTensor();   // [1, 129, 768]
            globalHidden = tuple->elements()[1].toTensor();    // [1, 1536]
        }

        // Attention mask for cross-attention: [1, 129] all true
        auto attentionMask = torch::ones({1, encoderHidden.size(1)},
                                         torch::TensorOptions().dtype(torch::kBool).device(device_));

        // 5. Diffusion loop
        auto latent = diffusionLoop(encoderHidden, globalHidden, attentionMask,
                                    request.steps, request.cfgScale, seed);

        // 6. VAE decode
        result.audio = decodeLatent(latent);
        result.success = true;
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.errorMessage = e.what();
        juce::Logger::writeToLog("T5ynthInference::generate failed: " + juce::String(e.what()));
    }

    auto endTime = std::chrono::steady_clock::now();
    result.generationTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    return result;
}
