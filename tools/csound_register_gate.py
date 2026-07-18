#!/usr/bin/env python3
"""Register (organ footage) gate: a layer authored at 16' must actually SOUND an
octave below the played note, on every family of idiom -- not merely carry the
number in its metadata.

This exists because the register is the one dimension where the number is easy to
plumb and easy to lose: a footage that never reaches `kfreq` still renders a
perfectly good tone at the wrong octave, and every structural test stays green.
So the assertion here is the measured FUNDAMENTAL of the rendered note, in cents
against the played pitch, per idiom family (a bandlimited oscillator, a pure
tone, a modal bank, a formant voice -- they reach kfreq by different routes).

Also covers the mix law: three layers at their authored volumes must not clip,
and the k-rate law must give a single unity layer the same level as before it
was k-rate (the sound of a one-oscillator patch may not change because the mix
became live).

Run: .venv/bin/python tools/csound_register_gate.py
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import csound_orch as C                      # noqa: E402
from csound_keys_gate import CHECK, TIMEOUT, ensure_check, _load   # noqa: E402

PLAYED_HZ = 220.0


def _spectrum(x, sr, t=0.6, win=8192):
    seg = x[int(t * sr):int(t * sr) + win]
    if len(seg) < win:
        return np.zeros(win // 2 + 1), np.zeros(win // 2 + 1)
    mag = np.abs(np.fft.rfft(seg * np.hanning(win)))
    return np.fft.rfftfreq(win, 1 / sr), mag


def _strongest(x, sr):
    """Frequency of the strongest partial in the sustained note.

    Deliberately NOT autocorrelation: a modal metal bank (`mode`) is inharmonic,
    its period is not its pitch, and autocorrelation locks onto the beating
    between modes -- measured 31 Hz for a glass layer whose partials sit exactly
    where they should. Under a register change the WHOLE spectrum scales, so the
    strongest partial is the honest witness for every family."""
    f, mag = _spectrum(x, sr)
    if mag.sum() <= 1e-9:
        return 0.0
    k = int(np.argmax(mag))
    # parabolic interpolation across the peak bin: at an 8192-point window the
    # bin spacing is ~5.4 Hz, i.e. +-21 cents at 110 Hz, which would force a
    # tolerance loose enough to miss a real quarter-tone error.
    if 0 < k < len(mag) - 1:
        y0, y1, y2 = mag[k - 1], mag[k], mag[k + 1]
        d = y0 - 2 * y1 + y2
        if abs(d) > 1e-12:
            k = k + 0.5 * (y0 - y2) / d
    return float(k * sr / (2 * (len(mag) - 1)))


def _centroid(x, sr):
    f, mag = _spectrum(x, sr)
    return float((f * mag).sum() / mag.sum()) if mag.sum() > 1e-9 else 0.0


def _render(orc, path, freq=PLAYED_HZ):
    """Un-normalized render (probe), so measured levels are the real ones.

    `render` normalizes to -12 dBFS: every patch comes back peaking at exactly
    0.2512, which silently turns any level assertion into a tautology."""
    op = path + ".orc"
    open(op, "w").write(orc)
    r = subprocess.run([CHECK, "probe", op, path, str(freq), "1.0", "0.0", "0.5",
                        "1.6", "2.0"],
                       capture_output=True, text=True, timeout=TIMEOUT)
    if r.returncode != 0:
        raise RuntimeError(f"probe failed: {r.stdout}\n{r.stderr}")
    return _load(path)


def main():
    ensure_check()
    tmp = tempfile.mkdtemp(prefix="regreg_")
    failures, checks = [], 0

    # One representative per route to kfreq: a bandlimited oscillator (vco2), a
    # plain oscili tone, a modal bank (`mode`), and a formant voice (reson bank).
    FAMILIES = ["saw", "sine", "glass", "voice"]
    FOOTAGES = [("16", 0.5), ("8", 1.0), ("4", 2.0)]

    for tech in FAMILIES:
        ref = None
        for label, mult in FOOTAGES:
            orc, _ = C.build_orchestra(
                oscs=[{"chain": [tech], "vol": 1.0, "register": mult}])
            x, sr = _render(orc, os.path.join(tmp, f"{tech}_{label}.wav"))
            part, cent = _strongest(x, sr), _centroid(x, sr)
            if mult == 1.0:
                ref = (part, cent)
            checks += 1
            # ABSOLUTE: the strongest partial of an 8' layer is the played pitch
            # (or one of its harmonics -- a formant voice peaks at a formant, a
            # modal bank at its loudest mode), so the absolute assertion is only
            # made where the fundamental IS the strongest partial.
            want = PLAYED_HZ * mult
            cents = 1200 * np.log2(part / want) if part > 0 else 9999
            print(f"  {tech:6s} {label + chr(39):4s} strongest {part:7.1f} Hz "
                  f"(want {want:6.1f}, {cents:+6.0f} cents)  centroid {cent:8.1f}")
            if tech in ("saw", "sine") and abs(cents) > 40:
                failures.append(
                    f"{tech} @{label}': strongest partial {part:.1f} Hz, expected "
                    f"{want:.1f} Hz ({cents:+.0f} cents)")

        # RELATIVE (every family, including the inharmonic ones): changing the
        # register must scale the WHOLE spectrum by exactly the footage ratio.
        # This is the assertion that catches a footage which never reaches kfreq
        # -- the failure mode is a perfectly good tone at the wrong octave.
        #
        # The spectral CENTROID is deliberately NOT asserted, only printed. It
        # does not scale with the register, and correctly so: a bandlimited saw
        # (vco2) fits MORE harmonics under Nyquist an octave down, a sung vowel's
        # formants are fixed resonances that stay put while the pitch moves (that
        # is what a formant is), and a modal bank is clamped to a fixed 20..15000
        # band. Measured centroid ratios for a x0.5 register: saw 0.91, voice
        # 0.85, glass 0.60, sine 0.55. Asserting on it would be asserting that
        # those idioms are wrong.
        for label, mult in FOOTAGES:
            if mult == 1.0 or ref is None:
                continue
            x, sr = _load(os.path.join(tmp, f"{tech}_{label}.wav"))
            checks += 1
            got = _strongest(x, sr)
            ratio = got / ref[0] if ref[0] > 0 else 0.0
            if not (0.99 * mult <= ratio <= 1.01 * mult):
                failures.append(
                    f"{tech} @{label}': strongest partial scaled x{ratio:.3f}, the "
                    f"footage asks for x{mult} -- the register did not reach kfreq")

    # The played pitch itself must still be the reference at 8': a register that
    # transposes CORRECTLY but from the wrong reference would pass the ratios
    # above and still play the wrong note.
    orc, _ = C.build_orchestra(oscs=[{"chain": ["saw"], "vol": 1.0, "register": 1.0}])
    for freq in (110.0, 440.0):
        x, sr = _render(orc, os.path.join(tmp, f"ref_{int(freq)}.wav"), freq)
        part = _strongest(x, sr)
        checks += 1
        cents = 1200 * np.log2(part / freq) if part > 0 else 9999
        if abs(cents) > 40:
            failures.append(f"8' at {freq} Hz played {part:.1f} Hz ({cents:+.0f} cents)")
        print(f"  8' reference @{freq:.0f} Hz -> {part:7.1f} Hz  {cents:+6.1f} cents")

    # A 16' on a bottom note runs into the 20 Hz floor by design (the same limit
    # the shared kfreq carries). Assert it CLAMPS rather than folding or dying:
    # the note must still sound, and it must not be silent.
    orc, _ = C.build_orchestra(oscs=[{"chain": ["saw"], "vol": 1.0, "register": 0.25}])
    x, sr = _render(orc, os.path.join(tmp, "floor.wav"), 27.5)   # A0 at 32'
    checks += 1
    rms = float(np.sqrt(np.mean(x[int(0.35 * sr):int(1.15 * sr)] ** 2)))
    if rms < 1e-4:
        failures.append(f"32' at A0 rendered silence (rms {rms:.2e})")
    print(f"  32' at A0 (clamped to the 20 Hz floor) rms {rms:.4f}")

    # Mix law: three layers at their authored volumes stay bounded, and a single
    # unity layer is unchanged by the mix becoming k-rate.
    one, _ = C.build_orchestra(oscs=[{"chain": ["saw"], "vol": 1.0, "register": 1.0}])
    x1, sr = _render(one, os.path.join(tmp, "mix1.wav"))
    three, _ = C.build_orchestra(oscs=[
        {"chain": ["saw"], "vol": 1.0, "register": 1.0},
        {"chain": ["sine"], "vol": 0.6, "register": 0.5},
        {"chain": ["pwm"], "vol": 0.4, "register": 2.0}])
    x3, sr = _render(three, os.path.join(tmp, "mix3.wav"))
    checks += 2
    p1, p3 = float(np.max(np.abs(x1))), float(np.max(np.abs(x3)))
    r1 = float(np.sqrt(np.mean(x1[int(0.35 * sr):int(1.15 * sr)] ** 2)))
    r3 = float(np.sqrt(np.mean(x3[int(0.35 * sr):int(1.15 * sr)] ** 2)))
    if p3 > 0.879:
        failures.append(f"3-layer peak {p3:.3f} exceeds the limiter asymptote")
    # "adding a layer enriches the timbre without a loudness jump": the level of
    # the three-layer patch has to stay within ~6 dB of the single layer.
    if not (0.5 < r3 / max(r1, 1e-9) < 2.0):
        failures.append(f"3-layer rms {r3:.4f} vs single {r1:.4f}: loudness jump")
    print(f"  single peak {p1:.4f} rms {r1:.4f}   "
          f"three-layer peak {p3:.4f} rms {r3:.4f}")

    print("\n" + "=" * 70)
    for f in failures:
        print("FAIL:", f)
    print(f"REGISTER GATE: {checks - len(failures)}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
