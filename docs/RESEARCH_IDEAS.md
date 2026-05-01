# Research Ideas

Forward-looking research notes for T5ynth. These are not implementation promises and should be treated as design hypotheses until validated against the current Stable Audio Open pipeline, backend hooks, and audible output.

## Fine-Grained Prompt / Embedding Mix Strategies

Current T5ynth prompt mixing is primarily global: prompt A and prompt B are encoded into T5 embeddings, then the system navigates between or around those vectors before generation. A future "Latent Lab Synth" style workflow could make this more precise by treating prompt B as a targeted injection into prompt A, rather than as a full-prompt crossfade.

Conceptually:

```text
Prompt A = base identity / body / main sonic material
Prompt B = mutation / gesture / texture / perturbation
Result   = A with B injected only at selected semantic or denoising hot spots
```

Possible strategies:

- **Layer-selective injection**: Inject prompt B only into selected conditioning or transformer layers, if the backend exposes stable hooks. Earlier layers may influence global structure more strongly; later layers may be better candidates for surface detail, timbre, and texture. This must be verified empirically for Stable Audio Open, not assumed.
- **Token- or phrase-targeted injection**: Apply B only to selected semantic regions of prompt A, for example mutating "metallic overtones" while leaving "warm analog bass drone" mostly intact. This would require token-level or phrase-level conditioning control rather than a single pooled embedding vector.
- **Attention-gated injection**: Use attention activity to find hot spots where prompt A is already strongly active, then inject B only there. This keeps B from globally overpowering A and could create more legible hybrid sounds.
- **Temporal injection during denoising**: Even though diffusion denoising updates the full latent at each step, conditioning does not have to be constant across the whole sampling schedule. Prompt B could be introduced only during selected denoising steps, ramped in or out over the sampler trajectory, or alternated between step ranges. This is "temporal" in sampling-time, not necessarily direct audio-time editing. Audible time-local effects would need validation because denoising steps are not equivalent to seconds in the decoded waveform.
- **Directional delta injection**: Inject the direction from neutral to B instead of the full B embedding:

  ```text
  delta = embedding(B) - embedding(neutral)
  result = embedding(A) + strength * delta
  ```

  This may preserve A more reliably while still adding B-like character.
- **Contrastive injection**: Add desired B traits while subtracting an unwanted direction, for example adding "glassy texture" while suppressing "rhythmic fragmentation". This could use positive and negative prompt directions if the model responds coherently.
- **Stochastic micro-injection**: Randomize small B injections across selected layers, steps, tokens, or strengths. This is highly experimental but promising for variant generation: it could produce families of related sounds where A remains recognizable but B appears as unstable local mutations.

### Note On Frequency-Band Mixing

Frequency-band injection is probably not realistic at the text-embedding level alone. A T5 embedding does not expose direct frequency bins or spectral regions. Frequency-aware control would need an additional audio-latent, spectrogram, decoder, or post-generation analysis/control path. It should not be presented as a simple embedding-mix feature.

### Possible UI Shape

A future research UI should avoid a single global `A <-> B` slider as the only control. A stronger interface would expose a compact matrix:

```text
               early layers   mid layers   late layers   denoise steps   randomize
Prompt B          0.05          0.25          0.60          ramp 20-60%      0.15
```

Musically, the user-facing model could be:

```text
Base Prompt       = what remains recognizable
Mutation Prompt   = what enters the base
Injection Target  = where it enters
Injection Strength= how strongly it enters
Injection Schedule= when it enters during sampling
Injection Mode    = add / replace / delta / gated / stochastic
```

The research question is whether these controls create audible, repeatable differences that are more interesting than ordinary prompt interpolation, without turning the UI into an opaque technical lab panel.
