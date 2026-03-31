# Portierungstabelle: Vue-Referenz → JUCE

3 Spalten: Nr | Original-Code (TypeScript) | Neuer Code (C++)
Rechte Spalte wird später gefüllt. Jede Zeile muss mathematisch identisch sein.

---

## 1. useAudioLooper.ts

### 1.1 Konstanten

| # | Original-Code | JUCE |
|---|---|---|
| 1 | `const SCHEDULE_AHEAD = 0.005` | |
| 2 | `const XCORR_WINDOW = 512` | |
| 3 | `const XCORR_SEARCH = 2000` | |

### 1.2 encodeWav(buffer, startSample, endSample)

| # | Original-Code | JUCE |
|---|---|---|
| 4 | `function encodeWav(buffer: AudioBuffer, startSample: number, endSample: number): Blob {` | |
| 5 | `  const nc = buffer.numberOfChannels, sr = buffer.sampleRate` | |
| 6 | `  const len = endSample - startSample, ds = len * nc * 2` | |
| 7 | `  const ab = new ArrayBuffer(44 + ds), v = new DataView(ab)` | |
| 8 | `  const ws = (o: number, s: string) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)) }` | |
| 9 | `  ws(0, 'RIFF'); v.setUint32(4, 36 + ds, true); ws(8, 'WAVE'); ws(12, 'fmt ')` | |
| 10 | `  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, nc, true)` | |
| 11 | `  v.setUint32(24, sr, true); v.setUint32(28, sr * nc * 2, true)` | |
| 12 | `  v.setUint16(32, nc * 2, true); v.setUint16(34, 16, true); ws(36, 'data'); v.setUint32(40, ds, true)` | |
| 13 | `  const chs: Float32Array[] = []` | |
| 14 | `  for (let c = 0; c < nc; c++) chs.push(buffer.getChannelData(c))` | |
| 15 | `  let off = 44` | |
| 16 | `  for (let i = startSample; i < endSample; i++) {` | |
| 17 | `    for (let c = 0; c < nc; c++) {` | |
| 18 | `      const s = Math.max(-1, Math.min(1, chs[c]![i]!))` | |
| 19 | `      v.setInt16(off, s < 0 ? s * 0x8000 : s * 0x7fff, true); off += 2` | |
| 20 | `    }` | |
| 21 | `  }` | |
| 22 | `  return new Blob([ab], { type: 'audio/wav' })` | |
| 23 | `}` | |

### 1.3 optimizeLoopEndSample(data, loopStart, loopEnd)

| # | Original-Code | JUCE |
|---|---|---|
| 24 | `function optimizeLoopEndSample(data: Float32Array, loopStart: number, loopEnd: number): number {` | |
| 25 | `  const win = Math.min(XCORR_WINDOW, Math.floor((loopEnd - loopStart) / 4))` | |
| 26 | `  if (win < 16) return loopEnd` | |
| 27 | `  const searchLo = Math.max(loopStart + win * 2, loopEnd - XCORR_SEARCH)` | |
| 28 | `  const searchHi = Math.min(data.length, loopEnd + XCORR_SEARCH)` | |
| 29 | `  let bestCorr = -Infinity` | |
| 30 | `  let bestEnd = loopEnd` | |
| 31 | `  for (let cand = searchLo; cand < searchHi; cand++) {` | |
| 32 | `    const eStart = cand - win` | |
| 33 | `    if (eStart < loopStart) continue` | |
| 34 | `    let sum = 0, normA = 0, normB = 0` | |
| 35 | `    for (let i = 0; i < win; i++) {` | |
| 36 | `      const a = data[loopStart + i]!` | |
| 37 | `      const b = data[eStart + i]!` | |
| 38 | `      sum += a * b` | |
| 39 | `      normA += a * a` | |
| 40 | `      normB += b * b` | |
| 41 | `    }` | |
| 42 | `    const denom = Math.sqrt(normA * normB)` | |
| 43 | `    const corr = denom > 0 ? sum / denom : 0` | |
| 44 | `    if (corr > bestCorr) {` | |
| 45 | `      bestCorr = corr` | |
| 46 | `      bestEnd = cand` | |
| 47 | `    }` | |
| 48 | `  }` | |
| 49 | `  return bestEnd` | |
| 50 | `}` | |

### 1.4 applyLoopProcessing(ac, source, loopStart, loopEnd, optimize, crossfadeMs)

| # | Original-Code | JUCE |
|---|---|---|
| 51 | `function applyLoopProcessing(ac: AudioContext, source: AudioBuffer, loopStart: number, loopEnd: number, optimize: boolean, crossfadeMs: number): { buffer: AudioBuffer; optimizedEnd: number; fadeSamples: number } {` | |
| 52 | `  const copy = ac.createBuffer(source.numberOfChannels, source.length, source.sampleRate)` | |
| 53 | `  for (let ch = 0; ch < source.numberOfChannels; ch++) {` | |
| 54 | `    copy.getChannelData(ch).set(source.getChannelData(ch))` | |
| 55 | `  }` | |
| 56 | `  let actualEnd = loopEnd` | |
| 57 | `  if (optimize && source.numberOfChannels > 0) {` | |
| 58 | `    actualEnd = optimizeLoopEndSample(source.getChannelData(0), loopStart, loopEnd)` | |
| 59 | `  }` | |
| 60 | `  const loopLen = actualEnd - loopStart` | |
| 61 | `  const fadeSamples = Math.min(` | |
| 62 | `    Math.floor(crossfadeMs / 1000 * source.sampleRate),` | |
| 63 | `    Math.floor(loopLen / 2),` | |
| 64 | `  )` | |
| 65 | `  if (fadeSamples >= 2) {` | |
| 66 | `    for (let ch = 0; ch < copy.numberOfChannels; ch++) {` | |
| 67 | `      const d = copy.getChannelData(ch)` | |
| 68 | `      for (let i = 0; i < fadeSamples; i++) {` | |
| 69 | `        const t = i / fadeSamples` | |
| 70 | `        const gHead = Math.sin(t * Math.PI * 0.5)` | |
| 71 | `        const gTail = Math.cos(t * Math.PI * 0.5)` | |
| 72 | `        const headIdx = loopStart + i` | |
| 73 | `        const tailIdx = actualEnd - fadeSamples + i` | |
| 74 | `        d[headIdx] = d[headIdx]! * gHead + d[tailIdx]! * gTail` | |
| 75 | `      }` | |
| 76 | `    }` | |
| 77 | `    actualEnd -= fadeSamples` | |
| 78 | `  }` | |
| 79 | `  return { buffer: copy, optimizedEnd: actualEnd, fadeSamples }` | |
| 80 | `}` | |

### 1.5 createPalindromeBuffer(ac, source, loopStart, loopEnd)

| # | Original-Code | JUCE |
|---|---|---|
| 81 | `function createPalindromeBuffer(ac: AudioContext, source: AudioBuffer, loopStart: number, loopEnd: number): { buffer: AudioBuffer; palindromeEnd: number } {` | |
| 82 | `  const loopLen = loopEnd - loopStart` | |
| 83 | `  if (loopLen < 4) return { buffer: source, palindromeEnd: loopEnd }` | |
| 84 | `  const reverseLen = loopLen - 2` | |
| 85 | `  const newLen = source.length + reverseLen` | |
| 86 | `  const result = ac.createBuffer(source.numberOfChannels, newLen, source.sampleRate)` | |
| 87 | `  for (let ch = 0; ch < source.numberOfChannels; ch++) {` | |
| 88 | `    const src = source.getChannelData(ch)` | |
| 89 | `    const dst = result.getChannelData(ch)` | |
| 90 | `    for (let i = 0; i < loopEnd; i++) dst[i] = src[i]!` | |
| 91 | `    for (let i = 0; i < reverseLen; i++) {` | |
| 92 | `      dst[loopEnd + i] = src[loopEnd - 2 - i]!` | |
| 93 | `    }` | |
| 94 | `    for (let i = loopEnd; i < src.length; i++) {` | |
| 95 | `      dst[i + reverseLen] = src[i]!` | |
| 96 | `    }` | |
| 97 | `  }` | |
| 98 | `  return { buffer: result, palindromeEnd: loopEnd + reverseLen }` | |
| 99 | `}` | |

### 1.6 Composable State

| # | Original-Code | JUCE |
|---|---|---|
| 100 | `let ctx: AudioContext \| null = null` | |
| 101 | `let activeSource: AudioBufferSourceNode \| null = null` | |
| 102 | `let activeGain: GainNode \| null = null` | |
| 103 | `let originalBuffer: AudioBuffer \| null = null` | |
| 104 | `let rawBase64: string \| null = null` | |
| 105 | `let destinationNode: AudioNode \| null = null` | |
| 106 | `const isPlaying = ref(false)` | |
| 107 | `const isLooping = ref(false)` | |
| 108 | `const transposeSemitones = ref(0)` | |
| 109 | `const loopStartFrac = ref(0)` | |
| 110 | `const loopEndFrac = ref(1)` | |
| 111 | `const bufferDuration = ref(0)` | |
| 112 | `const hasAudio = ref(false)` | |
| 113 | `const crossfadeMs = ref(150)` | |
| 114 | `const normalizeOn = ref(true)` | |
| 115 | `const peakAmplitude = ref(0)` | |
| 116 | `const loopOptimize = ref(false)` | |
| 117 | `const loopPingPong = ref(false)` | |
| 118 | `const optimizedEndFrac = ref(1)` | |
| 119 | `let preparedLoopStartSec = 0` | |
| 120 | `let preparedLoopEndSec = 0` | |
| 121 | `let preparedColdStartSec = 0` | |

### 1.7 rateForPlayback()

| # | Original-Code | JUCE |
|---|---|---|
| 122 | `function rateForPlayback(): number {` | |
| 123 | `  return Math.pow(2, transposeSemitones.value / 12)` | |
| 124 | `}` | |

### 1.8 loopBoundsSamples(buf)

| # | Original-Code | JUCE |
|---|---|---|
| 125 | `function loopBoundsSamples(buf: AudioBuffer): [number, number] {` | |
| 126 | `  return [` | |
| 127 | `    Math.floor(loopStartFrac.value * buf.length),` | |
| 128 | `    Math.min(buf.length, Math.ceil(loopEndFrac.value * buf.length)),` | |
| 129 | `  ]` | |
| 130 | `}` | |

### 1.9 measurePeak(buffer)

| # | Original-Code | JUCE |
|---|---|---|
| 131 | `function measurePeak(buffer: AudioBuffer): number {` | |
| 132 | `  let peak = 0` | |
| 133 | `  for (let ch = 0; ch < buffer.numberOfChannels; ch++) {` | |
| 134 | `    const d = buffer.getChannelData(ch)` | |
| 135 | `    for (let i = 0; i < d.length; i++) {` | |
| 136 | `      const a = Math.abs(d[i]!)` | |
| 137 | `      if (a > peak) peak = a` | |
| 138 | `    }` | |
| 139 | `  }` | |
| 140 | `  return peak` | |
| 141 | `}` | |

### 1.10 normalizeBuffer(buffer)

| # | Original-Code | JUCE |
|---|---|---|
| 142 | `function normalizeBuffer(buffer: AudioBuffer): void {` | |
| 143 | `  const peak = measurePeak(buffer)` | |
| 144 | `  if (peak < 0.001) return` | |
| 145 | `  const g = 0.95 / peak` | |
| 146 | `  for (let ch = 0; ch < buffer.numberOfChannels; ch++) {` | |
| 147 | `    const d = buffer.getChannelData(ch)` | |
| 148 | `    for (let i = 0; i < d.length; i++) d[i]! *= g` | |
| 149 | `  }` | |
| 150 | `}` | |

### 1.11 prepareBuffer(ac, source)

| # | Original-Code | JUCE |
|---|---|---|
| 151 | `function prepareBuffer(ac: AudioContext, source: AudioBuffer): AudioBuffer {` | |
| 152 | `  const [ls, le] = loopBoundsSamples(source)` | |
| 153 | `  const sr = source.sampleRate` | |
| 154 | `  let processed: AudioBuffer` | |
| 155 | `  if (loopPingPong.value) {` | |
| 156 | `    let actualEnd = le` | |
| 157 | `    if (loopOptimize.value && source.numberOfChannels > 0) {` | |
| 158 | `      actualEnd = optimizeLoopEndSample(source.getChannelData(0), ls, le)` | |
| 159 | `    }` | |
| 160 | `    const copy = ac.createBuffer(source.numberOfChannels, source.length, sr)` | |
| 161 | `    for (let ch = 0; ch < source.numberOfChannels; ch++) {` | |
| 162 | `      copy.getChannelData(ch).set(source.getChannelData(ch))` | |
| 163 | `    }` | |
| 164 | `    const { buffer: palindrome, palindromeEnd } = createPalindromeBuffer(ac, copy, ls, actualEnd)` | |
| 165 | `    optimizedEndFrac.value = actualEnd / source.length` | |
| 166 | `    preparedLoopStartSec = ls / sr` | |
| 167 | `    preparedLoopEndSec = palindromeEnd / sr` | |
| 168 | `    preparedColdStartSec = preparedLoopStartSec` | |
| 169 | `    processed = palindrome` | |
| 170 | `  } else {` | |
| 171 | `    const { buffer: loopProcessed, optimizedEnd, fadeSamples } = applyLoopProcessing(ac, source, ls, le, loopOptimize.value, crossfadeMs.value)` | |
| 172 | `    optimizedEndFrac.value = optimizedEnd / source.length` | |
| 173 | `    preparedLoopStartSec = ls / sr` | |
| 174 | `    preparedLoopEndSec = optimizedEnd / sr` | |
| 175 | `    preparedColdStartSec = (ls + fadeSamples) / sr` | |
| 176 | `    processed = loopProcessed` | |
| 177 | `  }` | |
| 178 | `  if (normalizeOn.value) normalizeBuffer(processed)` | |
| 179 | `  return processed` | |
| 180 | `}` | |

### 1.12 createSource(ac, buffer)

| # | Original-Code | JUCE |
|---|---|---|
| 181 | `function createSource(ac: AudioContext, buffer: AudioBuffer): AudioBufferSourceNode {` | |
| 182 | `  const src = ac.createBufferSource()` | |
| 183 | `  src.buffer = buffer` | |
| 184 | `  src.loop = isLooping.value` | |
| 185 | `  src.playbackRate.value = rateForPlayback()` | |
| 186 | `  src.loopStart = preparedLoopStartSec` | |
| 187 | `  src.loopEnd = preparedLoopEndSec` | |
| 188 | `  return src` | |
| 189 | `}` | |

### 1.13 startSource(ac, playBuffer)

| # | Original-Code | JUCE |
|---|---|---|
| 190 | `function startSource(ac: AudioContext, playBuffer: AudioBuffer) {` | |
| 191 | `  const newGain = ac.createGain()` | |
| 192 | `  newGain.gain.value = 0` | |
| 193 | `  newGain.connect(destinationNode ?? ac.destination)` | |
| 194 | `  const newSource = createSource(ac, playBuffer)` | |
| 195 | `  newSource.connect(newGain)` | |
| 196 | `  const now = ac.currentTime + SCHEDULE_AHEAD` | |
| 197 | `  const fadeSec = crossfadeMs.value / 1000` | |
| 198 | `  const oldSource = activeSource` | |
| 199 | `  const oldGain = activeGain` | |
| 200 | `  const isCrossfade = !!(oldSource && oldGain && isPlaying.value)` | |
| 201 | `  if (isCrossfade && oldSource && oldGain) {` | |
| 202 | `    if (fadeSec <= 0) {` | |
| 203 | `      oldGain.gain.cancelScheduledValues(0)` | |
| 204 | `      oldGain.gain.setValueAtTime(0, now)` | |
| 205 | `      oldSource.stop(now + 0.01)` | |
| 206 | `      newGain.gain.setValueAtTime(1, now)` | |
| 207 | `    } else {` | |
| 208 | `      const oldGainVal = oldGain.gain.value` | |
| 209 | `      oldGain.gain.cancelScheduledValues(0)` | |
| 210 | `      oldGain.gain.setValueAtTime(oldGainVal, now)` | |
| 211 | `      oldGain.gain.linearRampToValueAtTime(0, now + fadeSec)` | |
| 212 | `      oldSource.stop(now + fadeSec + 0.05)` | |
| 213 | `      newGain.gain.setValueAtTime(0, now)` | |
| 214 | `      newGain.gain.linearRampToValueAtTime(1, now + fadeSec)` | |
| 215 | `    }` | |
| 216 | `  } else {` | |
| 217 | `    newGain.gain.setValueAtTime(1, now)` | |
| 218 | `  }` | |
| 219 | `  const offset = isCrossfade ? preparedLoopStartSec : preparedColdStartSec` | |
| 220 | `  newSource.start(now, offset)` | |
| 221 | `  newSource.onended = () => {` | |
| 222 | `    if (newSource === activeSource) {` | |
| 223 | `      isPlaying.value = false` | |
| 224 | `      activeSource = null` | |
| 225 | `      activeGain = null` | |
| 226 | `    }` | |
| 227 | `  }` | |
| 228 | `  activeSource = newSource` | |
| 229 | `  activeGain = newGain` | |
| 230 | `  isPlaying.value = true` | |
| 231 | `}` | |

### 1.14 play(base64Wav)

| # | Original-Code | JUCE |
|---|---|---|
| 232 | `async function play(base64Wav: string) {` | |
| 233 | `  const ac = ensureContext()` | |
| 234 | `  const decoded = await decodeBase64Wav(base64Wav)` | |
| 235 | `  originalBuffer = decoded` | |
| 236 | `  rawBase64 = base64Wav` | |
| 237 | `  bufferDuration.value = decoded.duration` | |
| 238 | `  hasAudio.value = true` | |
| 239 | `  peakAmplitude.value = measurePeak(decoded)` | |
| 240 | `  startSource(ac, prepareBuffer(ac, decoded))` | |
| 241 | `}` | |

### 1.15 loadBuffer(base64Wav)

| # | Original-Code | JUCE |
|---|---|---|
| 242 | `async function loadBuffer(base64Wav: string) {` | |
| 243 | `  const ac = ensureContext()` | |
| 244 | `  const decoded = await decodeBase64Wav(base64Wav)` | |
| 245 | `  originalBuffer = decoded` | |
| 246 | `  rawBase64 = base64Wav` | |
| 247 | `  bufferDuration.value = decoded.duration` | |
| 248 | `  hasAudio.value = true` | |
| 249 | `  peakAmplitude.value = measurePeak(decoded)` | |
| 250 | `}` | |

### 1.16 replay()

| # | Original-Code | JUCE |
|---|---|---|
| 251 | `function replay() {` | |
| 252 | `  if (!originalBuffer) return` | |
| 253 | `  const ac = ensureContext()` | |
| 254 | `  startSource(ac, prepareBuffer(ac, originalBuffer))` | |
| 255 | `}` | |

### 1.17 stop()

| # | Original-Code | JUCE |
|---|---|---|
| 256 | `function stop() {` | |
| 257 | `  if (activeSource) { try { activeSource.stop() } catch { /* */ } activeSource = null }` | |
| 258 | `  if (activeGain) { activeGain.disconnect(); activeGain = null }` | |
| 259 | `  isPlaying.value = false` | |
| 260 | `}` | |

### 1.18 retrigger()

| # | Original-Code | JUCE |
|---|---|---|
| 261 | `function retrigger() {` | |
| 262 | `  if (!originalBuffer) return` | |
| 263 | `  stop()` | |
| 264 | `  const ac = ensureContext()` | |
| 265 | `  startSource(ac, prepareBuffer(ac, originalBuffer))` | |
| 266 | `}` | |

### 1.19 setLoop(on)

| # | Original-Code | JUCE |
|---|---|---|
| 267 | `function setLoop(on: boolean) {` | |
| 268 | `  isLooping.value = on` | |
| 269 | `  if (activeSource) activeSource.loop = on` | |
| 270 | `}` | |

### 1.20 setTranspose(semitones)

| # | Original-Code | JUCE |
|---|---|---|
| 271 | `function setTranspose(semitones: number) {` | |
| 272 | `  transposeSemitones.value = semitones` | |
| 273 | `  if (activeSource) activeSource.playbackRate.value = rateForPlayback()` | |
| 274 | `}` | |

### 1.21 glideToSemitones(semitones, timeMs)

| # | Original-Code | JUCE |
|---|---|---|
| 275 | `function glideToSemitones(semitones: number, timeMs: number) {` | |
| 276 | `  transposeSemitones.value = semitones` | |
| 277 | `  if (activeSource && ctx) {` | |
| 278 | `    const now = ctx.currentTime` | |
| 279 | `    const rate = Math.pow(2, semitones / 12)` | |
| 280 | `    activeSource.playbackRate.setValueAtTime(activeSource.playbackRate.value, now)` | |
| 281 | `    activeSource.playbackRate.linearRampToValueAtTime(rate, now + timeMs / 1000)` | |
| 282 | `  }` | |
| 283 | `}` | |

### 1.22 setLoopStart(frac)

| # | Original-Code | JUCE |
|---|---|---|
| 284 | `function setLoopStart(frac: number) {` | |
| 285 | `  loopStartFrac.value = Math.max(0, Math.min(frac, loopEndFrac.value - 0.01))` | |
| 286 | `  if (activeSource && originalBuffer) {` | |
| 287 | `    activeSource.loopStart = loopStartFrac.value * originalBuffer.duration` | |
| 288 | `  }` | |
| 289 | `}` | |

### 1.23 setLoopEnd(frac)

| # | Original-Code | JUCE |
|---|---|---|
| 290 | `function setLoopEnd(frac: number) {` | |
| 291 | `  loopEndFrac.value = Math.max(loopStartFrac.value + 0.01, Math.min(frac, 1))` | |
| 292 | `  optimizedEndFrac.value = loopEndFrac.value` | |
| 293 | `  if (activeSource && originalBuffer) {` | |
| 294 | `    activeSource.loopEnd = loopEndFrac.value * originalBuffer.duration` | |
| 295 | `  }` | |
| 296 | `}` | |

### 1.24 setCrossfade(ms)

| # | Original-Code | JUCE |
|---|---|---|
| 297 | `function setCrossfade(ms: number) {` | |
| 298 | `  crossfadeMs.value = Math.max(0, Math.min(ms, 500))` | |
| 299 | `  if (crossfadeDebounce) clearTimeout(crossfadeDebounce)` | |
| 300 | `  crossfadeDebounce = setTimeout(() => {` | |
| 301 | `    if (originalBuffer && isPlaying.value) replay()` | |
| 302 | `  }, 100)` | |
| 303 | `}` | |

### 1.25 setNormalize(on)

| # | Original-Code | JUCE |
|---|---|---|
| 304 | `function setNormalize(on: boolean) {` | |
| 305 | `  normalizeOn.value = on` | |
| 306 | `  if (loopModeDebounce) clearTimeout(loopModeDebounce)` | |
| 307 | `  loopModeDebounce = setTimeout(() => {` | |
| 308 | `    if (originalBuffer && isPlaying.value) replay()` | |
| 309 | `  }, 100)` | |
| 310 | `}` | |

### 1.26 setLoopOptimize(on)

| # | Original-Code | JUCE |
|---|---|---|
| 311 | `function setLoopOptimize(on: boolean) {` | |
| 312 | `  loopOptimize.value = on` | |
| 313 | `  if (loopModeDebounce) clearTimeout(loopModeDebounce)` | |
| 314 | `  loopModeDebounce = setTimeout(() => {` | |
| 315 | `    if (originalBuffer && isPlaying.value) replay()` | |
| 316 | `  }, 100)` | |
| 317 | `}` | |

### 1.27 setLoopPingPong(on)

| # | Original-Code | JUCE |
|---|---|---|
| 318 | `function setLoopPingPong(on: boolean) {` | |
| 319 | `  loopPingPong.value = on` | |
| 320 | `  if (loopModeDebounce) clearTimeout(loopModeDebounce)` | |
| 321 | `  loopModeDebounce = setTimeout(() => {` | |
| 322 | `    if (originalBuffer && isPlaying.value) replay()` | |
| 323 | `  }, 100)` | |
| 324 | `}` | |

### 1.28 exportRaw()

| # | Original-Code | JUCE |
|---|---|---|
| 325 | `function exportRaw(): Blob \| null {` | |
| 326 | `  if (!rawBase64) return null` | |
| 327 | `  const bin = atob(rawBase64)` | |
| 328 | `  const bytes = new Uint8Array(bin.length)` | |
| 329 | `  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)` | |
| 330 | `  return new Blob([bytes], { type: 'audio/wav' })` | |
| 331 | `}` | |

### 1.29 exportLoop()

| # | Original-Code | JUCE |
|---|---|---|
| 332 | `function exportLoop(): Blob \| null {` | |
| 333 | `  if (!originalBuffer) return null` | |
| 334 | `  const [s, e] = loopBoundsSamples(originalBuffer)` | |
| 335 | `  if (e <= s) return null` | |
| 336 | `  return encodeWav(originalBuffer, s, e)` | |
| 337 | `}` | |

### 1.30 getWaveformPeaks(numBins)

| # | Original-Code | JUCE |
|---|---|---|
| 338 | `function getWaveformPeaks(numBins: number): Float32Array \| null {` | |
| 339 | `  if (!originalBuffer) return null` | |
| 340 | `  const ch = originalBuffer.getChannelData(0)` | |
| 341 | `  const peaks = new Float32Array(numBins)` | |
| 342 | `  const binSize = ch.length / numBins` | |
| 343 | `  for (let i = 0; i < numBins; i++) {` | |
| 344 | `    const start = Math.floor(i * binSize)` | |
| 345 | `    const end = Math.min(Math.floor((i + 1) * binSize), ch.length)` | |
| 346 | `    let max = 0` | |
| 347 | `    for (let j = start; j < end; j++) {` | |
| 348 | `      const abs = Math.abs(ch[j]!)` | |
| 349 | `      if (abs > max) max = abs` | |
| 350 | `    }` | |
| 351 | `    peaks[i] = max` | |
| 352 | `  }` | |
| 353 | `  return peaks` | |
| 354 | `}` | |

### 1.31 dispose()

| # | Original-Code | JUCE |
|---|---|---|
| 355 | `function dispose() {` | |
| 356 | `  stop()` | |
| 357 | `  originalBuffer = null; rawBase64 = null; hasAudio.value = false` | |
| 358 | `  if (ctx && ctx.state !== 'closed') ctx.close()` | |
| 359 | `  ctx = null` | |
| 360 | `}` | |

---

*useAudioLooper.ts: 360 Zeilen erfasst. Fortgesetzt in separater Datei wegen Größe.*
