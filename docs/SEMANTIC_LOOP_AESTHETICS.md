# Der semantische Loop — ästhetische Modi, Motoren, Referenzen

*Stand: 2026-06-13. Forschungs-/Designnotiz, keine Implementierungszusage. Festgehalten aus einer Arbeitssitzung, damit die Begriffe und die Bibliografie nicht verloren gehen.*

## Worum es geht

T5ynths bestehender Resynth-Loop ist ein **Signal-Loop**: Audio → `init_audio` (SA3) → Audio (siehe `docs/INJECTION_RECTANGLE_PLAN.md` und die i2i-Notizen; der Output wird als Audio-Konditionierung zurückgespeist). Der hier dokumentierte **semantische Loop** ist etwas anderes: der Output wird *in Begriffe übersetzt*, und die Begriffe steuern die nächste Runde —

```
Audio → CLAP-Audio-Embedding → nächste Tags aus einem Vokabular
      → Prompt → T5-Encoder → Audio
```

Wichtig: **CLAP beschreibt nicht, CLAP rankt.** Es hat keinen Sprach-Decoder; es projiziert Audio und Text in denselben Vektorraum und misst Ähnlichkeit. „Beschreiben" heißt hier immer *gegen eine Kandidatenliste ranken* (Zero-Shot-Klassifikation), kein freies Captioning. Freies Captioning gäbe es nur mit einem Audio-LLM (Qwen2-Audio, SALMONN, LTU) — ressourcenlastiger Plan B, der zugleich das Ohr *und* den Interpreten ersetzt.

Die Machbarkeitsprobe für die CLAP-Seite liegt in `tools/clap_probe.py` (`laion/clap-htsat-unfused`, drei Kandidatenvokabulare, Trennbarkeits- und Drift-Metriken, plus ein Sinus/Rausch-Selbstcheck, der defekte Modelle laut abfängt).

## Probe-Befund (2026-06-13)

Erster Lauf gegen die echten Resynth-Outputs (`tools/clap_probe_out/REPORT.md`):

- **CLAP hört Synth-Texturen — ja.** Drei *unabhängig* erstellte Vokabulare (gemined MusicCaps, AudioSet-Ontologie, handgemachte naive-Liste) konvergieren pro Klang auf denselben semantischen Cluster (z. B. `original.wav` → aggressive/shouting/screaming bei allen dreien; ein Bass-Sample → bass/heavy/punchy). Diese Cross-Vokabular-Konvergenz ist starkes Indiz, dass CLAP echte perzeptuelle Qualität liest, nicht Rauschen. top1-Cosinus 0.47–0.52 (gesund). **Go-Signal für die Modi, die ein funktionierendes Ohr brauchen (Homöostase, Assoziation).**
- **Trennbarkeit ≠ Größe — empirisch bestätigt.** Redundanz: audioset (632 Labels) **0.156** < musiccaps (200) 0.247 < naive (112) **0.291**. Die kleinste Liste ist die redundanteste (die absichtlichen Synonym-Cluster), die größte die am besten gespreizte. Bestätigt die „prune-to-spread statt mehr-Labels"-Empfehlung.
- **Register-Befund:** audioset gibt die *entschiedensten*, aber *quellen*-gerahmten Tags („Speech synthesizer", „Hi-hat", grotesk auch „Donkey, ass" für einen aggressiven Synth); naive die *timbre/affekt*-gerahmten, aber redundanten („digital, robotic, punchy, fat"); musiccaps mischt Brauchbares mit affektivem Füllwort-Müll („engaging, addictive, youthful"). → Das ideale Loop-Vokabular ist ein *kuratiertes Timbre/Affekt-Register (naive-Stil), getrimmt auf Trennbarkeit (audioset-Stil)* — genau die Synonym-Collapse-/Max-Spread-Empfehlung.
- **Geschenk für den Kritik-Modus:** dass CLAP einen abstrakten Synth-Drone auf „Children shouting"/„Donkey, ass" abbildet, *zeigt* unmittelbar die Quellen-/Kulturtaxonomie, die CLAP der unbekannten Textur aufzwingt — der bias-tragende Charakter wird hier sichtbar. Nicht Störung, sondern genau das Material des Kritik-Modus.
- **Drift (Q3):** CLAP-Audio-Embeddings spreizen stark (Cosinus zum Anker 0.11–0.90) → das Ohr unterscheidet die Varianten klar. ABER *nicht monoton* in „sigma": `sigma0.050_iter20` driftet weit (0.111), `sigma0.500_iter10` bleibt nah (0.904) — Iterationszahl × Sigma, und der Resynth bewegt sich nicht monoton im CLAP-Raum. Eine saubere Monoton-Demonstration bräuchte einen kontrollierten Einzelfamilien-Sweep (Vorbehalt: `anchor_family.wav` ist evtl. ein anderer Basisklang).
- **Gotcha dauerhaft dokumentiert:** `laion/larger_clap_music` (music-getunt) ist im HF-Port **textseitig defekt** (Pooler-Kollaps, Cross-Modal-Ausrichtung ~0.01) — Audio-Turm gesund. `assert_model_sane()` fängt diese Bug-Klasse jetzt laut ab. Default daher `clap-htsat-unfused` (generisch, aber korrekt). Music-Tuning-Rückgewinnung = Folgeschritt über nativen `laion_clap`-Loader.

## Folge-Analysen (2026-06-13): kuratiertes Vokabular + kontrollierter Drift-Sweep

`tools/clap_followup.py` (Output `tools/clap_followup_out/REPORT.md`):

**A. Kuratiertes Timbre-Vokabular, auf Trennbarkeit gepruned.** 146 handkuratierte Timbre/Affekt-Begriffe → max-min/k-center-Pruning auf 64 maximal gespreizte. Redundanz **0.284 → 0.212** — im *richtigen* Register (Timbre/Affekt) und besser separiert als naive (0.291) oder musiccaps (0.247), nahe an audioset (0.156, das aber quellen-gerahmt ist). Das Pruning hält je Synonym-Cluster einen Vertreter (von bright/brilliant/airy/piercing/shrill bleibt nur „dull"/„dark"). Re-Ranking gibt saubere Timbre-Tags ohne Quellen-Attribution oder Affekt-Füllwörter (original → violent/aggressive/anxious/rough; ein Bass-Sample → punchy/explosive/fat/distorted). **→ Das vom Probe-Befund anvisierte „ideale Loop-Vokabular", jetzt gebaut.**

**B. Kontrollierter Drift-Sweep (8 σ × 6 Iter, alle Resynths *desselben* Originals).**
- **Innerhalb jedes σ: glatte monotone Abnahme** des Cosinus zum Original über die Iterationen (σ0.05: 0.94→0.77→0.52→0.16→0.13→0.11). **CLAP verfolgt die kumulative Loop-Drift treu — Q3 positiv aufgelöst** (der frühere „nicht-monoton"-Eindruck kam von gemischten Klängen, nicht von einem CLAP-Defizit).
- **Niedriges σ driftet am stärksten** (σ0.05 → 0.11) — konsistent mit der spektralen finA (low σ = starke Evolution).
- **Hohes σ: CLAP WIDERSPRICHT der spektralen Metrik.** finA sagt Wash-out bleibt *nah* (0.94 bei σ0.48); CLAP sagt es driftet *weit* (σ0.475 iter20 = 0.16), oft über eine *späte Bifurkation* (σ0.50 hält bis iter10 bei 0.90, fällt dann auf 0.30). Befund: **CLAP-Wahrnehmungsdistanz ≠ spektrale timbre_corr — komplementär, nicht redundant.** Für einen *semantischen* Loop ist CLAP das relevante Signal (wahrgenommener Charakter, nicht Spektralhülle). Vorbehalt: die Spät-Bifurkationen bei hohem σ könnten teils Loop-Dynamik (Metastabilität), teils Sampling sein — nicht überinterpretieren.

**C. Music-getuntes CLAP getestet (Schritt 3).** `laion/larger_clap_music` ist defekt (s. o.), ABER `laion/larger_clap_music_and_speech` (gleiche „larger"-Linie) **besteht das Sanity-Gate** (sine 0.44/0.20, noise 0.13/0.39) — **kein nativer `laion_clap`-Loader nötig.** Fairer Vergleich (`tools/clap_followup_music_out/`) gegen unfused:
- **Trennbarkeit: unfused gewinnt.** Kuratiert-gepruned 0.212 (unfused) vs. 0.306 (music) — das Music-Modell ist anisotroper (text-text 0.59 vs. 0.20), Cosinus stärker gestaucht.
- **Timbre-Treue auf musikalischen Texturen: music gewinnt.** Für `original.wav` (Findings: „warm analog bass drone", Centroid **199 Hz** = dunkel) sagt music **warm/bowed/dull/breathy/consonant** — konsistent mit dem tiefen Centroid; unfused sagt **violent/aggressive/rough** — eine Fehlcharakterisierung. Music-Tuning liest *musikalischen* Charakter treuer.
- **Drift: beide verfolgen** (ähnlich).
→ **Echter Trade-off, modusabhängig** (wie CLAP-vs-LLM): für den **Regelungs-/Homöostase-Loop** (entschiedenes, separiertes Steuersignal) unfused; für **kritisch-ästhetische Charakterisierung** (treues Hören musikalischen Charakters) `music_and_speech`. Default bleibt unfused (Trennbarkeit + kanonisch); `music_and_speech` ist die validierte music-getunte Alternative. Der native `laion_clap`-Loader ist damit **nicht nötig**.

## Homöostase-PoC (2026-06-14): der kybernetische Regelkreis, gebaut

Modus 1 *konkret* auf der bestehenden init_audio-Resynth-Harness geschlossen (`tools/clap_homeostasis.py`, Bericht `tools/clap_homeostasis_out/REPORT.md`). Geschlossene Schleife: **Sensor** = CLAP-Cosinus des aktuellen Outputs zu einem fixen Anker; **Aktuator** = `init_noise_level` (sigma) der nächsten Generation; **Gesetz** = proportional, `sigma = clip(BASE + Kp·(Sollwert − cos), 0.05, 0.5)` (cos unter Soll = zu weit gedriftet → sigma hoch → zurückziehen; cos über Soll = zu nah → sigma runter → driften lassen). SA3 (einzige init_audio-Engine), Anker „warm analog bass drone", gleiches Prompt im Loop (= Homöostase-Regime; Cross-Prompt-Transport zu einem *anderen* B-Prompt ist Modus 5).

**Prozedur-Lehre (im Tool verankert): erst charakterisieren, dann regeln.** Die beiden ungeregelten Fix-sigma-Loops *sind* die Streckencharakterisierung — sie zeichnen das erreichbare Band; der Sollwert muss *hinein*. Der erste Lauf nahm Sollwert 0.5 (weit unter Band) → der Regler sättigte korrekt, aber nutzlos am sigma-Boden. Das war die Messung; 0.88 ist Band-Mitte.

- **Fixed seed (deterministisch, = `randomSeedToggle false`):** Die Strecke ist ein **starker semantischer Attraktor** — erreichbares Band über 12 Iter ist **eng, ~[0.84, 0.92]**: selbst maximaler Drift bewegt den CLAP-Cosinus nur von 0.97 auf 0.84. sigma ist ein sauberer, monotoner Aktuator (low driftet weg, high bleibt nah). Der Regler **moduliert sigma aktiv** (0.05→0.37) und hält den Mitte-Band-Sollwert (0.88) **besser als beide Fix-sigma-Extreme** (Settling-Fehler 0.036 vs. 0.043/0.043), mit sichtbarer Korrektur (Überschwingen it08–09 → sigma hoch → Erholung it10). Sichtbarer Rest-Offset (~0.85 statt 0.88) = erwarteter P-Regler-Rest; ein I-Term würde ihn tilgen. **→ Der kybernetische Loop schließt und regelt. Di Scipios „structural coupling", buchstäblich und gemessen.**
- **Random seed (= Plugin-„Automatic variation", `seed = -1`):** **Ehrliches Negativ-Ergebnis.** Frischer Seed je Iter injiziert *inkohärentes* Rauschen, das sich teilweise *auslöscht* statt aufzuaddieren → (a) systematischer Drift **verzögert** (low bleibt bis ~it10 hoch, bricht erst it11–12 ein), (b) das Seed-Rauschen **überdeckt sigmas Autorität** über die Regelgröße auf 12-Iter-Horizont (die beiden Baselines *kreuzen* sich). Entgegen der naiven Vermutung driftet **high sigma nicht mehr** — gleiches Prompt rendert in dieselbe Region, seed-unabhängig. P-Regelung auf cos-zu-Anker regelt dort **nicht zuverlässig** → braucht Integral/Filterung oder längeren Horizont; Einzellauf-Vergleich (unabhängige Seeds je Loop) ist zudem rausch-konfundiert.

**Folgerung.** Der Homöostat funktioniert im *deterministischen* Regime sauber (sigma = monotoner Aktuator). Das *stochastische* Regime — das die Auto-Variation des Plugins trifft — lockert den Attraktor **nicht**, sondern erschwert die Regelung (Rauschen schluckt das Stellsignal; das Einzellauf-Verdikt kippt zwischen Läufen → über Seeds mitteln). Quantifiziert nebenbei die **Loop-Attraktor-Stärke** des i2i-Loops, gegen die der Homöostat das Gegengift ist.

**Asymmetrie-Befund (PI-Versuch, derselbe Tag).** Ein Integral-Term (PI + EMA-Messfilter + Anti-Windup) zum Offset-Tilgen machte beide Regime *schlechter*. Grund = echte Strecken-Eigenschaft: **der Drift ist asymmetrisch/irreversibel.** Gleiches Stellsignal sigma=0.5 hält cos 0.92 *vom Anker aus*, bleibt aber bei ~0.80 *nach* dem Abdriften — einziger Unterschied = History. **sigma bremst den Drift, kehrt ihn aber kaum um** (das driftete init_audio ist ~halbes Signal). Das Integral nimmt beidseitige Korrigierbarkeit an; hier ist Abwärts-Overshoot unrecoverbar → es läuft hoch und rail-t. Reines P (von oben annähern, *stoppen*) ist der richtige Regler für eine irreversible Strecke. **Ästhetische Pointe:** das Selbst-Hören der Maschine hat eine *Richtung*, ist nicht zeitsymmetrisch — ein kleiner, konkreter Fall des Sogs zum eigenen Attraktor. Design-Konsequenz fürs Plugin: den Homöostaten als **Drift-Bremse** bauen, nicht als Sollwert-Servo (Details: `tools/clap_homeostasis_out/REPORT.md`).

## Die eine Achse, die alles ordnet

Die im Folgenden gesammelten Modi sind keine Varianten *einer* Operation. Sie spannen **affirmativ → dekonstruktiv** auf — und das ist fast deckungsgleich mit der technischen Achse **Ranking → Generierung**, also **Regelung → Autorschaft**. Daher ist „CLAP vs. LLM" keine einmalige Entscheidung; der **Modus wählt den Motor**.

> **CLAP ist das Ohr. Das LLM ist der Interpret.**

- Affirmative Modi schließen die Schleife mit dem **Ohr allein** (Distanz zu einem Ziel messen, korrigieren — fast Regelungstechnik).
- Dekonstruktive Modi brauchen den **Interpreten obendrauf**, weil sie etwas *erfinden* müssen, das im Vokabular noch nicht steht.
- Ein **Audio-LLM** ist beides in einem Modell (Plan B).

Folgerung für die Architektur: plausibel **CLAP immer (als Ohr), LLM optional darüber** — außer im reinen Audio-LLM-Pfad. Ein zweistufiges CLAP→Tags→LLM reasoniert über eine *symbolische, vokabularbegrenzte* Beschreibung (gut, wo das Symbolische der Punkt ist, z. B. Kulturkritik); ein Audio-LLM hört das *sub-symbolische* Detail (besser für Abduktion).

## Die fünf Modi

### 1. Homöostase / kybernetische Korrektur (affirmativ)
Reine *Regelung*: Distanz zu einer Zielregion im Embedding-Raum, zurückziehen. Braucht keine Sprache. — **Motor: CLAP allein.**
Homöostase ist konservativ (zieht zum Ziel zurück) — der affirmative Pol.
→ Linie: 2.-Ordnung-Kybernetik; Di Scipio, Kayn, Tudor, Nakamura (s. Bibliografie).

### 2. Variation / B-Prompt-Entwurf (mittig)
Braucht generative Rekombination. CLAP kann nur *vorhandene* Kandidaten ranken → „nimm benachbarte Tags" ist eine schwache Variation. — **Motor: CLAP-Ohr + LLM.**
Durch den 2-Embedding-Osc ist eine „Variation" hier kein *Punkt*, sondern eine *Richtung* (siehe Modus 5).

### 3. Kritik / Kontextualisierung — westliche Biases, Orientalismus (dekonstruktiv)
Der anspruchsvollste und heikelste Modus — und **noch Entwurf, nicht gebaut** (s. Ehrlichkeits-Fußnoten). Zwei Punkte, die er ernst nehmen müsste:

- Die *kritische Geste* (rekontextualisieren, widerstehen) braucht Weltwissen → LLM. CLAP kann höchstens *Nähe* zu einer klischee-etikettierten Region *detektieren* — und ein solches Vokabular zu bauen ist selbst schon ein kuratorischer, situierter Akt.
- **Der Widerspruch:** CLAP/MusicCaps/AudioSet *sind* das westliche, anglophone Ohr (trainiert auf YouTube/AudioSet mit englischen Captions). „Orientalismus mit CLAP detektieren" hieße, mit genau dem Instrument zu kritisieren, das den Bias verkörpert. Für ein kritisch-ästhetisches/arts-education-Projekt ist das kein Bug, sondern ein **produktiver Widerspruch**, den man *vorzeigen* statt verstecken sollte.

Die intendierte Haltung wäre: nicht **korrigieren** (paternalistischer Techno-Solutionismus — selbst ein neues Klischee), sondern **exponieren/befragen** — die Taxonomie des Datensatzes als Konstrukt hörbar machen („dieser Output liegt im 'world/exotic'-Cluster von AudioSet"). Ob der Synth das je einlöst, ist offen; im Instrument steht es heute nicht.
— **Motor (geplant): CLAP-Detektor + LLM-Kritiker, reflexiv gerahmt** (oder Audio-LLM, um unter das Vokabular zu hören).
→ Linie: Ochoa Gautier, Robinson, Said/Locke, Sterne. Anschluss an die hauseigene „anti-extractive"-A/B-Philosophie.

### 4. Assoziation / ästhetische Abduktion (dekonstruktiv)
Peirce: Abduktion = Schluss auf eine *neue Hypothese*, der kreative Schluss. CLAP *deduziert innerhalb* eines gegebenen Raums (bewertet Kandidaten); Abduktion *erweitert* den Raum (erfindet den Kandidaten). Strukturell generativ → **Motor: LLM, idealerweise Audio-LLM**, weil Abduktion sich am sub-symbolischen Detail festmacht, das ein Tag-Vokabular wegwirft.
→ Linie: Peirce; Dorst („frame creation"). Der offenste Modus — der Synth, der überrascht.

### 5. Entwicklungskette (quer)
Der scheinbare Widerspruch zum 2-Embedding-Osc löst sich auf: Das A/B-Paar *ist* schon ein **Vektor** (Richtung A→B im Embedding-Raum). Eine Kette ist eine **Trajektorie**. Der Osc steht ihr nicht entgegen — er ist der **Einzelschritt** der Kette: jedes Glied re-ankert (aktueller Output → nächstes A, imaginiertes Ziel → B). Das ist wörtlich die bestehende Auto-Regen-/Drift-Maschinerie. Eine T5ynth-„Kette" ist damit reicher als eine Playlist: eine Folge *gerichteter Morphs*, jeder mit eigener A→B-Spannung.
— **Motor: CLAP platziert jedes Glied, LLM imaginiert das nächste Ziel (Abduktion), der Osc ist die Morph-Engine.**
→ Linie: generative Trajektorie; Roads (Mikroklang).

## Synthese auf einen Blick

| Modus | Pol | Reale Linie | Minimaler Motor |
|---|---|---|---|
| Homöostase | affirmativ | Di Scipio, Kayn, 2.-Ordnung-Kybernetik | **CLAP allein** |
| Variation (B-Prompt) | mittig | computer-assisted composition | CLAP-Ohr + **LLM** |
| Kritik/Kontext | dekonstruktiv | Ochoa, Robinson, Locke | CLAP-Detektor + **LLM**, reflexiv |
| Abduktion | dekonstruktiv | Peirce; Dorst | **Audio-LLM** (sub-symbolisch) |
| Entwicklungskette | quer | generative Trajektorie; Roads | CLAP + LLM + **Osc-als-Morph** |

Die fünf Intuitionen entdecken je eine benannte Tradition wieder.

## Technische Ahnenlinie der CLAP-Idee

„Sound analysieren → in einen Wahrnehmungs-/Deskriptorraum legen → resynthetisieren" ist die **korpusbasierte/konkatenative Synthese**. Der CLAP-Loop ist deren *semantische* Fortschreibung: nicht Audio-Deskriptoren/Latents, sondern eine *sprachnahe* Karte. Genau diese Sprachkopplung ist der Neuwert gegenüber einem reinen Audio-Latent (RAVE).

## Wieso man das wollen sollte

- **Schwache Antwort** (generative/modulare Tradition): der *kontrollierte Kontrollverlust*. Kybernetische Klangkunst von Kayn bis Di Scipio schätzt die Emergenz — das System überrascht seinen Macher. Nicht *ein* Verhalten, sondern patchbare Tendenzen, die man *kultiviert*, nicht *spielt*.
- **Starke Antwort** (die Absicht, nicht der erreichte Zustand): Fast alle KI-Audio-Werkzeuge (Magenta voran) zielen auf *mehr/besseres* Material — affirmativ, produktivistisch. Die Gegenrichtung, die dieses Projekt *anstrebt*, wäre ein kritisch-ästhetisches und pädagogisches Instrument, das die kulturelle Situation seines eigenen Outputs befragbar hält, statt sie als gegeben zu nehmen — die Verbindungslinie zur anti-extraktiven A/B-Philosophie. Ob und wo das eingelöst ist, steht in den Ehrlichkeits-Fußnoten; heute ist es überwiegend Programm, nicht Funktion.

## Annotierte Bibliografie

### Kybernetik / ökosystemische Klangkunst
- **Agostino Di Scipio** (*1962, ital. Komponist). Schlüsseltext: „'Sound is the Interface': From Interactive to Ecosystemic Signal Processing", *Organised Sound* 8(3), 2003. Werkreihe *Audible Ecosystems* / *Ecosystemic* (u. a. *Background Noise Study*, *Modes of Interference*). Das DSP-System ist strukturell an den eigenen Klang im Raum gekoppelt (Mikrofon → Verarbeitung → Lautsprecher → Raum → Mikrofon); Verhalten *emergiert* aus der Rückkopplung. Theoriebezug: Autopoiese, „structural coupling". *Exakt der Homöostase-Modus.* → Einstieg: der Organised-Sound-Aufsatz.
- **Roland Kayn** (1933–2011, dt. Komponist). „Kybernetische Musik": elektronische Prozesse, die sich selbst organisieren; der Komponist *richtet ein generatives System ein*, das Material entwickelt, statt es zu komponieren. Werke: *Tektra* (1982–84), *A Little Electronic Milky Way of Sound*. → Einstieg: Reissues auf Frozen Reeds; Kayns eigene Bezeichnung „cybernetic music".
- **David Tudor** (1926–1996). *Rainforest* (I–IV, 1968–73): elektroakustische Umgebung; physische Objekte als klangerzeugende Resonatoren/Wandler — der Klang „lebt" in den Objekten. → Einstieg: *Rainforest IV* (Composers Inside Electronics).
- **Toshimaru Nakamura** (*1962). „No-Input Mixing Board": ein Mischpult ohne Eingang, nur die interne Rückkopplung als alleinige Klangquelle — das Instrument *ist* sein eigener Loop. Tokyoter Onkyō-Szene.
- **Theorierahmen:** Humberto Maturana & Francisco Varela, *Autopoiesis and Cognition* (1980) / *Der Baum der Erkenntnis* (1984); Heinz von Foerster (Kybernetik 2. Ordnung — das beobachtende System ist Teil des Beobachteten).

### Kritische / dekoloniale Sound Studies
- **Ana María Ochoa Gautier.** *Aurality: Listening and Knowledge in Nineteenth-Century Colombia* (Duke UP, 2014). Wie Hörweisen, Stimme und „aurality" in koloniale Wissensordnungen verstrickt sind. Der Schlüsseltext für „Hören ist nicht neutral".
- **Dylan Robinson** (Stó:lō). *Hungry Listening: Resonant Theory for Indigenous Sound Studies* (Univ. of Minnesota Press, 2020). „Hungry listening" = die siedlerkoloniale, *extraktive*, inhaltsgierige Hörweise, die Klang als Ressource konsumiert — kontrastiert mit indigenen Hörweisen. *Direkter Anschluss an die „anti-extractive"-A/B-Philosophie.*
- **Edward W. Said.** *Orientalism* (1978). Grundlagenkritik der westlichen Konstruktion des „Orients"; auf Klang/Musik angewandt von:
- **Ralph P. Locke.** *Musical Exoticism: Images and Reflections* (Cambridge UP, 2009); *Music and the Exotic from the Renaissance to Mozart* (2015). Wie „exotische" Klangsignaturen (Skalen, Instrumente, Gesten) Klischees codieren. → Einstieg für „orientalistische Klischees im Klang".
- **Jonathan Sterne.** *The Audible Past* (Duke UP, 2003); Hg., *The Sound Studies Reader* (Routledge, 2012). → Einstieg ins Feld Sound Studies überhaupt.

### Abduktion / Designtheorie
- **Charles Sanders Peirce.** Abduktion (auch „Retroduktion"): der Schluss auf eine erklärende *Hypothese* — die einzige Schlussform, die genuin Neues einführt (vs. Deduktion/Induktion). Relevant: der assoziierende Modus *erfindet* Kandidaten, deduziert nicht.
- **Kees Dorst.** „The Core of 'Design Thinking' and its Application", *Design Studies* 32(6), 2011; *Frame Innovation* (MIT Press, 2015). „Frame creation" = abduktiv einen neuen Deutungsrahmen entwerfen, statt im gegebenen zu optimieren. Die designtheoretische Übersetzung von Abduktion.

### Technische Ahnenlinie (korpusbasierte/konkatenative Synthese)
- **Diemo Schwarz.** *CataRT* (IRCAM): korpusbasierte konkatenative Synthese in Echtzeit — ein Klangkorpus wird nach Audio-Deskriptoren analysiert und in einem Deskriptorraum navigierbar. Der direkte nicht-neuronale Vorfahr der „Sound → Raum → Resynthese"-Idee.
- **FluCoMa** (Fluid Corpus Manipulation). Leitung Pierre Alexandre Tremblay, CeReNeM, Univ. Huddersfield (Partner u. a. IRCAM). Toolkit für Machine-Listening + ML auf Klangkorpora in Max/SuperCollider/Pd. Das Milieu, in dem „Deskriptor/Embedding-Analyse → Reorganisation" künstlerische Praxis ist.
- **Orchidea / Orchids** (IRCAM, Carmine-Emanuele Cella u. a.): computergestützte Orchestrierung — Zielklang → Kombination realer Instrumente, die ihn approximiert. „Abduktion auf ein Ziel hin" als bestehende Technik.
- **RAVE** (Antoine Caillon & Philippe Esling, IRCAM-ACIDS, 2021): neuronaler Echtzeit-Autoencoder für Audio; Latent-Navigation und Timbre-Transfer. Das neuronale Pendant; in T5ynth bereits evaluiert (Training suspendiert, s. Devlog/Memory).

### Generative Musik / Mikroklang
- **Brian Eno.** „Generative music" (Begriff popularisiert 1996); *Music for Airports* (1978); generative Apps (*Bloom*). Der affirmative, ambient-generative Pol.
- **Curtis Roads.** *Microsound* (MIT Press, 2001). Granular-/Mikroklang-Synthese. Relevant für T5ynths Granular-Engine und den „Strom aus Partikeln"-Aspekt der Entwicklungskette.

## Offene Punkte / Ehrlichkeits-Fußnoten

- **Was gebaut ist vs. was Programm ist (Stand jetzt):** Implementiert und spielbar ist der Off-Distribution-Apparat — A/B-Mischung samt Extrapolation über die Pole, die Injection-Modi über die DiT-Blöcke, die Embedding-Manipulatoren (Rauschen/„Chaos", Magnitude, Dimension-Offsets) und die Resynth-Rückkopplung. Das treibt das Modell weg vom Plausiblen — es macht damit noch keine *Biases* offen. Der semantische Loop (die fünf Modi oben, v. a. Modus 3) und alles, was „hörbar/verhandelbar" verspricht, ist **Entwurf**, nicht Funktion. Die kritisch-reflexive Beziehung zum Output entsteht, wenn überhaupt, im *Spielen* — nicht durch ein Feature, das sie ankündigt.
- Die Zuschreibungen sind aus dem Stand synthetisiert (Computermusik, Klangkunst, Sound Studies). Tragfähig, aber den **Kritik-Modus** mit jemandem aus *dem* Feld gegenlesen — das ist Autorschaft und Politik, kein Feature-Toggle.
- Der Befund der CLAP-Probe verschiebt die Lesart je Modus: für **Homöostase** zählt CLAPs *Konsistenz* (ähnliche Klänge → ähnliche Region); für den **Kritik-Modus** ist der MusicCaps/AudioSet-*Bias gerade informativ* (er zeigt die westliche Taxonomie).
- Verwandt: `docs/RESEARCH_IDEAS.md` (Embedding-Mix-Strategien), `docs/INJECTION_RECTANGLE_PLAN.md` (Injection-Modi), die i2i-/Resynth-Notizen (Signal-Loop), `tools/clap_probe.py` (CLAP-Machbarkeit).
