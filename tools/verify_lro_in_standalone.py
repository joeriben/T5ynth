#!/usr/bin/env python3
"""Make Homebrew's Csound unreachable, then produce an LRO sound in the BUILT
Standalone and record it. BJ's acceptance test for bundling Csound, 2026-07-26:
"Alles darunter beweist nichts — es ist genau der Fehler, dass ein grüner Lauf auf
der Maschine, die die Abhängigkeit schon hat, wie ein Beweis aussieht."

    tools/verify_lro_in_standalone.py [--preset "Bowed String"] [--seconds 3]
    tools/verify_lro_in_standalone.py --prove-it-can-fail

What it does, in the app, not beside it:

  1. takes a REAL authored orchestra out of one of the .t5p presets in the user's
     bank (the LRO's own output, not a body written for this test);
  2. gives the app a home of its own via CFFIXED_USER_HOME, and puts that
     orchestra into the standalone's stored plugin state with engine_mode=csound,
     so the app compiles it while starting;
  3. routes the app's output to BlackHole 2ch and enables a virtual MIDI port that
     this script owns, both through the same stored settings;
  4. launches the built app under sandbox-exec with every Homebrew Csound path
     denied — and refuses to interpret the result unless the denial actually bit;
  5. records the device with nothing played, as a control, then records one note
     and requires audible sound AT THE PITCH ASKED FOR, and names the WAV, because
     the last word on a sound is BJ's ear, not a number.

--prove-it-can-fail is the run that makes the others mean something: it denies the
bundle's own Contents/libs as well, and then requires the app to make NO sound.

Needs: BlackHole 2ch, ffmpeg, python-rtmidi, numpy (all in the maintainer Mac's
.venv — run it with .venv/bin/python).
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APP = REPO / "build_clean/T5ynth_artefacts/Release/Standalone/akroasys.app"
PRESETS = Path.home() / "Library/T5ynth/presets"
OUT_DEVICE = "BlackHole 2ch"
MIDI_PORT = "akroasys LRO acceptance"
NOTE = 57                     # A3, 220 Hz — the pitch every LRO measurement uses
SILENCE_FLOOR = 1e-3
REAL_SETTINGS = Path.home() / "Library/Application Support/akróasys.settings"
LAUNCHED = []                 # apps this run started, and is responsible for

# Same list as tools/verify_csound_bundle.py: every way a Homebrew Csound could be
# reached. The Cellar path is the one that matters — the library's baked-in opcode
# directory points into it.
HOMEBREW_CSOUND = (
    "/opt/homebrew/Cellar/csound",
    "/opt/homebrew/opt/csound",
    "/opt/homebrew/bin/csound",
    "/opt/homebrew/lib/libcsnd6.6.0.dylib",
    "/usr/local/opt/csound",
    "/Library/Frameworks/CsoundLib64.framework",
)

MAGIC_XML = 0x21324356        # juce::AudioProcessor::copyXmlToBinary
# juce::MemoryBlock::toBase64Encoding is NOT RFC 4648: "<byte count>." followed by
# 6-bit groups read LSB-first out of the byte stream, through this alphabet.
JUCE_BASE64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def fail(msg):
    print(f"FAIL  {msg}")
    sys.exit(1)


def ok(msg):
    print(f"ok    {msg}")


def orchestra_from_preset(name):
    """The orchestra text out of a .t5p. It sits in the preset's JSON as an escaped
    string, so it is unescaped the way it was written rather than by hand."""
    candidates = sorted(PRESETS.glob("*.t5p"))
    if name:
        candidates = [p for p in candidates if p.stem.lower() == name.lower()] or \
                     [p for p in candidates if name.lower() in p.stem.lower()]
    for path in candidates:
        m = re.search(rb"<CsoundSynthesizer>.*?</CsoundSynthesizer>",
                      path.read_bytes(), re.S)
        if not m:
            continue
        text = json.loads('"' + m.group(0).decode("utf-8", "replace") + '"')
        if "instr 1" in text:
            return path.stem, text
    fail(f"no preset with a stored orchestra found in {PRESETS}"
         + (f" matching '{name}'" if name else ""))


def juce_base64(data):
    """juce::MemoryBlock::toBase64Encoding. Verified against a real settings file
    written by the app: encode(decode(x)) == x."""
    acc = int.from_bytes(data, "little")
    return f"{len(data)}." + "".join(
        JUCE_BASE64[(acc >> (i * 6)) & 0x3F]
        for i in range(((len(data) << 3) + 5) // 6))


def filter_state(orchestra):
    """The standalone's stored plugin state: the APVTS tree as XML, in the binary
    wrapper AudioProcessor::copyXmlToBinary writes (LE magic, LE length, UTF-8
    text, a NUL), in JUCE's own base64. Only engine_mode is set —
    AudioProcessorValueTreeState leaves every parameter the tree does not mention
    at its default, so this state says "Csound engine, this orchestra" and nothing
    else. The parameter's stored value is the choice INDEX, and
    EngineMode::Csound == 4 (src/dsp/BlockParams.h:519); the id is "engine_mode"
    (BlockParams.h:43), and PluginProcessor reads the orchestra from the root's
    csoundOrchestra attribute (PluginProcessor.cpp:6430)."""
    esc = (orchestra.replace("&", "&amp;").replace("<", "&lt;")
                    .replace(">", "&gt;").replace('"', "&quot;"))
    xml = ('<?xml version="1.0" encoding="UTF-8"?> '
           f'<T5ynth csoundOrchestra="{esc}">'
           f'<PARAM id="engine_mode" value="4.0"/>'
           f'</T5ynth>')
    body = xml.encode("utf-8")
    return juce_base64(struct.pack("<II", MAGIC_XML, len(body)) + body + b"\x00")


def throwaway_home(scratch, orchestra):
    """A home of our own, so the real settings file is neither read nor written.

    $HOME is NOT enough and this is measured: JUCE resolves userHomeDirectory
    through NSHomeDirectory() (juce_Files_mac.mm:181), which reads the password
    database and ignores $HOME. The app therefore silently used the maintainer's
    own settings — his output device, his MIDI inputs — and the test recorded a
    device the synth was not playing to. CFFIXED_USER_HOME is what CoreFoundation
    honours; the launch below sets both, and the sandbox denies the real settings
    file outright so a regression here cannot quietly fall back to it.

    ~/Library/T5ynth is symlinked: the models and the preset bank live there and
    this test only reads them."""
    home = scratch / "home"
    if home.exists():
        shutil.rmtree(home)
    (home / "Library/Application Support").mkdir(parents=True)
    (home / "Library/Logs").mkdir(parents=True)
    (home / "Library/Caches").mkdir(parents=True)
    (home / "Library/T5ynth").symlink_to(Path.home() / "Library/T5ynth")
    # juce::PropertiesFile stores a string as a val ATTRIBUTE and an XML value as a
    # CHILD element — both shapes below are copied from a settings file the app
    # itself wrote. A string put in element text is silently read as empty, which
    # is a state that restores nothing and a synth that plays nothing.
    (home / "Library/Application Support/akróasys.settings").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n'
        f'  <VALUE name="filterState" val="{filter_state(orchestra)}"/>\n'
        '  <VALUE name="audioSetup">\n'
        f'    <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="{OUT_DEVICE}"\n'
        '                 audioInputDeviceName="" audioDeviceRate="48000.0"\n'
        '                 audioDeviceBufferSize="256">\n'
        f'      <MIDIINPUT name="{MIDI_PORT}"/>\n'
        '    </DEVICESETUP>\n'
        '  </VALUE>\n'
        '  <VALUE name="shouldMuteInput" val="1"/>\n'
        '</PROPERTIES>\n', encoding="utf-8")
    return home


def sandbox_profile(scratch, deny_bundled=False):
    denies = "\n".join(
        f'(deny file-read* (subpath "{p}"))' if os.path.isdir(p)
        else f'(deny file-read* (literal "{p}"))'
        for p in HOMEBREW_CSOUND)
    # The maintainer's own settings file: denied for reading AND writing, so this
    # test can neither borrow his session (which would make a pass meaningless —
    # his state names his output device and his MIDI ports) nor overwrite it.
    denies += (f'\n(deny file-read* (literal "{REAL_SETTINGS}"))'
               f'\n(deny file-write* (literal "{REAL_SETTINGS}"))')
    if deny_bundled:
        denies += f'\n(deny file-read* (subpath "{APP / "Contents/libs"}"))'
    path = scratch / "deny_homebrew_csound.sb"
    path.write_text(f"(version 1)\n(allow default)\n{denies}\n")
    return path


def denial_bit(profile):
    """Prove the sandbox cuts Csound off before believing anything it produces."""
    probe = ("import os,sys;"
             "sys.exit(0 if not any(os.path.exists(p) and os.access(p, os.R_OK)"
             f" for p in {HOMEBREW_CSOUND + (str(REAL_SETTINGS),)!r}) else 1)")
    r = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile),
                        sys.executable, "-c", probe])
    return r.returncode == 0


def avfoundation_index(name):
    """ffmpeg's avfoundation input wants the device's INDEX in `-i :<n>`; a name
    that does not resolve leaves it recording the default input, which is the
    built-in microphone — a silent recording that says nothing about the synth."""
    listing = subprocess.run(
        ["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true",
         "-i", ""], capture_output=True, text=True).stderr
    audio = listing.split("audio devices:")[-1]
    for line in audio.splitlines():
        m = re.search(r"\[(\d+)\] (.+)$", line)
        if m and m.group(2).strip() == name:
            return m.group(1)
    fail(f"'{name}' is not an avfoundation audio device — install BlackHole 2ch, "
         "or name the loopback device this machine has")


def record(device_index, seconds, path):
    """Record the loopback device to a WAV, synchronously."""
    subprocess.run(
        ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
         "-f", "avfoundation", "-i", f":{device_index}",
         "-t", str(seconds), "-ac", "2", "-ar", "48000", str(path)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=seconds + 30)
    return path


def mono_samples(wav_path):
    with wave.open(str(wav_path), "rb") as w:
        frames = w.readframes(w.getnframes())
        width, channels, rate = w.getsampwidth(), w.getnchannels(), w.getframerate()
    if width != 2:
        fail(f"unexpected sample width {width}")
    count = len(frames) // 2
    inter = struct.unpack("<%dh" % count, frames[:count * 2])
    return [inter[i] / 32768.0
            for i in range(0, len(inter) - channels + 1, channels)], rate


def analyse(wav_path, window_s=1.0):
    """Peak, RMS and the pitch trajectory the recording actually has. A peak
    alone would accept hum, a click or the tail of something else — for this to
    be the synth answering, the sound has to BE the note that was asked for."""
    mono, rate = mono_samples(wav_path)
    skip = int(0.2 * rate)                   # the device's own start-up click
    seg = mono[skip:]
    if not seg:
        fail(f"nothing was recorded into {wav_path}")
    peak = max(abs(s) for s in seg)
    rms = (sum(s * s for s in seg) / len(seg)) ** 0.5
    return (peak, rms,
            partial_trajectory(mono, skip, rate, window_s), len(mono) / rate)


def partial_trajectory(mono, start, rate, window_s=1.0):
    """The strongest partial of each half-overlapping window, by DFT.

    Not autocorrelation: an LRO sound MOVES by design, and the bowed-string idiom
    this runs on spreads its energy over 199…234 Hz. Correlation over a moving
    period locks onto a wrong lag (measured: 265 Hz for a note whose spectrum
    plainly peaks at 227) — the estimator, not the synth, was wrong.

    And not one fixed early window either: a bowed body may creak on higher
    modes for seconds before it settles on the note. Measured on 'Bowed
    String', same build, eight runs in one day: six had 220 Hz strongest by
    0.3–1.3 s, two sat at 601/612 Hz there and reached 220 Hz only from ~2 s
    (1214 → 612 → 224 Hz across one 3 s note). So the whole recording is
    measured and the CALLER decides what counts as the note — it requires the
    pitch to be HELD across two consecutive windows, because a single in-band
    window is also what a sweeping interferer produces in passing.

    The window follows the note: 1 s for notes of a second and up, the note
    length itself below that (floor 0.25 s, which still gives 4 Hz bins — the
    55 Hz accept band is ±6.6 Hz wide, so it keeps more than one bin). With a
    fixed 1 s window, a 0.5 s note had its energy in the high-gain region of
    only ONE window and the held-pitch rule below could not be satisfied at
    all — whether it failed then depended on the device-open delay, which the
    script cannot observe.

    Times are window starts, relative to the 0.2 s device-skip mark of the
    recording — NOT note time; the device-open delay in front is variable.
    Windows whose level never reaches 1e-4 are omitted (before the onset,
    after the tail); the caller marks the resulting time gaps in its printout."""
    import numpy as np
    out = []
    win = int(window_s * rate)
    for s in range(start, len(mono) - win + 1, win // 2):
        seg = np.asarray(mono[s:s + win], dtype=float)
        if np.abs(seg).max() < 1e-4:
            continue                         # before the onset / after the tail
        spectrum = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        freqs = np.fft.rfftfreq(len(seg), 1.0 / rate)
        band = (freqs > 40.0) & (freqs < 4000.0)
        out.append(((s - start) / rate,
                    float(freqs[band][np.argmax(spectrum[band])])))
    return out


def main():
    global APP
    ap = argparse.ArgumentParser()
    # A preset that is actually in the bank: 'Bowed Cello' was renamed and the
    # default has been failing the run it is meant to make one command long.
    ap.add_argument("--preset", default="Bowed String",
                    help="preset whose stored orchestra is played")
    ap.add_argument("--seconds", type=float, default=3.0,
                    help="note length; the default preset takes ~1-2 s of bow "
                         "creak to settle on the pitch, so a very short note "
                         "fails the pitch gate truthfully (measured at 0.5 s: "
                         "1212 Hz creak throughout, release included)")
    ap.add_argument("--app", default=str(APP),
                    help="the bundle to test (default: the one in build_clean)")
    ap.add_argument("--keep", action="store_true", help="leave the app running")
    ap.add_argument("--prove-it-can-fail", action="store_true",
                    help="take the bundled Csound away and require SILENCE — the "
                         "run that shows a green run means something")
    args = ap.parse_args()

    try:
        import rtmidi
    except ImportError:
        fail("python-rtmidi is needed to send the note (pip install python-rtmidi)")
    if not shutil.which("ffmpeg"):
        fail("ffmpeg is needed to record BlackHole")
    APP = Path(args.app).resolve()
    if not APP.is_dir():
        fail(f"no built Standalone at {APP}")
    # Refuse rather than kill: an instance already running may be someone's open
    # session, and it would in any case be feeding the same loopback device and
    # listening to the same MIDI port, which is how a stuck note from an earlier
    # run once made the recording look like a success.
    running = subprocess.run(["pgrep", "-f", str(APP / "Contents/MacOS")],
                             capture_output=True, text=True).stdout.split()
    if running:
        fail(f"akroasys is already running (pid {', '.join(running)}) — quit it "
             "first; two instances share the audio device and the MIDI port")

    scratch = Path(os.environ.get("TMPDIR", "/tmp")) / "lro_standalone_check"
    scratch.mkdir(parents=True, exist_ok=True)
    profile = sandbox_profile(scratch, deny_bundled=args.prove_it_can_fail)
    if not denial_bit(profile):
        fail("the sandbox does not deny Homebrew's Csound — this test would prove "
             "nothing")
    ok("Homebrew's Csound is unreachable inside the sandbox")
    if args.prove_it_can_fail:
        ok("and so is the bundle's own Contents/libs — this run must produce NO "
           "sound; if it does, the test is measuring something else")
        print("      EXPECTED: the app will be killed by dyld before main() "
              "(\"Library not loaded: @loader_path/../libs/CsoundLib64\") and "
              "macOS will file a crash report for it. That IS the result.")

    name, orchestra = orchestra_from_preset(args.preset)
    ok(f"orchestra taken from preset '{name}' ({len(orchestra)} chars)")
    home = throwaway_home(scratch, orchestra)

    # The virtual port must exist BEFORE the app scans MIDI devices, or the stored
    # <MIDIINPUT> entry names a device that is not there yet.
    midi = rtmidi.MidiOut()
    midi.open_virtual_port(MIDI_PORT)
    ok(f"virtual MIDI port '{MIDI_PORT}' open")

    device_index = avfoundation_index(OUT_DEVICE)
    ok(f"'{OUT_DEVICE}' is avfoundation audio device {device_index}")

    # T5YNTH_ALLOW_NO_MODELS keeps the setup wizard out of the way: this test is
    # about the oscillator, and the neural engines are not part of it.
    # CFFIXED_USER_HOME is the one that moves NSHomeDirectory, and NSHomeDirectory
    # is what JUCE asks; HOME comes along for everything below Foundation.
    env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
               T5YNTH_ALLOW_NO_MODELS="1")
    app_log = scratch / "app.log"
    with app_log.open("w") as log:
        app = subprocess.Popen(
            ["/usr/bin/sandbox-exec", "-f", str(profile),
             str(APP / "Contents/MacOS/akroasys")],
            env=env, stdout=log, stderr=subprocess.STDOUT)
    # sandbox-exec execs the app in place, so this pid IS the app and the cleanup
    # can be exact — no pkill pattern that might match something else, or (as it
    # did) nothing at all, leaving an instance holding a note into the next run.
    if not args.keep:
        LAUNCHED.append(app)
    time.sleep(18)                      # window, audio device, orchestra compile
    if app.poll() is not None:
        if args.prove_it_can_fail:
            print("PASS  without its bundled Csound the app does not even start, "
                  "so the sound in the normal run came from the bundle")
            return 0
        print(app_log.read_text()[-2000:])
        fail("the app exited instead of starting")
    log_text = app_log.read_text()
    if "Contents/libs/Opcodes64" not in log_text:
        fail("the app did not report using the BUNDLED plugin opcodes — "
             "OPCODEDIR was not overridden")
    ok("the app is up and Csound reports the bundled Opcodes64")

    # The control comes first, as its own recording: with the app up and no note
    # sent, the loopback device must be silent. BlackHole is system-wide, so
    # without this a passing browser tab would read as a synth. It is a separate
    # take rather than a lead-in inside the note's recording because ffmpeg opens
    # the device with a variable delay of up to a second — long enough to push the
    # note into the window that was supposed to prove nothing was playing.
    control = record(device_index, 1.5, scratch / "lro_control.wav")
    ctl_peak, _, _, _ = analyse(control)
    if ctl_peak >= SILENCE_FLOOR:
        fail(f"{OUT_DEVICE} is not silent with the app up and no note played "
             f"(peak {ctl_peak:.4f}) — something else is playing through it, so a "
             "recording of it would prove nothing about the synth")
    ok(f"control: {OUT_DEVICE} silent while nothing is played (peak "
       f"{ctl_peak:.2e})")

    wav = scratch / "lro_note.wav"
    # The recording must OUTLIVE the note: the device-open delay (up to ~1 s)
    # plus the 1 s pre-note sleep sit at the front, so with -t = args.seconds
    # the file used to end about a second BEFORE note-off and a body that
    # settles on the pitch late was judged on its creaky attack alone.
    rec = subprocess.Popen(
        ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
         "-f", "avfoundation", "-i", f":{device_index}",
         "-t", str(args.seconds + 1.5), "-ac", "2", "-ar", "48000", str(wav)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(1.0)                     # let the device open before the note
    midi.send_message([0x90, NOTE, 100])
    time.sleep(args.seconds)
    midi.send_message([0x80, NOTE, 0])
    rec.wait(timeout=30)
    ok(f"recorded {args.seconds:.1f} s note (MIDI {NOTE}, 220 Hz)")

    window_s = min(1.0, max(0.25, args.seconds))   # see partial_trajectory
    peak, rms, trajectory, length = analyse(wav, window_s)
    if args.prove_it_can_fail:
        if peak >= SILENCE_FLOOR:
            fail(f"the app made sound ({peak:.4f}) with its own Contents/libs "
                 "denied — then this test is not measuring the bundled Csound, "
                 "and a green normal run means nothing")
        print("PASS  with its bundled Csound taken away the app is silent, so the "
              "sound in the normal run came from the bundle")
        return 0
    if peak < SILENCE_FLOOR:
        fail(f"the app played SILENCE (peak {peak:.2e} over {length:.1f} s) — "
             f"see {app_log}")
    # ±12 % is about two semitones: wide enough for the vibrato and bow noise an
    # LRO sound is supposed to have (movement by default). The pitch gate alone
    # never excluded mains hum (50 and 60 Hz both sit inside 55 ±12 %) — the
    # control recording is what guards stationary interferers, and it precedes
    # the note, so a source that starts later must instead fail the rule below:
    # TWO CONSECUTIVE windows on the note. One in-band window is not enough,
    # because the accept bands are only ~8 semitones apart and anything that
    # SWEEPS (a glide, speech, a stream) crosses one in a single window —
    # measured: a 150→900 Hz glide that is never the note passes an any-window
    # rule. Holding a band for two adjacent windows (a window and a half of
    # overlap-connected signal) is what a played note does and a sweep does
    # not. Known boundary, accepted: pitch movement DEEPER than the ±12 % band
    # (~2.5 semitones and up) can alias against the window hop and fail runs
    # at some LFO rates — the preset this check ships with spreads 199…234 Hz,
    # inside the band with margin. Widening the rule into a real pitch tracker
    # is meter-building this tool deliberately stays out of.
    if len(trajectory) < 2:
        fail(f"could not apply the held-pitch rule: the {length:.1f} s "
             f"recording yields {len(trajectory)} sounding {window_s:.2g} s "
             "analysis window(s), and holding the note takes two")
    octaves = [220.0 * (2 ** k) for k in range(-2, 3)]
    on_note = [any(abs(hz - o) < 0.12 * o for o in octaves) for _, hz in trajectory]
    hop = window_s / 2 + 0.05               # the window step, plus slack
    held = [i for i in range(len(trajectory) - 1)
            if on_note[i] and on_note[i + 1]
            and trajectory[i + 1][0] - trajectory[i][0] <= hop]
    bits = []
    for i, (t, hz) in enumerate(trajectory):
        if i and t - trajectory[i - 1][0] > hop:
            bits.append("…")                # silent windows were skipped here
        bits.append(f"{hz:.0f}")
    path = " → ".join(bits)
    if not held:
        fail(f"the sound never holds the note that was played: its strongest "
             f"partial runs {path} Hz and MIDI {NOTE} is 220 Hz (an octave of "
             "it, held for two consecutive windows, would pass — a preset "
             "whose body does not follow the played pitch fails this by "
             "design; run the check on one that does)")
    first = trajectory[held[0]]
    ok(f"heard it: {first[1]:.1f} Hz held from {first[0]:.1f} s into the "
       f"{length:.1f} s recording ({sum(on_note)}/{len(on_note)} windows on "
       f"the note; strongest partial: {path} Hz), peak {peak:.4f}, "
       f"rms {rms:.4f}")
    print(f"PASS  the built Standalone plays the LRO with no Csound on the system")
    print(f"      recording: {wav}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    finally:
        for proc in LAUNCHED:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
