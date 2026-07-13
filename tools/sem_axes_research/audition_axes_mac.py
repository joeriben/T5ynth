#!/usr/bin/env python3
"""Ear-ground ALL surviving PCA axes (pc2,3,4,5,7,8,10) on the correct UNMASKED
algorithm, to catalog per-pole which single side is interesting -> then pair the
good halves into bipolar sliders.

Same faithful reproduction as the corrected knob test: T5ynth IPC backend, SA3
medium, unmask_manipulation=True (result += t*pca[k] over all 256 positions, ==
Fedora Latent Lab), t in {-2,+2}, fixed seed per prompt. 6 prompts the user found
discriminating (violin, guitar, marimba, pad, vocal, wind). NEUTRAL labels only
(pcN / -2 / base / +2) -- no texture names, so the ear is not primed.
"""
from __future__ import annotations
import json, struct, subprocess, sys, time, wave
from pathlib import Path
import numpy as np

REPO = Path(__file__).resolve().parents[2]
BACKEND = REPO / "backend" / "pipe_inference.py"
VENV_PY = REPO / ".venv" / "bin" / "python"
SCRATCH = Path(__file__).resolve().parent
OUT = SCRATCH / "pca_axis_audition"
COMPONENTS = SCRATCH / "pca_components_sa3.pt"

MODEL = "stable-audio-3-medium"
DUR, STEPS, CFG = 6.0, 8, 1.0
TS = [-2, 2]
PCS = [2, 3, 4, 5, 7, 8, 10]
# (key, modality, raw prompt, seed).  The 4 STANDARD test samples (v2 set) first,
# then the 6 discriminating "additional" prompts the user found useful.
PROMPTS = [
    ("piano",    "music", "a piano, c3",                        101101),
    ("samba",    "music", "a samba rhythm, 120bpm",             202202),
    ("water",    "sfx",   "water pouring",                      303303),
    ("snare",    "sfx",   "a snare roll",                       404404),
    ("violin",   "music", "a violin, sustained note",           100001),
    ("guitar",   "music", "a distorted electric guitar chord",  100002),
    ("marimba",  "music", "a marimba melody",                   100004),
    ("pad",      "music", "a warm analog synth pad",            100005),
    ("vocal",    "music", "a female vocal, aah",                100006),
    ("wind",     "sfx",   "wind howling",                       100010),
]


class PipeClient:
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=False)
        self.stdin, self.stdout = self.process.stdin, self.process.stdout
        self.info = self._read_ready()

    def _read_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            c = self.stdout.read(n - len(buf))
            if not c:
                raise RuntimeError(f"backend closed pipe ({len(buf)}/{n})")
            buf.extend(c)
        return bytes(buf)

    def _read_ready(self):
        h = self._read_exact(1)
        if h == b"\x00":
            raise RuntimeError(self._read_exact(struct.unpack("<I", self._read_exact(4))[0]).decode("utf-8", "replace"))
        assert h == b"\x02", h
        return json.loads(self._read_exact(struct.unpack("<H", self._read_exact(2))[0]).decode("utf-8"))

    def request(self, payload):
        self.stdin.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
        self.stdin.flush()
        h = self._read_exact(1)
        if h == b"\x00":
            raise RuntimeError(self._read_exact(struct.unpack("<I", self._read_exact(4))[0]).decode("utf-8", "replace"))
        assert h == b"\x01", h
        _, samples, channels, sr, seed, el = struct.unpack("<iiiiif", self._read_exact(24))
        pcm = np.frombuffer(self._read_exact(samples * channels * 4), dtype="<f4").copy()
        audio = pcm.reshape(channels, samples)
        nd = struct.unpack("<H", self._read_exact(2))[0]
        if nd:
            self._read_exact(nd * 12)
        return {"audio": audio, "sr": sr, "el": el / 1000.0}

    def close(self):
        try:
            if self.stdin and not self.stdin.closed:
                self.stdin.close()
        finally:
            if self.process.poll() is None:
                self.process.terminate()


def write_wav(path, audio, sr):
    a = np.clip(audio, -1, 1).astype(np.float32)
    pcm = (a.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(a.shape[0]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def features(audio, sr):
    y = audio.mean(axis=0) if audio.ndim == 2 else audio
    y = np.asarray(y, dtype=np.float64)
    if y.size == 0:
        return {}
    rms = float(np.sqrt(np.mean(y ** 2)))
    zcr = float(np.mean(np.abs(np.diff(np.sign(y))) > 0))
    win, hop = 2048, 512
    if y.size < win:
        y = np.pad(y, (0, win - y.size))
    nf = 1 + (len(y) - win) // hop
    w = np.hanning(win); freqs = np.fft.rfftfreq(win, 1.0 / sr)
    mags = np.empty((nf, freqs.size))
    for i in range(nf):
        mags[i] = np.abs(np.fft.rfft(y[i * hop:i * hop + win] * w))
    fsum = mags.sum(axis=1) + 1e-9
    centroid = float(np.mean((mags * freqs[None, :]).sum(axis=1) / fsum))
    flux = np.maximum(0.0, np.diff(mags, axis=0)).sum(axis=1)
    onset = sum(1 for i in range(1, len(flux) - 1)
                if flux[i] > flux.mean() + flux.std() and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]) if flux.size > 2 else 0
    return {"centroid": round(centroid, 1), "zcr": round(zcr, 4),
            "rms": round(rms, 4), "onset_per_s": round(onset / (len(y) / sr), 2)}


def offsets(pca, pc, t):
    r = pca[pc - 1]
    return [[i, float(t * r[i])] for i in range(r.shape[0])]


def main():
    import torch
    OUT.mkdir(parents=True, exist_ok=True)
    pca = torch.load(str(COMPONENTS), map_location="cpu").float().numpy()
    client = PipeClient([str(VENV_PY), str(BACKEND)])
    print(f"ready device={client.info.get('default')}", flush=True)
    assert MODEL in client.info.get("models", [])

    def gen(raw, mod, offs, seed, fname):
        req = {"model": MODEL, "prompt_a": raw, "prompt_b": raw, "alpha": 0.0,
               "track_type": mod, "modality_epoch": 0, "unmask_manipulation": True,
               "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": seed}
        if offs:
            req["dimension_offsets"] = offs
        r = client.request(req)
        write_wav(OUT / fname, r["audio"], r["sr"])
        return features(r["audio"], r["sr"])

    manifest = {"model": f"{MODEL} unmasked (== Latent Lab knob)", "pcs": PCS, "prompts": []}
    t0 = time.time(); n = 0
    try:
        for key, mod, raw, seed in PROMPTS:
            prec = {"key": key, "modality": mod, "prompt": raw, "seed": seed,
                    "base": gen(raw, mod, None, seed, f"{key}_base.wav"), "axes": {}}
            n += 1
            print(f"[{n}] {key} base ({time.time()-t0:.0f}s)", flush=True)
            for pc in PCS:
                cell = {}
                for t in TS:
                    fn = f"{key}_pc{pc}_t{t:+d}.wav"
                    cell[f"t{t:+d}"] = {"file": fn, "feat": gen(raw, mod, offsets(pca, pc, t), seed, fn)}
                    n += 1
                prec["axes"][f"pc{pc}"] = cell
            manifest["prompts"].append(prec)
            print(f"    {key} axes done ({time.time()-t0:.0f}s)", flush=True)
            json.dump(manifest, open(OUT / "manifest.json", "w"), indent=2)
    finally:
        client.close()
    build_html(OUT, manifest)
    print(f"\n{n} clips in {time.time()-t0:.0f}s -> {OUT}", flush=True)


def build_html(out, m):
    # ONE shared <audio> element + click-to-play buttons. Exclusive by construction
    # (a single element can only play one thing), the active cell is highlighted, and
    # the sticky bar shows exactly what is playing. No 150-element overhead / no
    # cross-firing handlers -> responsive, unambiguous.
    css = ("body{font:15px system-ui;margin:0 24px 40px;max-width:940px}"
           "h1{font-size:20px}h2{margin-top:30px;border-top:2px solid #ddd;padding-top:8px;font-size:17px}"
           "th{font:12px system-ui;color:#666}small{color:#666}"
           "#now{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:4px;"
           "border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}"
           "#player{width:360px;height:34px}#nowlabel{font-weight:bold;font-size:15px;color:#333}"
           "table{border-collapse:collapse}"
           ".clip{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:5px;"
           "padding:6px 14px;font-size:14px;min-width:56px}"
           ".clip:hover{background:#e7e7e7}"
           ".clip.playing{background:#ffca28;border-color:#f57f17;font-weight:bold}"
           ".ft{font:10px monospace;color:#aaa;margin-top:3px}")
    R = ["<div id='now'><audio id='player' controls preload='auto'></audio>"
         "<span id='nowlabel'>&mdash; nothing playing &mdash;</span></div>",
         "<h1>SA3 PCA axis audition &mdash; unmasked, neutral</h1>",
         f"<p><b>{m['model']}</b> &middot; {len(m['prompts'])} prompts (4 standard + 6 additional) "
         "&middot; t = &minus;2 / base / +2 &middot; fixed seed per row.</p>",
         "<p>Click a cell to play &mdash; <b>one at a time</b>; the playing cell turns amber and the bar "
         "above names it. Click the amber cell again to stop. No texture names on purpose: for each axis, "
         "per prompt, decide is <b>&minus;2</b> interesting? is <b>+2</b> interesting? what does the good "
         "pole sound like? (feature hints: cen=centroid, on=onset/s, zcr)</p>"]

    def btn(file, label, pole, feat):
        if not file:
            return "<td></td>"
        fs = (f"<div class='ft'>cen {feat.get('centroid','?')} on {feat.get('onset_per_s','?')} "
              f"zcr {feat.get('zcr','?')}</div>") if feat else ""
        return (f"<td align=center style='border:1px solid #ccc'>"
                f"<button class='clip' data-src='{file}' data-label='{label}'>{pole}</button>{fs}</td>")

    for pc in m["pcs"]:
        R.append(f"<h2>pc{pc}</h2>")
        R.append("<table cellpadding=5>")
        R.append("<tr><th>prompt</th><th>&minus;2</th><th>base</th><th>+2</th></tr>")
        for p in m["prompts"]:
            ax = p["axes"].get(f"pc{pc}", {})
            cm, cp = ax.get("t-2", {}), ax.get("t+2", {})
            k = p["key"]
            R.append("<tr>"
                     f"<td style='border:1px solid #ccc;font-size:12px'><b>{k}</b> "
                     f"<small>({p['modality']})</small></td>"
                     + btn(cm.get("file"), f"pc{pc} &middot; {k} &middot; &minus;2", "&minus;2", cm.get("feat", {}))
                     + btn(f"{k}_base.wav", f"{k} &middot; base", "base", p["base"])
                     + btn(cp.get("file"), f"pc{pc} &middot; {k} &middot; +2", "+2", cp.get("feat", {}))
                     + "</tr>")
        R.append("</table>")

    js = ("<script>"
          "var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;"
          "function clr(){if(A){A.classList.remove('playing');A=null;}L.innerHTML='&mdash; nothing playing &mdash;';}"
          "document.addEventListener('click',function(e){"
          "var b=e.target.closest('.clip');if(!b)return;"
          "if(b===A){P.pause();P.currentTime=0;clr();return;}"          # click active = stop+reset
          "if(A)A.classList.remove('playing');A=b;b.classList.add('playing');"
          "L.innerHTML=b.dataset.label;P.src=b.dataset.src;P.play();"
          "});"
          "P.addEventListener('ended',clr);"
          "</script>")
    html = ("<!doctype html><meta charset=utf-8><title>SA3 PCA axis audition</title><style>"
            + css + "</style>" + "".join(R) + js)
    open(out / "listening.html", "w").write(html)


if __name__ == "__main__":
    main()
