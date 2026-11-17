#!/usr/bin/env python3
"""The Omni's sound effects (spec §3, §10; Stage 3 Task 13, the alarm Stage 5 Task 5), written as raw PCM into firmware/apps/omni/assets/.

Every sound here is synthesised from scratch by the code below: oscillators, a seeded noise generator and filters.
Nothing is sampled, traced, transcribed or imitated from any recording, the show included.

  sfx_latch.pcm   ~140 ms  two filtered-noise ticks 35 ms apart, each ringing a short 2.6 kHz resonant ping
  sfx_whoosh.pcm  ~2.2 s   band-passed noise, its centre swept 300 -> 1800 Hz and back; the envelope (and the sweep)
                           follow omni_spin_off's speed curve (omni_model.c: fast attack, long tail)
  sfx_surge.pcm   ~2.4 s   a rising detuned saw pair 80 -> 900 Hz plus a noise swell, peaking at 2.23 s (the flash)
  sfx_chime.pcm   ~1.2 s   a two-partial bell, 1320 Hz and 1980 Hz with exponential decays (the green lens)
  sfx_alarm.pcm   1.2 s    the timer's alarm: a two-strike bell (880 Hz and 1320 Hz with exponential decays), struck at
                           0 and 0.35 s

Each is raw s16le, mono, 16 kHz, at most 3 s, peaking at -3 dBFS (re 32767) with 5 ms (80-sample) linear edges.
Deterministic: a fixed-seed xorshift32 noise source and plain float maths, so a second run writes the same bytes on the
same Python and libm (math.sin/exp/pow may round differently elsewhere, by an LSB here and there). So the committed
.pcm files are the assets: tools/assets.sh only regenerates them with --sfx, and otherwise compares (and warns).
The script asserts, for each file (plan D4): duration <= 3 s, peak -3 +- 0.1 dBFS, the first and last 80 samples within
the ramp envelope, and an even byte length. The files are headerless, so the format (s16le, mono, 16 kHz) is set by
construction (struct '<h', SR); what is asserted is that the file read back as s16le is exactly the synthesised
samples (when writing), and assets.sh's check() holds the manifest's samples, rate and channels against the bytes.

Usage: tools/gen_sfx.py write [dir] [--only NAME...]
                                             synthesise and write dir/sfx_*.pcm (default firmware/apps/omni/assets);
                                             --only writes just the named effects (e.g. sfx_alarm), never the others
       tools/gen_sfx.py compare [dir]        synthesise in memory; WARN for each file in dir that differs (never writes)
       tools/gen_sfx.py manifest dir json    assert dir's sfx_*.pcm (D4) and write their PCM manifest entries to json
Listen on the host: aplay -f S16_LE -r 16000 -c 1 firmware/apps/omni/assets/sfx_whoosh.pcm
"""
import json
import math
import os
import struct
import sys

SR = 16000
MAX_S = 3.0
EDGE = 80                                   # 5 ms at 16 kHz
FS = 32767
PEAK_DBFS = -3.0
TARGET = round(FS * 10 ** (PEAK_DBFS / 20))  # 23197
TAU = 2 * math.pi


class Noise:
    """xorshift32 with a fixed seed: white noise in [-1, 1), the same sequence on every run."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF or 1

    def white(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s / 2147483648.0 - 1.0


class BandPass:
    """RBJ band-pass (0 dB peak gain), direct form I; set() may retune it every sample."""

    def __init__(self):
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.c = (0.0, 0.0, 0.0, 0.0, 0.0)

    def set(self, f, q):
        w = TAU * f / SR
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        self.c = (alpha / a0, 0.0, -alpha / a0, -2 * math.cos(w) / a0, (1 - alpha) / a0)

    def run(self, x):
        b0, b1, b2, a1, a2 = self.c
        y = b0 * x + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def polyblep(t, dt):
    """PolyBLEP residual for a saw's step at phase 0 (t in [0, 1), dt the phase increment): no audible aliasing."""
    if t < dt:
        t /= dt
        return t + t - t * t - 1.0
    if t > 1.0 - dt:
        t = (t - 1.0) / dt
        return t * t + t + t + 1.0
    return 0.0


# ------------------------------------------------------------------------------------------------ the effects
def latch():
    """Two ticks 35 ms apart (the second softer): short decaying noise bursts, band-passed high, each ringing a
    2.6 kHz resonance (its impulse response: a damped sine)."""
    n = round(0.140 * SR)
    rnd = Noise(0x1A7C4)
    bp = BandPass()
    bp.set(3800, 0.9)
    ticks = ((0.006, 1.0), (0.041, 0.72))   # both past the 5 ms edge
    out = []
    for i in range(n):
        t = i / SR
        exc, ping = 0.0, 0.0
        for t0, a in ticks:
            dt = t - t0
            if 0 <= dt < 0.008:
                exc += a * math.exp(-dt / 0.0011)
            if dt >= 0:
                ping += a * math.sin(TAU * 2600 * dt) * math.exp(-dt / 0.016)
        out.append(0.9 * bp.run(exc * rnd.white()) + 0.45 * ping)
    return out


def spin_speed(s):
    """omni_spin_off's speed: d/ds of (1 - s^1.55)^3, up to its constant: s^0.55 (1 - s^1.55)^2 (fast attack, long
    tail), normalised by the caller."""
    s = min(max(s, 0.0), 1.0)
    return s ** 0.55 * (1 - s ** 1.55) ** 2


def whoosh():
    """Noise through a band-pass whose centre follows the spin's speed (300 Hz at rest, 1800 Hz at the fastest, log
    spaced), with an airy layer an octave and a bit above; loudness follows the same speed curve."""
    n = round(2.2 * SR)
    vmax = max(spin_speed(k / 4096) for k in range(4097))
    rnd = Noise(0x5EED0)
    lo, hi = BandPass(), BandPass()
    out = []
    for i in range(n):
        env = spin_speed(i / (n - 1)) / vmax
        fc = 300 * 6 ** env
        lo.set(fc, 2.5)
        hi.set(min(fc * 1.9, 0.45 * SR), 4.0)
        w = rnd.white()
        out.append(env * (lo.run(w) + 0.35 * env * hi.run(w)))
    return out


def surge():
    """Two saws 1.2 % apart rising exponentially 80 -> 900 Hz to the flash at 2.23 s, through a one-pole low-pass that
    opens with them, plus band-passed noise swelling to the same peak; a 45 ms fall after it. The saws start so that
    they are in phase at 2.23 s: their slow beat crests on the flash, not before it."""
    n = round(2.4 * SR)
    tp = 2.23
    D = (1 - 0.006, 1 + 0.006)
    rnd = Noise(0x50A6E)
    bp = BandPass()
    lag = sum(80 * (900 / 80) ** (min(i / SR, tp) / tp) * (D[1] - D[0]) / SR for i in range(round(tp * SR)))
    ph = [0.0, (-lag) % 1.0]   # saw 1 gains `lag` cycles on saw 0 by tp
    lp = 0.0
    out = []
    for i in range(n):
        t = i / SR
        u = min(t, tp) / tp
        f = 80 * (900 / 80) ** u
        saw = 0.0
        for k, d in enumerate(D):
            dt = f * d / SR
            saw += 2 * ph[k] - 1 - polyblep(ph[k], dt)
            ph[k] += dt
            if ph[k] >= 1:
                ph[k] -= 1
        fcut = min(3 * f + 200, 0.45 * SR)
        lp += (1 - math.exp(-TAU * fcut / SR)) * (0.5 * saw - lp)
        bp.set(min(2 * f + 400, 0.45 * SR), 0.7)
        noise = bp.run(rnd.white())
        env = 0.15 + 0.85 * u ** 1.6 if t <= tp else math.exp(-(t - tp) / 0.045)   # audible from the start
        out.append(env * (lp + 0.6 * u ** 1.4 * noise))
    return out


def chime():
    """1320 Hz and 1980 Hz (a fifth above), each decaying exponentially (the brighter partial dies first), after a
    4 ms strike (so the peak lies past the 5 ms edge)."""
    n = round(1.2 * SR)
    out = []
    for i in range(n):
        t = i / SR
        strike = 1 - math.exp(-t / 0.004)
        out.append(strike * (math.sin(TAU * 1320 * t) * math.exp(-t / 0.42) +
                             0.55 * math.sin(TAU * 1980 * t) * math.exp(-t / 0.26)))
    return out


def alarm():
    """The timer's alarm (Stage 5): a bell struck twice, at 0 and 0.35 s. Each strike sounds 880 Hz and 1320 Hz (a fifth
    above), each decaying exponentially (the brighter partial dies first), after a 4 ms strike (so the first peak lies
    past the 5 ms edge). Original: two sine partials, nothing else."""
    n = round(1.2 * SR)
    out = []
    for i in range(n):
        t = i / SR
        v = 0.0
        for t0 in (0.0, 0.35):
            dt = t - t0
            if dt >= 0:
                strike = 1 - math.exp(-dt / 0.004)
                v += strike * (math.sin(TAU * 880 * dt) * math.exp(-dt / 0.38) +
                               0.5 * math.sin(TAU * 1320 * dt) * math.exp(-dt / 0.22))
        out.append(v)
    return out


EFFECTS = (("sfx_latch", latch), ("sfx_whoosh", whoosh), ("sfx_surge", surge), ("sfx_chime", chime),
           ("sfx_alarm", alarm))


# ------------------------------------------------------------------------------------------------ edges, level, checks
def ramp(i, n):
    """The edge envelope: linear over the first and last EDGE samples (0 at the very ends), 1 between."""
    return min(1.0, i / EDGE, (n - 1 - i) / EDGE)


def finish(x):
    """5 ms edges, then scaled to the -3 dBFS peak and rounded to s16."""
    n = len(x)
    y = [v * ramp(i, n) for i, v in enumerate(x)]
    k = TARGET / max(abs(v) for v in y)
    return [max(-FS, min(FS, round(v * k))) for v in y]


def check(name, path, want=None):
    """Reads the file back and asserts the D4 rules (and, given want, that it decodes as s16le to exactly those
    samples); returns its sample count and peak."""
    data = open(path, 'rb').read()
    assert len(data) % 2 == 0, f'{name}: odd byte length {len(data)}'
    n = len(data) // 2
    s = struct.unpack(f'<{n}h', data)
    assert want is None or list(s) == want, f'{name}: the file does not read back as the samples written'
    assert n / SR <= MAX_S, f'{name}: {n / SR:.3f} s > {MAX_S} s'
    pk = max(abs(v) for v in s)
    db = 20 * math.log10(pk / FS)
    assert abs(db - PEAK_DBFS) <= 0.1, f'{name}: peak {db:.3f} dBFS'
    for i in list(range(EDGE)) + list(range(n - EDGE, n)):
        assert abs(s[i]) <= TARGET * ramp(i, n) + 1, f'{name}: sample {i} = {s[i]} outside the 5 ms edge ramp'
    return n, db


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    args = sys.argv[1:]
    mode = args.pop(0) if args else ''
    only = None
    if mode == 'write' and '--only' in args:   # write [dir] --only NAME...: the names run to the end
        k = args.index('--only')
        args, only = args[:k], args[k + 1:]
    known = [name for name, _ in EFFECTS]
    if mode not in ('write', 'compare', 'manifest') or (mode == 'manifest' and len(args) != 2) or len(args) > 2 or \
            (mode == 'write' and len(args) > 1) or (only is not None and (not only or set(only) - set(known))):
        if only is not None and set(only) - set(known):
            print(f'unknown effect(s) {" ".join(sorted(set(only) - set(known)))}; known: {" ".join(known)}',
                  file=sys.stderr)
        sys.exit(__doc__.split('Usage: ')[1].split('Listen')[0].rstrip())
    d = args[0] if args else os.path.join(root, 'firmware', 'apps', 'omni', 'assets')
    if mode == 'write':
        os.makedirs(d, exist_ok=True)
        for name, fn in EFFECTS:
            if only is not None and name not in only:
                continue
            s = finish(fn())
            path = os.path.join(d, name + '.pcm')
            with open(path, 'wb') as f:
                f.write(struct.pack(f'<{len(s)}h', *s))
            n, db = check(name, path, s)
            print(f'  {name}.pcm {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS (written)')
    elif mode == 'compare':
        for name, fn in EFFECTS:
            path = os.path.join(d, name + '.pcm')
            s = finish(fn())
            have = open(path, 'rb').read() if os.path.isfile(path) else None
            if have != struct.pack(f'<{len(s)}h', *s):
                print(f'  WARN {name}.pcm: this host synthesises different bytes than {path}'
                      f'{" (missing)" if have is None else ""}; kept as is (assets.sh --sfx regenerates)')
            else:
                print(f'  {name}.pcm: matches the generator')
    else:
        entries = []
        for name, _ in EFFECTS:
            n, db = check(name, os.path.join(d, name + '.pcm'))
            print(f'  {name}.pcm {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS')
            entries.append({'file': name + '.pcm', 'name': name, 'cf': 'PCM', 'samples': n, 'rate': SR, 'channels': 1,
                            'format': 's16le', 'face': 'sound'})
        with open(args[1], 'w') as f:
            f.write(json.dumps(entries, indent=1) + '\n')


if __name__ == '__main__':
    main()
