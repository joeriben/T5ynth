; A BOWED STRING -- a string, a bow ARM, and the BOX the string is mounted on.
;
; STRING  a waveguide with the bow's friction curve at the contact point: the
;         hair sticks to the string and slips off it, and that cycle is the tone
;         (Smith, ICMC 1986; Csound `wgbow`). Nothing in it is noise.
; ARM     the stroke. The hair bites at the start of the note and lets go, and
;         the weight and the speed keep moving afterwards, because a drawn bow
;         never stands still. All of it sits in the arm, none of it on the pitch.
;         The catch is NOT a control: swept, it does not travel anywhere -- the
;         stick-slip decides which mode it grabs, and the answer is not ordered.
; BOX     what radiates is the string's force at the bridge, through the body.
;         The four signature modes as a filter bank -- A0 272 Hz (the air
;         breathing through the f-holes), CBR 407, B1- 462, B1+ 551 -- and the
;         bridge hill at 2.3 kHz. Frequencies: J. Woodhouse, `Euphonics -- The
;         Science of Musical Instruments`, 5.3. The Q values are not in that
;         source and are not pretended to be; they are this entry's, by ear.
;         Without the box a waveguide string is a buzz.
; PITCH   it plays the key from the bottom of the keyboard to about 440 Hz: over
;         press x place x register, 62 of 63 settings land within 3.5 cents. From
;         880 Hz up the bow can catch a PARTIAL instead of the fundamental and
;         the note comes out an octave or a twelfth high -- 10 of 27 settings
;         there. That is what a bow near the bridge does, it is declared and not
;         bounded away, and it is why this body is a low and middle voice.
kpress  = 2.00   ; press [1.00..3.00]: the weight the hair carries into the string
kplace  = 0.130  ; place [0.050..0.230]: where along the string the bow is drawn
kbox    = 1.00   ; box [0.50..1.40]: how big the body is, cello up to violin
khill   = 0.250  ; hill [0.05..0.60]: how much the bridge sings along with it
kf      limit kfreq * koct1, 20, 12000
kcatch  = exp(-knote * 14.0)                             ; the hair bites, then lets go
kdraw   poscil 0.30, 0.190                               ; the weight the arm carries
kdraw2  poscil 0.17, 0.310
kbreath poscil 0.22, 0.130                               ; and how fast it is drawn
kpr     limit kpress * (1 + 0.55 * kcatch + kdraw + kdraw2), 0.90, 3.40
kpl     limit kplace * (1 - 0.45 * kcatch), 0.050, 0.230 ; it catches nearer the bridge
kspd    = 0.55 * (1 - 0.80 * exp(-knote * 30.0)) * (1 + kbreath)
astr    wgbow kspd, kf, kpr, kpl, 0, 0, giSine, 20       ; pressed past ~3.5 the bow
aa0     mode astr, 272 * kbox, 18                        ; forces the octave
acbr    mode astr, 407 * kbox, 20
ab1m    mode astr, 462 * kbox, 25
ab1p    mode astr, 551 * kbox, 25
ahill   mode astr, 2300 * kbox, 4
abody   = aa0 * 0.050 + acbr * 0.035 + ab1m * 0.040 + ab1p * 0.040 + ahill * khill
abodyb  balance abody, astr, 1                           ; the box is a FILTER, not a
asig    = (astr * 0.30 + abodyb * 0.85) * 1.05           ; fader: held against what
                                                         ; went into it, so `box` and
                                                         ; `hill` move colour and not
                                                         ; level, and the arm's own
                                                         ; breathing survives (both
                                                         ; sides carry it)
