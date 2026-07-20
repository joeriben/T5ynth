#!/usr/bin/env python3
"""Compare entkitscher (De-Kitsch) stance system-prompts on the REAL small LLM.

The shipped 'entkitscher' prompt is SUBTRACTIVE ("find the cliché and REMOVE it")
with three internal negations — which the local Qwen2.5-1.5B handles poorly
(user report: "de-kitsch funktioniert nicht gut, llm wohl zu klein"). This harness
runs the OLD prompt against POSITIVE reframes ("Versachlichung / Sachlichkeit /
nüchterne Version") on a set of deliberately kitschy (prev_prompt, tags) inputs,
over the same `interpret` IPC op the plugin uses, so we can pick the variant the
small model actually follows.

Run:  .venv/bin/python tools/test_entkitscher_prompt.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"

# The shipped prompt (RepromptStances.cpp syspEntkitscher / clap_llm_loop _mode_entkitscher).
OLD = (
    "You de-kitsch a sound. Find the clichéd, sentimental, conventional, "
    "over-expected qualities in what is heard and REMOVE them — keep the same "
    "subject and sound-type, do NOT invert the meaning and do NOT make it harsh, "
    "just strip the cliché. Rebuild from concrete, specific, unsentimental sonic "
    "detail. Describe positively (never 'no', 'without', 'not'). "
    "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label."
)

# Candidate B: plainer, no persona, single positive transform, NO example.
NEW_B = (
    "You rewrite a sound description in plain, sober language. Restate the current "
    "prompt as the SAME sound described factually and matter-of-factly, in plain "
    "neutral words — only what is acoustically there, no story and no decoration. "
    "Keep the subject and the sound-type. "
    "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label."
)

# Candidate C: plain + one worked example (the strongest small-LLM lever).
NEW_C = (
    "You rewrite a sound description in plain, sober language. Restate the current "
    "prompt as the SAME sound named factually, the way a sound engineer's notes "
    "would: state the physical sound and its source in neutral acoustic words, and "
    "leave out the poetic, emotional and scene-setting words. Keep the subject and "
    "the sound-type. Example: \"the warm embrace of a mother's lullaby\" becomes "
    "\"soft low vocal hum\". "
    "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label."
)

# Candidate D: engineer persona + the same worked example, minimal.
# FINAL/shipped wording — ASCII-only (colon instead of em-dash) to avoid the UTF-8
# mojibake class of bug that bit the stance prompts in Phase B.
NEW_D = (
    "You are a sound engineer writing plain notes. Rewrite the current prompt as a "
    "sober, factual description of the SAME sound: name the physical sound and its "
    "source in neutral acoustic words, leaving out emotion, story and atmosphere. "
    "Example: \"the warm embrace of a mother's lullaby\" becomes \"soft low vocal "
    "hum\". "
    "Reply with ONLY one short prompt (3 to 10 words) - no quotes, no label."
)

CANDIDATES = {"OLD": OLD, "NEW_D (FINAL eng+ex)": NEW_D}

# Deliberately kitschy prompt + the timbre tags a machine ear would hear.
CASES = [
    ("a deep underwater bell of eternal longing",     "resonant, metallic, dark, sustained, low"),
    ("heavenly choir of angels in golden light",       "bright, airy, vocal, reverberant, warm"),
    ("the lonely cry of a forgotten soul",             "hollow, breathy, mournful, distant, thin"),
    ("magical sparkling fairy-dust dreams",            "glassy, shimmering, high, delicate, tinkling"),
    ("majestic sunrise over an endless ocean of hope", "swelling, lush, broad, bright, evolving"),
]


def user_turn(prev, tags):
    return f'Current prompt: "{prev}"\nHeard: {tags}'


def main():
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    try:
        for prev, tags in CASES:
            print("\n" + "=" * 78)
            print(f'KITSCH IN : "{prev}"')
            print(f"  heard   : {tags}")
            for name, sysp in CANDIDATES.items():
                # No max_new_tokens: omitting the key lets run_instruct size the
                # reply to the real model context. A cap here would truncate a
                # candidate stance mid-sentence and make it look like the stance
                # rambles or trails off — i.e. it would bias the very comparison
                # this tool exists to make.
                out = client.request_text({
                    "mode": "interpret",
                    "system_prompt": sysp,
                    "prompt_a": user_turn(prev, tags),
                    "device": "cpu",
                }).strip()
                print(f"  {name:18s}: {out}")
    finally:
        client.close() if hasattr(client, "close") else None


if __name__ == "__main__":
    main()
