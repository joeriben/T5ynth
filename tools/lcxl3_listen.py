#!/usr/bin/env python3
"""LCXL3 MIDI input capture — ground-truth what each physical control transmits.

Puts the XL into the SAME state the synth uses (DAW mode + relative encoders) and
logs every incoming message, so we can see exactly which CC / channel / value a
given fader or encoder sends. Resolves the "A/B fader looks dead but the code says
CC5/ch16 → gen_alpha should work" contradiction by measurement, not guessing.

Run with the T5ynth app CLOSED so nothing else drives the port.

Protocol (programmer's reference):
  enable DAW mode      : Note On ch16, note 0x0C, vel 127   (9F 0C 7F)   p.8
  encoder relative mode: CC ch7  (B6h) rowID 0x45/0x48/0x49 = 0x7F        p.10
  faders/encoders out  : channel 16 ;  buttons out: channel 1            p.9
"""
import sys
import time
import mido


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0

    outs = mido.get_output_names()
    ins  = mido.get_input_names()
    out_port = next((n for n in outs if 'LCXL3' in n and 'DAW' in n), None)
    in_port  = next((n for n in ins  if 'LCXL3' in n and 'DAW' in n), None)
    if not out_port or not in_port:
        print('XL DAW port not found.')
        print('  outputs:', outs)
        print('  inputs :', ins)
        sys.exit(1)

    out = mido.open_output(out_port)
    out.send(mido.Message('note_on', channel=15, note=0x0C, velocity=127))  # DAW mode on
    time.sleep(0.1)
    for rowid in (0x45, 0x48, 0x49):                                        # encoders → relative
        out.send(mido.Message('control_change', channel=6, control=rowid, value=0x7F))
    time.sleep(0.1)
    print(f'DAW mode + relative encoders enabled on: {out_port}')
    print(f'Listening on: {in_port}  for {duration:.0f}s')
    print('Move the controls now. Each (channel, type, cc/note) is aggregated below.\n')

    # key -> dict(count, vmin, vmax, first, last)
    agg = {}
    inp = mido.open_input(in_port)
    t_end = time.monotonic() + duration
    while time.monotonic() < t_end:
        for msg in inp.iter_pending():
            if msg.type == 'control_change':
                key = ('CC', msg.channel + 1, msg.control)
                val = msg.value
            elif msg.type in ('note_on', 'note_off'):
                key = (msg.type, msg.channel + 1, msg.note)
                val = msg.velocity
            else:
                key = (msg.type, getattr(msg, 'channel', -1) + 1, -1)
                val = getattr(msg, 'value', -1)
            a = agg.get(key)
            if a is None:
                agg[key] = {'count': 1, 'vmin': val, 'vmax': val, 'first': val, 'last': val}
                print(f'  NEW  {key[0]:10} ch{key[1]:<2} #{key[2]:<4} val={val}')
            else:
                a['count'] += 1
                a['vmin'] = min(a['vmin'], val)
                a['vmax'] = max(a['vmax'], val)
                a['last'] = val
        time.sleep(0.005)

    print('\n================ SUMMARY (sorted by channel, then number) ================')
    for key in sorted(agg.keys(), key=lambda k: (k[1], k[2], k[0])):
        a = agg[key]
        print(f'  {key[0]:10} ch{key[1]:<2} #{key[2]:<4} '
              f'count={a["count"]:<5} val {a["vmin"]}..{a["vmax"]} (first={a["first"]} last={a["last"]})')
    out.close()
    inp.close()


if __name__ == '__main__':
    main()
