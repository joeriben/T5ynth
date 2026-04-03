#include "PipeInference.h"
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

PipeInference::~PipeInference()
{
    shutdown();
}

juce::String PipeInference::findPython(const juce::File& backendDir) const
{
    auto projectRoot = backendDir.getParentDirectory();
    for (auto& rel : { ".venv/bin/python", "venv/bin/python",
                       ".venv/bin/python3", "venv/bin/python3" })
    {
        auto py = projectRoot.getChildFile(rel);
        if (py.existsAsFile())
            return py.getFullPathName();
    }
    for (auto& path : { "/usr/bin/python3", "/usr/local/bin/python3" })
    {
        if (juce::File(path).existsAsFile())
            return path;
    }
    return "python3";
}

bool PipeInference::isChildAlive() const
{
#ifdef _WIN32
    return false;
#else
    if (childPid_ <= 0) return false;
    int status = 0;
    pid_t result = waitpid(childPid_, &status, WNOHANG);
    return result == 0;  // 0 = still running
#endif
}

bool PipeInference::tryRestart()
{
    juce::Logger::writeToLog("PipeInference: attempting restart...");
    shutdown();
    if (backendDir_.exists())
        return launch(backendDir_);
    return false;
}

bool PipeInference::launch(const juce::File& backendDir)
{
#ifdef _WIN32
    juce::Logger::writeToLog("PipeInference: not supported on Windows yet");
    return false;
#else
    backendDir_ = backendDir;
    auto script = backendDir.getChildFile("pipe_inference.py");
    if (!script.existsAsFile())
    {
        juce::Logger::writeToLog("PipeInference: pipe_inference.py not found");
        return false;
    }

    auto python = findPython(backendDir);
    juce::Logger::writeToLog("PipeInference: launching " + python + " " + script.getFullPathName());

    // Create bidirectional pipes: parent→child (stdin), child→parent (stdout)
    int pipeIn[2];   // parent writes to pipeIn[1], child reads from pipeIn[0]
    int pipeOut[2];  // child writes to pipeOut[1], parent reads from pipeOut[0]

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        juce::Logger::writeToLog("PipeInference: pipe() failed");
        return false;
    }

    childPid_ = fork();
    if (childPid_ < 0)
    {
        juce::Logger::writeToLog("PipeInference: fork() failed");
        return false;
    }

    if (childPid_ == 0)
    {
        // Child process
        close(pipeIn[1]);   // close write end of stdin pipe
        close(pipeOut[0]);  // close read end of stdout pipe
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[0]);
        close(pipeOut[1]);

        auto pyStr = python.toStdString();
        auto scriptStr = script.getFullPathName().toStdString();
        execlp(pyStr.c_str(), pyStr.c_str(), scriptStr.c_str(), nullptr);
        _exit(1);  // exec failed
    }

    // Parent process
    close(pipeIn[0]);   // close read end of stdin pipe
    close(pipeOut[1]);  // close write end of stdout pipe
    stdinFd_ = pipeIn[1];
    stdoutFd_ = pipeOut[0];

    // Wait for ready signal (\x02 + uint16 len + JSON) with 120s timeout
    // (loading two pipelines can take 30-60s)
    char readyByte = 0;
    if (!readExact(&readyByte, 1, 120000))
    {
        juce::Logger::writeToLog("PipeInference: timeout waiting for ready");
        shutdown();
        return false;
    }

    if (readyByte != '\x02')
    {
        juce::Logger::writeToLog("PipeInference: unexpected ready byte: " + juce::String((int)readyByte));
        shutdown();
        return false;
    }

    // Read device info JSON (uint16 length + JSON bytes)
    uint16_t infoLen = 0;
    if (readExact(&infoLen, 2, 5000) && infoLen > 0)
    {
        std::vector<char> infoBuf(infoLen + 1, 0);
        if (readExact(infoBuf.data(), infoLen, 5000))
        {
            auto infoJson = juce::JSON::parse(juce::String::fromUTF8(infoBuf.data(), infoLen));
            if (auto* arr = infoJson.getProperty("devices", {}).getArray())
            {
                for (auto& d : *arr)
                    availableDevices_.add(d.toString());
            }
            defaultDevice_ = infoJson.getProperty("default", "cpu").toString();

            if (auto* arr = infoJson.getProperty("models", {}).getArray())
            {
                for (auto& m : *arr)
                    availableModels_.add(m.toString());
            }
            defaultModel_ = infoJson.getProperty("default_model", "").toString();

            juce::Logger::writeToLog("PipeInference: devices=" + availableDevices_.joinIntoString(",")
                                      + " default=" + defaultDevice_
                                      + " models=" + availableModels_.joinIntoString(",")
                                      + " default_model=" + defaultModel_);
        }
    }

    ready_ = true;
    juce::Logger::writeToLog("PipeInference: ready");
    return true;
#endif
}

void PipeInference::shutdown()
{
    ready_ = false;
#ifndef _WIN32
    if (stdinFd_ >= 0) { close(stdinFd_); stdinFd_ = -1; }
    if (stdoutFd_ >= 0) { close(stdoutFd_); stdoutFd_ = -1; }
    if (childPid_ > 0)
    {
        kill(childPid_, SIGTERM);
        waitpid(childPid_, nullptr, WNOHANG);
        childPid_ = -1;
    }
#endif
}

bool PipeInference::readExact(void* dest, int numBytes, int timeoutMs)
{
#ifdef _WIN32
    return false;
#else
    auto* ptr = static_cast<char*>(dest);
    int remaining = numBytes;
    auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);

    while (remaining > 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        if (stdoutFd_ < 0)
            return false;

        auto n = read(stdoutFd_, ptr, static_cast<size_t>(remaining));
        if (n > 0)
        {
            ptr += n;
            remaining -= static_cast<int>(n);
        }
        else if (n == 0)
        {
            return false;  // EOF — child died
        }
        else
        {
            if (errno == EINTR) continue;
            juce::Thread::sleep(1);
        }
    }
    return true;
#endif
}

bool PipeInference::writeExact(const void* src, int numBytes)
{
#ifdef _WIN32
    return false;
#else
    if (stdinFd_ < 0) return false;
    auto* ptr = static_cast<const char*>(src);
    int remaining = numBytes;
    while (remaining > 0)
    {
        auto n = write(stdinFd_, ptr, static_cast<size_t>(remaining));
        if (n > 0)
        {
            ptr += n;
            remaining -= static_cast<int>(n);
        }
        else if (n < 0 && errno != EINTR)
        {
            return false;
        }
    }
    return true;
#endif
}

PipeInference::Result PipeInference::generate(const Request& request)
{
    Result result;

    // Check if subprocess is alive, auto-restart if dead
    if (ready_ && !isChildAlive())
    {
        juce::Logger::writeToLog("PipeInference: subprocess died, restarting...");
        if (!tryRestart())
        {
            result.errorMessage = "Inference crashed — restart failed";
            return result;
        }
        juce::Logger::writeToLog("PipeInference: restarted successfully");
    }

    if (!ready_ || stdinFd_ < 0)
    {
        result.errorMessage = "Inference not ready";
        return result;
    }

    // Build JSON request
    auto json = juce::DynamicObject::Ptr(new juce::DynamicObject());
    json->setProperty("prompt_a", request.promptA);
    if (request.promptB.isNotEmpty())
        json->setProperty("prompt_b", request.promptB);
    json->setProperty("alpha", request.alpha);
    json->setProperty("magnitude", request.magnitude);
    json->setProperty("noise_sigma", request.noiseSigma);
    json->setProperty("duration", request.durationSeconds);
    json->setProperty("start_pos", request.startPosition);
    json->setProperty("steps", request.steps);
    json->setProperty("cfg_scale", request.cfgScale);
    json->setProperty("seed", request.seed);
    if (request.device.isNotEmpty())
        json->setProperty("device", request.device);
    if (request.model.isNotEmpty())
        json->setProperty("model", request.model);

    // Serialize dimension offsets from DimensionExplorer
    if (!request.dimensionOffsets.empty())
    {
        juce::Array<juce::var> offsets;
        for (auto& [idx, val] : request.dimensionOffsets)
        {
            juce::Array<juce::var> pair;
            pair.add(idx);
            pair.add(val);
            offsets.add(juce::var(pair));
        }
        json->setProperty("dimension_offsets", juce::var(offsets));
    }

    // Serialize semantic axes
    if (!request.semanticAxes.empty())
    {
        auto axesObj = juce::DynamicObject::Ptr(new juce::DynamicObject());
        for (auto& [key, val] : request.semanticAxes)
            axesObj->setProperty(key, val);
        json->setProperty("semantic_axes", juce::var(axesObj.get()));
    }

    auto jsonStr = juce::JSON::toString(juce::var(json.get()), true);
    jsonStr = jsonStr.removeCharacters("\n\r") + "\n";

    // Write request
    if (!writeExact(jsonStr.toRawUTF8(), static_cast<int>(jsonStr.getNumBytesAsUTF8())))
    {
        // Write failed — subprocess likely dead
        if (tryRestart())
        {
            result.errorMessage = "Inference restarted — try again";
        }
        else
            result.errorMessage = "Inference crashed — restart failed";
        return result;
    }

    // Read response status byte (3 min timeout for dual-pipeline startup)
    char status = 0;
    if (!readExact(&status, 1, 180000))
    {
        // Timeout or dead process
        if (!isChildAlive())
        {
            juce::Logger::writeToLog("PipeInference: subprocess died during generation");
            tryRestart();
            result.errorMessage = "Inference crashed — restarted, try again";
        }
        else
            result.errorMessage = "Timeout waiting for response";
        return result;
    }

    if (status == '\x00')
    {
        juce::uint32 msgLen = 0;
        if (readExact(&msgLen, 4))
        {
            std::vector<char> msg(msgLen + 1, 0);
            readExact(msg.data(), static_cast<int>(msgLen));
            result.errorMessage = juce::String::fromUTF8(msg.data(), static_cast<int>(msgLen));
        }
        else
            result.errorMessage = "Unknown error";
        return result;
    }

    if (status != '\x01')
    {
        result.errorMessage = "Unexpected response: " + juce::String((int)status);
        return result;
    }

    // Read header: flag(i32), samples(i32), channels(i32), sampleRate(i32), seed(i32), timeMs(f32)
    struct { int32_t flag, samples, channels, sampleRate, seed; float timeMs; } header;
    if (!readExact(&header, sizeof(header)))
    {
        result.errorMessage = "Failed to read header";
        return result;
    }

    int totalFloats = header.samples * header.channels;
    std::vector<float> pcm(static_cast<size_t>(totalFloats));
    if (!readExact(pcm.data(), totalFloats * static_cast<int>(sizeof(float))))
    {
        result.errorMessage = "Failed to read PCM data (" + juce::String(totalFloats * 4) + " bytes)";
        return result;
    }

    // Build AudioBuffer [channels, samples]
    result.audio.setSize(header.channels, header.samples);
    for (int ch = 0; ch < header.channels; ++ch)
    {
        auto* dest = result.audio.getWritePointer(ch);
        auto* src = pcm.data() + static_cast<size_t>(ch) * static_cast<size_t>(header.samples);
        std::memcpy(dest, src, static_cast<size_t>(header.samples) * sizeof(float));
    }

    result.success = true;
    result.seed = header.seed;
    result.generationTimeMs = header.timeMs;

    // Read embedding stats (uint16 num_dims + float32[dims] A + float32[dims] B)
    uint16_t numDims = 0;
    if (readExact(&numDims, 2, 5000) && numDims > 0)
    {
        result.embeddingA.resize(numDims);
        result.embeddingB.resize(numDims);
        readExact(result.embeddingA.data(), numDims * static_cast<int>(sizeof(float)), 5000);
        readExact(result.embeddingB.data(), numDims * static_cast<int>(sizeof(float)), 5000);
    }

    return result;
}

bool PipeInference::preload(const juce::String& model, const juce::String& device)
{
    if (!ready_ || stdinFd_ < 0) return false;

    auto json = juce::DynamicObject::Ptr(new juce::DynamicObject());
    json->setProperty("mode", "preload");
    json->setProperty("model", model);
    if (device.isNotEmpty())
        json->setProperty("device", device);

    auto jsonStr = juce::JSON::toString(juce::var(json.get()), true);
    jsonStr = jsonStr.removeCharacters("\n\r") + "\n";

    if (!writeExact(jsonStr.toRawUTF8(), static_cast<int>(jsonStr.getNumBytesAsUTF8())))
        return false;

    // Read and discard the preload response (empty audio)
    char status = 0;
    if (!readExact(&status, 1, 120000)) return false;

    if (status == '\x01')
    {
        // Read and discard header + 0-sample PCM + embedding footer
        struct { int32_t flag, samples, channels, sampleRate, seed; float timeMs; } header;
        if (!readExact(&header, sizeof(header))) return false;
        int totalFloats = header.samples * header.channels;
        if (totalFloats > 0)
        {
            std::vector<float> discard(static_cast<size_t>(totalFloats));
            readExact(discard.data(), totalFloats * static_cast<int>(sizeof(float)));
        }
        uint16_t numDims = 0;
        readExact(&numDims, 2, 5000);
        if (numDims > 0)
        {
            std::vector<float> discard(numDims * 2);
            readExact(discard.data(), numDims * 2 * static_cast<int>(sizeof(float)), 5000);
        }
        return true;
    }
    else if (status == '\x00')
    {
        // Error — read and discard error message
        juce::uint32 msgLen = 0;
        if (readExact(&msgLen, 4))
        {
            std::vector<char> msg(msgLen + 1, 0);
            readExact(msg.data(), static_cast<int>(msgLen));
        }
    }
    return false;
}
