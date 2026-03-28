#include "BackendConnection.h"

BackendConnection::BackendConnection()
    : Thread("T5ynth Backend IO")
{
    startThread(juce::Thread::Priority::normal);
}

BackendConnection::~BackendConnection()
{
    signalThreadShouldExit();
    workAvailable.signal();
    waitForThreadToExit(5000);
}

void BackendConnection::setEndpoint(const juce::String& url)
{
    const juce::ScopedLock sl(endpointLock);
    endpointUrl = url;
}

juce::String BackendConnection::getEndpoint() const
{
    const juce::ScopedLock sl(const_cast<juce::CriticalSection&>(endpointLock));
    return endpointUrl;
}

bool BackendConnection::checkHealth()
{
    int statusCode = 0;
    auto response = httpGet("/health", statusCode);

    bool ok = false;
    if (statusCode == 200 && response.isNotEmpty())
    {
        auto json = juce::JSON::parse(response);
        ok = json.getProperty("status", "").toString() == "ok";
    }

    bool wasConnected = connected.exchange(ok);
    if (wasConnected != ok)
    {
        const juce::ScopedLock sl(callbackLock);
        if (connectionCallback)
        {
            auto cb = connectionCallback;
            juce::MessageManager::callAsync([cb, ok]() { cb(ok); });
        }
    }

    return ok;
}

void BackendConnection::requestGeneration(const GenerationRequest& request, GenerationCallback callback)
{
    auto jsonBody = request.toJson();

    enqueueWork([this, jsonBody, callback]()
    {
        int statusCode = 0;
        auto response = httpPost("/api/cross_aesthetic/synth", jsonBody, statusCode);

        GenerationResult result;

        if (statusCode == 200 && response.isNotEmpty())
        {
            auto json = juce::JSON::parse(response);

            if (json.getProperty("success", false))
            {
                auto base64Audio = json.getProperty("audio_base64", "").toString();
                if (decodeBase64Wav(base64Audio, result.audioBuffer, result.sampleRate))
                {
                    result.success = true;
                    result.seed = static_cast<int>(json.getProperty("seed", -1));
                    result.generationTimeMs = static_cast<float>(
                        static_cast<double>(json.getProperty("generation_time_ms", 0)));
                }
                else
                {
                    result.errorMessage = "Failed to decode audio data";
                }
            }
            else
            {
                result.errorMessage = json.getProperty("error", "Unknown backend error").toString();
            }
        }
        else
        {
            result.errorMessage = "HTTP request failed (status " + juce::String(statusCode) + ")";
        }

        if (callback)
            juce::MessageManager::callAsync([callback, result]() { callback(result); });
    });
}

void BackendConnection::fetchSemanticAxes(AxesCallback callback)
{
    enqueueWork([this, callback]()
    {
        int statusCode = 0;
        auto response = httpGet("/api/cross_aesthetic/semantic_axes", statusCode);

        bool success = false;
        juce::var axesData;

        if (statusCode == 200 && response.isNotEmpty())
        {
            auto json = juce::JSON::parse(response);
            if (json.getProperty("success", false))
            {
                axesData = json.getProperty("axes", juce::var());
                success = true;
            }
        }

        if (callback)
            juce::MessageManager::callAsync([callback, success, axesData]() { callback(success, axesData); });
    });
}

void BackendConnection::setConnectionCallback(ConnectionCallback cb)
{
    const juce::ScopedLock sl(callbackLock);
    connectionCallback = std::move(cb);
}

// ---------- Thread ----------

void BackendConnection::run()
{
    while (!threadShouldExit())
    {
        workAvailable.wait(-1);

        if (threadShouldExit())
            break;

        std::function<void()> work;
        {
            const juce::ScopedLock sl(queueLock);
            if (!workQueue.empty())
            {
                work = std::move(workQueue.front());
                workQueue.pop();
            }
        }

        if (work)
            work();
    }
}

void BackendConnection::enqueueWork(std::function<void()> work)
{
    {
        const juce::ScopedLock sl(queueLock);
        workQueue.push(std::move(work));
    }
    workAvailable.signal();
}

// ---------- HTTP helpers ----------

juce::String BackendConnection::httpGet(const juce::String& path, int& statusCode)
{
    auto endpoint = getEndpoint();
    juce::URL url(endpoint + path);

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(120000)
                       .withStatusCode(&statusCode);

    auto stream = url.createInputStream(options);
    if (stream == nullptr)
    {
        statusCode = 0;
        return {};
    }

    return stream->readEntireStreamAsString();
}

juce::String BackendConnection::httpPost(const juce::String& path, const juce::String& jsonBody, int& statusCode)
{
    auto endpoint = getEndpoint();
    juce::URL url = juce::URL(endpoint + path).withPOSTData(jsonBody);

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders("Content-Type: application/json")
                       .withConnectionTimeoutMs(120000)
                       .withStatusCode(&statusCode);

    auto stream = url.createInputStream(options);
    if (stream == nullptr)
    {
        statusCode = 0;
        return {};
    }

    return stream->readEntireStreamAsString();
}

// ---------- Audio decode ----------

bool BackendConnection::decodeBase64Wav(const juce::String& base64,
                                        juce::AudioBuffer<float>& buffer,
                                        double& sampleRate)
{
    if (base64.isEmpty())
        return false;

    // Decode base64 using juce::Base64 (more robust than MemoryBlock::fromBase64Encoding)
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, base64))
    {
        DBG("BackendConnection: Base64 decoding failed, length=" + juce::String(base64.length()));
        return false;
    }

    auto wavData = decoded.getMemoryBlock();
    auto memStream = std::make_unique<juce::MemoryInputStream>(wavData, false);

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(memStream.release(), true));

    if (reader == nullptr)
    {
        DBG("BackendConnection: WAV reader creation failed, data size=" + juce::String((int)wavData.getSize()));
        return false;
    }

    sampleRate = reader->sampleRate;
    auto numSamples = static_cast<int>(reader->lengthInSamples);
    auto numChannels = static_cast<int>(reader->numChannels);

    buffer.setSize(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, numChannels > 1);

    return true;
}
