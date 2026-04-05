#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

T5ynthProcessor::T5ynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "T5ynth", createParameterLayout())
{
}

T5ynthProcessor::~T5ynthProcessor() = default;

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

    // Voice count: Mono, 4, 6, 8, 12, 16
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"voice_count", 1}, "Voice Count",
        juce::StringArray{"Mono", "4", "6", "8", "12", "16"}, 3)); // default 8

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
        juce::StringArray{"Off", "Lowpass", "Highpass", "Bandpass"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filter_slope", 1}, "Filter Slope",
        juce::StringArray{"6dB", "12dB", "18dB", "24dB"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_mix", 1}, "Filter Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter_kbd_track", 1}, "Filter Kbd Track",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_time", 1}, "Delay Time",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.35f), 250.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_feedback", 1}, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delay_mix", 1}, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.3f));

    // Reverb
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"reverb_mix", 1}, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.25f));

    // Algorithmic reverb parameters (only active when reverb_type == Algo)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"algo_room", 1}, "Algo Room",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"algo_damping", 1}, "Algo Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"algo_width", 1}, "Algo Width",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Generation
    // Alpha: quadratic curve around 0 for fine control near center (±0.15 sensitive zone)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_alpha", 1}, "Alpha",
        juce::NormalisableRange<float>(-2.0f, 2.0f,
            [](float s, float e, float n) {
                float c = n * 2.0f - 1.0f;
                float curved = (c >= 0.0f ? 1.0f : -1.0f) * c * c;
                return s + (e - s) * (curved * 0.5f + 0.5f);
            },
            [](float s, float e, float v) {
                float norm = (v - s) / (e - s);
                float c = norm * 2.0f - 1.0f;
                float uncurved = (c >= 0.0f ? 1.0f : -1.0f) * std::sqrt(std::abs(c));
                return uncurved * 0.5f + 0.5f;
            },
            [](float s, float e, float v) { return juce::jlimit(s, e, v); }
        ), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_magnitude", 1}, "Magnitude",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gen_noise", 1}, "Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.0f));
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
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift_regen", 1}, "Regenerate",
        juce::StringArray{"Manual", "Auto", "1st Bar"}, 0));
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
        juce::StringArray{"---", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan", "Filter", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix", "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift2_target", 1}, "Drift2 Target",
        juce::StringArray{"---", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan", "Filter", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix", "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift1_wave", 1}, "Drift1 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Sq"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift2_wave", 1}, "Drift2 Wave",
        juce::StringArray{"Sine", "Tri", "Saw", "Sq"}, 0));

    // Drift 3 target + waveform (was missing — drift3 rate/depth existed but had no target/wave)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"drift3_target", 1}, "Drift3 Target",
        juce::StringArray{"---", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan", "Filter", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix", "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 0));
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

    // Velocity sensitivity (per envelope, 0=fixed, 1=full velocity)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amp_vel_sens", 1}, "Amp Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod1_vel_sens", 1}, "Mod1 Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mod2_vel_sens", 1}, "Mod2 Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // ENV Loop (per envelope)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"amp_loop", 1}, "Amp Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"mod1_loop", 1}, "Mod1 Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"mod2_loop", 1}, "Mod2 Loop", false));

    // ENV targets:
    // 0=None, 1=DCA, 2=Filter, 3=Scan, 4=Pitch, 5=DelayTime, 6=DelayFB, 7=DelayMix, 8=ReverbMix,
    // 9=LFO1Rate, 10=LFO1Depth, 11=LFO2Rate, 12=LFO2Depth
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod1_target", 1}, "Mod1 Target",
        juce::StringArray{"---", "DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mod2_target", 1}, "Mod2 Target",
        juce::StringArray{"---", "DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth"}, 0));
    // LFO targets: 0=None, 1=Filter, 2=Scan, 3=Pitch, 4=DlyTime, 5=DlyFB, 6=DlyMix, 7=RevMix,
    //              8=ENV1Amt, 9=ENV2Amt, 10=ENV3Amt
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo1_target", 1}, "LFO1 Target",
        juce::StringArray{"---", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"lfo2_target", 1}, "LFO2 Target",
        juce::StringArray{"---", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                          "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 0));

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
        juce::StringArray{"One-shot", "Loop", "Ping-Pong"}, 0));
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
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"delay_type", 1}, "Delay Type",
        juce::StringArray{"Off", "Stereo"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"reverb_type", 1}, "Reverb Type",
        juce::StringArray{"Off", "Dark", "Medium", "Bright", "Algo"}, 0));

    // Limiter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"limiter_thresh", 1}, "Limiter Threshold",
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), -3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"limiter_release", 1}, "Limiter Release",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.3f), 100.0f));

    // (reverb_ir merged into reverb_type switchbox)

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
        juce::StringArray{"1/1", "1/2", "1/4", "1/8", "1/16"}, 4)); // default 1/16
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
    // Arp mode (Off = disabled, rest = enabled with that pattern)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"arp_mode", 1}, "Arp Mode",
        juce::StringArray{"Off", "Up", "Down", "UpDown", "Random"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"arp_gate", 1}, "Arp Gate",
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.01f), 0.8f));
    // Global seq gate + preset
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"seq_gate", 1}, "Seq Gate",
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"seq_preset", 1}, "Seq Preset",
        juce::StringArray{"Octave Bounce", "Wide Leap", "Off-Beat Minor", "Glide Groove", "Sparse Stab",
                          "Rising Arc", "Scatter", "Chromatic", "Bass Walk", "Gated Pulse"}, 0));

    // HF boost: compensate VAE decoder high-frequency rolloff
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"gen_hf_boost", 1}, "HF Boost", true));

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
    algoReverb.prepare(sampleRate, samplesPerBlock);
    // Load default IR (medium plate)
    reverb.loadImpulseResponse(BinaryData::emt_140_plate_medium_wav,
                               static_cast<size_t>(BinaryData::emt_140_plate_medium_wavSize));
    lastReverbIr = 1; // 0=Bright, 1=Medium, 2=Dark
    limiter.prepare(sampleRate, samplesPerBlock);
    stepSequencer.prepare(sampleRate, samplesPerBlock);
    arpeggiator.prepare(sampleRate, samplesPerBlock);
    lfo1Buffer.resize(static_cast<size_t>(samplesPerBlock));
    lfo2Buffer.resize(static_cast<size_t>(samplesPerBlock));
    reverbSendBuffer.setSize(2, samplesPerBlock);

    silentBlockCount = 0;
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

    // ── Idle detection ──────────────────────────────────────────────────────
    bool seqRunning = parameters.getRawParameterValue("seq_running")->load() > 0.5f;
    bool hasActivity = voiceManager.hasActiveVoices()
                       || !midiMessages.isEmpty()
                       || seqRunning;

    if (hasActivity)
        silentBlockCount = 0;
    else
        ++silentBlockCount;

    // PHASE 2: Deep idle (tails fully decayed) → buffer already cleared, just return
    if (silentBlockCount > TAIL_BLOCKS)
    {
        audioIdle.store(true, std::memory_order_relaxed);
        // Keep free-running LFOs phase-accurate
        lfo1.advancePhase(numSamples);
        lfo2.advancePhase(numSamples);
        return;
    }
    audioIdle.store(false, std::memory_order_relaxed);

    // ── GAIN STAGING ────────────────────────────────────────────────────────
    // Per Voice: Osc +-1.0 → VCA up to +-4.0 → Filter (gain-neutral, reso +12dB)
    // Sum:       N voices * 1/sqrt(N) scaling → ~constant perceived loudness
    // Post-Sum:  Delay+Reverb up to ~2.7x → Master 0dB max → Limiter -3dB
    // ────────────────────────────────────────────────────────────────────────

    // ── Voice count ──────────────────────────────────────────────────────────
    {
        static constexpr int voiceCounts[] = { 1, 4, 6, 8, 12, 16 };
        int vcIdx = static_cast<int>(parameters.getRawParameterValue("voice_count")->load());
        voiceManager.setVoiceLimit(voiceCounts[juce::jlimit(0, 5, vcIdx)]);
    }

    // ── Read all parameters into BlockParams ──────────────────────────────────
    BlockParams bp;
    bp.ampAttack  = parameters.getRawParameterValue("amp_attack")->load();
    bp.ampDecay   = parameters.getRawParameterValue("amp_decay")->load();
    bp.ampSustain = parameters.getRawParameterValue("amp_sustain")->load();
    bp.ampRelease = parameters.getRawParameterValue("amp_release")->load();
    bp.ampAmount  = parameters.getRawParameterValue("amp_amount")->load();
    bp.ampVelSens = parameters.getRawParameterValue("amp_vel_sens")->load();
    bp.ampLoop    = parameters.getRawParameterValue("amp_loop")->load() > 0.5f;

    bp.mod1Attack  = parameters.getRawParameterValue("mod1_attack")->load();
    bp.mod1Decay   = parameters.getRawParameterValue("mod1_decay")->load();
    bp.mod1Sustain = parameters.getRawParameterValue("mod1_sustain")->load();
    bp.mod1Release = parameters.getRawParameterValue("mod1_release")->load();
    bp.mod1Amount  = parameters.getRawParameterValue("mod1_amount")->load();
    bp.mod1VelSens = parameters.getRawParameterValue("mod1_vel_sens")->load();
    bp.mod1Target  = static_cast<int>(parameters.getRawParameterValue("mod1_target")->load());
    bp.mod1Loop    = parameters.getRawParameterValue("mod1_loop")->load() > 0.5f;

    bp.mod2Attack  = parameters.getRawParameterValue("mod2_attack")->load();
    bp.mod2Decay   = parameters.getRawParameterValue("mod2_decay")->load();
    bp.mod2Sustain = parameters.getRawParameterValue("mod2_sustain")->load();
    bp.mod2Release = parameters.getRawParameterValue("mod2_release")->load();
    bp.mod2Amount  = parameters.getRawParameterValue("mod2_amount")->load();
    bp.mod2VelSens = parameters.getRawParameterValue("mod2_vel_sens")->load();
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
    // filter_type: 0=Off, 1=LP, 2=HP, 3=BP → filterEnabled from type, DSP type is 0-based
    {
        int ft = static_cast<int>(parameters.getRawParameterValue("filter_type")->load());
        bp.filterEnabled = (ft > 0);
        bp.filterType = ft > 0 ? ft - 1 : 0;  // 0=LP, 1=HP, 2=BP for DSP
    }
    bp.baseCutoff = parameters.getRawParameterValue("filter_cutoff")->load();
    bp.baseReso = parameters.getRawParameterValue("filter_resonance")->load();
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
    driftLfo.setRegenMode(static_cast<int>(parameters.getRawParameterValue("drift_regen")->load()));
    int d1t = static_cast<int>(parameters.getRawParameterValue("drift1_target")->load());
    int d2t = static_cast<int>(parameters.getRawParameterValue("drift2_target")->load());
    int d3t = static_cast<int>(parameters.getRawParameterValue("drift3_target")->load());
    // Auto-enable drift when any target is set (target 0 = "---" = None)
    bool driftHasTarget = (d1t != 0) || (d2t != 0) || (d3t != 0);
    bool driftManualEnable = parameters.getRawParameterValue("drift_enabled")->load() > 0.5f;
    driftLfo.setEnabled(driftHasTarget || driftManualEnable);
    driftLfo.setLfoRate(0, parameters.getRawParameterValue("drift1_rate")->load());
    driftLfo.setLfoDepth(0, parameters.getRawParameterValue("drift1_depth")->load());
    driftLfo.setLfoTarget(0, d1t);
    driftLfo.setLfoWaveform(0, static_cast<int>(parameters.getRawParameterValue("drift1_wave")->load()));
    driftLfo.setLfoRate(1, parameters.getRawParameterValue("drift2_rate")->load());
    driftLfo.setLfoDepth(1, parameters.getRawParameterValue("drift2_depth")->load());
    driftLfo.setLfoTarget(1, d2t);
    driftLfo.setLfoWaveform(1, static_cast<int>(parameters.getRawParameterValue("drift2_wave")->load()));
    driftLfo.setLfoRate(2, parameters.getRawParameterValue("drift3_rate")->load());
    driftLfo.setLfoDepth(2, parameters.getRawParameterValue("drift3_depth")->load());
    driftLfo.setLfoTarget(2, d3t);
    driftLfo.setLfoWaveform(2, static_cast<int>(parameters.getRawParameterValue("drift3_wave")->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    // Apply drift offsets to their respective targets
    bp.driftScanOffset   = driftLfo.getOffsetForTarget(DriftLFO::TgtWtScan);
    bp.driftFilterOffset = driftLfo.getOffsetForTarget(DriftLFO::TgtFilter);
    bp.driftPitchOffset  = driftLfo.getOffsetForTarget(DriftLFO::TgtPitch);
    // Block-level drift targets (delay/reverb) applied after modDelayTime etc. are declared

    // Drift → envelope amounts (additive, clamped to 0–1)
    bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv1Amt));
    bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv2Amt));
    bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv3Amt));

    // ── Sampler settings ─────────────────────────────────────────────────────
    int loopModeIdx = static_cast<int>(parameters.getRawParameterValue("loop_mode")->load());
    masterSampler.setLoopMode(static_cast<SamplePlayer::LoopMode>(loopModeIdx));
    masterSampler.setCrossfadeMs(parameters.getRawParameterValue("crossfade_ms")->load());
    masterSampler.setNormalize(parameters.getRawParameterValue("normalize")->load() > 0.5f);
    masterSampler.setLoopOptimize(parameters.getRawParameterValue("loop_optimize")->load() > 0.5f);

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    // (seqRunning already read above for idle detection)
    float seqBpm = parameters.getRawParameterValue("seq_bpm")->load();
    int seqSteps = static_cast<int>(parameters.getRawParameterValue("seq_steps")->load());
    float seqGate = parameters.getRawParameterValue("seq_gate")->load();
    int seqPreset = static_cast<int>(parameters.getRawParameterValue("seq_preset")->load());
    int arpModeRaw = static_cast<int>(parameters.getRawParameterValue("arp_mode")->load());
    bool arpEnabled = arpModeRaw > 0;
    int arpMode = arpModeRaw > 0 ? arpModeRaw - 1 : 0; // 0=Up,1=Down,2=UpDown,3=Random
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

    // ── Sample-accurate MIDI + Voice rendering ──────────────────────────────
    bool lfo1TrigMode = static_cast<int>(parameters.getRawParameterValue("lfo1_mode")->load()) == 1;
    bool lfo2TrigMode = static_cast<int>(parameters.getRawParameterValue("lfo2_mode")->load()) == 1;

    // Re-check: seq/arp may have generated notes
    if (voiceManager.hasActiveVoices() || !midiMessages.isEmpty())
        silentBlockCount = 0;

    bool skipSynthesis = (silentBlockCount > 0 && !voiceManager.hasActiveVoices()
                          && midiMessages.isEmpty());

    // Modulation values (zero when skipping synthesis)
    float modDelayTime = 0.0f, modDelayFb = 0.0f, modDelayMix = 0.0f, modReverbMix = 0.0f;

    // Drift → block-level FX targets (runs even during tail for smooth drift)
    modDelayTime += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayTime);
    modDelayFb   += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayFb);
    modDelayMix  += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayMix);
    modReverbMix += driftLfo.getOffsetForTarget(DriftLFO::TgtReverbMix);
    VoiceManager::VoiceOutput voiceOut;

    if (!skipSynthesis)
    {
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

            lfo1Buf[i] = l1;
            lfo2Buf[i] = l2;
        }

        // LFO → envelope amounts (multiplicative, clamped to 0–1)
        {
            float l1End = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
            float l2End = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
            if (bp.lfo1Target == LfoTarget::Env1Amt) bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  * (1.0f + l1End));
            if (bp.lfo1Target == LfoTarget::Env2Amt) bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount * (1.0f + l1End));
            if (bp.lfo1Target == LfoTarget::Env3Amt) bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount * (1.0f + l1End));
            if (bp.lfo2Target == LfoTarget::Env1Amt) bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  * (1.0f + l2End));
            if (bp.lfo2Target == LfoTarget::Env2Amt) bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount * (1.0f + l2End));
            if (bp.lfo2Target == LfoTarget::Env3Amt) bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount * (1.0f + l2End));
        }

        // ── Sample-accurate rendering: split block at MIDI event boundaries ──
        {
            auto midiIter = midiMessages.cbegin();
            int renderPos = 0;

            while (renderPos < numSamples)
            {
                // Next sub-block ends at the next MIDI event or block end
                int subEnd = numSamples;
                if (midiIter != midiMessages.cend())
                    subEnd = juce::jmin((*midiIter).samplePosition, numSamples);

                // Render voices up to this point
                int subLen = subEnd - renderPos;
                if (subLen > 0)
                    voiceOut = voiceManager.renderBlock(buffer, bp,
                        lfo1Buf + renderPos, lfo2Buf + renderPos, renderPos, subLen);

                // Process all MIDI events at this sample position
                while (midiIter != midiMessages.cend()
                       && (*midiIter).samplePosition <= subEnd)
                {
                    const auto msg = (*midiIter).getMessage();
                    if (msg.isNoteOn())
                    {
                        int note = msg.getNoteNumber();
                        float velocity = msg.getFloatVelocity();
                        bool isBind = (msg.getChannel() == 2);

                        lastMidiNote.store(note, std::memory_order_relaxed);
                        lastMidiVelocity.store(juce::roundToInt(velocity * 127.0f),
                                               std::memory_order_relaxed);
                        lastMidiNoteOn.store(true, std::memory_order_relaxed);

                        voiceManager.noteOn(note, velocity, isBind,
                            isBind ? 0.0f : stepSequencer.getGlideTime(),
                            lfo1TrigMode, lfo2TrigMode);
                    }
                    else if (msg.isNoteOff())
                    {
                        voiceManager.noteOff(msg.getNoteNumber());
                        if (!voiceManager.hasActiveVoices())
                            lastMidiNoteOn.store(false, std::memory_order_relaxed);
                    }
                    ++midiIter;
                }

                renderPos = subEnd;
            }
        }
        lastTriggeredNote = voiceOut.lastTriggeredNote;

        // Capture last LFO values for block-rate modulation
        float lastMod1Val = voiceOut.lastMod1Val;
        float lastMod2Val = voiceOut.lastMod2Val;
        float lastLfo1Val = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
        float lastLfo2Val = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;

        // Filter is now per-voice (in SynthVoice::renderSample)

        // ── Accumulate block-rate modulation for delay/reverb ─────────────────
        // (Pitch modulation is handled per-sample in SynthVoice::renderSample)
        if (bp.mod1Target == EnvTarget::DelayTime)  modDelayTime += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayFB)    modDelayFb += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayMix)   modDelayMix += lastMod1Val;
        if (bp.mod1Target == EnvTarget::ReverbMix)  modReverbMix += lastMod1Val;
        if (bp.mod2Target == EnvTarget::DelayTime)  modDelayTime += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayFB)    modDelayFb += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayMix)   modDelayMix += lastMod2Val;
        if (bp.mod2Target == EnvTarget::ReverbMix)  modReverbMix += lastMod2Val;

        // Env → LFO modulation
        if (bp.mod1Target == EnvTarget::LFO1Rate)   lfo1.setRate(baseLfo1Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO1Depth)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod1Val * baseLfo1Depth));
        if (bp.mod1Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO2Depth)  lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod1Val * baseLfo2Depth));
        if (bp.mod2Target == EnvTarget::LFO1Rate)   lfo1.setRate(baseLfo1Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO1Depth)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod2Val * baseLfo1Depth));
        if (bp.mod2Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO2Depth)  lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod2Val * baseLfo2Depth));
        if (bp.lfo1Target == LfoTarget::DelayTime)  modDelayTime += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayFB)    modDelayFb += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayMix)   modDelayMix += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo1Val;
        if (bp.lfo2Target == LfoTarget::DelayTime)  modDelayTime += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayFB)    modDelayFb += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayMix)   modDelayMix += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo2Val;
    }
    else
    {
        // Free-running LFOs: advance phase without per-sample computation
        lfo1.advancePhase(numSamples);
        lfo2.advancePhase(numSamples);
    }

    // ── Effects (parallel send-bus: dry + delay + reverb → limiter) ───────
    int delayType = static_cast<int>(parameters.getRawParameterValue("delay_type")->load());
    bool delayEnabled = delayType > 0;
    int reverbType = static_cast<int>(parameters.getRawParameterValue("reverb_type")->load());
    bool reverbEnabled = reverbType > 0;
    bool reverbIsAlgo = reverbType == 4;

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
        float baseRevMix = parameters.getRawParameterValue("reverb_mix")->load();
        float revMix = juce::jlimit(0.0f, 1.0f, baseRevMix + modReverbMix);

        if (reverbIsAlgo)
        {
            algoReverb.setRoomSize(parameters.getRawParameterValue("algo_room")->load());
            algoReverb.setDamping(parameters.getRawParameterValue("algo_damping")->load());
            algoReverb.setWidth(parameters.getRawParameterValue("algo_width")->load());
            algoReverb.setMix(revMix);
        }
        else
        {
            // Convolution: map reverb_type 1=Dark→2, 2=Medium→1, 3=Bright→0
            int irIndex = reverbType == 1 ? 2 : reverbType == 2 ? 1 : 0;
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
            reverb.setMix(revMix);
        }
    }

    // Parallel send-bus architecture (reference useEffects.ts):
    //   source → dry(compensated) ──→ sum → limiter
    //          → delaySend ─────────→ sum
    //          → reverbSend ────────→ sum
    // Combined peak: delay ~1.7x + reverb wet additive → up to ~2.7x before master.
    //
    // Delay already implements send-bus internally (dry*comp + wet*mix).
    // When both FX are active, reverb needs the ORIGINAL source, not delay output.
    // Helper lambda: process reverb (convolution or algo) on a buffer
    auto processReverb = [&](juce::AudioBuffer<float>& buf) {
        if (reverbIsAlgo)
            algoReverb.processBlock(buf);
        else
            reverb.processBlock(buf);
    };

    if (delayEnabled && reverbEnabled)
    {
        // Save original source for reverb (pre-allocated buffer, no heap alloc)
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Delay modifies buffer in-place: output = dry*comp + delayed*mix
        delay.processBlock(buffer);

        // Process reverb on original source (wet-only: set mix=1 temporarily)
        float savedRevMix = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue("reverb_mix")->load() + modReverbMix);
        if (reverbIsAlgo) { algoReverb.setMix(1.0f); } else { reverb.setMix(1.0f); }
        processReverb(reverbSendBuffer);
        if (reverbIsAlgo) { algoReverb.setMix(savedRevMix); } else { reverb.setMix(savedRevMix); }

        // Add reverb send to output: buffer += convolved * reverbMix
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSendBuffer.getReadPointer(ch);
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
        // Reverb as send: dry signal stays, wet is added on top
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        float revMixVal = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue("reverb_mix")->load() + modReverbMix);
        if (reverbIsAlgo) { algoReverb.setMix(1.0f); } else { reverb.setMix(1.0f); }
        processReverb(reverbSendBuffer);
        if (reverbIsAlgo) { algoReverb.setMix(revMixVal); } else { reverb.setMix(revMixVal); }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSendBuffer.getReadPointer(ch);
            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] += rev[i] * revMixVal;
        }
    }

    // ── Update modulated values for GUI ghost indicators ────────────────────
    // Only store non-NaN when something actually modulates the parameter.
    constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();

    if (!skipSynthesis)
    {
        // Filter cutoff ghost
        {
            bool filterModulated = (bp.mod1Target == EnvTarget::Filter || bp.mod2Target == EnvTarget::Filter ||
                                    bp.lfo1Target == LfoTarget::Filter || bp.lfo2Target == LfoTarget::Filter ||
                                    bp.kbdTrack > 0.0f) && voiceOut.hasActiveVoices;
            modulatedValues.filterCutoff.store(filterModulated ? voiceOut.lastModulatedCutoff : NO_GHOST,
                                               std::memory_order_relaxed);
        }

        // Scan ghost
        {
            bool scanModulated = (bp.mod1Target == EnvTarget::Scan || bp.mod2Target == EnvTarget::Scan ||
                                  bp.lfo1Target == LfoTarget::Scan || bp.lfo2Target == LfoTarget::Scan ||
                                  std::abs(bp.driftScanOffset) > 0.001f) && voiceOut.hasActiveVoices;
            modulatedValues.scanPosition.store(scanModulated ? voiceOut.lastModulatedScan : NO_GHOST,
                                               std::memory_order_relaxed);
        }

        // LFO1 Rate/Depth ghost
        {
            bool lfo1RateMod  = bp.mod1Target == EnvTarget::LFO1Rate  || bp.mod2Target == EnvTarget::LFO1Rate;
            bool lfo1DepthMod = bp.mod1Target == EnvTarget::LFO1Depth || bp.mod2Target == EnvTarget::LFO1Depth;
            modulatedValues.lfo1Rate.store(lfo1RateMod ? lfo1.getRate() : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo1Depth.store(lfo1DepthMod ? lfo1.getDepth() : NO_GHOST, std::memory_order_relaxed);
        }

        // LFO2 Rate/Depth ghost
        {
            bool lfo2RateMod  = bp.mod1Target == EnvTarget::LFO2Rate  || bp.mod2Target == EnvTarget::LFO2Rate;
            bool lfo2DepthMod = bp.mod1Target == EnvTarget::LFO2Depth || bp.mod2Target == EnvTarget::LFO2Depth;
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
                ? juce::jlimit(1.0f, 2000.0f, parameters.getRawParameterValue("delay_time")->load() * (1.0f + modDelayTime))
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
    // Store raw audio (unmodified) for preset embedding and re-apply on toggle
    if (&audioBuffer != &generatedAudioRaw)
        generatedAudioRaw.makeCopyOf(audioBuffer);
    generatedSampleRate = sr;

    // Conditionally apply HF boost to compensate VAE decoder rolloff
    bool hfOn = parameters.getRawParameterValue("gen_hf_boost")->load() > 0.5f;
    juce::AudioBuffer<float> boostedBuffer;
    if (hfOn)
    {
        boostedBuffer.makeCopyOf(audioBuffer);
        applyHfBoost(boostedBuffer, sr);
    }
    const auto& feedBuffer = hfOn ? boostedBuffer : audioBuffer;

    // Keep generatedAudioFull in sync (used for waveform display + presets)
    generatedAudioFull.makeCopyOf(feedBuffer);

    // Feed the sampler/voice chain
    masterSampler.loadBuffer(feedBuffer, sr);

    // In wavetable mode, auto-position brackets to the active signal region
    // (trim leading/trailing silence so extraction doesn't produce useless frames)
    if (isWavetableMode())
    {
        const int numSamples = feedBuffer.getNumSamples();
        if (numSamples > 0)
        {
            const float* data = feedBuffer.getReadPointer(0);
            const int windowSize = 256;
            const float threshold = 0.002f; // Peak threshold (~-54dB)

            int firstActive = 0;
            int lastActive = numSamples;

            // Find first window with peak above threshold
            for (int pos = 0; pos + windowSize <= numSamples; pos += windowSize)
            {
                float peak = 0.0f;
                for (int i = 0; i < windowSize; ++i)
                    peak = std::max(peak, std::abs(data[pos + i]));
                if (peak > threshold)
                {
                    firstActive = pos;
                    break;
                }
            }

            // Find last window with peak above threshold
            for (int pos = numSamples - windowSize; pos >= 0; pos -= windowSize)
            {
                float peak = 0.0f;
                int len = juce::jmin(windowSize, numSamples - pos);
                for (int i = 0; i < len; ++i)
                    peak = std::max(peak, std::abs(data[pos + i]));
                if (peak > threshold)
                {
                    lastActive = juce::jmin(pos + windowSize, numSamples);
                    break;
                }
            }

            // Small margin (one window each side)
            firstActive = juce::jmax(0, firstActive - windowSize);
            lastActive  = juce::jmin(numSamples, lastActive + windowSize);

            float startFrac = static_cast<float>(firstActive) / static_cast<float>(numSamples);
            float endFrac   = static_cast<float>(lastActive)  / static_cast<float>(numSamples);

            // Only apply if the detected region is meaningful
            if (endFrac - startFrac >= 0.05f)
            {
                masterSampler.setLoopStart(startFrac);
                masterSampler.setLoopEnd(endFrac);
            }
        }
    }

    // Wavetable extraction respects the bracket region
    float extractStart = masterSampler.getLoopStart();
    float extractEnd   = masterSampler.getLoopEnd();
    masterOsc.extractFramesFromBuffer(feedBuffer, sr, extractStart, extractEnd);

    voiceManager.distributeSamplerBuffer(masterSampler);
    voiceManager.distributeWavetableFrames(masterOsc);

    // Snapshot channel 0 for waveform display
    if (feedBuffer.getNumChannels() > 0 && feedBuffer.getNumSamples() > 0)
    {
        waveformSnapshot.setSize(1, feedBuffer.getNumSamples(), false, false, true);
        waveformSnapshot.copyFrom(0, 0, feedBuffer, 0, 0, feedBuffer.getNumSamples());

        // Normalize display when wavetable mode or sampler normalize is active
        bool normDisplay = isWavetableMode()
            || (parameters.getRawParameterValue("normalize")->load() > 0.5f);
        if (normDisplay)
        {
            float peak = 0.0f;
            const float* d = waveformSnapshot.getReadPointer(0);
            for (int i = 0; i < waveformSnapshot.getNumSamples(); ++i)
                peak = std::max(peak, std::abs(d[i]));
            if (peak > 0.001f)
                waveformSnapshot.applyGain(0.95f / peak);
        }

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
// filter_type: 0=Off, 1=Lowpass, 2=Highpass, 3=Bandpass
static int filterTypeFromString(const juce::String& s) {
    if (s == "off") return 0;
    if (s == "highpass") return 2;
    if (s == "bandpass") return 3;
    return 1; // lowpass (default)
}
static juce::String filterTypeToString(int i) {
    if (i == 0) return "off";
    if (i == 2) return "highpass";
    if (i == 3) return "bandpass";
    return "lowpass";
}

// filter_slope: 0=6dB, 1=12dB, 2=18dB, 3=24dB
static int filterSlopeFromString(const juce::String& s) {
    if (s == "6") return 0;
    if (s == "18") return 2;
    if (s == "24") return 3;
    return 1; // 12dB (default)
}
static juce::String filterSlopeToString(int i) {
    if (i == 0) return "6";
    if (i == 2) return "18";
    if (i == 3) return "24";
    return "12";
}

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
    if (s == "filter") return 6;
    if (s == "pitch") return 7;
    if (s == "delay_time") return 8;
    if (s == "delay_feedback") return 9;
    if (s == "delay_mix") return 10;
    if (s == "reverb_mix") return 11;
    return 0; // none
}
static juce::String driftTargetToString(int i) {
    const char* names[] = {"none","alpha","sem_axis_1","sem_axis_2","sem_axis_3",
                           "wt_scan","filter","pitch","delay_time","delay_feedback",
                           "delay_mix","reverb_mix"};
    return (i >= 0 && i <= 11) ? names[i] : "none";
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

// ═══════════════════════════════════════════════════════════════════
// HF boost — two-band high shelf to compensate VAE decoder rolloff
// ═══════════════════════════════════════════════════════════════════

void T5ynthProcessor::applyHfBoost(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto shelf1 = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 4000.0, 0.6, juce::Decibels::decibelsToGain(3.0f));
    auto shelf2 = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 10000.0, 0.6, juce::Decibels::decibelsToGain(4.5f));

    juce::dsp::IIR::Filter<float> f1, f2;
    f1.coefficients = shelf1;
    f2.coefficients = shelf2;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        f1.reset();
        f2.reset();
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            data[i] = f1.processSample(data[i]);
            data[i] = f2.processSample(data[i]);
        }
    }
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
    // Regen mode: 0=Manual, 1=Auto, 2=1st Bar
    int regenMode = static_cast<int>(get("drift_regen"));
    const char* regenNames[] = {"manual", "auto", "1st_bar"};
    root->setProperty("regenMode", regenNames[juce::jlimit(0, 2, regenMode)]);

    // Wavetable
    juce::DynamicObject::Ptr wt = new juce::DynamicObject();
    wt->setProperty("scan", get("osc_scan"));
    root->setProperty("wavetable", wt.get());

    // Effects
    juce::DynamicObject::Ptr fx = new juce::DynamicObject();
    // Delay type: 0=Off, 1=Stereo
    int dlyType = static_cast<int>(get("delay_type"));
    fx->setProperty("delayType", dlyType == 0 ? "off" : "stereo");
    fx->setProperty("delayTimeMs", get("delay_time"));
    fx->setProperty("delayFeedback", get("delay_feedback"));
    fx->setProperty("delayMix", get("delay_mix"));
    fx->setProperty("delayDamp", get("delay_damp"));
    // Reverb type: 0=Off, 1=Dark, 2=Medium, 3=Bright, 4=Algo
    int revType = static_cast<int>(get("reverb_type"));
    const char* revNames[] = {"off", "dark", "medium", "bright", "algo"};
    fx->setProperty("reverbType", revNames[juce::jlimit(0, 4, revType)]);
    fx->setProperty("reverbMix", get("reverb_mix"));
    root->setProperty("effects", fx.get());

    // Filter — store NORMALIZED cutoff (0-1), not Hz
    juce::DynamicObject::Ptr filt = new juce::DynamicObject();
    int ftRaw = static_cast<int>(get("filter_type"));
    filt->setProperty("enabled", ftRaw > 0);
    filt->setProperty("type", filterTypeToString(ftRaw));
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
        s->setProperty("bind", step.bind);
        stepArr.add(s.get());
    }
    seq->setProperty("steps", stepArr);
    root->setProperty("sequencer", seq.get());

    // Arpeggiator
    juce::DynamicObject::Ptr arp = new juce::DynamicObject();
    int arpModeVal = static_cast<int>(get("arp_mode"));
    arp->setProperty("enabled", arpModeVal > 0);
    const char* arpPatterns[] = {"up", "down", "updown", "random"};
    arp->setProperty("pattern", arpModeVal > 0 ? arpPatterns[std::min(arpModeVal - 1, 3)] : "up");
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
    // Regen mode (new: "manual"/"auto"/"1st_bar", old: bool autoRegen)
    if (root->hasProperty("regenMode"))
    {
        juce::String rm = root->getProperty("regenMode").toString();
        int rIdx = 0;
        if (rm == "auto") rIdx = 1;
        else if (rm == "1st_bar") rIdx = 2;
        setParam(parameters, "drift_regen", static_cast<float>(rIdx));
    }
    else if (root->hasProperty("autoRegen"))
    {
        setParam(parameters, "drift_regen", root->getProperty("autoRegen") ? 1.0f : 0.0f);
    }

    // ── Wavetable ──
    if (auto* wt = root->getProperty("wavetable").getDynamicObject())
    {
        if (wt->hasProperty("scan"))
            setParam(parameters, "osc_scan", static_cast<float>(wt->getProperty("scan")));
    }

    // ── Effects ──
    if (auto* fx = root->getProperty("effects").getDynamicObject())
    {
        // Delay type (new format: "off"/"stereo", old format: delayEnabled bool)
        if (fx->hasProperty("delayType"))
        {
            juce::String dt = fx->getProperty("delayType").toString();
            setParam(parameters, "delay_type", dt == "stereo" ? 1.0f : 0.0f);
        }
        else if (fx->hasProperty("delayEnabled"))
        {
            setParam(parameters, "delay_type", fx->getProperty("delayEnabled") ? 1.0f : 0.0f);
        }
        setParam(parameters, "delay_time", static_cast<float>(fx->getProperty("delayTimeMs")));
        setParam(parameters, "delay_feedback", static_cast<float>(fx->getProperty("delayFeedback")));
        setParam(parameters, "delay_mix", static_cast<float>(fx->getProperty("delayMix")));
        if (fx->hasProperty("delayDamp"))
            setParam(parameters, "delay_damp", static_cast<float>(fx->getProperty("delayDamp")));
        // Reverb type (new format: "off"/"dark"/"medium"/"bright"/"algo",
        //              old format: reverbEnabled bool + reverbVariant string)
        if (fx->hasProperty("reverbType"))
        {
            juce::String rt = fx->getProperty("reverbType").toString();
            int rIdx = 0;
            if (rt == "dark") rIdx = 1;
            else if (rt == "medium") rIdx = 2;
            else if (rt == "bright") rIdx = 3;
            else if (rt == "algo") rIdx = 4;
            setParam(parameters, "reverb_type", static_cast<float>(rIdx));
        }
        else if (fx->hasProperty("reverbEnabled"))
        {
            if (fx->getProperty("reverbEnabled"))
            {
                // Map old variant to new type: bright→3, medium→2, dark→1
                int oldIr = fx->hasProperty("reverbVariant")
                    ? reverbVariantFromString(fx->getProperty("reverbVariant").toString()) : 1;
                int newType = oldIr == 0 ? 3 : oldIr == 2 ? 1 : 2; // bright→3, dark→1, medium→2
                setParam(parameters, "reverb_type", static_cast<float>(newType));
            }
            else
            {
                setParam(parameters, "reverb_type", 0.0f);
            }
        }
        setParam(parameters, "reverb_mix", static_cast<float>(fx->getProperty("reverbMix")));
    }

    // ── Filter — CRITICAL: cutoff is normalized 0-1, convert to Hz ──
    if (auto* filt = root->getProperty("filter").getDynamicObject())
    {
        // Merge enabled + type: if enabled=false, set type to Off (0)
        bool filtEnabled = filt->getProperty("enabled");
        int filtType = filterTypeFromString(filt->getProperty("type").toString());
        if (!filtEnabled) filtType = 0;  // Off
        setParam(parameters, "filter_type", static_cast<float>(filtType));
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
        // Preserve current seq_running state — don't stop playback on preset load
        // bool seqEnabled = seq->getProperty("enabled");
        // setParam(parameters, "seq_running", seqEnabled ? 1.0f : 0.0f);
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
                if (s->hasProperty("bind"))
                    stepSequencer.setStepBind(i, static_cast<bool>(s->getProperty("bind")));
                else if (s->hasProperty("glide"))  // backward compat
                    stepSequencer.setStepBind(i, static_cast<bool>(s->getProperty("glide")));
            }
        }
    }

    // ── Arpeggiator ──
    if (auto* arp = root->getProperty("arpeggiator").getDynamicObject())
    {
        bool arpIsEnabled = arp->getProperty("enabled");
        if (arpIsEnabled)
        {
            juce::String pattern = arp->getProperty("pattern").toString();
            int mode = 1; // default: Up (Off=0,Up=1,Down=2,UpDown=3,Random=4)
            if (pattern == "down") mode = 2;
            else if (pattern == "updown") mode = 3;
            else if (pattern == "random") mode = 4;
            setParam(parameters, "arp_mode", static_cast<float>(mode));
            setParam(parameters, "seq_mode", static_cast<float>(mode)); // keep seq_mode in sync
        }
        else
        {
            setParam(parameters, "arp_mode", 0.0f); // Off
            setParam(parameters, "seq_mode", 0.0f);
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
