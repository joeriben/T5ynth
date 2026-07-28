# Re-Prompt für den DCO — vom Selbst-Hören zum Selbst-Lesen

**Status (2026-07-24, verified against code).** Re-Prompt is live:
`PromptPanel::triggerDcoReprompt()` (`src/gui/PromptPanel.cpp:2713`)
implements this document's exact mechanism — one `interpret()` call under
the selected stance, reading the oscillator's own last bake instead of CLAP
("lesen → deuten → umformulieren", cited by name in the code comment) — with
all six stances unchanged (`RepromptStances.h`) and no auto-loop, matching
the document's own open items. The 2026-07-22 Nachtrag below (model swap to
the one gemma-4-12B) is current. One drift past that Nachtrag: since the
same-day switch to Csound authoring (`docs/plans/HANDOVER_LCO.md`), the
`resolved`/`flags` JSON this document treats as the complete self-description
no longer exists — only a natural-language `reading` string survives, and
`flags` is confirmed always-empty in the live code.

*Stand: 2026-07-10. Designnotiz, keine Implementierungszusage. Konzeptionelle Übertragung der Re-Prompt-Idee (Stance-gesteuerter semantischer Loop, `docs/SEMANTIC_LOOP_AESTHETICS.md`, implementiert in `src/inference/RepromptStances.*` + `RepromptStanceBar`) auf das DCO-Paradigma (Advanced-Panel).*

> **Nachtrag 2026-07-22 — das Modell, gegen das diese Notiz geschrieben ist, gibt es nicht mehr.** Der separat installierte Qwen2.5-1.5B-Übersetzer ist ausgebaut (`dd2e0373`, `9ece63b3`). Das Produkt hat genau **ein** Sprachmodell: `google/gemma-4-12B-it-qat-q4_0-gguf`, ein 4-Bit-GGUF über llama.cpp (`backend/pipe_inference.py:900-915`, Slot `gemma-4-12b-it-qat-q4_0`). Dasselbe Modell übersetzt, treibt Re-Prompt/`interpret` **und** bedient den Csound-Pfad (`translate`/`interpret` über `run_author_instruct` → `_resolve_coder_model_dir`, `:1205-1233`; `csound` löst das Modell selbst auf, `:3611-3628`). Das Argument der Notiz bleibt stehen; nur seine Prämisse „klein" nicht. Konkret betroffen: die Stellen unten, die aus der Schwäche des kleinen Modells argumentieren, sind einzeln markiert. Die Anti-Cycling-Logit-Transformationen, die der 1.5B im Re-Prompt-Pfad brauchte, sind ersatzlos entfernt — sie waren die Krücke des kleinen Modells, auf dem Autor-Modell nachgemessen und wirkungslos (`run_author_instruct` verwirft sie auf dem GGUF-Pfad, `pipe_inference.py:1216-1224`).
>
> **Und eine Folgerung dieser Notiz verliert ihre Grundlage.** Die Latenzangaben unten (~1–2 s) sind 1.5B-Werte und tragen nicht mehr. Der Stance-/Interpret-Schritt selbst (eine Umschreibung von 3–10 Wörtern) ist auf dem 12B **nicht** gemessen. Gemessen ist nur der ungleich schwerere Csound-Autoren-Aufruf — voller Katalog-Systemprompt (`_CS_SYSTEM_PROMPT_HEAD` + Katalog, `csound_orch.py:4157`), Antwort sind die acht Schlüssel-Zeilen, aus denen Python die Orchestra baut — mit **Median 12,2 s** (`294f49fb`, 10/11 Korpus-Prompts); das ist ein anderer Aufruf und keine Zahl für den Stance-Schritt. Was daraus folgt: „~1–2 s" ist weg. Was **nicht** daraus folgt: dass der Loop langsamer als der neurale sei — dafür fehlt sowohl die Messung des Stance-Schritts auf diesem Modell als auch überhaupt eine Zahl für den neuralen Loop (die Notiz sagt nur „Sekunden Latenz pro Glied"). Die Schlüsse „der Loop wäre *schneller* als der neurale" (Übersetzungstabelle) und „spielbare Geste statt Hintergrund-Evolution" (Abschnitt „Der Loop auf der gehaltenen Note") sind damit **offen**, nicht widerlegt. Die konzeptionelle Pointe (Determinismus, Fixpunkt, Flags als Material) hängt ohnehin nicht daran.
>
> **Ebenfalls überholt, unabhängig vom Modell:** die Op `dco` gibt es nicht mehr (gelöscht in `40600a0e`). Der unten skizzierte Zweischritt „`interpret` + `dco`" hat seine zweite Hälfte verloren; der lebende Pfad ist `mode:"csound"`.

> **Nachtrag 2026-07-28 — die Begründung für „CLAP entfällt ersatzlos" trägt nicht mehr, und der Loop soll hören.** Der Ohr-Verzicht unten steht auf einer Prämisse, die es nicht mehr gibt: `resolved`+`flags`+`recipe` eines **geschlossenen, vorgehörten** Lexikons. Ein Rezept zu lesen hieß, in einer Liste bereits gehörter Dinge nachzuschlagen — deshalb war Selbst-Lektüre ein gültiger Ersatz fürs Ohr. Der geschriebene Csound ist offen: niemand hat den Code vorher gehört, und die Opcode-Liste eines Bodys sagt über den Klang so wenig wie ein Prompt über den SA3-Render. Der LRO steht damit epistemisch **genau da, wo T5osc steht**. Faktisch verarbeitet `triggerDcoReprompt` heute kein Audio, sondern nur die `READING:`-Zeile, die das Autor-Modell selbst über seinen eigenen Code schreibt (`backend/lco_write.py`; fehlt sie, listet `_fallback_reading()` die gefundenen Opcodes), und `dcoLastFlagsLine_` ist konstant leer — der Ehrlichkeitskanal existiert nicht mehr. Ein Modell deutet also, was ein Modell über seinen eigenen Text behauptet.
>
> **Entscheidung BJ, 2026-07-28: der Re-Prompt soll hören; CLAP ist dafür das VORLÄUFIGE Ohr** („taggen, und wir machen das erstmal so"). Mit ausdrücklichem Vorbehalt: *„if any, clap wäre ungeeignet. diese Entscheidung ist alt und war ggf. eher pragmatisch als sachdienlich."* Der Sachgrund steht in `docs/SEMANTIC_LOOP_AESTHETICS.md`: **CLAP beschreibt nicht, CLAP rankt** — Zero-Shot gegen eine feste Kandidatenliste, kein freies Captioning, und es ist das AudioSet-/anglophone Ohr. Eine selbst erfundene Textur würde damit auf ein kuratiertes Tag-Register zurückgefaltet, also gegen das Ziel des LRO. Der dort benannte Plan B — ein **Audio-LLM** (Qwen2-Audio, SALMONN, LTU), das Ohr *und* Interpret zugleich ersetzt — ist der eigentliche Kandidat, wenn die Vorläufigkeit endet.
>
> **Was das NICHT wiederbelebt:** den Self-Check. Kein Comparer, kein Urteil, keine Korrekturschleife (21.07., „self check ist eine katastrophe", auskompiliert als `T5YNTH_LCO_SELFCHECK 0`). Gehört wird als **Material für die Haltung**, nicht als Prüfung.
>
> **Kein Argument gegen ein LRO-Ohr ist die Signallänge.** Der neurale Render geht ab 0,1 s (`makeDurationRange(0.1f, …)`, `src/PluginProcessor.cpp`) — CLAP läuft im geshippten Loop längst darauf; der LRO liefert einen mehrsekündigen bewegten Verlauf (`renderBareOscillator` bis 60 s, Movement by default).

## Worum es geht

Das neurale Easy-Panel besitzt einen semantischen Loop: Das Instrument **hört sich selbst an** (CLAP rankt den letzten Render gegen ein Timbre-Vokabular), ein Interpret (2026-07-10: ein kleiner, Qwen2.5-1.5B; seit 2026-07-22 das eine 12B-Modell, s. Nachtrag) **deutet das Gehörte unter einer Haltung** (Stance: transcribe / entkitscher / verniedlicher / variation / abduction / opposite) und **schreibt den Prompt um**; die Kopplung (`concat2`: menschliches Original + jüngste Interpretation) speist die nächste Generation. Die Frage dieser Notiz: Was ist die *treue* Kopie dieser Idee im DCO — nicht ihre mechanische Portierung?

## Der eine fundamentale Unterschied: Das Ohr wird überflüssig

Der neurale Loop braucht CLAP, weil die Deutung des Prompts durch das Modell **opak** ist: Was SA3 aus „warm analog bass" *gemacht hat*, weiß man erst nach dem Anhören. Die Schleife ist epistemisch: hören → deuten → umformulieren.

Der DCO hat dieses Problem nicht. Seine Deutung liegt **vollständig und maschinenlesbar** vor, bevor ein Sample gerendert ist:

- `resolved` — was der Router verstanden hat (technique, adjectives, motion, values),
- `flags` — was er *nicht* oder nur approximativ verstanden hat (der Ehrlichkeitskanal),
- `recipe` — die exakte, deterministische DSP-Konsequenz.

Es gibt nichts zu erhören, was nicht schon im Rezept steht. **CLAP entfällt ersatzlos; an die Stelle des Ohrs tritt die Selbst-Lektüre.** Der DCO-Loop ist: lesen → deuten → umformulieren.

Das ist kritisch-ästhetisch nicht weniger, sondern pointierter: Der neurale Loop beobachtet eine fremde Black Box zweiter Ordnung (CLAP hört SA3, beide tragen fremde Taxonomien). Der DCO-Loop verhandelt die **Grenze des eigenen, selbst kuratierten Vokabulars** — die Glass Box wird als situierte Auswahl befragbar. Für ein pädagogisches Instrument ist das der seltenere Fall: Die Kritik trifft nicht „die KI da draußen", sondern die eigene Kurationsentscheidung (`dco_lexicon.json`).

## Übersetzungstabelle

| Neural (gebaut) | DCO (Konzept) |
|---|---|
| CLAP-Tags des letzten Renders („das Ohr") | `resolved` in Worten: „heard: pulse, width sweep, 0.35 Hz motion" — deterministisch, kein Ranking |
| DSP-Spektralwörter (transcribe-Input) | Rezept-Fakten: Keyframe-Kinds, motion_rate_hz, frames — die exakte „Messung" |
| *(kein Äquivalent)* | **`flags`** — „nicht verstanden: shimmering; approximiert: bell inharmonicity". Das DCO-Spezifikum; der neurale Loop kennt kein explizites Nichtverstehen. |
| prev-Prompt + Anti-Stasis-Memory (≤3 Glieder) | identisch übernehmbar |
| `cleanPrompt` (Label-Echos, Längenkappung) | identisch übernehmbar (2026-07-10 begründet mit den 1.5B-Schwächen; ob das 12B die Nachreinigung noch braucht, ist offen — s. Nachtrag) |
| Music-Suffix-Preservation (bpm/Pitch-Anker) | **entfällt** — der Bake konditioniert nicht auf Tempo/Tonhöhe (Pitch kommt von MIDI, Motion-Tempo aus dem Lexikon) |
| Kopplung `concat2` (Original + letzte Interpretation) | übernehmbar — *und* es existiert eine DCO-native Alternative: die **Morph-Chain-Kopplung** (s. u.) |
| Loop-Trigger: Drift-Regen-Zyklus | Schritt-Trigger neben BAKE; Auto-Loop optional (Bake ist ~ms, der Interpret-Schritt ~1–2 s — der Loop wäre *schneller* als der neurale) **⚠ offen: ~1–2 s war der 1.5B; auf dem 12B ist der Stance-Schritt ungemessen, s. Nachtrag** |
| A/B-Polarität (variation arbeitet auf Pol B) | **kein Gegenstück** — der DCO hat ein Promptfeld (bewusste Entscheidung). Die Spannung „A→B" lebt im DCO *innerhalb* des Rezepts: als Keyframe-Kette. |

## Die sechs Stances im DCO — gleiche Haltung, verschobene Wirkung

Die geshippten Stances sind Text→Text-Transformationen; sie laufen **unverändert** auf DCO-Prompts, weil jeder Modell-Output durch denselben Lexikon-Trichter muss (Guardrail-Invariante: der Router bleibt „router not author", keine Zahl entsteht außerhalb des Lexikons — der Loop kann die Validierung nicht umgehen, egal was das Modell schreibt). Diese Invariante hängt nicht an der Modellgröße, gilt also für das 12B unverändert (`docs/DCO_LLM_GUARDRAILS.md` §1a) — sie steht allerdings aus einem anderen Grund zur Disposition, s. dort. Aber ihre *Wirkung* verschiebt sich charakteristisch:

1. **transcribe** (affirmativer Pol): „Schreibe wörtlich, was gemessen wurde" heißt hier: Schreibe den Prompt als das, was `resolved` sagt. Konvergiert auf reines Lexikon-Vokabular; Flags → 0. Anders als neural ist der **Fixpunkt erreichbar** (Determinismus): der Loop *terminiert* sichtbar, wenn Prompt und Maschinendeutung deckungsgleich sind. Der Fixpunkt-Test des Routers als spielbare Geste.
2. **variation**: Wandert durch Lexikon-Nachbarschaften — kontrollierte Exploration des Rezeptraums. Funktioniert unverändert.
3. **opposite**: Trifft auf die bipolaren Adjektiv-Achsen des Lexikons (bright↔dark, thick↔thin, Motion schnell↔langsam) und landet in echten Gegen-Rezepten. Im DCO *besser* definiert als neural, weil die Achsen explizit existieren.
4. **abduction** (dekonstruktiver Pol): Springt zu Szenen („was könnte so klingen") — der Router muss die Szene zurück in Technik+Adjektive zwingen und produziert dabei **Flags**: Die Reduktionsleistung des Lexikons wird als Abfall sichtbar. Die Flags-Liste im Panel wird zum Protokoll dessen, was die Formalisierung wegwirft.
5. **entkitscher / verniedlicher**: Register-Transformationen; wirken auf die Adjektiv-Wahl. Laufen, aber die Pointe ist schwächer (DCO-Prompts sind selten kitschig — es gibt wenig zu entkitschen). Kandidaten fürs Weglassen im DCO-Kontext; Konsistenz der Bar über beide Panels spräche fürs Behalten. **Offene Kurationsfrage, kein technisches Problem.**

**Denkbarer siebter, DCO-nativer Stance — „Grenzgänger" (Arbeitstitel):** Nimm die Flags als Material — „mache das Nichtverstandene zum Zentrum des nächsten Prompts". Das ist die DCO-Fassung von Modus 3 (Kritik) aus `SEMANTIC_LOOP_AESTHETICS.md`, mit umgekehrter Stoßrichtung: Kritisiert wird nicht AudioSets Taxonomie, sondern die eigene. Der Loop pendelt dann an der Vokabulargrenze entlang, statt zu konvergieren. (Nur Konzept; ob das Modell diese Haltung hält, wäre wie beim entkitscher-Reframe empirisch zu prüfen — 2026-07-10 war das die Frage an ein 1.5B, heute an das 12B.)

## Kopplung: Text-Concat oder Morph-Chain

Die neurale Kopplung `concat2` („Original, jüngste Interpretation") ist direkt übernehmbar — der Scanner liest beides, Original-Technik bleibt präsent, Adjektive addieren sich.

Die *schönere*, DCO-native Kopplung existiert aber seit den Morph-Chains (89c6a7ee): **Das alte Rezept morpht wörtlich ins neue** — der Loop-Schritt komponiert `<alte Kern-Technik> into <neue Kern-Technik>` und macht den Interpretationsschritt selbst zur klingenden Bewegung *innerhalb einer einzigen Table*. Kopplung nicht als Text-Verkettung, sondern als Rezept-Komposition. Grenzen ehrlich benannt: nur Ein-Keyframe-Techniken sind kettbar, Cap 4 — die Kopplung fällt auf concat2 zurück, wo die Kette nicht komponiert (mehrteilige Rezepte, Flag vorhanden). Beide Kopplungen sind dieselbe Stelle im Datenfluss; das kann ein Toggle sein, konzeptionell sind es zwei Lesarten von „das Neue antwortet dem Alten".

## Der Loop auf der gehaltenen Note

Die Plattform-Invariante (gehaltene Note folgt der aktuellen Table per Regen-XFade-Crossfade; seit dem Motion-Transport-Fix auch über DCO↔neural-Flips klickfrei) macht den DCO-Loop **performativ**: Ein gehaltener Akkord wandert hörbar durch die Interpretationskette — jede Iteration ein Crossfade in die nächste Deutung, ohne Neuanschlag. Der neurale Loop hat das prinzipiell auch, aber mit Sekunden Latenz pro Glied; der DCO-Loop taktet in Interpret-Geschwindigkeit (~1–2 s) und wird damit zur *spielbaren* Geste statt zur Hintergrund-Evolution. **⚠ Prämisse weg, Schluss offen:** ~1–2 s war der 1.5B; auf dem heutigen 12B ist der Stance-Schritt nicht gemessen (s. Nachtrag). Ob diese Geste noch spielbar taktet, ist damit eine offene Messfrage, keine erledigte.

Jedes Kettenglied ist vollständig dokumentierbar — (Prompt, resolved, flags, recipe) sind kleine JSON-Objekte, verlustfrei und deterministisch reproduzierbar (der neurale Loop kann seine Audio-Glieder nur als WAV oder gar nicht erinnern). Das ist der natürliche Anschluss an Eventlog/Preset-Persistenz (bestehender Seam: DCO-Bakes werden noch nicht geloggt/persistiert — der Loop verschärft den Bedarf, liefert aber auch das Format gleich mit).

## Was bewusst NICHT kopiert wird

- **CLAP** — kein Ohr nötig; das Rezept ist die vollständige Selbstbeschreibung. (Wollte man später „wie klingt die Table wirklich im Voice-Kontext nach Filter/FX" befragen, wäre das ein *anderes*, neues Konzept — Selbst-Hören hinter der eigenen DSP-Kette — und bleibt hier ausdrücklich draußen.)
- **Spektral-DSP-Deskriptoren** — ersetzt durch Rezept-Fakten (exakt statt gemessen).
- **Music-Suffix-Preservation** — gegenstandslos ohne bpm/Pitch-Konditionierung.
- **A/B-Pol-Logik** — der DCO hat ein Feld; die variation-Stance verliert ihren „Pol B"-Rahmen und wird zur einfachen Selbst-Variation.

## Infrastruktur-Beobachtung (macht das Konzept billig)

Der DCO-Router *benutzt das Sprachmodell bereits* (S2-Residue-Routing im selben Backend-Prozess — **2026-07-22: der S2-Residue-Schritt läuft nicht mehr, der lebende Pfad schickt den GANZEN Prompt in einem Aufruf; `docs/DCO_LLM_GUARDRAILS.md` §1a. Was das Argument trägt, bleibt aber wahr: das Modell liegt im selben Prozess**). Es gibt kein neues Modell, kein neues Gating (`PromptPanel::setLlmAvailable`, gespeist aus `SettingsPage::isCoderModelInstalled` — `MainPanel.cpp:594-596` —, deckt beide Fälle; 2026-07-10 hieß das `setQwenAvailable`), keinen neuen IPC-Frame-Typ — der Stance-Schritt ist ein `interpret`-Aufruf (bestehende Op) gefolgt von einem `dco`-Aufruf (**2026-07-22: diese Op ist gelöscht, `40600a0e`; an ihre Stelle tritt `mode:"csound"`**). Die StanceBar-Komponente existiert und ist APVTS-gebunden. Konzeptionell ist der DCO-Re-Prompt damit fast reine *Verdrahtung* plus die Kurationsentscheidungen oben — das Teuerste ist das Nachdenken, und das ist dieses Dokument.

## Offene Punkte / Ehrlichkeitsfußnoten

- **Stance-Kuration:** entkitscher/verniedlicher behalten (Bar-Konsistenz) oder durch „Grenzgänger" ersetzen (DCO-Schärfe)? Kurationsfrage an den Autor des Instruments, kein Derivat aus Technik.
- **Auto-Loop ja/nein:** Der Einzelschritt ist der Kern. Ein Auto-Loop (Intervall- oder pro-Bake-Takt) ist trivial möglich, braucht aber eine Stopp-Ästhetik (transcribe terminiert von selbst; abduction nie).
- **Ein Feld, keine Historie sichtbar:** Der DCO-Prompt ist panel-lokal und flüchtig (bestehender Seam). Eine sichtbare Ketten-Historie (n Glieder zurück) wäre fürs Verstehen wertvoll, ist aber ein eigenes UI-Konzept — hier nicht mitentworfen.
- **Modell-Realismus:** Die Stance-Prompts wurden fürs neurale Vokabular kalibriert (3–10 Wörter, Timbre-Register). Ob sie unverändert lexikongängige DCO-Prompts erzeugen oder eine DCO-Fassung der User-Turns brauchen („write using oscillator words"), ist empirisch zu klären — dieselbe Prüfschleife wie beim entkitscher-Reframe (tools/test_entkitscher_prompt.py als Muster).
- Verwandt: `docs/SEMANTIC_LOOP_AESTHETICS.md` (Quelle der Idee), `docs/DCO_LLM_GUARDRAILS.md` (der Trichter, der alles absichert), Memory `project_semantic_loop_aesthetics` / `project_dco_oscillator`.
