#!/usr/bin/env python3
"""The dw theme's four sounds (dw wave 1 Task 8), written as raw PCM into firmware/apps/dw/assets/.

Every sound here is synthesised from scratch by the code below: oscillators, a seeded noise generator and filters.
There are no inputs of any kind: nothing is sampled, traced, transcribed or imitated from any recording (spec section
14). The chime is a plain clock strike: a bell-like stack of partials, nothing more.

  dw_chime.pcm  2.80 s  the hourly tower-clock strike: five partials over 330 Hz (x 1, 2.0, 2.76, 3.9, 5.4), each
                        dying away on its own, after a 2 ms felt-hammer tick
  dw_ping.pcm   0.19 s  a soft inharmonic bell (one per hour, 360 ms apart)
  dw_latch.pcm  1.00 s  a door latch click with a warm swell (silent from 0.95 s)
  dw_tick.pcm   0.08 s  a key-page tick

Each is raw s16le, mono, 16 kHz, at most 4 s, peaking at -3 dBFS with 5 ms (80-sample) linear edges; the chime is at
most 3.0 s (the scheduler's bound, chime.c, is 3.44 s); the chime and the latch fade to silence before their last 5 %
(about -40 dBFS rms or quieter there), so the 5 ms edge never cuts them off, and a ping is at most 0.2 s. The primitives
(noise, band-pass, edges, level) are tools/gen_sfx.py's, imported unchanged. Deterministic: fixed-seed noise and plain
float maths, so a second run writes the same bytes on the same Python and libm (compare WARNs, never fails, on a host
that differs).

Usage: tools/gen_sfx_dw.py write [dir]       synthesise and write dir/dw_*.pcm (default firmware/apps/dw/assets)
       tools/gen_sfx_dw.py compare [dir]     synthesise in memory; WARN for each file in dir that differs (never writes)
       tools/gen_sfx_dw.py manifest dir json assert dir's dw_*.pcm and write their PCM manifest entries to json
Listen on the host: aplay -f S16_LE -r 16000 -c 1 firmware/apps/dw/assets/dw_chime.pcm
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_sfx                                        # noqa: E402  (its primitives; the file is not modified)
from gen_sfx import SR, EDGE, FS, PEAK_DBFS, TARGET, TAU, Noise, BandPass, ramp, finish   # noqa: E402,F401

MAX_S = 4.0
CHIME_MS_MAX = 3000


# ------------------------------------------------------------------------------------------------ the chime
def chime():
    """A tower-clock strike: five partials over a 330 Hz fundamental (the speaker rolls off below about 300 Hz), at
    x 1, 2.0, 2.76, 3.9, 5.4 with amplitudes 1, .5, .35, .2, .1 and decays of 1.2, .8, .6, .4, .3 s, set off by a 2 ms
    felt-hammer tick (white noise through a 2 kHz band-pass, -24 dB). It rings 2.80 s and is faded to silence between
    2.2 s and 2.6 s, so the last 5 % is quiet."""
    n = round(2.80 * SR)
    ratios = (1.0, 2.0, 2.76, 3.9, 5.4)
    amps = (1.0, 0.5, 0.35, 0.2, 0.1)
    taus = (1.2, 0.8, 0.6, 0.4, 0.3)
    rnd = Noise(0x70C10)
    tick = BandPass()
    tick.set(2000.0, 1.0)
    out = []
    for i in range(n):
        t = i / SR
        strike = 1 - math.exp(-t / 0.0015)
        x = strike * sum(a * math.sin(TAU * 330.0 * r * t) * math.exp(-t / tau)
                         for r, a, tau in zip(ratios, amps, taus))
        if t < 0.002:                                # the hammer's felt tick
            x += 10 ** (-24 / 20) * tick.run(rnd.white() * math.exp(-t / 0.0007))
        if t > 2.6:
            x = 0.0
        elif t > 2.2:
            x *= math.cos(0.5 * math.pi * (t - 2.2) / 0.4) ** 2
        out.append(x)
    return out


# ------------------------------------------------------------------------------------------------ the small sounds
def ping():
    """A soft inharmonic bell: partials at 1 : 2.32 : 3.87 of 1180 Hz, the upper ones dying first, after a 3 ms
    strike."""
    n = round(0.19 * SR)
    out = []
    for i in range(n):
        t = i / SR
        strike = 1 - math.exp(-t / 0.003)
        out.append(strike * (math.sin(TAU * 1180 * t) * math.exp(-t / 0.055) +
                             0.40 * math.sin(TAU * 1180 * 2.32 * t) * math.exp(-t / 0.032) +
                             0.18 * math.sin(TAU * 1180 * 3.87 * t) * math.exp(-t / 0.018)))
    return out


def latch():
    """A door latch: a bright click at 12 ms, a duller clack at 70 ms, under a warm swell (band-passed noise,
    around 380 Hz) that rises to 0.35 s, falls away and is silent from 0.95 s."""
    n = round(1.0 * SR)
    rnd = Noise(0x1A7C8)
    hi, warm = BandPass(), BandPass()
    hi.set(2800, 1.2)
    warm.set(380, 1.8)
    clicks = ((0.012, 1.0, 1900.0), (0.070, 0.7, 1100.0))
    out = []
    for i in range(n):
        t = i / SR
        w = rnd.white()
        exc = ping_ = 0.0
        for t0, a, fq in clicks:
            dt = t - t0
            if dt >= 0:
                if dt < 0.006:
                    exc += a * math.exp(-dt / 0.0012)
                ping_ += a * math.sin(TAU * fq * dt) * math.exp(-dt / 0.014)
        sw = (t / 0.35) ** 2 * math.exp(3 * (1 - t / 0.35))                       # peaks 1.0 at 0.35 s
        if t > 0.70:
            sw *= math.cos(0.5 * math.pi * min((t - 0.70) / 0.25, 1.0)) ** 2   # silent from 0.95 s
        out.append(0.8 * hi.run(exc * w) + 0.45 * ping_ + 0.9 * sw * warm.run(w))
    return out


def tick():
    """A key-page tick: a 3 ms noise burst through a 3 kHz band-pass over a 2.2 kHz ring of 7 ms."""
    n = round(0.08 * SR)
    rnd = Noise(0x71C4)
    bp = BandPass()
    bp.set(3000, 1.0)
    out = []
    for i in range(n):
        t = i / SR
        dt = t - 0.006
        e = ping_ = 0.0
        if dt >= 0:
            if dt < 0.005:
                e = math.exp(-dt / 0.0009)
            ping_ = math.sin(TAU * 2200 * dt) * math.exp(-dt / 0.007)
        out.append(0.9 * bp.run(e * rnd.white()) + 0.5 * ping_)
    return out


# name, synthesiser, longest allowed length in seconds
EFFECTS = (("dw_chime", chime, CHIME_MS_MAX / 1000.0), ("dw_ping", ping, 0.2), ("dw_latch", latch, 1.2),
           ("dw_tick", tick, 0.1))
NAMES = tuple(e[0] for e in EFFECTS)   # tools/check_apps.py: the only .pcm files apps/dw/assets may hold


# ------------------------------------------------------------------------------------------------ checks, main
def check(name, limit, path, want=None):
    """Reads the file back and asserts: even length, (given want) it decodes to exactly those samples, length within
    both the file's limit and MAX_S, peak -3 +- 0.1 dBFS, the 5 ms edges. Returns the sample count and peak."""
    data = open(path, 'rb').read()
    assert len(data) % 2 == 0, f'{name}: odd byte length {len(data)}'
    n = len(data) // 2
    s = struct.unpack(f'<{n}h', data)
    assert want is None or list(s) == want, f'{name}: the file does not read back as the samples written'
    assert n / SR <= min(limit, MAX_S), f'{name}: {n / SR:.3f} s > {min(limit, MAX_S)} s'
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
    if mode not in ('write', 'compare', 'manifest') or (mode == 'manifest' and len(args) != 2) or len(args) > 2:
        sys.exit(__doc__.split('Usage: ')[1].split('Listen')[0].rstrip())
    d = args[0] if args else os.path.join(root, 'firmware', 'apps', 'dw', 'assets')
    if mode == 'write':
        os.makedirs(d, exist_ok=True)
        for name, fn, limit in EFFECTS:
            s = finish(fn())
            path = os.path.join(d, name + '.pcm')
            with open(path, 'wb') as f:
                f.write(struct.pack(f'<{len(s)}h', *s))
            n, db = check(name, limit, path, s)
            print(f'  {name}.pcm {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS (written)')
    elif mode == 'compare':
        for name, fn, _ in EFFECTS:
            path = os.path.join(d, name + '.pcm')
            s = finish(fn())
            have = open(path, 'rb').read() if os.path.isfile(path) else None
            if have != struct.pack(f'<{len(s)}h', *s):
                print(f'  WARN {name}.pcm: this host synthesises different bytes than {path}'
                      f'{" (missing)" if have is None else ""}; kept as is (gen_sfx_dw.py write regenerates)')
            else:
                print(f'  {name}.pcm: matches the generator')
    else:
        entries = []
        for name, _, limit in EFFECTS:
            n, db = check(name, limit, os.path.join(d, name + '.pcm'))
            print(f'  {name}.pcm {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS')
            entries.append({'file': name + '.pcm', 'name': name, 'cf': 'PCM', 'samples': n, 'rate': SR, 'channels': 1,
                            'format': 's16le', 'face': 'sound'})
        with open(args[1], 'w') as f:
            f.write(json.dumps(entries, indent=1) + '\n')


if __name__ == '__main__':
    main()
