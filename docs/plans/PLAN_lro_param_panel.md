# Die Parameter des geschriebenen Instruments im LRO-Panel — ENTWURF

**Status:** Entwurf, vollständig. Keine Implementation — die beginnt erst auf Dein Wort.
Skizze: `docs/plans/lro_param_panel_sketch.svg` (A = Karte ohne Orchester, B = die Parameterspalten,
C = wie eine `PARAM`-Zeile zum Kanal wird). §8 hält die vier Punkte fest, die der erste Entwurf
offengelassen hatte; sie sind jetzt entschieden und begründet, nicht als Fragen zurückgereicht.

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

**(a) Die `params` der Bibliothekseinträge, die der Autor aufgeschlagen hat.** Falsch. Der Autor
*adaptiert und kombiniert* — der geschriebene Körper ist nicht der Eintrag. Ein Regler „bowl",
weil `singing_bowl` aufgeschlagen war, während im Code gar keine Schalenreihe mehr steht, ist
exakt die Halluzination. Die Bibliothek ist Orientierung, kein Menü
([[project_lco_llm_authors_csound]]) — und deshalb auch keine Parameterquelle.

**(b) Den emittierten Csound nach `k…`-Zuweisungen absuchen.** Falsch. Ein Körper hat 20 bis 60
k-Variablen; welche davon eine *Achse* ist, was sie bedeutet und welcher Bereich sicher ist, ist
aus dem Code nicht ablesbar. Die Kommentar-Konvention der Bibliothek (`kbowl = 0.50 ;
bowl[0.0..1.0]: …`) gilt für die **kuratierten** Einträge, nicht für frei geschriebenen Code —
der Autor ist auf nichts dergleichen verpflichtet. Raten wäre hier Raten.

**(c) Der Autor erklärt sie selbst.** Richtig, und die einzige Quelle, die etwas *weiß*: er hat
den Körper geschrieben, er weiß, welche drei Größen ihn steuern, wie sie in der Sprache des
Spielers heißen und welcher Bereich trägt. Er erklärt bereits zwei Dinge im Fence — die
`READING`-Zeile und die `SET`-Zeilen — und für die gilt schon die richtige Disziplin:
*„Anything else is refused rather than guessed at"* (`read_settings`, `backend/lco_write.py:626`).

**Entwurfsentscheidung: (c).** Alles, was das Panel zeigt, steht wörtlich in der Antwort des
Autors. Das Plugin erfindet keinen Namen, keinen Bereich und keine Vorgabe.

---

## 2. Der Vertrag: zwei neue Fence-Zeilen

Neben `READING:` und `SET:` darf die Antwort tragen:

```
LAYER: <1..3> "<Name des Teilinstruments>"
PARAM: <1..3><a..d> "<Name des Reglers>" = <Vorgabe 0..1> ; <ein Satz, was er tut>
```

- **`LAYER`** ist die Spaltenüberschrift. Sie benennt das *Teilinstrument*, nicht die Technik.
- **`PARAM`** ist eine Zeile in dieser Spalte. Die Ziffer ist die Spalte, der Buchstabe der Platz
  darin: `1a` bis `3d`.
- **Der Wertebereich ist immer 0..1.** Absichtlich, und es ist die zweite tragende Entscheidung
  nach (c): ein einziger Reglertyp für alles, keine erfundene Einheit im UI, keine Skala, die
  irgendwo falsch gerundet wird. Die Abbildung auf Hz, Q, Index, Verhältnis steht dort, wo sie
  hingehört — im Code des Autors: `kbow = 0.10 + 3.90 * kp1a`.
- **Makros kosten nichts.** Ein `kp1a`, das der Autor an drei Stellen einsetzt, IST ein Makro.
  Das Panel muss dafür nichts können; die Freiheit liegt beim Schreiber, wo sie hingehört.
- **Semantisch, nicht technisch** steht als Regel im Systemprompt, mit Beispielen: „Bow pressure",
  „Breath", „Metal" — nicht „kbow", „Index", „Q", „Ratio". Ein Name, den ein Spieler nicht liest,
  ist falsch (BJ, 2026-07-28).

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

Der Mechanismus existiert bereits und wird nur weitergeführt. Die Orchestra bekommt heute schon
globale Kanäle mit Vorgabewerten im Kopf (`lco_write.py:1464`):

```csound
chnset 1.0000, "osc1vol"        →  kvol1  chnget "osc1vol"
```

Genauso, mit der Vorgabe aus der `PARAM`-Zeile:

```csound
chnset 0.4200, "lroP1a"         →  kp1a   chnget "lroP1a"
```

- **12 feste Kanäle** (3 Spalten × 4 Plätze). Fest, weil die Orchestra-Kopfzeilen dann für jede
  Zahl von Achsen gleich aussehen und ein Offline-Render ohne Host genau das spielt, was der Autor
  geschrieben hat.
- **Ein Reglerzug schreibt den Kanal.** Kein Neu-Kompilieren, kein Neu-Schreiben, keine erneute
  Inferenz. Das ist der Unterschied zwischen einem Regler und einem neuen Prompt.
- **12 feste APVTS-Parameter** halten die Werte — damit sind sie automatisierbar, MIDI-lernbar und
  stehen im Preset. Die **Beschriftung** ist kein Parameter: sie kommt aus den `PARAM`-Zeilen, die
  neben dem Orchestra-Text gespeichert werden.
- `CsoundEngine` löst und cacht heute 16×6 Kanalzeiger; die 12 globalen kommen mit demselben
  Helfer dazu (`CsoundEngine.cpp:565/573`). Geschrieben wird pro Block, wie die Stimmkanäle.

**Anti-Halluzinations-Prüfung auf der Maschinenseite:** eine `PARAM`-Zeile, deren `kp…` im Körper
nirgends gelesen wird, wird **verworfen** — ein Regler, der nichts bewegt, ist genau der Defekt,
den das Ganze ausschließen soll. Umgekehrt: ein `kp…` im Körper ohne `PARAM`-Zeile bekommt keinen
Regler, sondern steht auf seiner Vorgabe.

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
`LAYER`-Name in der Osc-Farbe; darunter 3 (höchstens 4) Zeilen. Weniger Spalten werden breiter,
nicht zentriert-schmal — eine Spalte nutzt die Karte.

**Die Zeile ist die `SliderRow` aus `src/gui/GuiHelpers.h:858`**, unverändert, wie überall sonst
im Synth: Name links, Bahn, Wert rechts, Rechtsklick = MIDI-Learn. „Einheitliches Slider-Layout"
heißt genau das — dieselbe Komponente, keine zweite Bauform (§8 der Arbeitsaufträge: das Panel
übernimmt die vorhandenen Bauteile, es erfindet keine).

---

## 5. Randfälle, ausgeschrieben

- **Autor erklärt nichts.** Karte bleibt auf der Bibliotheksliste. Kein leeres Gitter, keine
  erfundenen Regler.
- **Neues Orchestra.** Neue Achsen, Werte auf die neuen Vorgaben. Ein Regler von vorher hat im
  neuen Körper keine Bedeutung; ihn stehen zu lassen wäre eine stille Lüge.
- **Preset.** `PARAM`-Namen und -Vorgaben reisen mit dem Orchestra-Text im `.t5p`. Ohne das ist das
  Panel nach dem Laden leer, während der Klang weiterläuft. (Formatänderung → `docs/PRESET_FORMAT.md`.)
- **Kaputte Zeile** (Bereich außerhalb 0..1, Platz `1e`, Name leer, doppelter Platz): verworfen wie
  eine kaputte `SET`-Zeile, mit Vermerk in der Antwort — nie repariert, nie geraten.
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
