// Standalone parity test for src/inference/RepromptStances.{h,cpp}: proves the C++
// port of clap_llm_loop.py's _clean_prompt / _concat2 / per-stance builders +
// system prompts matches the verified Python algorithm.
//
// The expected values below are GROUND-TRUTHED from the Python tool
// (.venv/bin/python -c "from tools.clap_llm_loop import _clean_prompt, _concat2").
//
// Compile (macOS) — juce_core only, no generated JuceHeader.h needed:
//   c++ -std=c++17 -ObjC++ -DNDEBUG=1 -I JUCE/modules \
//     -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
//     -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 \
//     -framework Foundation -framework CoreFoundation -framework Security \
//     -framework Carbon -framework AppKit -framework IOKit \
//     JUCE/modules/juce_core/juce_core.mm \
//     JUCE/modules/juce_core/juce_core_CompilationTime.cpp \
//     src/inference/RepromptStances.cpp tools/test_reprompt_stances.cpp \
//     -o tools/test_reprompt_stances && tools/test_reprompt_stances

#include "../src/inference/RepromptStances.h"
#include <cstdio>

static int g_fail = 0;

static void check (const char* what, const juce::String& got, const juce::String& want)
{
    if (got == want)
        std::printf ("  ok   %-22s = %s\n", what, got.toRawUTF8());
    else
    {
        std::printf ("  FAIL %-22s\n      got : %s\n      want: %s\n",
                     what, got.toRawUTF8(), want.toRawUTF8());
        ++g_fail;
    }
}

static void checkTrue (const char* what, bool cond)
{
    if (cond) std::printf ("  ok   %s\n", what);
    else      { std::printf ("  FAIL %s\n", what); ++g_fail; }
}

int main()
{
    using namespace RepromptStances;

    std::printf ("cleanPrompt:\n");
    check ("label strip",   cleanPrompt ("heard: whispering wind in the pines"),
                            "whispering wind in the pines");
    check ("straight quote", cleanPrompt ("\"a deep underwater bell\""),
                             "a deep underwater bell");
    check ("label+curly",   cleanPrompt (juce::String::fromUTF8 (
                                "Prompt B: \xe2\x80\x9cglassy shards of light\xe2\x80\x9d")),
                            "glassy shards of light");
    check ("trailing FW",   cleanPrompt ("cosmic radiation and"), "cosmic radiation");
    check ("word cap 12",   cleanPrompt ("one two three four five six seven eight "
                                         "nine ten eleven twelve thirteen fourteen"),
                            "one two three four five six seven eight nine ten eleven twelve");
    check ("whitespace",    cleanPrompt ("   "), "");
    check ("first line",    cleanPrompt ("Neural ear: bright, airy\nsecond line ignored"),
                            "bright, airy");
    check ("curly single",  cleanPrompt (juce::String::fromUTF8 (
                                "\xe2\x80\x98""curly single\xe2\x80\x99")), "curly single");
    check ("trail FW comma", cleanPrompt ("metallic, resonant drone with"),
                             "metallic, resonant drone");
    // 200 chars, no spaces → cap to 120 (rsplit on a no-space prefix keeps it).
    checkTrue ("char cap 120", cleanPrompt (juce::String::repeatedString ("a", 200)).length() == 120);

    std::printf ("concat2:\n");
    check ("concat2",       concat2 ("rain on a roof", "distant thunder"),
                            "rain on a roof, distant thunder");
    check ("concat2 dup",   concat2 ("rain", "rain"), "rain");
    check ("concat2 empty", concat2 ("rain", ""), "rain");

    std::printf ("system prompts non-empty:\n");
    const char* keys[] = { "transcribe", "entkitscher", "verniedlicher",
                           "variation", "abduction", "opposite" };
    for (auto* k : keys)
        checkTrue (k, stanceSystemPrompt (k).isNotEmpty());
    checkTrue ("off → empty sysp", stanceSystemPrompt ("off").isEmpty());
    checkTrue ("planetarizer not shipped", stanceSystemPrompt ("planetarizer").isEmpty());

    // UTF-8 fidelity: the non-ASCII chars must round-trip as proper UTF-8 (NOT the
    // double-encoded mojibake the implicit const char*→String ctor would produce).
    // The em-dash U+2014 and é U+00E9 are the only non-ASCII in these prompts.
    const juce::String emdash = juce::String::fromUTF8 ("\xe2\x80\x94");
    const juce::String eacute = juce::String::fromUTF8 ("\xc3\xa9");
    checkTrue ("transcribe has real em-dash",  stanceSystemPrompt ("transcribe").contains (emdash));
    checkTrue ("verniedlicher has em-dash",    stanceSystemPrompt ("verniedlicher").contains (emdash));
    checkTrue ("entkitscher has em-dash",      stanceSystemPrompt ("entkitscher").contains (emdash));
    checkTrue ("entkitscher has real é",       stanceSystemPrompt ("entkitscher").contains (eacute));
    // mojibake guard: the double-encoded em-dash starts with \xc3\xa2 (Ã¢) — must be absent.
    checkTrue ("no mojibake em-dash",
               ! stanceSystemPrompt ("transcribe").contains (juce::String::fromUTF8 ("\xc3\xa2\xc2\x80\xc2\x94")));

    std::printf ("user turns:\n");
    juce::StringArray recent { "old scene a", "old scene b" };
    check ("abduction turn",
           buildStanceUserTurn ("abduction", "bright, metallic", "prev b", recent, "warm, tonal"),
           "Heard: bright, metallic\nAlready tried (do not reuse): old scene a / old scene b");
    check ("abduction no recent",
           buildStanceUserTurn ("abduction", "bright, metallic", "prev b", {}, ""),
           "Heard: bright, metallic");
    check ("transcribe turn",
           buildStanceUserTurn ("transcribe", "bright, metallic", "prev b", recent, "warm, tonal"),
           "Neural ear: bright, metallic\nSpectral: warm, tonal");
    check ("transcribe no spectral",
           buildStanceUserTurn ("transcribe", "bright, metallic", "prev b", recent, ""),
           "Neural ear: bright, metallic");
    check ("variation turn",
           buildStanceUserTurn ("variation", "bright", "shimmering bells", {}, ""),
           "Current Prompt B: \"shimmering bells\"\nHeard now: bright");
    check ("entkitscher turn",
           buildStanceUserTurn ("entkitscher", "bright", "sunset romance", {}, ""),
           "Current prompt: \"sunset romance\"\nHeard: bright");
    checkTrue ("off → empty turn",
               buildStanceUserTurn ("off", "x", "y", {}, "").isEmpty());

    std::printf (g_fail == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
