#pragma once
#include "DcoRecipe.h"
#include <JuceHeader.h>

namespace dco {

/**
 * Parse a backend-authored recipe JSON (docs/DCO_LLM_GUARDRAILS.md §5) into a
 * Recipe. Belt-and-braces layering: the backend validates before sending and
 * DcoBaker clamps every numeric field again before use — this parser only does
 * type-safe extraction, enum mapping, and structural caps so hostile or
 * truncated JSON cannot allocate unbounded vectors. Unknown kind/curve strings
 * fall back to Saw/Lin (the backend enum is a superset-stable contract).
 */
inline Keyframe::Kind kindFromString(const juce::String& s)
{
    if (s == "additive") return Keyframe::Kind::Additive;
    if (s == "square")   return Keyframe::Kind::Square;
    if (s == "pulse")    return Keyframe::Kind::Pulse;
    if (s == "triangle") return Keyframe::Kind::Triangle;
    if (s == "fm2")      return Keyframe::Kind::Fm2;
    if (s == "cheby")    return Keyframe::Kind::Cheby;
    if (s == "ring")     return Keyframe::Kind::Ring;
    return Keyframe::Kind::Saw;
}

inline Curve curveFromString(const juce::String& s)
{
    if (s == "fast") return Curve::Fast;
    if (s == "slow") return Curve::Slow;
    return Curve::Lin;
}

inline Recipe recipeFromVar(const juce::var& v)
{
    Recipe r;
    r.keyframes.clear();
    r.motion.clear();

    constexpr int kMaxKeyframes = 8;
    constexpr int kMaxSegments  = 16;
    constexpr int kMaxPartials  = 128;

    if (const auto* kfs = v.getProperty("keyframes", juce::var()).getArray())
    {
        for (const auto& kfVar : *kfs)
        {
            if ((int) r.keyframes.size() >= kMaxKeyframes)
                break;
            Keyframe kf;
            kf.kind  = kindFromString(kfVar.getProperty("kind", "saw").toString());
            kf.width = (float) (double) kfVar.getProperty("width", 0.5);
            kf.ratio = (float) (double) kfVar.getProperty("ratio", 2.0);
            kf.index = (float) (double) kfVar.getProperty("index", 1.0);
            kf.order = (int) kfVar.getProperty("order", 2);
            kf.drive = (float) (double) kfVar.getProperty("drive", 1.0);
            kf.mix   = (float) (double) kfVar.getProperty("mix", 1.0);
            kf.shape = (float) (double) kfVar.getProperty("shape", 0.0);
            if (const auto* parts = kfVar.getProperty("partials", juce::var()).getArray())
            {
                for (const auto& pVar : *parts)
                {
                    if ((int) kf.partials.size() >= kMaxPartials)
                        break;
                    Partial p;
                    p.h     = (int) pVar.getProperty("h", 1);
                    p.a     = (float) (double) pVar.getProperty("a", 0.0);
                    p.phase = (float) (double) pVar.getProperty("phase", 0.0);
                    kf.partials.push_back(p);
                }
            }
            r.keyframes.push_back(std::move(kf));
        }
    }

    if (const auto* segs = v.getProperty("motion", juce::var()).getArray())
    {
        for (const auto& sVar : *segs)
        {
            if ((int) r.motion.size() >= kMaxSegments)
                break;
            MotionSegment seg;
            seg.to      = (int) sVar.getProperty("to", 0);
            seg.durFrac = (float) (double) sVar.getProperty("dur_frac", 0.0);
            seg.curve   = curveFromString(sVar.getProperty("curve", "lin").toString());
            r.motion.push_back(seg);
        }
    }

    r.loop   = (bool) v.getProperty("loop", true);
    r.frames = (int) v.getProperty("frames", 128);
    r.motionRateHz = static_cast<float>(
        static_cast<double>(v.getProperty("motion_rate_hz", 0.0)));
    return r;
}

} // namespace dco
