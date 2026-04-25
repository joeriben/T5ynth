#pragma once
#include <JuceHeader.h>
#include <optional>
#include "../dsp/SamplePlayer.h"

/**
 * Heuristic tag suggestions for a preset.
 *
 * Two independent sources, intentionally simple — output is a *suggestion*
 * the user can accept, edit or discard:
 *
 *  1. Audio-content tags derived from `SamplePlayer::NormalizeAnalysis`
 *     (re-uses the same statistics the normalizer already computes; no
 *     new DSP needed). Maps `chooseNormalizeMode()` and the underlying
 *     `crestDb`, `activeRatio`, `peakToPercentileDb`, `durationSeconds`
 *     and `peak` features onto a small fixed taxonomy.
 *
 *  2. Prompt-keyword tags via case-insensitive substring match against a
 *     short curated lexicon. Operates on `promptA + promptB`.
 *
 * Both sources merge into a deduplicated `juce::StringArray`, preserving
 * insertion order. Empty sources (no audio loaded, no prompt text) are
 * skipped silently.
 */
namespace PresetTagSuggester
{
    inline juce::StringArray fromAnalysis(const SamplePlayer& sampler,
                                          std::optional<SamplePlayer::NormalizeAnalysis> analysisOpt)
    {
        juce::StringArray tags;
        if (!analysisOpt.has_value())
            return tags;

        const auto& a = *analysisOpt;
        if (a.peak <= 0.0f || a.durationSeconds <= 0.0f)
            return tags;

        const auto mode = sampler.chooseNormalizeMode(a);
        switch (mode)
        {
            case SamplePlayer::NormalizeMode::Transient: tags.add("transient"); break;
            case SamplePlayer::NormalizeMode::Sustained: tags.add("sustained"); break;
            case SamplePlayer::NormalizeMode::PeakCap:   tags.add("hot");       break;
            case SamplePlayer::NormalizeMode::Bypass:    tags.add("quiet");     break;
        }

        if (a.activeRatio < 0.25f)             tags.addIfNotAlreadyThere("sparse");
        else if (a.activeRatio > 0.85f)        tags.addIfNotAlreadyThere("dense");

        if (a.durationSeconds < 1.5f)          tags.addIfNotAlreadyThere("short");
        else if (a.durationSeconds > 15.0f)    tags.addIfNotAlreadyThere("long");

        if (a.peakToPercentileDb > 12.0f)      tags.addIfNotAlreadyThere("one-shot");

        return tags;
    }

    inline juce::StringArray fromPrompts(const juce::String& promptA,
                                         const juce::String& promptB)
    {
        struct Rule { const char* tag; std::initializer_list<const char*> needles; };
        static const Rule kRules[] = {
            { "pad",      { "pad", "atmosphere", "drone" } },
            { "bass",     { "bass", "sub", " 808" } },
            { "lead",     { "lead", "synth lead" } },
            { "perc",     { "perc", "drum", "kick", "snare", "hat", "hit" } },
            { "noise",    { "noise", "static", "wash", "hiss" } },
            { "ambient",  { "ambient", "ethereal", "dreamy", "celestial" } },
            { "bright",   { "bright", "shimmer", "crystal", "glass" } },
            { "dark",     { "dark", "deep", "subterranean", "abyss" } },
            { "metallic", { "metal", "metallic", "bell", "chime" } },
            { "vocal",    { "vocal", "voice", "choir", "vox" } },
        };

        const auto haystack = (promptA + " " + promptB).toLowerCase();
        if (haystack.trim().isEmpty())
            return {};

        juce::StringArray tags;
        for (const auto& rule : kRules)
        {
            for (auto* needle : rule.needles)
            {
                if (haystack.contains(juce::String(needle).toLowerCase()))
                {
                    tags.addIfNotAlreadyThere(rule.tag);
                    break;
                }
            }
        }
        return tags;
    }

    inline juce::StringArray suggest(const SamplePlayer& sampler,
                                     std::optional<SamplePlayer::NormalizeAnalysis> analysisOpt,
                                     const juce::String& promptA,
                                     const juce::String& promptB)
    {
        juce::StringArray merged = fromAnalysis(sampler, analysisOpt);
        for (auto& t : fromPrompts(promptA, promptB))
            merged.addIfNotAlreadyThere(t);
        return merged;
    }
}
