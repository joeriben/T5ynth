# Das kybernetische Moment als Lenkung — ein Saatklang, sechs Rückkopplungs-Politiken

Der Loop schließt sich semantisch: SA3 generiert → CLAP beschreibt den Output gegen ein Vokabular zurück → diese Worte steuern den nächsten Prompt; das Audio trägt zusätzlich über `init_audio` (init_noise=0.5) mit. **Die Frage ist nicht „wo sitzt der Bias", sondern „auf welche Weise lässt sich der Rückkopplungs-Moment verwenden".** Bias-Aufdeckung ist nur *eine* von mehreren Verwendungen (s. e5/e4) — und die reduktivste. Die anderen — affirmative Selbst-Variation, die Prompt-B-Variationsmaschine, adversarielles Gegen-Steuern, ästhetische Abduktion — sind die eigentliche Bandbreite.

**Was geliefert wird, sind Klänge zum Hören.** Die `cos→Anker`-Zahl ist ein *Drift-Diagnostikum* (wie weit hat sich der Klang von der reinen Preset-Identität entfernt), nicht das Urteil — das Urteil fällt am Ohr. WAVs liegen unter jedem Experiment-Verzeichnis.

Generationsparameter VERBATIM aus dem Preset (kein hartcodiertes duration/seed):

- Preset: **Creamy-Dreamy SA3**, Modell `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, magnitude 1.0, noise_sigma 0.0, injection `linear`
- **seed 2128708858 über alle Iterationen FIX** (Preset randomSeed=off) → nur Worte + init_audio treiben den Drift, nicht Seed-Rauschen

**Saat-Kollision (Identität, jede Iteration fix): A=`creamy cream, birds chirping` × B=`dreamy dream` bei α=+0.005.** Wo nicht anders vermerkt, wird die vorige Maschinen-Hörung an jeden Pol ANGEHÄNGT (`pole, qualities`); die Saat-Identität führt, die Re-Hörung moduliert. Iter 1 ist der reine Preset-Render (der Drift-Anker).

## Die sechs Verwendungen im Überblick

| # | Verzeichnis | Verwendung | Politik | cos→Anker (it10) |
|---|---|---|---|---:|
| 1 | run1_both | **Affirmative Selbst-Variation** (Proto-Homöostase) | Tags → BEIDE Pole, α=ALPHA0 | +0.782 |
| 2 | run2_onlyB_null | **Kontrolle / nackter Signal-Loop** | nur B, α=0 → Null-Kollaps | +0.898 |
| 3 | e6_driftB | **Variationsmaschine Prompt B** | A fix = Anker, nur B driftet | +0.925 |
| 4 | e3_counter | **Adversarielles Gegen-Steuern** | Antonym des TOP-Readings → beide Pole | +0.717 |
| 5 | e5_audioset | **Vokabular als Lenkrad** (Menü-Wahl ist *auch* ein kritischer Akt — eine Dimension) | quell-gerahmtes AudioSet-Menü | +0.889 |
| 6 | e4_two_ears | **Ästhetische Abduktion** (zwei Ohren bringen Worte ein, die nicht im Saatklang stehen) | unfused→A, music→B | +0.864 |

**Quer-Befund:** ein Saatklang, sechs Rückkopplungs-Politiken, sechs Ziele — aber alle Ziele bleiben in der **sanften dreamy/mellow/crystalline-Familie** (cos 0.72–0.93). Es gibt keinen metallischen Attraktor; der dramatische Unterschied früherer Läufe war ein **Dauer-Artefakt** (3s vs. die echten 11s), nicht „die Maschine". Das kybernetische Moment ist **Lenkung, nicht Effekt**: was man zurückspeist, entscheidet das Ziel.

---

## run1_both — Affirmative Selbst-Variation

*Qualities an BEIDE Pole angehängt, α=ALPHA0. Überlebt die Kollision ihre eigene Re-Hörung? → Ja, mild: sie variiert um sich selbst, ohne wegzulaufen.*

vocab size: 112 · CLAP cosine zu iter-1 Anker: +1.000 → +0.782 (Drift +0.218).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.48), mellow (0.44), crystalline (0.44) | +1.000 |
| 2 | dreamy, mellow, crystalline | dreamy (0.44), crystalline (0.39), mellow (0.39) | +0.931 |
| 3 | dreamy, crystalline, mellow | dreamy (0.50), mellow (0.49), delicate (0.39) | +0.929 |
| 4 | dreamy, mellow, delicate | mellow (0.46), dreamy (0.45), glittering (0.40) | +0.900 |
| 5 | mellow, dreamy, glittering | dreamy (0.53), mellow (0.49), delicate (0.42) | +0.877 |
| 6 | dreamy, mellow, delicate | dreamy (0.51), mellow (0.46), crystalline (0.37) | +0.877 |
| 7 | dreamy, mellow, crystalline | mellow (0.47), dreamy (0.46), delicate (0.39) | +0.922 |
| 8 | mellow, dreamy, delicate | mellow (0.48), dreamy (0.43), delicate (0.40) | +0.914 |
| 9 | mellow, dreamy, delicate | mellow (0.39), dreamy (0.38), delicate (0.30) | +0.830 |
| 10 | mellow, dreamy, delicate | mellow (0.45), dreamy (0.36), gloomy (0.31) | +0.782 |

## run2_onlyB_null — Kontrolle / nackter Signal-Loop

*Nur B = Pol-B + qualities, α=0 → der Blend hebt sich zu **null** auf, der A-Pol fällt weg. Der bloße init_audio-Signal-Loop. Die Kontrolle: die Lücke zu run1 ist, was die Worte tun.*

vocab size: 112 · CLAP cosine zu iter-1 Anker: +1.000 → +0.898 (Drift +0.102).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.50), mellow (0.43), crystalline (0.40) | +1.000 |
| 2 | dreamy, mellow, crystalline (→null) | dreamy (0.54), glassy (0.42), crystalline (0.42) | +0.921 |
| 3 | dreamy, glassy, crystalline (→null) | dreamy (0.52), crystalline (0.43), glassy (0.42) | +0.914 |
| 4 | dreamy, crystalline, glassy (→null) | dreamy (0.52), crystalline (0.47), glassy (0.46) | +0.898 |
| 5 | dreamy, crystalline, glassy (→null) | dreamy (0.50), crystalline (0.48), glassy (0.46) | +0.914 |
| 6 | dreamy, crystalline, glassy (→null) | dreamy (0.49), glassy (0.46), crystalline (0.46) | +0.866 |
| 7 | dreamy, glassy, crystalline (→null) | dreamy (0.51), crystalline (0.48), delicate (0.47) | +0.882 |
| 8 | dreamy, crystalline, delicat (→null) | dreamy (0.46), crystalline (0.45), delicate (0.44) | +0.868 |
| 9 | dreamy, crystalline, delicat (→null) | dreamy (0.48), crystalline (0.47), delicate (0.42) | +0.886 |
| 10 | dreamy, crystalline, delicat (→null) | crystalline (0.50), dreamy (0.50), glassy (0.44) | +0.898 |

> Lesart: run2 driftet sogar *weniger* als run1 — der reine Signal-Loop hält die Identität fester, die Worte (run1) schieben aktiv. Das ist der Beleg „die Worte tun Arbeit", neutral formuliert.

## e6_driftB — Variationsmaschine Prompt B

*ASYMMETRISCH: Pol A bleibt rein (menschlicher Anker), nur Pol B driftet unter der Maschinen-Hörung. Kollision = stabiler Anker vs. ohr-getriebene Variation. **Dies ist der Lauf-Beleg für das vom User bestätigte UI-Feature: Prompt B = semantische Variationsmaschine.***

vocab size: 112 · CLAP cosine zu iter-1 Anker: +1.000 → +0.925 (Drift +0.075).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.45), mellow (0.40), glittering (0.36) | +1.000 |
| 2 | dreamy, mellow, glittering | dreamy (0.49), mellow (0.44), crystalline (0.39) | +0.930 |
| 3 | dreamy, mellow, crystalline | dreamy (0.49), mellow (0.45), glittering (0.39) | +0.860 |
| 4 | dreamy, mellow, glittering | dreamy (0.51), mellow (0.46), glassy (0.46) | +0.865 |
| 5 | dreamy, mellow, glassy | dreamy (0.54), crystalline (0.46), mellow (0.45) | +0.874 |
| 6 | dreamy, crystalline, mellow | dreamy (0.51), mellow (0.46), crystalline (0.44) | +0.897 |
| 7 | dreamy, mellow, crystalline | dreamy (0.46), glassy (0.42), crystalline (0.42) | +0.910 |
| 8 | dreamy, glassy, crystalline | dreamy (0.51), mellow (0.46), crystalline (0.45) | +0.884 |
| 9 | dreamy, mellow, crystalline | dreamy (0.52), mellow (0.45), delicate (0.42) | +0.871 |
| 10 | dreamy, mellow, delicate | dreamy (0.45), mellow (0.43), crystalline (0.40) | +0.925 |

## e3_counter — Adversarielles Gegen-Steuern

*ADVERSARIELL (die vom /goal verlangte Richtung). Lies die TOP-k der Maschine, hänge das ANTONYM jeder Lesart an BEIDE Pole (α=ALPHA0). 'qualities fed' = Antonyme, mit denen wir gegensteuern; 'machine hears' = was sie trotzdem liest. Befund: die weiche Identität WIDERSTEHT der Härte — weiches init_audio lässt sich vom Wort „screaming" nicht überschreiben; man kann gegen das Ohr argumentieren, aber nicht „gewinnen".*

vocab size: 112 · CLAP cosine zu iter-1 Anker: +1.000 → +0.717 (Drift +0.283 — der weiteste der symmetrischen Läufe).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.46), mellow (0.38), crystalline (0.38) | +1.000 |
| 2 | percussive, screaming | dreamy (0.46), crystalline (0.40), glittering (0.38) | +0.940 |
| 3 | percussive, robotic | mellow (0.43), dreamy (0.42), crystalline (0.36) | +0.899 |
| 4 | screaming, percussive | dreamy (0.48), mellow (0.46), gloomy (0.42) | +0.895 |
| 5 | percussive, screaming | mellow (0.43), dreamy (0.41), crystalline (0.37) | +0.893 |
| 6 | screaming, percussive | mellow (0.50), dreamy (0.42), delicate (0.38) | +0.866 |
| 7 | screaming, percussive, punchy | mellow (0.49), dreamy (0.36), delicate (0.35) | +0.745 |
| 8 | screaming, percussive, punchy | mellow (0.46), gloomy (0.29), dreamy (0.29) | +0.693 |
| 9 | screaming, percussive | mellow (0.42), dreamy (0.28), velvety (0.27) | +0.701 |
| 10 | screaming, percussive, humming | mellow (0.35), dreamy (0.27), velvety (0.22) | +0.717 |

## e5_audioset — Vokabular als Lenkrad

*Kollisions-verankert, aber das QUELL-GERAHMTE AudioSet-Menü statt des Timbre-Menüs. Das Menü ist eine Stellschraube — und seine Wahl ist auch ein kritischer Akt (eine Dimension von mehreren). Hier KEINE Gewalt: die Ontologie hat für einen warmen Pad passende sanfte Kategorien (Electronic organ/Lullaby/Mellotron). Die frühere „Train horn/Donkey"-Politik war das 3s-Artefakt.*

vocab size: 632 · CLAP cosine zu iter-1 Anker: +1.000 → +0.889 (Drift +0.111).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | Electronic organ (0.48), Lullaby (0.47), Mellotron (0.45) | +1.000 |
| 2 | Electronic organ, Lullaby, Mellotr | Electronic organ (0.42), Lullaby (0.39), Mellotron (0.38) | +0.941 |
| 3 | Electronic organ, Lullaby, Mellotr | Mellotron (0.45), Lullaby (0.45), Musical instrument (0.44) | +0.933 |
| 4 | Mellotron, Lullaby, Musical instru | Electronic organ (0.51), Musical instrument (0.50), Mellotron (0.47) | +0.881 |
| 5 | Electronic organ, Musical instrume | Lullaby (0.52), Musical instrument (0.49), Electronic organ (0.47) | +0.940 |
| 6 | Lullaby, Musical instrument, Elect | Musical instrument (0.50), Electronic organ (0.47), Organ (0.44) | +0.907 |
| 7 | Musical instrument, Electronic org | Electronic organ (0.50), Organ (0.48), Musical instrument (0.46) | +0.900 |
| 8 | Electronic organ, Organ, Musical i | Electronic organ (0.52), Organ (0.52), Musical instrument (0.47) | +0.864 |
| 9 | Electronic organ, Organ, Musical i | Electronic organ (0.50), Organ (0.47), Musical instrument (0.45) | +0.889 |
| 10 | Electronic organ, Organ, Musical i | Electronic organ (0.50), Musical instrument (0.48), Organ (0.48) | +0.889 |

## e4_two_ears — Ästhetische Abduktion

*Zwei Ohren, jedes an einen anderen Pol gehängt (unfused→A, music→B). Nicht „zwei westliche Ohren = der Bias" (das war Slop) — sondern: die music-CLAP bringt Worte ins Spiel, die im Saatklang gar nicht stehen (brassy, warm, evolving). Die Maschine SCHLÄGT einen ästhetischen Rahmen VOR, den der Mensch nicht geschrieben hat = Abduktion. Dass die zwei Ohren denselben Klang verschieden hören, ist der Stoff dafür.*

vocab size: 112 · CLAP cosine zu iter-1 Anker: +1.000 → +0.864 (Drift +0.136).

| iter | A / B (pole + each ear's tag) | unfused hears | music hears | cos→anchor |
|---:|---|---|---|---:|
| 1 | creamy cream, birds chirping / dreamy dream | dreamy, mellow, crystalline | brassy, brilliant, crystalline | +1.000 |
| 2 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, crystalline, glassy | warm, brassy, brilliant | +0.938 |
| 3 | creamy cream, birds chirping / dreamy dream, warm | dreamy, mellow, crystalline | warm, brassy, evolving | +0.901 |
| 4 | creamy cream, birds chirping / dreamy dream, warm | dreamy, glassy, mellow | delicate, warm, hollow | +0.920 |
| 5 | creamy cream, birds chirping / dreamy dream, delicate | dreamy, mellow, crystalline | brassy, delicate, warm | +0.907 |
| 6 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, crystalline, mellow | brassy, delicate, warm | +0.838 |
| 7 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, mellow, crystalline | delicate, brassy, warm | +0.916 |
| 8 | creamy cream, birds chirping / dreamy dream, delicate | dreamy, mellow, crystalline | brassy, delicate, earthy | +0.891 |
| 9 | creamy cream, birds chirping / dreamy dream, brassy | crystalline, dreamy, mellow | brassy, delicate, warm | +0.946 |
| 10 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, mellow, gloomy | brassy, brilliant, crystalline | +0.864 |

---

## Vorbehalt & nächster Schritt

- Gemessen ist der **CLAP-Raum + die Worte**, nicht der Klang. Den sonischen Wert entscheidet die User-Audition der WAVs.
- `init_noise`/CFG = Arbeitspunkt (höheres CFG = Worte lenken stärker; niedrigeres init_noise = das Signal trägt mehr).
- **Offene Richtung:** bisher wird die CLAP-Hörung als *roher Tag-Append* zurückgespeist (CLAP allein). Die Modi *Variation / Kritik / Abduktion / Entwicklungskette* brauchen einen **Interpreten** zwischen Ohr und nächstem Prompt — wofür das im Plugin bereits vorhandene Übersetzungs-LLM (kann mehr als übersetzen) in Frage kommt: „CLAP = Ohr, LLM = Interpret".
