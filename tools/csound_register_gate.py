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
import re
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

    # ---- EVERY LAYER carries its OWN register, and the right one ----
    #
    # The single-layer checks above cannot see a register wired to the wrong
    # layer, or wired to none: an adversarial pass proved it by swapping koct1
    # and koct2 in a three-layer patch and by deleting the multiply from all
    # three -- both stayed green, because peak and rms do not observe pitch.
    #
    # Three pure tones, one per register, with DIFFERENT volumes: the spectrum
    # then identifies each layer twice over, by where its partial sits and by how
    # loud it is. Swapping two registers moves the loudness to the wrong
    # frequency; deleting the register collapses all three onto one.
    # Ablation, measured on the emitted text (not argued):
    #   unmodified          110Hz 1.00  220Hz 0.54  440Hz 0.31   GREEN
    #   koct1/koct2 swapped 110Hz 0.66  220Hz 1.00  440Hz 0.34   RED
    #   every "* koctN" cut 110Hz 0.00  220Hz 1.00  440Hz 0.00   RED
    layers = [{"chain": ["sine"], "vol": 1.0, "register": 0.5},    # 110 Hz, loudest
              {"chain": ["sine"], "vol": 0.6, "register": 1.0},    # 220 Hz
              {"chain": ["sine"], "vol": 0.3, "register": 2.0}]    # 440 Hz, quietest
    orc, _ = C.build_orchestra(oscs=layers)
    x, sr = _render(orc, os.path.join(tmp, "perlayer.wav"))
    f, mag = _spectrum(x, sr)
    def _at(hz):
        band = (f > hz * 0.97) & (f < hz * 1.03)
        return float(mag[band].max()) if band.any() else 0.0
    got = {hz: _at(hz) for hz in (110.0, 220.0, 440.0)}
    loudest = max(got.values()) or 1e-9
    rel = {hz: v / loudest for hz, v in got.items()}
    print("  per-layer: " + "  ".join(
        f"{int(hz)}Hz {rel[hz]:.2f}" for hz in (110.0, 220.0, 440.0)))
    for hz, want in ((110.0, 1.00), (220.0, 0.60), (440.0, 0.30)):
        checks += 1
        # generous band (a sine layer's partial is exact, the tolerance is for
        # window leakage), but far tighter than the 0.3/0.6/1.0 spacing
        if abs(rel[hz] - want) > 0.18:
            failures.append(
                f"layer at {int(hz)} Hz has relative level {rel[hz]:.2f}, its "
                f"volume asks for {want:.2f} -- register and volume are on "
                f"different layers, or a register never reached kfreq")

    # ---- the k-rate mix law is actually doing the normalizing ----
    #
    # Tested against its own ABLATION rather than against a loudness window. A
    # window would have to be wide enough for layers that correlate: three full
    # layers an octave apart are harmonically related, sum far more coherently
    # than sqrt(N), and land 1.56x above a single layer -- which is the designed
    # behaviour ("a mix gain that needs to know whether its inputs happen to
    # correlate is a special case waiting to be wrong"), not a defect. Any window
    # loose enough to accept 1.56 also accepts kmix=1. So: render the same patch
    # with the law and with `kmix = 1` spliced in, and assert the law attenuates
    # by 1/sqrt(sum(vol)). That cannot be satisfied by a broken law.
    one, _ = C.build_orchestra(oscs=[{"chain": ["saw"], "vol": 1.0, "register": 1.0}])
    x1, sr = _render(one, os.path.join(tmp, "mix1.wav"))
    full, _ = C.build_orchestra(oscs=[
        {"chain": ["saw"], "vol": 1.0, "register": 1.0},
        {"chain": ["square"], "vol": 1.0, "register": 0.5},
        {"chain": ["pwm"], "vol": 1.0, "register": 2.0}])
    xf, sr = _render(full, os.path.join(tmp, "mixfull.wav"))
    nolaw = re.sub(r"  kmix     = 1 / sqrt\(kvsum < 1 \? 1 : kvsum\)[^\n]*",
                   "  kmix     = 1", full)
    r1 = float(np.sqrt(np.mean(x1[int(0.35 * sr):int(1.15 * sr)] ** 2)))
    rf = float(np.sqrt(np.mean(xf[int(0.35 * sr):int(1.15 * sr)] ** 2)))
    pf = float(np.max(np.abs(xf)))
    checks += 2
    if pf > 0.879:
        failures.append(f"3-layer peak {pf:.3f} exceeds the limiter asymptote")
    if nolaw == full:
        failures.append("could not splice out the k-rate mix law -- this check "
                        "no longer tests anything")
    else:
        xn, sr = _render(nolaw, os.path.join(tmp, "mixnolaw.wav"))
        rn = float(np.sqrt(np.mean(xn[int(0.35 * sr):int(1.15 * sr)] ** 2)))
        want = 1.0 / np.sqrt(3.0)          # three layers at vol 1.0
        got = rf / max(rn, 1e-9)
        print(f"  mix law: attenuates by {got:.3f} (1/sqrt(3) = {want:.3f}); "
              f"one layer rms {r1:.4f}, three full layers rms {rf:.4f}, peak {pf:.4f}")
        if abs(got - want) > 0.05:
            failures.append(
                f"the k-rate mix law attenuates three full layers by {got:.3f}, "
                f"expected 1/sqrt(3) = {want:.3f}")

    # ---- a single unity layer is EXACTLY what it was before the mix went k-rate ----
    # Not a claim in a docstring: compare against the same orchestra with the
    # weighting spliced out, which is literally the pre-change emission.
    bare = re.sub(r"  asig     = kvol1 \* kmix \* ", "  asig     = ", one)
    if bare == one:
        failures.append("could not splice the k-rate weight out of the single-osc "
                        "mix line -- this check no longer tests anything")
        checks += 1
    else:
        xb, sr = _render(bare, os.path.join(tmp, "bare.wav"))
        checks += 1
        n = min(len(xb), len(x1))
        diff = float(np.max(np.abs(xb[:n] - x1[:n])))
        print(f"  single unity layer vs un-weighted form: max sample diff {diff:.2e}")
        if diff > 1e-6:
            failures.append(
                f"a single layer at vol 1.0 changed when the mix became k-rate "
                f"(max sample difference {diff:.2e})")

    print("\n" + "=" * 70)
    for f in failures:
        print("FAIL:", f)
    print(f"REGISTER GATE: {checks - len(failures)}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
