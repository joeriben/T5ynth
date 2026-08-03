# Die Parameter des geschriebenen Instruments im LRO-Panel — ENTWURF

**Status:** gebaut. **§1 und §2 sind einmal umgeworfen worden** — die Reglerquelle des Entwurfs
(„der Autor erklärt sie selbst") ist gemessen gescheitert und durch die Bibliothek ersetzt; beide
Abschnitte tragen die Korrektur, der Rest des Entwurfs steht unverändert.
Skizze: `docs/plans/lro_param_panel_sketch.svg` (A = Karte ohne Orchester, B = die Parameterspalten,
C = wie eine Bibliothekszeile zum Kanal wird). §8 hält die vier Punkte fest, die der erste
Entwurf offengelassen hatte.

**Der Auftrag (BJ, 2026-07-31, wörtlich):**
> „Das LRO-Panel wird: SOBALD der Autor ein Instrument geschrieben hat die einstellbaren
> PARAMETER dieses Instruments, bzw. seiner Teilinstrumente präsentieren, und zwar im
> einheitlichen Slider-Layout, keine Halluzinationen hier. Spaltenweise, max 3
> instrumente/Spalten, semantische, nicht technische benennungen, slider können Makros
> repräsentieren. ich denke max 4 Zeilen/Slider oder Knobs, besser nur 3 in jeder Spalte."

Damit ist die offene Konsequenz aus [[project_lco_params_are_the_user_surface]] (2026-07-28:
„diese parameter … sind dann auch diejenigen die wir später dem User exponieren im ‚Prompt
Orchestra' Feld") entschieden — bis auf die vier Fragen am Ende.

---

## 1. Die eine Kernfrage: woher die Regler kommen

„Keine Halluzinationen" ist hier keine Stilregel, sondern die ganze Konstruktion. Es gibt drei
denkbare Quellen, und nur eine hält.

**(a) Die `params` der Bibliothekseinträge, die der Autor aufgeschlagen hat.** Im Entwurf
verworfen — mit einem Argument, das nur die *rohe* Form von (a) trifft: ein Regler „bowl", bloß
weil `singing_bowl` aufgeschlagen *war*, während im Code keine Schalenreihe mehr steht, wäre
tatsächlich die Halluzination. Was daraus nicht folgt, und was der Entwurf übersprungen hat: ein
Parameter, der **aufgeschlagen war UND im geschriebenen Körper steht**, ist keine Vermutung
über den Körper, sondern eine Lesung davon.

**(b) Den emittierten Csound nach `k…`-Zuweisungen absuchen.** Falsch, und das bleibt es. Ein
Körper hat 20 bis 60 k-Variablen; welche davon eine *Achse* ist, was sie bedeutet und welcher
Bereich trägt, ist aus einer Zuweisung nicht ablesbar.

**(c) Der Autor erklärt sie selbst.** Die Entwurfsentscheidung — und **gemessen gescheitert.**
gemma-4-12b, „a bowed steel bar under a breathing bottle": sechs formal einwandfreie
`PARAM`-Zeilen mit guten Namen, und im Körper `kstr 0.9884`, `kblow = 0.57 + kdrift` — **kein
einziges `kp`**. Alle sechs Regler wurden von der Anti-Halluzinationsprüfung verworfen, weil sie
nichts bewegt hätten. Das Modell schreibt den Vertrag und verdrahtet ihn nicht; ein schärferer
Prompt hat daran nichts geändert. BJ, 2026-07-31: *„Deine Annahme dass der Autor die Regler
selbst schreiben soll und die NICHT in der Bibliothek stehen sollen ist offenkundig gescheitert."*

**Gebaut ist (a) in seiner tragenden Form: ein Bibliotheksparameter, der in den geschriebenen
Körper überlebt hat.** Die Bibliothek deklariert, was an einem Körper spielbar ist, und zwar als
eine Zeile, die zugleich der Wert im Code und ihre eigene Beschreibung ist:

```csound
kbowl   = 0.50   ; bowl[0.0..1.0]: which measured bowl the mode series is
```

Behält der Autor diese Zeile, ist es ein Regler. Streicht er sie, ist es keiner. **Der Host
verdrahtet** — `wire_controls` schreibt genau diese eine Zeile in `kbowl = 0 + 1 * kp1a` um —,
also kann der Fehlermodus aus (c) nicht mehr auftreten: der Autor schreibt nie ein `kp`, er kann
es nicht vergessen. Sein einziger Anteil ist die **Zahl**, und die ist genau das Richtige: wo der
Regler steht, wenn der Spieler den Klang zum ersten Mal hört.

---

## 2. Der Vertrag: eine Bibliothekszeile, die stehen bleibt

Kein neuer Fence-Vertrag. Der Autor deklariert nichts; er *behält* oder er *lässt weg*:

```csound
k<var>  = <Zahl>   ; <name>[<lo>..<hi>]: <was er tut>
```

- **Der Name gehört der Bibliothek**, nicht dem Autor: er muss in den `params` eines Eintrags
  stehen, der für diese Autorenschaft aufgeschlagen war. Ein selbst erfundener Name in derselben
  Form ist **kein** Regler und wird mit Grund in `refused` gemeldet. Damit zeigt das Panel das
  Vokabular der Bibliothek und kann gar kein anderes zeigen.
- **Die Spalte ist der Bibliothekseintrag** — das Teilinstrument, unter dem Namen, den die
  Bibliothek ihm gibt („singing bowl"), in der Reihenfolge, in der die Zeilen im Körper stehen.
  Drei Spalten, vier Plätze. Trägt ein Name mehrere Einträge (`ring` gehört heute vieren), gewinnt
  der Eintrag, dessen *übrige* Parameter der Körper ebenfalls trägt.
- **Der Regler ist im UI immer 0..1**, abgebildet auf `[lo..hi]` der Bibliothek. Ein einziger
  Reglertyp, keine erfundene Einheit, und die Abbildung steht dort, wo sie hingehört: in der
  Zeile, die der Host schreibt.
- **Semantisch, nicht technisch** ist damit keine Prompt-Regel mehr, sondern eine Eigenschaft der
  Bibliothek: `bowl`, `warble`, `ring`, `sung`, `width`, `age` sind kuratierte Wörter. Der
  Kurztext unter dem Regler ist der Glosse-Teil derselben Zeile — nicht `note` aus den `params`,
  das ist die Messung und läuft über einen Absatz.
- **Makros kosten nichts.** Eine Bibliotheksvariable, die im Körper an drei Stellen wirkt, IST ein
  Makro. Das Panel muss dafür nichts können.

**Spalte = Teilinstrument, nicht zwingend Layer.** Die Plattform kennt drei Layer (`kvol1..3` /
`koct1..3`, „You may layer up to THREE oscillators", `lco_write.py:487`) — BJs „max 3
instrumente/Spalten" trifft genau diese Grenze. Aber ein **Morph** („a > b") ist EINE Stimme mit
zwei Enden und nur einem `kvol1`; seine beiden Enden sind trotzdem zwei Teilinstrumente. Deshalb
zählt die Spalte, was der Autor als Teil benennt: bei „a + b" die Layer, bei „a > b" die beiden
Enden. Drei Spalten bleiben die Grenze.

**Höchstens vier Zeilen je Spalte, drei als Regel** — der Systemprompt fordert drei und lässt vier
zu; eine fünfte wird verworfen.

---

## 3. Die Leitung: wie ein Regler den Klang erreicht

Die Form existiert bereits im Orchestra-Kopf und wird weitergeführt. Der Kopf setzt heute für die
Misch- und Oktavregler eine Vorgabe und liest sie in `instr 1` zurück:

```csound
chnset 1.0000, "osc1vol"        →  kvol1  chnget "osc1vol"
```

(Nur die Form, nicht das Vorbild: `osc1vol`/`osc1oct` werden vom Plugin nie geschrieben — im
`src/`-Baum steht keine einzige Referenz darauf. Die Vorgabe im Kopf ist alles, was diese beiden
Kanäle je tragen. Für die zwölf Reglerkanäle gilt das nicht: die schreibt `processBlock` pro
Block aus dem `paramCache`.)

Genauso, mit der Zahl aus der Bibliothekszeile als Vorgabe:

```csound
chnset 0.5, "lroP1a"            →  kp1a   chnget "lroP1a"
                                →  kbowl  = 0 + 1 * kp1a   ; bowl[0.0..1.0]: …
```

Die Vorgabe im Kopf ist die **zurückgerechnete Reglerstellung** zur Zahl des Autors, und sie wird
mit allen Stellen geschrieben (`%.17g`), nicht auf vier Nachkommastellen gerundet. Vier Stellen
genügen nicht: bei `age [0..3] = 1.00` ergibt gerundetes `0.3333` über `3 * 0.3333 = 0.9999` ein
`int()` von 0 statt 1 — die Vorgabe hätte einen anderen Klang gehabt als die Zeile, aus der sie
stammt. Die Zahl ist keine Anzeige, sie ist der Klang.

- **12 feste Kanäle** (3 Spalten × 4 Plätze). Fest, weil die Orchestra-Kopfzeilen dann für jede
  Zahl von Achsen gleich aussehen und ein Offline-Render ohne Host genau das spielt, was der Autor
  geschrieben hat.
- **Ein Reglerzug schreibt den Kanal.** Kein Neu-Kompilieren, kein Neu-Schreiben, keine erneute
  Inferenz. Das ist der Unterschied zwischen einem Regler und einem neuen Prompt.
- **12 feste APVTS-Parameter** halten die Werte — damit sind sie automatisierbar, MIDI-lernbar und
  stehen im Preset. Die **Beschriftung** ist kein Parameter: sie kommt aus der Bibliothekszeile,
  die neben dem Orchestra-Text gespeichert wird.
- `CsoundEngine` löst und cacht heute 16×6 Kanalzeiger; die 12 globalen kommen mit demselben
  Helfer dazu (`CsoundEngine.cpp:565/573`). Geschrieben wird pro Block, wie die Stimmkanäle.

**Anti-Halluzination, jetzt konstruktiv statt prüfend:** die Prüfung des Entwurfs („eine `PARAM`-
Zeile, deren `kp…` niemand liest, wird verworfen") war richtig und hat auch genau das getan — sie
hat nur *alle* Regler verworfen, weil der Autor keinen einzigen verdrahtet hat. Da der Host jetzt
verdrahtet, ist die Verbindung nicht mehr etwas, das schiefgehen und dann bemerkt werden kann:
ein Regler existiert genau dann, wenn seine Zeile im Körper steht, und diese Zeile IST die
Verbindung. Verworfen wird nur noch, was die Bibliothek nicht deklariert, und was über drei
Spalten oder vier Plätze hinausgeht.

---

## 4. Das Panel

**Ort:** die Karte im ENGINE-Panel, die im LRO ohnehin nichts eigenes zu zeigen hat, plus die
untere Zeile, die durch das Hochziehen von Oktave und Rauschen frei geworden ist.

**Zwei Zustände (Skizze A und B):**

| | Karte zeigt |
|---|---|
| noch kein Orchestra geschrieben | die Bibliotheksliste, wie heute |
| Autor hat geschrieben | die `READING`-Zeile als Überschrift, darunter die Spalten |

**Aufbau:** bis zu drei Spalten nebeneinander, durch eine dünne Linie getrennt; Spaltenkopf = der
Name des Bibliothekseintrags in der Osc-Farbe; darunter 3 (höchstens 4) Zeilen. Weniger Spalten werden breiter,
nicht zentriert-schmal — eine Spalte nutzt die Karte.

**Die Zeile ist die `SliderRow` aus `src/gui/GuiHelpers.h:858`**, unverändert, wie überall sonst
im Synth: Name links, Bahn, Wert rechts, Rechtsklick = MIDI-Learn. „Einheitliches Slider-Layout"
heißt genau das — dieselbe Komponente, keine zweite Bauform (§8 der Arbeitsaufträge: das Panel
übernimmt die vorhandenen Bauteile, es erfindet keine).

---

## 5. Randfälle, ausgeschrieben

- **Der Körper trägt keine Bibliothekszeile.** Karte bleibt auf der Bibliotheksliste. Kein leeres
  Gitter, keine erfundenen Regler. Das trifft heute auch fünf Einträge, die `params` deklarieren,
  aber keine solche Zeile im Code haben (`fm_bell`, `drum_head`, `string`, `blown_bottle`,
  `driven_metal`) — das ist Bibliotheksarbeit, ein Eintrag nach dem anderen, keine Panelarbeit.
  `fm_bell` ist am 2026-08-03 gemacht: `index`, `ring` und `detune` stehen jetzt als eigene
  Zeilen im Körper, die Konstanten, die sie ohnehin waren (Spitzenindex, Verdunkelungszeit,
  Hz-Abstand der Doublette), und der Klang auf den deklarierten Vorgaben ist derselbe geblieben
  (−64 dB Rest gegen den ausgelieferten Körper). Offen bleiben `drum_head` (drei seiner vier
  Achsen haben nicht einmal ein `anchor_code`-Beispiel, zwei davon beschreiben nichts, was aus
  der Membranphysik folgt — eigene Arbeit mit Verfahren und Quelle, kein Folgeschnitt),
  `string.bow`, `blown_bottle.blow` und `driven_metal.stretch`/`drive`.
- **Der Regler steht im Körper und erreicht nichts.** Der Autor darf eine Zeile auch dann
  schreiben, wenn die Bibliothek für diese Achse keine hat — und hat sie dann selbst zu
  verdrahten. Dass die Variable GELESEN wird, ist eine Textfrage und eine Stufe zu wenig:
  gemessen am 2026-08-03 an genau so einem Fall standen `ring` und `detune` als Regler unter der
  Hand des Spielers und bewegten nichts. Seitdem entscheidet das nicht mehr der Text, sondern ein
  Render (`lco_write.gate_knobs`): das fertige Orchestra auf der Position des Autors und dann auf
  0.0 / 0.5 / 1.0. Ändert sich kein einziges Sample, wird der Regler einbehalten, die Zeile behält
  ihre eigene Zahl, und die übrigen rücken in die frei gewordenen Kanäle. Schwelle gibt es keine —
  „ändert kein Sample" braucht keine Hörtheorie; WIE VIEL eine Achse tut, entscheidet weiter BJs
  Ohr. Ohne Compiler, ohne Regler oder bei einem Körper, der sich zweimal hintereinander nicht
  gleich rendert, schweigt das Gate: fehlende Messung ist kein Urteil.
  **Gemessen wird, wo das Instrument LÄUFT**, nicht wo es bequem ist: bei sr 176400 (die
  Vorgabe-Überabtastung der LRO, nicht 44100), mit `ktimb` auf seiner Ruhelage 64/127 statt auf 0,
  und an ZWEI Betriebspunkten — 220 Hz / 4 s / vel 0.80 / kein Druck, und 880 Hz / 8 s / vel 0.35 /
  Druck 0.7 / `ktimb` 1.0. Ein Punkt kann nur „hier tot" sagen: an einem einzigen wurden ein
  `warble` hinter `ktimb`, eines hinter `kpres` und eines, das erst eine Oktave höher greift,
  einbehalten, obwohl alle drei im Plugin arbeiten. Der zweite Punkt kostet fast nichts, weil ein
  lebendiger Regler schon beim ersten Render aussteigt (0,6–1,1 s pro Klang über die zehn
  Bibliothekskörper mit Reglerzeilen).
  Drei Dinge daran sind nicht Bequemlichkeit, sondern Voraussetzung. Die beiden Punkte spielen
  **verschiedene Stimmen** (`ivoice` 1 und 5), sonst bliebe ein Körper ungemessen, der seine Achse
  über den Stimmindex verteilt. Der Gate-Kanal wird nicht auf 1 geklemmt, sondern zu einer
  **Flanke bei 0,01 s** umgeschrieben, damit `changed2(ktrig)` feuert und alles läuft, was am
  Anschlag hängt — dieselbe Mechanik, die `tools/lco_measure.py` benutzt; ohne sie schweigt jeder
  Körper, dessen Hülle auf den Anschlag zurückgesetzt wird. Und verglichen werden nicht die
  Puffer, sondern ein **sha256 über den Render** plus eine Nicht-endlich-Marke; die Puffer selbst
  sind 45–90 MB pro Punkt und lagen als Spitzenlast bei 426 MB im Backend.
  Zwei Grenzen, ausgeschrieben statt verschwiegen: ein Regler, der erst **nach 8,5 s** etwas tut,
  wird einbehalten (die längere Note verkleinert das Fenster, sie schließt es nicht). Und ein
  Körper, dessen Summe ein NaN enthält, kommt hier gar nicht als NaN an — `clip` im `_TAIL` macht
  daraus einen Gleichpegel; dann sind alle Regler wirklich tot und der Fehler liegt eine Stufe
  früher, bei einer `perform_check`, die ein Orchester durchlässt, dessen Ausgang eine Konstante
  ist.
- **Neues Orchestra.** Neue Achsen, Werte auf die neuen Vorgaben. Ein Regler von vorher hat im
  neuen Körper keine Bedeutung; ihn stehen zu lassen wäre eine stille Lüge.
- **Preset.** Namen und Vorgaben reisen mit dem Orchestra-Text im `.t5p`. Ohne das ist das
  Panel nach dem Laden leer, während der Klang weiterläuft. (Formatänderung → `docs/PRESET_FORMAT.md`.)
- **Zahl außerhalb des Bereichs.** Auf `[lo..hi]` geklemmt, mit Vermerk — dieselbe Behandlung wie
  bei einer `SET`-Zeile: die Zahl ist eine Position, der Regler ist das Ergebnis.
- **Der Autor ist ein kleines Modell.** Ob 3B/7B die zwei Zeilen zuverlässig schreiben, ist offen
  und wird gemessen, bevor gebaut wird: dieselbe Disziplin wie bei `SET`, und der Ausfallmodus ist
  gutartig (keine Zeilen → Bibliotheksliste, wie heute).

---

## 6. Was das ausdrücklich NICHT ist

Kein Katalog aller Regler aller Einträge im Voraus — das hat BJ am 2026-07-30 abgeräumt
(„es werden nicht zig parameter auf vorrat angezeigt, das geht gar nicht"). Gezeigt wird
ausschließlich, was der Autor in **diesem** Klang verdrahtet hat.

---

## 7. Was das kostet

`backend/lco_write.py` (Vertrag im Systemprompt, Parser, Kanäle im Kopf) · IPC-Nutzlast um die
Parameterliste erweitert (`docs/IPC_PROTOCOL.md`) · `CsoundEngine` um 12 globale Kanäle ·
`PluginProcessor` 12 APVTS-Parameter + Kanalschreiben pro Block · `SynthPanel` das Gitter ·
`.t5p` um die Namen. Der Csound- und der Kanalteil sind klein, der Vertragsteil ist der, der
gemessen werden muss.

---

## 8. Die vier Punkte, die der Entwurf offen ließ — entschieden

Sie standen hier als Fragen. Keine davon blockiert etwas, also gehören sie in den Entwurf, nicht
auf Deinen Tisch. Jede ist ein Satz von Dir entfernt, wieder anders zu sein.

1. **Spalte = das vom Autor benannte Teilinstrument**, nicht strikt Layer 1/2/3. Ein Morph
   („a > b") ist EINE Stimme mit einem `kvol1` und trotzdem zwei Körpern; bei strikter
   Layer-Bindung bekäme er eine Spalte für zwei Instrumente, und der zweite hätte keinen Ort.
   Drei Spalten bleiben die Grenze, weil die Plattform drei Layer kennt.
2. **Waagerechter Slider, kein Knopf.** Deine Vorgabe ist das *einheitliche* Slider-Layout, und
   die `SliderRow` in ihrer waagerechten Form ist überall sonst im Synth die Zeile: Name links,
   Bahn, Wert rechts. Ein Knopf stellt den Namen darunter und bricht genau diese Einheit. Die
   Komponente kann beides — es bleibt eine Zeile Code, falls Du Knöpfe willst.
3. **Die Bibliotheksliste verschwindet, sobald geschrieben wurde.** An ihre Stelle tritt die
   `READING`-Zeile des Autors als Überschrift. Die Liste ist Orientierung VOR dem Schreiben;
   danach ist das geschriebene Instrument das Thema der Karte, und zwei Listen übereinander
   wären wieder der Vorrats-Katalog, den Du abgeräumt hast. Der Weg zurück ist der leere
   Zustand: kein Orchester → Liste, wie heute.
4. **Der Regler startet auf dem Wert, den der Autor geschrieben hat.** Alles andere verlässt den
   Klang, den Du gerade gehört hast, beim ersten Anfassen — und die Vorgabe steht ohnehin im
   `chnset` im Kopf der Orchestra, sonst spielte ein Offline-Render etwas anderes als das Plugin.

Offen bleibt genau eins, und das ist eine Messung, keine Entscheidung: ob ein kleiner Autor die
zwei Zeilen zuverlässig schreibt (§5, letzter Punkt). Das wird gemessen, bevor gebaut wird.
