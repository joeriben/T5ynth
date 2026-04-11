# T5ynth — Claude Code Rules

## Session Start

At the beginning of every session:
1. Run `git log --oneline -10` and `git status` — do NOT reconstruct state from memory
2. If the user reports a crash or bug: that is the ONLY priority. No feature work until resolved.
3. Read memory for context, but verify against current code before acting on it.

## Mandatory Process

- NEVER write code before reading ALL relevant files completely.
- NEVER assume — verify every claim by reading code. If tempted to say "probably": investigate instead.
- After EVERY code change: spawn a verification agent (model: opus) with the prompt "This code has a bug. Find it." Only proceed if none found.
- Before EVERY build: spawn a safety agent checking JUCE destruction order, audio thread safety, and crash vectors.
- State your reasoning BEFORE acting, not after.
- If a change spans >2 files: read ALL files first, state the complete signal flow, THEN code.
- One concern per commit. Never batch unrelated changes.

## JUCE Safety (BLOCKING — must check before every build)

1. Every juce::Timer subclass MUST have `stopTimer()` in its destructor, BEFORE any member is destroyed.
2. NEVER call `setLookAndFeel(nullptr)` — causes WindowServer crash on macOS.
3. APVTS attachments (unique_ptr) must be destroyed BEFORE their target components — check declaration order in .h.
4. NEVER allocate memory, lock a mutex, or do file I/O on the audio thread.
5. A destructor crash in a JUCE app crashes the macOS WindowServer, killing ALL GUI applications.

## Build

- Always `build_clean/` with Release config.
- Build and verify as part of every change — don't ask.
- E2E with installed app (`/Applications/T5ynth.app`) before release tags.

## Quality

- Never hallucinate names — grep the repo first. Canonical: "UCDCAE AI Lab".
- Never add UI elements the user didn't request.
- Never modify preset files (.t5p) or binary resources without explicit permission.
- Never insert editorial commentary into documentation.
- Consistent UI: colored header bar, module-specific colors, match SynthPanel quality.

## Agent Spawns

- Always set `model: "opus"` on verification and planning agents.
- Use agents for verification, not as a substitute for reading code yourself.
