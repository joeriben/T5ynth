# T5ynth v1.7.0-beta.1 Marketing Kit

Stand: 2026-05-01

Ziel: T5ynth soll Aufmerksamkeit bei Synth-, Sounddesign-, Open-Source- und kunstpaedagogisch interessierten Communities bekommen, ohne laufende Marketing-Personalressourcen zu erzeugen. Das Konzept ist bewusst auf wenige, wiederverwendbare Assets und wenige hochwertige Kontaktpunkte ausgelegt.

## 1. Kurzdiagnose

T5ynth ist schwer zu vermitteln, weil viele Begriffe sofort in die falsche Richtung fuehren. Chatbot-Begriffe klingen nach ChatGPT, technische Vektorbegriffe nach Fachsprache, Oszillator-Metaphern nach Theorie, KI-Musik nach Ergebnis-Automation. Der Einstieg muss deshalb aus musikalischer Kontrolle heraus kommen:

- Jedes der unterstuetzten Audiomodelle hat einen verborgenen Klangraum, bevor Audio entsteht.
- T5ynth macht diesen Raum als Instrument zugaenglich.
- Impuls A und Impuls B sind keine zwei Samples, keine zwei Songwuensche und keine zwei Oszillatoren.
- A und B markieren einen inneren Klangraum.
- Der T5ynth Oscillator ist ein Meta-Oszillator: kein einzelner Klanggenerator, sondern ein Zugang zu vielen moeglichen Klaengen innerhalb der Grenzen des gewaehlten Modells.
- T5ynth macht diesen Raum vor der Audio-Entstehung formbar: zwischen den Markern ziehen, staerker oder weicher machen, stoeren, entlang Klangqualitaeten biegen, einzelne Dimensionen oeffnen, den Eintritt einer Idee in die andere veraendern und den Raum driften lassen.
- Danach entsteht ein spielbares Fragment, das mit Sampler, Wavetable, Filter, Huelle, LFO, Sequencer, Delay und Reverb weitergeformt wird.

Die staerkste Formulierung ist:

> Resonance with meaning.

Die Abgrenzung zu Ergebnis-Automation ist wichtig, aber nicht als erster Satz. Der erste Satz soll Empowerment zeigen: nicht Blackbox bedienen, sondern den verborgenen Raum vor dem Ergebnis zugaenglich machen.

## 2. Positionierung

### Hero

English:

> Resonance with meaning.
> With two short impulses, this innovative music synthesizer opens up a hidden space before audio comes into being.

### Kernversprechen

T5ynth ist ein vollwertiger Musik-Synthesizer, der mit Bedeutung resoniert. Mit zwei kurzen Impulsen oeffnet T5ynth einen verborgenen Raum zwischen zwei Bedeutungspolen im aktuell gewaehlten Modell: Stable Audio Open 1.0, Stable Audio Open Small oder AudioLDM2. Diese Pole koennen Texturen, Transienten, Patterns, sonic and musical fragments, Field Recordings, Alltagsgeraeusche, Orchester-Gesten, Alien-Stimmen, menschliche Gefuehlsausdruecke oder unmoegliche Hybride sein. Der Raum ist ein begrenzter Raum des gewaehlten Modells, aber ein Raum mit vielen moeglichen Klaengen. Der T5ynth Oscillator ist in diesem Sinn ein Meta-Oszillator: Er erzeugt nicht zuerst einen Klang, sondern macht den Raum explorierbar, aus dem ein Klang werden kann. Dort kannst du zwischen den Markern ziehen, staerker oder weicher machen, stoeren, entlang Klangqualitaeten biegen, einzelne Dimensionen oeffnen, den Eintritt einer Idee in die andere veraendern und den Raum driften lassen. Normale KI-Audio-Anwendungen verbergen diesen Raum und liefern nur ein Ergebnis; T5ynth macht ihn als Instrument zugaenglich. Generation ist dabei kein separater KI-Schritt nach dem Synth, sondern ein Teil des Instruments. Danach wird das gerenderte Fragment mit Sampler/Wavetable, Filter, Envelopes, LFOs, Sequencer, Delay und Reverb weitergeformt.

### Claim-Optionen

1. "Resonance with meaning."
2. "With two short impulses, this innovative music synthesizer opens up a hidden space before audio comes into being."
3. "Mark two poles of meaning. Shape the space before generation."
4. "A/B mark a space, not two samples."
5. "From hidden model space to playable synth fragment."

### Drei Proof Points

1. **A/B markieren einen Klangraum:** Impuls A und Impuls B sind keine zwei Samples, keine Songwuensche und keine zwei Oszillatoren. Sie oeffnen einen inneren Klangraum mit vielen moeglichen Klaengen innerhalb der Grenzen des gewaehlten Modells.
2. **Pre-audio Kontrolle:** T5ynth macht den Raum formbar, bevor Audio entsteht: ziehen, druecken, stoeren, biegen, einzelne Dimensionen oeffnen, Eingriffspunkte veraendern, driften lassen.
3. **Generation als Teil des Instruments:** Der Synth beginnt nicht nach Generate. T5ynth greift in die Generierung selbst ein; das gerenderte Fragment ist eine Station im Signalweg.

### Kommunikations-Grenzen

- Immer als Beta kommunizieren.
- Public Release Scope: Die tagged GitHub Releases veroeffentlichen derzeit macOS- und Windows-Installer. Linux/VST3/AU nicht als aktuelle oeffentliche Release-Assets bewerben; nur in Build-/Source-/Roadmap-Kontexten nennen.
- Modellgewichte werden nicht gebundelt. Nutzer:innen laden Modelle separat und akzeptieren deren Lizenzen.
- AudioLDM2 ist non-commercial only; nicht als kommerziell frei nutzbare Engine bewerben.
- Nicht defensiv starten: keine Headline, die zuerst nur erklaert, was T5ynth nicht ist.
- Lieferanten-/Material-Framing nicht als Kernbotschaft verwenden. Besser: spielbares Fragment, verborgener Klangraum, pre-audio Kontrolle.
- "Meta-Oszillator" ist als erklaerender Begriff nuetzlich, aber nicht als erster Claim. Erst Resonanz/Klangraum/Empowerment, dann Meta-Oszillator als Praezisierung.
- Technische Vektor-/Encoder-Fachbegriffe erst im technischen Hintergrund verwenden, nicht in Hero, Kurzbeschreibung oder Pitch.
- Semantik-Oszillator-Framing nicht als Claim verwenden.
- Fuer den Modellraum konsequent "space" / "Klangraum" verwenden, nicht "field". "Pitch Field" bleibt nur als UI-/Feature-Name des Sequencers erlaubt.

## 3. Zielgruppen

### Primaer

1. Experimentelle Synth- und Sounddesign-User
   - Hook: "Ein Instrument, das vor der Audio-Entstehung ansetzt."
   - Gute Formate: kurze Audio/Videodemos, A/B-Klangraum-Sweeps, Injection-Mode-Vergleiche, Drift-Demos.

2. Open-Source- und Freeware-Audio-Communities
   - Hook: GPLv3, beta, lokales Instrument, keine SaaS-Subscription.
   - Gute Orte: KVR Product/News, GitHub Release, eventuell KVR Developer Challenge 2026 nach Eligibility-Check.

3. KI-kritische Musiker:innen
   - Hook: "Nicht die Blackbox bedienen, sondern vor dem Ergebnis eingreifen."
   - Gute Formate: FAQ, kurzer Manifest-Absatz, Demo "from hidden sound space to playable patch".

4. Artistic Research / Arts Education / Creative Coding
   - Hook: Zugriff auf einen verborgenen Modellraum als gestalterische und paedagogische Frage.
   - Gute Formate: LinkedIn, Uni-/Lab-Kontext, CDM Pitch, kurzer Essay.

### Sekundaer

- Plugin-News-Seiten und Synth-Blogs.
- Preset-/Sounddesign-/Soundpack-Macher.
- YouTube-Kanaele mit Fokus auf experimental synthesis, generative sequencing, AI in music production.

## 4. Low-Resource Operating Model

Die realistische Arbeitsform ist nicht "Social Media machen", sondern ein kleines Presskit plus ein woechentlicher Beweisclip.

### Einmalige Vorbereitung, 3-5 Stunden

1. Release-Seite pruefen: GitHub Release, README, Installationshinweise, Screenshots.
2. Presskit-Ordner anlegen: 6 Screenshots, 6 Audio-Clips, 2 kurze Videos, Logos/Icon, 1 Pressetext.
3. Eine Landing-Notiz in GitHub/README verlinken: "Resonance with meaning."
4. KVR Product/News vorbereiten.
5. Kontaktliste mit 8-12 Stellen anlegen.

### Laufender Rhythmus, 45-75 Minuten pro Woche

- Ein Clip oder Screenshot-Thread pro Woche.
- Maximal zwei Community-Antwortfenster pro Woche, z.B. Dienstag/Freitag je 20 Minuten.
- Bugs konsequent auf GitHub Issues umlenken.
- Kein taegliches Posten, keine Plattform bedienen, die staendige Kurzvideo-Produktion erzwingt.

## 5. 30-Tage-Plan

| Zeitraum | Ziel | Aktionen | Aufwand |
|---|---|---|---|
| Tag 1-2 | Basis materialisieren | Screenshots erzeugen, 3 Audio-Beispiele exportieren, Release-URL pruefen, Presskit-Ordner bauen | 3-5 h |
| Tag 3 | Owned Release | GitHub Release/README/README-Badge pruefen, Social-Post 1, KVR Product/News einreichen | 1-2 h |
| Woche 1 | Fachpresse anstossen | 6-8 individuelle E-Mails an passende Blogs/Magazine | 2 h |
| Woche 2 | Verstaendnis schaffen | Clip: "Resonance with meaning" | 1 h |
| Woche 3 | Feature beweisen | Clip: BPM-synced Drift + Auto-Regenerate | 1 h |
| Woche 4 | Community vertiefen | Forum-Post oder devlog: Injection Modes und warum sie den Klangraum anders bewegen | 1 h |

Danach monatlich: ein neues Preset-Pack, ein Feature-Clip oder ein kurzer Forschungs-/Entwicklungsbericht.

## 6. Kanalprioritaet

### Prioritaet A

1. GitHub Release + README
   - Muss klar, ruhig und installierbar sein.
   - Beta, Modell-Lizenzen und aktuelle Release-Assets sichtbar machen.

2. KVR Audio
   - KVR hat Developer Accounts, Product Database und News-Submissions fuer Audio-Software.
   - Zuerst normales Product/News-Listing.
   - KVR Developer Challenge 2026 ist interessant, aber vorab Eligibility klaeren: T5ynth ist bereits oeffentlich beta-released, waehrend die Challenge-Regeln Public-Beta-Testing und bereits oeffentliche Releases problematisch machen koennen. Nicht ungeprueft einplanen.

3. Synth Anatomy / Synthtopia / Create Digital Music / Bedroom Producers Blog
   - Nur kurze, spezifische Pitches.
   - Schwerpunkt: "resonance with meaning", "pre-audio control", "music synthesizer".

4. Ein Demo-Video auf YouTube/Vimeo
   - Kein YouTuber-Stil noetig.
   - 2-4 Minuten reichen: A/B markieren einen Raum, Alpha/Drift/Injection bewegen ihn, danach entsteht ein spielbarer Patch.

### Prioritaet B

- Mastodon / Bluesky: gut fuer KI-kritische, Open-Source- und Creative-Coding-Netzwerke.
- LinkedIn: gut fuer Forschungs-/Arts-Education-Winkel.
- KVR Forum / lines / Elektronauts / ModWiggler: nur wenn der Post technisch-substanziell ist und die jeweiligen Regeln gecheckt wurden.

### Nicht priorisieren

- Paid Ads.
- Taegliche Shorts/Reels/TikTok.
- Allgemeine AI-Influencer.
- Breite "producer hacks" Kanaele, solange Installation/Modell-Setup noch erklaerungsbeduerftig ist.

## 7. Asset-Liste

### Screenshots

1. `01-main-generation-panel.png`
   - Impulse A/B, Alpha, Injection Modes sichtbar; Caption: "A/B mark the hidden sound space."
2. `02-drift-bpm-sync.png`
   - Drift-LFOs mit Clock/Synchronisation; Caption: "Drift moves the space before audio exists."
3. `03-generative-sequencer-strands.png`
   - Polyphoner generativer Sequencer mit mehreren Strands.
4. `04-wavetable-sampler-mode.png`
   - Sampler/Wavetable als vertraute Synth-Bruecke nach der Generierung.
5. `05-dimension-explorer.png`
   - 768 Dimensionen als Forschungs-/Explorationsbild.
6. `06-model-manager.png`
   - ehrliche Modellinstallation und Lizenzsichtbarkeit.

### Audio-Clips

1. "A/B sound-space sweep": ein Impuls-Paar, Alpha langsam bewegen.
2. "Injection mode comparison": Linear, Step-in, Layer, Combo 1-3, gleiches Impuls-Paar.
3. "BPM-synced drift": 8 Takte mit Drift, Auto-Regenerate und Delay Sync.
4. "Playable wavetable": aus einem generierten Fragment wird eine spielbare Tonfolge.
5. "Generative sequencer": 5 Strands, Pitch Field, Filter/Delay.
6. "Hidden-space-to-instrument": A/B + Alpha/Drift -> Fragment -> Filter/Envelope/Delay/Reverb.

### Video-Mini-Skripte

1. 90 Sekunden: "Resonance with meaning"
   - 0-15s: Hero-Satz, Generation Panel zeigen.
   - 15-35s: Impuls A/B markieren einen inneren Klangraum, keine zwei Samples.
   - 35-65s: Alpha, Drift und Injection Modes bewegen diesen Raum vor der Audio-Entstehung.
   - 65-90s: Generate, Fragment spielen, mit Synth-Werkzeugen formen.

2. 2 Minuten: "BPM-synced Drift in v1.7"
   - LFO/Drift Clock einschalten.
   - Slow division `4/1` oder `8/1`.
   - Auto-Regenerate zeigen.
   - DAW/Sequencer-Bezug hoerbar machen.

3. 3 Minuten: "Six injection modes, one sound space"
   - Gleiches Impuls-Paar.
   - Linear als Referenz.
   - Step-in/Layer/Combo als unterschiedliche Eingriffe in die Diffusion.
   - Keine Theorie ueberfrachten; hoerbarer Vergleich zaehlt.

## 8. Textmaterial

### One-liner, English

Resonance with meaning.

### Kurzbeschreibung, English

T5ynth is an innovative music synthesizer built around resonance with meaning. Impulse A and Impulse B mark two poles of meaning in the selected engine: Stable Audio Open 1.0, Stable Audio Open Small, or AudioLDM2. The space between them becomes shapeable before audio exists: pull, push, perturb, bend along sound qualities, open individual dimensions, change where one idea enters the other, and let the space drift. T5ynth then renders a playable fragment that can be shaped with sampler/wavetable playback, filters, envelopes, LFOs, sequencing, delay and reverb.

### Website-/README-Teaser, English

T5ynth does not start with a sample or a wavetable. It starts one step earlier: in the resonance space between two poles of meaning. Impulse A and Impulse B are not two finished results; they mark a space of possible sounds in the selected engine.

Alpha, Magnitude, Noise, sound-character axes, the Dimension Explorer, Injection Modes and Drift move that space before audio exists. Only then does T5ynth render a short playable fragment. From there, the workflow becomes familiar synthesis: sampler or wavetable playback, filters, envelopes, LFOs, sequencing, delay and reverb.

In v1.7.0-beta.1, LFOs, Drift LFOs and Delay can be clock-synced, tying slow multi-bar movement, background regeneration and tempo-locked delay more closely to DAW, sequencer and performance workflows.

### Press pitch, English

Subject: T5ynth v1.7.0-beta.1 - resonance with meaning

Hi [Name],

I would like to share T5ynth v1.7.0-beta.1, an innovative music synthesizer that gives musicians a sound space they define themselves.

The Generation section starts with Impulse A and Impulse B. These are not two samples, two song requests or two oscillators; they are two markers that open up a space of possible sounds in the selected engine: Stable Audio Open 1.0, Stable Audio Open Small, or AudioLDM2. Alpha, Magnitude, Noise, sound-character axes, the Dimension Explorer, Injection Modes and Drift move that space before audio is generated. T5ynth then renders a short playable fragment that can be used as a sampler or wavetable source and shaped with familiar synth tools.

The new version adds BPM sync for LFOs, Drift LFOs and Delay. Slow pre-audio movement, background auto-regeneration and delay movement can now be tied to musical divisions across multiple bars, making T5ynth easier to connect to DAW and sequencer workflows.

Release: https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1
Repository: https://github.com/joeriben/t5ynth
Press kit / screenshots / audio: [Press kit URL]

Best,
[Name]

### KVR-/Plugin-News-Text, English

T5ynth v1.7.0-beta.1 has been released.

T5ynth is an innovative GPLv3 music synthesizer that gives musicians a sound space they define themselves. Its Generation section opens a pre-audio space in the selected engine: Stable Audio Open 1.0, Stable Audio Open Small, or AudioLDM2. Impulse A and Impulse B mark that space, while Alpha, Magnitude, Noise, sound-character axes, a 768-dimension explorer and diffusion-layer injection modes move it before audio is generated. The resulting fragment can then be played through sampler or wavetable modes and processed with filters, envelopes, LFOs, drift, sequencing, delay and reverb.

New in v1.7.0-beta.1:

- BPM sync for LFOs, Drift LFOs and Delay Time.
- Multi-bar drift divisions for slow pre-audio and timbral movement.
- Host transport sync when available, with standalone sequencer fallback.
- Fixes for preset restore of clock modes and injection modes.
- Per-voice LFO Trigger mode fix.
- True dry/wet crossfade for Delay mix.

T5ynth is currently a beta release. Model weights are not bundled and must be downloaded separately under their respective licenses. Tagged GitHub releases currently publish macOS and Windows installers.

Links:

- Release: https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1
- Source: https://github.com/joeriben/t5ynth
- Presets: https://github.com/joeriben/T5ynth-Presets

### Forum post, English

Title: T5ynth v1.7.0-beta.1 - resonance with meaning

I released T5ynth v1.7.0-beta.1. Short version: it is an innovative music synthesizer that gives musicians a sound space they define themselves.

The UI calls the two text fields Impulse A and Impulse B. These are not ChatGPT commands, song requests, two samples or two oscillators, but musical impulses such as "steady clean saw wave, C3" and "120 bpm syncopated transient pattern". A/B mark an inner sound space. Alpha, Magnitude, Noise, sound-character axes, the Dimension Explorer, Injection Modes and Drift move that space before audio exists.

T5ynth then renders a short playable fragment. From there, it becomes a familiar synth workflow: sampler or wavetable playback, filters, envelopes, LFOs, drift, sequencing, delay and reverb.

New in v1.7.0-beta.1 is BPM sync for LFOs, Drift LFOs and Delay. For long drift movements and background auto-regeneration, this matters musically: the sound space keeps moving, but now it can move in musical divisions.

Release: https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1
Repository: https://github.com/joeriben/t5ynth
Audio/screenshots: [Press kit URL]

I am especially interested in audible A/Bs: which impulse pairs and injection modes open musically compelling spaces, and which ones break in interesting ways?

### Social posts, English

1. T5ynth v1.7.0-beta.1 is out: an innovative music synth built around resonance with meaning. A/B mark the space; Alpha, Drift and Injection move it before audio exists. https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1

2. Impulse A and B in T5ynth are not two samples, song requests or oscillators. They mark a sound space inside the model. Then Alpha, Noise, axes, Dimension Explorer, Injection and Drift decide where that space becomes audio. https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1

3. A sound can begin before audio exists: cross "steady clean saw wave, C3" with "120 bpm syncopated transient pattern". T5ynth turns that space into a playable fragment for sampler, wavetable, filters and sequencing. v1.7 adds clocked Drift. https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1

4. New clip: one impulse pair, six Injection Modes. Not six orders sent to a black box, but six interventions in the sound space before generation. This is where T5ynth becomes interesting: as an instrument. [Video link]

### LinkedIn / Research post, English

T5ynth v1.7.0-beta.1 is out. For me, the project is a practical thesis: generative models do not have to be result machines. They can also be treated as spaces where musical decisions happen before the result appears.

T5ynth gives musicians a sound space they define themselves. Impulse A and B mark that space in the selected audio engine; if you want to, Alpha, Semantic Axes, Noise, the Dimension Explorer, Injection Modes and Drift move it before audio exists. Only then does T5ynth render a playable fragment that can be shaped with synth tools.

The new version adds BPM sync for LFOs, Drift LFOs and Delay. This makes movement of the pre-audio sound space rhythmically connectable to DAWs, sequencers, repetition and performance.

It is also an arts education question: how can AI systems be reworked so they foster access, exploration, judgment and creative agency rather than consumption and substitution?

Release: https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1

## 9. Presskit-Struktur

Empfohlene Struktur:

```text
presskit/
  README.txt
  screenshots/
    01-main-generation-panel.png
    02-drift-bpm-sync.png
    03-generative-sequencer-strands.png
    04-wavetable-sampler-mode.png
    05-dimension-explorer.png
    06-model-manager.png
  audio/
    01-ab-sound-space-sweep.wav
    02-injection-mode-comparison.wav
    03-bpm-synced-drift.wav
    04-playable-wavetable.wav
  video/
    t5ynth-inside-text-to-audio-model-90s.mp4
  text/
    press-release-en.md
    short-copy.txt
```

`README.txt` sollte nur enthalten:

- Was ist T5ynth in einem Satz?
- Was ist neu in v1.7.0-beta.1?
- Download-Link.
- Plattform-/Beta-Hinweis.
- Modell-/Lizenz-Hinweis.
- Kontakt.

## 10. Erfolgskriterien

Nicht auf Downloadzahlen allein schauen. Fuer ein Beta-Instrument sind bessere Signale:

- 3-5 qualifizierte Community-Kommentare mit echtem Sounddesign-Interesse.
- 1-2 externe Posts/News-Erwaehnungen.
- 1 Demo/Clip, der von jemand anderem geteilt wird.
- 2-3 neue Issues mit konkreten, reproduzierbaren Beobachtungen.
- 1 Preset- oder Sound-Beitrag aus der Community.

## 11. Naechster sinnvoller Schritt

Nach README, Manual und Marketing-Kit sollte ein kurzes Demo-Video entstehen:

1. Generation Panel zeigen.
2. A/B als Klangraum erklaeren.
3. Alpha sweepen.
4. Injection Mode wechseln.
5. Drift + Auto-Regenerate einschalten.
6. Fragment in Wavetable/Sampler spielen.
7. Filter/Delay/Reverb dazunehmen.

Kein langes Tutorial. Ziel: In 90 Sekunden beweisen, dass T5ynth nicht eine Blackbox bedient, sondern den Raum vor dem Ergebnis spielbar macht.
