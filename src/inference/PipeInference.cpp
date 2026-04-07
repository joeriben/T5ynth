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

juce::File PipeInference::findBundledBinary(const juce::File& backendDir) const
{
    // PyInstaller-bundled binary: backendDir/dist/pipe_inference/pipe_inference
    auto dist = backendDir.getChildFile("dist/pipe_inference/pipe_inference");
    if (dist.existsAsFile()) return dist;

    // Installed layout: binary next to backendDir
    // macOS app bundle: Contents/Resources/backend/pipe_inference
    // Linux/Windows:    backend/pipe_inference(.exe)
    auto sibling = backendDir.getChildFile("pipe_inference");
    if (sibling.existsAsFile()) return sibling;

   #ifdef _WIN32
    auto siblingExe = backendDir.getChildFile("pipe_inference.exe");
    if (siblingExe.existsAsFile()) return siblingExe;
   #endif

    return {};  // not found — fall back to Python
}

juce::String PipeInference::findPython(const juce::File& backendDir) const
{
    auto projectRoot = backendDir.getParentDirectory();

   #ifdef _WIN32
    for (auto& rel : { ".venv/Scripts/python.exe", "venv/Scripts/python.exe",
                       ".venv/Scripts/python3.exe", "venv/Scripts/python3.exe" })
   #else
    for (auto& rel : { ".venv/bin/python", "venv/bin/python",
                       ".venv/bin/python3", "venv/bin/python3" })
   #endif
    {
        auto py = projectRoot.getChildFile(rel);
        if (py.existsAsFile())
            return py.getFullPathName();
    }
   #ifndef _WIN32
    for (auto& path : { "/usr/bin/python3", "/usr/local/bin/python3" })
    {
        if (juce::File(path).existsAsFile())
            return path;
    }
   #endif
    return "python3";
}

bool PipeInference::isConnected() const
{
   #ifdef _WIN32
    return hChildStdinWr_ != INVALID_HANDLE_VALUE;
   #else
    return stdinFd_ >= 0;
   #endif
}

bool PipeInference::isChildAlive() const
{
#ifdef _WIN32
    if (hProcess_ == INVALID_HANDLE_VALUE) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(hProcess_, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
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
    // Atomic guard: only one launch() may run at a time
    bool expected = false;
    if (!launching_.compare_exchange_strong(expected, true))
    {
        juce::Logger::writeToLog("PipeInference: launch already in progress, skipping");
        return ready_.load();
    }

    // If subprocess is already running, nothing to do
    if (ready_.load() && isChildAlive())
    {
        juce::Logger::writeToLog("PipeInference: already running, skipping launch");
        launching_ = false;
        return true;
    }
    // Kill any orphaned subprocess before starting a new one
    shutdown();

    backendDir_ = backendDir;

    // Resolve executable: bundled binary (PyInstaller) or Python + script
    auto bundled = findBundledBinary(backendDir);
    juce::String execPath;
    juce::String scriptPath;  // empty when using bundled binary

    if (bundled.existsAsFile())
    {
        execPath = bundled.getFullPathName();
        juce::Logger::writeToLog("PipeInference: launching bundled " + execPath);
    }
    else
    {
        auto script = backendDir.getChildFile("pipe_inference.py");
        if (!script.existsAsFile())
        {
            juce::Logger::writeToLog("PipeInference: pipe_inference.py not found");
            return false;
        }
        execPath = findPython(backendDir);
        scriptPath = script.getFullPathName();
        juce::Logger::writeToLog("PipeInference: launching " + execPath + " " + scriptPath);
    }

#ifdef _WIN32
    // ── Windows: CreatePipe + CreateProcess ──────────────────────────
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hChildStdinRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdinWr = INVALID_HANDLE_VALUE;
    HANDLE hChildStdoutRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdoutWr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0)
        || !CreatePipe(&hChildStdinRd, &hChildStdinWr, &sa, 0))
    {
        juce::Logger::writeToLog("PipeInference: CreatePipe failed");
        return false;
    }

    // Parent-side handles must NOT be inherited
    SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};

    // Build command line
    juce::String cmdLine;
    if (scriptPath.isEmpty())
        cmdLine = "\"" + execPath + "\"";
    else
        cmdLine = "\"" + execPath + "\" \"" + scriptPath + "\"";

    auto cmdWide = cmdLine.toWideCharPointer();
    std::wstring cmdBuf(cmdWide);

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        juce::Logger::writeToLog("PipeInference: CreateProcess failed (error "
                                  + juce::String((int)GetLastError()) + ")");
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        CloseHandle(hChildStdoutRd);
        CloseHandle(hChildStdoutWr);
        return false;
    }

    // Close child-side handles (now owned by child process)
    CloseHandle(hChildStdinRd);
    CloseHandle(hChildStdoutWr);
    CloseHandle(pi.hThread);

    hChildStdinWr_ = hChildStdinWr;
    hChildStdoutRd_ = hChildStdoutRd;
    hProcess_ = pi.hProcess;

#else
    // ── POSIX: pipe + fork + exec ───────────────────────────────────
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
        close(pipeIn[1]);
        close(pipeOut[0]);
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[0]);
        close(pipeOut[1]);

        auto execStr = execPath.toStdString();
        if (scriptPath.isEmpty())
            execlp(execStr.c_str(), execStr.c_str(), nullptr);
        else
        {
            auto scriptStr = scriptPath.toStdString();
            execlp(execStr.c_str(), execStr.c_str(), scriptStr.c_str(), nullptr);
        }
        _exit(1);
    }

    // Parent process
    close(pipeIn[0]);
    close(pipeOut[1]);
    stdinFd_ = pipeIn[1];
    stdoutFd_ = pipeOut[0];
#endif

    // Wait for ready signal (\x02 + uint16 len + JSON) with 120s timeout
    // (loading two pipelines can take 30-60s)
    char readyByte = 0;
    if (!readExact(&readyByte, 1, 120000))
    {
        juce::Logger::writeToLog("PipeInference: timeout waiting for ready");
        shutdown();
        launching_ = false;
        return false;
    }

    if (readyByte != '\x02')
    {
        juce::Logger::writeToLog("PipeInference: unexpected ready byte: " + juce::String((int)readyByte));
        shutdown();
        launching_ = false;
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
    launching_ = false;
    juce::Logger::writeToLog("PipeInference: ready");
    return true;
}

void PipeInference::shutdown()
{
    ready_ = false;
#ifdef _WIN32
    if (hChildStdinWr_ != INVALID_HANDLE_VALUE) { CloseHandle(hChildStdinWr_); hChildStdinWr_ = INVALID_HANDLE_VALUE; }
    if (hChildStdoutRd_ != INVALID_HANDLE_VALUE) { CloseHandle(hChildStdoutRd_); hChildStdoutRd_ = INVALID_HANDLE_VALUE; }
    if (hProcess_ != INVALID_HANDLE_VALUE)
    {
        TerminateProcess(hProcess_, 0);
        WaitForSingleObject(hProcess_, 3000);
        CloseHandle(hProcess_);
        hProcess_ = INVALID_HANDLE_VALUE;
    }
#else
    if (stdinFd_ >= 0) { close(stdinFd_); stdinFd_ = -1; }
    if (stdoutFd_ >= 0) { close(stdoutFd_); stdoutFd_ = -1; }
    if (childPid_ > 0)
    {
        kill(childPid_, SIGTERM);
        // Wait up to 3s for graceful exit, then force-kill
        for (int i = 0; i < 30; ++i)
        {
            if (waitpid(childPid_, nullptr, WNOHANG) != 0)
                break;
            juce::Thread::sleep(100);
        }
        kill(childPid_, SIGKILL);
        waitpid(childPid_, nullptr, 0);
        childPid_ = -1;
    }
#endif
}

bool PipeInference::readExact(void* dest, int numBytes, int timeoutMs)
{
    auto* ptr = static_cast<char*>(dest);
    int remaining = numBytes;
    auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);

    while (remaining > 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;

       #ifdef _WIN32
        if (hChildStdoutRd_ == INVALID_HANDLE_VALUE) return false;
        DWORD bytesRead = 0;
        if (!ReadFile(hChildStdoutRd_, ptr, static_cast<DWORD>(remaining), &bytesRead, nullptr))
            return false;
        if (bytesRead == 0) return false;  // pipe broken
        ptr += bytesRead;
        remaining -= static_cast<int>(bytesRead);
       #else
        if (stdoutFd_ < 0) return false;
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
       #endif
    }
    return true;
}

bool PipeInference::writeExact(const void* src, int numBytes)
{
    if (!isConnected()) return false;
    auto* ptr = static_cast<const char*>(src);
    int remaining = numBytes;
    while (remaining > 0)
    {
       #ifdef _WIN32
        DWORD bytesWritten = 0;
        if (!WriteFile(hChildStdinWr_, ptr, static_cast<DWORD>(remaining), &bytesWritten, nullptr))
            return false;
        ptr += bytesWritten;
        remaining -= static_cast<int>(bytesWritten);
       #else
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
       #endif
    }
    return true;
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

    if (!ready_ || !isConnected())
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
    if (!ready_ || !isConnected()) return false;

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
