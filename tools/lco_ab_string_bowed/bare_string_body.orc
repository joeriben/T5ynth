; A BOWED STRING -- a waveguide string with the bow's friction curve at the
; contact point: the hair sticks to the string and slips off it, and that cycle
; is the tone. Nothing in it is noise. (Smith, ICMC 1986; Csound `wgbow`.)
kpress  = 2.40   ; press [1.00..3.00]: how hard the hair is pushed into the string
kplace  = 0.127  ; place [0.050..0.230]: where along the string the bow is drawn
kf      limit kfreq * koct1, 20, 12000
karm1   poscil 0.130, 0.170                             ; the arm is never steady, and
karm2   poscil 0.070, 0.290                             ;   that is where a bowed note's
kpr     limit kpress * (1 + karm1 + karm2), 0.90, 3.40  ;   life comes from -- the
abow    wgbow 0.55, kf, kpr, kplace, 0, 0, giSine, 20   ;   pressure, never the pitch
asig    = abow * 0.95                                   ; pressed past ~3.5 the bow
                                                        ; forces the octave; above
                                                        ; ~1 kHz a light bow can
                                                        ; catch a partial and whistle
