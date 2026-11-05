"""Velora ad music: original, pure numpy synthesis (no samples, no third-party material). Writes public/music_part1.wav (timeline 0-13 s) and public/music_part2.wav (timeline 12-30 s).
Part 1: bright, upbeat guitar-pop groove (strummed Karplus-Strong guitar, claps, side-chained synth chords, bouncy bass), UI clicks/whooshes, tape-stop.
Part 2: confident minimal modern groove (tight kick, claps, short sub bass, muted Rhodes stabs), no siren or big drop; resolves to a warm major chord.
The reference commercials cannot be copied or sampled; drop licensed audio into public/ and point Ad.tsx at it instead."""
import numpy as np, wave, os
SR = 44100
rng = np.random.default_rng(5)

def t(d): return np.arange(int(d * SR)) / SR
def mtof(m): return 440.0 * 2 ** ((m - 69) / 12)
def lp(x, fc):
    a = 1 - np.exp(-2 * np.pi * fc / SR); y = np.empty_like(x); z = 0.0
    for i, v in enumerate(x): z += a * (v - z); y[i] = z
    return y
def bp(x, lo, hi): return lp(x, hi) - lp(x, lo)
def ar(n, a=0.003, r=0.05):
    e = np.ones(n); a = max(1, int(a * SR)); r = max(1, int(r * SR))
    e[:a] = np.linspace(0, 1, a); e[-r:] *= np.linspace(1, 0, r); return e
def place(buf, snd, at, gain=1.0, pan=0.0):
    i = int(at * SR); n = min(len(snd), buf.shape[1] - i)
    if n <= 0 or i < 0: return
    l = np.cos((pan + 1) * np.pi / 4); r = np.sin((pan + 1) * np.pi / 4)
    buf[0, i:i + n] += snd[:n] * gain * l; buf[1, i:i + n] += snd[:n] * gain * r
def phase(f): return 2 * np.pi * np.cumsum(f) / SR
def saw(f): return 2 * ((np.cumsum(f) / SR) % 1.0) - 1

# ---------------- instruments ----------------
def kick(deep=False):
    tt = t(.4); f = (40 if deep else 50) + 130 * np.exp(-tt * 32)
    return np.sin(phase(f)) * np.exp(-tt * (6 if deep else 9)) * 1.15
def clap():
    d = .3; n = bp(rng.standard_normal(int(d * SR)), 900, 3600); tt = t(d)
    e = np.exp(-tt * 16)
    for k in (0.0, 0.011, 0.023): e += 0.9 * np.exp(-np.clip(tt - k, 0, None) * 140) * (tt >= k)
    return n * e * .6
def snare():
    d = .3; tt = t(d); n = bp(rng.standard_normal(len(tt)), 1200, 7000)
    return n * np.exp(-tt * 15) * .8 + np.sin(phase(190 + 60 * np.exp(-tt * 40))) * np.exp(-tt * 25) * .5
def hat(o=False, vel=1.0):
    d = .25 if o else .05; tt = t(d); n = rng.standard_normal(len(tt)); n = n - lp(n, 7500)
    return n * np.exp(-tt * (14 if o else 110)) * .32 * vel
def shaker():
    d = .07; tt = t(d); n = bp(rng.standard_normal(len(tt)), 4000, 9000)
    return n * np.exp(-tt * 45) * .28 * (1 - np.exp(-tt * 400))
def click():                      # tactile UI click
    tt = t(.05); return (np.sin(2 * np.pi * 2600 * tt) * np.exp(-tt * 140) * .6 + bp(rng.standard_normal(len(tt)), 2000, 9000) * np.exp(-tt * 200) * .8)
def tick():
    tt = t(.04); return np.sin(2 * np.pi * 1500 * tt) * np.exp(-tt * 160) * .5
def whoosh(d=.5, up=True):
    tt = t(d); n = rng.standard_normal(len(tt)); x = bp(n, 300, 5000) * (np.sin(np.pi * tt / d) ** 2)
    return x * .5
def riser(d=1.0, hi=7000):
    tt = t(d); n = bp(rng.standard_normal(len(tt)), 800, hi); return n * (tt / d) ** 2.2 * .45
def crash(d=3.0):
    tt = t(d); n = rng.standard_normal(len(tt)); n = n - lp(n, 3500); return n * np.exp(-tt * 1.5) * .45
def piano(m, d=1.6, bright=1.0):
    f = mtof(m); tt = t(d); x = np.zeros(len(tt))
    for k, (a, dec) in enumerate(((1, 2.4), (.55, 3.2), (.3, 4.2), (.16, 6), (.08, 8))):
        x += a * bright ** (k * .4) * np.sin(2 * np.pi * f * (k + 1) * (1 + 0.00025 * (k + 1) ** 2) * tt) * np.exp(-tt * dec)
    x += .3 * np.sin(2 * np.pi * f * tt) * np.exp(-tt * 40)  # hammer
    return x * ar(len(tt), .002, .18) * .5
def bell(m, d=1.8):
    f = mtof(m); tt = t(d)
    return (np.sin(2 * np.pi * f * tt) + .5 * np.sin(2 * np.pi * f * 2.76 * tt) * np.exp(-tt * 3) + .3 * np.sin(2 * np.pi * f * 5.4 * tt) * np.exp(-tt * 6)) * np.exp(-tt * 2.2) * ar(len(tt), .002, .3) * .45
def lead(m, d, bright=1.0):
    f = mtof(m); tt = t(d); vib = 1 + .004 * np.sin(2 * np.pi * 5.5 * tt) * np.minimum(1, tt * 4)
    x = (saw(f * vib) + saw(f * vib * 1.007)) * .5
    x = lp(x, 2400 * bright)
    return x * ar(len(tt), .01, .12) * .4
def pad(notes, d, a=1.5, r=1.8, bright=.25):
    tt = t(d); out = np.zeros(len(tt))
    for m in notes:
        for dt in (-.004, .004):
            out += np.sin(2 * np.pi * mtof(m) * (1 + dt) * tt + rng.random() * 6) + bright * np.sin(4 * np.pi * mtof(m) * (1 + dt) * tt)
    return out / (len(notes) * 2) * ar(len(tt), a, r) * (1 + .1 * np.sin(2 * np.pi * .13 * tt))
def sub_bass(m, d):
    f = mtof(m); tt = t(d); return np.sin(2 * np.pi * f * tt) * ar(len(tt), .01, .08) * .7
def b808(m, d, glide_to=None):
    tt = t(d); f = mtof(m) * (1 + 1.2 * np.exp(-tt * 45))
    if glide_to is not None: f = f * (mtof(glide_to) / mtof(m)) ** (np.clip(tt / d, 0, 1) ** 2)
    x = np.tanh(2.2 * np.sin(phase(f))) * np.exp(-tt * 1.6) * ar(len(tt), .002, .06)
    return x * .75
def siren(d, lo=620, hi=1180, per=1.7):
    tt = t(d); f = lo + (hi - lo) * (0.5 + 0.5 * np.sin(2 * np.pi * tt / per - 1.2))
    x = lp(saw(f), 3800) + .3 * np.sin(phase(f * 2)); return x * ar(len(tt), .3, .4) * .22
def stab(notes, d=.5):
    tt = t(d); x = np.zeros(len(tt))
    for m in notes: x += saw(np.full(len(tt), mtof(m))) + saw(np.full(len(tt), mtof(m) * 1.006))
    x = lp(x / (len(notes) * 2), 900 + 2800 * 1) * np.exp(-tt * 7); return x * ar(len(tt), .003, .06) * .5
def zap(d=.35, up=False):
    tt = t(d); f = (200 + 3500 * np.exp(-tt * 10)) if not up else (200 + 3500 * (tt / d) ** 2)
    x = np.sin(phase(f) + 3 * np.sin(phase(f * 1.5))) + .4 * bp(rng.standard_normal(len(tt)), 1500, 8000)
    return x * np.exp(-tt * 6) * ar(len(tt), .001, .05) * .5
def reverb(buf, wet=.2):
    out = buf.copy()
    for dly, g in ((.037, .5), (.061, .42), (.097, .34), (.151, .27), (.223, .2), (.317, .14), (.43, .1)):
        k = int(dly * SR); out[0, k:] += buf[1, :-k] * g * wet * 2; out[1, k:] += buf[0, :-k] * g * wet * 2
    return out
def save(name, buf):
    buf = np.tanh(buf * 1.1); buf = buf / np.abs(buf).max() * .89
    with wave.open(os.path.join('public', name), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes((buf.T * 32767).astype('<i2').tobytes())
    print(name, round(buf.shape[1] / SR, 2), 's')
def tape_stop(buf, start, dur):
    i0 = int(start * SR); n = int(dur * SR); seg = buf[:, i0:i0 + n].copy()
    speed = np.linspace(1, 0.04, n) ** 1.6; pos = np.cumsum(speed); pos = np.clip(pos, 0, n - 1)
    for c in range(2): buf[c, i0:i0 + n] = np.interp(pos, np.arange(n), seg[c]) * np.linspace(1, 0, n) ** .5
    buf[:, i0 + n:] = 0


# ---- extra instruments for the revised mixes ----
def guitar(m, d=.7, bright=1.0):          # Karplus-Strong plucked string (clean electric/acoustic guitar-like)
    f = mtof(m); n = int(d * SR); L = max(2, int(SR / f))
    line = (rng.random(L) * 2 - 1) * bright; out = np.empty(n); p = 0
    for i in range(n):
        v = line[p]; out[i] = v
        line[p] = .5 * (v + line[(p + 1) % L]) * .9965; p = (p + 1) % L
    return out * ar(n, .001, .08) * .55
def strum(notes, gain_curve=None, spread=.012):
    return [(k * spread, n) for k, n in enumerate(notes)]
def rhodes(m, d=1.0, soft=1.0):
    f = mtof(m); tt = t(d)
    x = np.sin(2 * np.pi * f * tt + (1.1 * soft) * np.exp(-tt * 6) * np.sin(2 * np.pi * f * tt)) * np.exp(-tt * 3.2)
    x += .12 * np.sin(2 * np.pi * f * 12 * tt) * np.exp(-tt * 35)
    return x * ar(len(tt), .003, .12) * .5
def sidechain(buf, beats, depth=.55, rel=.22):
    env = np.ones(buf.shape[1])
    for b in beats:
        i = int(b * SR); n = int(rel * SR)
        if i >= len(env): continue
        n = min(n, len(env) - i); env[i:i + n] = np.minimum(env[i:i + n], 1 - depth * np.exp(-np.linspace(0, 5, n)))
    return buf * env
def soft_swish(d=.4): return whoosh(d) * .6

C, G, Am, F = [60, 64, 67], [59, 62, 67], [57, 60, 64], [57, 60, 65]
CHORDS1 = [C, G, Am, F]; ROOT1 = [36, 31, 33, 29]
BAR = 2.0

# ================= Part 1: bright, upbeat guitar-pop groove with claps (timeline 0-13 s) =================
buf = np.zeros((2, int(13 * SR)))
padb = np.zeros_like(buf)
for i in range(6):                                                       # bright synth chord bed (side-chained)
    ch = CHORDS1[i % 4]; tt = t(2.3); bed = np.zeros(len(tt))
    for n in ch + [ch[0] + 12]: bed += saw(np.full(len(tt), mtof(n))) + saw(np.full(len(tt), mtof(n) * 1.005))
    bed = lp(bed / 8, 2600) * ar(len(tt), .02, .25)
    place(padb, bed, i * BAR, .26)
beats = [b * .5 for b in range(0, 24)]
buf += sidechain(padb, beats, .6, .25)
for n, at in zip((84, 88, 91), (.2, .36, .52)): place(buf, bell(n, 1.2), at, .45, pan=(at - .4))        # power-up chime
place(buf, whoosh(.9), 0.0, .3)
for k in range(7): place(buf, tick(), .35 + k * .09, .25 if k == 0 else .12, pan=-.5 + k * .17)         # taskbar items pop from the centre
place(buf, click(), 2.6, .5); place(buf, whoosh(.45), 3.0, .4)
for bar in range(0, 6):                                                  # groove
    b0 = bar * BAR; full = bar >= 1
    for q in range(4):
        place(buf, kick(), b0 + q * .5, .55 + .2 * full)
        place(buf, shaker(), b0 + q * .5 + .25, .8, pan=.4 if q % 2 else -.4)
        if full: place(buf, hat(vel=.6), b0 + q * .5 + .25, .5, pan=.3)
    if full:
        place(buf, clap(), b0 + .5, .75); place(buf, clap(), b0 + 1.5, .75)
    place(buf, hat(True), b0 + 1.75, .35)
    ch = CHORDS1[bar % 4]; root = ROOT1[bar % 4]
    for pos, o in ((0, 0), (.75, 12), (1.0, 0), (1.5, 0), (1.75, 12)): place(buf, sub_bass(root + o, .3), b0 + pos, .75)
for bar in range(1, 6):                                                  # strummed guitar on the off-beats
    ch = CHORDS1[bar % 4]; b0 = bar * BAR
    for pos in (.25, .75, 1.25, 1.75):
        for k, n in enumerate(ch + [ch[0] + 12]): place(buf, guitar(n + 12, .45), b0 + pos + k * .012, .17, pan=-.25 + .1 * k)
place(buf, click(), 6.0, .8); place(buf, whoosh(.55), 5.0, .5); place(buf, click(), 5.35, .4)             # drag + snap
place(buf, whoosh(.6), 7.5, .5)                                                                          # widgets slide in
place(buf, riser(1.6), 10.2, .8); place(buf, whoosh(1.2), 10.8, .5)                                      # dive through the glass
buf = buf[:, :int(12.35 * SR)]
tape_stop(buf, 11.55, .8)
buf = np.concatenate([buf, np.zeros((2, int(13 * SR) - buf.shape[1]))], axis=1)
save('music_part1.wav', reverb(buf, .12))

# ================= Part 2: confident, minimal modern groove, no big drop (timeline 12-30 s = local 0-18 s) =================
buf = np.zeros((2, int(18 * SR)))
Dm, Bb, Cc, Gm = [62, 65, 69, 72], [58, 62, 65, 69], [60, 64, 67, 71], [55, 58, 62, 65]    # Dm9-ish, Bbmaj7, Cmaj7, Gm7
CH2 = [Dm, Bb, Cc, Gm]; RT2 = [26, 22, 24, 31]
place(buf, soft_swish(.6), 0.0, .5); place(buf, kick(True), 0.0, .8); place(buf, sub_bass(26, 1.2), 0.0, .7)
for bar in range(0, 6):                                                  # bars of 2 s from local 0.0
    b0 = bar * BAR; ch = CH2[bar % 4]; root = RT2[bar % 4]
    for q in range(4):
        if q in (0, 2) or (q == 3 and bar % 2): place(buf, kick(True), b0 + q * .5 + (0 if q != 3 else .25), .75)
        place(buf, hat(vel=1.0 if q % 2 == 0 else .55), b0 + q * .5, .45, pan=.3)
        place(buf, hat(vel=.45), b0 + q * .5 + .25, .4, pan=-.3)
    place(buf, clap(), b0 + .5, .55); place(buf, clap(), b0 + 1.5, .55)
    for pos, d in ((0, .4), (.75, .22), (1.25, .3), (1.75, .2)): place(buf, sub_bass(root + (12 if pos == .75 else 0), d), b0 + pos, .85)
    for pos in (.0, .75, 1.25):                                          # muted Rhodes stabs
        for k, n in enumerate(ch): place(buf, rhodes(n, .55), b0 + pos + k * .006, .2, pan=-.2 + .13 * k)
    if 2 <= bar <= 4:                                                    # montage section: busier hats
        for k in range(16): place(buf, hat(vel=.35), b0 + k * .125 + .0625, .3, pan=.2)
place(buf, soft_swish(.5), 7.5, .5); place(buf, soft_swish(.5), 9.5, .45)   # text rewrite + layers
for at in (7.8, 8.2, 8.6): place(buf, tick(), at, .25, pan=(at - 8.2))
buf = buf[:, :int(12 * SR)]; buf = np.concatenate([buf, np.zeros((2, int(6 * SR)))], axis=1)
place(buf, kick(True), 12.0, .75); place(buf, clap(), 12.0, .45); place(buf, sub_bass(24, 4.0), 12.0, .8)   # resolve to C at 24 s
for bar in range(2):
    b0 = 12.0 + bar * BAR
    for q in range(4):
        place(buf, kick(True), b0 + q * .5, .45); place(buf, shaker(), b0 + q * .5 + .25, .6)
    place(buf, clap(), b0 + .5, .5); place(buf, clap(), b0 + 1.5, .5)
for bar in range(2):
    for pos in (0, .75, 1.25):
        for k, n in enumerate(C + [72]): place(buf, guitar(n + 12, .5), 12.0 + bar * BAR + pos + k * .012, .17, pan=-.2 + .13 * k)
FIN = 16.0                                                               # final blended chord: warm major, rings out
for k, n in enumerate((48, 55, 60, 64, 67, 71, 74)): place(buf, rhodes(n, 3.0, .8), FIN + k * .02, .34, pan=-.4 + .13 * k)
for k, n in enumerate((60, 64, 67, 72)): place(buf, guitar(n, 2.0), FIN + .02 * k, .22)
place(buf, sub_bass(24, 2.6), FIN, .7); place(buf, bell(96, 2.4), FIN, .22)
buf[:, int(16.8 * SR):] *= np.linspace(1, .4, buf.shape[1] - int(16.8 * SR))
buf[:, :int(.05 * SR)] *= np.linspace(0, 1, int(.05 * SR))
save('music_part2.wav', reverb(buf, .14))
