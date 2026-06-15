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
#include <cmath>
#include <clocale>

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

    // Best-effort: run under a comma-decimal locale so the bpm parse is proven
    // locale-independent (a regression to std::strtof/atoi would misread "128.5"
    // → 128 here). No-op if the locale isn't installed (then C locale stays).
    if (std::setlocale (LC_NUMERIC, "de_DE.UTF-8") || std::setlocale (LC_NUMERIC, "de_DE"))
        std::printf ("(comma-decimal LC_NUMERIC active)\n");

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

    // Trailing pitch/tempo preservation (NEW C++ helpers, not ported from Python):
    // keep user-appended c3 / 120bpm anchors verbatim through every LLM rewrite.
    std::printf ("music suffix:\n");
    check ("suf note+bpm",   trailingMusicSuffix ("warm analog pad, c3, 120bpm"), ", c3, 120bpm");
    check ("suf bpm space",  trailingMusicSuffix ("deep bass 90 bpm"), " 90 bpm");
    check ("suf accidental", trailingMusicSuffix ("bell Db5"), " Db5");
    checkTrue ("suf none mid",   trailingMusicSuffix ("warm c3 pad").isEmpty());    // not trailing
    checkTrue ("suf none word",  trailingMusicSuffix ("drum and bass").isEmpty());  // false-pos guard
    checkTrue ("suf none num",   trailingMusicSuffix ("house 124").isEmpty());      // bare number, no bpm
    checkTrue ("suf none no-oct", trailingMusicSuffix ("warm pad b").isEmpty());    // bare note, no octave
    check ("core",           stripMusicSuffix ("warm analog pad, c3, 120bpm"), "warm analog pad");
    check ("core none",      stripMusicSuffix ("techno beat"), "techno beat");
    checkTrue ("core token-only empty", stripMusicSuffix ("120bpm").isEmpty());     // pole was only a token
    check ("reattach",            reattachMusicSuffix ("glassy bell", ", c3, 120bpm"), "glassy bell, c3, 120bpm");
    check ("reattach empty suf",  reattachMusicSuffix ("glassy bell", ""), "glassy bell");
    check ("reattach empty core", reattachMusicSuffix ("", "120bpm"), "120bpm");
    check ("reattach no dbl comma", reattachMusicSuffix ("warm pad,", ", c3"), "warm pad, c3");
    // token-only pole composed shape: must NOT leak a leading comma.
    check ("token-only pole shape", reattachMusicSuffix ("glassy bell", trailingMusicSuffix ("120bpm")),
                                    "glassy bell, 120bpm");

    // Phase 1 structured parse: the trailing run → notes (pc/octave/MIDI) + tempo.
    std::printf ("music parse:\n");
    {
        auto s = Music::parse ("warm analog pad, c3, 120bpm");
        check     ("parse core",       s.core, "warm analog pad");
        check     ("parse verbatim",   s.verbatim, ", c3, 120bpm");
        checkTrue ("parse c3 pc=0",    s.notes.size() == 1 && s.notes[0].pitchClass == 0);
        checkTrue ("parse c3 oct=3",   s.notes.size() == 1 && s.notes[0].octave == 3);
        checkTrue ("parse c3 midi=48", s.notes.size() == 1 && s.notes[0].midiNote == 48);
        checkTrue ("parse bpm=120",    s.hasBpm && std::abs (s.bpm - 120.0f) < 0.01f);
    }
    {
        auto s = Music::parse ("bell C#3 Db5");
        checkTrue ("parse 2 notes",    s.notes.size() == 2);
        checkTrue ("enharmonic C#==Db",
                   s.notes.size() == 2 && s.notes[0].pitchClass == 1 && s.notes[1].pitchClass == 1);
        checkTrue ("octaves 3 & 5",
                   s.notes.size() == 2 && s.notes[0].octave == 3 && s.notes[1].octave == 5);
        checkTrue ("no bpm",           ! s.hasBpm);
    }
    {
        auto s = Music::parse ("a4");
        checkTrue ("A4 midi=69",       s.notes.size() == 1 && s.notes[0].midiNote == 69);  // A440 anchor
    }
    {
        auto s = Music::parse ("128.5 bpm");
        checkTrue ("decimal bpm",      s.hasBpm && std::abs (s.bpm - 128.5f) < 0.01f);
        checkTrue ("bpm-only no notes", s.notes.isEmpty());
    }
    {
        auto s = Music::parse ("techno beat");
        checkTrue ("no music",         ! s.hasMusic() && s.notes.isEmpty());
    }
    {
        // A/B kept separate: A carries only a note, B only a tempo.
        auto ab = Music::parseAB ("warm pad c3", "deep bass 90bpm");
        checkTrue ("AB a-note c3",
                   ab.a.notes.size() == 1 && ab.a.notes[0].pitchClass == 0 && ab.a.notes[0].octave == 3);
        checkTrue ("AB a no bpm",   ! ab.a.hasBpm);
        checkTrue ("AB b bpm=90",   ab.b.hasBpm && std::abs (ab.b.bpm - 90.0f) < 0.01f);
        checkTrue ("AB b no notes", ab.b.notes.isEmpty());
    }

    std::printf ("system prompts non-empty:\n");
    const char* keys[] = { "transcribe", "entkitscher", "verniedlicher",
                           "variation", "abduction", "opposite" };
    for (auto* k : keys)
        checkTrue (k, stanceSystemPrompt (k).isNotEmpty());
    checkTrue ("off → empty sysp", stanceSystemPrompt ("off").isEmpty());
    checkTrue ("planetarizer not shipped", stanceSystemPrompt ("planetarizer").isEmpty());

    // UTF-8 fidelity: the non-ASCII chars must round-trip as proper UTF-8 (NOT the
    // double-encoded mojibake the implicit const char*→String ctor would produce).
    // The em-dash U+2014 is the only non-ASCII left, in transcribe + verniedlicher.
    // entkitscher was reframed (Versachlichung) to an ASCII-only prompt on purpose,
    // so it must NOT use fromUTF8 and carries no em-dash/é.
    const juce::String emdash = juce::String::fromUTF8 ("\xe2\x80\x94");
    checkTrue ("transcribe has real em-dash",  stanceSystemPrompt ("transcribe").contains (emdash));
    checkTrue ("verniedlicher has em-dash",    stanceSystemPrompt ("verniedlicher").contains (emdash));
    checkTrue ("entkitscher ascii (no em-dash)", ! stanceSystemPrompt ("entkitscher").contains (emdash));
    checkTrue ("entkitscher worked example",    stanceSystemPrompt ("entkitscher").contains ("soft low vocal hum"));
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
