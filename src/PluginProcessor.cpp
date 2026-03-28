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
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"drift_regen", 1}, "Drift Regen", false));
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

    // Drift targets + waveform selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift1_target", 1}, "Drift1 Target",
        juce::StringArray{"None", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift2_target", 1}, "Drift2 Target",
        juce::StringArray{"None", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift1_wave", 1}, "Drift1 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Sq"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift2_wave", 1}, "Drift2 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Sq"}, 0));

    // ENV Amount (per envelope)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_amount", 1}, "Amp Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_amount", 1}, "Mod1 Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_amount", 1}, "Mod2 Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // ENV Loop (per envelope)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"amp_loop", 1}, "Amp Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"mod1_loop", 1}, "Mod1 Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"mod2_loop", 1}, "Mod2 Loop", false));

    // ENV / LFO targets (for modulation routing in processBlock)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod1_target", 1}, "Mod1 Target",
        juce::StringArray{"DCA", "Filter", "Scan", "---"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod2_target", 1}, "Mod2 Target",
        juce::StringArray{"DCA", "Filter", "Scan", "---"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_target", 1}, "LFO1 Target",
        juce::StringArray{"Filter", "Scan", "Alpha", "---"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_target", 1}, "LFO2 Target",
        juce::StringArray{"Filter", "Scan", "Alpha", "---"}, 3));

    // LFO Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_mode", 1}, "LFO1 Mode",
        juce::StringArray{"Free", "Trig"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_mode", 1}, "LFO2 Mode",
        juce::StringArray{"Free", "Trig"}, 0));

    // Delay damp
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_damp", 1}, "Delay Damp",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Looper controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"loop_mode", 1}, "Loop Mode",
        juce::StringArray{"One-shot", "Loop", "Ping-Pong"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"crossfade_ms", 1}, "Crossfade",
        juce::NormalisableRange<float>(0.0f, 500.0f, 10.0f), 150.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"normalize", 1}, "Normalize", false));

    // Effect enables
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"filter_enabled", 1}, "Filter Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"delay_enabled", 1}, "Delay Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"reverb_enabled", 1}, "Reverb Enabled", false));

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

    // Sequencer / Arpeggiator
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"seq_mode", 1}, "Seq Mode",
        juce::StringArray{"Seq", "Arp Up", "Arp Dn", "Arp UD", "Arp Rnd"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"seq_running", 1}, "Seq Running", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"seq_bpm", 1}, "Seq BPM",
        juce::NormalisableRange<float>(20.0f, 300.0f, 0.1f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"seq_steps", 1}, "Seq Steps", 1, 64, 16));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"arp_rate", 1}, "Arp Rate",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.25f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"arp_octaves", 1}, "Arp Octaves", 1, 4, 1));

    // Master volume
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"master_vol", 1}, "Master Volume",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));

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
    stepSequencer.prepare(sampleRate, samplesPerBlock);
    arpeggiator.prepare(sampleRate, samplesPerBlock);
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

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Read all parameters at block start ──────────────────────────────────
    // Amp envelope
    ampEnvelope.setAttack(parameters.getRawParameterValue("amp_attack")->load());
    ampEnvelope.setDecay(parameters.getRawParameterValue("amp_decay")->load());
    ampEnvelope.setSustain(parameters.getRawParameterValue("amp_sustain")->load());
    ampEnvelope.setRelease(parameters.getRawParameterValue("amp_release")->load());
    float ampAmount = parameters.getRawParameterValue("amp_amount")->load();
    ampEnvelope.setLooping(parameters.getRawParameterValue("amp_loop")->load() > 0.5f);

    // Mod envelope 1
    modEnvelope1.setAttack(parameters.getRawParameterValue("mod1_attack")->load());
    modEnvelope1.setDecay(parameters.getRawParameterValue("mod1_decay")->load());
    modEnvelope1.setSustain(parameters.getRawParameterValue("mod1_sustain")->load());
    modEnvelope1.setRelease(parameters.getRawParameterValue("mod1_release")->load());
    float mod1Amount = parameters.getRawParameterValue("mod1_amount")->load();
    int mod1Target = static_cast<int>(parameters.getRawParameterValue("mod1_target")->load());
    modEnvelope1.setLooping(parameters.getRawParameterValue("mod1_loop")->load() > 0.5f);

    // Mod envelope 2
    modEnvelope2.setAttack(parameters.getRawParameterValue("mod2_attack")->load());
    modEnvelope2.setDecay(parameters.getRawParameterValue("mod2_decay")->load());
    modEnvelope2.setSustain(parameters.getRawParameterValue("mod2_sustain")->load());
    modEnvelope2.setRelease(parameters.getRawParameterValue("mod2_release")->load());
    float mod2Amount = parameters.getRawParameterValue("mod2_amount")->load();
    int mod2Target = static_cast<int>(parameters.getRawParameterValue("mod2_target")->load());
    modEnvelope2.setLooping(parameters.getRawParameterValue("mod2_loop")->load() > 0.5f);

    // LFOs
    lfo1.setRate(parameters.getRawParameterValue("lfo1_rate")->load());
    lfo1.setDepth(parameters.getRawParameterValue("lfo1_depth")->load());
    lfo1.setWaveform(static_cast<int>(parameters.getRawParameterValue("lfo1_wave")->load()));
    int lfo1Target = static_cast<int>(parameters.getRawParameterValue("lfo1_target")->load());

    lfo2.setRate(parameters.getRawParameterValue("lfo2_rate")->load());
    lfo2.setDepth(parameters.getRawParameterValue("lfo2_depth")->load());
    lfo2.setWaveform(static_cast<int>(parameters.getRawParameterValue("lfo2_wave")->load()));
    int lfo2Target = static_cast<int>(parameters.getRawParameterValue("lfo2_target")->load());

    // Filter base values
    bool filterEnabled = parameters.getRawParameterValue("filter_enabled")->load() > 0.5f;
    float baseCutoff = parameters.getRawParameterValue("filter_cutoff")->load();
    float baseReso = parameters.getRawParameterValue("filter_resonance")->load();
    int filterType = static_cast<int>(parameters.getRawParameterValue("filter_type")->load());
    float filterMix = parameters.getRawParameterValue("filter_mix")->load();
    float kbdTrack = parameters.getRawParameterValue("filter_kbd_track")->load();

    // Scan base value
    float baseScan = parameters.getRawParameterValue("osc_scan")->load();

    // Master volume (dB → linear)
    float masterDb = parameters.getRawParameterValue("master_vol")->load();
    float masterGain = juce::Decibels::decibelsToGain(masterDb);

    // Drift LFOs (block-rate)
    driftLfo.setEnabled(parameters.getRawParameterValue("drift_enabled")->load() > 0.5f);
    driftLfo.setAutoRegen(parameters.getRawParameterValue("drift_regen")->load() > 0.5f);
    driftLfo.setLfoRate(0, parameters.getRawParameterValue("drift1_rate")->load());
    driftLfo.setLfoDepth(0, parameters.getRawParameterValue("drift1_depth")->load());
    driftLfo.setLfoTarget(0, static_cast<int>(parameters.getRawParameterValue("drift1_target")->load()));
    driftLfo.setLfoRate(1, parameters.getRawParameterValue("drift2_rate")->load());
    driftLfo.setLfoDepth(1, parameters.getRawParameterValue("drift2_depth")->load());
    driftLfo.setLfoTarget(1, static_cast<int>(parameters.getRawParameterValue("drift2_target")->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    // Apply drift to scan if targeted (target 4 = WtScan in DriftLFO enum)
    float driftScanOffset = driftLfo.getOffsetForTarget(4); // WtScan

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    int seqMode = static_cast<int>(parameters.getRawParameterValue("seq_mode")->load());
    bool seqRunning = parameters.getRawParameterValue("seq_running")->load() > 0.5f;
    float seqBpm = parameters.getRawParameterValue("seq_bpm")->load();
    int seqSteps = static_cast<int>(parameters.getRawParameterValue("seq_steps")->load());
    float arpRate = parameters.getRawParameterValue("arp_rate")->load();
    int arpOctaves = static_cast<int>(parameters.getRawParameterValue("arp_octaves")->load());
    bool arpEnabled = (seqMode >= 1); // 0=Seq only, 1-4=Arp modes

    // Stage 1: Step sequencer (generates MIDI notes from step grid)
    stepSequencer.setBpm(static_cast<double>(seqBpm));
    stepSequencer.setNumSteps(seqSteps);
    if (seqRunning)
        stepSequencer.start();
    else
        stepSequencer.stop();
    stepSequencer.processBlock(buffer, midiMessages);

    // Stage 2: Arpeggiator (consumes note-on/off, generates arpeggiated output)
    if (arpEnabled)
    {
        arpeggiator.setBpm(static_cast<double>(seqBpm));
        arpeggiator.setRate(arpRate);
        arpeggiator.setOctaveRange(arpOctaves);
        arpeggiator.setMode(static_cast<T5ynthArpeggiator::Mode>(seqMode - 1));

        // Feed note events (from seq + external MIDI) to arpeggiator
        juce::MidiBuffer filtered;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                arpeggiator.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                arpeggiator.noteOff(msg.getNoteNumber());
            else
                filtered.addEvent(msg, metadata.samplePosition);
        }
        midiMessages.swapWith(filtered);
        arpeggiator.processBlock(buffer, midiMessages);
    }
    else
    {
        arpeggiator.reset();
    }

    // ── MIDI ────────────────────────────────────────────────────────────────
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            currentNote = static_cast<float>(msg.getNoteNumber());
            currentVelocity = msg.getFloatVelocity();
            noteIsOn = true;
            lastMidiNote.store(msg.getNoteNumber(), std::memory_order_relaxed);
            lastMidiVelocity.store(juce::roundToInt(msg.getFloatVelocity() * 127.0f), std::memory_order_relaxed);
            lastMidiNoteOn.store(true, std::memory_order_relaxed);
            ampEnvelope.noteOn(currentVelocity);
            modEnvelope1.noteOn(currentVelocity);
            modEnvelope2.noteOn(currentVelocity);
            wavetableOsc.setFrequency(juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()));

            // LFO trigger mode: reset phase on note-on
            if (static_cast<int>(parameters.getRawParameterValue("lfo1_mode")->load()) == 1)
                lfo1.reset();
            if (static_cast<int>(parameters.getRawParameterValue("lfo2_mode")->load()) == 1)
                lfo2.reset();
        }
        else if (msg.isNoteOff())
        {
            noteIsOn = false;
            lastMidiNoteOn.store(false, std::memory_order_relaxed);
            ampEnvelope.noteOff();
            modEnvelope1.noteOff();
            modEnvelope2.noteOff();
        }
    }

    // ── Audio generation + per-sample modulation ────────────────────────────
    // Target indices: 0=DCA, 1=Filter, 2=Scan, 3=None (env)
    //                 0=Filter, 1=Scan, 2=Alpha, 3=None (lfo)

    if (engineMode == EngineMode::Wavetable && wavetableOsc.hasFrames())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Process mod sources
            float ampEnv = ampEnvelope.processSample() * ampAmount;
            float mod1Env = modEnvelope1.processSample() * mod1Amount;
            float mod2Env = modEnvelope2.processSample() * mod2Amount;
            float lfo1Val = lfo1.processSample(); // already scaled by depth
            float lfo2Val = lfo2.processSample();

            // Compute modulated scan position
            float scanMod = baseScan + driftScanOffset;
            if (mod1Target == 2) scanMod += mod1Env;        // env → scan
            if (mod2Target == 2) scanMod += mod2Env;
            if (lfo1Target == 1) scanMod += lfo1Val;        // lfo → scan
            if (lfo2Target == 1) scanMod += lfo2Val;
            wavetableOsc.setScanPosition(juce::jlimit(0.0f, 1.0f, scanMod));

            // Generate sample
            float sample = wavetableOsc.processSample();

            // Apply DCA (amp envelope + any mod env targeting DCA)
            float vca = ampEnv;
            if (mod1Target == 0) vca *= (1.0f + mod1Env);   // env → DCA (additive boost)
            if (mod2Target == 0) vca *= (1.0f + mod2Env);
            sample *= vca;

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, sample);
        }
    }
    else if (engineMode == EngineMode::Looper && looper.hasAudio())
    {
        looper.processBlock(buffer);

        for (int i = 0; i < numSamples; ++i)
        {
            float ampEnv = ampEnvelope.processSample() * ampAmount;
            float mod1Env = modEnvelope1.processSample() * mod1Amount;
            float mod2Env = modEnvelope2.processSample() * mod2Amount;
            lfo1.processSample(); // advance phase even in looper mode
            lfo2.processSample();

            float vca = ampEnv;
            if (mod1Target == 0) vca *= (1.0f + mod1Env);
            if (mod2Target == 0) vca *= (1.0f + mod2Env);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * vca);
        }
    }
    else
    {
        // Fallback sine oscillator — always produces sound for MIDI
        for (int i = 0; i < numSamples; ++i)
        {
            ampEnvelope.processSample();
            modEnvelope1.processSample();
            modEnvelope2.processSample();
            lfo1.processSample();
            lfo2.processSample();

            if (currentNote >= 0 && noteIsOn)
            {
                float freq = juce::MidiMessage::getMidiNoteInHertz(static_cast<int>(currentNote));
                fallbackPhase += freq / static_cast<float>(getSampleRate());
                if (fallbackPhase >= 1.0f) fallbackPhase -= 1.0f;
                float sample = std::sin(fallbackPhase * juce::MathConstants<float>::twoPi) * 0.3f;
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample(ch, i, sample);
            }
        }
    }

    // ── Filter with modulation ──────────────────────────────────────────────
    if (filterEnabled)
    {
        // Compute modulated cutoff (block-rate approximation — good enough for filter)
        float cutoffMod = baseCutoff;

        // Keyboard tracking: shift cutoff based on note relative to C3
        if (kbdTrack > 0.0f && currentNote >= 0)
            cutoffMod *= std::pow(2.0f, (currentNote - 60.0f) / 12.0f * kbdTrack);

        // Envelope → filter: sweep proportional to base (like web UI: base × (1 + env × 8))
        float lastMod1 = modEnvelope1.processSample(); // approximate last value
        float lastMod2 = modEnvelope2.processSample();
        // Step back — we already advanced, so use the last computed values
        // For block-rate this is close enough
        if (mod1Target == 1) cutoffMod *= (1.0f + mod1Amount * 4.0f * lastMod1);
        if (mod2Target == 1) cutoffMod *= (1.0f + mod2Amount * 4.0f * lastMod2);

        // LFO → filter: bipolar modulation
        float lastLfo1 = lfo1.processSample();
        float lastLfo2 = lfo2.processSample();
        if (lfo1Target == 0) cutoffMod *= (1.0f + lastLfo1 * 2.0f);
        if (lfo2Target == 0) cutoffMod *= (1.0f + lastLfo2 * 2.0f);

        cutoffMod = juce::jlimit(20.0f, 20000.0f, cutoffMod);

        filter.setCutoff(cutoffMod);
        filter.setResonance(baseReso);
        filter.setType(filterType);
        filter.setMix(filterMix);
        filter.processBlock(buffer);
    }

    // ── Effects ─────────────────────────────────────────────────────────────
    if (parameters.getRawParameterValue("delay_enabled")->load() > 0.5f)
        delay.processBlock(buffer);

    if (parameters.getRawParameterValue("reverb_enabled")->load() > 0.5f)
        reverb.processBlock(buffer);

    // ── Master volume ───────────────────────────────────────────────────────
    buffer.applyGain(masterGain);

    // ── Limiter (always on, internal safety) ────────────────────────────────
    limiter.processBlock(buffer);
}

void T5ynthProcessor::loadGeneratedAudio(const juce::AudioBuffer<float>& audioBuffer, double sr)
{
    looper.loadBuffer(audioBuffer, sr);
    wavetableOsc.extractFramesFromBuffer(audioBuffer, sr);

    // Auto-trigger envelopes so looper plays immediately without MIDI
    noteIsOn = true;
    ampEnvelope.noteOn(1.0f);
    modEnvelope1.noteOn(1.0f);
    modEnvelope2.noteOn(1.0f);

    // Snapshot channel 0 for waveform display
    if (audioBuffer.getNumChannels() > 0 && audioBuffer.getNumSamples() > 0)
    {
        waveformSnapshot.setSize(1, audioBuffer.getNumSamples(), false, false, true);
        waveformSnapshot.copyFrom(0, 0, audioBuffer, 0, 0, audioBuffer.getNumSamples());
        newWaveformReady.store(true, std::memory_order_release);
    }
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
