#!/usr/bin/env python3
"""LCXL3 LED diagnostic probe.

Sends KNOWN RGB colours to the top encoder row so we can compare what the
device actually renders against what we send. This isolates whether the
device honours all three RGB channels in T5ynth's current DAW-mode handshake.

Run with the T5ynth app CLOSED so nothing competes for the DAW port.
Protocol (programmer's reference p.8/p.12):
  enable DAW mode : Note On ch16, note 0x0C, vel 127  (9F 0C 7F)
  set LED RGB     : F0 00 20 29 02 15 01 53 <idx> <R> <G> <B> F7   (idx == CC)
"""
import sys
import time
import mido


def rgb_sysex(idx, r, g, b):
    # mido adds the F0/F7 framing; data is the inner bytes only.
    return mido.Message('sysex', data=[0x00, 0x20, 0x29, 0x02, 0x15,
                                       0x01, 0x53, idx & 0x7F,
                                       r & 0x7F, g & 0x7F, b & 0x7F])


def main():
    select_submode = '--daw-control' in sys.argv

    names = mido.get_output_names()
    port = next((n for n in names if 'LCXL3' in n and 'DAW' in n), None)
    if not port:
        print('XL DAW port not found. Visible ports:')
        for n in names:
            print('   ', n)
        sys.exit(1)

    out = mido.open_output(port)
    print('opened:', port)

    cold = '--cold' in sys.argv
    if cold:
        # Cold-start reset: drop DAW mode so the next enable is a REAL switch
        # (device in Custom/Standalone), then mimic the synth's 100 ms defer.
        out.send(mido.Message('note_on', channel=15, note=0x0C, velocity=0))
        print('sent: DAW mode DISABLE (cold-start reset), waiting 600ms')
        time.sleep(0.6)

    # Enable DAW mode (host LED colouring is DAW-mode-only).
    out.send(mido.Message('note_on', channel=15, note=0x0C, velocity=127))
    delay = 0.1 if cold else 0.2   # 0.1 == the synth's xlLedTimer_ defer
    time.sleep(delay)
    print(f'sent: DAW mode ENABLE, waited {int(delay * 1000)}ms')

    if select_submode:
        # Hypothesis test: select DAW Control sub-mode (ch7 CC30 = 0x1E -> 0x02).
        out.send(mido.Message('control_change', channel=6, control=0x1E, value=0x02))
        time.sleep(0.2)
        print('sent: DAW Control sub-mode select (ch7 CC30=2)')

    if cold or '--rel' in sys.argv:   # cold-start mimic includes the synth's rel step
        # Hypothesis test: replicate the synth — switch the 3 encoder rows to
        # RELATIVE mode (ch7 B6h, rowIDs 0x45/0x48/0x49 = 7F) BEFORE colouring,
        # exactly as lightXLLeds() does. If the same 6 colours now render orange,
        # the relative-mode setup is what corrupts LED rendering.
        for rowid in (0x45, 0x48, 0x49):
            out.send(mido.Message('control_change', channel=6, control=rowid, value=0x7F))
        time.sleep(0.05)
        print('sent: encoder relative mode rows 1-3 (ch7 B6h 45/48/49 = 7F)')

    # Clear faders 5-12 + encoders 13-36 for a clean field.
    for cc in list(range(5, 13)) + list(range(13, 37)):
        out.send(rgb_sysex(cc, 0, 0, 0))
    time.sleep(0.05)

    if '--full' in sys.argv:
        # Replicate the ENTIRE kPage1 colour scheme exactly (LaunchControlXLLeds.h).
        # If this renders correctly via clean MIDI, every colour value + the protocol
        # are proven good and the synth's orange cast is purely a C++ send-path bug.
        ENV   = (127, 55, 0)    # amber
        LFO   = (12, 127, 104)  # teal
        DRIFT = (0, 59, 127)    # blue
        GEN   = (51, 63, 117)   # periwinkle
        FILT  = (62, 38, 127)   # violet
        FX    = (0, 94, 106)    # cyan
        VOL   = (0, 100, 41)    # green
        scheme = {
            5: GEN, 6: GEN, 7: FILT, 8: FILT, 9: FILT, 10: FX, 11: FX, 12: VOL,
            13: ENV, 14: ENV, 15: ENV, 16: ENV, 17: LFO, 18: LFO, 19: DRIFT, 20: DRIFT,
            21: ENV, 22: ENV, 23: ENV, 24: ENV, 25: LFO, 26: LFO, 27: DRIFT, 28: DRIFT,
            29: ENV, 30: ENV, 31: ENV, 32: ENV, 33: LFO, 34: LFO, 35: DRIFT, 36: DRIFT,
        }
        for cc, (r, g, b) in scheme.items():
            out.send(rgb_sysex(cc, r, g, b))
        out.close()
        print('\nSent the FULL kPage1 scheme (faders 5-12 + encoder rows 13-36).')
        print('Expected: faders = periwinkle/violet/cyan/green; encoder rows = '
              'amber(1-4) / teal(5-6) / blue(7-8) in every row.')
        return

    # Probe the TOP encoder row, left to right (CC 13..18).
    probe = [
        (13, 127,   0,   0, 'PURE RED   (127,0,0)'),
        (14,   0, 127,   0, 'PURE GREEN (0,127,0)'),
        (15,   0,   0, 127, 'PURE BLUE  (0,0,127)'),
        (16, 127, 127, 127, 'WHITE      (127,127,127)'),
        (17,  12, 127, 104, 'our TEAL   (12,127,104)  <- reported as orange'),
        (18,   0,  59, 127, 'our BLUE   (0,59,127)    <- reported as orange'),
    ]
    print('\nTop encoder row, leftmost 6 knobs:')
    for idx, r, g, b, label in probe:
        out.send(rgb_sysex(idx, r, g, b))
        print(f'  knob {idx-12} (CC{idx}): {label}')

    out.close()
    print('\nDone. Look at the TOP encoder row, leftmost 6 knobs, and report each colour.')
    print('Re-run with  --daw-control  to test the DAW-Control sub-mode hypothesis.')


if __name__ == '__main__':
    main()
