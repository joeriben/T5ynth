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
}
