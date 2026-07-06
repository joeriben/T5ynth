#!/usr/bin/env python3
"""
LED color-science verifier for Launch Control XL 3 encoder group colors.

Pipeline per color:
  8-bit sRGB hex -> 7-bit quantize (round(c*127/255)) -> back to 8-bit for math
  -> linear sRGB -> XYZ (D65) -> Lab. CIEDE2000 for pairwise distance.
  CVD: Machado, Oliveira & Fernandes (2009) severity-1.0 matrices applied in
  linear-RGB space (the model is defined on linearized sRGB), severity = full
  dichromacy.

All ΔE2000 computed on the 7-bit-QUANTIZED colors (what the device actually shows).
"""
import numpy as np

# ---------------------------------------------------------------------------
# sRGB <-> linear <-> XYZ <-> Lab
# ---------------------------------------------------------------------------
def srgb_to_linear(c):
    c = np.asarray(c, float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def linear_to_srgb(c):
    c = np.asarray(c, float)
    c = np.clip(c, 0.0, 1.0)
    s = np.where(c <= 0.0031308, c * 12.92, 1.055 * (c ** (1/2.4)) - 0.055)
    return np.clip(s * 255.0, 0, 255)

# sRGB D65 matrix
M_RGB2XYZ = np.array([
    [0.4124564, 0.3575761, 0.1804375],
    [0.2126729, 0.7151522, 0.0721750],
    [0.0193339, 0.1191920, 0.9503041],
])
Xn, Yn, Zn = 0.95047, 1.00000, 1.08883  # D65 white

def linear_to_xyz(lin):
    return M_RGB2XYZ @ np.asarray(lin, float)

def xyz_to_lab(xyz):
    x, y, z = xyz[0]/Xn, xyz[1]/Yn, xyz[2]/Zn
    def f(t):
        d = 6/29
        return np.where(t > d**3, np.cbrt(t), t/(3*d*d) + 4/29)
    fx, fy, fz = f(x), f(y), f(z)
    L = 116*fy - 16
    a = 500*(fx - fy)
    b = 200*(fy - fz)
    return np.array([L, a, b])

def srgb8_to_lab(rgb8):
    return xyz_to_lab(linear_to_xyz(srgb_to_linear(rgb8)))

# ---------------------------------------------------------------------------
# CIEDE2000
# ---------------------------------------------------------------------------
def ciede2000(lab1, lab2):
    L1, a1, b1 = lab1
    L2, a2, b2 = lab2
    avg_Lp = (L1 + L2) / 2.0
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    avg_C = (C1 + C2) / 2.0
    G = 0.5 * (1 - np.sqrt(avg_C**7 / (avg_C**7 + 25.0**7)))
    a1p = (1 + G) * a1
    a2p = (1 + G) * a2
    C1p = np.hypot(a1p, b1)
    C2p = np.hypot(a2p, b2)
    avg_Cp = (C1p + C2p) / 2.0
    def hp(ap, b):
        h = np.degrees(np.arctan2(b, ap))
        return h + 360 if h < 0 else h
    h1p = hp(a1p, b1)
    h2p = hp(a2p, b2)
    dLp = L2 - L1
    dCp = C2p - C1p
    if C1p * C2p == 0:
        dhp = 0.0
    elif abs(h2p - h1p) <= 180:
        dhp = h2p - h1p
    elif h2p - h1p > 180:
        dhp = h2p - h1p - 360
    else:
        dhp = h2p - h1p + 360
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2.0)
    if C1p * C2p == 0:
        avg_hp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        avg_hp = (h1p + h2p) / 2.0
    elif h1p + h2p < 360:
        avg_hp = (h1p + h2p + 360) / 2.0
    else:
        avg_hp = (h1p + h2p - 360) / 2.0
    T = (1 - 0.17*np.cos(np.radians(avg_hp - 30))
         + 0.24*np.cos(np.radians(2*avg_hp))
         + 0.32*np.cos(np.radians(3*avg_hp + 6))
         - 0.20*np.cos(np.radians(4*avg_hp - 63)))
    d_ro = 30 * np.exp(-(((avg_hp - 275)/25.0)**2))
    Rc = 2 * np.sqrt(avg_Cp**7 / (avg_Cp**7 + 25.0**7))
    Sl = 1 + (0.015*(avg_Lp - 50)**2) / np.sqrt(20 + (avg_Lp - 50)**2)
    Sc = 1 + 0.045*avg_Cp
    Sh = 1 + 0.015*avg_Cp*T
    Rt = -np.sin(np.radians(2*d_ro)) * Rc
    return np.sqrt((dLp/Sl)**2 + (dCp/Sc)**2 + (dHp/Sh)**2
                   + Rt*(dCp/Sc)*(dHp/Sh))

# ---------------------------------------------------------------------------
# CVD simulation: Machado, Oliveira, Fernandes (2009), severity 1.0
# Matrices operate on LINEAR sRGB.
# ---------------------------------------------------------------------------
M_PROTAN = np.array([
    [0.152286, 1.052583, -0.204868],
    [0.114503, 0.786281,  0.099216],
    [-0.003882,-0.048116, 1.051998],
])
M_DEUTAN = np.array([
    [0.367322, 0.860646, -0.227968],
    [0.280085, 0.672501,  0.047413],
    [-0.011820, 0.042940, 0.968881],
])
M_TRITAN = np.array([
    [1.255528, -0.076749, -0.178779],
    [-0.078411, 0.930809,  0.147602],
    [0.004733,  0.691367,  0.303900],
])

def simulate_cvd(rgb8, M):
    lin = srgb_to_linear(rgb8)
    sim = M @ lin
    return linear_to_srgb(sim)  # back to 8-bit sRGB

# ---------------------------------------------------------------------------
# WCAG relative luminance & contrast ratio
# ---------------------------------------------------------------------------
def rel_luminance(rgb8):
    lin = srgb_to_linear(rgb8)
    return 0.2126*lin[0] + 0.7152*lin[1] + 0.0722*lin[2]

def contrast_ratio(rgb_a, rgb_b):
    La = rel_luminance(rgb_a)
    Lb = rel_luminance(rgb_b)
    hi, lo = max(La, Lb), min(La, Lb)
    return (hi + 0.05) / (lo + 0.05)

# ---------------------------------------------------------------------------
# 7-bit quantization
# ---------------------------------------------------------------------------
def hex2rgb(h):
    h = h.lstrip('#')
    return np.array([int(h[0:2],16), int(h[2:4],16), int(h[4:6],16)], float)

def to7bit(rgb8):
    return np.round(np.asarray(rgb8) * 127.0 / 255.0).astype(int)

def from7bit(rgb7):
    # device drives LED at 7-bit; the equivalent 8-bit value it represents.
    # Convert 7-bit code back to an 8-bit-scale color for perceptual math:
    return np.asarray(rgb7, float) * 255.0 / 127.0

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------
BODY = hex2rgb('#1a1a1c')

triads = {
    'T1 (T5ynth axes)': {
        'Env': '#FF6F00', 'LFO': '#2196F3', 'Drift': '#E91E63'},
    'T2': {
        'Env': '#FF6F00', 'LFO': '#00BCD4', 'Drift': '#4CAF50'},
    'T3 (Okabe-Ito)': {
        'Env': '#E69F00', 'LFO': '#56B4E9', 'Drift': '#CC79A7'},
    # T4 proposal: keep ENV amber, LFO = strong cyan-blue (high luma, far hue),
    # Drift = vivid green (far from both, high luma). Tuned for CVD + 7-bit.
    'T4 (proposed)': {
        'Env': '#FF6F00', 'LFO': '#18A0E0', 'Drift': '#21C45A'},
}

conditions = {
    'Normal':  None,
    'Deutan':  M_DEUTAN,
    'Protan':  M_PROTAN,
    'Tritan':  M_TRITAN,
}

def quantized_lab(hexstr, M):
    """8-bit hex -> 7-bit quant -> back to 8-bit -> (optional CVD) -> Lab."""
    rgb8 = hex2rgb(hexstr)
    rgb7 = to7bit(rgb8)
    rgb8q = from7bit(rgb7)
    if M is not None:
        rgb8q = simulate_cvd(rgb8q, M)
    return srgb8_to_lab(rgb8q)

THRESH = 20.0

print("="*78)
print("7-BIT QUANTIZED VALUES + LUMINANCE / CONTRAST ON BODY #1a1a1c")
print("="*78)
print(f"{'Triad':<18}{'Grp':<6}{'hex':<9}{'7bit RGB':<16}{'relLum':<9}{'contrast':<9}")
for tname, t in triads.items():
    for grp, hx in t.items():
        rgb7 = to7bit(hex2rgb(hx))
        rgb8q = from7bit(rgb7)
        lum = rel_luminance(rgb8q)
        cr = contrast_ratio(rgb8q, BODY)
        print(f"{tname:<18}{grp:<6}{hx:<9}"
              f"rgb({rgb7[0]:>3},{rgb7[1]:>3},{rgb7[2]:>3})  "
              f"{lum:<9.4f}{cr:<9.2f}")
    print()

print("="*78)
print(f"PAIRWISE CIEDE2000 (7-bit quantized).  SAFE threshold = {THRESH}")
print("="*78)
results = {}
for tname, t in triads.items():
    pairs = [('Env','LFO'), ('LFO','Drift'), ('Env','Drift')]
    print(f"\n--- {tname} ---")
    print(f"{'pair':<14}" + "".join(f"{c:<10}" for c in conditions))
    triad_min = {}
    for (g1, g2) in pairs:
        row = f"{g1+'|'+g2:<14}"
        pmin = 1e9
        for cname, M in conditions.items():
            lab1 = quantized_lab(t[g1], M)
            lab2 = quantized_lab(t[g2], M)
            de = ciede2000(lab1, lab2)
            pmin = min(pmin, de)
            row += f"{de:<10.1f}"
        triad_min[(g1,g2)] = pmin
        flag = "OK" if pmin >= THRESH else "FAIL"
        row += f"  min={pmin:.1f} [{flag}]"
        print(row)
    overall = min(triad_min.values())
    verdict = "PASS" if overall >= THRESH else "FAIL"
    # also note the worst luminance/contrast
    worst_cr = min(contrast_ratio(from7bit(to7bit(hex2rgb(hx))), BODY)
                   for hx in t.values())
    results[tname] = (overall, verdict, worst_cr)
    print(f"  >>> TRIAD MIN ΔE2000 = {overall:.1f}  =>  {verdict}"
          f"   (worst contrast-on-black = {worst_cr:.2f}:1)")

print("\n" + "="*78)
print("SUMMARY")
print("="*78)
for tname, (ov, v, cr) in results.items():
    print(f"{tname:<18} min ΔE2000={ov:6.1f}  worst-contrast={cr:5.2f}:1  -> {v}")
