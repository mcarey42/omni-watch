#!/usr/bin/env python3
"""The Vortex tunnel's generator (dw wave 2 Task 13, WD37): our own texture and lookup table, following the recipe of
faces-modern.js:687-731 (vnoise, vxTexture, vxBuild). It reads no image and draws nothing traced: tileable value noise,
streaky along the tunnel, through a polar remap. Standard library only.

  dw_vortex.py gen --out DIR      DIR/dw_v12_tx0.png, _tx1.png, _tx2.png (the three palettes' 256 x 256 textures,
                                  opaque RGBA; tools/facepack.py packs each as an RGB565 bin through the PNG round trip)
                                  and DIR/dw_v12_lut.dat (the table, written as it ships; exempt from the PNG round trip)
  dw_vortex.py compare ASSETS     generate again and compare with ASSETS/dw_v12_lut.dat and ASSETS/dw_v12_tx{0,1,2}.bin,
                                  byte for byte (tools/dw_assets.sh --check); exit 1 on any difference
  dw_vortex.py selftest           two runs byte-identical; the textures tileable; the table's header, padding and mask
  dw_vortex.py palette-count      R150's question (wave 2 Task 14): the distinct RGB565 values of each palette's packed
                                  texture and of the three together (an 8-bit index needs <= 256 per palette)

Determinism (R147 PR17): the outputs are identical on every host, so --check compares byte for byte with no tolerance.
Only IEEE-754 basic operations are used (+, -, x, /, sqrt, all correctly rounded, and the exact frexp, ldexp, floor):
the noise's random grid is integer arithmetic (the mockup's 32-bit generator, rng() at faces-modern.js:30), and atan2,
ln, exp, pow and sin (palette 2's hue sweep, wave 2 Task 14) are this file's own series over those operations, never
libm's transcendental functions (which may differ by an ulp between hosts). Rounding is explicit: the texture's channels as Uint8ClampedArray rounds (to nearest,
ties to even: Python's round), the mask as Math.round (floor(x + 0.5)).

The table, dw_v12_lut.dat: "DWL1", u16 w, u16 h (little-endian, 233 x 233), then w x h cells (u, v, m) row-major, then
one 0 pad byte (8 + 233^2 x 3 = 162 875 is odd; every assets file is an even length): 162 876 B. Cell (i, j) is the
screen block at (2 i, 2 j), its centre (x, y) = (2 i + 1 - 233, 2 j + 1 - 233) from the screen's centre, r = |(x, y)|:
  - r < 104 or r > 236: (0, 0, 0), black;
  - u = floor(th 512 + depth 0.55) & 255, th = atan2(y, x) / 2 pi + 0.5 (two texture turns around, twisted with depth:
    the spiral arms), depth = 256 x 38 / r (1 / r); v = floor(depth) & 255;
  - m = round(s(k) o 256) clamped to 255, k = clamp((r - 104) / 46, 0, 1), s the smoothstep k^2 (3 - 2 k), o = 1 -
    clamp((r - 200) / 60, 0, 0.35): black at the inner edge, full by r 150, a little dimmer past 200.
The textures (faces-modern.js:682-686 VX_PALS): palette 0 GOLD & BLUE (a (255, 170, 60), b (60, 130, 255), hot (255,
240, 210)), 1 STORM (a (120, 150, 200), b (30, 50, 110), hot (255, 150, 80)), 2 PRISM (b (80, 200, 255), hot (255, 230,
140), and a swept round the hue with the column: a_k = 128 + 127 sin(6 u 2 pi + (0, 2.1, 4.2)_k), u = column / 256,
:710). Four tileable value noises (seed, cells across, cells down) n1 (11, 8, 6), n2 (23, 16, 12), n3 (37, 4, 4), n4 (41, 48, 8); f = .5 n1 + .3 n2
+ .2 n4; wa = clamp((f - .42) 2.4)^1.6; wb = clamp((n3 - .35) 2)^1.4 (1 - .6 wa); hot = clamp((f - .72) 4)^2; each
channel a wa 1.1 + b wb .55 + hot_c hot, rounded and clamped to 0..255. Texel (row, column) = (v, u): u runs around the
tunnel, v along it.
"""
import argparse
import math
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facepack  # noqa: E402  (its PNG codec and RGB565 packing: the texture takes the same round trip as every bin)

N = 233
S = 256
LUT_NAME = 'dw_v12_lut.dat'
TEX_NAMES = ('dw_v12_tx0', 'dw_v12_tx1', 'dw_v12_tx2')   # by palette
LUT_BYTES = 162876
PALETTES = [{'a': (255, 170, 60), 'b': (60, 130, 255), 'hot': (255, 240, 210)},    # GOLD & BLUE (:683)
            {'a': (120, 150, 200), 'b': (30, 50, 110), 'hot': (255, 150, 80)},     # STORM (:684)
            {'a': None, 'b': (80, 200, 255), 'hot': (255, 230, 140)}]              # PRISM (:685): a swept, :710
PI = 3.141592653589793
TAU = PI * 2   # the mockup's Math.PI * 2 (exact: a power-of-two multiple)
LN2 = 0.6931471805599453


# ------------------------------------------------------------------ deterministic maths (basic operations only)
def ln(x):
    """ln x, x > 0: x = m 2^e (frexp, exact), ln m = 2 atanh((m - 1) / (m + 1)) by its series."""
    m, e = math.frexp(x)
    z = (m - 1.0) / (m + 1.0)
    z2, term, acc = z * z, z, 0.0
    for k in range(1, 60, 2):
        acc += term / k
        term *= z2
    return e * LN2 + 2.0 * acc


def exp(y):
    """e^y: y = k ln 2 + r, |r| <= ln 2 / 2, e^r by Taylor, then ldexp (exact)."""
    k = math.floor(y / LN2 + 0.5)
    r = y - k * LN2
    term, acc = 1.0, 1.0
    for n in range(1, 30):
        term = term * r / n
        acc += term
    return math.ldexp(acc, int(k))


def powf(x, p):
    return 0.0 if x <= 0.0 else exp(p * ln(x))


def atan(a):
    """atan a for a >= 0: halve the angle twice (atan a = 2 atan(a / (1 + sqrt(1 + a^2)))), then the series."""
    inv = a > 1.0
    if inv:
        a = 1.0 / a
    for _ in range(2):
        a = a / (1.0 + math.sqrt(1.0 + a * a))
    a2, term, acc = a * a, a, 0.0
    for k in range(40):
        acc += term / (2 * k + 1) * (1.0 if k % 2 == 0 else -1.0)
        term *= a2
    t = 4.0 * acc
    return PI / 2.0 - t if inv else t


def atan2(y, x):
    if x == 0.0 and y == 0.0:
        return 0.0
    if x == 0.0:
        return PI / 2.0 if y > 0 else -PI / 2.0
    t = atan(abs(y) / abs(x))
    if x < 0:
        t = PI - t
    return t if y >= 0 else -t


def sin(x):
    """sin x: x reduced by 2 pi (floor, then one subtraction of the double 2 pi) into [-pi, pi), folded into
    [-pi / 2, pi / 2] by sin(pi - x) = sin x, then the Taylor series (the terms fall below an ulp by the 13th)."""
    k = math.floor(x / TAU + 0.5)
    r = x - k * TAU
    if r > PI / 2:
        r = PI - r
    elif r < -PI / 2:
        r = -PI - r
    r2, term, acc = r * r, r, 0.0
    for n in range(1, 30):
        acc += term
        term = -term * r2 / ((2 * n) * (2 * n + 1))
    return acc


def clamp(v, a, b):
    return a if v < a else b if v > b else v


# ------------------------------------------------------------------ the recipe
def rng(seed):
    """faces-modern.js:30 rng(): the 32-bit generator, in integers mod 2^32 (exact), / 2^32."""
    s = [seed & 0xFFFFFFFF]
    M = 0xFFFFFFFF

    def imul(a, b):
        return (a * b) & M

    def nxt():
        s[0] = (s[0] + 0x6D2B79F5) & M
        t = s[0]
        t = imul(t ^ (t >> 15), t | 1)
        t ^= (t + imul(t ^ (t >> 7), t | 61)) & M
        return ((t ^ (t >> 14)) & M) / 4294967296.0
    return nxt


def vnoise(size, seed, fx, fy):
    """vnoise() (:687-698): tileable value noise, fx cells across and fy down, smoothstep-interpolated. gx = x fx / size
    is exact (size is 256)."""
    rnd = rng(seed)
    grid = [rnd() for _ in range(fx * fy)]
    out = [0.0] * (size * size)
    for y in range(size):
        y0 = (y * fy) // size
        ty = (y * fy) / size - y0
        sy = ty * ty * (3 - 2 * ty)
        ra, rb = (y0 % fy) * fx, ((y0 + 1) % fy) * fx
        for x in range(size):
            x0 = (x * fx) // size
            tx = (x * fx) / size - x0
            sx = tx * tx * (3 - 2 * tx)
            a, b = grid[ra + x0 % fx], grid[ra + (x0 + 1) % fx]
            c, e = grid[rb + x0 % fx], grid[rb + (x0 + 1) % fx]
            top = a + (b - a) * sx
            out[y * size + x] = top + ((c + (e - c) * sx) - top) * sy
    return out


_WEIGHTS = []


def weights():
    """The palette-free part of vxTexture() (:702-707), computed once: per texel (wa, wb, hot) from the four noises."""
    if not _WEIGHTS:
        n1, n2, n3, n4 = vnoise(S, 11, 8, 6), vnoise(S, 23, 16, 12), vnoise(S, 37, 4, 4), vnoise(S, 41, 48, 8)
        for i in range(S * S):
            f = n1[i] * 0.5 + n2[i] * 0.3 + n4[i] * 0.2
            g2 = n3[i]
            wa = powf(clamp((f - 0.42) * 2.4, 0.0, 1.0), 1.6)
            wb = powf(clamp((g2 - 0.35) * 2, 0.0, 1.0), 1.4) * (1 - wa * 0.6)
            h = clamp((f - 0.72) * 4, 0.0, 1.0)
            _WEIGHTS.append((wa, wb, h * h))
    return _WEIGHTS


def texture(pal=0):
    """vxTexture() (:699-713): rows of size x 4 RGBA bytes (opaque); palette 2's a is swept round the hue by the
    column (:710: h = u 6, a_k = 128 + 127 sin(h TAU + (0, 2.1, 4.2)_k), this file's sin)."""
    P = PALETTES[pal]
    sweep = None
    if pal == 2:
        sweep = []
        for x in range(S):
            h = (x / S) * 6
            sweep.append((128 + 127 * sin(h * TAU), 128 + 127 * sin(h * TAU + 2.1), 128 + 127 * sin(h * TAU + 4.2)))
    wt = weights()
    rows = []
    for y in range(S):
        row = bytearray(S * 4)
        for x in range(S):
            wa, wb, hot = wt[y * S + x]
            a = sweep[x] if sweep else P['a']
            for k in range(3):
                c = a[k] * wa * 1.1 + P['b'][k] * wb * 0.55 + P['hot'][k] * hot
                row[4 * x + k] = int(clamp(round(c), 0, 255))   # Uint8ClampedArray: to nearest, ties to even
            row[4 * x + 3] = 255
        rows.append(row)
    return rows


def lut():
    """vxBuild() (:714-731): the table's bytes as it ships (header, cells, the pad byte)."""
    TAU = 2 * PI
    cells = bytearray(N * N * 3)
    for j in range(N):
        y = 2 * j + 1 - 233
        for i in range(N):
            x = 2 * i + 1 - 233
            r2 = x * x + y * y
            if r2 < 104 * 104 or r2 > 236 * 236:      # r < 104 or r > 236, compared exactly
                continue
            r, k = math.sqrt(r2), (j * N + i) * 3
            th = atan2(float(y), float(x)) / TAU + 0.5
            depth = S * 38 / r                         # depth ~ 1 / r
            cells[k] = math.floor(th * S * 2 + depth * 0.55) & 255
            cells[k + 1] = math.floor(depth) & 255
            mm = clamp((r - 104) / 46, 0.0, 1.0)
            o = 1 - clamp((r - 200) / 60, 0.0, 0.35)
            cells[k + 2] = min(255, math.floor(mm * mm * (3 - 2 * mm) * o * 256 + 0.5))   # Math.round; 256 -> 255
    data = b'DWL1' + struct.pack('<HH', N, N) + bytes(cells)
    return data + b'\0' * (len(data) % 2)


def tex_bin(rows, pal=0):
    """A texture as facepack.py packs it (RGB565, R86 rounding): the shipped dw_v12_tx<pal>.bin."""
    return facepack.pack_bin(S, S, rows, 'RGB565', TEX_NAMES[pal])


def gen(out):
    os.makedirs(out, exist_ok=True)
    for pal, name in enumerate(TEX_NAMES):
        facepack.png_write(os.path.join(out, name + '.png'), S, S, texture(pal))
    data = lut()
    with open(os.path.join(out, LUT_NAME), 'wb') as f:
        f.write(data)
    print('  dw_vortex: %s.png 256x256 (x %d palettes), %s %d B' % ('/'.join(TEX_NAMES), len(TEX_NAMES), LUT_NAME,
                                                                     len(data)))


def generated_bins(td):
    """gen into td, then the shipped bytes of every generated file: {name: bytes}"""
    gen(td)
    want = {LUT_NAME: open(os.path.join(td, LUT_NAME), 'rb').read()}
    for pal, name in enumerate(TEX_NAMES):
        w, h, rows = facepack.png_read(os.path.join(td, name + '.png'))
        want[name + '.bin'] = tex_bin(rows, pal)
    return want


def compare(assets):
    bad = []
    with tempfile.TemporaryDirectory() as td:
        want_all = generated_bins(td)
    for name, want in want_all.items():
        p = os.path.join(assets, name)
        if not os.path.isfile(p):
            bad.append('%s: missing' % name)
        elif open(p, 'rb').read() != want:
            bad.append('%s: not the generator\'s bytes' % name)
    for b in bad:
        print('VORTEX BAD', b)
    if not bad:
        print('VORTEX OK: %s are dw_vortex.py\'s output, byte for byte' % ', '.join(sorted(want_all)))
    return 1 if bad else 0


def palette_count():
    """R150 (wave 2 Task 14): could the three textures ship as one 8-bit 256 x 256 index plus three 256-entry RGB565
    palettes, losslessly? Only if each palette's packed texture uses at most 256 RGB565 values AND one index plane maps
    to all three at once (the distinct (tx0, tx1, tx2) triples at most 256). Prints the counts; exit 0."""
    with tempfile.TemporaryDirectory() as td:
        want = generated_bins(td)
    planes = [struct.unpack('<%dH' % (S * S), want[n + '.bin'][12:]) for n in TEX_NAMES]
    for n, pl in zip(TEX_NAMES, planes):
        print('  %s: %d distinct RGB565 values' % (n, len(set(pl))))
    print('  (tx0, tx1, tx2) triples: %d distinct' % len(set(zip(*planes))))
    return 0


# ------------------------------------------------------------------ self-test
def lut_problems(data):
    out = []
    if len(data) != LUT_BYTES or len(data) % 2:
        out.append('%d bytes, want %d (even)' % (len(data), LUT_BYTES))
        return out
    if data[:4] != b'DWL1' or struct.unpack('<HH', data[4:8]) != (N, N):
        out.append('header %r' % data[:8])
    if data[-1] != 0:
        out.append('pad byte %d' % data[-1])
    for j in range(N):
        for i in range(N):
            x, y = 2 * i + 1 - 233, 2 * j + 1 - 233
            r2, k = x * x + y * y, 8 + (j * N + i) * 3
            m = data[k + 2]
            if (r2 < 104 * 104 or r2 > 236 * 236) and data[k:k + 3] != b'\0\0\0':
                out.append('cell (%d, %d) at r^2 %d is not black' % (i, j, r2))
            elif 150 * 150 <= r2 <= 200 * 200 and m != 255:
                out.append('cell (%d, %d) at r^2 %d: m %d, want 255' % (i, j, r2, m))
    return out[:5]


def tile_problems(rows):
    """Tileable: the seam (column 255 next to column 0, row 255 next to row 0) differs no more than the largest step
    between neighbours inside the texture."""
    px = lambda x, y: rows[y][4 * x:4 * x + 3]
    step = lambda p, q: max(abs(a - b) for a, b in zip(p, q))
    inner_h = max(step(px(x, y), px(x + 1, y)) for y in range(S) for x in range(S - 1))
    inner_v = max(step(px(x, y), px(x, y + 1)) for y in range(S - 1) for x in range(S))
    seam_h = max(step(px(S - 1, y), px(0, y)) for y in range(S))
    seam_v = max(step(px(x, S - 1), px(x, 0)) for x in range(S))
    out = []
    if seam_h > inner_h:
        out.append('column 255 -> 0 steps %d, more than any step within a row (%d)' % (seam_h, inner_h))
    if seam_v > inner_v:
        out.append('row 255 -> 0 steps %d, more than any step within a column (%d)' % (seam_v, inner_v))
    return out, (seam_h, inner_h, seam_v, inner_v)


def selftest():
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)
            print('SELFTEST FAIL', what)
    # the maths against libm, loosely (the bytes need only be our own, but the curves must be the recipe's)
    for x in (1e-6, 0.01, 0.3, 0.5, 0.99, 1.0, 2.5, 100.0):
        ok(abs(ln(x) - math.log(x)) < 1e-12 * max(1.0, abs(math.log(x))), 'ln(%g)' % x)
    for y in (-30.0, -1.0, 0.0, 0.5, 3.0):
        ok(abs(exp(y) - math.exp(y)) <= 1e-13 * math.exp(y), 'exp(%g)' % y)
    for yx in ((1, 1), (-3, 7), (5, -2), (-1, -1), (0, -4), (4, 0), (-4, 0), (1e-9, 1)):
        ok(abs(atan2(*yx) - math.atan2(*yx)) < 1e-13, 'atan2%r' % (yx,))
    ok(abs(powf(0.37, 1.6) - 0.37 ** 1.6) < 1e-13, 'pow(.37, 1.6)')
    for x in (0.0, 0.5, -1.0, 1.5707963, 3.0, 3.2, -3.3, 6.0, 12.5, 37.6, 41.9):   # palette 2's sweep reaches 6 TAU + 4.2
        ok(abs(sin(x) - math.sin(x)) < 1e-14, 'sin(%g)' % x)
    r = rng(11)
    ok([round(r(), 12) for _ in range(2)] == [round(v, 12) for v in _rng_js_ref()], 'rng(11) matches the mockup\'s')
    with tempfile.TemporaryDirectory() as td:
        a, b = os.path.join(td, 'a'), os.path.join(td, 'b')
        gen(a)
        _WEIGHTS.clear()   # the second run computes everything again
        gen(b)
        for n in [LUT_NAME] + [t + '.png' for t in TEX_NAMES]:
            ok(open(os.path.join(a, n), 'rb').read() == open(os.path.join(b, n), 'rb').read(),
               'two runs give byte-identical %s' % n)
        data = open(os.path.join(a, LUT_NAME), 'rb').read()
        for p in lut_problems(data):
            ok(False, 'the table: ' + p)
        lit = sum(1 for k in range(8, len(data) - 1, 3) if data[k + 2])
        ok(lit > 30000, 'the ring is lit (%d cells)' % lit)
        planes = []
        for pal, name in enumerate(TEX_NAMES):
            w, h, rows = facepack.png_read(os.path.join(a, name + '.png'))
            ok((w, h) == (S, S), '%s is 256 x 256' % name)
            ok(all(row[4 * x + 3] == 255 for row in rows for x in range(S)), '%s is opaque' % name)
            probs, steps = tile_problems(rows)
            for p in probs:
                ok(False, '%s tileable: %s' % (name, p))
            print('  %s tileable: seam %d / %d (across), %d / %d (down)' % ((name,) + steps))
            tb = tex_bin(rows, pal)
            ok(len(tb) == 12 + S * S * 2 and tb[1] == 0x12, '%s packs as a 256 x 256 RGB565 bin' % name)
            planes.append(struct.unpack('<%dH' % (S * S), tb[12:]))
            ok(len(set(planes[-1])) > 200, '%s is textured (> 200 RGB565 values)' % name)
        ok(planes[0] != planes[1] and planes[1] != planes[2] and planes[0] != planes[2], 'the three palettes differ')
        # palette 2's hue sweep: a lit texel's colour follows its column (the same weights a column apart differ)
        ok(len(set(planes[2][y * S + 0] for y in range(S))) > 1, 'PRISM varies down a column')
    print('dw_vortex selftest: %s' % ('FAIL (%d)' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def _rng_js_ref():
    """rng(11)'s first two values as the mockup's JavaScript gives them (node -e, faces-modern.js:30), recorded."""
    return [0.5115870486479253, 0.5299464082345366]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('selftest')
    p = sub.add_parser('gen')
    p.add_argument('--out', required=True)
    p = sub.add_parser('compare')
    p.add_argument('assets')
    sub.add_parser('palette-count')
    a = ap.parse_args()
    if a.cmd == 'selftest':
        return selftest()
    if a.cmd == 'palette-count':
        return palette_count()
    if a.cmd == 'gen':
        gen(a.out)
        return 0
    return compare(a.assets)


if __name__ == '__main__':
    sys.exit(main())
