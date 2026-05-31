# SA3 Ungated Install — Implementation Plan

Status: **in implementation** (updated 2026-06-01). Goal: login-free installation
of Stable Audio 3 (Small Music + Small SFX) — no HuggingFace account, no token, no
Gemma manual-approval — by sourcing the weights from the **ungated Comfy-Org repo**
instead of the gated `stabilityai/*` repo.

## Update 2026-06-01 — self-contained, NO mirror (supersedes the mirror plan below)

The metadata-mirror approach in the older sections below is **superseded**. Decision:
Comfy-Org is the *only* download source and **nothing** goes on a T5ynt-owned repo. The
small metadata Comfy-Org omits is supplied by T5ynth itself.

- **Weights → Comfy-Org** (unchanged): `checkpoints/stable_audio_3_small_{music,sfx}.safetensors`
  → `model.safetensors`; `text_encoders/t5gemma_b_b_ul2.safetensors` → `t5gemma-b-b-ul2/model.safetensors`.
  Fetched by the Stage-1 reassembly engine (`downloadReassemblyInThread`, commit `ea6ec736`).
  Comfy-Org sizes: music/sfx `2270384940`; t5gemma `1187264003`.
- **~60 KB configs → bundled in the app** (BinaryData), written into the model dir at install:
  `model_config.json` (shared by music+sfx — the backend derives `TrackType` from the dir NAME,
  not the config: `_native_modality_prefix`, `pipe_inference.py:1017-1021`) plus the four t5gemma
  configs (`config.json`, `generation_config.json`, `tokenizer_config.json`, `special_tokens_map.json`).
- **Tokenizer → extracted from Comfy-Org's own t5gemma weights.** The SentencePiece model ships
  INSIDE `t5gemma_b_b_ul2.safetensors` as a `spiece_model` tensor (dtype U8, 4 241 003 bytes,
  data_offsets `[1182981120, 1187222123]`, abs file bytes `[1183023000, 1187264003)` = ends at EOF).
  The installer extracts those bytes → `t5gemma-b-b-ul2/tokenizer.model`. No 34 MB `tokenizer.json`.
- **Backend dep:** add `protobuf<3.21` (3.20.3) to `backend/requirements.txt`. The conditioner
  calls `AutoTokenizer.from_pretrained` with default `use_fast=True` (`conditioners.py:587`); with
  only `tokenizer.model` present it converts slow→fast via `GemmaConverter`, which needs protobuf.
  protobuf ≥4 breaks `sentencepiece` 0.1.99's bundled `*_pb2` → pin `<3.21`.
- **Licensing (120% mandate):** the only Gemma/Stability *weight/tokenizer* bytes on disk come from
  what the user downloads from Comfy-Org under the in-app-accepted license (the tokenizer is carved
  out of those weights — never bundled or hosted by us). Bundled configs are tiny JSON. Ship the
  Gemma Terms + Prohibited Use Policy + SA Community License into the install dir + an app `NOTICE`;
  in-app license acceptance stays mandatory and is extended to cover Gemma.

### Verified 2026-06-01
- Comfy-Org tree = weights only (no config/tokenizer files): HF tree API.
- `spiece_model` tensor present + extractable: safetensors header peek (4 241 003 B, ends at EOF).
- Extracted `tokenizer.model` (no `tokenizer.json`) → `AutoTokenizer.from_pretrained` default
  (`use_fast=True`, protobuf 3.20.3) → `GemmaTokenizerFast` with **token IDs byte-identical** to the
  shipped `tokenizer.json` on every test prompt incl. `TrackType: SFX/Music` and empty string.
- Backend t5gemma path dir-validates only `model.safetensors` (`pipe_inference.py:461`); tokenizer
  files are not dir-validated → **no backend change** beyond the protobuf dep.

### Still to verify (BLOCKING, CLAUDE.md §7)
- The Comfy-Org **checkpoint** (DiT weights) generates correct audio through the real pipe
  (music + sfx). Same byte size as Stability's (`2270384940`) but repacked (different oid) — run a
  real generation, do not trust the size match.

### Stage plan
- **Stage 1 (done, `ea6ec736`):** dormant `ReassemblyAsset` engine.
- **Stage 2 (next):** `protobuf<3.21` in requirements; bundle the 5 configs (BinaryData); C++
  spiece_model extractor + config writer; SA3 catalog activation (2-weight asset arrays,
  `downloadable=true`, Comfy-Org `hfRepo`, Gemma+SA license notice); UI → download style; drop SA3
  from the Auto-Scan manual list; license docs into the install dir. Land after the §7 generation
  test so main never activates an unverified weights source.

---

_Historical (superseded mirror approach) below._

## Why this is possible (verified, not speculative)

| Fact | Evidence |
| --- | --- |
| `Comfy-Org/stable-audio-3` is ungated | HF API `gated=False`; anonymous file fetch → `302 → CDN → 200 OK`, `X-Xet-Cas-Uid=public` |
| It carries all SA3 variants | `checkpoints/stable_audio_3_{small_music,small_sfx,medium}{,_base}.safetensors` + shared `text_encoders/t5gemma_b_b_ul2.safetensors` |
| The original is gated, even the small JSONs | `stabilityai/stable-audio-3-small-music` `gated=auto`; anonymous fetch of *every* file incl. `config.json` / `tokenizer.model` → `401 GatedRepo` |
| t5gemma is itself gated | `google/t5gemma-b-b-ul2` `gated=manual` (Gemma license, owner approval) |
| Comfy-Org's t5gemma loads cleanly into our stack | Real `T5GemmaEncoderModel.from_pretrained` (encoder-only, `is_encoder_decoder=False`): **0 missing / 0 mismatched** keys; forward → `(1,4,768)` finite. `model.` prefix-strip + decoder/`spiece_model` drop are automatic (`base_model_prefix="model"`). **No key-rewrite needed.** |
| Comfy-Org omits the metadata | The repo ships **only** weights: no `model_config.json`, no t5gemma `config.json` / `tokenizer.*` / `generation_config.json` |

A naive raw `state_dict` key-diff falsely reports "rewrite needed" — it does not model
`from_pretrained`'s prefix mapping. Trust the real load.

## Architecture decision

- **Heavy weights → Comfy-Org** (trusted org, ungated). NOT the `LeeAeron/t5gemma-b-b-ul2`
  community reupload (ungated but mis-declares license as MIT — it is Gemma — fragile, takedown-prone).
- **Small metadata → a T5ynt-owned mirror** (GitHub release asset), exactly the pattern
  `t5-base` already uses via `kT5BaseGhFiles` (`SetupWizard.cpp:120`). Not vendored into the
  binary (the Gemma tokenizer is ~38 MB).
- **License compliance via in-folder docs.** Redistribution terms (Gemma: ship the Gemma
  Terms + Prohibited Use Policy; SA Community: ship license copy + attribution) are satisfied
  by writing the license files **into the installed model dir** — the same `LICENSE.md` /
  `LICENSE_GEMMA.md` / `NOTICE` the Stability repo already carries. In-app license
  *acceptance* (confirmation dialog) stays mandatory.
- **Mechanism switch:** SA3 moves from the gated manual path (`downloadable=false` → browser
  flow) to the ungated auto-download path (`downloadable=true`) that AudioLDM2 / t5-base use.

## Source layout → target layout

Comfy-Org repo paths must be remapped onto the `stable_audio_tools` dir the backend expects
(`pipe_inference.py:266` reads `model_config.json`; `:455-471` expects `t5gemma-b-b-ul2/model.safetensors`):

```
# from Comfy-Org/stable-audio-3 (ungated weights)
checkpoints/stable_audio_3_small_music.safetensors  →  <dir>/model.safetensors
text_encoders/t5gemma_b_b_ul2.safetensors           →  <dir>/t5gemma-b-b-ul2/model.safetensors

# from T5ynt metadata mirror (GitHub release)
model_config.json                                   →  <dir>/model_config.json
t5gemma-b-b-ul2/{config.json, generation_config.json,
  tokenizer.json, tokenizer.model, tokenizer_config.json,
  special_tokens_map.json}                          →  <dir>/t5gemma-b-b-ul2/...
LICENSE.md, LICENSE_GEMMA.md, NOTICE                →  <dir>/...   (license docs)
```

## SA3 music vs sfx (verified by LFS sha256)

The two SA3 variants share **byte-identical weights** on the stabilityai originals:
`model.safetensors` AND the t5gemma encoder have the SAME LFS sha256 across music and sfx.
The only difference is `model_config.json` (the `TrackType: SFX` prompt prefix + training-only
flags). Comfy-Org re-packs them as two separate files with different sha256 (pure repacking —
also differs from the stabilityai sha; the `ed9cf1b6...` that read as "music" elsewhere is
Comfy-Org's *sfx* hash). So per-variant we fetch the matching Comfy-Org checkpoint + the
matching `model_config.json`; the t5gemma encoder is shared.

The size-based duplicate-guard (`installFromManifestFolder:874-888`) therefore mis-fires for
music vs sfx (identical 2.27 GB size). **Being fixed in a parallel session — out of scope here.**

## Steps

1. **Catalog** — `kKnownModels` (`SetupWizard.cpp:187` / `:201`). Both SA3 entries:
   `hfRepo → "Comfy-Org/stable-audio-3"`, `downloadable → true`. Add a `KnownModel` field for
   the **in-repo checkpoint path** (since one repo now holds several checkpoints; `small_music`
   vs `small_sfx`). Keep `encoderSubfolder = "t5gemma-b-b-ul2"`.

2. **Path mapping** — the download writes to `targetDir.getChildFile(df.remotePath)`
   (`downloadAllFilesInThread`, `SetupWizard.cpp:1489`). Introduce a remap
   `remotePath → localPath` so the Comfy-Org subfolder layout lands at the target paths above.
   `startDownload` (`:1121`) builds `filesToDownload` from the tree API — restrict it to the
   one checkpoint + the t5gemma weight for this model id.

3. **Metadata mirror** — new GitHub release tag (e.g. `assets/sa3-metadata-v1`) carrying the
   metadata + license files listed above; a second asset source merged with the Comfy-Org
   weights (analogous to `downloadGhReleaseInThread` / `GhAsset`). **Blocked on the license
   read below before upload.**

4. **License docs in folder** — `isRepoDocFile` (`SetupWizard.cpp:226`) currently filters
   `license*` / `notice` / `.md` as "never needed". The three license files must flip from
   *ignore* to *ship*: included in the mirror (step 3) and written into the model dir by the
   reassembly.

5. **License notice** — SA3 `licenseNotice` must cover **SA Community AND Gemma** (the
   vanishing HF gate was also the Gemma consent). Extend the notice text + add the Gemma terms
   link to `licenseUrl`.

6. **Backend** — unchanged. After steps 2–4 the dir matches `pipe_inference.py`'s expectations.
   Verify the mirrored `model_config.json` is byte-identical (SHA) to the Stability original.

## Verification gate (BLOCKING per CLAUDE.md §7)

1. Fresh install into an empty app-support dir, **no token** → auto-download → real SA3
   generation through the actual pipe (not just encoder load). Covers the historic
   "nuschelnde Stimme" regression path.
2. Both variants (music + sfx) — they share encoder + metadata.
3. `model_config.json` SHA: mirror vs. Stability original.

## Open implementation details (non-blocking)

- Tokenizer: mirror the 34 MB `tokenizer.json`, or extract `spiece_model` from Comfy-Org's
  weight tensor → `tokenizer.model` and let `AutoTokenizer` build the slow tokenizer (saves
  34 MB, +1 verification that it loads without `tokenizer.json`).
- Pin Comfy-Org to a fixed revision (commit hash, not `main`) so a re-upload can't break the
  reassembly / sizes.

## Out of scope

- SAO 1.0 / SAO Small: their encoder is `t5-base` (Apache, already ungated + GitHub-mirrored).
  No change needed.
- SA3 `medium` variants: not currently in the catalog.
