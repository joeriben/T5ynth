#pragma once
#include <JuceHeader.h>
#include <functional>

/** Background, non-blocking check against the GitHub "latest release" API.
 *  Runs entirely on its own thread — never touches audio-thread state and never
 *  blocks plugin construction or Python backend/model startup. A result (if any)
 *  is marshalled onto the message thread before onUpdateAvailable fires. */
class UpdateChecker : public juce::Thread
{
public:
    explicit UpdateChecker(juce::String currentVersion);
    ~UpdateChecker() override;

    /** Fired on the message thread iff the GitHub "latest" release is newer than
     *  currentVersion. Never fired on network error, timeout, or "no update". */
    std::function<void(juce::String newVersion, juce::String htmlUrl)> onUpdateAvailable;

    void run() override;

private:
    juce::String currentVersion_;
    static bool isNewer(const juce::String& latestTag, const juce::String& current);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
