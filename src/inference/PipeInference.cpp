#include "PipeInference.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <array>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/resource.h>
#endif

namespace
{
int readEnvInt(const char* name, int fallback, int minValue, int maxValue)
{
    if (auto* raw = std::getenv(name))
    {
        char* end = nullptr;
        const auto value = std::strtol(raw, &end, 10);
        if (end != raw)
            return juce::jlimit(minValue, maxValue, static_cast<int>(value));
    }

    return fallback;
}

void setEnvValue(const char* name, const juce::String& value)
{
    const auto valueStd = value.toStdString();
   #ifdef _WIN32
    _putenv_s(name, valueStd.c_str());
   #else
    setenv(name, valueStd.c_str(), 1);
   #endif
}

void setEnvIfUnset(const char* name, const juce::String& value)
{
    if (auto* existing = std::getenv(name); existing != nullptr && existing[0] != '\0')
        return;

    setEnvValue(name, value);
}

void capThreadEnv(const char* name, int limit)
{
    if (auto* raw = std::getenv(name))
    {
        char* end = nullptr;
        const auto current = std::strtol(raw, &end, 10);
        if (end != raw && current >= 1 && current <= limit)
            return;
    }

    setEnvValue(name, juce::String(limit));
}

void configureInferenceCpuBudget()
{
    const auto workerThreads = readEnvInt("T5YNTH_INFERENCE_CPU_THREADS", 2, 1, 16);
    const auto interopThreads = readEnvInt("T5YNTH_INFERENCE_INTEROP_THREADS", 1, 1, 16);

    for (auto* key : { "OMP_NUM_THREADS",
                       "MKL_NUM_THREADS",
                       "OPENBLAS_NUM_THREADS",
                       "NUMEXPR_NUM_THREADS",
                       "VECLIB_MAXIMUM_THREADS",
                       "BLIS_NUM_THREADS",
                       "TORCH_NUM_THREADS" })
    {
        capThreadEnv(key, workerThreads);
    }

    capThreadEnv("TORCH_NUM_INTEROP_THREADS", interopThreads);
    setEnvIfUnset("OMP_WAIT_POLICY", "PASSIVE");
    setEnvIfUnset("KMP_BLOCKTIME", "0");
    setEnvIfUnset("MKL_DYNAMIC", "TRUE");
    setEnvIfUnset("PYTHONUNBUFFERED", "1");
}

#ifdef _WIN32
DWORD inferencePriorityClass()
{
    auto* rawEnv = std::getenv("T5YNTH_INFERENCE_PRIORITY");
    if (rawEnv == nullptr)
        return BELOW_NORMAL_PRIORITY_CLASS;

    const juce::String raw { rawEnv };
    const auto value = raw.trim().toLowerCase();

    if (value == "normal")
        return 0;
    if (value == "idle")
        return IDLE_PRIORITY_CLASS;

    return BELOW_NORMAL_PRIORITY_CLASS;
}
#else
void lowerCurrentProcessPriority()
{
    const auto niceValue = readEnvInt("T5YNTH_INFERENCE_NICE", 10, 0, 19);
    if (niceValue > 0)
        setpriority(PRIO_PROCESS, 0, niceValue);
}
#endif
}

PipeInference::~PipeInference()
{
    shutdown();
}

juce::File PipeInference::findBundledBinary(const juce::File& backendDir) const
{
    // PyInstaller-bundled binary:
    //   POSIX   backendDir/dist/pipe_inference/pipe_inference
    //   Windows backendDir/dist/pipe_inference/pipe_inference.exe
    auto dist = backendDir.getChildFile("dist/pipe_inference/pipe_inference");
    if (isCompatibleBundledBinary(dist)) return dist;

   #ifdef _WIN32
    auto distExe = backendDir.getChildFile("dist/pipe_inference/pipe_inference.exe");
    if (isCompatibleBundledBinary(distExe)) return distExe;

    // Installed layout: binary next to backendDir
    // Windows: backend/pipe_inference.exe
    auto siblingExe = backendDir.getChildFile("pipe_inference.exe");
    if (isCompatibleBundledBinary(siblingExe)) return siblingExe;
   #else
    // Installed layout: binary next to backendDir
    // macOS app bundle: Contents/Resources/backend/pipe_inference
    // Linux:            backend/pipe_inference
    auto sibling = backendDir.getChildFile("pipe_inference");
    if (isCompatibleBundledBinary(sibling)) return sibling;
   #endif

    return {};  // not found — fall back to Python
}

bool PipeInference::isCompatibleBundledBinary(const juce::File& binary) const
{
    if (!binary.existsAsFile())
        return false;

    auto stream = binary.createInputStream();
    if (stream == nullptr)
    {
        juce::Logger::writeToLog("PipeInference: could not inspect bundled backend " + binary.getFullPathName());
        return false;
    }

    std::array<unsigned char, 4> magic { 0, 0, 0, 0 };
    if (stream->read(magic.data(), static_cast<int>(magic.size())) != static_cast<int>(magic.size()))
        return false;

   #ifdef _WIN32
    const bool compatible = magic[0] == 'M' && magic[1] == 'Z';
   #elif JUCE_MAC
    const bool compatible =
        (magic[0] == 0xfe && magic[1] == 0xed && magic[2] == 0xfa && magic[3] == 0xce) ||
        (magic[0] == 0xce && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
        (magic[0] == 0xfe && magic[1] == 0xed && magic[2] == 0xfa && magic[3] == 0xcf) ||
        (magic[0] == 0xcf && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
        (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbe);
   #else
    const bool compatible = magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
   #endif

    if (!compatible)
    {
        juce::Logger::writeToLog("PipeInference: ignoring incompatible bundled backend "
                                  + binary.getFullPathName());
    }

    return compatible;
}

void PipeInference::prepareBundledBinary(const juce::File& backendDir, const juce::File& binary) const
{
   #ifndef _WIN32
    if (!binary.setExecutePermission(true))
    {
        juce::Logger::writeToLog("PipeInference: could not ensure execute bit on "
                                  + binary.getFullPathName());
    }
   #endif

   #if JUCE_MAC
    if (backendDir.getFullPathName().containsIgnoreCase(".app/Contents/Resources/backend"))
    {
        juce::ChildProcess proc;
        const juce::StringArray args { "xattr", "-dr", "com.apple.quarantine",
                                       backendDir.getFullPathName() };

        if (!proc.start(args, juce::ChildProcess::wantStdErr))
        {
            juce::Logger::writeToLog("PipeInference: could not start xattr cleanup for "
                                      + backendDir.getFullPathName());
            return;
        }

        proc.waitForProcessToFinish(5000);
        const auto output = proc.readAllProcessOutput().trim();

        if (proc.getExitCode() != 0 && output.isNotEmpty())
        {
            juce::Logger::writeToLog("PipeInference: xattr cleanup failed: " + output);
        }
    }
   #endif
}

juce::String PipeInference::maybeAugmentMacStandaloneError(const juce::File& backendDir,
                                                           const juce::String& detail) const
{
   #if JUCE_MAC
    const bool isBundledStandalone
        = backendDir.getFullPathName().containsIgnoreCase(".app/Contents/Resources/backend");

    if (isBundledStandalone
        && (detail.containsIgnoreCase("exec failed")
            || detail.containsIgnoreCase("permission denied")
            || detail.containsIgnoreCase("operation not permitted")
            || detail.containsIgnoreCase("code signature")
            || detail.containsIgnoreCase("library not loaded")))
    {
        return detail
             + "\n\nmacOS appears to be blocking the app bundle or one of its bundled "
               "libraries. The .pkg installer avoids this path and removes quarantine "
               "attributes after install.";
    }
   #endif

    return detail;
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
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
   #ifdef _WIN32
    return hChildStdinWr_ != INVALID_HANDLE_VALUE;
   #else
    return stdinFd_ >= 0;
   #endif
}

bool PipeInference::isChildAlive() const
{
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
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
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    juce::Logger::writeToLog("PipeInference: attempting restart...");
    shutdown();
    if (backendDir_.exists())
        return launch(backendDir_);
    return false;
}

bool PipeInference::launch(const juce::File& backendDir)
{
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
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
        prepareBundledBinary(backendDir, bundled);
        execPath = bundled.getFullPathName();
        juce::Logger::writeToLog("PipeInference: launching bundled " + execPath);
    }
    else
    {
        auto script = backendDir.getChildFile("pipe_inference.py");
        if (!script.existsAsFile())
        {
            const auto dist = backendDir.getChildFile("dist/pipe_inference/pipe_inference");
           #ifdef _WIN32
            const auto distExe = backendDir.getChildFile("dist/pipe_inference/pipe_inference.exe");
            const auto siblingExe = backendDir.getChildFile("pipe_inference.exe");
           #else
            const auto sibling = backendDir.getChildFile("pipe_inference");
           #endif

            if (dist.existsAsFile()
               #ifdef _WIN32
                || distExe.existsAsFile()
                || siblingExe.existsAsFile()
               #else
                || sibling.existsAsFile()
               #endif
            )
            {
                lastError_ = "Bundled backend has incompatible binary format for this platform";
            }
            else
            {
                lastError_ = "Backend not found in " + backendDir.getFullPathName();
            }
            juce::Logger::writeToLog("PipeInference: " + lastError_);
            launching_ = false;
            return false;
        }
        execPath = findPython(backendDir);
        scriptPath = script.getFullPathName();
        juce::Logger::writeToLog("PipeInference: launching " + execPath + " " + scriptPath);
    }

    configureInferenceCpuBudget();
    auto stderrLog = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("T5ynth/Logs/backend_stderr.log");

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
    HANDLE hChildStderrWr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0)
        || !CreatePipe(&hChildStdinRd, &hChildStdinWr, &sa, 0))
    {
        lastError_ = "CreatePipe failed (error " + juce::String((int)GetLastError()) + ")";
        juce::Logger::writeToLog("PipeInference: " + lastError_);
        if (hChildStdinRd != INVALID_HANDLE_VALUE) CloseHandle(hChildStdinRd);
        if (hChildStdinWr != INVALID_HANDLE_VALUE) CloseHandle(hChildStdinWr);
        if (hChildStdoutRd != INVALID_HANDLE_VALUE) CloseHandle(hChildStdoutRd);
        if (hChildStdoutWr != INVALID_HANDLE_VALUE) CloseHandle(hChildStdoutWr);
        launching_ = false;
        return false;
    }

    // Parent-side handles must NOT be inherited
    SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0);

    stderrLog.getParentDirectory().createDirectory();
    std::wstring stderrPath(stderrLog.getFullPathName().toWideCharPointer());
    hChildStderrWr = CreateFileW(stderrPath.c_str(),
                                 GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 &sa,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    if (hChildStderrWr == INVALID_HANDLE_VALUE)
    {
        juce::Logger::writeToLog("PipeInference: could not open backend stderr log "
                                  + stderrLog.getFullPathName()
                                  + " (error " + juce::String((int)GetLastError()) + ")");
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = hChildStderrWr != INVALID_HANDLE_VALUE ? hChildStderrWr
                                                          : GetStdHandle(STD_ERROR_HANDLE);
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

    const DWORD creationFlags = CREATE_NO_WINDOW | inferencePriorityClass();

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                        creationFlags, nullptr, nullptr, &si, &pi))
    {
        lastError_ = "CreateProcess failed (error " + juce::String((int)GetLastError()) + ")";
        if (hChildStderrWr != INVALID_HANDLE_VALUE)
            lastError_ << "\nLog: " << stderrLog.getFullPathName();
        juce::Logger::writeToLog("PipeInference: " + lastError_);
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        CloseHandle(hChildStdoutRd);
        CloseHandle(hChildStdoutWr);
        if (hChildStderrWr != INVALID_HANDLE_VALUE) CloseHandle(hChildStderrWr);
        launching_ = false;
        return false;
    }

    // Close child-side handles (now owned by child process)
    CloseHandle(hChildStdinRd);
    CloseHandle(hChildStdoutWr);
    if (hChildStderrWr != INVALID_HANDLE_VALUE) CloseHandle(hChildStderrWr);
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

        // Redirect stderr to log file for post-mortem diagnosis
        auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("T5ynth/Logs");
        logDir.createDirectory();
        auto logPath = logDir.getChildFile("backend_stderr.log").getFullPathName().toStdString();
        int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logFd >= 0) { dup2(logFd, STDERR_FILENO); close(logFd); }

        lowerCurrentProcessPriority();

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

        auto message = std::string("PipeInference: exec failed for ")
                     + execStr + ": " + std::strerror(errno) + "\n";
        ::write(STDERR_FILENO, message.c_str(), message.size());
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
        // Read stderr log for diagnosis
        juce::String stderrContent;
        if (stderrLog.existsAsFile())
            stderrContent = stderrLog.loadFileAsString().trimEnd();

        if (stderrContent.isNotEmpty())
        {
            lastError_ = maybeAugmentMacStandaloneError(
                backendDir,
                stderrContent.fromLastOccurrenceOf("\n", false, false).trim());
            lastError_ << "\nLog: " << stderrLog.getFullPathName();
        }
        else if (!isChildAlive())
            lastError_ = "Backend process crashed on startup";
        else
            lastError_ = "Timeout waiting for backend (120s)";

        if (!lastError_.contains("Log: "))
            lastError_ << "\nLog: " << stderrLog.getFullPathName();

        juce::Logger::writeToLog("PipeInference: " + lastError_);
        shutdown();
        launching_ = false;
        return false;
    }

    if (readyByte == '\x00')
    {
        // Backend sent an error message: \x00 + uint32 length + UTF-8 message
        uint32_t errLen = 0;
        if (readExact(&errLen, 4, 5000) && errLen > 0 && errLen < 100000)
        {
            std::vector<char> errBuf(errLen + 1, 0);
            if (readExact(errBuf.data(), errLen, 5000))
                lastError_ = juce::String::fromUTF8(errBuf.data(), static_cast<int>(errLen));
            else
                lastError_ = "Backend error (could not read message)";
        }
        else
            lastError_ = "Backend error (no message)";

        juce::Logger::writeToLog("PipeInference: " + lastError_);
        shutdown();
        launching_ = false;
        return false;
    }

    if (readyByte != '\x02')
    {
        lastError_ = "Backend protocol error (unexpected byte: " + juce::String((int)readyByte) + ")";
        juce::Logger::writeToLog("PipeInference: " + lastError_);
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
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
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
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
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

    // Research-mode prompt-injection fields. Always serialised so that the bundled
    // backend sees them; older backends ignore unknown fields.
    json->setProperty("injection_mode", request.injectionMode);
    json->setProperty("injection_transition_at", request.injectionTransitionAt);
    json->setProperty("late_phase_alpha", request.latePhaseAlpha);
    json->setProperty("split_start", request.splitStart);
    json->setProperty("split_end",   request.splitEnd);

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

    // Read response status byte (3 min timeout for long first-request warm loads)
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

    // Read embedding stats (uint16 num_dims + float32[dims] A/B/baseline)
    uint16_t numDims = 0;
    if (readExact(&numDims, 2, 5000) && numDims > 0)
    {
        result.embeddingA.resize(numDims);
        result.embeddingB.resize(numDims);
        result.embeddingBaseline.resize(numDims);
        readExact(result.embeddingA.data(), numDims * static_cast<int>(sizeof(float)), 5000);
        readExact(result.embeddingB.data(), numDims * static_cast<int>(sizeof(float)), 5000);
        readExact(result.embeddingBaseline.data(), numDims * static_cast<int>(sizeof(float)), 5000);
    }

    return result;
}

bool PipeInference::preload(const juce::String& model, const juce::String& device)
{
    const std::lock_guard<std::recursive_mutex> lock(stateMutex_);
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
            std::vector<float> discard(numDims * 3);
            readExact(discard.data(), numDims * 3 * static_cast<int>(sizeof(float)), 5000);
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
