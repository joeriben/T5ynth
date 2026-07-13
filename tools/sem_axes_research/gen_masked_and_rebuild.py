#!/usr/bin/env python3
"""Fill the missing masked comparisons the user asked for, then rebuild the page.
- masked transient dose sweep (4 bases x t) next to the existing unmasked
- masked rhythmic dose sweep (hum, sine) next to unmasked
- single-word C (rhythmic_sustained) at HIGHER t on hum (chase the 'subtle oscillations')
Every HTML cell's number is recomputed from its WAV so ear + metric stay consistent.
"""
import os, sys, time
import numpy as np

S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
from audition_axes_mac import PipeClient, write_wav, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG

RHYTHMIC = "a rhythmic, percussive, syncopated, pulsing, driving, staccato, beat-driven groove"
SUSTAINED = "a sustained, continuous, persistent, stable, held, unbroken, droning, steady tone"
TRANSIENT = "sharp, punchy, percussive, snappy, staccato, clicking, plucked transient attacks"
SMOOTH = "a smooth, soft, legato, gentle, flowing, mellow, even sustained swell"
SEED = 11
TS = ["0.3", "0.6", "0.9", "1.2"]

TP_BASES = [("organ", "a sustained organ chord"), ("pad", "a warm analog synth pad"),
            ("violin", "a held violin note, sustained"), ("drone", "a smooth sine drone")]
RH_BASES = [("hum", "a steady sustained hum"), ("sine", "a single sine drone")]

TMO = os.path.join(S, "transient_masked_out"); os.makedirs(TMO, exist_ok=True)
RMO = os.path.join(S, "rhythm_masked_out"); os.makedirs(RMO, exist_ok=True)
CHO = os.path.join(S, "c_hight_out"); os.makedirs(CHO, exist_ok=True)


def onset_env(y, sr, win=1024, hop=256):
    y = np.asarray(y, np.float64)
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    Sp = np.empty((nf, win // 2 + 1))
    for i in range(nf):
        Sp[i] = np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    return np.maximum(0.0, np.diff(Sp, axis=0)).sum(axis=1), sr / hop


def pulse_of(y, sr):
    env, fr = onset_env(y, sr)
    e = env - env.mean()
    if np.allclose(e, 0):
        return 0.0
    ac = np.correlate(e, e, "full")[len(e) - 1:]; ac = ac / (ac[0] + 1e-12)
    lo, hi = int(0.2 * fr), min(int(1.5 * fr), len(ac) - 1)
    return float(np.max(ac[lo:hi])) if hi > lo else 0.0


def crest_of(y, sr):
    y = np.asarray(y, np.float64)
    return float(np.max(np.abs(y)) / (np.sqrt(np.mean(y ** 2)) + 1e-12))


import wave


def load(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    return a.reshape(-1, w.getnchannels()).mean(1), w.getframerate()


def mono(a):
    return a.mean(0) if a.ndim == 2 else a


client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time(); n = 0


def gen(base, sem, poles, unmask, out, fn):
    global n
    req = {"model": MODEL, "prompt_a": base, "prompt_b": base, "alpha": 0.0,
           "track_type": "music", "modality_epoch": 0, "unmask_manipulation": unmask,
           "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": SEED,
           "semantic_axes": sem, "axes_amount": 1.0}
    if poles:
        req["semantic_axes_poles"] = poles
    r = client.request(req); write_wav(os.path.join(out, fn), r["audio"], r["sr"]); n += 1


for key, base in TP_BASES:
    for t in TS:
        gen(base, {"trn": float(t)}, {"trn": [SMOOTH, TRANSIENT]}, False, TMO, f"{key}_t{t}.wav")
    print(f"  transient masked {key} ({time.time()-t0:.0f}s)", flush=True)
for key, base in RH_BASES:
    for t in TS:
        gen(base, {"rhy": float(t)}, {"rhy": [SUSTAINED, RHYTHMIC]}, False, RMO, f"{key}_t{t}.wav")
    print(f"  rhythmic masked {key} ({time.time()-t0:.0f}s)", flush=True)
for t in ["1.5", "2.0", "3.0"]:
    gen("a steady sustained hum", {"rhythmic_sustained": float(t)}, None, False, CHO, f"hum_sw_t{t}.wav")
print(f"  C single-word high-t done. {n} clips in {time.time()-t0:.0f}s", flush=True)
client.close()

# ---------------- rebuild HTML ----------------
MT = "bundle_mask_test_out"; TP = "transient_pin_out"; RH = "bundle_rhythm_out"
miss = []
_cache = {}


def feat(folder, fn, kind):
    p = os.path.join(S, folder, fn)
    if not os.path.exists(p):
        miss.append(f"{folder}/{fn}"); return None
    if (folder, fn, kind) not in _cache:
        y, sr = load(p)
        _cache[(folder, fn, kind)] = pulse_of(y, sr) if kind == "pulse" else crest_of(y, sr)
    return _cache[(folder, fn, kind)]


def clip(folder, fn, disp, label, kind, cls=""):
    v = feat(folder, fn, kind)
    if v is None:
        return "<td style='color:#c00;font-size:11px'>missing</td>"
    ft = f"{v:.2f}" if kind == "pulse" else f"{v:.0f}"
    return (f"<td class='{cls}'><button class='clip' data-src='{folder}/{fn}' data-label='{label}'>"
            f"{disp}</button><div class='ft'>{ft}</div></td>")


R = ["<div id='now'><audio id='player' controls preload='auto'></audio>"
     "<span id='nowlabel'>&mdash; nichts &mdash;</span></div>",
     "<h1>Bundled-Pole Injektion &mdash; Testh&ouml;ren (v2, maskiert vs unmaskiert)</h1>",
     "<div class='note'>Ein Klick spielt, Klick auf die <b>gelbe</b> Zelle stoppt. Zahl unter der Zelle = "
     "gemessen (rhythmic <b>pulse</b>, transient <b>crest</b>). Dein Ohr-Befund eingearbeitet: "
     "<b>maskiert = identit&auml;tswahrend (der musikalische Modus)</b>, unmaskiert = Morph/extrem.</div>"]

# Section 1: mask test (unchanged)
R.append("<h2>1 &middot; Mask-Test (A unmask / B mask / C Einzelwort)</h2>")
R.append("<table><tr><th>Prompt</th><th>base</th><th>t0.6 A</th><th>t0.6 B</th><th>t0.6 C</th>"
         "<th>t1.2 A</th><th>t1.2 B</th><th>t1.2 C</th></tr>")
R.append("<tr><td class='p'><b>rhythmic</b><br><small>hum</small></td>"
         + clip(MT, "rhy_base.wav", "base", "rhythmic base", "pulse")
         + clip(MT, "rhy_t0.6_A_bundle_unmasked.wav", "A", "rhy t0.6 UNMASK", "pulse", "A")
         + clip(MT, "rhy_t0.6_B_bundle_masked.wav", "B", "rhy t0.6 MASK", "pulse", "B")
         + clip(MT, "rhy_t0.6_C_singleword_masked.wav", "C", "rhy t0.6 1-Wort", "pulse", "C")
         + clip(MT, "rhy_t1.2_A_bundle_unmasked.wav", "A", "rhy t1.2 UNMASK", "pulse", "A")
         + clip(MT, "rhy_t1.2_B_bundle_masked.wav", "B", "rhy t1.2 MASK", "pulse", "B")
         + clip(MT, "rhy_t1.2_C_singleword_masked.wav", "C", "rhy t1.2 1-Wort", "pulse", "C") + "</tr>")
R.append("<tr><td class='p'><b>transient</b><br><small>pad</small></td>"
         + clip(MT, "trn_base.wav", "base", "transient base", "crest")
         + clip(MT, "trn_t0.6_A_bundle_unmasked.wav", "A", "trn t0.6 UNMASK", "crest", "A")
         + clip(MT, "trn_t0.6_B_bundle_masked.wav", "B", "trn t0.6 MASK", "crest", "B")
         + "<td style='background:#fafafa'></td>"
         + clip(MT, "trn_t1.2_A_bundle_unmasked.wav", "A", "trn t1.2 UNMASK", "crest", "A")
         + clip(MT, "trn_t1.2_B_bundle_masked.wav", "B", "trn t1.2 MASK", "crest", "B")
         + "<td style='background:#fafafa'></td></tr>")
R.append("</table>")


def dose_block(title, note, bases, unm_folder, msk_folder, kind):
    R.append(f"<h2>{title}</h2>")
    R.append(f"<div class='note'>{note}</div>")
    R.append("<table><tr><th>Prompt</th><th>Modus</th>"
             + "".join(f"<th>t={t}</th>" for t in TS) + "</tr>")
    for key, desc in bases:
        base_cell = clip(unm_folder, f"{key}_t0.0.wav", "base", f"{key} base", kind)
        R.append(f"<tr><td rowspan=2 class='p'><b>{key}</b><br><small>{desc}</small><br>{_barebtn(unm_folder, key)}</td>"
                 f"<td class='A' style='font-size:11px'>unmask<br><small>Morph</small></td>"
                 + "".join(clip(unm_folder, f"{key}_t{t}.wav", f"t{t}", f"{key} t{t} UNMASK", kind, "A") for t in TS)
                 + "</tr>")
        R.append("<tr><td class='B' style='font-size:11px'>mask<br><small>Identit&auml;t</small></td>"
                 + "".join(clip(msk_folder, f"{key}_t{t}.wav", f"t{t}", f"{key} t{t} MASK", kind, "B") for t in TS)
                 + "</tr>")
    R.append("</table>")


def _barebtn(folder, key):
    fn = f"{key}_t0.0.wav"
    if not os.path.exists(os.path.join(S, folder, fn)):
        return ""
    return (f"<button class='clip' data-src='{folder}/{fn}' data-label='{key} base' "
            f"style='min-width:40px;font-size:11px'>base</button>")


dose_block("2 &middot; Transient &mdash; Dosis, unmask vs mask",
           "<span class='listen'>Vergleiche unmask- gegen mask-Zeile.</span> Deine Frage aus Test 2: "
           "die maskierte Reihe fehlte &mdash; hier ist sie. (crest)",
           TP_BASES, TP, "transient_masked_out", "crest")

dose_block("3 &middot; Rhythmic &mdash; Dosis, unmask vs mask",
           "<span class='listen'>hum &amp; sine</span>, unmask vs mask. Du h&ouml;rtest Basis unmaskiert bis ~0.9 noch drin &mdash; "
           "die mask-Zeile sollte sie noch klarer halten. (pulse)",
           RH_BASES, RH, "rhythm_masked_out", "pulse")

# Section 4: C single-word at higher t
R.append("<h2>4 &middot; Wortmaske (C) bei h&ouml;herem t &mdash; die &bdquo;subtilen Schwingungen&ldquo;</h2>")
R.append("<div class='note'>rhythmic_sustained <b>Einzelwort, maskiert</b> auf hum. Du sagtest: bei 1.2 nicht "
         "uninteressant, k&ouml;nnte h&ouml;her etwas anderes werden. t&nbsp;1.2&rarr;3.0. (pulse)</div>")
R.append("<table><tr><th>base</th><th>t1.2</th><th>t1.5</th><th>t2.0</th><th>t3.0</th></tr><tr>"
         + clip(MT, "rhy_base.wav", "base", "hum base", "pulse")
         + clip(MT, "rhy_t1.2_C_singleword_masked.wav", "t1.2", "hum C t1.2", "pulse", "C")
         + clip("c_hight_out", "hum_sw_t1.5.wav", "t1.5", "hum C t1.5", "pulse", "C")
         + clip("c_hight_out", "hum_sw_t2.0.wav", "t2.0", "hum C t2.0", "pulse", "C")
         + clip("c_hight_out", "hum_sw_t3.0.wav", "t3.0", "hum C t3.0", "pulse", "C")
         + "</tr></table>")

CSS = ("body{font:15px system-ui;margin:0 24px 70px;max-width:1040px;color:#222}"
       "h1{font-size:20px}h2{margin-top:32px;border-top:2px solid #ddd;padding-top:10px;font-size:16px}"
       ".note{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:8px 12px;font-size:13px;margin:8px 0}"
       ".listen{color:#b26a00;font-weight:bold}"
       "#now{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;border-bottom:2px solid #ccc;"
       "z-index:20;display:flex;align-items:center;gap:16px}"
       "#player{width:380px;height:34px}#nowlabel{font-weight:bold;font-size:15px;color:#333}"
       "table{border-collapse:collapse;margin-top:6px}th{font:12px system-ui;color:#666;padding:4px 6px}"
       "td{border:1px solid #ccc;padding:5px 7px;text-align:center;vertical-align:middle}"
       "td.p{text-align:left;font-size:12px;background:#fafafa;min-width:130px}"
       ".clip{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:5px;padding:6px 11px;font-size:13px;min-width:44px}"
       ".clip:hover{background:#e7e7e7}.clip.playing{background:#ffca28;border-color:#f57f17;font-weight:bold}"
       ".ft{font:10px monospace;color:#999;margin-top:3px}"
       "td.A{border-left:3px solid #2e7d32}td.B{border-left:3px solid #c62828}td.C{border-left:3px solid #aaa}")
JS = ("<script>var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;"
      "function clr(){if(A){A.classList.remove('playing');A=null;}L.textContent='\\u2014 nichts \\u2014';}"
      "document.addEventListener('click',function(e){var b=e.target.closest('.clip');if(!b)return;"
      "if(b===A){P.pause();P.currentTime=0;clr();return;}"
      "if(A)A.classList.remove('playing');A=b;b.classList.add('playing');"
      "L.textContent=b.getAttribute('data-label');P.src=b.getAttribute('data-src');P.play();});"
      "P.addEventListener('ended',clr);</script>")
html = "<!doctype html><meta charset=utf-8><title>Bundled-Pole Testh&ouml;ren v2</title><style>" + CSS + "</style>" + "".join(R) + JS
out = os.path.join(S, "listening_final.html")
open(out, "w").write(html)
print("missing:", miss if miss else "none")
print("wrote", out)
