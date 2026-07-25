#pragma once
#include <JuceHeader.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

/*  The standalone's audio INPUT, opened on demand instead of at launch.

    Why this exists (measured 2026-07-25). JUCE's StandalonePluginHolder opens
    the audio device inside its constructor, BEFORE any window is created:
    createWindow() -> createPluginHolder() -> init() -> reloadAudioDeviceState()
    -> AudioDeviceManager::initialise(). If that call blocks, the app has no
    window, no crash and no log entry -- the process lives and looks like it
    never started. Two `sample` runs of exactly that state showed the main
    thread parked in mach_msg to coreaudiod inside AudioDeviceCreateIOProcID.

    What made it block: the instrument declares a stereo input bus (Resynth's
    ext capture), so the standalone requested an input device as well as an
    output one, and the stored setup named two DIFFERENT devices --

        <DEVICESETUP audioOutputDeviceName="MacBook Pro Speakers"
                     audioInputDeviceName="MacBook Pro Microphone" .../>

    -- which JUCE serves by building an AudioIODeviceCombiner, an aggregate of
    the two. Registering that aggregate with the CoreAudio server is the call
    that hangs while the server still holds a hard-killed previous client's
    stream usage. Output-only start does not go down that path at all.

    So the launcher starts the standalone with NO input (see
    StandaloneAppLauncher.cpp) and parks the user's chosen input device here;
    openParkedInputDevice() puts it back when Resynth ext actually needs to
    listen. Nothing is thrown away: park and unpark are the same string.

    Plugin builds never see any of this -- there the host owns the devices.
*/
namespace t5ynth::standalone
{

/** Property key the parked input device name lives under, in the standalone's
    own PropertiesFile beside JUCE's "audioSetup". */
inline constexpr const char* kParkedInputKey = "parkedAudioInputDevice";

/** Take the input device out of the stored setup BEFORE JUCE reads it, and
    remember it under kParkedInputKey.

    Must run before the StandalonePluginHolder is constructed:
    AudioDeviceManager::initialiseFromXML restores audioInputDeviceName from
    the saved XML unconditionally -- the requested input CHANNEL count (0, via
    the launcher's channel constraint) does not suppress it, so a saved input
    device would be reopened at launch regardless. Idempotent: with nothing to
    park it leaves both the setup and the parked value untouched. */
inline void parkStoredInputDevice(juce::PropertySet& settings)
{
    auto setup = settings.getXmlValue("audioSetup");
    if (setup == nullptr)
        return;

    juce::String parked;

    // "audioDeviceName" is JUCE's single-name form, used when input and output
    // are the SAME device. Splitting it into an output-only name keeps that
    // device as the output and parks it as the input, so unparking restores
    // exactly the same combination.
    if (setup->hasAttribute("audioDeviceName"))
    {
        parked = setup->getStringAttribute("audioDeviceName");
        setup->setAttribute("audioOutputDeviceName", parked);
        setup->removeAttribute("audioDeviceName");
    }
    else if (setup->hasAttribute("audioInputDeviceName"))
    {
        parked = setup->getStringAttribute("audioInputDeviceName");
    }

    if (parked.isEmpty())
        return;

    setup->removeAttribute("audioInputDeviceName");
    settings.setValue(kParkedInputKey, parked);
    settings.setValue("audioSetup", setup.get());
}

/** Open the parked input device (or the current default one when nothing was
    parked, e.g. a first run) on the running standalone.

    Returns an empty string on success, otherwise the device manager's own
    error text -- the caller shows it; this never fails silently.
    "not a standalone" is success: in a plugin the host supplies the input and
    there is nothing to open. Message thread only: it reopens the audio device.

    Leaves the parked value in place. It is the user's choice of input, and it
    has to survive the next launch, which parks it again. */
inline juce::String openParkedInputDevice()
{
    JUCE_ASSERT_MESSAGE_THREAD

   #if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return {};                       // not running as the standalone

    auto& devices = holder->deviceManager;

    // "Already listening" is judged by the channels the device actually opened,
    // never by the device NAME: a previous attempt can leave a name set while
    // the device carries zero input channels, and a name-only check would then
    // refuse to ever try again — cementing exactly the broken state.
    if (auto* open = devices.getCurrentAudioDevice())
        if (open->getActiveInputChannels().countNumberOfSetBits() > 0)
            return {};

    // Captured ONCE, and every attempt starts from this copy. Not from the
    // live setup: setAudioDeviceSetup calls deleteCurrentDevice() BEFORE it
    // validates the device names, and that clears the OUTPUT name along with
    // the input one. A second attempt reading the live setup would therefore
    // carry an empty output device, open with zero output channels, and leave
    // the instrument silent — persisted to the settings file, so the silence
    // would survive the next launch. This is also what gets restored below
    // when no attempt manages to listen.
    const auto original = devices.getAudioDeviceSetup();

    auto applyInput = [&devices, &original](const juce::String& deviceName) -> juce::String
    {
        auto setup = original;
        setup.inputDeviceName = deviceName;

        // Explicit channels, NOT useDefaultInputChannels: "default" means the
        // input-channel count the manager was INITIALISED with, and the
        // launcher initialised it with zero on purpose. Leaving that flag set
        // opens the device with an empty input channel set — and
        // setAudioDeviceSetup still returns success, so the capture ring would
        // stay empty forever with nothing reporting a failure.
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange(0, 2, true);   // the processor's stereo input bus

        return devices.setAudioDeviceSetup(setup, true);
    };

    auto* settings = holder->settings.get();
    const auto parked = settings != nullptr ? settings->getValue(kParkedInputKey) : juce::String();

    juce::String error;
    if (parked.isNotEmpty())
        error = applyInput(parked);

    // Parked device unplugged, or never chosen (first run): fall back to the
    // device type's default input instead of leaving ext dead until someone
    // edits Audio Settings by hand.
    if (parked.isEmpty() || error.isNotEmpty())
    {
        if (auto* type = devices.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto names = type->getDeviceNames(true);
            const int  index = type->getDefaultDeviceIndex(true);
            if (juce::isPositiveAndBelow(index, names.size()))
                error = applyInput(names[index]);
        }
    }

    // Judged on the DEVICE, not on the return string: setAudioDeviceSetup
    // reports success for an open that yielded no input channels at all. And
    // when nothing ended up listening, the setup captured above is put back —
    // otherwise a failed attempt leaves the manager with no output device (or
    // no device at all), which would turn a click on "ext" into total silence.
    auto* device = devices.getCurrentAudioDevice();
    if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
    {
        // The rollback itself can fail: if the output device disappeared between
        // the capture above and here, setAudioDeviceSetup has already deleted
        // the current device before rejecting `original`, leaving NO device at
        // all. JUCE does not recover from that on its own —
        // AudioDeviceManager::audioDeviceListChanged(), the path that
        // re-selects a device after an unplug, is guarded on
        // currentAudioDevice != nullptr, which this just made null. So the app
        // would stay silent until someone opens Audio Settings by hand, even
        // with other outputs present. Falling back to defaults costs one line.
        const auto rollbackError = devices.setAudioDeviceSetup(original, true);
        if (rollbackError.isNotEmpty() || devices.getCurrentAudioDevice() == nullptr)
            devices.initialiseWithDefaultDevices(0, 2);   // output-only, as at launch

        return error.isNotEmpty() ? error
                                  : juce::String("the audio input could not be opened.");
    }

    // Only now, and only for an open that really happened: JUCE mutes the
    // standalone's input by default (shouldMuteInput, its feedback-loop
    // protection) and hands the processor an empty buffer while muted —
    // unconditionally, it does not consult the feedback-loop flag. That flag is
    // now false (the launcher reports no input channels), which removes BOTH
    // the "Mute audio input" checkbox and the notification bar that would let
    // the user undo it. So on any profile that never unmuted, ext would capture
    // digital silence with no control left to fix it. Pressing "ext" is the
    // request to listen; this grants it.
    holder->getMuteInputValue().setValue(false);

    return {};
   #else
    return {};                           // the host owns the input
   #endif
}

} // namespace t5ynth::standalone
