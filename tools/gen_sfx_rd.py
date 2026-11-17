#!/usr/bin/env python3
"""The Mining Ship theme's hourly chime, "Ship Clears Its Throat" (rd wave 1 Task 8; spec section 11), written as raw
PCM into firmware/apps/rd/assets/.

Every sample here is synthesised from scratch by the code below: additive oscillators, a seeded noise generator and
filters. There are no inputs of any kind: nothing is read, sampled, traced, transcribed or imitated from any recording,
the show included (spec section 13). The recipe is the theme designer's own (spec section 11's table), never the
show's music or effects.

Two voicings of the one recipe, each 2.4 s:
  rd_chime.pcm    the audible voicing (R156 as refined by R158/PR9; the default, the one the hour plays): the thrum's
                  two saws at 110.0 and 111.6 Hz and its sine at 220 Hz; the low-pass cutoff x4 (360 Hz at 0,
                  exponentially to 1 680 Hz at 0.70 s, held to 1.50 s, exponentially to 480 Hz at 2.30 s), Q 4; then a
                  4-pole high-pass at 200 Hz. The watch's speaker rolls off below about 300 Hz, so the low thrum is
                  implied by its harmonics (the missing fundamental) instead of sounding its own 55 Hz.
  rd_chime_r.pcm  the recipe as written (`rd chime play recipe`, the owner's listening check): saws at 55.0 and
                  55.8 Hz (0.5 each) and a 27.5 Hz sine (0.7) into a 2-pole resonant low-pass, Q 4, its cutoff 90 Hz at
                  0, exponentially to 420 Hz at 0.70 s, held to 1.50 s, exponentially to 120 Hz at 2.30 s.
Both: the thrum's amplitude rises exponentially from -80 dB to 0.34 at 0.55 s, holds to 1.50 s and falls
exponentially to silence at 2.35 s; its three oscillators hold their pitch to 1.60 s, then glide linearly down 3 % by
2.30 s. The bip (0.75 s): a 50 % pulse at 880 Hz, 4 ms attack to 0.22, exponential decay to silence over 110 ms, through
a band-pass at 1.2 kHz, Q 1. The bop (0.95 s): a 25 % pulse gliding linearly 622 -> 600 Hz over its 150 ms, the same
envelope over 150 ms and the same band-pass (deliberately a little flat). The relay click (2.20 s): 6 ms of seeded
white noise with a linear decay plus 0.6 x one cycle of 1.8 kHz, through a high-pass at 2 kHz, level 0.25.

The primitives new here: band-limited saw and pulse by additive synthesis to 32 harmonics (only those below Nyquist,
each waveform scaled to a unit peak as a periodic wave is), the time-varying resonant 2-pole low-pass (RBJ) and the
high-pass (RBJ, one Butterworth biquad, or two in cascade for the 4-pole). tools/gen_sfx.py is imported unchanged:
its noise, band-pass, edges and level (finish: 5 ms edges, normalised to PEAK_DBFS like every theme sound, the
recipe's relative levels kept). Raw s16le, mono, 16 kHz, 2.4 s = 76 800 B each. Deterministic: fixed-seed noise and
plain float maths, so a second run writes the same bytes on the same Python and libm (compare WARNs, never fails, on
a host that differs; manifest asserts).

Usage: tools/gen_sfx_rd.py render [dir] [--voicing audible|recipe]
                                             synthesise and write dir/<voicing's file> (default both voicings, into
                                             firmware/apps/rd/assets)
       tools/gen_sfx_rd.py compare [dir] [--voicing audible|recipe]
                                             synthesise in memory; WARN for each file in dir that differs (never writes)
       tools/gen_sfx_rd.py manifest dir json assert dir's two files and write their PCM manifest entries to json
Listen on the host: aplay -f S16_LE -r 16000 -c 1 firmware/apps/rd/assets/rd_chime.pcm
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_sfx                                        # noqa: E402  (its primitives; the file is not modified)
from gen_sfx import SR, EDGE, FS, PEAK_DBFS, TARGET, TAU, Noise, BandPass, ramp, finish   # noqa: E402,F401

VOICINGS = {'audible': 'rd_chime.pcm', 'recipe': 'rd_chime_r.pcm'}   # the names check_apps allows
NAMES = tuple(f[:-len('.pcm')] for f in VOICINGS.values())            # check_apps' sound rule reads these
STEMS = ('thrum', 'bip', 'bop', 'click')
LEN_S = 2.4
N = round(LEN_S * SR)                                 # 38 400 samples, 76 800 B
HARMONICS = 32
NYQ = SR / 2

# the thrum's voicings: (saw Hz, saw Hz, sine Hz), the low-pass cutoff's breakpoints (0, 0.70 s, 2.30 s), the high-pass
THRUM = {
    'recipe': {'osc': (55.0, 55.8, 27.5), 'cut': (90.0, 420.0, 120.0), 'hp': None},
    'audible': {'osc': (110.0, 111.6, 220.0), 'cut': (360.0, 1680.0, 480.0), 'hp': 200.0},
}


# ------------------------------------------------------------------------------------------------ the primitives
class Additive:
    """A band-limited periodic wave by additive synthesis: sum over n of a_n cos(n phi) + b_n sin(n phi) for the
    harmonics n = 1..32 that stay below Nyquist at f_max, scaled to a unit peak (as a WebAudio periodic wave
    normalises). The phase accumulates per sample, so the frequency may change every sample."""

    def __init__(self, coeffs, f_max):
        k = min(HARMONICS, max(1, math.ceil(NYQ / f_max) - 1))   # n * f_max < SR / 2
        self.c = [coeffs(n) for n in range(1, k + 1)]
        pk = max(abs(self._raw(i / 4096)) for i in range(4096))
        self.g = 1.0 / pk
        self.ph = 0.0

    def _raw(self, ph):
        x = TAU * ph
        return sum(a * math.cos(n * x) + b * math.sin(n * x) for n, (a, b) in enumerate(self.c, 1))

    def run(self, f):
        """The sample at the current phase, then the phase advanced by f / SR."""
        x = TAU * self.ph
        c1, s1 = math.cos(x), math.sin(x)
        cn, sn = 1.0, 0.0
        acc = 0.0
        for a, b in self.c:   # cos/sin of n x by the angle-sum recurrence (plain float maths, deterministic)
            cn, sn = cn * c1 - sn * s1, sn * c1 + cn * s1
            acc += a * cn + b * sn
        self.ph = (self.ph + f / SR) % 1.0
        return self.g * acc


def saw(f_max):
    """A rising sawtooth: -(2/pi) sum sin(n phi) / n."""
    return Additive(lambda n: (0.0, -2.0 / (math.pi * n)), f_max)


def pulse(duty, f_max):
    """A pulse of the given duty (the mockup's periodic-wave coefficients): a_n = 2/(n pi) sin(2 pi n d),
    b_n = 2/(n pi) (1 - cos(2 pi n d))."""
    return Additive(lambda n: (2.0 / (n * math.pi) * math.sin(TAU * n * duty),
                               2.0 / (n * math.pi) * (1.0 - math.cos(TAU * n * duty))), f_max)


class Biquad:
    """RBJ low-pass or high-pass, direct form I; set() may retune it every sample."""

    def __init__(self, kind):
        self.kind = kind
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.c = (0.0, 0.0, 0.0, 0.0, 0.0)

    def set(self, f, q):
        w = TAU * f / SR
        cw = math.cos(w)
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha
        if self.kind == 'lp':
            b0 = (1 - cw) / 2
            b1 = 1 - cw
        else:
            b0 = (1 + cw) / 2
            b1 = -(1 + cw)
        self.c = (b0 / a0, b1 / a0, b0 / a0, -2 * cw / a0, (1 - alpha) / a0)

    def run(self, x):
        b0, b1, b2, a1, a2 = self.c
        y = b0 * x + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def highpass(f, poles):
    """A Butterworth high-pass of 2 or 4 poles at f: one biquad (Q 1/sqrt 2) or two in cascade (Q 0.5412, 1.3066)."""
    qs = (1 / math.sqrt(2),) if poles == 2 else (1 / (2 * math.cos(math.pi / 8)), 1 / (2 * math.cos(3 * math.pi / 8)))
    stages = []
    for q in qs:
        b = Biquad('hp')
        b.set(f, q)
        stages.append(b)
    return stages


def xramp(v0, v1, t0, t1, t):
    """An exponential ramp from v0 at t0 to v1 at t1 (as WebAudio's exponentialRampToValueAtTime)."""
    return v0 * (v1 / v0) ** ((t - t0) / (t1 - t0))


# ------------------------------------------------------------------------------------------------ the voices
def thrum(voicing):
    p = THRUM[voicing]
    f1, f2, fs = p['osc']
    c0, c1, c2 = p['cut']
    o1, o2 = saw(f1), saw(f2)
    lp = Biquad('lp')
    hp = highpass(p['hp'], 4) if p['hp'] else []
    ph = 0.0
    out = []
    for i in range(N):
        t = i / SR
        g = 1.0 if t < 1.6 else 1.0 - 0.03 * min((t - 1.6) / 0.7, 1.0)   # the engine sags: -3 % from 1.60 to 2.30 s
        x = 0.5 * o1.run(f1 * g) + 0.5 * o2.run(f2 * g) + 0.7 * math.sin(TAU * ph)
        ph = (ph + fs * g / SR) % 1.0
        if t < 0.7:
            fc = xramp(c0, c1, 0.0, 0.7, t)
        elif t < 1.5:
            fc = c1
        elif t < 2.3:
            fc = xramp(c1, c2, 1.5, 2.3, t)
        else:
            fc = c2
        lp.set(fc, 4.0)
        y = lp.run(x)
        for s in hp:
            y = s.run(y)
        if t < 0.55:
            amp = xramp(1e-4, 0.34, 0.0, 0.55, t)
        elif t < 1.5:
            amp = 0.34
        elif t < 2.35:
            amp = xramp(0.34, 1e-4, 1.5, 2.35, t)
        else:
            amp = 0.0
        out.append(amp * y)
    return out


def beep(at, f0, f1, dur, duty):
    """A pulse from at for dur s gliding linearly f0 -> f1, through the 1.2 kHz band-pass (Q 1), then its envelope:
    4 ms exponential attack from -80 dB to 0.22, exponential decay to -80 dB at dur, silent after."""
    o = pulse(duty, max(f0, f1))
    bp = BandPass()
    bp.set(1200.0, 1.0)
    i0 = round(at * SR)
    out = []
    for i in range(N):
        dt = (i - i0) / SR
        if dt < 0:
            out.append(bp.run(0.0))
            continue
        x = o.run(f0 + (f1 - f0) * min(dt / dur, 1.0)) if dt < dur else 0.0
        y = bp.run(x)
        if dt < 0.004:
            env = xramp(1e-4, 0.22, 0.0, 0.004, dt)
        elif dt < dur:
            env = xramp(0.22, 1e-4, 0.004, dur, dt)
        else:
            env = 0.0
        out.append(env * y)
    return out


def click():
    """At 2.20 s: 6 ms (96 samples) of seeded white noise decaying linearly, plus 0.6 x one cycle of 1.8 kHz, through a
    2 kHz high-pass, at 0.25."""
    rnd = Noise(0xC11C8)
    hp = highpass(2000.0, 2)
    i0 = round(2.2 * SR)
    m = round(0.006 * SR)
    cyc = SR / 1800.0
    out = []
    for i in range(N):
        k = i - i0
        x = 0.0
        if 0 <= k < m:
            x = rnd.white() * (1 - k / m) + (0.6 * math.sin(TAU * 1800.0 * k / SR) if k < cyc else 0.0)
        for s in hp:
            x = s.run(x)
        out.append(0.25 * x)
    return out


def render_stem(voicing, stem):
    """One voice of the voicing alone, unnormalised, N samples (the tests' view, PR34). Bip, bop and click are the same
    in both voicings."""
    if voicing not in VOICINGS:
        raise ValueError('unknown voicing %r (%s)' % (voicing, ' | '.join(VOICINGS)))
    if stem == 'thrum':
        return thrum(voicing)
    if stem == 'bip':
        return beep(0.75, 880.0, 880.0, 0.110, 0.50)
    if stem == 'bop':
        return beep(0.95, 622.0, 600.0, 0.150, 0.25)
    if stem == 'click':
        return click()
    raise ValueError('unknown stem %r (%s)' % (stem, ' | '.join(STEMS)))


def samples(voicing):
    mix = [sum(t) for t in zip(*(render_stem(voicing, s) for s in STEMS))]
    return finish(mix)


def render(voicing):
    """The voicing's file: s16le mono 16 kHz, 2.4 s, 76 800 B, 5 ms edges, normalised to PEAK_DBFS."""
    s = samples(voicing)
    return struct.pack(f'<{len(s)}h', *s)


# ------------------------------------------------------------------------------------------------ checks, main
def check(name, path, want=None):
    """Reads the file back and asserts: exactly N samples (2.4 s, 76 800 B), (given want) exactly those bytes, peak
    PEAK_DBFS +- 0.1 dB, the 5 ms edges. Returns the sample count and peak."""
    with open(path, 'rb') as f:
        data = f.read()
    assert want is None or data == want, f'{name}: the file does not read back as the bytes written'
    assert len(data) == 2 * N, f'{name}: {len(data)} bytes, not {2 * N} (2.4 s of s16le mono at 16 kHz)'
    s = struct.unpack(f'<{N}h', data)
    pk = max(abs(v) for v in s)
    db = 20 * math.log10(max(pk, 1) / FS)
    assert abs(db - PEAK_DBFS) <= 0.1, f'{name}: peak {db:.3f} dBFS'
    for i in list(range(EDGE)) + list(range(N - EDGE, N)):
        assert abs(s[i]) <= TARGET * ramp(i, N) + 1, f'{name}: sample {i} = {s[i]} outside the 5 ms edge ramp'
    return N, db


def usage():
    sys.exit(__doc__.split('Usage: ')[1].split('Listen')[0].rstrip())


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    args = sys.argv[1:]
    mode = args.pop(0) if args else ''
    voicings = list(VOICINGS)
    if '--voicing' in args:
        k = args.index('--voicing')
        if mode == 'manifest' or k + 1 >= len(args) or args[k + 1] not in VOICINGS:
            usage()
        voicings = [args[k + 1]]
        del args[k:k + 2]
    if mode not in ('render', 'compare', 'manifest') or (mode == 'manifest' and len(args) != 2) or \
            (mode != 'manifest' and len(args) > 1) or any(a.startswith('-') for a in args):
        usage()
    d = args[0] if args else os.path.join(root, 'firmware', 'apps', 'rd', 'assets')
    if mode == 'render':
        os.makedirs(d, exist_ok=True)
        for v in voicings:
            data = render(v)
            path = os.path.join(d, VOICINGS[v])
            with open(path, 'wb') as f:
                f.write(data)
            n, db = check(VOICINGS[v], path, data)
            print(f'  {VOICINGS[v]} ({v}) {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS (written)')
    elif mode == 'compare':
        for v in voicings:
            path = os.path.join(d, VOICINGS[v])
            have = None
            if os.path.isfile(path):
                with open(path, 'rb') as f:
                    have = f.read()
            if have != render(v):
                print(f'  WARN {VOICINGS[v]}: this host synthesises different bytes than {path}'
                      f'{" (missing)" if have is None else ""}; kept as is (gen_sfx_rd.py render regenerates)')
            else:
                print(f'  {VOICINGS[v]}: matches the generator')
    else:
        entries = []
        for v, f in VOICINGS.items():
            n, db = check(f, os.path.join(d, f))
            print(f'  {f} ({v}) {n} samples, {n / SR:.3f} s, {2 * n} bytes, peak {db:.2f} dBFS')
            entries.append({'file': f, 'name': f[:-len('.pcm')], 'cf': 'PCM', 'samples': n, 'rate': SR,
                            'channels': 1, 'format': 's16le', 'face': 'sound'})
        with open(args[1], 'w') as f:
            f.write(json.dumps(entries, indent=1) + '\n')


if __name__ == '__main__':
    main()
