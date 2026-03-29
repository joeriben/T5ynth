# Bug-Analyse und Roadmap

## Behobene Bugs (Session 5, zweiter Durchlauf)

### 1. DCF Envelope bei Idle — GEFIXT
**Problem**: envFactor = 1-amount wenn Envelope idle → Cutoff permanent unter Base.
**Ursache**: JUCE processBlock läuft immer, Web Audio AudioParam-Verbindungen existieren nur wenn aktiv.
**Fix**: `!modEnvelope.isIdle()` Guard hinzugefügt.

### 2. modPitch nie angewendet — GEFIXT
**Problem**: Pitch-Target akkumulierte Werte, wendete sie nie an.
**Ursache**: Mein Versäumnis beim Erweitern der Mod-Targets.
**Fix**: `wavetableOsc.setFrequency(baseFreq * (1 + modPitch))` nach Akkumulation.

### 3+4. Fehlende APVTS-Parameter — GEFIXT
**Problem**: seq_division und seq_glide_time hatten keine UI-Parameter.
**Fix**: `AudioParameterChoice` für Division (1/1..1/16, default 1/8) und `AudioParameterFloat` für GlideTime (10-500ms, default 80) hinzugefügt.

### 5. barStartFlag — NICHT GEFIXT (Low Priority)
**Warum**: Braucht Backend-Integration (runSynthBackground). Erst relevant wenn Auto-Regen-Pipeline steht.

### 6. Limiter prepare() — GEFIXT
**Problem**: Hardcoded -0.3dB in prepare().
**Fix**: Auf -3.0dB korrigiert (Vorlage-Default).

### 7. Signal-Flow parallel — GEFIXT
**Problem**: Reverb bekam Delay-Output statt Original.
**Fix**: Pre-FX-Buffer kopiert, Reverb verarbeitet Kopie wet-only, Wet-Anteil wird zum Delay-Output addiert.

## JUCE vs. Web Audio: Architektur-Unterschiede

### Was JUCE BESSER kann

| Feature | Web Audio | JUCE | Vorteil |
|---------|-----------|------|---------|
| **Sample-Rate** | 44.1/48 kHz browser-abhängig | Beliebig (96/192 kHz) | Bessere Audioqualität |
| **Latenz** | 128-1024 Samples (Browser-Buffer) | ~64 Samples (ASIO/CoreAudio) | Echtzeitfähig |
| **Per-Sample DSP** | Nur via AudioWorklet (kompliziert) | Nativ im processBlock | Flexiblere Modulation |
| **Polyphonie** | Aufwändig (mehrere AudioNodes) | Trivial (Voice-Pool) | **Nächstes Feature** |
| **MIDI** | WebMIDI API (limitiert) | Native MIDI I/O | DAW-Integration |
| **Plugin-Format** | Keins (nur Standalone) | VST3/AU/AAX | Professioneller Einsatz |
| **Threading** | Single-Thread + Worker | Audio-Thread + Message-Thread | Keine UI-Blockierung |
| **Filter-Qualität** | BiquadFilter (IIR, kann instabil werden) | TPT (Topology-Preserving) | Stabil bei hoher Resonanz |

### Was das für den Synth bedeutet

1. **Per-Sample Modulation** statt AudioParam-Scheduling: In JUCE modulieren wir Filter-Cutoff per Sample, nicht per Block. Das ist **genauer** als die Vorlage.

2. **Filter-Qualität**: JUCE TPT-Filter ist numerisch stabiler als Web Audio BiquadFilter bei hoher Resonanz und schnellen Cutoff-Sweeps. Der Cascade-Modus (24dB) profitiert besonders.

3. **Polyphonie**: In Web Audio braucht jede Stimme einen kompletten AudioNode-Graph (teuer). In JUCE ist es ein einfacher Voice-Pool mit shared DSP-State.

## Limiter: JUCE-Limitation und Lösung

**Problem**: `juce::dsp::Limiter` hat nur threshold+release. Vorlage nutzt DynamicsCompressor mit knee=6, ratio=20, attack=1ms.

**Lösung**: `juce::dsp::Compressor<float>` verwenden — hat threshold, ratio, attack, release. Kein Knee, aber bei ratio=20 und attack=1ms ist das Verhalten nah genug am Brickwall-Limiter der Vorlage. Alternative: Eigener Soft-Knee-Limiter (Peak-Detector + Gain-Computer + Ballistics).

**Priorität**: Niedrig — der aktuelle Limiter funktioniert, nur leicht anders.

## Roadmap: Polyphonie

### Voraussetzungen (müssen VORHER clean sein)

1. **VCA/Envelope Gain-Staging** — aktuell sauber:
   - ampEnvelope: 0→1 (velocity×amount), RC-discharge release
   - Soft retrigger: kein Click bei schnellen Note-Ons
   - Looping: re-enter Attack bei Sustain

2. **DCF Envelope** — jetzt gefixt:
   - Subtractive sweep: Start unter Base, Peak über Base
   - Nur aktiv wenn Envelope läuft (isIdle Guard)

3. **Note-Stack** — implementiert:
   - Last-note-priority, Legato (Pitch ohne Retrigger)
   - Basis für polyphonen Stimmenzuweiser

4. **Engine-Stop** — implementiert:
   - Stop nach Release + 50ms
   - CPU-sparend: keine idle Stimmen

### Polyphonie-Architektur (Entwurf)

```
class SynthVoice {
    WavetableOscillator osc;  // oder AudioLooper
    ADSREnvelope ampEnv;
    ADSREnvelope modEnv1, modEnv2;
    LFO lfo1, lfo2;           // oder global shared
    int currentNote = -1;
    float currentVelocity = 0;
    bool active = false;

    void noteOn(int note, float velocity);
    void noteOff();
    void processSample(float& left, float& right);
};

class VoiceManager {
    static constexpr int MAX_VOICES = 8;
    std::array<SynthVoice, MAX_VOICES> voices;

    void noteOn(int note, float velocity);  // Voice stealing
    void noteOff(int note);
    void processBlock(AudioBuffer& buffer);
};
```

**Design-Entscheidungen**:
- **Shared vs. per-Voice**: Filter, Delay, Reverb, Limiter sind GLOBAL (nach Voice-Summe). LFOs können global oder per-Voice sein.
- **Voice-Stealing**: Oldest-Note oder Quietest-Voice.
- **Wavetable**: Alle Voices teilen die gleichen Frames (kein Speicher-Problem).
- **Looper**: Komplizierter — jede Voice braucht eigene Read-Position, aber shared Buffer.

**Reihenfolge**:
1. SynthVoice-Klasse extrahieren (Osc + Envelopes)
2. VoiceManager mit noteOn/noteOff
3. processBlock → Voice-Summe → Filter → FX → Limiter
4. Voice-Stealing-Policy
5. Unison/Detune (optional, nicht in Vorlage)
