# FINDINGS — 2026-07-21 — Does the 7B map language onto the instrument parameters?

**Status (2026-07-24, verified against code and git history).** The
mechanism this document measured — `dco_llm_map.py`'s word→KEY routing
feeding Python-side anchor tables (`csound_orch.py`'s `_DRUM_ANCHORS`/
`_FMEP_ANCHORS`/`_PARAM_SCHEMAS`) — no longer exists; `csound_orch.py` is
deleted (`3728a42f`, 2026-07-22) and the model now writes numeric parameter
values directly into the Csound it authors. The specific measurement (2/15
prompts reached the param syntax, `spot` never) is therefore a fact about a
dead mechanism, not a current number — nobody has re-run an equivalent
probe against the current architecture. The design lesson it produced is
visibly live, though: the current library's `anchor_code` field
(`docs/plans/HANDOVER_LCO.md` §2, "the same instrument rendered at each
anchor of its character axis, so the model can see which numbers move with
which word") reads as a direct answer to this finding's root cause. Current
architecture and the seven parametrised instruments:
`docs/plans/HANDOVER_LCO.md` §2.

Question (BJ): before extending the parametrised LCO instruments (fm_ep, drum_head, …),
clarify how a 7B is supposed to map natural language and metaphors onto discrete jargon
anchors like `spot=centre` "even halfway sensibly".

Answered **empirically**, not by argument: `tools/lco_param_map_probe.py` drives the real
7B over the actual plugin wire (`mode=csound`, pipe_inference IPC) with 15 prompts spanning
direct → synonym → metaphor → far-metaphor across both parametrised instruments, and dumps
the structured `oscillators` field so the params the model REALLY set are visible.

Reproduce: `.venv/bin/python tools/lco_param_map_probe.py`
Raw: `tools/lco_param_map_out/pmap_results.json` (untracked, per the *_out convention).

## How the mechanism is meant to work

The 7B does **not** invent a float. The catalogue it is shown carries, per parametrised key,
a `params:` line of anchor WORDS (`backend/dco_llm_map.py:80` `_params_line`), e.g.:

```
drum_head: … spot = struck in the centre or at the rim, tension = slack and dull to
tight and bright, damping = ringing open or muffled to a thud
  params: pitched=tom|mixed|timpani  spot=centre|halfway|rim  tension=slack|normal|tight  damping=open|damped|muffled
```

The model picks ONE word from a closed 3–4 word enum; a Python table then maps word→number
(`backend/csound_orch.py` `_DRUM_ANCHORS` / `_FMEP_ANCHORS`, ~742–765). Only three keys are
parametrised at all (`_PARAM_SCHEMAS`, ~771): `analog_osc`, `fm_ep`, `drum_head`. Everything
else (rhodes, wurlitzer, clarinet, glass, pulse, …) has **no** params.

## Measured result — params set on 2 / 15 prompts

| Prompt | expected | **actually set** | route |
|---|---|---|---|
| a clangy bright metallic electric piano | ting=clangy | — | metallic_fm › fm_ep + adj |
| a soft mellow rhodes with no metal | ting=none | — | **rhodes** (non-param key) |
| a hollow nasal reed piano | reed=reed | — | **clarinet** (!) |
| an electric piano struck hard, biting | strike=hard | **ting=1.0** ⚠ wrong axis | fm_ep |
| an e-piano like little glass bells | ting/reed↑ | — | fm_ep › glass |
| a warm woody rhodes, felt not metal | ting=none reed=full | — | rhodes › glass |
| a rhodes that sounds like it is underwater | soft/dark | — | rhodes + adj |
| a dead muffled tom, no ring | damping=muffled | — | drum_head |
| a bright tight singing timpani | tension=tight pitched=timpani | **pitched=1.0 tension=1.0** ✓ | drum_head › glass |
| a thin hard edgy **rimshot** drum | **spot=rim** | — | drum_head + adj "thin, edgy" |
| a deep round drum hit **dead in the centre** | **spot=centre** | — *(literal "centre")* | drum_head |
| a boomy **slack** floor tom ringing **wide open** | tension=slack damping=open | — *(literal anchors)* | drum_head |
| a drum that thuds like a heartbeat | tom, dull | — | **pulse › saw › square** (away) |
| a drum like knocking on a cardboard box | muffled tom | — | **pulse** (away) |
| a sharp cracking snare-like crack near the edge | spot=rim tension=tight | — | **pulse › silence** (away) |

Params set: **2/15** — one correct (timpani: pitched=timpani + tension=tight), one wrong axis
(asked strike, set ting). `spot` was set **never**, not even on a literal "dead in the centre".

## What this means — the bottleneck is one level EARLIER than the gloss wording

1. **The 7B almost never enters the parameter syntax (13/15 empty).** The descriptive words
   land instead as an **adjective** ("thin, edgy", "mellow", "warm woody") or a **prepended
   oscillator** (metallic_fm, glass). Even literal anchor words in the prompt — "slack",
   "wide open", "dead in the centre" — do not become params. This is the primary gap; it sits
   BEFORE the question of how each anchor is glossed.

2. **The parametrised key does not win its own natural words.** "rhodes" / "electric piano"
   route to the **non-parametrised** rhodes/wurlitzer keys (by design, `csound_orch.py`
   ~186–265, the fm_ep→rhodes handover); "reed piano" → clarinet; "drum … heartbeat / box /
   snare" → pulse. A user who types "a soft mellow rhodes" cannot reach fm_ep's ting/ring/
   reed/strike at all. Consequence for the self-correction thread: if first-authoring routes
   to a param-less key, the planned parameter-move correction has **nothing to move**.

3. **When the syntax IS reached, the mechanism is sound.** The one clean hit (timpani) shows
   audible-named axes map correctly. The one wrong-axis hit (strike→ting) and both `spot`
   zeros confirm the secondary, known defect: `spot`'s model-visible gloss names only the
   physical CAUSE ("centre / rim") while its audible glosses ("deep round full" / "thin hard
   edgy") already exist in `dco_lexicon.json` but are withheld by `_params_line`. Real, but
   fixable, and only Problem #3.

## Recommended order (data-driven)

1. Get the model to actually USE the param syntax — params as the PRIMARY channel of a
   parametrised key, not an optional afterthought that adjectives + prepended keys absorb.
2. Make parametrised keys win their natural words (route rhodes/electric piano to fm_ep, or
   parametrise rhodes/wurlitzer themselves) — else the self-correction runs over param-less keys.
3. Then surface the audible glosses (`spot` first).

This is the same axis as the open self-correction work (`docs/plans/HANDOVER_lco_selfcorrect.md`,
Thread 1): a mediocre first map is only tolerable if the parameter-move correction reliably
climbs from it — which presupposes (1) and (2).

## One open mechanism question

Whether the 13 empty cases are **non-emission** (model never wrote `key(name=anchor)`) or
**parser-drop** is not yet pinned: `stderr` does not log the raw reply. The `reading` pattern
points to non-emission (the 2 hits render cleanly), but a single raw-reply log line would
settle it — a ~2-minute instrumented rerun of the probe.
