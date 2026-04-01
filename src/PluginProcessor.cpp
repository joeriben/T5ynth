#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

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
    inference.unloadModels();
    backendManager.stop();
}

bool T5ynthProcessor::loadInferenceModels(const juce::File& modelDir)
{
    return inference.loadModels(modelDir);
}

bool T5ynthProcessor::launchPipeInference(const juce::File& backendDir)
{
    return pipeInference.launch(backendDir);
}

juce::AudioProcessorValueTreeState::ParameterLayout T5ynthProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Oscillator
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"osc_scan", 1}, "Scan Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Amplitude Envelope (A=0, D=200ms, S=10%, R=180ms)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_attack", 1}, "Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_decay", 1}, "Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_sustain", 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_release", 1}, "Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 180.0f));

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
        juce::StringArray{"12dB", "24dB"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_mix", 1}, "Filter Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_kbd_track", 1}, "Filter Kbd Track",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_time", 1}, "Delay Time",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.1f), 250.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_feedback", 1}, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_mix", 1}, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    // Reverb
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"reverb_mix", 1}, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f));

    // Generation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_alpha", 1}, "Alpha",
        juce::NormalisableRange<float>(-2.0f, 2.0f), 0.0f));
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
        juce::StringArray{"Sampler", "Wavetable"}, 0));

    // Mod Envelope 1 (A=0, D=2500ms, S=10%, R=4000ms, Amt=100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_attack", 1}, "Mod1 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_decay", 1}, "Mod1 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 2500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_sustain", 1}, "Mod1 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_release", 1}, "Mod1 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 4000.0f));

    // Mod Envelope 2 (A=0, D=2500ms, S=10%, R=4000ms, Amt=100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_attack", 1}, "Mod2 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_decay", 1}, "Mod2 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 2500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_sustain", 1}, "Mod2 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_release", 1}, "Mod2 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 4000.0f));

    // LFO 1 (reference defaults: rate=2.0, depth=0, sine)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo1_rate", 1}, "LFO1 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo1_depth", 1}, "LFO1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_wave", 1}, "LFO1 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Square"}, 0));

    // LFO 2 (reference defaults: rate=0.5, depth=0, triangle)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo2_rate", 1}, "LFO2 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lfo2_depth", 1}, "LFO2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_wave", 1}, "LFO2 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Square"}, 1));

    // Drift LFO
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"drift_enabled", 1}, "Drift Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"drift_regen", 1}, "Drift Regen", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift1_rate", 1}, "Drift1 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift1_depth", 1}, "Drift1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift2_rate", 1}, "Drift2 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.005f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift2_depth", 1}, "Drift2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift3_rate", 1}, "Drift3 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.002f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drift3_depth", 1}, "Drift3 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

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

    // Drift 3 target + waveform (was missing — drift3 rate/depth existed but had no target/wave)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift3_target", 1}, "Drift3 Target",
        juce::StringArray{"None", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift3_wave", 1}, "Drift3 Wave",
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

    // ENV targets:
    // 0=DCA, 1=Filter, 2=Scan, 3=Pitch, 4=DelayTime, 5=DelayFB, 6=DelayMix, 7=ReverbMix,
    // 8=LFO1Rate, 9=LFO1Amt, 10=LFO2Rate, 11=LFO2Amt, 12=None
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod1_target", 1}, "Mod1 Target",
        juce::StringArray{"DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth", "---"}, 12));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod2_target", 1}, "Mod2 Target",
        juce::StringArray{"DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth", "---"}, 12));
    // LFO targets: 0=Filter, 1=Scan, 2=Pitch, 3=DlyTime, 4=DlyFB, 5=DlyMix, 6=RevMix,
    //              7=LFO1Rate, 8=LFO2Rate, 9=LFO1Depth, 10=LFO2Depth, 11=None
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_target", 1}, "LFO1 Target",
        juce::StringArray{"Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO2 Rate", "LFO2 Depth", "---"}, 9));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_target", 1}, "LFO2 Target",
        juce::StringArray{"Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO1 Rate", "LFO1 Depth", "---"}, 9));

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
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Sampler controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"loop_mode", 1}, "Loop Mode",
        juce::StringArray{"One-shot", "Loop", "Ping-Pong"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"crossfade_ms", 1}, "Crossfade",
        juce::NormalisableRange<float>(0.0f, 500.0f, 10.0f), 150.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"normalize", 1}, "Normalize", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"loop_optimize", 1}, "Loop Optimize", false));

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
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), -3.0f));
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
    // Sequencer note division (reference: 1/1, 1/2, 1/4, 1/8, 1/16)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"seq_division", 1}, "Seq Division",
        juce::StringArray{"1/1", "1/2", "1/4", "1/8", "1/16"}, 3)); // default 1/8
    // Sequencer glide time (reference: 10-500ms, default 80)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"seq_glide_time", 1}, "Glide Time",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 80.0f));
    // Arp rate: musical divisions (reference: 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"arp_rate", 1}, "Arp Rate",
        juce::StringArray{"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"arp_octaves", 1}, "Arp Octaves", 1, 4, 1));
    // Separate arp enable + mode (seq_mode kept for old preset compat)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"arp_enabled", 1}, "Arp Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"arp_mode", 1}, "Arp Mode",
        juce::StringArray{"Up", "Down", "UpDown", "Random"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"arp_gate", 1}, "Arp Gate",
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.01f), 0.8f));
    // Global seq gate + preset
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"seq_gate", 1}, "Seq Gate",
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"seq_preset", 1}, "Seq Preset",
        juce::StringArray{"East Coast", "West Coast", "Synthwave", "Techno", "Dub Techno",
                          "Ambient", "IDM Glitch", "Solar", "Arpeggio Bass", "Trance Gate"}, 0));

    // Master volume: purely attenuative (0dB max). DAW fader handles boost.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"master_vol", 1}, "Master Volume",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void T5ynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    masterOsc.prepare(sampleRate, samplesPerBlock);
    masterSampler.prepare(sampleRate, samplesPerBlock);
    voiceManager.prepare(sampleRate, samplesPerBlock);
    lfo1.prepare(sampleRate);
    lfo2.prepare(sampleRate);
    postFilter.prepare(sampleRate, samplesPerBlock);
    delay.prepare(sampleRate, samplesPerBlock);
    reverb.prepare(sampleRate, samplesPerBlock);
    // Load default IR (medium plate)
    reverb.loadImpulseResponse(BinaryData::emt_140_plate_medium_wav,
                               static_cast<size_t>(BinaryData::emt_140_plate_medium_wavSize));
    lastReverbIr = 1; // 0=Bright, 1=Medium, 2=Dark
    limiter.prepare(sampleRate, samplesPerBlock);
    stepSequencer.prepare(sampleRate, samplesPerBlock);
    arpeggiator.prepare(sampleRate, samplesPerBlock);
    lfo1Buffer.resize(static_cast<size_t>(samplesPerBlock));
    lfo2Buffer.resize(static_cast<size_t>(samplesPerBlock));

}

void T5ynthProcessor::releaseResources()
{
    masterOsc.reset();
    masterSampler.reset();
    voiceManager.reset();
}

void T5ynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();


    // ── GAIN STAGING ────────────────────────────────────────────────────────
    // Per Voice: Osc +-1.0 → VCA up to +-4.0 → Filter (gain-neutral, reso +12dB)
    // Sum:       N voices * 1/sqrt(N) scaling → ~constant perceived loudness
    // Post-Sum:  Delay+Reverb up to ~2.7x → Master 0dB max → Limiter -3dB
    // ────────────────────────────────────────────────────────────────────────

    // ── Read all parameters into BlockParams ──────────────────────────────────
    BlockParams bp;
    bp.ampAttack  = parameters.getRawParameterValue("amp_attack")->load();
    bp.ampDecay   = parameters.getRawParameterValue("amp_decay")->load();
    bp.ampSustain = parameters.getRawParameterValue("amp_sustain")->load();
    bp.ampRelease = parameters.getRawParameterValue("amp_release")->load();
    bp.ampAmount  = parameters.getRawParameterValue("amp_amount")->load();
    bp.ampLoop    = parameters.getRawParameterValue("amp_loop")->load() > 0.5f;

    bp.mod1Attack  = parameters.getRawParameterValue("mod1_attack")->load();
    bp.mod1Decay   = parameters.getRawParameterValue("mod1_decay")->load();
    bp.mod1Sustain = parameters.getRawParameterValue("mod1_sustain")->load();
    bp.mod1Release = parameters.getRawParameterValue("mod1_release")->load();
    bp.mod1Amount  = parameters.getRawParameterValue("mod1_amount")->load();
    bp.mod1Target  = static_cast<int>(parameters.getRawParameterValue("mod1_target")->load());
    bp.mod1Loop    = parameters.getRawParameterValue("mod1_loop")->load() > 0.5f;

    bp.mod2Attack  = parameters.getRawParameterValue("mod2_attack")->load();
    bp.mod2Decay   = parameters.getRawParameterValue("mod2_decay")->load();
    bp.mod2Sustain = parameters.getRawParameterValue("mod2_sustain")->load();
    bp.mod2Release = parameters.getRawParameterValue("mod2_release")->load();
    bp.mod2Amount  = parameters.getRawParameterValue("mod2_amount")->load();
    bp.mod2Target  = static_cast<int>(parameters.getRawParameterValue("mod2_target")->load());
    bp.mod2Loop    = parameters.getRawParameterValue("mod2_loop")->load() > 0.5f;

    // LFOs (global)
    lfo1.setRate(parameters.getRawParameterValue("lfo1_rate")->load());
    lfo1.setDepth(parameters.getRawParameterValue("lfo1_depth")->load());
    lfo1.setWaveform(static_cast<int>(parameters.getRawParameterValue("lfo1_wave")->load()));
    bp.lfo1Target = static_cast<int>(parameters.getRawParameterValue("lfo1_target")->load());

    lfo2.setRate(parameters.getRawParameterValue("lfo2_rate")->load());
    lfo2.setDepth(parameters.getRawParameterValue("lfo2_depth")->load());
    lfo2.setWaveform(static_cast<int>(parameters.getRawParameterValue("lfo2_wave")->load()));
    bp.lfo2Target = static_cast<int>(parameters.getRawParameterValue("lfo2_target")->load());

    // Filter
    bp.filterEnabled = parameters.getRawParameterValue("filter_enabled")->load() > 0.5f;
    bp.baseCutoff = parameters.getRawParameterValue("filter_cutoff")->load();
    bp.baseReso = parameters.getRawParameterValue("filter_resonance")->load();
    bp.filterType = static_cast<int>(parameters.getRawParameterValue("filter_type")->load());
    bp.filterSlope = static_cast<int>(parameters.getRawParameterValue("filter_slope")->load());
    bp.filterMix = parameters.getRawParameterValue("filter_mix")->load();
    bp.kbdTrack = parameters.getRawParameterValue("filter_kbd_track")->load();

    // Scan
    bp.baseScan = parameters.getRawParameterValue("osc_scan")->load();

    // Engine mode — read directly from APVTS (0=Sampler, 1=Wavetable)
    int engineModeRaw = static_cast<int>(parameters.getRawParameterValue("engine_mode")->load());
    bp.engineIsWavetable = (engineModeRaw == 1);
    voiceManager.setEngineMode(bp.engineIsWavetable ? SynthVoice::EngineMode::Wavetable
                                                     : SynthVoice::EngineMode::Sampler);

    // Master volume (dB → linear)
    float masterDb = parameters.getRawParameterValue("master_vol")->load();
    float masterGain = juce::Decibels::decibelsToGain(masterDb);

    // Drift LFOs (block-rate)
    driftLfo.setEnabled(parameters.getRawParameterValue("drift_enabled")->load() > 0.5f);
    driftLfo.setAutoRegen(parameters.getRawParameterValue("drift_regen")->load() > 0.5f);
    driftLfo.setLfoRate(0, parameters.getRawParameterValue("drift1_rate")->load());
    driftLfo.setLfoDepth(0, parameters.getRawParameterValue("drift1_depth")->load());
    driftLfo.setLfoTarget(0, static_cast<int>(parameters.getRawParameterValue("drift1_target")->load()));
    driftLfo.setLfoWaveform(0, static_cast<int>(parameters.getRawParameterValue("drift1_wave")->load()));
    driftLfo.setLfoRate(1, parameters.getRawParameterValue("drift2_rate")->load());
    driftLfo.setLfoDepth(1, parameters.getRawParameterValue("drift2_depth")->load());
    driftLfo.setLfoTarget(1, static_cast<int>(parameters.getRawParameterValue("drift2_target")->load()));
    driftLfo.setLfoWaveform(1, static_cast<int>(parameters.getRawParameterValue("drift2_wave")->load()));
    driftLfo.setLfoRate(2, parameters.getRawParameterValue("drift3_rate")->load());
    driftLfo.setLfoDepth(2, parameters.getRawParameterValue("drift3_depth")->load());
    driftLfo.setLfoTarget(2, static_cast<int>(parameters.getRawParameterValue("drift3_target")->load()));
    driftLfo.setLfoWaveform(2, static_cast<int>(parameters.getRawParameterValue("drift3_wave")->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    // Apply drift to scan if targeted (APVTS target 5 = WtScan)
    bp.driftScanOffset = driftLfo.getOffsetForTarget(5); // WtScan

    // ── Sampler settings ─────────────────────────────────────────────────────
    int loopModeIdx = static_cast<int>(parameters.getRawParameterValue("loop_mode")->load());
    masterSampler.setLoopMode(static_cast<SamplePlayer::LoopMode>(loopModeIdx));
    masterSampler.setCrossfadeMs(parameters.getRawParameterValue("crossfade_ms")->load());
    masterSampler.setNormalize(parameters.getRawParameterValue("normalize")->load() > 0.5f);
    masterSampler.setLoopOptimize(parameters.getRawParameterValue("loop_optimize")->load() > 0.5f);

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    bool seqRunning = parameters.getRawParameterValue("seq_running")->load() > 0.5f;
    float seqBpm = parameters.getRawParameterValue("seq_bpm")->load();
    int seqSteps = static_cast<int>(parameters.getRawParameterValue("seq_steps")->load());
    float seqGate = parameters.getRawParameterValue("seq_gate")->load();
    int seqPreset = static_cast<int>(parameters.getRawParameterValue("seq_preset")->load());
    bool arpEnabled = parameters.getRawParameterValue("arp_enabled")->load() > 0.5f;
    int arpMode = static_cast<int>(parameters.getRawParameterValue("arp_mode")->load());
    int arpRate = static_cast<int>(parameters.getRawParameterValue("arp_rate")->load());
    int arpOctaves = static_cast<int>(parameters.getRawParameterValue("arp_octaves")->load());
    float arpGate = parameters.getRawParameterValue("arp_gate")->load();

    // Preset change detection
    if (seqPreset != lastSeqPreset)
    {
        stepSequencer.loadPreset(seqPreset);
        lastSeqPreset = seqPreset;
    }

    // Stage 1: Step sequencer
    stepSequencer.setBpm(static_cast<double>(seqBpm));
    stepSequencer.setNumSteps(seqSteps);
    stepSequencer.setDivision(static_cast<int>(parameters.getRawParameterValue("seq_division")->load()));
    stepSequencer.setGlideTime(parameters.getRawParameterValue("seq_glide_time")->load());
    stepSequencer.setAllGates(seqGate);
    if (seqRunning)
        stepSequencer.start();
    else
        stepSequencer.stop();
    stepSequencer.processBlock(buffer, midiMessages);

    // Stage 2: Arpeggiator (consumes seq note events, generates arpeggiated output)
    if (arpEnabled)
    {
        arpeggiator.setBpm(static_cast<double>(seqBpm));
        arpeggiator.setRate(arpRate);
        arpeggiator.setOctaveRange(arpOctaves);
        arpeggiator.setMode(static_cast<T5ynthArpeggiator::Mode>(arpMode));
        arpeggiator.setGate(arpGate);

        juce::MidiBuffer filtered;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                arpeggiator.setBaseNote(msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
            {
                // When seq is running, don't kill arp on seq gate-offs —
                // arp runs continuously, seq just updates the base note
                if (!seqRunning)
                    arpeggiator.stopArp();
            }
            else
                filtered.addEvent(msg, metadata.samplePosition);
        }
        midiMessages.swapWith(filtered);
        arpeggiator.processBlock(buffer, midiMessages);
    }
    else
    {
        // Clean up any hanging arp note before resetting
        if (arpeggiator.getLastPlayedNote() >= 0)
            midiMessages.addEvent(juce::MidiMessage::noteOff(1, arpeggiator.getLastPlayedNote()), 0);
        arpeggiator.reset();
    }

    // Consume bar-start flag (for future background regen integration)
    stepSequencer.barStartFlag.exchange(false);

    // ── MIDI → VoiceManager (polyphonic) ──────────────────────────────────
    bool lfo1TrigMode = static_cast<int>(parameters.getRawParameterValue("lfo1_mode")->load()) == 1;
    bool lfo2TrigMode = static_cast<int>(parameters.getRawParameterValue("lfo2_mode")->load()) == 1;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            float velocity = msg.getFloatVelocity();
            bool isGlide = (msg.getChannel() == 2);

            lastMidiNote.store(note, std::memory_order_relaxed);
            lastMidiVelocity.store(juce::roundToInt(velocity * 127.0f), std::memory_order_relaxed);
            lastMidiNoteOn.store(true, std::memory_order_relaxed);

            voiceManager.noteOn(note, velocity, isGlide, stepSequencer.getGlideTime(),
                                lfo1TrigMode, lfo2TrigMode);
        }
        else if (msg.isNoteOff())
        {
            voiceManager.noteOff(msg.getNoteNumber());
            if (!voiceManager.hasActiveVoices())
                lastMidiNoteOn.store(false, std::memory_order_relaxed);
        }
    }

    // ── Audio generation via VoiceManager + global LFOs ─────────────────────
    float baseLfo1Rate = parameters.getRawParameterValue("lfo1_rate")->load();
    float baseLfo2Rate = parameters.getRawParameterValue("lfo2_rate")->load();
    float baseLfo1Depth = parameters.getRawParameterValue("lfo1_depth")->load();
    float baseLfo2Depth = parameters.getRawParameterValue("lfo2_depth")->load();

    // Re-prepare master sampler if settings changed, then distribute to all voices
    if (masterSampler.hasAudio())
    {
        if (masterSampler.needsReprepare())
            masterSampler.preparePlaybackBuffer();
        voiceManager.distributeSamplerBuffer(masterSampler);
    }

    // Pre-compute global LFO values for the block (needed by VoiceManager)
    float* lfo1Buf = lfo1Buffer.data();
    float* lfo2Buf = lfo2Buffer.data();
    for (int i = 0; i < numSamples; ++i)
    {
        float l1 = lfo1.processSample();
        float l2 = lfo2.processSample();

        // LFO cross-modulation
        if (bp.lfo1Target == 7) lfo2.setRate(baseLfo2Rate * (1.0f + l1));
        if (bp.lfo1Target == 8) lfo2.setDepth(std::max(0.0f, baseLfo2Depth + l1 * baseLfo2Depth));
        if (bp.lfo2Target == 7) lfo1.setRate(baseLfo1Rate * (1.0f + l2));
        if (bp.lfo2Target == 8) lfo1.setDepth(std::max(0.0f, baseLfo1Depth + l2 * baseLfo1Depth));

        lfo1Buf[i] = l1;
        lfo2Buf[i] = l2;
    }

    // Render all voices (summed with 1/sqrt(N) scaling)
    auto voiceOut = voiceManager.renderBlock(buffer, bp, lfo1Buf, lfo2Buf, numSamples);
    lastTriggeredNote = voiceOut.lastTriggeredNote;

    // Capture last LFO values for block-rate modulation
    float lastMod1Val = voiceOut.lastMod1Val;
    float lastMod2Val = voiceOut.lastMod2Val;
    float lastLfo1Val = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
    float lastLfo2Val = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;

    // Filter is now per-voice (in SynthVoice::renderSample)

    // ── Accumulate block-rate modulation for delay/reverb ─────────────────
    // (Pitch modulation is handled per-sample in SynthVoice::renderSample)
    float modDelayTime = 0.0f, modDelayFb = 0.0f, modDelayMix = 0.0f, modReverbMix = 0.0f;

    if (bp.mod1Target == 4) modDelayTime += lastMod1Val;
    if (bp.mod1Target == 5) modDelayFb += lastMod1Val;
    if (bp.mod1Target == 6) modDelayMix += lastMod1Val;
    if (bp.mod1Target == 7) modReverbMix += lastMod1Val;
    if (bp.mod2Target == 4) modDelayTime += lastMod2Val;
    if (bp.mod2Target == 5) modDelayFb += lastMod2Val;
    if (bp.mod2Target == 6) modDelayMix += lastMod2Val;
    if (bp.mod2Target == 7) modReverbMix += lastMod2Val;

    // Env → LFO modulation (block-rate, applied for next block's LFO computation)
    // 8=LFO1Rate, 9=LFO1Depth, 10=LFO2Rate, 11=LFO2Depth
    if (bp.mod1Target == 8)  lfo1.setRate(baseLfo1Rate * (1.0f + lastMod1Val));
    if (bp.mod1Target == 9)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod1Val * baseLfo1Depth));
    if (bp.mod1Target == 10) lfo2.setRate(baseLfo2Rate * (1.0f + lastMod1Val));
    if (bp.mod1Target == 11) lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod1Val * baseLfo2Depth));
    if (bp.mod2Target == 8)  lfo1.setRate(baseLfo1Rate * (1.0f + lastMod2Val));
    if (bp.mod2Target == 9)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod2Val * baseLfo1Depth));
    if (bp.mod2Target == 10) lfo2.setRate(baseLfo2Rate * (1.0f + lastMod2Val));
    if (bp.mod2Target == 11) lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod2Val * baseLfo2Depth));
    if (bp.lfo1Target == 3) modDelayTime += lastLfo1Val;
    if (bp.lfo1Target == 4) modDelayFb += lastLfo1Val;
    if (bp.lfo1Target == 5) modDelayMix += lastLfo1Val;
    if (bp.lfo1Target == 6) modReverbMix += lastLfo1Val;
    if (bp.lfo2Target == 3) modDelayTime += lastLfo2Val;
    if (bp.lfo2Target == 4) modDelayFb += lastLfo2Val;
    if (bp.lfo2Target == 5) modDelayMix += lastLfo2Val;
    if (bp.lfo2Target == 6) modReverbMix += lastLfo2Val;

    // ── Effects (parallel send-bus: dry + delay + reverb → limiter) ───────
    bool delayEnabled = parameters.getRawParameterValue("delay_enabled")->load() > 0.5f;
    bool reverbEnabled = parameters.getRawParameterValue("reverb_enabled")->load() > 0.5f;

    if (delayEnabled)
    {
        float baseDelayTime = parameters.getRawParameterValue("delay_time")->load();
        float baseDelayFb = parameters.getRawParameterValue("delay_feedback")->load();
        float baseDelayMix = parameters.getRawParameterValue("delay_mix")->load();
        // Apply modulation offsets to delay params
        delay.setTime(juce::jlimit(1.0f, 5000.0f, baseDelayTime * (1.0f + modDelayTime)));
        delay.setFeedback(juce::jlimit(0.0f, 0.95f, baseDelayFb + modDelayFb * baseDelayFb));
        delay.setMix(juce::jlimit(0.0f, 1.0f, baseDelayMix + modDelayMix));
        delay.setDamp(parameters.getRawParameterValue("delay_damp")->load());
    }

    if (reverbEnabled)
    {
        // Switch IR if the selection changed
        int irIndex = static_cast<int>(parameters.getRawParameterValue("reverb_ir")->load());
        if (irIndex != lastReverbIr)
        {
            const void* irData = nullptr;
            size_t irSize = 0;
            switch (irIndex)
            {
                case 0: irData = BinaryData::emt_140_plate_bright_wav;
                        irSize = static_cast<size_t>(BinaryData::emt_140_plate_bright_wavSize); break;
                case 1: irData = BinaryData::emt_140_plate_medium_wav;
                        irSize = static_cast<size_t>(BinaryData::emt_140_plate_medium_wavSize); break;
                case 2: irData = BinaryData::emt_140_plate_dark_wav;
                        irSize = static_cast<size_t>(BinaryData::emt_140_plate_dark_wavSize); break;
            }
            if (irData != nullptr)
            {
                reverb.loadImpulseResponse(irData, irSize);
                lastReverbIr = irIndex;
            }
        }
        float baseRevMix = parameters.getRawParameterValue("reverb_mix")->load();
        reverb.setMix(juce::jlimit(0.0f, 1.0f, baseRevMix + modReverbMix));
    }

    // Parallel send-bus architecture (reference useEffects.ts):
    //   source → dry(compensated) ──→ sum → limiter
    //          → delaySend ─────────→ sum
    //          → reverbSend ────────→ sum
    // Combined peak: delay ~1.7x + reverb wet additive → up to ~2.7x before master.
    //
    // Delay already implements send-bus internally (dry*comp + wet*mix).
    // When both FX are active, reverb needs the ORIGINAL source, not delay output.
    if (delayEnabled && reverbEnabled)
    {
        // Save original source for reverb
        juce::AudioBuffer<float> reverbSrc(numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSrc.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Delay modifies buffer in-place: output = dry*comp + delayed*mix
        delay.processBlock(buffer);

        // Process reverb on original source (wet-only: set mix=1 temporarily)
        float savedRevMix = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue("reverb_mix")->load() + modReverbMix);
        reverb.setMix(1.0f); // wet-only for convolution
        reverb.processBlock(reverbSrc); // reverbSrc is now 100% convolved
        reverb.setMix(savedRevMix); // restore for next block

        // Add reverb send to output: buffer += convolved * reverbMix
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSrc.getReadPointer(ch);
            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] += rev[i] * savedRevMix;
        }
    }
    else if (delayEnabled)
    {
        delay.processBlock(buffer);
    }
    else if (reverbEnabled)
    {
        reverb.processBlock(buffer);
    }

    // ── Update modulated values for GUI ghost indicators ────────────────────
    // Only store non-NaN when something actually modulates the parameter.
    constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();

    // Filter cutoff: modulated by env(target==1), LFO(target==0), or kbd track
    {
        bool filterModulated = (bp.mod1Target == 1 || bp.mod2Target == 1 ||
                                bp.lfo1Target == 0 || bp.lfo2Target == 0 ||
                                bp.kbdTrack > 0.0f) && voiceOut.hasActiveVoices;
        modulatedValues.filterCutoff.store(filterModulated ? voiceOut.lastModulatedCutoff : NO_GHOST,
                                           std::memory_order_relaxed);
    }

    // Scan: modulated by env(target==2), LFO(target==1), drift
    {
        bool scanModulated = (bp.mod1Target == 2 || bp.mod2Target == 2 ||
                              bp.lfo1Target == 1 || bp.lfo2Target == 1 ||
                              std::abs(bp.driftScanOffset) > 0.001f) && voiceOut.hasActiveVoices;
        modulatedValues.scanPosition.store(scanModulated ? voiceOut.lastModulatedScan : NO_GHOST,
                                           std::memory_order_relaxed);
    }

    // LFO1 Rate/Depth: modulated by env(8/9) or LFO cross-mod(lfo2Target==7/8)
    {
        bool lfo1RateMod  = bp.mod1Target == 8  || bp.mod2Target == 8  || bp.lfo2Target == 7;
        bool lfo1DepthMod = bp.mod1Target == 9  || bp.mod2Target == 9  || bp.lfo2Target == 8;
        modulatedValues.lfo1Rate.store(lfo1RateMod ? lfo1.getRate() : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.lfo1Depth.store(lfo1DepthMod ? lfo1.getDepth() : NO_GHOST, std::memory_order_relaxed);
    }

    // LFO2 Rate/Depth: modulated by env(10/11) or LFO cross-mod(lfo1Target==7/8)
    {
        bool lfo2RateMod  = bp.mod1Target == 10 || bp.mod2Target == 10 || bp.lfo1Target == 7;
        bool lfo2DepthMod = bp.mod1Target == 11 || bp.mod2Target == 11 || bp.lfo1Target == 8;
        modulatedValues.lfo2Rate.store(lfo2RateMod ? lfo2.getRate() : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.lfo2Depth.store(lfo2DepthMod ? lfo2.getDepth() : NO_GHOST, std::memory_order_relaxed);
    }

    // Delay/Reverb: modulated by env or LFO targeting them
    {
        bool dlyTimeMod = modDelayTime != 0.0f;
        bool dlyFbMod   = modDelayFb != 0.0f;
        bool dlyMixMod  = modDelayMix != 0.0f;
        bool revMixMod  = modReverbMix != 0.0f;
        modulatedValues.delayTime.store(dlyTimeMod && delayEnabled
            ? juce::jlimit(1.0f, 5000.0f, parameters.getRawParameterValue("delay_time")->load() * (1.0f + modDelayTime))
            : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.delayFeedback.store(dlyFbMod && delayEnabled
            ? juce::jlimit(0.0f, 0.95f, parameters.getRawParameterValue("delay_feedback")->load() * (1.0f + modDelayFb))
            : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.delayMix.store(dlyMixMod && delayEnabled
            ? juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue("delay_mix")->load() + modDelayMix)
            : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.reverbMix.store(revMixMod && reverbEnabled
            ? juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue("reverb_mix")->load() + modReverbMix)
            : NO_GHOST, std::memory_order_relaxed);
    }

    // ── Master volume ───────────────────────────────────────────────────────
    buffer.applyGain(masterGain);

    // ── Limiter (always on, internal safety) ────────────────────────────────
    limiter.setThreshold(parameters.getRawParameterValue("limiter_thresh")->load());
    limiter.setRelease(parameters.getRawParameterValue("limiter_release")->load());
    limiter.processBlock(buffer);
}

bool T5ynthProcessor::isWavetableMode() const
{
    return static_cast<int>(parameters.getRawParameterValue("engine_mode")->load()) == 1;
}

bool T5ynthProcessor::isSamplerMode() const
{
    return !isWavetableMode();
}

void T5ynthProcessor::loadGeneratedAudio(const juce::AudioBuffer<float>& audioBuffer, double sr)
{
    // Feed the sampler/voice chain
    masterSampler.loadBuffer(audioBuffer, sr);

    // Wavetable extraction respects the bracket region
    float extractStart = masterSampler.getLoopStart();
    float extractEnd   = masterSampler.getLoopEnd();
    masterOsc.extractFramesFromBuffer(audioBuffer, sr, extractStart, extractEnd);

    voiceManager.distributeSamplerBuffer(masterSampler);
    voiceManager.distributeWavetableFrames(masterOsc);

    // Snapshot channel 0 for waveform display
    if (audioBuffer.getNumChannels() > 0 && audioBuffer.getNumSamples() > 0)
    {
        waveformSnapshot.setSize(1, audioBuffer.getNumSamples(), false, false, true);
        waveformSnapshot.copyFrom(0, 0, audioBuffer, 0, 0, audioBuffer.getNumSamples());
        newWaveformReady.store(true, std::memory_order_release);
    }
}

void T5ynthProcessor::reextractWavetable()
{
    if (waveformSnapshot.getNumSamples() > 0)
    {
        float start = masterSampler.getLoopStart();
        float end   = masterSampler.getLoopEnd();
        masterOsc.extractFramesFromBuffer(waveformSnapshot, getSampleRate(), start, end);
        voiceManager.distributeWavetableFrames(masterOsc);
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

// ═══════════════════════════════════════════════════════════════════
// JSON Preset Import/Export (compatible with Vue reference format)
// ═══════════════════════════════════════════════════════════════════

// Conversion helpers matching useFilter.ts:
//   normalizedToFreq(n) = 20 * pow(1000, n)     // 0→20Hz, 1→20kHz
//   freqToNormalized(f) = log(f/20) / log(1000)
static float cutoffNormToHz(float n) { return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, n)); }
static float cutoffHzToNorm(float hz) { return std::log(juce::jlimit(20.0f, 20000.0f, hz) / 20.0f) / std::log(1000.0f); }

// String↔index mappings for preset fields
static int filterTypeFromString(const juce::String& s) {
    if (s == "highpass") return 1;
    if (s == "bandpass") return 2;
    return 0; // lowpass
}
static juce::String filterTypeToString(int i) {
    if (i == 1) return "highpass";
    if (i == 2) return "bandpass";
    return "lowpass";
}

static int filterSlopeFromString(const juce::String& s) { return s == "24" ? 1 : 0; }
static juce::String filterSlopeToString(int i) { return i == 1 ? "24" : "12"; }

static int reverbVariantFromString(const juce::String& s) {
    if (s == "bright") return 0;
    if (s == "dark") return 2;
    return 1; // medium
}
static juce::String reverbVariantToString(int i) {
    if (i == 0) return "bright";
    if (i == 2) return "dark";
    return "medium";
}

// Mod target: reference uses strings, JUCE uses choice index
// Current JUCE env targets: 0=DCA, 1=Filter, 2=Scan, 3=None
// Current JUCE LFO targets: 0=Filter, 1=Scan, 2=Alpha, 3=None
// Reference targets: none, dca, dcf_cutoff, pitch, delay_time, delay_feedback, delay_mix,
//                    reverb_mix, lfo1_rate, lfo2_rate, lfo1_depth, lfo2_depth, wt_scan
// Env targets: 0=DCA, 1=Filter, 2=Scan, 3=Pitch, 4=DlyTime, 5=DlyFB, 6=DlyMix, 7=RevMix, 8=None
static int envTargetFromString(const juce::String& s) {
    if (s == "dca") return 0;
    if (s == "dcf_cutoff") return 1;
    if (s == "wt_scan") return 2;
    if (s == "pitch") return 3;
    if (s == "delay_time") return 4;
    if (s == "delay_feedback") return 5;
    if (s == "delay_mix") return 6;
    if (s == "reverb_mix") return 7;
    return 8; // none
}
static juce::String envTargetToString(int i) {
    const char* names[] = {"dca","dcf_cutoff","wt_scan","pitch","delay_time","delay_feedback","delay_mix","reverb_mix","none"};
    return (i >= 0 && i <= 8) ? names[i] : "none";
}

// LFO targets: 0=Filter, 1=Scan, 2=Pitch, 3=DlyTime, 4=DlyFB, 5=DlyMix, 6=RevMix,
//              7=LFO2Rate/LFO1Rate, 8=LFO2Depth/LFO1Depth, 9=None
static int lfoTargetFromString(const juce::String& s) {
    if (s == "dcf_cutoff") return 0;
    if (s == "wt_scan") return 1;
    if (s == "pitch") return 2;
    if (s == "delay_time") return 3;
    if (s == "delay_feedback") return 4;
    if (s == "delay_mix") return 5;
    if (s == "reverb_mix") return 6;
    if (s == "lfo1_rate" || s == "lfo2_rate") return 7;
    if (s == "lfo1_depth" || s == "lfo2_depth") return 8;
    return 9; // none
}
static juce::String lfoTargetToString(int i) {
    const char* names[] = {"dcf_cutoff","wt_scan","pitch","delay_time","delay_feedback","delay_mix","reverb_mix","lfo_rate","lfo_depth","none"};
    return (i >= 0 && i <= 9) ? names[i] : "none";
}

static int lfoWaveFromString(const juce::String& s) {
    if (s == "triangle") return 1;
    if (s == "sawtooth") return 2;
    if (s == "square") return 3;
    return 0; // sine
}
static juce::String lfoWaveToString(int i) {
    if (i == 1) return "triangle";
    if (i == 2) return "sawtooth";
    if (i == 3) return "square";
    return "sine";
}

static int lfoModeFromString(const juce::String& s) { return s == "trigger" ? 1 : 0; }
static juce::String lfoModeToString(int i) { return i == 1 ? "trigger" : "free"; }

static int driftTargetFromString(const juce::String& s) {
    if (s == "alpha") return 1;
    if (s == "sem_axis_1") return 2;
    if (s == "sem_axis_2") return 3;
    if (s == "sem_axis_3") return 4;
    if (s == "wt_scan") return 5;
    return 0; // none
}
static juce::String driftTargetToString(int i) {
    if (i == 1) return "alpha";
    if (i == 2) return "sem_axis_1";
    if (i == 3) return "sem_axis_2";
    if (i == 4) return "sem_axis_3";
    if (i == 5) return "wt_scan";
    return "none";
}

static int driftWaveFromString(const juce::String& s) {
    if (s == "triangle") return 1;
    if (s == "sawtooth") return 2;
    if (s == "square") return 3;
    return 0; // sine
}
static juce::String driftWaveToString(int i) {
    if (i == 1) return "triangle";
    if (i == 2) return "sawtooth";
    if (i == 3) return "square";
    return "sine";
}

// Helper to safely set a parameter value
static void setParam(juce::AudioProcessorValueTreeState& p, const juce::String& id, float val) {
    if (auto* param = p.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(val));
}

juce::String T5ynthProcessor::exportJsonPreset() const
{
    auto* p = const_cast<juce::AudioProcessorValueTreeState*>(&parameters);
    auto get = [&](const juce::String& id) { return p->getRawParameterValue(id)->load(); };

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("name", "T5ynth Export");
    root->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));

    // Synth params
    juce::DynamicObject::Ptr synth = new juce::DynamicObject();
    synth->setProperty("promptA", ""); // prompts are GUI-only, not in APVTS
    synth->setProperty("promptB", "");
    synth->setProperty("alpha", get("gen_alpha"));
    synth->setProperty("magnitude", get("gen_magnitude"));
    synth->setProperty("noise", get("gen_noise"));
    synth->setProperty("duration", get("gen_duration"));
    synth->setProperty("startPosition", get("gen_start"));
    synth->setProperty("steps", static_cast<int>(get("gen_steps")));
    synth->setProperty("cfg", get("gen_cfg"));
    synth->setProperty("seed", static_cast<int>(get("gen_seed")));
    synth->setProperty("device", lastDevice);
    root->setProperty("synth", synth.get());

    // Engine
    juce::DynamicObject::Ptr engine = new juce::DynamicObject();
    engine->setProperty("mode", isSamplerMode() ? "sampler" : "wavetable");
    int lm = static_cast<int>(get("loop_mode"));
    engine->setProperty("loopMode", lm == 0 ? "oneshot" : (lm == 1 ? "loop" : "pingpong"));
    engine->setProperty("loopStartFrac", static_cast<double>(masterSampler.getLoopStart()));
    engine->setProperty("loopEndFrac", static_cast<double>(masterSampler.getLoopEnd()));
    engine->setProperty("crossfadeMs", get("crossfade_ms"));
    root->setProperty("engine", engine.get());

    // Modulation: 3 envelopes
    juce::DynamicObject::Ptr modObj = new juce::DynamicObject();
    juce::Array<juce::var> envArr;
    const juce::String envPrefixes[] = {"amp_", "mod1_", "mod2_"};
    const juce::String envTargetIds[] = {"", "mod1_target", "mod2_target"};
    for (int i = 0; i < 3; ++i)
    {
        juce::DynamicObject::Ptr env = new juce::DynamicObject();
        env->setProperty("attackMs", get(envPrefixes[i] + "attack"));
        env->setProperty("decayMs", get(envPrefixes[i] + "decay"));
        env->setProperty("sustain", get(envPrefixes[i] + "sustain"));
        env->setProperty("releaseMs", get(envPrefixes[i] + "release"));
        env->setProperty("amount", get(envPrefixes[i] + "amount"));
        if (i == 0)
            env->setProperty("target", "dca"); // amp env is always DCA
        else
            env->setProperty("target", envTargetToString(static_cast<int>(get(envTargetIds[i]))));
        env->setProperty("loop", get(envPrefixes[i] + "loop") > 0.5f);
        envArr.add(env.get());
    }
    modObj->setProperty("envs", envArr);

    // Modulation: 2 LFOs
    juce::Array<juce::var> lfoArr;
    for (int i = 0; i < 2; ++i)
    {
        juce::DynamicObject::Ptr lfo = new juce::DynamicObject();
        juce::String pre = "lfo" + juce::String(i + 1) + "_";
        lfo->setProperty("rate", get(pre + "rate"));
        lfo->setProperty("depth", get(pre + "depth"));
        lfo->setProperty("waveform", lfoWaveToString(static_cast<int>(get(pre + "wave"))));
        lfo->setProperty("target", lfoTargetToString(static_cast<int>(get(pre + "target"))));
        lfo->setProperty("mode", lfoModeToString(static_cast<int>(get(pre + "mode"))));
        lfoArr.add(lfo.get());
    }
    modObj->setProperty("lfos", lfoArr);
    root->setProperty("modulation", modObj.get());

    // Drift LFOs
    juce::Array<juce::var> driftArr;
    for (int i = 0; i < 3; ++i)
    {
        juce::DynamicObject::Ptr d = new juce::DynamicObject();
        juce::String pre = "drift" + juce::String(i + 1) + "_";
        d->setProperty("rate", get(pre + "rate"));
        d->setProperty("depth", get(pre + "depth"));
        d->setProperty("waveform", driftWaveToString(static_cast<int>(get(pre + "wave"))));
        d->setProperty("target", driftTargetToString(static_cast<int>(get(pre + "target"))));
        driftArr.add(d.get());
    }
    root->setProperty("driftLfos", driftArr);
    root->setProperty("autoRegen", get("drift_regen") > 0.5f);

    // Wavetable
    juce::DynamicObject::Ptr wt = new juce::DynamicObject();
    wt->setProperty("scan", get("osc_scan"));
    root->setProperty("wavetable", wt.get());

    // Effects
    juce::DynamicObject::Ptr fx = new juce::DynamicObject();
    fx->setProperty("delayEnabled", get("delay_enabled") > 0.5f);
    fx->setProperty("delayTimeMs", get("delay_time"));
    fx->setProperty("delayFeedback", get("delay_feedback"));
    fx->setProperty("delayMix", get("delay_mix"));
    fx->setProperty("delayDamp", get("delay_damp"));
    fx->setProperty("reverbEnabled", get("reverb_enabled") > 0.5f);
    fx->setProperty("reverbMix", get("reverb_mix"));
    fx->setProperty("reverbVariant", reverbVariantToString(static_cast<int>(get("reverb_ir"))));
    root->setProperty("effects", fx.get());

    // Filter — store NORMALIZED cutoff (0-1), not Hz
    juce::DynamicObject::Ptr filt = new juce::DynamicObject();
    filt->setProperty("enabled", get("filter_enabled") > 0.5f);
    filt->setProperty("type", filterTypeToString(static_cast<int>(get("filter_type"))));
    filt->setProperty("slope", filterSlopeToString(static_cast<int>(get("filter_slope"))));
    filt->setProperty("cutoff", cutoffHzToNorm(get("filter_cutoff")));
    filt->setProperty("resonance", get("filter_resonance"));
    filt->setProperty("mix", get("filter_mix"));
    filt->setProperty("kbdTrack", get("filter_kbd_track"));
    root->setProperty("filter", filt.get());

    // Sequencer
    juce::DynamicObject::Ptr seq = new juce::DynamicObject();
    seq->setProperty("enabled", get("seq_running") > 0.5f);
    seq->setProperty("bpm", get("seq_bpm"));
    int stepCount = static_cast<int>(get("seq_steps"));
    seq->setProperty("stepCount", stepCount);
    juce::Array<juce::var> stepArr;
    for (int i = 0; i < stepCount; ++i)
    {
        const auto& step = stepSequencer.getStep(i);
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("active", step.enabled);
        s->setProperty("semitone", step.note - 60); // MIDI → semitone offset from C3
        s->setProperty("velocity", static_cast<double>(step.velocity));
        s->setProperty("gate", static_cast<double>(step.gate));
        s->setProperty("glide", step.glide);
        stepArr.add(s.get());
    }
    seq->setProperty("steps", stepArr);
    root->setProperty("sequencer", seq.get());

    // Arpeggiator
    juce::DynamicObject::Ptr arp = new juce::DynamicObject();
    int seqMode = static_cast<int>(get("seq_mode"));
    arp->setProperty("enabled", seqMode >= 1);
    const char* arpPatterns[] = {"up", "down", "updown", "random"};
    arp->setProperty("pattern", seqMode >= 1 ? arpPatterns[std::min(seqMode - 1, 3)] : "up");
    // Export arp rate as musical division string
    const char* arpRates[] = {"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"};
    int arpRateIdx = static_cast<int>(get("arp_rate"));
    arp->setProperty("rate", arpRateIdx >= 0 && arpRateIdx < 7 ? arpRates[arpRateIdx] : "1/16");
    arp->setProperty("octaveRange", static_cast<int>(get("arp_octaves")));
    root->setProperty("arpeggiator", arp.get());

    return juce::JSON::toString(root.get(), true);
}

bool T5ynthProcessor::importJsonPreset(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    // ── Synth params ──
    if (auto* synth = root->getProperty("synth").getDynamicObject())
    {
        setParam(parameters, "gen_alpha", static_cast<float>(synth->getProperty("alpha")));
        setParam(parameters, "gen_magnitude", static_cast<float>(synth->getProperty("magnitude")));
        setParam(parameters, "gen_noise", static_cast<float>(synth->getProperty("noise")));
        setParam(parameters, "gen_duration", static_cast<float>(synth->getProperty("duration")));
        setParam(parameters, "gen_start", static_cast<float>(synth->getProperty("startPosition")));
        setParam(parameters, "gen_steps", static_cast<float>(static_cast<int>(synth->getProperty("steps"))));
        setParam(parameters, "gen_cfg", static_cast<float>(synth->getProperty("cfg")));
        setParam(parameters, "gen_seed", static_cast<float>(static_cast<int>(synth->getProperty("seed"))));
    }

    // ── Engine ──
    if (auto* engine = root->getProperty("engine").getDynamicObject())
    {
        juce::String mode = engine->getProperty("mode").toString();
        // Accept both "looper" (legacy) and "sampler" (current)
        setParam(parameters, "engine_mode", mode == "wavetable" ? 1.0f : 0.0f);

        juce::String lm = engine->getProperty("loopMode").toString();
        int loopModeIdx = (lm == "loop") ? 1 : (lm == "pingpong" ? 2 : 0);
        setParam(parameters, "loop_mode", static_cast<float>(loopModeIdx));

        masterSampler.setLoopStart(static_cast<float>(engine->getProperty("loopStartFrac")));
        masterSampler.setLoopEnd(static_cast<float>(engine->getProperty("loopEndFrac")));
        setParam(parameters, "crossfade_ms", static_cast<float>(engine->getProperty("crossfadeMs")));
    }

    // ── Modulation ──
    if (auto* mod = root->getProperty("modulation").getDynamicObject())
    {
        auto* envsArr = mod->getProperty("envs").getArray();
        if (envsArr)
        {
            const juce::String envPrefixes[] = {"amp_", "mod1_", "mod2_"};
            const juce::String envTargetIds[] = {"", "mod1_target", "mod2_target"};
            for (int i = 0; i < std::min(3, envsArr->size()); ++i)
            {
                auto* env = (*envsArr)[i].getDynamicObject();
                if (!env) continue;
                setParam(parameters, envPrefixes[i] + "attack", static_cast<float>(env->getProperty("attackMs")));
                setParam(parameters, envPrefixes[i] + "decay", static_cast<float>(env->getProperty("decayMs")));
                setParam(parameters, envPrefixes[i] + "sustain", static_cast<float>(env->getProperty("sustain")));
                setParam(parameters, envPrefixes[i] + "release", static_cast<float>(env->getProperty("releaseMs")));
                setParam(parameters, envPrefixes[i] + "amount", static_cast<float>(env->getProperty("amount")));
                setParam(parameters, envPrefixes[i] + "loop", env->getProperty("loop") ? 1.0f : 0.0f);
                if (i > 0)
                    setParam(parameters, envTargetIds[i], static_cast<float>(envTargetFromString(env->getProperty("target").toString())));
            }
        }

        auto* lfosArr = mod->getProperty("lfos").getArray();
        if (lfosArr)
        {
            for (int i = 0; i < std::min(2, lfosArr->size()); ++i)
            {
                auto* lfo = (*lfosArr)[i].getDynamicObject();
                if (!lfo) continue;
                juce::String pre = "lfo" + juce::String(i + 1) + "_";
                setParam(parameters, pre + "rate", static_cast<float>(lfo->getProperty("rate")));
                setParam(parameters, pre + "depth", static_cast<float>(lfo->getProperty("depth")));
                setParam(parameters, pre + "wave", static_cast<float>(lfoWaveFromString(lfo->getProperty("waveform").toString())));
                setParam(parameters, pre + "target", static_cast<float>(lfoTargetFromString(lfo->getProperty("target").toString())));
                setParam(parameters, pre + "mode", static_cast<float>(lfoModeFromString(lfo->getProperty("mode").toString())));
            }
        }
    }

    // ── Drift LFOs ──
    auto* driftArr = root->getProperty("driftLfos").getArray();
    if (driftArr)
    {
        for (int i = 0; i < std::min(3, driftArr->size()); ++i)
        {
            auto* d = (*driftArr)[i].getDynamicObject();
            if (!d) continue;
            juce::String pre = "drift" + juce::String(i + 1) + "_";
            setParam(parameters, pre + "rate", static_cast<float>(d->getProperty("rate")));
            setParam(parameters, pre + "depth", static_cast<float>(d->getProperty("depth")));
            setParam(parameters, pre + "wave", static_cast<float>(driftWaveFromString(d->getProperty("waveform").toString())));
            setParam(parameters, pre + "target", static_cast<float>(driftTargetFromString(d->getProperty("target").toString())));
        }
    }
    if (root->hasProperty("autoRegen"))
        setParam(parameters, "drift_regen", root->getProperty("autoRegen") ? 1.0f : 0.0f);

    // ── Wavetable ──
    if (auto* wt = root->getProperty("wavetable").getDynamicObject())
    {
        if (wt->hasProperty("scan"))
            setParam(parameters, "osc_scan", static_cast<float>(wt->getProperty("scan")));
    }

    // ── Effects ──
    if (auto* fx = root->getProperty("effects").getDynamicObject())
    {
        setParam(parameters, "delay_enabled", fx->getProperty("delayEnabled") ? 1.0f : 0.0f);
        setParam(parameters, "delay_time", static_cast<float>(fx->getProperty("delayTimeMs")));
        setParam(parameters, "delay_feedback", static_cast<float>(fx->getProperty("delayFeedback")));
        setParam(parameters, "delay_mix", static_cast<float>(fx->getProperty("delayMix")));
        if (fx->hasProperty("delayDamp"))
            setParam(parameters, "delay_damp", static_cast<float>(fx->getProperty("delayDamp")));
        setParam(parameters, "reverb_enabled", fx->getProperty("reverbEnabled") ? 1.0f : 0.0f);
        setParam(parameters, "reverb_mix", static_cast<float>(fx->getProperty("reverbMix")));
        if (fx->hasProperty("reverbVariant"))
            setParam(parameters, "reverb_ir", static_cast<float>(reverbVariantFromString(fx->getProperty("reverbVariant").toString())));
    }

    // ── Filter — CRITICAL: cutoff is normalized 0-1, convert to Hz ──
    if (auto* filt = root->getProperty("filter").getDynamicObject())
    {
        setParam(parameters, "filter_enabled", filt->getProperty("enabled") ? 1.0f : 0.0f);
        setParam(parameters, "filter_type", static_cast<float>(filterTypeFromString(filt->getProperty("type").toString())));
        if (filt->hasProperty("slope"))
            setParam(parameters, "filter_slope", static_cast<float>(filterSlopeFromString(filt->getProperty("slope").toString())));
        // Convert normalized cutoff to Hz: 20 * pow(1000, n)
        float cutoffNorm = static_cast<float>(filt->getProperty("cutoff"));
        setParam(parameters, "filter_cutoff", cutoffNormToHz(cutoffNorm));
        setParam(parameters, "filter_resonance", static_cast<float>(filt->getProperty("resonance")));
        if (filt->hasProperty("mix"))
            setParam(parameters, "filter_mix", static_cast<float>(filt->getProperty("mix")));
        if (filt->hasProperty("kbdTrack"))
            setParam(parameters, "filter_kbd_track", static_cast<float>(filt->getProperty("kbdTrack")));
    }

    // ── Sequencer ──
    if (auto* seq = root->getProperty("sequencer").getDynamicObject())
    {
        bool seqEnabled = seq->getProperty("enabled");
        setParam(parameters, "seq_running", seqEnabled ? 1.0f : 0.0f);
        setParam(parameters, "seq_bpm", static_cast<float>(seq->getProperty("bpm")));
        int stepCount = static_cast<int>(seq->getProperty("stepCount"));
        setParam(parameters, "seq_steps", static_cast<float>(stepCount));
        stepSequencer.setNumSteps(stepCount);

        auto* stepsArr = seq->getProperty("steps").getArray();
        if (stepsArr)
        {
            for (int i = 0; i < std::min(stepCount, stepsArr->size()); ++i)
            {
                auto* s = (*stepsArr)[i].getDynamicObject();
                if (!s) continue;
                int semitone = static_cast<int>(s->getProperty("semitone"));
                stepSequencer.setStepNote(i, 60 + semitone); // C3 + semitone offset
                stepSequencer.setStepVelocity(i, static_cast<float>(s->getProperty("velocity")));
                stepSequencer.setStepEnabled(i, static_cast<bool>(s->getProperty("active")));
                if (s->hasProperty("gate"))
                    stepSequencer.setStepGate(i, static_cast<float>(s->getProperty("gate")));
                if (s->hasProperty("glide"))
                    stepSequencer.setStepGlide(i, static_cast<bool>(s->getProperty("glide")));
            }
        }
    }

    // ── Arpeggiator ──
    if (auto* arp = root->getProperty("arpeggiator").getDynamicObject())
    {
        bool arpEnabled = arp->getProperty("enabled");
        if (arpEnabled)
        {
            juce::String pattern = arp->getProperty("pattern").toString();
            int mode = 1; // default: Up
            if (pattern == "down") mode = 2;
            else if (pattern == "updown") mode = 3;
            else if (pattern == "random") mode = 4;
            setParam(parameters, "seq_mode", static_cast<float>(mode));
        }
        else
        {
            setParam(parameters, "seq_mode", 0.0f); // Seq only, no arp
        }
        setParam(parameters, "arp_octaves", static_cast<float>(static_cast<int>(arp->getProperty("octaveRange"))));
        if (arp->hasProperty("rate"))
        {
            juce::String rateStr = arp->getProperty("rate").toString();
            int rateIdx = 2; // default 1/16
            if (rateStr == "1/4") rateIdx = 0;
            else if (rateStr == "1/8") rateIdx = 1;
            else if (rateStr == "1/16") rateIdx = 2;
            else if (rateStr == "1/32") rateIdx = 3;
            else if (rateStr == "1/4T") rateIdx = 4;
            else if (rateStr == "1/8T") rateIdx = 5;
            else if (rateStr == "1/16T") rateIdx = 6;
            setParam(parameters, "arp_rate", static_cast<float>(rateIdx));
        }
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new T5ynthProcessor();
}
