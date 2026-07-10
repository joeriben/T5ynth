#include "RepromptStances.h"
#include <regex>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Faithful C++ port of the curated subset of tools/clap_llm_loop.py.
//
// The system prompts and user-turn builders are reproduced VERBATIM from the
// tool's MODES factories (the verified algorithm). Where the Python f-strings
// interpolate the changing per-turn state, the C++ does the same string build.
// ─────────────────────────────────────────────────────────────────────────────

namespace RepromptStances
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
    // fromUTF8: the em-dash (\xe2\x80\x94) is UTF-8 — the implicit const char*→String
    // ctor would re-encode the bytes from Latin-1 and produce mojibake (the Phase B
    // bug-hunt finding); fromUTF8 takes the bytes verbatim as UTF-8.
    return juce::String::fromUTF8 (
           "You are a machine transcription engine for a text-to-audio synthesizer. You "
           "receive ONLY machine measurements of the latest sound: the timbre words a "
           "neural ear matched, and signal-level spectral descriptors. Compose them "
           "LITERALLY into ONE short generation prompt (3 to 8 words) describing those "
           "measured qualities as a sound. Add NO scene, NO story, NO metaphor, NO place "
           "and NO human imagery \xe2\x80\x94 only the measured sonic attributes. "
           "Reply with ONLY the prompt - no quotes, no label, no explanation.");
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
    // Positive "Versachlichung" reframe (2026-06-16): the small local Qwen2.5-1.5B
    // followed the old subtractive "find the cliche and REMOVE it" framing (three
    // internal negations) poorly — it often stayed kitschy or invented new scenes
    // (user report). A direct "restate it soberly/factually" transform + ONE worked
    // example is what a 1.5B model reliably follows. ASCII-only (no fromUTF8) so the
    // Phase B mojibake class cannot recur. Verified vs the old prompt on the real
    // interpret op (tools/test_entkitscher_prompt.py). Keep in sync with
    // clap_llm_loop.py _mode_entkitscher.
    return "You are a sound engineer writing plain notes. Rewrite the current prompt as a "
           "sober, factual description of the SAME sound: name the physical sound and its "
           "source in neutral acoustic words, leaving out emotion, story and atmosphere. "
           "Example: \"the warm embrace of a mother's lullaby\" becomes \"soft low vocal "
           "hum\". "
           "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.";
}

static juce::String syspVerniedlicher()
{
    // fromUTF8: the em-dash (\xe2\x80\x94) is UTF-8 — see syspTranscribe.
    return juce::String::fromUTF8 (
           "You are a narrator of gentle magical realism for sound. Rewrite the prompt so it "
           "feels emotionally safe for a child yet sonically fascinating: turn the "
           "threatening into the wondrous and the harsh into the mysterious \xe2\x80\x94 REINTERPRET, "
           "do not censor (a conflict becomes a riddle, darkness becomes shelter). Keep the "
           "core but heal its impact, an aesthetic of warmth and wonder. "
           "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label.");
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

// ── DCO per-stance user-turn builder (self-READING twin of buildStanceUserTurn) ──
// Same six stances, same per-stance shape/tone as buildStanceUserTurn above, but
// every input is the DCO router's own machine-readable reading of the last bake
// (resolved + recipe facts + flags), never CLAP tags/spectral words — see the
// header doc and docs/DCO_REPROMPT_CONCEPT.md ("lesen -> deuten -> umformulieren").
juce::String buildDcoStanceUserTurn (const juce::String& stanceKey,
                                     const juce::String& machineReading,
                                     const juce::String& flagsLine,
                                     const juce::String& prevPrompt,
                                     const juce::StringArray& recentList)
{
    // Identical shape to buildStanceUserTurn's local triedClause; duplicated
    // rather than shared, mirroring this file's existing style of one small
    // lambda per builder (buildStanceUserTurn does the same above).
    auto triedClause = [&recentList] () -> juce::String
    {
        if (recentList.isEmpty()) return {};
        return "\nAlready tried (do not reuse): " + recentList.joinIntoString (" / ");
    };

    if (stanceKey == "transcribe")
    {
        // The shared system prompt asks for "machine measurements" from "a
        // neural ear" / "spectral descriptors" — machineReading IS that
        // measurement here (resolved + recipe facts), so it slots into the same
        // shape. flagsLine surfaces what the router could NOT map, mirroring
        // transcribe's "measured qualities only, no scene/story/metaphor" brief.
        const juce::String notUnderstood = flagsLine.isNotEmpty()
            ? ("\nNot understood: " + flagsLine) : juce::String();
        return "Machine reading: " + machineReading + notUnderstood;
    }
    if (stanceKey == "abduction")
    {
        // Bare description of what the oscillator does (no prevPrompt) — mirrors
        // buildStanceUserTurn's "Heard: {tags}{tried}" shape.
        return "The oscillator does: " + machineReading + triedClause();
    }
    if (stanceKey == "opposite")
    {
        // DCO opposite is BETTER defined than neural's opposite (docs/
        // DCO_REPROMPT_CONCEPT.md, "Die sechs Stances im DCO"): the lexicon's
        // bipolar adjective/motion axes give it a real prompt to invert, so —
        // unlike neural's opposite, which reads only tags — the DCO version
        // also carries prevPrompt.
        return "Current prompt: \"" + prevPrompt + "\"\nThe machine read it as: "
             + machineReading + triedClause();
    }
    if (stanceKey == "entkitscher")
    {
        return "Current prompt: \"" + prevPrompt + "\"\nMachine reading: " + machineReading;
    }
    if (stanceKey == "verniedlicher")
    {
        return "Current prompt: \"" + prevPrompt + "\"\nMachine reading: " + machineReading;
    }
    if (stanceKey == "variation")
    {
        return "Current prompt: \"" + prevPrompt + "\"\nThe machine read it as: " + machineReading;
    }
    return {};   // "off" or unknown
}

// ── DCO/LCO reference-vocabulary constraint (Re-Prompt grounding) ────────────
// Appended to the LCO re-prompt user turn ONLY (the neural loop never calls
// this). `referenceVocabulary` is the backend brief verbatim — the exact set of
// words backend/dco_recipe.py's scanner resolves — so a rewrite that stays
// inside it is one this same pipeline can actually read. Empty in → empty out
// (an old backend without the field, or before the first bake): the turn is
// unchanged and behaves exactly as it did before this grounding existed.
juce::String dcoVocabularyConstraintBlock (const juce::String& referenceVocabulary)
{
    if (referenceVocabulary.trim().isEmpty())
        return {};
    return "\n\nBuild the new prompt using ONLY the synthesizer's own vocabulary "
           "below. These are the exact words the instrument can read; any other "
           "word is silently ignored, so a prompt built from outside words changes "
           "nothing. Recombine them freely.\n\n"
         + referenceVocabulary.trim();
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
            "current prompt b:", "current prompt:",
            // DCO Re-Prompt user-turn labels (buildDcoStanceUserTurn below) —
            // a leaked echo here would author into spurious lexicon flags,
            // corrupting the honesty channel. Mirrored in _clean_prompt
            // (tools/clap_llm_loop.py), which stays the source of truth.
            "machine reading:", "the oscillator does:",
            "the machine read it as:", "not understood:"
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

// ── trailing musical-token preservation (pitch + tempo) ──────────────────────
// User-appended scientific-pitch notes (c3, C#4, Db5…) and tempo markers
// (120bpm, 90 bpm) are GENERATION control tokens, not description: Stable Audio
// conditions tempo on "…bpm", a trailing note pins register. Every LLM rewrite —
// all loop stances AND the in-place translation — would otherwise paraphrase or
// drop them. So we split a trailing RUN of such tokens off the end, rewrite only
// the descriptive core, and re-append the run verbatim.
//
// The match is pure-ASCII (ASCII accidentals #/b only; \s/[,;] separators), so a
// byte-offset cut lands on a UTF-8 boundary even when the core holds multi-byte
// characters. Trailing-only via the $ anchor; regex_search's leftmost match from
// the earliest separator that reaches $ yields the MAXIMAL trailing run.
namespace {
    const std::regex& trailingMusicRe()
    {
        // One token: tempo (1–3 digits, optional decimals, optional space, "bpm")
        // OR scientific-pitch note (A–G, optional #/##/b/bb, octave -1|10|0–9).
        static const std::string tok =
            R"((?:[0-9]{1,3}(?:\.[0-9]+)?[ \t]*bpm|[a-g](?:##|#|bb|b)?(?:-1|10|[0-9])))";
        // (start | separator) token  (separator token)*  trailing-space  END
        static const std::regex re (
            R"((?:^|[\s,;])[ \t]*)" + tok + R"((?:[\s,;]+)" + tok + R"()*\s*$)",
            std::regex::icase | std::regex::optimize);
        return re;
    }
}

juce::String trailingMusicSuffix (const juce::String& prompt)
{
    const std::string s = prompt.toStdString();
    std::smatch m;
    if (std::regex_search (s, m, trailingMusicRe()))
    {
        const std::string run = m.str (0);   // the trailing run, verbatim (incl. leading separator)
        return juce::String (juce::CharPointer_UTF8 (run.c_str()));
    }
    return {};
}

juce::String stripMusicSuffix (const juce::String& prompt)
{
    const std::string s = prompt.toStdString();
    std::smatch m;
    if (std::regex_search (s, m, trailingMusicRe()))
    {
        const std::string core = s.substr (0, (size_t) m.position (0));   // ASCII cut → UTF-8-safe
        return juce::String (juce::CharPointer_UTF8 (core.c_str())).trim();
    }
    return prompt.trim();
}

juce::String reattachMusicSuffix (const juce::String& core, const juce::String& suffix)
{
    juce::String c = core.trim();
    while (c.isNotEmpty())   // drop trailing separators so the ", " junction can't double up
    {
        const auto ch = c.getLastCharacter();
        if (ch == ',' || ch == ';')
            c = c.dropLastCharacters (1).trim();
        else
            break;
    }
    juce::String suf = suffix;
    while (suf.isNotEmpty())   // drop leading separators; we re-add a canonical ", " junction
    {
        const auto ch = suf[0];
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            suf = suf.substring (1);
        else
            break;
    }
    suf = suf.trim();
    if (suf.isEmpty()) return c;
    if (c.isEmpty())   return suf;
    return c + ", " + suf;
}

} // namespace RepromptStances

// ── Structured musical parse (Phase 1) ───────────────────────────────────────
namespace Music
{
    Spec parse (const juce::String& prompt)
    {
        Spec spec;
        spec.verbatim = RepromptStances::trailingMusicSuffix (prompt);
        spec.core     = RepromptStances::stripMusicSuffix (prompt);
        if (spec.verbatim.isEmpty())
            return spec;   // no trailing tokens → core mirrors the (trimmed) prompt, no notes/bpm

        // Token grammar: group 1 = tempo number | groups 2/3/4 = note letter / accidental
        // / octave. Iterated over the verbatim run; sregex_iterator skips the separators.
        static const std::regex tokRe (
            R"(([0-9]{1,3}(?:\.[0-9]+)?)[ \t]*bpm|([a-g])(##|#|bb|b)?(-1|10|[0-9]))",
            std::regex::icase);
        static const int basePc[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a,b,c,d,e,f,g → pitch class

        const std::string run = spec.verbatim.toStdString();
        for (auto it = std::sregex_iterator (run.begin(), run.end(), tokRe);
             it != std::sregex_iterator(); ++it)
        {
            const std::smatch& m = *it;
            // juce::String::getFloat/IntValue parse '.'-decimal locale-INDEPENDENTLY;
            // std::strtof/atoi would misread "128.5" in a comma-decimal locale (de_DE).
            if (m[1].matched)                       // tempo (last bpm token wins)
            {
                spec.hasBpm  = true;
                spec.bpm     = juce::String (m[1].str().c_str()).getFloatValue();
                spec.bpmText = juce::String (juce::CharPointer_UTF8 (m[0].str().c_str()));
            }
            else if (m[2].matched)                  // pitch note
            {
                Note n;
                n.text = juce::String (juce::CharPointer_UTF8 (m[0].str().c_str()));
                char letter = m[2].str()[0];
                if (letter >= 'A' && letter <= 'G') letter = (char) (letter + 32);   // ASCII lower, locale-free
                int pc = basePc[letter - 'a'];
                if (m[3].matched)
                    for (char ch : m[3].str())      // each accidental shifts a semitone
                    {
                        if      (ch == '#')                ++pc;
                        else if (ch == 'b' || ch == 'B')   --pc;
                    }
                n.pitchClass = ((pc % 12) + 12) % 12;
                n.octave     = juce::String (m[4].str().c_str()).getIntValue();
                n.midiNote   = (n.octave + 1) * 12 + n.pitchClass;   // SPN: A4→69
                spec.notes.add (n);
            }
        }
        return spec;
    }

    ABSpec parseAB (const juce::String& promptA, const juce::String& promptB)
    {
        return { parse (promptA), parse (promptB) };   // independent — no cross-pole merge
    }
} // namespace Music
