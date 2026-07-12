// Smoke-test the LCO "HEARD AS" display-reading formatter in isolation: mirror
// the exact extraction PromptPanel::triggerDcoBake runs (resolved.adjectives /
// resolved.motion / recipe.keyframes / recipe.frames + technique + rate) on a
// representative backend response, and print the result — so the line structure
// and the UTF-8 separators (middle dot, arrow) are eyeballed before trusting
// them in the GUI. Build with the flags.make response file + SharedCode lib.
#include "JuceHeader.h"
#include <cstdio>

// Verbatim copy of the builder in PromptPanel.cpp (keep in sync). `resolved`,
// `recipeVar`, `technique`, `frames`, `motionRateHz` come from the parsed bake.
static juce::String buildReading(const juce::var& resolved, const juce::var& recipeVar,
                                 const juce::String& technique, int frames, float motionRateHz)
{
    const juce::String kMid   = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));      // " · "
    const juce::String kArrow = juce::String(juce::CharPointer_UTF8(" \xe2\x86\x92 "));  // " → "
    juce::StringArray lines;

    juce::String head = (technique.isNotEmpty() && technique != "?") ? technique : juce::String();
    if (const auto* adjArr = resolved.getProperty("adjectives", juce::var()).getArray())
    {
        juce::StringArray adjs;
        for (const auto& a : *adjArr) adjs.add(a.toString());
        adjs.removeDuplicates(false);
        if (adjs.size() > 5) adjs.removeRange(5, adjs.size() - 5);
        if (! adjs.isEmpty())
            head = head.isEmpty() ? adjs.joinIntoString(", ")
                                  : head + kMid + adjs.joinIntoString(", ");
    }
    if (head.isNotEmpty()) lines.add(head);

    if (const auto* motionArr = resolved.getProperty("motion", juce::var()).getArray())
    {
        juce::StringArray mots;
        for (const auto& m : *motionArr) mots.add(m.toString());
        if (! mots.isEmpty()) lines.add("motion: " + mots.joinIntoString(", "));
    }
    if (const auto* kfArr = recipeVar.getProperty("keyframes", juce::var()).getArray())
    {
        juce::StringArray kinds;
        for (const auto& kf : *kfArr) kinds.add(kf.getProperty("kind", "saw").toString());
        if (! kinds.isEmpty()) lines.add(kinds.joinIntoString(kArrow));
    }
    juce::String foot = juce::String(frames) + " frames";
    if (motionRateHz > 0.0f) foot += kMid + juce::String(motionRateHz, 2) + " Hz";
    lines.add(foot);
    return lines.joinIntoString("\n");
}

static void run(const char* label, const juce::String& json,
                const juce::String& technique, float rate)
{
    const auto parsed   = juce::JSON::parse(json);
    const auto resolved = parsed.getProperty("resolved", juce::var());
    const auto recipe   = parsed.getProperty("recipe", juce::var());
    const int  frames   = (int) recipe.getProperty("frames", 0);
    std::printf("== %s ==\n%s\n\n", label,
                buildReading(resolved, recipe, technique, frames, rate).toRawUTF8());
}

int main()
{
    run("additive, 2 adjectives, saw->sine",
        R"({"resolved":{"adjectives":["bright","metallic"],"motion":["rising"]},
            "recipe":{"frames":256,"keyframes":[{"kind":"saw"},{"kind":"sine"}]}})",
        "additive", 0.25f);

    run("FM, 3 keyframes, no motion words",
        R"({"resolved":{"adjectives":["clangorous","hollow","glassy"]},
            "recipe":{"frames":128,"keyframes":[{"kind":"sine"},{"kind":"square"},{"kind":"sine"}]}})",
        "fm", 0.0f);

    run("6 adjectives (cap at 5)",
        R"({"resolved":{"adjectives":["a","b","c","d","e","f"]},
            "recipe":{"frames":64,"keyframes":[{"kind":"tri"}]}})",
        "additive", 1.0f);

    run("empty resolved, minimal recipe",
        R"({"resolved":{},"recipe":{"frames":32,"keyframes":[]}})",
        "?", 0.0f);
    return 0;
}
