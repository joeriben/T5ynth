# Der semantische Loop — ästhetische Modi, Motoren, Referenzen

*Stand: 2026-06-13. Forschungs-/Designnotiz, keine Implementierungszusage. Festgehalten aus einer Arbeitssitzung, damit die Begriffe und die Bibliografie nicht verloren gehen.*

## Worum es geht

T5ynths bestehender Resynth-Loop ist ein **Signal-Loop**: Audio → `init_audio` (SA3) → Audio (siehe `docs/INJECTION_RECTANGLE_PLAN.md` und die i2i-Notizen; der Output wird als Audio-Konditionierung zurückgespeist). Der hier dokumentierte **semantische Loop** ist etwas anderes: der Output wird *in Begriffe übersetzt*, und die Begriffe steuern die nächste Runde —

```
Audio → CLAP-Audio-Embedding → nächste Tags aus einem Vokabular
      → Prompt → T5-Encoder → Audio
```

Wichtig: **CLAP beschreibt nicht, CLAP rankt.** Es hat keinen Sprach-Decoder; es projiziert Audio und Text in denselben Vektorraum und misst Ähnlichkeit. „Beschreiben" heißt hier immer *gegen eine Kandidatenliste ranken* (Zero-Shot-Klassifikation), kein freies Captioning. Freies Captioning gäbe es nur mit einem Audio-LLM (Qwen2-Audio, SALMONN, LTU) — ressourcenlastiger Plan B, der zugleich das Ohr *und* den Interpreten ersetzt.

Die Machbarkeitsprobe für die CLAP-Seite liegt in `tools/clap_probe.py` (music-getuntes `laion/larger_clap_music`, drei Kandidatenvokabulare, Trennbarkeits- und Drift-Metriken).

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
Pointe: Homöostase ist konservativ (zieht zum Attraktor zurück) und damit zugleich das **Gegengift gegen die Degenerations-/Kollaps-Gefahr des i2i-Loops** — der Gouverneur, der die Rückkopplung daran hindert, in den Loop-Attraktor zu fallen.
→ Linie: 2.-Ordnung-Kybernetik; Di Scipio, Kayn, Tudor, Nakamura (s. Bibliografie).

### 2. Variation / B-Prompt-Entwurf (mittig)
Braucht generative Rekombination. CLAP kann nur *vorhandene* Kandidaten ranken → „nimm benachbarte Tags" ist eine schwache Variation. — **Motor: CLAP-Ohr + LLM.**
Durch den 2-Embedding-Osc ist eine „Variation" hier kein *Punkt*, sondern eine *Richtung* (siehe Modus 5).

### 3. Kritik / Kontextualisierung — westliche Biases, Orientalismus (dekonstruktiv)
Der anspruchsvollste *und* heikelste Modus; der, der das Projekt erst *eigen* macht. Zwei ehrliche Punkte:

- Die *kritische Geste* (rekontextualisieren, widerstehen) braucht Weltwissen → LLM. CLAP kann höchstens *Nähe* zu einer klischee-etikettierten Region *detektieren* — und ein solches Vokabular zu bauen ist selbst schon ein kuratorischer, situierter Akt.
- **Der Widerspruch:** CLAP/MusicCaps/AudioSet *sind* das westliche, anglophone Ohr (trainiert auf YouTube/AudioSet mit englischen Captions). „Orientalismus mit CLAP detektieren" hieße, mit genau dem Instrument zu kritisieren, das den Bias verkörpert. Für ein kritisch-ästhetisches/arts-education-Projekt ist das kein Bug, sondern ein **produktiver Widerspruch**, den man *vorzeigen* statt verstecken sollte.

Daher die schärfere Fassung: nicht **korrigieren** (paternalistischer Techno-Solutionismus — selbst ein neues Klischee), sondern **exponieren/befragen** — die Taxonomie des Datensatzes *als Konstrukt hörbar* machen („dieser Output liegt im 'world/exotic'-Cluster von AudioSet"), den Synth zum *reflexiven* Instrument machen, das die kulturelle Eingebettetheit von „gutem Klang" verhandelbar macht.
— **Motor: CLAP-Detektor + LLM-Kritiker, reflexiv gerahmt** (oder Audio-LLM, um unter das Vokabular zu hören).
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
- **Starke Antwort** (die eigentliche): Fast alle KI-Audio-Werkzeuge (Magenta voran) zielen auf *mehr/besseres* Material — affirmativ, produktivistisch. Ein Synth, der die *kulturelle Situation seines eigenen Outputs kontextualisieren und befragen* kann, ist eine **andere Kategorie**: ein **kritisch-ästhetisches und pädagogisches Instrument**, das die Biases des maschinellen Hörens hörbar und verhandelbar macht. Das ist die Durchgangslinie zur anti-extraktiven A/B-Gleichheit und zum Arts-Education-Rahmen — eine Position, die kommerziell wie im Magenta-Forschungsraum praktisch niemand besetzt.

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

- Die Zuschreibungen sind aus dem Stand synthetisiert (Computermusik, Klangkunst, Sound Studies). Tragfähig, aber den **Kritik-Modus** mit jemandem aus *dem* Feld gegenlesen — das ist Autorschaft und Politik, kein Feature-Toggle.
- Der Befund der CLAP-Probe verschiebt die Lesart je Modus: für **Homöostase** zählt CLAPs *Konsistenz* (ähnliche Klänge → ähnliche Region); für den **Kritik-Modus** ist der MusicCaps/AudioSet-*Bias gerade informativ* (er zeigt die westliche Taxonomie).
- Verwandt: `docs/RESEARCH_IDEAS.md` (Embedding-Mix-Strategien), `docs/INJECTION_RECTANGLE_PLAN.md` (Injection-Modi), die i2i-/Resynth-Notizen (Signal-Loop), `tools/clap_probe.py` (CLAP-Machbarkeit).
