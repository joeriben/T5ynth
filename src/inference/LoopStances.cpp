#include "LoopStances.h"

// ─────────────────────────────────────────────────────────────────────────────
// Faithful C++ port of the curated subset of tools/clap_llm_loop.py.
//
// The system prompts and user-turn builders are reproduced VERBATIM from the
// tool's MODES factories (the verified algorithm). Where the Python f-strings
// interpolate the changing per-turn state, the C++ does the same string build.
// ─────────────────────────────────────────────────────────────────────────────

namespace LoopStances
{

// ── the six interpreter stances: system prompt (the stance) ──────────────────
// Reproduced verbatim from clap_llm_loop.py MODES[*] sysp.
static juce::String syspVariation()
{
    // _mode_variation: NOTE — the Python captures the fixed header_a into the
    // system prompt ('The fixed identity (Prompt A) is: "..."'). On the plugin
    // side the A pole's identity is whatever lives in the A editor at loop time;
    // the variation stance is applied to the B pole, with the live A as context,
    // so the per-turn user text carries the current B and what was heard, and the
    // system prompt states the variation contract WITHOUT pinning a literal A
    // (the loop reads A from the editor, not from a run-fixed header). This is the
    // one place the C++ cannot reproduce the Python byte-for-byte — see the report.
    return "You are the Prompt-B variation engine of a text-to-audio synthesizer. "
           "Each turn you receive the current Prompt B and the timbres a machine ear hears "
           "in the latest rendered sound. Write ONE new short Prompt B (3 to 8 words) that "
           "VARIES the current one: keep its spirit and the family of the sound, but shift "
           "the imagery in a fresh musical direction suggested by what is heard. "
           "Reply with ONLY the new prompt - no quotes, no label, no explanation.";
}

static juce::String syspAbduction()
{
    return "You are the interpreter of a text-to-audio synthesizer. You are given the bare "
           "timbre words a machine ear heard in a sound, plus the scenes already tried. Make "
           "an abductive leap: name a concrete, unexpected real-world scene or source that "
           "could plausibly PRODUCE such a sound, phrased as ONE short generation prompt "
           "(3 to 8 words). Each turn, leap to a scene CLEARLY DIFFERENT from the ones already "
           "tried - never repeat or lightly reword them. Be surprising but physically "
           "plausible. Reply with ONLY the prompt - no quotes, no label, no explanation.";
}

static juce::String syspTranscribe()
{
    return "You are a machine transcription engine for a text-to-audio synthesizer. You "
           "receive ONLY machine measurements of the latest sound: the timbre words a "
           "neural ear matched, and signal-level spectral descriptors. Compose them "
           "LITERALLY into ONE short generation prompt (3 to 8 words) describing those "
           "measured qualities as a sound. Add NO scene, NO story, NO metaphor, NO place "
           "and NO human imagery " "\xe2\x80\x94" " only the measured sonic attributes. "
           "Reply with ONLY the prompt - no quotes, no label, no explanation.";
}

static juce::String syspOpposite()
{
    return "You describe the exact diametral OPPOSITE of the sound. Invert both the things "
           "and their relations: bright becomes dark, fast becomes slow, hard becomes soft, "
           "calm becomes agitated, dense becomes sparse, near becomes far, growth becomes "
           "decay. Each turn invert the CURRENT sound into its contrary, clearly different "
           "from the opposites already tried. "
           "Reply with ONLY one short prompt (3 to 8 words) - no quotes, no label.";
}

static juce::String syspEntkitscher()
{
    return "You de-kitsch a sound. Find the clich" "\xc3\xa9" "d, sentimental, conventional, "
           "over-expected qualities in what is heard and REMOVE them " "\xe2\x80\x94" " keep the same "
           "subject and sound-type, do NOT invert the meaning and do NOT make it harsh, "
           "just strip the clich" "\xc3\xa9" ". Rebuild from concrete, specific, unsentimental sonic "
           "detail. Describe positively (never 'no', 'without', 'not'). "
           "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.";
}

static juce::String syspVerniedlicher()
{
    return "You are a narrator of gentle magical realism for sound. Rewrite the prompt so it "
           "feels emotionally safe for a child yet sonically fascinating: turn the "
           "threatening into the wondrous and the harsh into the mysterious " "\xe2\x80\x94" " REINTERPRET, "
           "do not censor (a conflict becomes a riddle, darkness becomes shelter). Keep the "
           "core but heal its impact, an aesthetic of warmth and wonder. "
           "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.";
}

juce::String stanceSystemPrompt (const juce::String& stanceKey)
{
    if (stanceKey == "transcribe")    return syspTranscribe();
    if (stanceKey == "entkitscher")   return syspEntkitscher();
    if (stanceKey == "verniedlicher") return syspVerniedlicher();
    if (stanceKey == "variation")     return syspVariation();
    if (stanceKey == "abduction")     return syspAbduction();
    if (stanceKey == "opposite")      return syspOpposite();
    return {};   // "off" or unknown
}

// ── per-stance user-turn builders ────────────────────────────────────────────
// Mirror the build(tags, prev_b, recent, spectral) closures. Each reads ONLY the
// inputs its Python counterpart reads (the rest are ignored), so the unused
// arguments are intentional.
juce::String buildStanceUserTurn (const juce::String& stanceKey,
                                  const juce::String& tags,
                                  const juce::String& prevPolePrompt,
                                  const juce::StringArray& recentList,
                                  const juce::String& spectral)
{
    // "Already tried (do not reuse): a / b / c" — recent joined by " / ", exactly
    // as the Python `" / ".join(recent)`. Used by abduction & opposite.
    auto triedClause = [&recentList] () -> juce::String
    {
        if (recentList.isEmpty()) return {};
        return "\nAlready tried (do not reuse): " + recentList.joinIntoString (" / ");
    };

    if (stanceKey == "transcribe")
    {
        // build(tags,...,spectral): "Neural ear: {tags}" + ("\nSpectral: {spectral}" if spectral)
        juce::String spec = spectral.isNotEmpty() ? ("\nSpectral: " + spectral) : juce::String();
        return "Neural ear: " + tags + spec;
    }
    if (stanceKey == "abduction")
    {
        // build(tags, prev_b, recent): "Heard: {tags}{tried}"
        return "Heard: " + tags + triedClause();
    }
    if (stanceKey == "opposite")
    {
        // build(tags, prev_b, recent): "Heard: {tags}{tried}"
        return "Heard: " + tags + triedClause();
    }
    if (stanceKey == "entkitscher")
    {
        // build(tags, prev_b): 'Current prompt: "{prev_b}"\nHeard: {tags}'
        return "Current prompt: \"" + prevPolePrompt + "\"\nHeard: " + tags;
    }
    if (stanceKey == "verniedlicher")
    {
        // build(tags, prev_b): 'Current prompt: "{prev_b}"\nHeard: {tags}'
        return "Current prompt: \"" + prevPolePrompt + "\"\nHeard: " + tags;
    }
    if (stanceKey == "variation")
    {
        // build(tags, prev_b): 'Current Prompt B: "{prev_b}"\nHeard now: {tags}'
        return "Current Prompt B: \"" + prevPolePrompt + "\"\nHeard now: " + tags;
    }
    return {};   // "off" or unknown
}

// ── _clean_prompt port ───────────────────────────────────────────────────────
// function words a mid-sentence max_new_tokens cut tends to leave dangling — drop
// them so the prompt ends on a content word. VERBATIM from _TRAIL_FW.
static bool isTrailFunctionWord (const juce::String& w)
{
    static const char* kTrailFw[] = {
        "and", "or", "of", "the", "a", "an", "with", "in", "on", "to", "from",
        "under", "through", "into", "at", "by", "for", "as", "but", "that", "while"
    };
    for (auto* fw : kTrailFw)
        if (w == fw) return true;
    return false;
}

juce::String cleanPrompt (const juce::String& raw, int maxChars, int maxWords)
{
    juce::String s = raw.trim();

    // Keep the first non-empty line.
    juce::String first;
    {
        juce::StringArray lines;
        lines.addLines (s);
        for (auto& line : lines)
        {
            auto t = line.trim();
            if (t.isNotEmpty()) { first = t; break; }
        }
    }
    if (first.isEmpty())
        return {};

    // s = first.strip().strip("`").strip()
    s = first.trim()
             .trimCharactersAtStart ("`").trimCharactersAtEnd ("`")
             .trim();

    // Strip a leading user-turn label echo (case-insensitive, first match wins),
    // exactly the label set in _clean_prompt.
    {
        static const char* kLabels[] = {
            "prompt b:", "new prompt b:", "new prompt:", "prompt:", "next:",
            "output:", "answer:", "heard:", "machine heard:", "neural ear:",
            "current prompt b:", "current prompt:"
        };
        const juce::String low = s.toLowerCase();
        for (auto* lab : kLabels)
        {
            if (low.startsWith (lab))
            {
                s = s.substring ((int) juce::String (lab).length()).trim();
                break;
            }
        }
    }

    // Strip wrapping quotes — straight or curly (incl. mismatched open/close),
    // mirroring s.strip("\"'“”‘’"). The curly set as UTF-8:
    //   “ U+201C, ” U+201D, ‘ U+2018, ’ U+2019.
    {
        const juce::String quoteChars = juce::String ("\"'")
            + juce::String::fromUTF8 ("\xe2\x80\x9c\xe2\x80\x9d\xe2\x80\x98\xe2\x80\x99");
        // .strip(set) removes ALL leading/trailing chars in the set, repeatedly.
        s = s.trim()
             .trimCharactersAtStart (quoteChars).trimCharactersAtEnd (quoteChars)
             .trim();
    }

    // Cap to maxChars on a word boundary: s[:max_chars].rsplit(" ", 1)[0].
    if (s.length() > maxChars)
    {
        juce::String head = s.substring (0, maxChars);
        const int sp = head.lastIndexOfChar (' ');
        s = (sp >= 0) ? head.substring (0, sp) : head;
    }

    // Cap to maxWords, then drop trailing function words.
    juce::StringArray words;
    words.addTokens (s, " ", "");
    words.removeEmptyStrings();
    while (words.size() > maxWords)
        words.remove (words.size() - 1);
    // while words and words[-1].strip(",;:.").lower() in _TRAIL_FW: words.pop()
    while (words.size() > 0)
    {
        juce::String last = words[words.size() - 1]
                                .trimCharactersAtStart (",;:.")
                                .trimCharactersAtEnd (",;:.")
                                .toLowerCase();
        if (isTrailFunctionWord (last))
            words.remove (words.size() - 1);
        else
            break;
    }

    // " ".join(words).strip(" ,;:")
    juce::String out = words.joinIntoString (" ");
    out = out.trimCharactersAtStart (" ,;:").trimCharactersAtEnd (" ,;:");
    return out;
}

// ── _concat2 port ────────────────────────────────────────────────────────────
juce::String concat2 (const juce::String& original, const juce::String& last)
{
    const juce::String l = last.trim();
    if (l.isNotEmpty() && l != original)
        return original + ", " + l;
    return original;
}

} // namespace LoopStances
