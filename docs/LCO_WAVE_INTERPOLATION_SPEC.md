# LCO: Interpolierte Wellen — Spezifikation (2026-07-14, Entwurf zur Freigabe)

## 1. Auftrag

BJ, wörtlich (2026-07-13): *„Es muss keinen ‚bewegten Pfad' geben […] Es muss 2–4, vielleicht 5 zu interpolierende Wellen geben"* — *„und die müssen KORREKT gebaut werden im Anschluss an eine LLM-basierte Übersetzung von Prompt zu Tools."*

Bewegung ist also **Interpolation zwischen 2–4 (max. 5) korrekt gebauten Wellen** (Stationen). Kein Trajektorien-Paradigma, kein neuer Synthesepfad. Kette: Prompt → LLM-Übersetzung zu Tools → Tools bauen die Wellen exakt → Engine interpoliert. Darüber: **movement by default** — jede Erzeugung hat ≥2 Stationen, außer „static" ist angeordnet; ungerichtete Bewegung fällt auf „Bewegung ins Gegenteil" (dunkel↔hell) zurück.

## 2. Ist-Zustand (exakte Anker)

Das Rezept-Format kann das alles bereits ausdrücken — `keyframes[]` (bis 8, `DcoRecipeJson.h:40`), `motion[]`, `motion_rate_hz`. **Der Wire-Contract ändert sich nicht.** Die Lücke liegt in Routing und Engine:

- **Harmonische Ketten interpolieren heute schon**: `dco::Baker` backt die Keyframes zu Frames, `readMipSample` (`WavetableOscillator.cpp:1041`) blendet per Catmull-Rom über die Scan-Position. Die 2-Keyframe-Vorlagen (E-Piano, PWM, Brass, Streicher) sind genau das Prinzip.
- **Inharmonische Wellen können es nicht**: `setAdditiveBank` (`WavetableOscillator.cpp:800`) nimmt genau **einen** Partialsatz; `synthAdditiveSample` (`:1108`) summiert feste `a·sin(phase)` und ignoriert die Scan-Position — obwohl `scanNow` im selben `processSample` (`:966–972`) bereits berechnet vorliegt.
- **Routing** (`PromptPanel.cpp:2131–2152`): nur *single-keyframe* inharmonisch → `loadDcoAdditive`; der Kommentar benennt die Lücke selbst („a multi-keyframe inharmonic morph needs animated additive, not yet built").
- **Glue** (`PluginProcessor.cpp:5146`): `loadDcoAdditive` ignoriert `motionRateHz` und schaltet `setDcoMotion(false)` — die Bank steht still.
- **Backend**: `_apply_motion_intent` (`dco_recipe.py:995`, uncommitted) synthetisiert bei fehlendem zweitem Keyframe einen gekippten Gegenteil-Endpunkt (`_spectral_tilt`, `:976`) — verweigert aber bei nicht-ganzzahligem h. `_apply_analog_life` (`lco_author.py:116`) bricht bei Inharmonik ab (`:170–174`). Beides Schutz vor dem Ganzzahl-Raster des Bake-Pfads — richtig für den Bake-Pfad, hinfällig für den neuen Sets-Pfad.

Folge: Genau die Zielklänge (Glocke/Metall/Glas/Becken/Spieluhr) stehen still.

## 3. Engine: Stationen in der additiven Bank

**Ein Mechanismus, minimal:** `MipData` (`WavetableOscillator.h:194`) hält statt eines Partialsatzes **K ausgerichtete Partialsätze** (Nutzer-Ebene: 1–5 Key-Waves; intern nach Charakter-Pässen bis ~64 Sätze — Sub-Stationen, §6.4; Speicher trivial, Voice-State unverändert. K=1 ≡ heutiges Verhalten):

```
struct MipData {
    …
    bool isAdditive;
    std::vector<std::vector<AdditivePartial>> partialSets;  // K Stationen, ALLE gleich lang (ausgerichtet)
    float additiveGain;   // 0.95 / max_über_Stationen(Σ|a_i|)
};
```

- **Ausrichtung ist Backend-Pflicht** (§5): alle Sätze haben identische Länge N ≤ 64 (`MAX_ADDITIVE_PARTIALS`), Index i bezeichnet in jedem Satz denselben Partial; `phase` ist eine Eigenschaft des Index (in allen Sätzen identisch). Die Engine validiert das in `setAdditiveBank(sets)` (gleiche Sanitize-Regeln wie heute `:813–836`; ungleiche Längen → auf kürzeste kappen, defensiv).
- **Blend**: `synthAdditiveSample` und `advanceAdditivePhases` erhalten `scanNow` (liegt am Aufrufort `:988/:1024` bereits vor). `pos = scanNow·(K−1)`, Segment `s=floor(pos)`, `t=pos−s`, pro Index: `a_i = lerp(a_i[s], a_i[s+1], t)` und `h_i = lerp(h_i[s], h_i[s+1], t)`. Phasen-Akkus bleiben pro Index (`activeAddPhase_` unverändert dimensioniert); `advanceAdditivePhases` nutzt das interpolierte `h_i` — der Wrap mod 2π (`:1140`) bleibt für jedes h klickfrei. Linear reicht (die Glätte kommt aus der stetigen Scan-Bewegung); K=1 degeneriert exakt zum heutigen Code.
- **Gain**: `0.95 / max_über_Stationen(Σ|a_i|)` statt pro Satz. Kein Clipping an keiner Scan-Position (|lerp| ≤ max der Summen), und die Lautheit zwischen dunkler und heller Station **atmet natürlich** — ausdrücklich KEINE Pro-Position-Renormierung (das war der Lautheits-Inversions-Defekt des Bake-Pfads, vom Ohr verworfen).
- **Nyquist**: unverändert pro Sample gedroppt (`:1121`). Im Standard-Modus (§4, h konstant) ist das Gate pro Note konstant — kein neues Klick-Risiko. Nur falls Gleit-Modus später Default-fähig wird: weiches Gate (a→0 über den letzten Halbton unter Nyquist) nachrüsten.
- **Held-note follow**: unangetastet — der active/target-Morph (`:995–1027`) crossfadet weiterhin zwei Banks, jede evaluiert an derselben `scanNow`; Phasen-Übernahme bei Morph-Ende (`:1016`) bleibt.

**Glue** (`PluginProcessor.cpp:5146`): `loadDcoAdditive(sets, motionRateHz)` — bei K≥2: `setDcoMotion(true, motionRateHz>0 ? motionRateHz : 0.25f)` (identische Logik wie `loadDcoWavetable :5122`); bei K=1 wie heute `setDcoMotion(false)`. Der manuelle Scan-Regler und die Envelope-Scan-Routung (EnvTarget::Scan, SynthVoice) wirken damit automatisch auch auf inharmonische Klänge — dieselbe Bewegung wie überall, kein Sonderweg. Display: statt eines Ein-Perioden-Bilds (`:5207`) K Ein-Perioden-Bilder als Frame-Strip — der 2.5D-Fächer im Engine-Fenster zeigt die Stationen ehrlich.

**Routing** (`PromptPanel.cpp:2138`): neue Entscheidung — enthält **irgendein** Keyframe der Kette einen nicht-ganzzahligen Partial (Test wie heute, 1e-3), geht die **gesamte Kette** als Stationen-Sets an `loadDcoAdditive`; das Backend garantiert dann, dass jede Station als additiver Partialsatz vorliegt (§5). Rein harmonische/klassische Ketten bleiben unverändert auf dem Bake-Pfad (volle Bandbreite, bit-exakt).

## 4. Die Ausrichtungs-Entscheidung (einzige echte Design-Wahl)

Wie werden die Partials zweier Stationen einander zugeordnet?

- **A — Vereinigungs-Blende (vorgeschlagener Default)**: Vereinigungsmenge aller h über die Stationen; fehlt ein Partial in einer Station, steht dort a=0. h ist pro Index konstant → reine **Spektralblende**, nichts gleitet. Vermeidet den vom Ohr verworfenen „Sirenen"-Effekt.
- **B — Paarweises Gleiten**: Partials werden gepaart, h interpoliert → Teiltöne **gleiten in der Frequenz** (Glocke „verstimmt sich" zur Zielwelle).

Die Engine kann nur B — A ist der Spezialfall von B mit konstantem h; **die Wahl liegt allein im Backend-Ausrichtungs-Tool**. Entscheid per **A/B-Render am Ohr** (gleiche Glocke→Ziel-Kette, beide Modi) am Ende von Bau-Slice 1, bevor Slice 2 den Default festschreibt.

## 5. Tools als exakte Wellen-Bauer (Backend)

Jede Station wird von einem Tool **exakt** berechnet; das LLM wählt nur Tools/Keys, nie Zahlen (Guardrail unverändert):

1. **Additive Sätze** — existieren (Technik-Templates, `_apply_inharm`-Streckung, Adjektiv-Deltas).
2. **Klassik→Partialsatz-Konverter** — nur für Ketten mit inharmonischem Anteil (sonst Bake-Pfad): exakte Fourier-Reihen (saw 1/n; square ungerade 1/n; triangle ungerade 1/n², alternierend; pulse(w) 2/(nπ)·sin(nπw); cheby exakt polynomial; ring als Summen-/Differenzpaare). Budget: Vereinigungsmenge ≤ 64 → schwächste Partials fallen zuerst, mit ehrlichem Flag („reduced to 64 partials for the moving inharmonic chain").
3. **FM-Spektrum-Tool (neu)** — FM ist ein **Tool, kein Oszillator**: 2-Op-FM (Carrier f0, Ratio r **float**, Index I) hat exakt berechenbare Komponenten bei f0·(1+k·r), k∈ℤ, Amplitude J_k(I) (Bessel; negative Frequenzen gespiegelt, Vorzeichen in a). Abbruch: |J_k(I)| < 2e-3 bzw. Carson-Band. Nicht-ganzzahlige r → inharmonischer Satz. **Index-Bewegung = Stationen**: z. B. Glocken-Clang I=6 → beruhigter Ton I=1.5, zwei exakt gebaute Wellen, die Interpolation macht die Bewegung. Der Wire-Keyframe `fm2` (ganzzahlige Ratio, Bake-Pfad) bleibt unangetastet — das Tool emittiert additive Keyframes.

## 6. Mehr-Pass-Verfahren pro Key-Table (bzw. Interpolation)

BJ (2026-07-14): *„wie bereits mindestens 2 mal betont wird es ein mehr-Pass-verfahren pro key-table geben […] bzw. für die Interpolationen, je nach dem."* Präzisiert durch seine Probe-Frage: Wie kommen **dirty, old, analog, washed-out, overdrive** in die Tables und die Interpolationen? — Ein **Konstruktions**-Verfahren in Pässen (kein Prüf-Loop; Verifikation bleibt §9). Charakter-/Textur-Adjektive sind keine Spektral-Deltas auf einer Station: sie leben in der **Mikro-Variation über die Frames** und in **Nichtlinearität** — die festgehaltene Frame-Regel wörtlich: *„dirty saw = Fluktuationen des Saw ÜBER die Frames; nie identische Frames außer saw/tri/square."*

Pässe pro Bake (Reihenfolge deterministisch):

1. **Stationen-Pass** — Tools bauen die 2–4 (max. 5) Key-Waves exakt (§5): die spektrale Identität.
2. **Interpolations-Pass** — das Render-Medium entsteht: Bake-Pfad = 256 Frames (Ketten proportional aufgeteilt); Sets-Pfad = kontinuierliche Stationen-Blende (§3). Die Engine blendet bewusst **linear und gleichverteilt**; die `motion[]`-Kurven (Segmente, Dauern, Kurvenformen) setzt dieser Pass um, indem er die Trajektorie über die Key-Waves in Sub-Stationen **abtastet** (wie der Bake-Pfad in 256 Frames bakt, nur ins Sets-Medium). Ebenso gilt die Inhalts-Schließung des Bake-Pfads weiter: eine loopende Stationen-Kette endet auf ihrer Start-Welle (sonst wird der Transport-Wrap 1→0 ein harter Spektralsprung).
3. **Charakter-Pässe** (0…n, ein Pass pro Textur-Adjektiv) transformieren das Medium:
   - *overdriven/distorted* — Waveshaping pro Frame (echte neue Harmonische; auf inharmonischen Sets Intermodulations-Partials bis Budget, mit ehrlichem Flag),
   - *dirty/gritty* — kleine deterministisch-gestreute Jitter der Partial-Amplituden/Phasen pro Frame; jeder Frame minimal anders,
   - *analog* — langsame kohärente Drift über die ganze Table (Mikro-Detune + Amplituden-Wobble, Goldener-Schnitt-Phasenversatz, nichts wiederholt sich exakt),
   - *old* — HF-Erosion + leichte Schmierung + Wow (langsames Pitch-Wobble über die Frames),
   - *washed-out* — spektrale Glättung/Unschärfe: Partial-Kanten und Übergänge verlieren Definition.
   Wirkung **pro Key-Table** (jede Station trägt den Charakter) und/oder **auf die Interpolationen** (die Fluktuation lebt zwischen den Stationen) — je Adjektiv festgelegt, BJs „je nach dem".
4. **Sets-Pfad-Medium**: statt 256 Frames fügen Charakter-Pässe **Sub-Stationen** ein — perturbierte Zwischen-Sätze zwischen den Key-Waves. Die Nutzer-Ebene bleibt 2–4 (5) Key-Waves; intern hebt die Engine den Satz-Cap an (§3), Sub-Stationen sind Render-Medium wie Frames.

Keimzelle existiert: `_apply_analog_life`/`_drift_frames` (`lco_author.py`) ist genau ein solcher Pass (Per-Frame-Perturbation) — wird vom Sonderfall zum allgemeinen Pass-Mechanismus generalisiert, mit Adjektiv→Pass-Zuordnung im Lexikon (die Textur-Adjektive wandern von Delta-Ops auf Pass-Definitionen um; Spektral-Adjektive wie bright/dark/hollow bleiben Delta-Ops auf den Stationen).

## 7. Movement by default (Author)

Paradigmen-Schaltpunkt in `_compose` (`dco_recipe.py`):

- Motion „static" aufgelöst → 1 Station, tote Motion — das **einzige** delegierte Nicht-Bewegen.
- Sonst, wenn nach Kompose nur 1 Station existiert und keine Motion benannt ist → **Default-Motion injizieren**: Gegenteil-Endpunkt synthetisieren (Spektral-Tilt dunkel↔hell) + sanfte Hin-und-zurück-Motion. Dafür wird die uncommitted Endpunkt-Synthese (`_apply_motion_intent`/`_spectral_tilt`) erweitert: **Inharmonik-Verweigerung streichen** (der Tilt rankt nach h und funktioniert auf jedem Partialsatz; der Sets-Pfad trägt ihn jetzt), Ein-Partial-Floor bleibt (ein Sinus hat kein Gegenteil-Spektrum — dort greift weiterhin analog-life/Drift auf dem Bake-Pfad).
- Benannte Motion-Intents (open_up, settle, breathe …) erzeugen den Endpunkt wie in der uncommitted Fassung — jetzt auch inharmonisch.
- `_apply_analog_life`-Inharmonik-Abbruch (`lco_author.py:170`) **bleibt** (er schützt den Bake-Drift-Fächer; inharmonisch läuft nie dort). Amplituden-Default „leise, aber nicht zu leise" bleibt Author-Sache (Stationen-Dynamik über a-Summen), nicht Engine-Gain.

Die LLM-Übersetzung Prompt→Tools (7B-Kandidat, `dco_llm_map.py`-PoC) ist von dieser Spec **unberührt** und profitiert nur: gewählte Motion-Keys werden auf inharmonischen Timbres endlich hörbar. Ihre Beförderung zum Primär-Mapper bleibt der separate, bereits notierte nächste Schritt.

## 8. Was sich nicht ändert

Wire-Contract (`{ok, recipe, resolved, flags, …}`, Keyframe-Schema, Enum-Strings); Guardrail (LLM wählt Keys); Bake-Pfad für rein harmonische Rezepte (bit-exakt, volle Bandbreite); Motion-Transport-Semantik; Held-note-Invarianten; BJs uncommitted Dateien (`pipe_inference.py`, AxesPanel/MainPanel/PresetManagerPanel) werden nicht angefasst; Arbeit auf `main`.

## 9. Verifikation (Ohr ist Instanz)

1. **Parity**: numpy-Referenz der Stationen-Interpolation vs. Render durch den echten `WavetableOscillator` (Audition-Tool-Pfad wie `audition_dco_bake.cpp`) — Spektren identisch.
2. **Ohr-Gate 1** (Ende Slice 1): Glocke mit 2 Stationen durch den echten Pfad, WAVs nach ~/Downloads, BJ hört. Dazu das **A/B** aus §4 (Blende vs. Gleiten). Keine Hörbarkeits-Behauptungen meinerseits.
3. **Regression**: bestehende Harnesse (Inharmonik-Plausibilität 10/10, harmonische Non-Regression 9/9, held-note-Guards) müssen grün bleiben; neu: Phasen-Stetigkeit über Segment-Grenzen, K=1-Degeneration bit-identisch.
4. **Adversarial Review** jedes Slices (etabliertes Verfahren: Spec von mir, Bau durch Sonnet-Agents, unabhängige Gegner-Agents).

## 10. Bau-Reihenfolge

- **Slice 1 — Engine + Glue + Routing** (C++): §3 komplett, Audition-Renders, Ohr-Gate 1 + A/B-Entscheid.
- **Slice 2 — Backend-Tools + Author + Charakter-Pässe** (Python): §5–§7, Flags, Unit-Suite, Testbench.
- **Slice 3 — Abnahme**: End-zu-End (Prompt → 7B-Map → Stationen-Pass → Charakter-Pässe → Engine) auf den bekannten Problem-Prompts (Kathedralglocke, glassy/brittle, crystalline pad, hohl/nasal, dirty/analog-Fälle); Ohr-Urteil BJ.

Jeder Slice wird einzeln committet (nur von mir autorisierte Dateien), kein Commit vor grüner Verifikation.
