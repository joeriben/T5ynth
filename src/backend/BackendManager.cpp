#include "BackendManager.h"

BackendManager::BackendManager() = default;

BackendManager::~BackendManager()
{
    stop();
}

void BackendManager::setStatusCallback(StatusCallback cb)
{
    statusCallback = std::move(cb);
}

void BackendManager::start()
{
    if (status == Status::Running || status == Status::Starting)
        return;

    status = Status::Starting;
    notifyStatus();

    auto backendDir = findBackendDirectory();
    auto pythonExe = findPythonExecutable();

    if (!backendDir.isDirectory())
    {
        DBG("BackendManager: backend directory not found");
        status = Status::Failed;
        notifyStatus();
        return;
    }

    auto serverPy = backendDir.getChildFile("server.py");
    if (!serverPy.existsAsFile())
    {
        DBG("BackendManager: server.py not found in " + backendDir.getFullPathName());
        status = Status::Failed;
        notifyStatus();
        return;
    }

    if (!pythonExe.existsAsFile())
    {
        DBG("BackendManager: Python executable not found");
        status = Status::Failed;
        notifyStatus();
        return;
    }

    DBG("BackendManager: starting " + pythonExe.getFullPathName()
        + " " + serverPy.getFullPathName());

    childProcess = std::make_unique<juce::ChildProcess>();

    juce::StringArray args;
    args.add(pythonExe.getFullPathName());
    args.add(serverPy.getFullPathName());

    if (!childProcess->start(args))
    {
        DBG("BackendManager: failed to start child process");
        childProcess.reset();
        status = Status::Failed;
        notifyStatus();
        return;
    }

    healthCheckAttempts = 0;
    startTimer(healthCheckIntervalMs);
}

void BackendManager::stop()
{
    stopTimer();

    if (childProcess != nullptr)
    {
        if (childProcess->isRunning())
        {
            DBG("BackendManager: killing backend process");
            childProcess->kill();
        }
        childProcess.reset();
    }

    status = Status::Stopped;
    notifyStatus();
}

void BackendManager::timerCallback()
{
    if (childProcess == nullptr || !childProcess->isRunning())
    {
        DBG("BackendManager: backend process died during startup");
        stopTimer();
        childProcess.reset();
        status = Status::Failed;
        notifyStatus();
        return;
    }

    // Quick synchronous health check to localhost
    juce::URL healthUrl(endpointUrl + "/health");
    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(1000)
                       .withStatusCode(&statusCode);

    auto stream = healthUrl.createInputStream(options);
    if (stream != nullptr && statusCode == 200)
    {
        auto response = stream->readEntireStreamAsString();
        auto json = juce::JSON::parse(response);

        if (json.getProperty("status", "").toString() == "ok")
        {
            DBG("BackendManager: backend is ready");
            stopTimer();
            status = Status::Running;
            notifyStatus();
            return;
        }
    }

    ++healthCheckAttempts;
    if (healthCheckAttempts >= maxHealthCheckAttempts)
    {
        DBG("BackendManager: health check timed out after "
            + juce::String(maxHealthCheckAttempts * healthCheckIntervalMs / 1000) + "s");
        stopTimer();
        status = Status::Failed;
        notifyStatus();
    }
}

void BackendManager::notifyStatus()
{
    if (statusCallback)
        statusCallback(status.load());
}

juce::File BackendManager::findPythonExecutable() const
{
    auto backendDir = findBackendDirectory();

    // 1. Check for venv inside backend dir
  #if JUCE_WINDOWS
    auto venvPython = backendDir.getChildFile("venv/Scripts/python.exe");
  #else
    auto venvPython = backendDir.getChildFile("venv/bin/python");
  #endif
    if (venvPython.existsAsFile())
        return venvPython;

    // 2. Check for .venv inside backend dir
  #if JUCE_WINDOWS
    auto dotVenvPython = backendDir.getChildFile(".venv/Scripts/python.exe");
  #else
    auto dotVenvPython = backendDir.getChildFile(".venv/bin/python");
  #endif
    if (dotVenvPython.existsAsFile())
        return dotVenvPython;

    // 3. Check for venv or .venv one level up (project root)
  #if JUCE_WINDOWS
    auto rootVenvPython = backendDir.getParentDirectory().getChildFile("venv/Scripts/python.exe");
    auto rootDotVenvPython = backendDir.getParentDirectory().getChildFile(".venv/Scripts/python.exe");
  #else
    auto rootVenvPython = backendDir.getParentDirectory().getChildFile("venv/bin/python");
    auto rootDotVenvPython = backendDir.getParentDirectory().getChildFile(".venv/bin/python");
  #endif
    if (rootDotVenvPython.existsAsFile())
        return rootDotVenvPython;
    if (rootVenvPython.existsAsFile())
        return rootVenvPython;

    // 3. System python3
    auto python3 = juce::File("/usr/bin/python3");
    if (python3.existsAsFile())
        return python3;

    // 4. System python
    auto python = juce::File("/usr/bin/python");
    if (python.existsAsFile())
        return python;

    return {};
}

juce::File BackendManager::findBackendDirectory() const
{
    // 1. Compile-time path from CMake (dev builds)
  #ifdef T5YNTH_BACKEND_DIR
    juce::File cmakeDir(T5YNTH_BACKEND_DIR);
    if (cmakeDir.isDirectory() && cmakeDir.getChildFile("server.py").existsAsFile())
        return cmakeDir;
  #endif

    // 2. Walk up from executable looking for backend/server.py
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto dir = exe.getParentDirectory();

    for (int i = 0; i < 8; ++i)
    {
        auto candidate = dir.getChildFile("backend");
        if (candidate.isDirectory() && candidate.getChildFile("server.py").existsAsFile())
            return candidate;
        dir = dir.getParentDirectory();
    }

    return {};
}
