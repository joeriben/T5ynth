# LCO-Instrumente — Referenz

Diese Datei beschreibt jedes Instrument im LCO-Lexikon (`backend/dco_lexicon.json`): was es ist, welche Parameter (Achsen) es hat und was jede Achse *hörbar* verändert. Sie ist kein Bedienhandbuch für den Autor-LLM — das liest den Index in `backend/lco_library.json` — und keine Entwicklungsdokumentation, das ist `docs/plans/HANDOVER_LCO.md`. Sie ist der Übersetzungsnachweis zwischen Zahl und Klang, Achse für Achse, und jede Zahl darin ist aus dem Lexikon kopiert, keine selbst gemessen oder berechnet.

Stand: Lexikon-Version 68 (Commit `e4418f6d`). **70 Einträge, davon 67 mit Parametern** (3 ohne: `sine`, `noise`, `pink_noise`) **und 144 Achsen insgesamt.** Das Lexikon wird aktiv erweitert — während der Arbeit an diesem Dokument wuchs es von 64 auf 69, dann bei dieser Aktualisierung auf 70 Einträge (neu: `tom`) —, die Zahlen hier sind der Stand des genannten Commits.

Für jede Achse gilt eine Regel ausnahmslos: **ein Parameter ist ein Farbregler — er darf die Lautstärke nicht bewegen.** Grenze: 0,5 dB Streuung über die Anker einer Achse bei 220 Hz, 1,0 dB bei 55/110/440/880/1760 Hz. Wo eine Achse diese Grenze reißt, steht das hier explizit, nicht geglättet.

**Referenzvergleich:** kein einziges Instrument in dieser Bibliothek wurde bisher gegen eine echte Aufnahme des benannten Instruments gehört-verglichen — das ist das Abnahmekriterium, nicht die Kür, und jede Stand-Zeile trägt deshalb `Referenzvergleich: offen`, ausnahmslos. `tools/lco_reference.py` erzeugt dafür SA3-Referenzaufnahmen nach `tools/lco_reference_out/<key>/` (eine echte Aufnahme im selben Ordner wird identisch gelesen) und legt mit `pairs` die eigenen Renderings des Eintrags zum Hören daneben. Für acht Instrumente existiert diese Referenz bereits und kann gehört werden: `flute`, `cymbal`, `organ`, `harpsichord`, `rain`, `vibraphone`, `waterphone`, `singing_bowl` — abgenommen gegen sie ist bislang aber keiner, `Referenzvergleich: offen` gilt also unverändert auch für diese acht.

Familien: [Grundwellenformen](#grundwellenformen) · [Synthese-Oszillatoren](#synthese-oszillatoren) · [Modulation und Waveshaping](#modulation-und-waveshaping) · [Additiv und FM](#additiv-und-fm) · [Geschlagene Resonatorkörper](#geschlagene-resonatorkörper) · [Blasinstrumente und Rohrblatt](#blasinstrumente-und-rohrblatt) · [Gestrichen und Saite](#gestrichen-und-saite) · [Tasteninstrumente](#tasteninstrumente) · [Gezupfte Zungen und Idiophone](#gezupfte-zungen-und-idiophone) · [Stimme](#stimme) · [Insekten und Tiere](#insekten-und-tiere) · [Natur und Textur](#natur-und-textur) · [Reibung und ungewöhnliche Erreger](#reibung-und-ungewöhnliche-erreger)

---

## Übersicht

| Eintrag | Familie | Achsen | Stand |
|---|---|---|---|
| `saw` | Grundwellenformen | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `square` | Grundwellenformen | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `pulse` | Grundwellenformen | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `pwm` | Grundwellenformen | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `triangle` | Grundwellenformen | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `sine` | Grundwellenformen | 0 | ohne Parameter · Referenzvergleich: offen |
| `bass_saw` | Synthese-Oszillator | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `supersaw` | Synthese-Oszillator | 1 | offener Defekt: die `spread`-Achse spreizt 1,03 dB bei 55 Hz (Grenze 1,0) · Referenzvergleich: offen |
| `sync` | Synthese-Oszillator | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `chiptune` | Synthese-Oszillator | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `theremin` | Synthese-Oszillator | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `sub_sine` | Synthese-Oszillator | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `analog_osc` | Synthese-Oszillator | 4 | offener Befund: rendert bei den Defaults bit-identisch zu `saw` · Referenzvergleich: offen |
| `ring_mod` | Modulation/Waveshaping | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `cheby` | Modulation/Waveshaping | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `additive` | Additiv/FM | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `fm` | Additiv/FM | 3 | Glosse korrigiert 2026-07-25 (nur Text) · Referenzvergleich: offen |
| `fm_bell` | Additiv/FM | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `fm_ep` | Additiv/FM | 4 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `metallic_fm` | Additiv/FM | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `drum_head` | Geschlagen | 4 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `tom` | Geschlagen | 4 | neu 2026-07-26, gemessen · Referenzvergleich: offen |
| `struck_bar` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `cymbal` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `glass` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `struck_glass` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `vibraphone` | Geschlagen | 3 | Umbau läuft — der ausgelieferte Körper ist ein abklingender Sinus (×4-Teilton 34 dB unter dem Grundton, Halbwertszeit 0,30 s) · Referenzvergleich: offen |
| `handpan` | Geschlagen | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `waterphone` | Geschlagen | 2 | neu, noch nicht nach Gehör abgenommen · Referenzvergleich: offen |
| `singing_bowl` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `bonang` | Geschlagen | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `clarinet` | Blasinstrument | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `flute` | Blasinstrument | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `brass` | Blasinstrument | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `free_reed` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `harmonica` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `bagpipe` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `sax` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `double_reed` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `didgeridoo` | Blasinstrument | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `ocarina` | Blasinstrument | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `strings` | Gestrichen/Saite | 2 | überarbeitet 2026-07-25, gemessen; zusätzlich offener Befund: die `desk`-Achse spreizt 1,05 dB bei 110 Hz und 1,09 dB bei 1760 Hz · Referenzvergleich: offen |
| `string` | Gestrichen/Saite | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `tanpura` | Gestrichen/Saite | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `hurdy_gurdy` | Gestrichen/Saite | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `organ` | Tasteninstrument | 1 | Glosse korrigiert 2026-07-25 (nur Text) · Referenzvergleich: offen |
| `rhodes` | Tasteninstrument | 2 | überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen |
| `wurlitzer` | Tasteninstrument | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `harpsichord` | Gezupft/Idiophon | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `mbira` | Gezupft/Idiophon | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `jaw_harp` | Gezupft/Idiophon | 3 | überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen |
| `voice` | Stimme | 2 | überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen |
| `voice_ee` | Stimme | 2 | überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen |
| `voice_oo` | Stimme | 2 | überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen |
| `overtone_voice` | Stimme | 3 | offener Defekt: die `select`-Achse pumpt im Notenverlauf 6,66 dB · Referenzvergleich: offen |
| `cicada` | Insekt/Tier | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `cricket` | Insekt/Tier | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `frog` | Insekt/Tier | 3 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `noise` | Natur/Textur | 0 | ohne Parameter · Referenzvergleich: offen |
| `pink_noise` | Natur/Textur | 0 | ohne Parameter · Referenzvergleich: offen |
| `wind` | Natur/Textur | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `rain` | Natur/Textur | 2 | überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen |
| `surf` | Natur/Textur | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `thunder` | Natur/Textur | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `hiss` | Natur/Textur | 1 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `crackle` | Natur/Textur | 1 | überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen |
| `bubbles` | Natur/Textur | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `ice` | Natur/Textur | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `bullroarer` | Reibung | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |
| `cuica` | Reibung | 2 | parametrisiert, Bounds grün · Referenzvergleich: offen |

---

## Grundwellenformen

### `saw`
*Surface forms: saw, sawtooth, saw wave, säge, sägezahn, ramp, saw tooth*

Die kanonische helle analoge Sägezahnwelle: alle Harmonischen vorhanden, mit 1/h abfallend. Rohmaterial für blechbläser-, streicher- und leadartige Klänge.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| skew | [0.02 .. 0.5] | 0.02 | triangle 0.5 · leaning 0.38 · ramp 0.26 · keen 0.14 · sawtooth 0.02 | Verschiebt das symmetrische Dreieck kontinuierlich zum Sägezahn. Der Klangschwerpunkt wandert von 759 Hz (Dreieck) auf 2467 Hz (Sägezahn); das Verhältnis ungerade/gerade Harmonische fällt von +39,9 dB auf +3,3 dB — ein Dreieck hat fast nur ungeradzahlige Teiltöne, ein Sägezahn alle. Beide Enden sind exakte Wellenformen (Kammfilter-Kontrast bleibt nahe 110 dB), keine gefilterten Näherungen. Lautstärke bleibt über die ganze Achse konstant (0,01 dB Streuung). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `square`
*Surface forms: square, square wave, rechteck, rechteckwelle, squarewave*

Rechteckwelle: nur ungeradzahlige Harmonische, mit 1/h abfallend — hohl und holzig, die Familie von Klarinette und gedackter Orgelpfeife.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| duty | [0.05 .. 0.5] | 0.5 | square 0.5 · fat 0.38 · reedy 0.25 · nasal 0.15 · sharp 0.05 | Der EIN-Anteil des Zyklus. Objektiv zeigt sich die Wirkung am Verhältnis ungerade/gerade Harmonische: bei 0,5 fehlen die geraden Harmonischen ganz (+29,4 dB), bei 0,05 stehen sie gleich laut daneben (+0,8 dB). Der Klangschwerpunkt verläuft dabei NICHT monoton (6304 → 5398 → 5262 → 5148 → 5373 Hz von 0,50 bis 0,05), weil ein schmaler werdender Puls zunächst seine Höhen verliert und die Energie erst danach wieder nach oben verteilt. Die Lautstärke steht über die ganze Achse (0,01 dB). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `pulse`
*Surface forms: pulse, puls, rectangle, pulse wave, narrow pulse, rectangular wave*

Schmale Rechteckwelle (~10 % Duty) — das dünne, nasale, reedige Ende der analogen Wellenform-Achse, oboen- und cembalonah.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| duty | [0.05 .. 0.5] | 0.1 | square 0.5 · fat 0.38 · reedy 0.25 · nasal 0.15 · sharp 0.05 | Dieselbe Achse wie bei `square`, hier mit Standardwert 0,1 (nasal/dünn). Der EIN-Anteil des Zyklus bestimmt das Verhältnis ungerade/gerade Harmonische (+29,4 dB bei 0,5 bis +0,8 dB bei 0,05); der Klangschwerpunkt verläuft nicht monoton (6304 → 5398 → 5262 → 5148 → 5373 Hz), weil eine schmaler werdende Pulsbreite zunächst die Höhen verliert und danach Energie wieder nach oben streut. Lautstärke konstant (0,01 dB). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `pwm`
*Surface forms: pwm, pulse width modulation, pulsbreite, pulsbreitenmodulation, pulsweitenmodulation, pw, duty sweep, pulsweite*

Klassisches PWM: die Pulsbreite schwingt kontinuierlich zwischen 80 % und 20 % und zurück — der Ton höhlt sich aus und füllt sich wieder.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| rate | [0.1 .. 8.0] | 0.5 | slow 0.15 · medium 0.5 · fast 2.0 · vibrato 8.0 | Wie schnell die Pulsbreite schwingt, in Hz. Das hörbare Signal ist der Beat, nicht die Farbe, und er liegt bei genau der doppelten Rate: 0,5 Hz Einstellung ergibt 1,00 Hz Beat, 1,0 Hz ergibt 2,00 Hz, 8,0 Hz ergibt 16,00 Hz — weil die Schwingung symmetrisch um das Rechteck läuft und beide Halbzyklen je einen Helligkeits-Peak liefern. Die Tiefe des Effekts bleibt ab 0,5 Hz konstant bei 0,22. Lautstärke über die Achse 0,41 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `triangle`
*Surface forms: triangle, triangle wave, dreieck, dreieckwelle, dreieckswelle*

Dreieckwelle: ungeradzahlige Harmonische, die mit 1/h² abfallen — die weichste der klassischen analogen Wellenformen, nahe an einer Flöte oder einem weichen Pfeifton.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| skew | [0.02 .. 0.5] | 0.5 | triangle 0.5 · leaning 0.38 · ramp 0.26 · keen 0.14 · sawtooth 0.02 | Dieselbe Achse wie bei `saw`, hier mit Standardwert 0,5 (Dreieck). Der Klangschwerpunkt wandert von 759 Hz (Dreieck) auf 2467 Hz (Sägezahn), das Verhältnis ungerade/gerade Harmonische von +39,9 dB auf +3,3 dB. Beide Enden sind exakte Wellenformen, keine gefilterten Näherungen. Lautstärke über die ganze Achse konstant (0,01 dB). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `sine`
*Surface forms: sine, sine wave, sinus, sinuswelle, pure tone, grundton, reiner ton*

Reiner Sinus: ein einzelner Teilton, kein harmonischer Inhalt. Referenzton und Klangkörper für Pfeifton, Stimmgabel oder Subbass. Ohne Parameter.

**Stand:** ohne Parameter · Referenzvergleich: offen

---

## Synthese-Oszillatoren

### `bass_saw`
*Surface forms: moog, bass, 303, moog bass, bassline, bass synth, acid, acid bass, tb 303, synthbass, bass lead*

Synth-Bass im Stil von Moog-Bass / TB-303 / Acid-Bassline: ein dunkles Sägezahn-Spektrum mit gedeckeltem Oberton-Anteil für ein aufgeräumtes tiefes Ende.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| bite | [6 .. 40] | 24 | dull 6 · capped 24 · open 40 | Die Tiefpass-Deckelung oberhalb der Sägezahnquelle, in Harmonischen der gespielten Note. Der Klangschwerpunkt läuft von 614 Hz (dumpf, fast Subton mit etwas Buzz) auf 2102 Hz (mehr Kante, immer noch deutlich unter einem vollen Sägezahn) bei 220 Hz. Lautstärke gehalten auf 0,09–0,13 dB. |
| sub | [0.0 .. 1.0] | 1.0 | none 0.0 · half 0.5 · full 1.0 | Das Gewicht des subharmonischen Sinus (eine Oktave tiefer) gegen den gefilterten Sägezahn. Der Klangschwerpunkt fällt von 1899 Hz auf 1532 Hz bei 220 Hz, während mehr Bass-Fundament dazukommt. Lautstärke gehalten auf 0,02–0,10 dB. Zwischen den Enden liegt der Subton nahe genug an den Sägezahn-Harmonischen, dass kurz ein Amplituden-Beat statt eines glatten Übergangs zu hören ist (Tiefe 0,209 bei sub 0,25, 220 Hz) — reale Physik einer zusätzlichen kohärenten Tiefpartie, kein Fehler. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `supersaw`
*Surface forms: supersaw, super saw, detuned, unison, detune, hypersaw, saw stack, jp8000, trance lead, unisono*

Der Trance- und Rave-Lead: ein Stapel aus sieben leicht gegeneinander verstimmten Sägezahnwellen, deren Schwebung selbst der Klang ist.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| spread | [0.0 .. 2.0] | 1.0 | unison 0.0 · stack 1.0 · wide 2.0 | Skaliert alle sechs Verstimmungs-Offsets gemeinsam (die siebte Stimme bleibt mittig). Bei 0 liegen alle sieben Sägezähne exakt auf einer Frequenz und Phase und summieren sich kohärent zu 7-facher statt der ~2,6-fachen (√7) Amplitude einer inkohärenten Streuung — das ist deutlich lauter als der Rest der Achse. Bei 2 stehen die äußeren Stimmen doppelt so weit auseinander wie im Standard. Eine Kompensation hält den größten Teil dieser Lautstärke-Sprünge flach, **aber ein offener Defekt bleibt:** bei 55 Hz liegt die gemessene Streuung über die Anker noch bei 1,03 dB (Grenze 1,0 dB) — Ursache ist die Schwebungsphase beim Notenstart bei einem Verstimmungspaar, das dort langsamer schwebt als eine Note lang ist, nicht die Kompensation selbst. Über 440 Hz aufwärts ist die Schwebung schnell genug, um sich innerhalb einer Note auszumitteln, dort hält die Korrektur zuverlässig. |

**Stand:** offener Defekt: die `spread`-Achse spreizt 1,03 dB bei 55 Hz (Grenze 1,0) · Referenzvergleich: offen

### `sync`
*Surface forms: sync, hard sync, hardsync, sync sweep, synchronisation, oszillatorsync, sync lead, oscillator sync, syncsweep, tearing sweep*

Der klassische reißende Sync-Lead: eine Slave-Sägezahnwelle, die vom Master bei jedem Zyklus neu gestartet wird.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| ratio | [1.5 .. 5.0] | 2.6 | near 1.5 · centre 2.6 · far 5.0 | Das Slave-zu-Master-Frequenzverhältnis in der Mitte der Sweep-Bewegung. Höheres Verhältnis packt mehr Slave-Zyklen (mehr „Risse") in jeden Master-Zyklus: Klangschwerpunkt steigt von 9085 Hz auf 10207 Hz bei 220 Hz, während der Kammfilter-Kontrast bei 35–37 dB bleibt (bei jedem Verhältnis eine harte, aliasing-artige Kante). Lautstärke ist durch die Konstruktion fix (Peak-zu-Peak-Amplitude ändert sich mit dem Verhältnis nicht): 0,01–0,07 dB Streuung. |
| sweep | [0.0 .. 2.5] | 1.1 | frozen 0.0 · sweeping 1.1 · wide 2.5 | Die Tiefe der Sweep-Bewegung um das Zentrum. Bei 0 ist das Verhältnis eingefroren und der Riss bewegt sich nicht mehr (Klangfarben-Wanderung nur 1–8 Cent, unter der Bewegungsschwelle — eine bewusst stillstehende Einstellung). Oberhalb 0 schwingt der Riss wieder hörbar (bis 1610 Cent Wanderung am oberen Anker). Lautstärke bleibt konstant: 0,05–0,12 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `chiptune`
*Surface forms: chiptune, 8 bit, 8bit, nes, gameboy, chip tune, c64, sid, game boy, nintendo, retro game, videospiel, arcade*

Die NES-/Game-Boy-/C64-Stimme: die Pulsbreite springt zwischen drei festen Werten (12,5 %, 25 %, 50 %) statt zu gleiten — die harten Retro-Umschaltungen.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| speed | [0.15 .. 4.0] | 0.9 | slow 0.15 · steady 0.9 · fast 4.0 | Die Rate des Umschaltens zwischen den drei Duty-Zonen. Da jede Zone auf gleiche Leistung normalisiert ist, ändert sich hier nur, wie schnell zwischen ihnen gewechselt wird, nicht die Lautstärke pro Zone: Streuung 0,01 dB bei 220 Hz, 0,01–0,05 dB bei 110/440/880 Hz. |
| wide | [0.0 .. 1.0] | 1.0 | flat 0.0 · half 0.5 · stepped 1.0 | Wie weit die drei Duty-Zonen von 50 % abweichen — bei 1 (Standard) 12,5/25/50 %, bei 0 fallen alle drei auf 50 % zusammen und das Umschalten wird unhörbar. Der (leistungsgewichtete) Klangschwerpunkt steigt von 525 Hz auf 748 Hz bei 220 Hz, weil geradzahlige Harmonische zurückkehren, sobald der Duty von 50 % abweicht. Lautstärke 0,01 dB bei 220 Hz, 0,00–0,05 dB bei 110/440/880 Hz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `theremin`
*Surface forms: theremin, ondes martenot, ondes, musical saw, singende säge, heterodyne*

Die atmende, fast vokale Reinheit eines geschwungenen Heterodyn-Tons: ein fast reiner Ton, dessen ungerade Harmonische langsam unter einer Heterodyn-Nichtlinearität schwanken.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| purity | [0.0 .. 1.0] | 0.42 | plain 0.0 · default 0.42 · thick 1.0 | Die Mitte des Heterodyn-Index. Der Klangschwerpunkt verdoppelt sich über die ganze Achse bei jedem Register (bei 220 Hz: 258 → 361 → 552 Hz von plain über default zu thick), der Ton bewegt sich vom fast reinen Träger zu einem dickeren Heterodyn-Brummen. Lautstärke gehalten auf 0,15–0,38 dB (am schlechtesten bei 880 Hz). |
| breath | [0.2 .. 3.0] | 0.9 | slow 0.2 · default 0.9 · fast 3.0 | Die Rate des langsamen Klangfarben-Schwankens; dessen Tiefe bleibt unverändert. Lautstärke ist von der Rate praktisch unberührt (0,15–0,16 dB), aber der Kammfilter-Kontrast bewegt sich deutlich: bei 220 Hz von 108,3 dB (slow) über 99,5 dB (default) und 95,1 dB auf 88,8 dB (fast) — ein reeller Klangfarbenwechsel, während das Schwanken schneller wird. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `sub_sine`
*Surface forms: sub bass, subbass, sub, 808, sine bass, sub oscillator, subosc, tiefbass, unterbass*

Sub-Bass im 808-Stil: ein reiner Grundton mit einer Teiler-Oktave darunter.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| weight | [0.0 .. 1.0] | 0.249 | fundamental 0.0 · default 0.249 · sub 1.0 | Die Balance zwischen Grundton und Sub-Oktave als gleichleistungs-Überblendung. Der Klangschwerpunkt fällt bei 220 Hz von 220 Hz (reiner Grundton) auf 171 Hz (reine Sub-Oktave), während die Balance zur Sub-Oktave wandert. Da die zweite Harmonische der Sub-Oktave genau auf der Grundtonfrequenz liegt, entsteht in der Mitte der Achse eine Interferenz-Delle von bis zu 1,8 dB bei 220 Hz (0,6–1,1 dB bei anderen Registern); eine Teilkorrektur hält die Streuung auf 0,34–0,82 dB über 110/220/440/880 Hz (unkompensiert 1,06–1,81 dB). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `analog_osc`
*Surface forms: analog oscillator, analogue oscillator, analog osc, analogue osc, vco, minimoog, sub 37, analog synth, analogue synth, juno, prophet, oberheim, analogoszillator, analoger oszillator, vintage synth*

Analoger Oszillator im Stil von Minimoog/Juno/Prophet: die Wellenform morpht kontinuierlich von Dreieck über Sägezahn und Rechteck bis zum schmalen Puls.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| wave | [0.0 .. 1.0] | 0.45 | triangle 0.0 · saw 0.45 · square 0.55 · pulse 1.0 | Welche Wellenform der Oszillator fährt, als ein durchgehender Übergang durch vier Formen statt eines Schalters: hohles Dreieck, dann heller Sägezahn, dann hohles, reediges Rechteck, dann dünner, nasaler Puls. Klangschwerpunkt an den vier Ankern: 689, 2211, 5686, 4557 Hz, bei einer Lautstärke innerhalb von 0,01 dB. |
| drive | [0.0 .. 1.0] | 0.0 | clean 0.0 · warm 0.3 · hot 0.6 · screaming 1.0 | Gegenintuitiv: ein Softclipper rundet zuerst die scharfe Kante der Wellenform ab, darum DUNKELT drive einen Sägezahn zunächst ab, bevor er heller wird (Klangfarbe 1999 → 2056 → 3134 Hz über die Achse). Bei einem Dreieck verstärkt sich die dritte Harmonische um 4,8 dB, während der Klangschwerpunkt von 525 auf 343 Hz FÄLLT — mehr Kante, nicht mehr Helligkeit. Lautstärke gehalten innerhalb 0,2 dB an jedem Anker. |
| fat | [0.0 .. 1.0] | 0.0 | single 0.0 · subtle 0.15 · thick 0.5 · wide 1.0 | Wie viele Oszillatoren mitlaufen und wie weit sie auseinanderliegen. Ab 0 setzt ein zweiter Oszillator ein, der bis zu 1 % schärfer steht und langsam gegen den ersten wandert — das ist Ensemble-Breite, nicht Lautstärke: die Schwebung geht von nichts auf 3,2 Hz bei einer Tiefe von 0,27, die Lautstärke bewegt sich dabei um 0,22 dB. |
| age | [0.0 .. 1.0] | 0.35 | new 0.0 · worn 0.35 · old 0.8 | Wie abgenutzt das Gerät ist: Tonhöhe, Pegel und Wellenform driften alle zusammen von einer Achse aus skaliert. Bei 0 ein makellos stehender Oszillator, bei 1 einer, der nicht stillhält. Zwei der drei Instabilitäten laufen bei 0,043 und 0,057 Hz — innerhalb EINER gehaltenen Note liest sich das eher als feste Verstimmung denn als hörbares Wandern; über eine Phrase hinweg wird der Unterschied hörbar. |

**Stand:** offener Befund: rendert bei den Defaults bit-identisch zu `saw` · Referenzvergleich: offen

---

## Modulation und Waveshaping

### `ring_mod`
*Surface forms: ring mod, ring modulation, ringmod, amplitude modulation, ringmodulation, robot tone, roboterklang*

Ringmodulation / AM im Verhältnis 2:1 — ein hohler, ungeradzahlig-harmonischer Ton (rechteck-artig) mit driftender Dioden-Unwucht und Träger-Durchlass für die metallische Kante.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| leak | [0.0 .. 1.0] | 0.1 | pure 0.0 · default 0.1 · ring and carrier together 1.0 | Wie viel des unmodulierten Trägers unter dem ringmodulierten Produkt durchscheint. Bei 0 kein Träger, nur Summen-/Differenztöne (reine Ringmodulation); mehr leak lässt den Träger selbst zurück und der Ton nähert sich einer AM mit wieder hörbarer Original-Tonhöhe. Die Helligkeit FÄLLT dabei, weil der Träger unter seinen eigenen Seitenbändern liegt: Klangschwerpunkt bei 220 Hz 439 → 427 → 332 Hz über die drei Anker. Lautstärke ist bei 110/220/440/880/1760 Hz auf 0,05–0,20 dB gehalten; bei 55 Hz schwankt sie unabhängig von `leak` um bis zu 2,06 dB, weil dort ein Schwebe-Zyklus zwischen Träger und dem driftenden Modulator länger als eine Note dauert und die Phase beim Notenstart entscheidet, nicht die Achse selbst. |
| ratio | [1.0 .. 4.0] | 2.0 | unison 1.0 · default 2.0 · wide 4.0 | Das Frequenzverhältnis des Modulators zum Träger. Klangschwerpunkt bei 220 Hz läuft von 225 Hz (unison) über 427 Hz (Standard-Oktave) bis 807 Hz (4:1), nahezu proportional zum Verhältnis, und folgt der Tastatur (nicht ortsfest). Lautstärke praktisch unberührt: 0,03–0,09 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `cheby`
*Surface forms: waveshaper, wave shaper, chebyshev, waveshaping, verzerrer, wellenformer, shaper, transfer curve, distortion curve, verzerrung*

Chebyshev-Waveshaper — eine feste Transferkurve mit ungeraden Chebyshev-Termen bis zur 11., die exakt auf ungerade Harmonische abbilden: der kontrollierte Weg zu Drive, Dreck und Verzerrungsfarbe.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| drive | [0.1 .. 0.3] | 0.3 | clean 0.1 · mid 0.2 · hot 0.3 | Wie weit der Träger die Chebyshev-Kurve hinauffährt. Klangschwerpunkt bei 220 Hz von 363 Hz auf 575 Hz; die dritte Harmonische verstärkt sich von −10,6 dB auf −6,5 dB relativ zum stärksten Teilton, während die Kurve härter faltet. Eine gemeinsame Korrektur mit `wander` hält die Lautstärke auf 0,29–0,30 dB über 55–3520 Hz (unkompensiert wären es 3,57–3,58 dB an jedem Register). |
| wander | [0.0 .. 1.0] | 1.0 | static 0.0 · mid 0.5 · breathing 1.0 | Die Tiefe zweier LFOs, die den Drive-Pegel gemeinsam auf- und abschwanken lassen. Bei 0 steht die Kurve vollkommen still (keine Bewegung, Kohärenz 0 bei jedem Register) — die einzige Bewegungsquelle dieses Klangs. Klangschwerpunkt bei 220 Hz von 492 Hz (Mitte) auf 575 Hz (voll). Lautstärke wie bei `drive` auf 0,29–0,30 dB gehalten. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Additiv und FM

### `additive`
*Surface forms: additive, additiv, partials, harmonics only, obertöne, additive synthesis, harmonic stack, obertonstapel, additive synthese*

Additiver Obertonstapel — der Allzweckweg, um jede harmonische Instrumentenfarbe zu bauen: ein siebenharmonischer `gbuzz`-Stapel, verdoppelt durch einen um drei Cent versetzten Zwilling, der langsam gegen ihn schwebt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| bright | [0.35 .. 0.9] | 0.68 | dark 0.35 · warm 0.68 · bright 0.9 | Wie hell die beiden Stapel sind (gemeinsamer `gbuzz`-Rolloff). Klangschwerpunkt bei 220 Hz von 340 Hz (dumpf, gerundet) auf 798 Hz (hart, obertonreich). Lautstärke gehalten auf 0,03–0,09 dB über 110/220/440/880 Hz. |
| partials | [3 .. 16] | 7 | sparse 3 · default 7 · rich 16 | Die Anzahl der Harmonischen in beiden Stapeln, als physikalische Zählgröße (nicht 0..1). Klangschwerpunkt bei 220 Hz von 394 Hz (dünner Buzz) auf 672 Hz (dichter, voller Stapel). Lautstärke gehalten auf 0,02–0,20 dB über 110/220/440/880 Hz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `fm`
*Surface forms: fm, fm synthesis, 2 op fm, 2 operator fm, frequency modulation, operator, phase modulation, phasenmodulation, fm synthese*

Einfache 2-Operator-FM (Verhältnis 2, Index 1,5) — ein Träger und ein Modulator, Seitenbänder breiten sich mit steigendem Index aus: der neutrale FM-Ausgangspunkt und die Wurzel jedes DX-artigen Klangs.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| index | [0.0 .. 1.0] | 0.2 | mellow 0.13 · warm 0.27 · bright 0.6 · harsh 0.82 · screaming 1.0 | Der FM-Spitzenindex: wie viele Seitenbänder der Ton trägt. Die Klangfarbe hellt sich steil auf, während die Lautstärke durch eine Balance-Stufe konstant gehalten wird — es bewegt sich nur die Farbe, nie das Volumen. |
| ring | [0.0 .. 1.0] | 0.333 | quick 0.11 · medium 0.44 · long 0.89 | Wie lange der helle Anschlag nachklingt, bevor der Ton dunkler wird. Jede FM-Note beginnt hell und verliert ihre Höhen im Verlauf — nie ihre Lautstärke. Niedrig kollabiert fast sofort zu einem dunklen Körper; hoch bleibt bis tief in die Note hinein hell. |
| detune | [0.0 .. 1.0] | 0.6 | pure 0.0 · gentle 0.4 · wide 0.85 | Eine zweite Stimme, fest um einige Hertz über der ersten versetzt, sodass beide gegeneinander driften und der Ton schwebt. Bei 0 steht der Ton scheinbar still, bewegt sich aber messbar weiter (0,72 Oktaven Farbwanderung über eine 4-s-Note) — was hier verschwindet, ist die Amplitudenschwebung zwischen den Stimmen (Pegelschwankung 0,7 dB gegen 6,7 dB bei `gentle`), nicht jede Bewegung. Der Versatz ist in HERTZ fest, also bei jeder Tonhöhe gleich groß — kein Cent-Detune und kein Pitch-Vibrato. |

**Stand:** Glosse korrigiert 2026-07-25 (nur Text) · Referenzvergleich: offen

### `fm_bell`
*Surface forms: bell, glocke, dx, dx7, fm bell, bell tone, church bell, kirchenglocke, chime, chimes, carillon, tubular bell, hand bell, handglocke, glockenklang*

Glocke / Kirchenglocke / Chimes / Carillon: echte inharmonische Glockenpartialtöne (h 2,76/5,4/8,93), als 2-Operator-FM im Verhältnis 1,41 geschrieben, sodass die Partialtöne abseits des harmonischen Rasters liegen statt darauf projiziert zu werden.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| index | [0.0 .. 1.0] | 0.333 | mellow 0.13 · warm 0.27 · bright 0.6 · harsh 0.82 · screaming 1.0 | Wie bei `fm`: der FM-Spitzenindex bestimmt die Zahl der Seitenbänder und damit die Helligkeit, bei konstanter Lautstärke (Balance-Stufe). |
| ring | [0.0 .. 1.0] | 0.444 | quick 0.11 · medium 0.44 · long 0.89 | Wie lange der helle Anschlag nachklingt, bevor der Ton dunkler wird — niedrig kollabiert fast sofort, hoch bleibt bis tief in die Note hinein hell. |
| detune | [0.0 .. 1.0] | 0.55 | pure 0.0 · gentle 0.4 · wide 0.85 | Zweite Stimme, fest in Hertz versetzt. Bei 0 kein Amplituden-Beat mehr (Pegelschwankung 0,0 dB gegen 5,6 dB bei `gentle`), aber die Farbe wandert weiterhin messbar (1,56 Oktaven über eine 4-s-Note) — ein „stiller" Ton existiert auf dieser Plattform nicht. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `fm_ep`
*Surface forms: electric piano, epiano, rhodes, e piano, elektrisches piano, fm piano, fm-piano, fm e-piano, dx piano, piano, wurlitzer, wurli, tine piano, reed piano, e-piano, rhodes piano, elektrisches klavier, tine*

Elektrisches Piano / Rhodes / Wurlitzer / DX-Piano als STEHENDER Klangkörper (der Synth besorgt Hüllkurve und Ausklang): ein angeschlagener Zungen- oder Zinken-Körper mit metallischem Anschlag, der in den Korpus zurückklingt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| ting | [0.0 .. 1.0] | 0.55 | none 0.0 · soft 0.3 · classic 0.55 · clangy 1.0 | Das metallische „Ting" des Anschlags — bei 0 nur der hölzerne Korpus. Es verliert im Verlauf der Note seine Helligkeit, nie seine Lautstärke; ist das eine Merkmal, das ein E-Piano von einem reinen FM-Ton unterscheidet. |
| ring | [0.0 .. 1.0] | 0.25 | short 0.0 · medium 0.45 · long 1.0 | Wie lange der metallische Anteil nachklingt, bevor er sich in den Korpus einfügt — kurz ist ein Rhodes-artiger Zinken-Klick, lang ein Wurlitzer-artiges Zungen-Schwirren. Die Farbe wandert 211 Hz über die Note bei `short`, 1257 Hz bei `long`, bei praktisch einer Lautstärke (0,02 dB). |
| hollowness | [0.0 .. 1.0] | 0.75 | full 0.0 · hollow 0.45 · tine 0.75 · reed 1.0 | Der Charakter des Korpus, von voll bis hohl. Objektives Kennzeichen ist das Verhältnis ungerade/gerade Harmonische (+7,2 → +77,4 dB), der Klangschwerpunkt bewegt sich dabei nur um 53 Hz. Am oberen Ende fehlen die geraden Harmonischen exakt, nicht nur leise. Lautstärke 0,01 dB. |
| strike | [0.0 .. 1.0] | 0.64 | soft 0.0 · normal 0.64 · hard 1.0 | Wie hart der Hammer trifft — wie hell der Korpus STARTET, bevor er sich beruhigt. Formt den Ton, nicht die Lautstärke: 252 Hz Farbwanderung, 4,5 dB Kammfilter-Änderung, aber nur 0,06 dB Lautstärkeunterschied. Selbst ein weicher Anschlag startet heller, als er sich später einpendelt; ohne diese Grundhelligkeit (bei `ting` 0) bewegt sich der Klang in der Note gar nicht mehr. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `metallic_fm`
*Surface forms: metal, bell metal, metall, gong, tam tam, tamtam, anvil, amboss*

Gong / Tamtam / Amboss: echte inharmonische Metallpartialtöne (h 1,73/3,19/4,51), als 2-Operator-FM im Verhältnis 2,41 geschrieben. Für geschlagenes Metall, dessen Partialtöne eine Wäsche statt eines Akkords sind — nicht für die Klangschale (`singing_bowl`), die eine echte modale Konstruktion mit eigenem Erreger ist.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| index | [0.0 .. 1.0] | 0.533 | mellow 0.13 · warm 0.27 · bright 0.6 · harsh 0.82 · screaming 1.0 | Der FM-Spitzenindex: wie viele Seitenbänder, wie hell — bei konstanter Lautstärke (Balance-Stufe). |
| ring | [0.0 .. 1.0] | 0.556 | quick 0.11 · medium 0.44 · long 0.89 | Wie lange der helle Anschlag nachklingt, bevor der Ton dunkler wird. |
| detune | [0.0 .. 1.0] | 0.65 | pure 0.0 · gentle 0.4 · wide 0.85 | Zweite Stimme, fest in Hertz versetzt. Bei 0 kein Amplituden-Beat mehr (Pegelschwankung 0,0 dB gegen 3,0 dB bei `gentle`), die Farbe wandert aber weiterhin messbar (1,33 Oktaven über eine 4-s-Note). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Geschlagene Resonatorkörper

Alle zehn Einträge dieser Familie treiben eine Bank von `mode`-Resonatoren (oder FM-Partialtönen) mit einem kontinuierlichen Rausch- bzw. Anregungssignal — die gehaltene Note steht, weil sie so muss (der Host besitzt Hüllkurve und Note-off, siehe HANDOVER_LCO.md §1); kein Körper hier klingt selbst ab.

### `drum_head`
*Surface forms: drum head, drumhead, membrane, skin, Trommelfell*

Eine gespannte Membran, deren Partialtöne KEINE harmonische Reihe bilden — das macht eine Trommel zur Trommel. Acht Membranmoden unter kontinuierlichem Rauschen; erklärt `; MOVEMENT: TEXTURE` (Bewegung ist der Erreger-Jitter, keine feste Rate). Dies ist die STEHENDE Variante — dieselbe Membran, gestrichen/gerieben und kontinuierlich erregt statt geschlagen; jedes Wort für eine geschlagene Trommel (drum, tom, floor tom, timpani, kettledrum, taiko, frame drum, hand drum, Trommel, Pauke, conga, bongo, djembe, tabla) erreicht jetzt `tom`.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| pitched | [0.0 .. 1.0] | 0.25 | tom 0.0 · mixed 0.5 · timpani 1.0 | Wie eindeutig die Tonhöhe ist. Eine ideale gespannte Membran klingt bei Verhältnissen, die keine ganzzahligen Vielfachen sind — kein klarer Ton (Tom); eine Luftkammer unter dem Fell zieht diese Verhältnisse zu ganzen Zahlen — ein echter Ton entsteht (Timpani). Die gespielte Tonhöhe selbst ändert sich nie, nur ob die Trommel überhaupt eine HAT. |
| strikepos | [0.0 .. 1.0] | 0.35 | centre 0.0 · halfway 0.5 · rim 1.0 | Wo das Fell angeschlagen wird. Mitte weckt nur die gleichmäßig über das ganze Fell atmenden Moden (rund, tief); näher am Rand weckt die welligen Moden (dünner, härter, schärfer gefärbt). |
| tension | [0.0 .. 1.0] | 0.5 | slack 0.0 · normal 0.5 · tight 1.0 | Wie straff das Fell gespannt ist, hörbar als Verschiebung, WELCHE Partialtöne tragen, nicht als Tonhöhe. Locker lässt nur die tiefsten übrig (dumpf, schlaff); straff bringt die oberen hoch (hell, gespannt). |
| damping | [0.0 .. 1.0] | 0.35 | open 0.0 · damped 0.5 · muffled 1.0 | Wie frei das Fell klingen darf. Offen lässt schmale, klar singende Resonanzen; gedämpft verbreitert sie, bis die Trommel eher wie ein Thud mit Farbe klingt als wie ein Ton. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `tom`
*Surface forms: drum, tom, floor tom, timpani, kettledrum, taiko, frame drum, hand drum, Trommel, Pauke, conga, bongo, djembe, tabla*

Eine geschlagene Membran, die VON SELBST klingt und ausklingt — ein Schlag-Erreger, an die Uhr der Note (`knote`) gekoppelt, in dieselben acht idealen Membranmoden wie `drum_head`, die mit der eigenen Güte (Q) der Moden abklingen statt unter einer Hüllkurve, sodass die hohen Moden zuerst sterben, genau wie bei einem echten Fell. `drum_head` ist das stehende Gegenstück — dieselbe Membran, gestrichen statt geschlagen.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| ring | [0.0 .. 1.0] | 0.35 | muffled 0.0 · open 0.5 · timpani 1.0 | Wie lange das Fell nach dem Schlag nachklingt — eine echte Abklingzeit, gesetzt durch die Güte der Modenfilter (T60 = 2,2·Q/f): alle acht Moden teilen eine Güte, die höheren (gleiche Güte, höhere Mittenfrequenz) klingen schneller ab, das Fell verstummt von oben nach unten statt in einer Farbe zu verklingen. Gemessene T60 an den drei Ankern bei 110 Hz: 0,150/0,937/3,820 s gegen Ziel 0,15/1,0/4,0 s. Die Güte bestimmt auch, wie viel Energie des Schlags in die ERSTEN 100 MS fällt — der Lautheits-Bezug dieses geschlagenen Körpers ist das Anschlagfenster, nicht die ganze Note (eine 0,15-s-Tom und eine 4-s-Timpani können nicht auf dieselbe Ganznotenenergie summieren). Gehaltene Abweichung im Anschlagfenster 0,05–0,18 dB über alle sechs Register. |
| strike | [0.0 .. 1.0] | 0.3 | soft 0.0 · medium 0.5 · hard 1.0 | Wie hart und wie hart-spitzig der Schlägel ist — Länge und spektrale Neigung des Bursts zugleich. Kontaktabklingrate 150/s (weich) bis 2000/s (hart); ein weicher Schlag weckt nur die tiefen Moden, ein harter reißt die ganze Bank auf. Ein kürzerer, härterer Kontakt steckt trotz hellerer Färbung WENIGER seiner Energie in die ersten 100 ms (ein langsamerer Kontakt regt die Moden während des Anschlagfensters länger nach). Abweichung im Anschlagfenster 0,10–0,31 dB über alle sechs Register. |
| pitched | [0.0 .. 1.0] | 0.25 | tom 0.0 · mixed 0.5 · timpani 1.0 | Wie eindeutig die Tonhöhe ist — dieselben Modenverhältnisse und Modengewichte wie bei `drum_head`, unverändert; die gespielte Tonhöhe selbst ändert sich nie, nur ob die Trommel überhaupt eine HAT. Abweichung im Anschlagfenster 0,02–0,09 dB über alle sechs Register — die am engsten gehaltene der vier Achsen. |
| strikepos | [0.0 .. 1.0] | 0.5 | centre 0.0 · halfway 0.5 · rim 1.0 | Wo das Fell angeschlagen wird — dieselben Anregungsgewichte wie bei `drum_head`, unverändert. Mitte weckt nur die gleichmäßig atmenden Moden (rund, tief), der Rand die welligen (dünn, hart, schärfer gefärbt). Abweichung im Anschlagfenster 0,22–0,88 dB über alle sechs Register — die am wenigsten enge Achse, an 440/880 Hz nah an der 1,0-dB-Grenze. |

**Stand:** neu 2026-07-26, gemessen · Referenzvergleich: offen

### `struck_bar`
*Surface forms: music box, musicbox, glockenspiel, celesta, spieluhr, metallophone, tuned bar, stabspiel*

Gestimmter, geschlagener Metallstab — Glockenspiel/Spieluhr/Celesta/Kalimba: ideale frei-frei-Stab-Partialtöne (h 2,76/5,4/8,93/13,34), heller und dünner als eine große Glocke.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| strike | [0.0 .. 1.0] | 0.45 | at the middle 0.0 · off centre 0.25 · struck 0.45 · near the end 1.0 | Wie weit von der Mitte des Stabs geschlagen wird. In der Mitte dominiert fast nur der Grundton (rund, weich); zum Ende hin tragen die hohen Moden (hart, hell). Klangschwerpunkt 641 → 1789 Hz, bei 0,26 dB Lautstärke. Die stärkste Achse dieses Eintrags: ein an der Mitte geschlagener Music-Box-Klang und ein nahe am Ende geschlagenes Glockenspiel sind derselbe Stab. |
| ring | [0.0 .. 1.0] | 0.5 | damped 0.0 · short 0.25 · ringing 0.5 · left open 1.0 | Wie lange das Metall nachklingt. Gemessen ohne die Lautstärke-Bindung: T60 (Abklingzeit) läuft von 2,15 s (gedämpft) über 4,39 s (halb) bis über 36 s (frei ausklingend). Kammfilter-Kontrast steigt von 16,5 auf 35,5 dB, weil eine schmalere Resonanz weniger vom Rauscherreger durchlässt; der Klangschwerpunkt bewegt sich dabei nur um 90 Hz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `cymbal`
*Surface forms: cymbal, crash cymbal, ride cymbal, hi hat, hihat, becken, splash cymbal, china cymbal*

Becken/Crash/Ride/Hi-Hat: eine geschlagene Metallplatte ohne definierte Tonhöhe — eine dichte Bank hoher Q-Resonatoren, alle abseits des harmonischen Rasters, gestimmt nur durch Durchmesser und Dicke.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| strike | [0.0 .. 1.0] | 0.45 | on the bell 0.0 · half way out 0.25 · struck 0.45 · on the edge 1.0 | Wie weit von der Kuppe zum Rand geschlagen wird. Die Kuppe ist steif und spricht die tiefen Moden (niedriger, gongartiger Klang); der Rand ist frei und spricht die hohen (dünn, zischend). Klangschwerpunkt 1048 → 3021 Hz — die weiteste Farbwanderung jeder Achse in dieser Gruppe — bei 0,12 dB Lautstärke. |
| ring | [0.0 .. 1.0] | 0.5 | choked 0.0 · tight 0.25 · ringing 0.5 · let ring 1.0 | Wie lange das Becken nachklingt. T60 läuft von 0,46 s (abgewürgt) bis über 10 s (offen ausklingend). Bei so breiten Bändern (Q 65 bis 1040) ist das Becken fast Rauschen, und erst der Ausklang selbst unterscheidet die Ecken der Achse voneinander — weder Kammfilter noch Klangschwerpunkt tun das zuverlässig. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `glass`
*Surface forms: glass, glass pad, glas, glasklang, glass bell, digital glass, crystal pad, glass synth*

Das GESPIELTE, digitale Glas (D-50-Linie): sechs `mode`-Resonatoren bei Glas-Verhältnissen (1, 2,71, 3,83, 5,17, 6,61, 8,09) mit sehr hoher Güte, deren tiefste Mode genau auf der gespielten Note liegt. Für echtes, ungestimmtes Glasgeschirr siehe `struck_glass`.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| strike | [0.0 .. 1.0] | 0.45 | at the base 0.0 · on the wall 0.25 · struck 0.45 · at the rim 1.0 | Wie weit oben an der Glaswand angeschlagen wird. Nahe der Basis dominiert der Grundton (rein, rundlich, fast sinusartig); am Rand sprechen alle Moden (dünn, hell, komplex). Klangschwerpunkt 505 → 1329 Hz; der Kammfilter-Kontrast FÄLLT dabei (42,3 → 24,4 dB), weil sechs gleichzeitig sprechende Moden die Lücken zwischen sich füllen. Lautstärke 0,34 dB. |
| ring | [0.0 .. 1.0] | 0.5 | a hand on it 0.0 · short 0.25 · ringing 0.5 · let go 1.0 | Wie lange das Glas nachklingt. T60 läuft von 3,45 s (abgedämpft) bis über 56 s (frei). Kammfilter-Kontrast steigt 27,6 → 39,0 dB, der Klangschwerpunkt bewegt sich dabei nur um 60 Hz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `struck_glass`
*Surface forms: struck glass, glass bowl, glass jar, glass bottle, glass pane, broken glass, glasschale, glasscherben, hitting glass, glass shards*

Echtes geschlagenes Glas — Scheibe, Glas, Flasche, dicke Schale: ein dichter, heller, unregelmäßiger Klang OHNE Ton auf der gespielten Tonhöhe (die überlebenden Partialtöne sitzen bei x2,6081/3,4581/5,1317/9,5295/15,8527, nichts bei x1). Gegenstück zu `glass`, dem gestimmten Digital-Glas.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| strike | [0.0 .. 1.0] | 0.45 | flat on the pane 0.0 · off centre 0.25 · struck 0.45 · on the edge 1.0 | Wie weit von der Mitte der Scheibe geschlagen wird. Neigt die fünf Mode-Pegel geometrisch (Mode n skaliert mit kstk^n, kstk von 0,55 in der Mitte bis 1,55 am Rand), sodass zum Rand hin die hohen Moden gemeinsam statt einzeln hochkommen. Gemessen bei 220 Hz wandert der Klangschwerpunkt 642 → 723 → 842 → 1361 Hz über die vier Anker (Faktor 2,1), die Lautstärke folgt mit 0,14 dB (0,34 dB bei 55 Hz, 0,05 dB bei 1760 Hz) — eine Farbachse, gehalten durch die abschließende `balance`. Ändert auch, wie stark die Note NACHGIBT: flach geschlagen liegen die ersten 200 ms 3,12 dB über den letzten 500 ms, am Rand geschlagen nur 0,30 dB — ein dumpfes Klopfen stirbt in die Scheibe zurück, ein glitzernder Randschlag steht. |
| ring | [0.0 .. 1.0] | 0.5 | a hand flat on it 0.0 · short 0.25 · ringing 0.5 · let go 1.0 | Wie lange das Glas nachklingt. Skaliert die Güte (Q) aller Moden um denselben Faktor, 4^(2·ring−1) — ein Viertel der ausgelieferten Güte gedämpft, das Vierfache offen —, sodass die ganze Bank gemeinsam schärfer oder stumpfer wird und die eigenen unharmonischen Verhältnisse der Scheibe (x2,61/3,46/5,13/9,53/15,85) nie wandern. Gemessen bei 220 Hz steigt der Klangschwerpunkt nur 761 → 934 Hz über die vier Anker, weil die Güte die BREITE ändert, nicht die Lage; hörbar ändert sich das Nachgeben: gedämpft liegen die letzten 500 ms 1,12 dB ÜBER den ersten 200 ms (nichts bleibt zum Abklingen), am Default 1,35 dB darunter. Lautstärke-Spreizung über die Anker 0,30 dB bei 220 Hz, 0,25 dB bei 55 Hz, 0,04 dB bei 1760 Hz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `vibraphone`
*Surface forms: vibraphone, vibes, vibraharp, vibraphon*

Ein echter, geschlagener Aluminiumstab, der VON SELBST nachklingt und abklingt — Csounds eigenes `vibes`-Modell, bei jeder Note neu angeschlagen. Weich und rund statt scharf, gemessene Halbwertszeit 284–315 ms. Zwei strukturelle Grenzen des Modells: es bricht oberhalb von rund 4 kHz um etwa 10 dB ein, und seine Tonhöhe friert beim Anschlag ein (ein Glide wird innerhalb einer Note nicht mitverfolgt).

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| mallet | [0.6 .. 1.0] | 0.9 | soft 0.6 · default 0.9 · hard 1.0 | Die Schlägelhärte. Klangschwerpunkt bei 880 Hz von 904 Hz (weich) auf 962 Hz (hart) — innerhalb des freigegebenen Bereichs eine moderate aber reale Aufhellung. Lautstärke gehalten auf 0,03–0,22 dB über 55–1760 Hz (außerhalb dieses Spielbereichs, bei 3520 Hz, bricht das Modell selbst ein und keine Kompensation kann das auffangen). |
| strikepos | [0.3 .. 0.7] | 0.5 | default 0.5 · toward one edge 0.3 · toward the other edge 0.7 | Die Anschlagsposition auf dem Stab. Die Mitte spricht den Grundton am reinsten (Kammfilter-Kontrast ca. 134 dB bei 440 Hz); außermittig weckt die höheren Stabmoden (Kammfilter ca. 46 dB, Klangschwerpunkt von rund 490 auf über 650 Hz bei 440 Hz). |
| motor | [0.0 .. 1.0] | 0.01 | default 0.01 · half speed 0.5 · full 1.0 | Die Tiefe des rotierenden Tremolo-Diskus-Effekts; die Rate bleibt fest bei 6 Hz. Amplitudenmodulation bei 6 Hz steigt von ca. 0,001 (nahezu still) auf 0,032–0,033 (volle Pulsation) — der charakteristische Vibraphon-Puls. |

**Stand:** Umbau läuft — der ausgelieferte Körper ist ein abklingender Sinus (×4-Teilton 34 dB unter dem Grundton, Halbwertszeit 0,30 s) · Referenzvergleich: offen

### `handpan`
*Surface forms: handpan, hang drum, hand pan, pantam, handpan drum, hangdrum, cupola drum*

Geschlagenes Metall, das dennoch GESTIMMT ist — die einzige Ausnahme unter den Schlagkörpern dieser Bibliothek (die sonst alle konstruktionsbedingt inharmonisch sind). Hammergestimmt auf drei tiefste Moden im Verhältnis 1:2:3 (Grundton, Oktave, Duodezime); die Hülle koppelt weitere Klangfelder, sodass ein Anschlag die übrigen mitanregt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| sympathy | [0.0 .. 1.0] | 0.45 | single note 0.0 · quiet shell 0.25 · blooming 0.45 · whole shell 1.0 | Wie viel der Hülle mitantwortet. Gemessen an der Kopplung: das Klangfeld eine Oktave tiefer klingt in seiner zweiten Mode mit, das eine Duodezime tiefer in seiner dritten — beide genau auf der gespielten Tonhöhe. Kammfilter-Kontrast fällt von 48,2 dB (ein Feld) auf 19,4 dB (ganze Hülle), der Klangschwerpunkt bewegt sich dabei nur um 12 Hz, weil alle mitschwingenden Partner auf oder unter der Note liegen. Lautstärke 0,06 dB. |
| gu | [0.0 .. 1.0] | 0.4 | closed 0.0 · open 0.4 · wide open 1.0 | Wie weit die Bodenöffnung (das „Gu") geöffnet ist. Zwei Effekte zugleich: eine Helmholtz-Resonanz kommt hinzu (fest in Hertz, 88–122 Hz, transponiert nicht mit der Tastatur), und die Hülle strahlt insgesamt mehr ab und klingt dadurch weniger eng. Die Bewegung im Klang verdoppelt sich fast (135 → 212 Hz Farbwanderung in der Note), während Kammfilter (25,5 → 23,7 dB) und Klangschwerpunkt (8 Hz) kaum reagieren. Lautstärke 0,06 dB. |
| shimmer | [0.0 .. 1.0] | 0.35 | true 0.0 · hammered 0.35 · loose 1.0 | Wie weit die übrigen Klangfelder von ihrem reinen Verhältnis abweichen (reale Hammerstimmung ist nie exakt). Kammfilter-Kontrast 28,1 → 21,3 dB, Klangschwerpunkt unbewegt (0 Hz), Lautstärke 0,01 dB — die Hülle liest sich als gespreizter statt gestapelter Akkord. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `waterphone`
*Surface forms: waterphone, water phone, waterphon, wasserphon*

Eine Stahlschale mit ungleichen Bronzestäben am Rand und eingeschlossenem Wasser, gestrichen oder geschlagen. Die Stäbe sind Kragarme (Verhältnis 1:6,267:17,55:34,39) — gestimmt UND inharmonisch, eine Kombination, die weder `cymbal` (ungestimmt) noch `handpan` (konsonant hammergestimmt) hat.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| water | [0.0 .. 1.0] | 0.45 | shallow 0.0 · half full 0.45 · brimming 1.0 | Wie viel Wasser in der Schale ist. Bestimmt die TIEFE der Masselast-Gleitbewegung auf den drei Kragarm-Partialtönen, nicht ob sie stattfindet — selbst bei 0,0 bleibt eine Grundbewegung erhalten. Klangschwerpunkt bei 220 Hz fällt von 4373,0 Hz (0,0) über 3861,6 Hz (Standard) auf 3268,1 Hz (1,0); die Farbe wandert dabei 1579/1793/1960 Cent — mehr Wasser dunkelt den Durchschnittsklang ab UND vertieft die Gleitbewegung. Bei diesem Parameter überschreitet die gemessene Lautstärke-Streuung an mehreren Registern und Notenlängen die 0,5/1,0-dB-Grenze (bis 2,66 dB bei 55 Hz, 3 s Notenlänge); Ursache ist die freilaufende, nie zurückgesetzte `kslo`-LFO des Körpers. |
| bow | [0.0 .. 1.0] | 0.5 | light bow 0.0 · ordinary bow 0.5 · hard bow 1.0 | Wie hart der Stab gestrichen wird. Hebt Güte (220 auf 1120) und Pegel der drei oberen Partialtöne gemeinsam an — das liest sich als TEXTUR-Wechsel, nicht als Lautstärke: die Kohärenz der Schimmer-Bewegung steigt stetig von 0,165 über 0,559 auf 0,713, die Farbwanderung von 1315 auf 1442 auf 1887 Hz — ein weicher Bogen lässt den Klang wandern, ein harter lässt ihn in ein regelmäßiges Schimmern einrasten. Auch hier überschreitet die Lautstärke-Streuung an mehreren Registern die 0,5/1,0-dB-Grenze (bis 1,43 dB bei 220 Hz), aus demselben `kslo`-Grund. |

**Stand:** neu, noch nicht nach Gehör abgenommen · Referenzvergleich: offen

### `singing_bowl`
*Surface forms: tibetan singing bowl, himalayan singing bowl, singing bowls, meditation bowl, tibetische klangschale, himalaya klangschale, klangschalen*

Tibetische Klangschale — ein Schalenresonator, der geschlagen (Hammerschlag, rauscherregt wie üblich) oder gerieben wird (ein Puja-Stab, der um den Rand geführt eine Stick-Slip-Selbsterregung erzeugt, die zu einem stehenden Ton anwächst statt abzuklingen — Inácio, Henrique & Antunes 2006). Jede Schalenmode ist ein fast entartetes PAAR, durch die Wandasymmetrie leicht aufgespalten, was jede Schale von selbst langsam schweben lässt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| rub | [0.0 .. 1.0] | 0.3 | struck 0.0 · half rubbed 0.5 · rubbed 1.0 | Überblendet einen Rausch-Anschlag zu einem kontinuierlichen, selbsterhaltenden Ton auf der gespielten Tonhöhe. Zum geriebenen Ende hin konzentriert sich die Energie zunehmend auf den Grundton — das ist die physikalisch korrekte Konvergenz (die Originalstudie beschreibt den Dauerton-Zustand als von der ersten Schalenmode dominiert), keine Unzulänglichkeit. |
| beat | [0.0 .. 1.0] | 0.2 | still 0.0 · default 0.2 · wide 0.5 · widest 1.0 | Wie weit das entartete Modenpaar der zwei tiefsten Schalenmoden aufgespalten ist (0,05–2,5 % der Modenfrequenz). Bei 220 Hz läuft die gemessene Schwebungsrate von 1,33 Hz (Standard) bis rund 5,3 Hz (breitester Wert) bei 21–28 % AM-Tiefe. Die Lautstärke wandert dabei bewusst 11–20 dB INNERHALB einer gehaltenen Note — eine Schwebung ist per Konstruktion eine Lautstärkebewegung in der Note, kein Fehler. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `bonang`
*Surface forms: bonang, gong chime, kettle gong, Bonang, Kesselgong, Gamelan-Kesselgong*

Gamelan-Bonang — eine Reihe kleiner gestimmter Bronze-Kesselgongs. Feldgemessene Kesselpartialtöne 1:1,52:3,46:3,92 (kein Partialton nahe der Oktave), dazu „Ombak": Gamelan-Ensembles stimmen Instrumente bewusst in eng verstimmten PAAREN, damit das Ensemble hörbar schwebt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| strike | [0.0 .. 1.0] | 0.45 | near the boss 0.0 · default 0.45 · off the boss 1.0 | Neigt die vier Partialtöne geometrisch, genau wie bei `struck_bar`s `strike`. Klangschwerpunkt bewegt sich bei 220 Hz um 125 Hz, die Klangbewegung in der Note um 282 Hz, bei 0,03–0,23 dB Lautstärke. |
| ombak | [0.0 .. 1.0] | 0.7 | still 0.0 · half 0.5 · default 0.7 · widest 1.0 | Blendet eine zweite, verstimmte vollständige Kopie des Kessel-Spektrums ein (leistungserhaltend). Gemessene Schwebungsrate bei Standard (220 Hz) 6,33 Hz, Tiefe 0,18; Klangschwerpunkt bewegt sich um 36 Hz, die Klangbewegung in der Note um 91 Hz. Lautstärke 0,05–0,18 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Blasinstrumente und Rohrblatt

### `clarinet`
*Surface forms: clarinet, klarinette, hollow reed, klarinettenton, klarinettenklang, chalumeau, bass clarinet, bassklarinette, reed instrument, cylindrical reed, holzblasinstrument*

Klarinette / Chalumeau / Bassklarinette — ein zylindrisches Rohrblatt: nur ungerade Harmonische (rechteck-verwandt), hohl und holzig.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| breath | [0.0 .. 1.0] | 0.5 | subtone 0.0 · soft 0.25 · playing 0.5 · pushed 0.75 · full 1.0 | Wie hart das Rohrblatt angeblasen wird, als FM-Index unter einem langsamen Druckzyklus. Objektives Kennzeichen ist, was sich NICHT ändert: das Verhältnis ungerade/gerade Harmonische bleibt bei +101 bis +105 dB über die ganze Achse — die zylindrische Bohrung, das ganze Signum des Instruments, überlebt jede Einstellung. Klangschwerpunkt 549 → 628 Hz. Die Klangbewegung in der Note läuft dabei andersherum, 305 → 143 Hz: ein leise geblasenes Rohrblatt hat mehr Spielraum für den Druckzyklus, ein hart geblasenes ist schon offen. Lautstärke 0,07 dB (ohne Kompensation wären es 3,52 dB). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `flute`
*Surface forms: flute, flöte, whistle, querflöte, recorder, blockflöte, piccolo, pan flute, panflöte, shakuhachi, atemton*

Flöte / Blockflöte / Piccolo / Panflöte — eine geblasene Luftsäule: ein hohler Vierharmonischer-Stapel, dessen Rolloff auf einem Dreisekunden-Druckzyklus atmet.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| breath | [0.0 .. 1.0] | 0.5 | whisper 0.0 · gentle 0.3 · playing 0.5 · strong 0.75 · overblown 1.0 | Wie hart die Luftsäule angeblasen wird. Das Gegenteil der Klarinette, und genau deshalb gibt es beide: das Verhältnis ungerade/gerade Harmonische FÄLLT (+13,4 → +8,1 dB), weil eine offene Säule beim härteren Blasen ihre geraden Harmonischen anhebt. Klangschwerpunkt 280 → 334 Hz, die Klangbewegung in der Note mehr als verdoppelt sich (27 → 65 Hz). Kammfilter 0,2 dB, Lautstärke 0,01 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `brass`
*Surface forms: brass, trumpet, trompete, blech, blechbläser, horn, french horn, waldhorn, trombone, posaune, tuba, cornet, flugelhorn, flügelhorn, brass section, fanfare*

Blechbläser — Trompete/Horn/Posaune/Tuba/Blechbläsersatz: ein Zwölfharmonischer-Stapel, dessen Rolloff sich einmal im Anschlag von dunkel nach hell öffnet und dann hält, durch eine feste 1200-Hz-Glockenformante gefärbt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| press | [0.0 .. 1.0] | 0.5 | mellow 0.0 · warm 0.3 · playing 0.5 · forte 0.75 · blaring 1.0 | Der Lippendruck: wie hell der Ton NACH dem Anschlag hält (das einmalige Öffnen des Anschlags selbst bleibt unberührt). Die Serie reicht von 9 bis 12 hörbaren Partialtönen, der Klangschwerpunkt steigt 550 → 969 Hz — die breiteste Farbachse der Blasinstrumentenfamilie. Das Verhältnis ungerade/gerade fällt +4,7 → +1,9 dB, die Klangbewegung in der Note verdreifacht sich (118 → 372 Hz). Kammfilter 0,4 dB, Lautstärke 0,05 dB (unkompensiert 3,59 dB plus 2,7 dB Verlust über die gehaltene Note). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `free_reed`
*Surface forms: accordion, accordeon, akkordeon, harmonium, bandoneon, bandonion, concertina, melodica, squeezebox, free reed, musette, ziehharmonika, handharmonika, quetschkommode, shruti box*

Akkordeon / Harmonium / Bandoneon — eine freie Zunge: drei `gbuzz`-Stapel (einer auf Tonhöhe, zwei ±21/−16 Cent versetzt), die gegeneinander schweben.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| musette | [0.0 .. 1.0] | 0.55 | single 0.0 · slight 0.25 · musette 0.6 · wet 1.0 | Eine Zunge oder drei. Ab 0 schweben zwei zusätzliche Zungen (+21/−16 Cent) hörbar gegen die erste (Tiefe 0,01 → 0,29 bei 2,3 Hz). Ein Amplituden-Beat, darum für Klangfarbenmesser fast unsichtbar (Klangschwerpunkt bewegt sich nur 1796 → 1434 Hz), aber deutlich hörbar als Schwebung. |
| press | [0.0 .. 1.0] | 0.45 | soft 0.1 · gentle 0.3 · full 0.55 · hard 0.8 · forced 1.0 | Wie hart der Blasebalg auf die Zunge drückt. Vertieft den harmonischen Kamm (44,0 → 49,0 dB) statt nur aufzuhellen: Klangschwerpunkt 1513 → 1702 Hz. Lautstärke konstant auf 0,02 dB. |
| rattle | [0.0 .. 1.0] | 0.3 | clean 0.0 · breathy 0.25 · rattly 0.55 · wheezing 1.0 | Das mechanische Eigengeräusch der Zunge — ein Luftstoß pro Halbzyklus durch eine 2600-Hz-Resonanz, mit dem Blasebalg an- und abschwellend. Die breiteste der drei Achsen: Klangschwerpunkt 585 → 2435 Hz, Kammfilter 96,2 → 35,4 dB, Lautstärke konstant auf 0,02 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `harmonica`
*Surface forms: harmonica, mouth organ, blues harp, mundharmonika, chromatic harmonica, diatonic harmonica*

Mundharmonika / Blues Harp — eine freie Zunge, gespielt in geschlossenen Händen; die Hände sind der Punkt. Eine Handhöhlen-Resonanz sitzt FEST in Hertz (380–1830 Hz), weil eine Höhle eine Höhle bleibt, egal welche Note gespielt wird.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| cup | [0.0 .. 1.0] | 0.4 | open 0.0 · half 0.4 · cupped 0.7 · muted 1.0 | Die Hand über der Muschel geschlossen oder geöffnet: eine Höhlenresonanz von 380 Hz (geschlossen, dunkel, gedämpft) bis 1830 Hz (offen, hell), mit einem 5,1-Hz-Handtremolo durchgehend. Fest in Hertz, färbt also bei jeder Tonhöhe dasselbe Band. Klangschwerpunkt 1700 → 1922 Hz, Kammfilter 41,2 → 37,6 dB, Lautstärke konstant auf 0,00 dB. |
| press | [0.0 .. 1.0] | 0.5 | soft 0.15 · normal 0.45 · hard 0.75 · overblown 1.0 | Wie hart der Atem die Zunge treibt. Vertieft den harmonischen Kamm (38,9 → 42,7 dB) bei fast unverändertem Klangschwerpunkt — reediger, nicht einfach heller. Lautstärke konstant auf 0,01 dB. |
| breath | [0.0 .. 1.0] | 0.35 | clean 0.0 · airy 0.3 · breathy 0.6 · gasping 1.0 | Luft am Rohrblatt vorbei statt hindurch. Die breiteste Achse: Klangschwerpunkt 600 → 2227 Hz, Kammfilter 70,0 → 31,7 dB. Sie hellt sich MIT dem Atemzyklus auf und bewegt sich daher mit der Note statt als feststehendes Rauschen zu wirken. Lautstärke konstant auf 0,00 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `bagpipe`
*Surface forms: bagpipe, bagpipes, highland pipes, uilleann pipes, dudelsack, gaida, zampogna, cornemuse, chanter, drone pipe, sackpfeife*

Dudelsack — ein Rohrblatt-Chanter plus DRONES: ein `gbuzz`-Chanter auf Tonhöhe, ein Tenor-Drone eine Oktave und ein Bass-Drone zwei Oktaven tiefer, alle gegeneinander verstimmt und schwebend.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| drone | [0.0 .. 1.0] | 0.6 | chanter 0.0 · one 0.35 · full 0.7 · droning 1.0 | Die parallelen Pfeifen neben dem Chanter, eine und zwei Oktaven tiefer. Schwebungstiefe 0,00 → 0,21. Klangschwerpunkt 876 → 500 Hz, während die tiefen Pfeifen einsetzen — und was sich hier wirklich bewegt, ist die klingende Grundtonhöhe selbst: f0 liest 220,0 Hz ohne Drones, 110,0 Hz auf halbem Weg, 55,0 Hz mit vollen Drones. Lautstärke konstant auf 0,10 dB. |
| reed | [0.0 .. 1.0] | 0.55 | soft 0.15 · steady 0.45 · strong 0.75 · screaming 1.0 | Wie hart der Blasebalgdruck das Rohrblatt treibt — die Helligkeitsachse: Klangschwerpunkt 353 → 1026 Hz. Lautstärke konstant auf 0,01 dB. |
| beat | [0.0 .. 1.0] | 0.45 | tuned 0.0 · close 0.3 · loose 0.6 · sour 1.0 | Wie weit die Drones verstimmt sind — eine RATEN-Achse, keine Tiefen-Achse: die Schwebungstiefe bleibt bei ~0,20, während die Rate von 1,0 über 1,7/2,3/3,3 auf 4,0 Hz steigt. Bewegt darum fast keine Klangfarbe (Klangschwerpunkt 576 → 577 Hz) und ist für Spektralmesser praktisch unsichtbar — hörbar als Schwebungsrate. Lautstärke 0,10 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `sax`
*Surface forms: saxophone, sax, alto sax, tenor sax, soprano sax, baritone sax, saxofon, saxophon, altsaxophon, tenorsaxophon*

Saxophon — ein einzelnes schlagendes Rohrblatt auf einer KONISCHEN Bohrung (der Unterschied zur Klarinette: ein Konus behält alle Harmonischen, ein Zylinder nur die ungeraden). Zwei feste `reson`-Formanten bei 900 und 1750 Hz geben den „Shout".

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| honk | [0.0 .. 1.0] | 0.5 | veiled 0.0 · round 0.3 · vocal 0.6 · shouting 1.0 | Wie stark der konische Bohrungs-Shout betont wird — eine Überblendung auf die beiden festen Formanten. Der Klangschwerpunkt FÄLLT dabei monoton, 1491 → 1202 Hz: die Formanten bündeln den Ton zur Mitte hin, statt ihn aufzuhellen. Lautstärke konstant auf 0,02 dB. |
| blow | [0.0 .. 1.0] | 0.45 | breathy 0.1 · soft 0.3 · full 0.55 · loud 0.8 · honking 1.0 | Wie hart das Rohrblatt getrieben wird. Vertieft den harmonischen Kamm (39,7 → 43,1 dB) und mehr als verdreifacht die Klangbewegung in der Note (115 → 373 Hz), bei fast konstantem Klangschwerpunkt über weite Strecken (1341 → 1417 Hz) — die beiden Bohrungsformanten halten die Farbe, während die Serie dahinter sich füllt. Lautstärke konstant auf 0,00 dB. |
| growl | [0.0 .. 1.0] | 0.15 | clean 0.0 · edge 0.15 · growl 0.45 · roar 1.0 | Das Summen des Spielers gegen das Rohrblatt, als Amplitudenmodulation bei 74 Hz (Seitenbänder, kein Vibrato). Der harmonische Kamm füllt sich von 48,7 auf 32,0 dB, während Seitenbänder zwischen die Partialtöne rutschen — genau das macht aus Ton Rasp. Jenseits der Achsenmitte übernimmt das Summen die klingende Grundtonhöhe selbst (f0 73,4 Hz bei gespielten 220 Hz). Lautstärke konstant auf 0,00 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `double_reed`
*Surface forms: oboe, bassoon, cor anglais, english horn, double reed, shawm, shehnai, shenai, suona, duduk, crumhorn, krummhorn, oboe d'amore, fagott, englischhorn, schalmei, zurna*

Oboe / Fagott / Schalmei — zwei Rohrblätter, die gegeneinander schlagen: die schmalste, dringlichste Art, ein Rohrblatt klingen zu lassen. Eine schmale `reson`-Höhlenformante wandert fest in Hertz von 480 Hz (fagottartig tief) bis 1580 Hz (oboenartig durchdringend).

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| throat | [0.0 .. 1.0] | 0.55 | bassoon 0.0 · cor anglais 0.35 · oboe 0.7 · shawm 1.0 | Welches Doppelrohrblatt es ist: die Bohrungsformante von 480 Hz (Fagott) bis 1580 Hz (Oboe), plus eine schwächere zweite bei deren 2,15-Fachem. Klangschwerpunkt 544 → 718 Hz, monoton. Die Formante wird relativ zur Serie SCHWÄCHER, je höher sie liegt (36 % auf 2 % Energieanteil) — reale Physik, kein Fehler. Lautstärke 0,01 dB. |
| press | [0.0 .. 1.0] | 0.5 | soft 0.15 · gentle 0.35 · full 0.6 · pressed 0.85 · forced 1.0 | Wie hart die beiden Blätter getrieben werden — die breiteste Achse: Klangschwerpunkt 472 → 1218 Hz, Klangbewegung in der Note steigt von 37 auf 172 Hz. Lautstärke konstant auf 0,00 dB. |
| bite | [0.0 .. 1.0] | 0.45 | loose 0.0 · normal 0.4 · pinched 0.7 · biting 1.0 | Wie eng die Blätter die Luft einklemmen — die Überblendung auf die Höhlenformanten. Das 400–2000-Hz-Band steigt von 41 % auf 55 % Energieanteil, monoton; der Klangschwerpunkt taucht ab und kehrt zurück (717 → 671 → 690 Hz), weil die Formante Energie von beiden Enden nach innen zieht statt nur nach oben. Lautstärke konstant auf 0,02 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `didgeridoo`
*Surface forms: didgeridoo, didjeridu, yidaki, mago, didge, didgeridu, aboriginal drone*

Ein lippengetriebener Holzdrone: exakt harmonisch, obwohl die Bohrung ein ausgehöhlter Ast ohne bestimmte Form ist — die periodische Lippen-Schließung, nicht die Bohrung, setzt die Serie. Was ein Spieler tatsächlich formt, ist die Impedanz des Ansatzrohrs.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| voice | [0.0 .. 1.0] | 0.3 | no voice 0.0 · a hum 0.3 · growling 0.65 · roaring 1.0 | Ein zweiter, ins Rohr gesummter Ton — multiplikativ, erzeugt also Seitenbänder statt sich nur aufzulagern. Kammfilter-Kontrast 70,4 → 16,9 dB (untere Hälfte der Achse), Klangschwerpunkt 1068 → 2523 Hz — die breiteste der drei Achsen. Das Summen liegt fest bei 97 Hz, unabhängig von der gespielten Note. Lautstärke 0,00 dB. |
| tongue | [0.0 .. 1.0] | 0.5 | closed 0.0 · low 0.3 · open 0.6 · wide open 1.0 | Wohin die Zunge die Ansatzrohr-Resonanz legt, von geschlossen-dunkel bis offen-hell. Das offene Band wandert 1150 → 2150 Hz, die beiden Sperrbänder folgen mit; der Klangschwerpunkt bewegt sich dabei deutlich kleiner (1630 → 1845 Hz), weil eine wandernde Formante eher einen Teil der Serie gegen einen anderen tauscht als etwas hinzuzufügen. Kammfilter 0,1 dB, Lautstärke 0,02 dB. |
| breath | [0.0 .. 1.0] | 0.4 | steady 0.0 · breathing 0.4 · pulsing 0.7 · surging 1.0 | Zirkularatmung, als Druck- statt Lautstärkepuls. Während die Wangen drücken, fällt der Lippendruck, die Serie verkürzt sich und der Ton dumpft ein- bis zweimal pro Sekunde — der Klang hört nie auf, was der ganze Sinn der Technik ist. Bewegt die Klangbewegung in der Note von 91 auf 481 Hz (Faktor fünf), bei 0,00 dB Lautstärke. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `ocarina`
*Surface forms: ocarina, vessel flute, gemshorn, okarina, clay flute, xun, helmholtz flute*

KEINE kleine Flöte: eine Gefäßflöte ist ein Helmholtz-Resonator mit genau EINER Resonanz, kann also nicht überblasen — ein fast reiner Sinus mit einem schwachen zweiten Teilton.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| breath | [0.0 .. 1.0] | 0.45 | soft 0.0 · blown 0.45 · hard 1.0 | Wie hart geblasen wird. Da es keine zweite Resonanz gibt, in die der Ton springen könnte, wird härteres Blasen nicht eine Oktave höher, sondern kantiger: der zweite Teilton wächst von 0,061 auf 0,289 des Grundtons (−24,3 auf −10,8 dB) und die Blasgeräusch-Kante rückt hoch. |
| chiff | [0.0 .. 1.0] | 0.4 | clean 0.0 · breathy 0.4 · airy 1.0 | Wie viel Wind im Ton liegt. Das Blasgeräusch wird von derselben einzigen Resonanz geformt wie der Ton selbst. Ein Rest Windanteil bleibt bei jeder Einstellung (eine Gefäßflöte ist nie ganz still), und eine Obergrenze verhindert, dass der Wind die Tonhöhe verdeckt (Zischen bleibt unter rund einem Drittel des Tons). |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Gestrichen und Saite

### `strings`
*Surface forms: strings, streicher, ensemble strings, string section, string ensemble, orchestral strings, streichorchester, streichquartett, string quartet, streichersatz, celli, massed strings, violins, violinen, geigen, violin section, cello section, erste geigen*

Streicher-SEKTION — Streichorchester, Ensemble, massierte Spieler (EIN gestrichenes Instrument ist `string`): drei Spieler zugleich, jeder mit eigener langsamer Intonationsdrift und eigenem Bogendruckzyklus, gegeneinander verstimmt; ein helles Sägezahn-Spektrum mit sanft atmender Harmonischenzahl.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| desk | [0.0 .. 2.0] | 1.0 | solo 0.0 · section 1.0 · wide 2.0 | Skaliert die Verstimmung der drei Spieler gegeneinander: bei 0 fallen sie auf einen einzigen Ton zusammen (ein Solo, lauter und kohärent), bei 2 fächern sie zu einem breiten, hörbar schwebenden Satz auf. Gemessene Schwebung bei 220 Hz: 0,8 Hz/Tiefe 0,010 (solo) → 0,8 Hz/0,462 (Satz) → 2,0 Hz/0,493 (breit). Die Lautstärkekorrektur hält 0,11/0,34/0,34/0,12 dB bei 110/220/440/880 Hz; darunter entfernt ein zweiter Term 2,2–3,8 dB zwischen 20,6 und 73,4 Hz. |
| bow | [0.02 .. 0.62] | 0.62 | soft 0.02 · hard 0.62 | Wie viele Harmonische unter dem Bogendruck-Tonfilter durchkommen: unten weich, dunkel, flautando; am Default (0,62) hart, hell, mit dem Kolophonium-Kratzen. Klangschwerpunkt wandert je nach Register unterschiedlich weit: bei 220 Hz 1338 → 2418 Hz, bei 880 Hz 3871 → 5866 Hz, bei 1760 Hz 5861 → 7636 Hz. Gemessene Spreizung 0,28 dB bei 220 Hz. |

**Stand:** überarbeitet 2026-07-25, gemessen; zusätzlich offener Befund: die `desk`-Achse spreizt 1,05 dB bei 110 Hz und 1,09 dB bei 1760 Hz · Referenzvergleich: offen

### `string`
*Surface forms: string, saite, plucked string, gezupfte saite, bowed string, gestrichene saite, guitar, gitarre, acoustic guitar, akustikgitarre, nylon string, steel string, harp, harfe, koto, sitar, banjo, mandolin, mandoline, lute, laute, zither, pizzicato, pizz, geige, fiddle, violin, violine, cello, violoncello, double bass, kontrabass, upright bass*

EINE echte Saite, angeschlagen und dann klingen gelassen — nicht die Ensemble-Maschine (`strings`, verstimmte Sägezähne) und nicht das gezupfte Spektrum ohne den Anschlag (`harpsichord`, gbuzz-Chöre). Ein auf die gespielte Tonhöhe gestimmter Kammresonator, kontinuierlich gespeist, damit eine gehaltene Note steht.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| bow | [0.0 .. 1.0] | 0.35 | plucked 0.0 · mixed 0.5 · bowed 1.0 | Wie die Saite in Gang gesetzt wird — der ganze Unterschied zwischen Gitarre und Cello. Ein Plektrum lässt sofort los, alles wird auf einmal geweckt und der Klang fällt danach ab; ein Bogen lässt nie los, er speist ständig nach, die Saite steht und atmet. Klangschwerpunkt wandert 4812 → 3463 Hz, der Kammfilter-Kontrast bleibt fast gleich (32,9 → 34,1 dB). Lautstärke 0,05 dB. |
| pick | [0.0 .. 1.0] | 0.3 | bridge 0.0 · normal 0.35 · middle 1.0 | Wo entlang der Länge die Saite angeregt wird — Geometrie, keine Klangfarbenregelung: an dem Punkt fehlt jede Harmonische, deren Schwingungsknoten genau dort liegt. Am Steg dünn, hart, nasal; in der Mitte fallen alle geraden Harmonischen zugleich weg, hohl und holzig. Gemessen in der Mitte liegen die Harmonischen 2, 4, 6, 8, 10 um 37 bis 43 dB unter dem Grundton — 30 bis 45 dB tiefer als am Steg. Bewegt 333 Hz Klangfarbe bei 0,00 dB Lautstärke. |
| damp | [0.0 .. 1.0] | 0.35 | open 0.0 · damped 0.5 · muted 1.0 | Wie frei die Saite klingen darf, wie eine flach aufgelegte Hand. Offen sind die Resonanzen schmal und die Saite singt klar; gestoppt verbreitern sie sich und die Saite liest sich als Klopfen mit Farbe statt als Tonhöhe — sie hört aber nie ganz auf zu klingen. Abstand zwischen den Harmonischen und dem Rest: 37,6 dB offen, 29,7 dB gestoppt. Klangschwerpunkt folgt leicht mit (3972 → 4314 Hz), Lautstärke 0,02 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `tanpura`
*Surface forms: tanpura, tambura, tampura, jivari, jvari, indian drone, raga drone, tanpura drone*

Die einzige Saite in dieser Bibliothek, die NACH dem Anschlag heller wird: ein Baumwollfaden (jivari) zwischen Saite und Steg lässt sie während jedes Zyklus auf- und abrollen, was Obertöne einspeist, die im reinen Zupfton nicht da waren — der Halo, der eine Tanpura unter eine Raga legt statt sie zu spielen. Die Tonhöhe hält über sechs Oktaven auf −0,1 bis +2,5 Cent genau.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| jvari | [0.0 .. 1.0] | 0.5 | barely touching 0.0 · set 0.5 · pushed hard 1.0 | Wie weit der Faden unter die Saite geschoben ist. Mehr Obertonreihe leuchtet gleichzeitig auf (kmul 0,30 → 0,82). Der Kammfilter-Kontrast steigt dabei NICHT einfach: gemessen 48,1 / 44,2 / 45,8 dB an den drei Ankern (fällt erst, steigt dann), weil eine vollere Serie auch die Täler des Fensters füllt. Klangschwerpunkt 1519 → 2210 Hz bei 0,00 dB Lautstärke. Bei 0 bleibt ein Rest-Halo bestehen — eine Tanpura mit ganz zurückgenommenem Faden ist ein anderes Instrument (`string`). |
| bloom | [0.0 .. 1.0] | 0.45 | close 0.0 · blooming 0.45 · high 1.0 | Wie weit hinauf in der Serie der Halo wandert. Sein Pegel bleibt dabei exakt gleich — gemessen an sieben Harmonischen (1, 2, 4, 8, 12, 17, 21) übereinstimmend bei −15,39 dB —, während der Klangschwerpunkt von 508 auf 4918 Hz läuft. Klangschwerpunkt der Note 1448 → 2397 Hz, Klangbewegung 384 → 1343 Hz, bei 0,00 dB Lautstärke. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `hurdy_gurdy`
*Surface forms: hurdy gurdy, hurdy-gurdy, hurdygurdy, drehleier, vielle a roue, vielle, chien, wheel fiddle*

Eine von einem RAD gestrichene Drone, die nie für einen Bogenwechsel aussetzt, mit eingebautem Schlagzeug: der Chien, ein loser Steg unter einer Saite, nur von einem Fadendreh gehalten. Beschleunigt die Kurbel, hebt der Zugimpuls der Saite ihn ab, er schlägt zurück — ans RAD gekoppelt, nicht an die Note.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| chien | [0.0 .. 1.0] | 0.45 | silent dog 0.0 · buzzing 0.45 · hard coup 1.0 | Wie hart der schnarrende Steg arbeitet — der Fadendreh. Setzt die Schlagrate von gut zwei pro Sekunde auf fast acht und wie viel vom Klang der Schlag ausmacht. Die Hüllkurve jedes Schlags ist gegen die PHASE geschrieben, nicht gegen die Zeit: Dauer und Energie pro Schlag skalieren mit 1/Rate, die Energie pro Sekunde bleibt unverändert. Verschiebt die lauteste Periodizität von 1,3 Hz (Rad) auf 7,7 Hz (Schlag); bleibt absichtlich unter dem Pegel der Drone. |
| press | [0.0 .. 1.0] | 0.5 | light 0.0 · loaded 0.5 · leaning in 1.0 | Wie stark das Rad gegen die Saiten drückt — zündet mehr Obertonreihe, genau wie Bogendruck bei einer Saite, ohne Lautstärkewanderung. Die Kurbel erreicht mehr als nur den Ausgang: das Rad ist nicht perfekt rund, das Kolophonium greift ungleichmäßig, die Helligkeit der Drone atmet im Kurbeltakt mit — bei `chien` 0 war das die einzige Bewegung im Klang. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Tasteninstrumente

### `organ`
*Surface forms: organ, orgel, drawbar, hammond, zugriegel, church organ, kirchenorgel, pipe organ, pfeifenorgel, tonewheel, b3, hammond organ, orgelklang*

Pfeifen-/Zugriegelorgel — Hammond, Tonewheel, Kirchenorgel: drei Ränge in typischer Registrierung, ein 8'-Prinzipal, eine 4'-Oktave und eine 5⅓'-Quinte, jede ein eigener `gbuzz`-Stapel mit eigener Drift-Rate.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| regist | [0.0 .. 1.0] | 0.5 | principal 0.0 · gentle 0.3 · plenum 0.5 · full 0.75 · mixture 1.0 | Die Registrierung: 8'-Prinzipal allein, oder mit 4'-Oktave und 5⅓'-Quinte dazugezogen. Ihr Kennzeichen ist nicht in erster Linie der Klangschwerpunkt (620 → 734 Hz), sondern WELCHEN Ton die Pfeifen zusammen ergeben: die Quinte klingt eine Quinte über dem Prinzipal, und 220/330 Hz haben keinen gemeinsamen Grundton oberhalb von 110 — die klingende Grundtonhöhe fällt daher eine Oktave, sobald die Quinte gezogen ist (f0 220,1 → 110,1 Hz), der historische Orgelbau-Trick für einen vorgetäuschten 16'-Rang. Kammfilter 24,6 dB (Prinzipal allein) bis 67,5–69,6 dB (obere Ränge dazu) — tiefer, nicht flacher, weil 220 und 330 beide Harmonische von 110 sind. Verhältnis ungerade/gerade kippt von +2,8 auf −7,0 dB. Lautstärke 0,02 dB, leistungserhaltend umverteilt (echtes Registerziehen würde sonst lauter). |

**Stand:** Glosse korrigiert 2026-07-25 (nur Text) · Referenzvergleich: offen

### `rhodes`
*Surface forms: rhodes, fender rhodes*

Ein echtes Rhodes-Zungenklavier, das VON SELBST klingt und ausklingt — Csounds eigenes `fmrhode`-Modell, bei jeder Note neu angeschlagen: ein glockenartiger metallischer Anschlag über hölzernem Korpus, die Note stirbt unabhängig davon, ob die Taste gehalten wird. Für die stehende Variante (Hüllkurve übernimmt der Synth) siehe `fm_ep`.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| bark | [0.0 .. 1.0] | 1.0 | mellow 0.0 · default 1.0 | Bewegt `fmrhode`s eigenen Modulationsindex und die Ausgangs-Überblendung gemeinsam. Kammfilter-Kontrast fällt von rund 90 dB bei 220 Hz auf rund 18 dB — dieselbe Form bei jedem Register (78,6→18,3 bei 110 Hz, 102,9→18,0 bei 440 Hz, 114,7→18,4 bei 880 Hz): vom reinen, klaren Ton zum heutigen härteren Bellen. Lautstärke über die ganze Note 0,14–0,15 dB, in den ersten 400 ms 0,76–0,78 dB. Eine quadratische Korrektur, die bis 2026-07-25 lief, erwies sich selbst als Defekt (hielt nur ein spätes 0,5–3,5-s-Fenster auf 0,010 dB, während die hörbare Note 1,70 dB über die ganze Note trieb) und wurde entfernt. |
| tremolo | [0.0 .. 0.5] | 0.01 | default 0.01 · wobble 0.25 · wide 0.5 | Die modelleigene Vibratotiefe; die Rate bleibt fest bei 6 Hz (zu schnell, um als Einzelpuls in einer 3-s-Note zu lesen). Amplitudenmodulation bei 6 Hz misst 0,006 bei Tiefe 0 (der modelleigene kleine Decay-Beat, auch bei Null vorhanden) bis 0,156 bei Tiefe 0,5. Lautstärke-Spreizung 0,03 dB bei jedem Register. |

Halbwertszeit gemessen 566–599 ms, Spitzenpegel flach bei 0,57 von 110 bis 1760 Hz.

**Stand:** überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen

### `wurlitzer`
*Surface forms: wurlitzer, wurlitzer piano*

Ein echtes Wurlitzer-Zungenklavier, das ebenfalls von selbst klingt und ausklingt — Csounds `fmwurlie`, bei jeder Note neu angeschlagen: reediger und bellender als ein Rhodes, die Helligkeit sitzt AUF den ungeraden Harmonischen statt im Kammfilter, und es klingt länger nach. `fm_ep` ist das stehende Gegenstück.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| bark | [0.0 .. 1.0] | 1.0 | mellow 0.0 · default 1.0 | Bewegt `fmwurlie`s eigenen Modulationsindex und die Ausgangs-Überblendung gemeinsam. Anders als beim Rhodes hellt dies vor allem durch mehr Teiltongehalt IM Grundton auf statt über den Kammfilter: Klangschwerpunkt bei 220 Hz läuft 221 → 336 → 554 Hz über bark 0,0/0,5/1,0 (Faktor rund 2,5, ebenso bei anderen Registern: 111→278 bei 110 Hz, 440→1108 bei 440 Hz, 880→2216 bei 880 Hz), während der Kammfilter-Kontrast sich kaum bewegt (90,1→94,0 dB bei 220 Hz). Lautstärke-Spreizung 0,000 dB bei jedem Register. |
| tremolo | [0.0 .. 0.5] | 0.01 | default 0.01 · wobble 0.25 · wide 0.5 | Die modelleigene Vibratotiefe; Rate fest bei 6 Hz, aus demselben Grund wie beim Rhodes. Amplitudenmodulation bei 6 Hz misst rund 0,008 bei Tiefe 0 an 220/440/880 Hz (das Register 110 Hz trägt einen größeren, registerspezifischen Eigen-Beat von rund 0,025, bei jeder Tiefe vorhanden), steigend auf rund 0,208 bei Tiefe 0,5. Lautstärke-Spreizung 0,004–0,005 dB bei jedem Register. |

Halbwertszeit gemessen 676–718 ms, Spitzenpegel flach bei 0,55 über das Register.

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Gezupfte Zungen und Idiophone

### `harpsichord`
*Surface forms: harpsichord, cembalo, spinet, spinett, virginal, clavecin, kielflügel, plucked keyboard*

Cembalo / Spinett / Virginal — ein gezupftes Tasteninstrument: zwei helle `gbuzz`-Chöre, ein 8' und ein 4' (ein Haar zu hoch gestimmt), die gegeneinander driften; das gezupfte Saitenspektrum ohne den Anschlag (die echte gezupfte Saite ist `string`).

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| quill | [0.45 .. 0.92] | 0.8 | soft 0.45 · default 0.8 · hard 0.92 | Wie hart der Kiel den 8'-Chor zupft, als gbuzz-Rolloff auf diesem Chor allein. Klangschwerpunkt bei 220 Hz läuft 728 → 1059 Hz. Der 4'-Chor sitzt eine Oktave und ein Haar daneben und schwebt nicht gegen den 8', darum hält die Lautstärke eng: Spreizung 0,03 dB bei jedem Register (110–880 Hz). |
| four | [0.0 .. 1.0] | 0.24 | off 0.0 · default 0.24 · full 1.0 | Die eigene gbuzz-Amplitude des 4'-Registerzugs — ein echter Registerzug, der Leistung hinzufügt statt sie zu verschieben. Klangschwerpunkt bei 220 Hz läuft 821 → 1054 Hz. Gegen 7,5 dB rohe Schwankung bei ganz gezogenem Register hält die Lautstärke jetzt auf 0,02 dB bei 220 Hz und 0,03–0,15 dB bei 110/440/880 Hz. Bei four=0 steht der Klang praktisch still. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `mbira`
*Surface forms: mbira, kalimba, thumb piano, likembe, sanza, karimba, mbira dzavadzimu, lamellophone, daumenklavier, sansula, african thumb piano*

Kalimba / Daumenklavier — eine gezupfte Stahlzunge über einem resonierenden Brett. Eine gleichförmige Zunge hätte Teiltöne bei 1 : 6,27 : 17,55 (Stimmgabel-Klang); eine echte Zunge ist zum Anschlagsende hin dünner GESCHMIEDET, was die Obertöne herunterzieht. Zwei Elemente gibt es sonst nirgends in der Bibliothek: die Machachara (lose Flaschenkapseln auf dem Brett) und die Deze (der Flaschenkürbis, in dem das Brett sitzt).

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| taper | [0.0 .. 1.0] | 0.55 | ideal bar 0.0 · kalimba 0.22 · dzavadzimu 0.45 · forged thin 1.0 | Wie weit die Zunge zur Spitze hin geschmiedet ist — die eigentliche Klangfarbenregelung: der zweite Teilton sitzt bei 6,27-fach ohne Verjüngung, 5,70-fach bei einer handelsüblichen Kalimba, 5,10-fach bei der gemessenen Shona-Mbira (1 : 5 : 14). Klangschwerpunkt 2304 → 1372 Hz bei 220 Hz. Der Kammfilter-Kontrast ist absichtlich NICHT monoton über die vier Anker (29,9 / 20,3 / 26,4 / 27,7 dB) — die Teiltöne gleiten durch exakt-harmonische Lagen hindurch, der Ton wechselt zwischen hohl und scheppernd. Lautstärke 0,11 dB. |
| buzz | [0.0 .. 1.0] | 0.35 | clean 0.0 · shells 0.35 · rattling 0.7 · swarming 1.0 | Die Machachara: wie viele Kapseln auf dem Brett liegen und wie locker. Klangschwerpunkt 1753 → 2082 Hz bei fast unverändertem Kammfilter (29,4 → 28,9 dB) — das Kennzeichen eines zugefügten unstimmigen Betts statt einer Klangfarbenänderung. Einschlagrate steigt parallel 320 → 840 pro Sekunde. Lautstärke 0,00 dB. |
| gourd | [0.0 .. 1.0] | 0.45 | board only 0.0 · small deze 0.35 · deze 0.6 · big calabash 1.0 | Die Deze, in der das Brett sitzt, als Helmholtz-Hohlraum von 340 Hz hinab bis 175 Hz — ABWÄRTS, da ein weiterer Hohlraum tiefer resoniert. Fest in Hertz: bei 165 Hz Klangschwerpunkt 1473 → 1345 Hz und Kammfilter 27,2 → 29,2 dB; bei 110 Hz 1067 → 1003 Hz; bei 220 Hz nur 1850 → 1838 Hz; bei 440 Hz gar nichts mehr — eine kleine Achse in der Referenzlage, Physik statt Zufall. Lautstärke innerhalb 0,18 dB überall. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `jaw_harp`
*Surface forms: jaw harp, jew's harp, mouth harp, morsing, khomus, doromb, munnharpa, maultrommel, morchang, vargan, guimbarde*

Maultrommel / Khomus / Morsing — eine Stahlzunge, gegen die Zähne gezupft: die Zunge liefert einen dichten harmonischen Kamm (68,8 dB Kontrast) auf EINER Tonhöhe, alles andere formt die Mundhöhle davor — ein Filter, der die Farbe verschiebt und die Serie unberührt lässt, genau das, was eine resonierende Mundhöhle IST.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| vowel | [0.0 .. 1.0] | 0.45 | oo 0.0 · oh 0.3 · ah 0.6 · ee 1.0 | Die Form des Mundes vor der Zunge, von `oo` bis `ee`. Klangschwerpunkt 799 → 1533 Hz, Kammfilter konstant auf 1,0 dB, Lautstärke auf 0,02 dB. Die Formanten sitzen FEST in Hertz — ein Mund transponiert nicht mit der Note. Die Klangbewegung in der Note ist beim dunklen Vokal am größten, beim hellen am kleinsten (399 → 70 Hz), weil dieselbe feste Hertz-Wanderung einen großen Anteil eines 840-Hz-Bands ausmacht, aber nur einen kleinen eines 2290-Hz-Bands. |
| sweep | [0.0 .. 1.0] | 0.55 | held 0.0 · drifting 0.35 · sliding 0.65 · wailing 1.0 | Wie weit der Mund während der Note wandert — die eigentliche Bewegungsachse. Die Klangbewegung in der Note verdoppelt sich, 155 → 307 Hz, bei fast unverändertem Klangschwerpunkt (1112 → 1190 Hz) und Kammfilter (0,9 dB). Ein Boden bleibt immer: der Mund steht nie ganz still, selbst bei 0. Lautstärke 0,01 dB. |
| pluck | [0.0 .. 1.0] | 0.3 | gentle 0.0 · plucked 0.4 · hard 0.75 · snapping 1.0 | Wie hart die Zunge angeschlagen wird — als Reichweite des Kammfilters, nicht als Pegel. Klangschwerpunkt 1148 → 1240 Hz, Kammfilter 69,4 → 65,1 dB: härteres Zupfen füllt das Spektrum ZWISCHEN den Harmonischen (die Zunge rattert gegen den Rahmen), statt heller zu machen. Bis 2026-07-25 bewegte diese Achse die Note kaum hörbar; die Mischung wurde oberhalb des Defaults verstärkt, `gentle` und Default bleiben bitidentisch. Lautstärke 0,000 dB durch `balance`. |

**Stand:** überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen

---

## Stimme

### `voice`
*Surface forms: voice, vocal, vocals, choir, vowel, ahh, aah, sung, soprano, voix, stimme, chor, gesang, human voice*

Eine gesungene menschliche Stimme auf offenem „ah" — echte Formantsynthese: eine `vco2`-Glottisquelle, gekippt durch `tone`, durch drei `reson`-Formanten bei 600/1040/2250 Hz, mit dreifachem Pitch-Jitter und zweifachem Amplituden-Schimmer, damit der Ton nie ganz steht. Die Tonhöhe folgt der Tastatur, die Formanten NICHT — das macht die Stimme aus und begrenzt sie auch: gemessen eben bis 660 Hz (innerhalb 3,3 dB), dann −13,8 dB bei 880 Hz, weil oberhalb des ersten Formanten keine Harmonische mehr hineinpasst.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| jitter | [0.0 .. 5.0] | 1.0 | steady 0.0 · voice 1.0 · strained 5.0 | Skaliert alle fünf vorhandenen Jitter-/Schimmer-Generatoren gemeinsam (drei Pitch-Raten 7,3/11,7/19,1 Hz, zwei Amplituden-Raten 5,1/8,9 Hz) von völlig steadyem Formantton bis zu einer weiten, angestrengten Stimme. Tonhöhenwanderung bei 220 Hz: 0,03 Cent bei 0, 5,74 Cent beim Default, 31,44 Cent am oberen Ende. Amplituden-Schwebungstiefe parallel 0,001 → 0,028 → 0,141. Lautstärke bleibt innerhalb 0,15 dB des Defaults bei jedem Register. |
| press | [120 .. 330] | 200 | relaxed 120 · voice 200 · pressed 330 | Bewegt die Grenzfrequenz der Glottisquellen-Steigung (−6 bis −12 dB/Oktave) VOR den Formanten: tief weich und gehaucht, hoch gepresst und angestrengt — echter stimmlicher Kraftaufwand. Klangschwerpunkt bei 220 Hz läuft 514 → 537 → 577 Hz, bei 880 Hz nur 1244 → 1249 → 1261 Hz. Die Kompensation hält 110–880 Hz unverändert (0,07/0,34/0,38/0,07 dB) und wurde jetzt auf die äußeren Register erweitert: bei 1760 Hz friert sie am 880-Hz-Randwert ein statt auf null auszublenden (0,31 dB, vorher 8,68 dB), bei 55 Hz übernimmt ein eigener additiver Term (0,52 dB, vorher 1,70 dB). Der Default rendert bitidentisch zum ausgelieferten Klang (max. Differenz 0,0 über 72 geprüfte Register-/Preroll-/Dauerkombinationen). |

**Stand:** überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen

### `voice_ee`
*Surface forms: eee vowel, ee vowel, bright voice, nasal voice, iii*

Ein helles, dünnes, nasales „iii" — dieselbe Formantbank wie `voice`, verschoben auf 270/2290/3010 Hz: ein sehr tiefer erster und ein sehr hoher zweiter Formant, das ergibt den gepressten Klang von „iii". Der erste Formant ist zugleich die Decke, und die liegt hier TIEF: eben bis 220 Hz, dann −6 dB bei 330 Hz, −21 dB bei 440 Hz, verschwunden bei 660 Hz — ein Bass-/Bariton-Vokal, kein Sopran-Vokal.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| jitter | [0.0 .. 5.0] | 1.0 | steady 0.0 · voice 1.0 · strained 5.0 | Dieselben fünf Jitter-/Schimmer-Generatoren wie bei `voice`. Tonhöhenwanderung bei 220 Hz: 0,08 Cent bei 0, 5,7 Cent beim Default, 29,48 Cent am oberen Ende. Amplituden-Schwebungstiefe 0,011 → 0,028 → 0,142. Lautstärke bleibt innerhalb 0,13 dB des Defaults bei jedem Register. |
| press | [120 .. 330] | 200 | relaxed 120 · voice 200 · pressed 330 | Dieselbe Glottis-Steigungsgrenze wie bei `voice`. Klangschwerpunkt bei 220 Hz läuft 367 → 388 → 429 Hz, bei 440 Hz 1314 → 1341 → 1401 Hz. Die Kompensation hält 110–880 Hz unverändert (0,05/0,34/0,32/0,08 dB); bei 1760 Hz friert sie jetzt am 880-Hz-Randwert ein (0,29 dB, vorher 8,66 dB), bei 55 Hz übernimmt ein eigener additiver Term (0,002 dB, vorher 2,23 dB). Der Default rendert bitidentisch zum ausgelieferten Klang (max. Differenz 0,0 über 72 Kombinationen). |

**Stand:** überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen

### `voice_oo`
*Surface forms: ooh vowel, oo vowel, dark voice, hollow voice, uuu*

Ein dunkles, rundes, hohles „uuu" — Formanten bei 300/870/2240 Hz, die beiden unteren eng beieinander und tief, der Klang ist ganz Körper und keine Kante. Gemessene Decke bei rund 330 Hz (eben bis 330, dann −14 dB bei 440 Hz), aus demselben Grund wie bei den anderen Vokalen: die Formanten stehen still, während die Stimme in sie hineinsteigt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| jitter | [0.0 .. 5.0] | 1.0 | steady 0.0 · voice 1.0 · strained 5.0 | Dieselben fünf Jitter-/Schimmer-Generatoren wie bei `voice`. Tonhöhenwanderung bei 220 Hz: 0,05 Cent bei 0, 5,68 Cent beim Default, 29,77 Cent am oberen Ende. Amplituden-Schwebungstiefe 0,008 → 0,028 → 0,142. Lautstärke bleibt innerhalb 0,11 dB des Defaults bei jedem Register. |
| press | [120 .. 330] | 200 | relaxed 120 · voice 200 · pressed 330 | Dieselbe Glottis-Steigungsgrenze wie bei `voice`. Klangschwerpunkt bei 220 Hz läuft 363 → 380 → 411 Hz, bei 440 Hz 863 → 871 → 888 Hz. Die Kompensation hält 110–880 Hz unverändert (0,08/0,30/0,38/0,08 dB); bei 1760 Hz friert sie jetzt am 880-Hz-Randwert ein (0,29 dB, vorher 8,66 dB), bei 55 Hz übernimmt ein eigener additiver Term (0,24 dB, vorher 1,98 dB). Der Default rendert bitidentisch zum ausgelieferten Klang (max. Differenz 0,0 über 72 Kombinationen). |

**Stand:** überarbeitet 2026-07-26, gemessen · Referenzvergleich: offen

### `overtone_voice`
*Surface forms: overtone singing, throat singing, sygyt, khoomei, kargyraa, obertongesang, harmonic singing, overtone chant, tuvan throat singing, oberton, kehlgesang*

Obertongesang / Sygyt / Khoomei — eine Stimme, die aus ihren eigenen Obertönen eine Melodie formt: der zweite und dritte Vokaltrakt-Formant verschmelzen zu EINER sehr schmalen Resonanz, die auf genau eine Harmonische eines dichten Buzz-Spektrums gestimmt wird, sodass nur diese als Pfeifton über der Drone durchkommt. Über `press` wandert der Klangschwerpunkt 356 Hz, während der Kammfilter-Kontrast von 57,1 auf 32,1 dB fällt (Spanne 25,0 dB) — das ist also nicht reine Klangfarbe ohne Quelländerung.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| select | [0.0 .. 1.0] | 0.45 | low harmonic 0.0 · fourth 0.3 · eighth 0.6 · high harmonic 1.0 | Welche Harmonische die verschmolzenen Formanten trifft — das ist die Melodie, für die der Stil da ist. Klangschwerpunkt 1301 → 2267 Hz. Bei 220 Hz wandert die Wahl etwa von der 5. zur 11. Harmonischen. Unterhalb rund 330 Hz Grundton sitzt die Resonanz FEST in Hertz und die Drone folgt der Tastatur, sodass sich die tönende Harmonische mit der Note ändert, wie bei einem echten Sänger. Lautstärke 0,21 dB. **Offener Defekt:** eine Anhebung gegen den Rolloff der Drone lässt den Pegel der jeweils gewählten Harmonischen mitwandern, wenn diese Achse innerhalb einer gehaltenen Note läuft — am schlechtesten gemessenen Punkt 6,66 dB, über der 1,0-dB-Grenze; jede bislang versuchte Konstante verschiebt nur diese Zahl, behebt sie aber nicht. |
| focus | [0.0 .. 1.0] | 0.6 | diffuse 0.0 · khoomei 0.4 · sygyt 0.75 · whistling 1.0 | Wie eng die beiden Formanten verschmolzen sind, als Güte der Resonanz: 9 (offen) bis 45 (volle Fokussierung). Fast keine Klangschwerpunkt-Änderung (1688 → 1844 Hz) und ein mäßiger, nicht-monotoner Kammfilter-Wechsel (47,9/45,1/49,1/50,1 dB, Spanne 7,0 dB) — aber die Klangbewegung in der Note springt 188 → 310 Hz: ein enges Fenster macht dieselbe Stufenmelodie als wanderndes Pfeifen hörbar, ein weites mittelt sie weg. Lautstärke 0,01 dB. |
| press | [0.0 .. 1.0] | 0.45 | relaxed 0.0 · speaking 0.3 · pressed 0.65 · strained 1.0 | Wie gepresst die Stimme ist, was den Buzz füllt, aus dem die Formante ihre Harmonische wählt. Klangschwerpunkt bewegt sich 299 Hz, die Klangbewegung in der Note 114 Hz, bei 2,1 dB Kammfilter über die ganze Achse — eine gepresstere Stimme liefert mehr Harmonische, keine andere Spektrumform. Lautstärke 0,18 dB. |

**Stand:** offener Defekt: die `select`-Achse pumpt im Notenverlauf 6,66 dB · Referenzvergleich: offen

---

## Insekten und Tiere

### `cicada`
*Surface forms: cicada, cicadas, zikade, zikaden, tymbal, cicada chorus, harvest fly, summer insects*

Eine Zikade hat KEINE Stimme: jede Rippe der Tymbal-Platte KNICKT ein, jedes Einknicken ist ein Impuls, und die RATE dieser Impulsfolge klingt als Tonhöhe (belegt 100–250 Knicke/s, Bennet-Clark & Young 1992). Die hörbare Energie sitzt woanders — im Luftsack-Band nahe 4,3 kHz, fest in Hertz; Rate und Sack liegen Größenordnungen auseinander, und die Serie ist dabei fast FLACH statt abzurollen, weil ein Knicken ein Klick ist.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| ribs | [0.0 .. 1.0] | 0.55 | few 0.0 · half 0.4 · most 0.7 · all 1.0 | Wie viele Rippen die Knicklast erreichen, als Flachheit der Impulsfolge — die breiteste Achse hier: Klangschwerpunkt 1627 → 3163 Hz, Klangbewegung in der Note 210 → 1348 Hz, bei 0,00 dB Lautstärke. Der Kammfilter bewegt sich kaum (57,5 → 51,2 dB): eine flachere Folge bleibt eine Folge, was wächst, ist die Reichweite der Serie. |
| sac | [0.0 .. 1.0] | 0.5 | deep body 0.0 · song 0.55 · small 1.0 | Wo der Luftsack im Hinterleib resoniert, 3000 bis 5300 Hz — der gemessene Bereich realer Arten. Klangschwerpunkt 2038 → 2614 Hz bei unverändertem Kammfilter (0,8 dB), das Kennzeichen eines Resonators vor unveränderter Quelle. Fest in Hertz, färbt jede Tonhöhe gleich. Lautstärke 0,00 dB. |
| rasp | [0.0 .. 1.0] | 0.4 | clockwork 0.0 · insect 0.4 · ragged 0.75 · tearing 1.0 | Wie unregelmäßig das Einknicken ist, als Jitter auf der Rate. Gemessen als Kammfilter-Änderung, nicht als Farbänderung: Kontrast 69,9 → 44,6 dB, während der Klangschwerpunkt nur 611 Hz wandert — die Serie verschmiert zu einem Band, statt sich zu verschieben. Bei 0 liest sich das als unplausibel sauberer elektronischer Summton. Lautstärke 0,00 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `cricket`
*Surface forms: cricket, crickets, grille, grillen, field cricket, stridulation, bush cricket, cricket chirp, katydid, night insects*

Eine Grille zieht einen Schaber über eine Zahnreihe, die Zahnschlag-RATE ist der Träger — deshalb ist der Ton fast ein reiner Sinus: der Zahnabstand ist evolutionär auf die Resonanz des Flügels selbst abgestimmt (gemessen 4–5 kHz bei einer Feldgrille), keine gefilterte Näherung. Aus der Nähe ist eine Grille aber NICHT ein reiner Ton: der über die Zähne schleifende Schaber ist als breitbandiges Rauschen weit über dem Träger hörbar.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| scrape | [0.0 .. 1.0] | 0.4 | clean 0.0 · close 0.4 · rasping 0.75 · grinding 1.0 | Wie viel von der Zahnreihe gegen den Flügelton zu hören ist — die breiteste Achse, auf der sich das Instrument bewegt: Klangschwerpunkt 1820 → 2188 Hz, Kammfilter 33,9 → 24,9 dB, Klangbewegung in der Note 32 → 221 Hz, bei 0,01 dB Lautstärke. Ihr Anteil folgt der Flügelgeschwindigkeit und atmet mit der Chor-Drift bei 0,31 Hz, unabhängig von der Einstellung. |
| chirp | [0.0 .. 1.0] | 0.5 | cold 0.0 · cool 0.3 · warm 0.65 · hot 1.0 | Flügelschläge pro Sekunde, 12 bis 50 — eine RATEN-Achse, für kein Spektralmessgerät sichtbar (nur 2 Hz Klangschwerpunkt, 0,5 dB Kammfilter), aber am Beat-Messwerkzeug klar ablesbar: 12,0 / 21,7 / 31,0 / 40,3 / 50,0 Hz gegen die verlangten 12/22/31/40/50, bei Pulstiefe 0,34–0,41. Dolbears Gesetz (Insektenrate folgt der Temperatur) ist der Grund, warum das überhaupt ein Regler ist. Lautstärke 0,01 dB. |
| pure | [0.0 .. 1.0] | 0.6 | reedy 0.0 · cricket 0.6 · whistle 1.0 | Wie sinusartig der Flügelton klingt, als Reichweite seiner Obertonreihe — bewusst eine kleine Achse: Klangschwerpunkt 1980 → 2087 Hz, Kammfilter 31,1 → 27,8 dB, weil eine Grille bei jeder Einstellung fast rein klingt und dies nur entscheidet, wie nahe. Rührt bewusst NICHT an die Zahnreihe — als es das tat, multiplizierten sich zwei Achsen und sieben Ecken des Würfels standen still. Lautstärke 0,01 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `frog`
*Surface forms: frog, frogs, tree frog, frosch, frösche, toad, kröte, frog call, bullfrog, peeper, pond at night*

Ein Frosch kodiert seine Art nicht in der Tonhöhe, sondern in der PULSRATE: der Kehlkopf erzeugt einen rauen Summton, die Rufe werden mit 15 bis 100 Pulsen pro Sekunde zerhackt, und genau diese Rate hört ein anderer Frosch. Vor dem Kehlkopf sitzt der aufgeblasene Schallsack, hier als fest in Hertz sitzender Formant (380–1530 Hz) modelliert — nach Rand & Dudley 1993 ist der Schallsack KEIN Hohlraumresonator (Frösche in Helium riefen bei unveränderter Frequenz), sondern ein impedanzanpassender Strahler; die Tonhöhe entsteht am Kehlkopf.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| pulse | [0.0 .. 1.0] | 0.5 | slow 0.0 · trill 0.4 · buzz 0.75 · rattle 1.0 | Pulse pro Sekunde, 15 bis 100 — der Artmarker. Am Beat-Messwerkzeug 15,0 / 57,7 / 100,0 Hz gegen die verlangten 15/58/100. An den Klangfarbenmessern bewegt es nur 5 Hz Klangschwerpunkt, aber 50,8 dB Kammfilter — das Zerhacken einer Serie in Audiorate legt Seitenbänder um jede Harmonische und füllt die Lücken dazwischen; genau dieser Kammfilter-Kollaps IST die Messung des Pulsierens. Lautstärke 0,01 dB. |
| sac | [0.0 .. 1.0] | 0.45 | flat 0.0 · tree frog 0.35 · full 0.7 · bullfrog 1.0 | Wie aufgeblasen der Schallsack ist, als fest in Hertz sitzender Formant von 380 bis 1530 Hz — ein Modell dessen, was der Sack ABSTRAHLT, nicht eines schwingenden Hohlraums. Die einzige Farbachse hier: Klangschwerpunkt 358 → 551 Hz, Klangbewegung in der Note 68 → 150 Hz, bei 0,00 dB Lautstärke. Sackt bei 0,37 Hz ab und füllt sich wieder — daher kommt die Bewegung des Instruments bei jeder Einstellung. |
| croak | [0.0 .. 1.0] | 0.55 | smooth 0.0 · purring 0.35 · croaking 0.7 · clacking 1.0 | Wie tief die Pulse in den Ruf einschneiden, von glattem Summen bis hartem Zerhacken. Kammfilter 70,4 → 63,1 dB über die ersten drei Viertel der Achse, 43,4 dB am oberen Ende, wo der Schnitt so tief geht, dass die klingende Grundtonhöhe auf ein Viertel der Note fällt (f0 55,0 Hz bei verlangten 220 Hz). Nur 2 Hz Klangschwerpunkt — wie bei `pulse` eine Kammfilter-, keine Farbmessung. Lautstärke 0,01 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Natur und Textur

### `noise`
*Surface forms: white noise, noise, white-noise, rauschen, weißes rauschen*

Weißes Rauschen — `rand`, ein flaches Vollspektrum-Rauschen ohne jede Tonhöhe. Die Tastatur ändert nichts daran (gemessen identisch bei 110 und 880 Hz); es ist ein Bett, die Tonhöhe muss von dem kommen, womit es gefiltert oder ringmoduliert wird — das Rohmaterial, aus dem jedes andere Rausch-Bett hier gebaut ist.

**Stand:** ohne Parameter · Referenzvergleich: offen

### `pink_noise`
*Surface forms: pink noise, pink-noise, rosa rauschen*

Rosa Rauschen — `pinkish`, eine 1/f-Neigung über `rand`: dasselbe Rauschen mit abgenommener Spitze, weicher und wärmer, und der Spektralform realer Umgebungsgeräusche deutlich näher als weißes Rauschen. Keine Tonhöhe, die Tastatur ändert nichts.

**Stand:** ohne Parameter · Referenzvergleich: offen

### `wind`
*Surface forms: wind, howling wind, breeze, gust, windig, wind noise*

Ein breites `reson`-Band, das über Rauschen auf- und abwandert, getrieben von zwei inkommensurablen langsamen Oszillatoren (0,071 und 0,113 Hz), damit die Böen nie in ein Muster fallen: das Band steigt beim Auffrischen von 380 auf rund 1420 Hz und wird dabei zugleich lauter — das macht bewegte Luft aus einem bloßen Filter-Sweep. Keine Tonhöhe, die Tastatur ändert nichts.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| speed | [0.0 .. 1.0] | 0.45 | light breeze 0.0 · breeze 0.25 · wind 0.45 · gale 1.0 | Wie hart es bläst. Ein äolischer Ton steigt mit der Windgeschwindigkeit und skaliert das ganze Böen-Band mit, von 158–417 Hz bei leichter Brise bis 652–1722 Hz im Sturm; die BREITE wächst quadratisch dazu (69 auf 1176 Hz), weil die Turbulenz mit der Geschwindigkeit steigt. Klangschwerpunkt 745 → 3046 Hz, Klangbewegung 204 → 826 Hz; die Hüllkurve glättet sich dabei (Rauigkeit 0,375 → 0,152), weil ein schnellerer Luftstrom gleichmäßiger ist. Lautstärke 0,14 dB über die ganze Achse. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `rain`
*Surface forms: rain, rainfall, drizzle, regen, raindrops*

Ein echtes Partikelbett, kein Rauschen: `dust2` feuert 250 bis 4000 einzelne Einschläge pro Sekunde, jeder lässt eine Fläche klingen (`reson` bei 2400 Hz), dazu eine dunklere Verschmelzung entfernter Tropfen im Hintergrund, das Ganze schwillt bei 0,043 Hz. Gemessen liegt das 17,8 dB (RMS) spektral von der `noise`-Referenz entfernt, die 5-ms-Hüllkurvenrauigkeit 0,409 gegen 0,034 bei reinem Rauschen. Keine Tonhöhe, die Tastatur ändert nichts.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| surface | [0.0 .. 1.0] | 0.45 | on leaves 0.0 · on earth 0.25 · on stone 0.45 · on a tin roof 1.0 | Worauf der Regen fällt: Blätter schlucken und antworten tief, ein Blechdach klingt hoch und hart — die Fläche IST die Resonanz, durch die das Prasseln klingt. Klangschwerpunkt 11393 → 11622 Hz, Crest-Faktor 3,20 → 3,95, bei 0,01 dB Lautstärke. |
| fall | [0.0 .. 1.0] | 0.5 | single drops 0.0 · steady rain 0.5 · downpour 1.0 | Die `dust2`-Rate, geometrisch von 250 auf 4000 Einschläge pro Sekunde. Mit den Tropfen im Vordergrund bewegt dieser Regler die Hüllkurvenrauigkeit 0,892 → 0,190 und den Crest-Faktor 24,8 → 17,1 dB — von einzeln zählbaren Tropfen zu einem verschmelzenden Guss. Gemessene Spreizung 0,28 dB über einen 11-Punkte-Verlauf bei jedem Register. |

**Stand:** überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen

### `surf`
*Surface forms: surf, ocean, sea, waves, meer, wellen*

Zwei Rauschbänder, von EINER Dünung bewegt: ein tiefer Körper (`tone` 700) immer da, ein heller Bruch (`atone` 1800), der nur am Scheitel der Dünung ankommt — als deren QUADRAT, sodass die Welle bricht statt einzublenden. Zwei inkommensurable Dünungen (0,055 und 0,083 Hz, rund 18 s und 12 s) halten die See davon ab, sich zu wiederholen. Keine Tonhöhe, die Tastatur ändert nichts.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| water | [0.0 .. 1.0] | 0.45 | a choppy lake 0.0 · a big lake 0.25 · surf 0.45 · an ocean swell 1.0 | Wie groß das Gewässer ist. Ein aufgewühlter See hat kaum Energie unter ein paar hundert Hertz, eine Ozeandünung liegt meist darunter — die Größe des Wassers ist, wo der Körper der Welle sitzt: 1571 Hz am einen Ende, 262 Hz am anderen. Klangschwerpunkt 10647 → 10892 Hz, Klangbewegung 5307 → 7825 Hz, Crest 2,34 → 3,20, Lautstärke 0,86 dB. Sitzt auf dem KÖRPER, nicht auf dem Bruch — der bleibt vom Quadrat der Dünung gesteuert, weil eine Welle nur am Scheitel bricht. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `thunder`
*Surface forms: thunder, rolling thunder, distant thunder, thunderclap, storm, rumble, rumbling, donner, grollen*

Ein tiefer breitbandiger Körper (`tone` 400), gerollt von drei inkommensurablen langsamen Oszillatoren (0,037/0,061/0,091 Hz), sodass das Grollen sich ständig neu über sich selbst faltet und nie pulsiert. Keine Tonhöhe, die Tastatur ändert nichts.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| distance | [0.0 .. 1.0] | 0.4 | overhead 0.0 · close 0.2 · thunder 0.4 · miles off 1.0 | Wie weit das Gewitter entfernt ist. Luft schluckt die Höhen, je weiter der Schall reist — Entfernung IST die Grenzfrequenz, 919 Hz direkt darüber bis 115 Hz meilenweit weg. Klangschwerpunkt 6020 → 4147 Hz, die Hüllkurve wird dabei RAUER (0,161 → 0,334), weil ohne die Höhen nur das langsame Rollen übrigbleibt. Lautstärke exakt gehalten auf 0,10 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `hiss`
*Surface forms: hiss, tape hiss, radio static, dead channel, zischen*

`atone` über Rauschen: nur die Spitze des Spektrums, der Klang eines toten Kanals. Keine Tonhöhe; die Tastatur ändert nichts, und der Eintrag erklärt `; MOVEMENT: TEXTURE` — konstruktionsbedingt stehendes Rauschen, eine Klasse, die kein Messverfahren dieses Projekts von einem gefegten Sägezahn unterscheiden kann (ein statisches schmalbandiges Bett liest sich mit 1005 Cent Farbwanderung, mehr als ein echter Sweep mit 959).

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| brightness | [500.0 .. 16000.0] | 6000.0 | full band 500.0 · thinned 1500.0 · tape hiss 6000.0 · thin sizzle 12000.0 | Die Eckfrequenz des einpoligen Hochpasses in Hertz: wie viel von der Spitze übrig bleibt. Klangschwerpunkt (leistungsgewichtet) 11346 Hz bei 500, 11931 Hz bei 1500, 13530 Hz bei 6000, 14386 Hz bei 12000 Hz; Energieanteil oberhalb 6 kHz 75/80/90/94 %. Selbst am unteren Ende bleiben 75,3 % der Energie über 6 kHz — kein dunkles Rauschen ist mit dieser Achse zu erreichen (dafür gibt es `rain`, `surf`, `wind`). Was wirklich wandert, ist der Anteil darunter: 2–6 kHz fällt von 18,4 auf 5,7 %, 0,5–2 kHz von 5,8 auf 0,3 % — der Unterschied zwischen breitem Rauschen und fernem Zischen. Rohe Pegeländerung 11,47 dB über den ganzen Bereich, durch eine gemessene Kompensation gehalten: Spreizung 0,075 dB bei 44,1 kHz, 0,062 bei 48 kHz, 0,066 bei 96 kHz. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `crackle`
*Surface forms: crackle, crackling, fire, campfire, knistern, feuer*

Dieselbe Partikel-Architektur wie `rain`, bei einem Hundertstel der Rate: `dust2` feuert rund 22 Knalle pro Sekunde, jeder lässt Holz klingen (`reson` 1600 Hz, breit), über einem feinen Zischen, das bei 0,13 Hz atmet. Spärlich genug, dass einzelne Ereignisse hörbar bleiben — das unterscheidet Feuer von Regen. Keine Tonhöhe, die Tastatur ändert nichts.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| blaze | [0.0 .. 1.0] | 0.4 | embers 0.0 · a hearth 0.4 · a bonfire 1.0 | Wie groß das Feuer ist — das ist die KNALLRATE, von unter sechs pro Sekunde in der Glut bis 170 im Lagerfeuer. Ein größeres Feuer knallt ÖFTER, nicht lauter. Kennzeichen ist der Crest-Faktor, 8,94 → 3,47 monoton, und die Bewegung 3441 → 1483 Hz — einzelne Knalle verschmelzen zum Kontinuum —, bei 0,02 dB Lautstärke. Neu vermessen 2026-07-25: über einen 11-Punkte-Verlauf hält die Achse 0,356 dB, der Klang reicht von 5,6 zählbaren Knallen pro Sekunde bis 169, Rauigkeit 0,154 → 0,549. |

**Stand:** überarbeitet 2026-07-25, gemessen · Referenzvergleich: offen

### `bubbles`
*Surface forms: bubbles, blubbern, brook, stream, creek, burbling, gurgling, glucksen, water bubbles, bubbling water, underwater*

Blasen in Wasser — ein Bach, eine Quelle, alles Gluckernde. Eine Blase ist eine Luftmasse auf einer Wasserfeder und klingt bei der Minnaert-Frequenz, f ≈ 3,26/R Hertz·Meter: eine Millimeterblase singt nahe 3,3 kHz, eine Zentimeterblase nahe 330 Hz. Jede ist ein gedämpfter Sinus (Güte rund 15), dessen Tonhöhe während des Pulses STEIGT, während die Blase sich zusammenzieht — deshalb klingt eine einzelne Blase wie ein Plopp, kein Klick.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| flow | [0.0 .. 1.0] | 0.45 | trickle 0.0 · running 0.45 · rushing 1.0 | Wie schnell das Wasser läuft: 8 bis 24 Auslösungen pro Sekunde im größten Strang, die anderen beiden Größen bei inkommensurablen Vielfachen davon, damit nie ein Puls entsteht. Das breitbandige Tuch unter den Blasen steigt mit — schnelleres Wasser ist zugleich mehr Blasen und mehr Tuch. Die Hüllkurve ist gegen die PHASE geschrieben statt gegen die Zeit, die Energie pro Sekunde hängt also nicht von der Rate ab. |
| size | [0.0 .. 1.0] | 0.5 | fine 0.0 · mixed 0.5 · wide 1.0 | Wie breit die Blasen sind, alle drei Größen zugleich entlang der Minnaert-Beziehung. Breite Blasen klingen tief und hohl, schmale hoch und gläsern — die Achse liest sich TONHÖHENMÄSSIG abwärts, je höher sie steht: bei 0 sitzen die drei Stränge bei 246–2003 Hz (auf eine 220-Hz-Note), bei 1 bei 29–236 Hz. Das breitbandige Wassertuch darunter dominiert den Klangschwerpunkt und zieht ihn dabei AUFWÄRTS (9378 → 10182 Hz), während die Blasen selbst sinken. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `ice`
*Surface forms: ice, eis, cracking ice, frozen lake, ice cracks, glacier, gletscher, frost, ice sheet, singing ice, thin ice, knacken, eisknacken, knirschen*

Krachendes Eis auf einem gefrorenen See — und der Grund, warum es nach Science-Fiction klingt statt nach einem Klick, ist Dispersion: Biegewellen in einer dünnen Platte haben eine Phasengeschwindigkeit, die mit der Wurzel der Frequenz wächst, die hohen Anteile eines einzelnen Impulses überholen also die tiefen und kommen zuerst an — ein absteigendes Pfeifen aus einem einzigen, augenblicklichen Knacken. Nichts sonst in dieser Bibliothek hat eine dispersive Quelle.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| crack | [0.0 .. 1.0] | 0.45 | settling 0.0 · working 0.45 · shattering 1.0 | Wie geschäftig das Eis ist: 4 bis 48 Ankünfte pro Sekunde, dazu eine zweite Folge bei inkommensurabler Rate und ein tiefer Jitter auf beiden, weil Eis unregelmäßig kracht. Wie bei `bubbles` ist die Hüllkurve gegen die PHASE geschrieben, die Energie pro Sekunde bleibt unabhängig von der Rate. Oberhalb von rund 24 pro Sekunde werden die einzelnen Sturzflüge fürs Ohr nicht mehr trennbar und das Ganze wird zu einem schimmernden Bett. |
| glide | [0.0 .. 1.0] | 0.5 | close 0.0 · far 0.5 · across the lake 1.0 | Wie weit über der Note jeder Sturzflug beginnt, physikalisch: wie weit das Knacken entfernt ist — je weiter die Welle reist, desto mehr überholen die hohen Anteile die tiefen, Entfernung IST die Tiefe des Glissandos. Von wenigen Halbtönen bis rund fünf Oktaven. Eine langsame kohärente Wanderung (0,19 Hz) liegt darüber. Die Note selbst ist da, wo jeder Sturzflug LANDET — diese Achse lässt die Tonhöhe unberührt. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

## Reibung und ungewöhnliche Erreger

### `bullroarer`
*Surface forms: bullroarer, bull-roarer, schwirrholz, schwirrgerät*

Schwirrholz — eine flache Holzklinge, an einer Schnur geschwungen. Das Schwingen versetzt die Klinge in Eigenrotation um ihre Längsachse (Autorotation, dasselbe aerodynamische Phänomen wie beim fallenden Ahornsamen); diese Drehung IST die Note — die zweizählige Symmetrie der Klinge legt den akustischen Grundton auf genau das ZWEIFACHE der mechanischen Drehrate (Fletcher, Tarnopolsky & Lai 2002, JASA 111(3):1189; Roger & Aubert 2006, Acta Acustica 92:826). Das Schwingen selbst (0,5–2,2 Hz) ist eine zweite, viel langsamere Periodizität: eine rotierende Dipol-Abstrahlung erzeugt eine Amplitudenpulsation bei der doppelten Schwungrate, die Bahnbewegung einen echten Doppler-Effekt auf dem Grundton selbst — beide in Quadratur, aus derselben Umlaufbewegung.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| speed | [0.0 .. 1.0] | 0.4 | barely turning 0.0 · swung 0.4 · whirled 0.7 · hard 1.0 | Wie schnell geschwungen wird — die Schwungrate, 0,5 Hz (gerade ausreichend für Autorotation) bis 2,2 Hz (harter, schneller Schwung). Amplitudenpulsation (dominant beim 2-Fachen der Schwungrate, 11,1 dB Scheitel-zu-Null) und Doppler-Verschiebung (bis zu 111,5 Cent Spitze-zu-Spitze am oberen Ende, gegen 111,8 Cent Entwurfswert) bewegen sich gemeinsam in Quadratur. Lautstärke-Spreizung 0,27–0,28 dB bei jedem Register. |
| size | [0.0 .. 1.0] | 0.4 | small 0.0 · ordinary 0.4 · large 1.0 | Wie groß die Klinge ist. Kann die gespielte Tonhöhe nicht bewegen (die gehört der Tastatur), sondern trägt das Verhältnis zwischen reinem aerodynamischem Ton und turbulentem Nachlauf: größere Klingen bewegen mehr Luft, mehr breitbandige Nachlaufenergie. Absichtlich auf eine Mischung von 0,01–0,08 begrenzt: bei einem weiteren Bereich läse der amplitudengewichtete Klangschwerpunkt 9340 Hz gegen die tatsächlich hörbaren 344 Hz. Im ausgelieferten Bereich bewegt der hörbare Klangschwerpunkt 221 auf 266 Hz (220-Hz-Register). Lautstärke-Spreizung 0,01–0,02 dB. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

### `cuica`
*Surface forms: cuica, cuíca, friction drum, reibetrommel*

Cuíca — eine brasilianische Reibetrommel: ein Bambusstab, von innen in der Mitte des Trommelfells befestigt, mit feuchtem Tuch gerieben, während die andere Hand die Tonhöhe per Daumendruck von außen biegt. Das Reiben ist ein Stick-Slip-Relaxationsoszillator — dieselbe Anregungsklasse wie eine gestrichene Saite; die einzige gefundene akustische Studie beschreibt es direkt so (Wheeler 2002, JASA 112(5):2266). `vco2`s bandbegrenzter Sägezahn trägt diese Tonhöhe, während der Daumen sie nur nach OBEN biegt. Kein Membran-/Hohlraumresonator ist über dem Sägezahn modelliert — Wheelers Studie besagt ausdrücklich, dass der Korpus wenig zum Klang beiträgt.

| Parameter | Bereich | Default | Anker | Was man hört |
|---|---|---|---|---|
| glide | [0.0 .. 1.0] | 0.5 | flat 0.0 · talking 0.5 · wailing 1.0 | Wie weit der Daumen die Tonhöhe biegt, UNIPOLAR nach oben von der gespielten Note. Ein Boden von 40 Cent hält die Biegung stets in Bewegung, bis zu vollen 1200 Cent (zwei Oktaven) am oberen Ende — ein gestalterischer Wert, keine gemessene Cuica-Zahl. Getrieben von einem sanft wandernden Zufallssignal, nicht von einem periodischen LFO, für den unregelmäßigen „sprechenden" Charakter. Gemessen bei 220 Hz: 26 Cent Streuung bei glide=0, ansteigend auf 577 Cent bei glide=1 (582 Cent bei 880 Hz, 1151 Cent bei 1760 Hz). Lautstärke-Spreizung 0,0–0,16 dB bei jedem Register. |
| grip | [0.0 .. 1.0] | 0.45 | loose 0.0 · ordinary 0.45 · hard 1.0 | Wie hart und gleichmäßig der Stab gerieben wird. Zwei Effekte: weniger Jitter (Jittertiefe fällt von 30 Cent bei grip=0 auf 0 bei grip=1) und mehr Helligkeit (eine registerrelative Tonfilter-Ecke vom 2-Fachen der Note bis zum 20-Fachen, bei 15 kHz gedeckelt). Hörbarer Klangschwerpunkt bewegt sich 296,5 auf 576,6 Hz bei 220 Hz. Lautstärke gehalten auf 0,02 dB über die Anker bei jedem Register durch eine gemessene Kompensation fünften Grades; der Deckel hält die Achse auf 0,58 dB schlimmstenfalls bei 1760 Hz mit voll geöffnetem `glide`, gegen 2,4–2,48 dB ungedeckelt. |

**Stand:** parametrisiert, Bounds grün · Referenzvergleich: offen

---

