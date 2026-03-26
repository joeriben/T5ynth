#include "PluginProcessor.h"
#include "PluginEditor.h"

T5ynthProcessor::T5ynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "T5ynth", createParameterLayout())
{
    backendManager.setStatusCallback([this](BackendManager::Status s)
    {
        if (s == BackendManager::Status::Running)
        {
            backendConnection.setEndpoint(backendManager.getEndpointUrl());
            backendConnection.checkHealth();
        }
    });

    backendManager.start();
}

T5ynthProcessor::~T5ynthProcessor()
{
    backendManager.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout T5ynthProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Oscillator
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"osc_scan", 1}, "Scan Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Amplitude Envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_attack", 1}, "Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_decay", 1}, "Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_sustain", 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_release", 1}, "Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 200.0f));

    // Filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_cutoff", 1}, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_resonance", 1}, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filter_type", 1}, "Filter Type",
        juce::StringArray{"Lowpass", "Highpass", "Bandpass"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filter_slope", 1}, "Filter Slope",
        juce::StringArray{"12dB", "24dB"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_mix", 1}, "Filter Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_kbd_track", 1}, "Filter Kbd Track",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_time", 1}, "Delay Time",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f), 300.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_feedback", 1}, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_mix", 1}, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Reverb
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"reverb_mix", 1}, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Generation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_alpha", 1}, "Alpha",
        juce::NormalisableRange<float>(-2.0f, 2.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_magnitude", 1}, "Magnitude",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_noise", 1}, "Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_duration", 1}, "Duration",
        juce::NormalisableRange<float>(0.1f, 47.0f, 0.1f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"gen_steps", 1}, "Steps", 1, 100, 20));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_cfg", 1}, "CFG Scale",
        juce::NormalisableRange<float>(1.0f, 15.0f, 0.1f), 7.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_start", 1}, "Start Position",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"gen_seed", 1}, "Seed", -1, 999999999, -1));

    // Engine mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"engine_mode", 1}, "Engine Mode",
        juce::StringArray{"Looper", "Wavetable"}, 0));

    // Mod Envelope 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_attack", 1}, "Mod1 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_decay", 1}, "Mod1 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_sustain", 1}, "Mod1 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_release", 1}, "Mod1 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 200.0f));

    // Mod Envelope 2
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_attack", 1}, "Mod2 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_decay", 1}, "Mod2 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_sustain", 1}, "Mod2 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_release", 1}, "Mod2 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 200.0f));

    // LFO 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo1_rate", 1}, "LFO1 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo1_depth", 1}, "LFO1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_wave", 1}, "LFO1 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Square", "S&H"}, 0));

    // LFO 2
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo2_rate", 1}, "LFO2 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo2_depth", 1}, "LFO2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_wave", 1}, "LFO2 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Square", "S&H"}, 0));

    // Drift LFO
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"drift_enabled", 1}, "Drift Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift1_rate", 1}, "Drift1 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift1_depth", 1}, "Drift1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift2_rate", 1}, "Drift2 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.07f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift2_depth", 1}, "Drift2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift3_rate", 1}, "Drift3 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.03f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift3_depth", 1}, "Drift3 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));

    // Limiter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"limiter_thresh", 1}, "Limiter Threshold",
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), -0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"limiter_release", 1}, "Limiter Release",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.3f), 100.0f));

    // Reverb IR
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"reverb_ir", 1}, "Reverb IR",
        juce::StringArray{"Bright", "Medium", "Dark"}, 1));

    // Arpeggiator
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"arp_mode", 1}, "Arp Mode",
        juce::StringArray{"Up", "Down", "UpDown", "Random", "Order"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"arp_rate", 1}, "Arp Rate",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.25f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"arp_octaves", 1}, "Arp Octaves", 1, 4, 1));

    // Sequencer
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"seq_bpm", 1}, "Seq BPM",
        juce::NormalisableRange<float>(20.0f, 300.0f, 0.1f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"seq_steps", 1}, "Seq Steps", 1, 64, 16));

    return { params.begin(), params.end() };
}

void T5ynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    wavetableOsc.prepare(sampleRate, samplesPerBlock);
    looper.prepare(sampleRate, samplesPerBlock);
    ampEnvelope.prepare(sampleRate);
    modEnvelope1.prepare(sampleRate);
    modEnvelope2.prepare(sampleRate);
    lfo1.prepare(sampleRate);
    lfo2.prepare(sampleRate);
    filter.prepare(sampleRate, samplesPerBlock);
    delay.prepare(sampleRate, samplesPerBlock);
    reverb.prepare(sampleRate, samplesPerBlock);
    limiter.prepare(sampleRate, samplesPerBlock);
}

void T5ynthProcessor::releaseResources()
{
    wavetableOsc.reset();
    looper.reset();
}

void T5ynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Process MIDI
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            currentNote = static_cast<float>(msg.getNoteNumber());
            currentVelocity = msg.getFloatVelocity();
            noteIsOn = true;
            ampEnvelope.noteOn(currentVelocity);
            wavetableOsc.setFrequency(juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()));
        }
        else if (msg.isNoteOff())
        {
            noteIsOn = false;
            ampEnvelope.noteOff();
        }
    }

    const int numSamples = buffer.getNumSamples();

    // Generate audio based on engine mode
    if (engineMode == EngineMode::Wavetable && wavetableOsc.hasFrames())
    {
        auto scanParam = parameters.getRawParameterValue("osc_scan");
        wavetableOsc.setScanPosition(scanParam->load());

        for (int i = 0; i < numSamples; ++i)
        {
            float sample = wavetableOsc.processSample();
            float envGain = ampEnvelope.processSample();
            sample *= envGain;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, sample);
        }
    }
    else if (engineMode == EngineMode::Looper && looper.hasAudio())
    {
        looper.processBlock(buffer);

        // Apply envelope
        for (int i = 0; i < numSamples; ++i)
        {
            float envGain = ampEnvelope.processSample();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * envGain);
        }
    }

    // Filter
    filter.processBlock(buffer);

    // Delay
    delay.processBlock(buffer);

    // Reverb
    reverb.processBlock(buffer);

    // Limiter
    limiter.processBlock(buffer);
}

void T5ynthProcessor::loadGeneratedAudio(const juce::AudioBuffer<float>& audioBuffer, double sr)
{
    looper.loadBuffer(audioBuffer, sr);
    wavetableOsc.extractFramesFromBuffer(audioBuffer, sr);
}

juce::AudioProcessorEditor* T5ynthProcessor::createEditor()
{
    return new T5ynthEditor(*this);
}

void T5ynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void T5ynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new T5ynthProcessor();
}
