#pragma once
// juce_core ONLY (juce::String / juce::StringArray) — this module is deliberately
// free of juce_graphics/juce_audio_* and of PipeInference, so it stays
// unit-testable in isolation (tools/test_reprompt_stances.cpp) and keeps the backend
// `interpret` op generic. Including the full <JuceHeader.h> aggregate here would
// drag in the graphics/audio modules this logic does not use.
#include <juce_core/juce_core.h>

/**
 * Stance / coupling logic for the CLAP→LLM semantic self-listening loop.
 *
 * A faithful C++ port of the curated subset of tools/clap_llm_loop.py (the
 * verified, working algorithm). This module is the interpreter HALF of the loop
 * on the plugin side: it provides the per-stance system prompts, the per-stance
 * user-turn builders, the leak/overrun cleaner, and the concat coupling helper.
 *
 * Self-contained on purpose — it depends only on juce_core (juce::String /
 * juce::StringArray), NOT on PipeInference and NOT on any audio/DSP code. That
 * keeps the backend `interpret` op generic (the stance lives here, not in the
 * subprocess) and makes this logic unit-testable in isolation.
 *
 * Stance keys mirror BlockParams.h RepromptStance::kEntries[i].key, which in turn
 * mirror the keys of clap_llm_loop.py's MODES dict. The SIX shipped stances are:
 *   transcribe, entkitscher, verniedlicher, variation, abduction, opposite.
 * (planetarizer/develop/critique from the tool are intentionally NOT ported —
 * too complex for the local Qwen2.5-1.5B interpreter; see clap_llm_loop notes.)
 */
namespace RepromptStances
{
    /** The system prompt (the "stance") for a given stance key, VERBATIM from
     *  clap_llm_loop.py MODES[key]. Empty for an unknown key or "off". */
    juce::String stanceSystemPrompt (const juce::String& stanceKey);

    /** Build the per-turn user text for a stance, mirroring the matching
     *  build(tags, prev_b, recent, spectral) closure in clap_llm_loop.py.
     *
     *  @param stanceKey   one of the six shipped stance ids
     *  @param tags        the CLAP top-k timbre words for the latest render
     *                     (comma-joined), i.e. the tool's `tags`
     *  @param prevPolePrompt  that pole's previous prompt — the chain's own last
     *                     link (`*_glieder[-1]`), NOT the applied concat prompt
     *  @param recentList  that pole's anti-stasis memory (its last ≤3 links,
     *                     `*_glieder[-3:]`); only the abduction/opposite stances
     *                     read it ("already tried, do not reuse")
     *  @param spectral    DSP spectral words; only the transcribe stance reads it
     *  @return the user-turn string; empty for an unknown key or "off". */
    juce::String buildStanceUserTurn (const juce::String& stanceKey,
                                      const juce::String& tags,
                                      const juce::String& prevPolePrompt,
                                      const juce::StringArray& recentList,
                                      const juce::String& spectral);

    /** Port of clap_llm_loop.py `_clean_prompt`: keep the first real line, strip
     *  label echoes (heard:/neural ear:/current prompt:/…) and wrapping quotes
     *  (straight or curly), cap to ≤maxWords words / ≤maxChars chars, and drop
     *  trailing function words so the prompt ends on a content word. Load-bearing:
     *  small instruct models leak labels/quotes and overrun t5gemma's token
     *  budget. */
    juce::String cleanPrompt (const juce::String& raw,
                              int maxChars = 120, int maxWords = 12);

    /** Port of clap_llm_loop.py `_concat2`: the concat (ab_add) coupling's
     *  generation prompt = the human ORIGINAL (first impulse) + ", " + ONLY the
     *  latest interpretation — never the accumulated chain (keeps the prompt
     *  short; the chain itself runs on its own last link elsewhere). */
    juce::String concat2 (const juce::String& original, const juce::String& last);

    /** Split a trailing RUN of musical CONTROL tokens off the end of a prompt so
     *  LLM rewrites (every loop stance + the in-place translation) leave the
     *  user's appended pitch/tempo anchors UNCHANGED. Two token kinds, case-
     *  insensitive:
     *    • scientific-pitch notes — A–G, optional ASCII accidental (#, ##, b, bb),
     *      octave -1 | 0–10  (c3, C#4, Db5, A0)
     *    • tempo — 1–3 digits (optional decimals), optional space, "bpm"
     *      (120bpm, 90 bpm, 128.5 BPM)
     *  Stable Audio conditions tempo on "…bpm" and a trailing note pins register,
     *  so these must survive verbatim. Trailing-only by design: a note/tempo word
     *  INSIDE the description is left for the rewrite. ASCII accidentals only (the
     *  unicode ♯/♭ are deliberately not matched, keeping the match pure-ASCII so
     *  the byte cut is UTF-8-safe).
     *  @return the matched trailing run VERBATIM (incl. its leading separator),
     *          or empty if the prompt has no trailing musical tokens. */
    juce::String trailingMusicSuffix (const juce::String& prompt);

    /** The descriptive CORE: the prompt with its trailingMusicSuffix() removed,
     *  trimmed. Feed this to the LLM (translate / stance interpret) so it never
     *  sees — and cannot mangle — the control tokens. */
    juce::String stripMusicSuffix (const juce::String& prompt);

    /** Re-attach a suffix from trailingMusicSuffix() to a rewritten core,
     *  normalising ONLY the core↔suffix junction to ", " (token spelling and
     *  inter-token separators are preserved). Empty suffix → core.trim(); empty
     *  core → the bare tokens. */
    juce::String reattachMusicSuffix (const juce::String& core, const juce::String& suffix);
}

// ── Structured musical parse ─────────────────────────────────────────────────
// Phase 1 of "T5ynth understands its prompts": turn the trailing pitch/tempo run
// (the same tokens RepromptStances::trailingMusicSuffix preserves) into STRUCTURED
// data — pitch classes, octaves, MIDI numbers, tempo — so later consumers (the
// sequencer's BPM, scaleRoot, note seeding) can use it. Purely additive: the
// preservation path is unchanged; this just exposes what the tokens MEAN.
//
// Source-of-truth note: parse the USER's appended tokens (intentional control),
// NOT the LLM's prose (hallucination-prone, and "the machine's musical reading"
// is a bias to SURFACE, not to obey). juce_core-only, like the rest of this file.
namespace Music
{
    struct Note
    {
        int pitchClass = 0;   // 0=C, 1=C#/Db … 11=B (accidentals resolved, mod 12)
        int octave     = 0;   // scientific-pitch octave as written (the digits after the note)
        int midiNote   = 0;   // MIDI number, SPN convention: A4→69, C4(middle C)→60, i.e. (octave+1)*12+pc.
                              // (If T5ynth adopts C3=60 elsewhere, that's a fixed +12 the consumer applies.)
        juce::String text;    // the token VERBATIM as the user wrote it ("C#3", "db5")
    };

    struct Spec
    {
        juce::String core;        // the prompt minus its trailing musical run (== stripMusicSuffix), trimmed
        juce::String verbatim;    // the trailing run VERBATIM (== trailingMusicSuffix); empty when none
        juce::Array<Note> notes;  // parsed pitch tokens, in source order
        bool  hasBpm = false;
        float bpm     = 0.0f;     // parsed tempo (if several bpm tokens, the LAST wins)
        juce::String bpmText;     // the bpm token VERBATIM ("120bpm", "90 bpm")

        bool hasMusic() const { return verbatim.isNotEmpty(); }
    };

    /** Parse a prompt's trailing musical run into a Spec. Spec::core/verbatim mirror
     *  RepromptStances::stripMusicSuffix/trailingMusicSuffix exactly (so the existing
     *  preservation behaviour is unchanged); notes/bpm add the structured reading. */
    Spec parse (const juce::String& prompt);
}
