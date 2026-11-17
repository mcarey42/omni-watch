#!/usr/bin/env python3
"""The rim keyboard's shared assets (spec sections 6.2-6.4 and 10; Tasks 5 and 8).

  kb_assets.py layout [--out PATH]      write kb_lay.bin
  kb_assets.py font --c FILE --out PATH pack an lv_font_conv C file (tools/fonts.sh writes it to a temporary file) as an
                                        OFA1 font asset (kb_mono21.bin, kb_mono42.bin)
  kb_assets.py icons [--out PATH]       draw kb_icons.bin
  kb_assets.py tick [--out PATH]        synthesise kb_tick.pcm
  kb_assets.py meta [--out PATH]        write firmware/components/watch_ui/kb_meta.h (the shared geometry and names)
  kb_assets.py copy                     copy the five files from watch_ui/assets into firmware/apps/{omni,dw,rd}/assets
                                        and write their rows into each manifest.json (byte-equal: check_apps static)
  kb_assets.py manifest --out JSON      the five manifest rows (tools/assets.sh joins them to the Omni's manifest)
  kb_assets.py selftest

The source of truth is firmware/components/watch_ui/assets/ (D5); the defaults write there.

kb_lay.bin: "OKL1" | u8 version 1 | u8 nsets 4 | u8 n[4] | the keys, n[0]+..+n[3] bytes, each 0x21..0x7E.
The sets, in ring order: abc and ABC (the 26 letters in order), 123 (0-9 and the 16 symbols most often found in
passwords) and #+= (the other 16 printable symbols). Space is its own key, so the sets and space cover all 95 printable
ASCII characters.

OFA1 (watch_ui/fontasset.h reads it, zero copy): "OFA1" | u8 version 1 | u8 bpp | u8 bitmap_format (0 plain) |
u8 line_height | i8 base_line | u8 subpx 0 | u16 first 0x20 | u16 count 97 | i8 underline_position |
u8 underline_thickness | u32 dsc_off | u32 bitmap_off (24 B, little-endian); then count glyph descriptors of 8 B, LVGL
9.5's lv_font_fmt_txt_glyph_dsc_t (u32 bitmap_index:20 | adv_w:12 << 20, u8 box_w, u8 box_h, i8 ofs_x, i8 ofs_y): glyph
id 0 reserved (all zero; lv_font_conv's glyph_id_start 1), then 0x20..0x7E, then U+2022; then the bitmap (lv_font_conv's
glyph_bitmap, each glyph ceil(box_w x box_h x bpp / 8) bytes at its index). dsc_off and bitmap_off are 4-byte aligned,
and the file is padded with zeros to a multiple of 4.

kb_icons.bin: one RGB565A8 LVGL bin, ICON_N cells of ICON_CELL px square in a row, in ICON_NAMES' order; the colour
plane is white wherever the alpha is not 0, so a screen tints it. Drawn here from the rim-keyboard mockup's own
geometry (eyeIcon, shiftIcon, delIcon, the space bracket and the netlist's lockGlyph), 4 x 4 supersampled.

kb_tick.pcm: an original click synthesised here (no samples): an LCG noise burst through two resonators, s16le mono
16 kHz, 400 samples (25 ms), peak -8 dBFS, zero at both ends. Standard library and basic float operations only
(no libm transcendental), so the bytes are the same on every host.
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys
import zlib

MAGIC = b'OKL1'
VERSION = 1
NSETS = 4
SET_MAX = 32
LETTERS = 'abcdefghijklmnopqrstuvwxyz'
SYMS_123 = '-_.@!?#$%&*+=/:;'
REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
WATCH_UI = os.path.join(REPO, 'firmware', 'components', 'watch_ui')
SRC_DIR = os.path.join(WATCH_UI, 'assets')
DEFAULT_OUT = os.path.join(SRC_DIR, 'kb_lay.bin')
META_OUT = os.path.join(WATCH_UI, 'kb_meta.h')
APPS = ('omni', 'dw', 'rd')
APP_ASSETS = os.path.join(REPO, 'firmware', 'apps', '%s', 'assets')
DW_TABLE = os.path.join(REPO, 'tools', 'export', 'dw_layers.json')
RD_TABLE = os.path.join(REPO, 'tools', 'export', 'rd_layers.json')
# the five shared names, in manifest order (each at most 15 characters: the mmap image's name field)
SHARED = ('kb_lay.bin', 'kb_mono21.bin', 'kb_mono42.bin', 'kb_icons.bin', 'kb_tick.pcm')
SHARED_SRC = 'watch_ui/assets, byte-equal (check_apps)'

FONT_MAGIC = b'OFA1'
FONT_VERSION = 1
FONT_FIRST = 0x20
FONT_LAST = 0x7E
FONT_EXTRA = 0x2022            # the bullet: a masked password character
FONT_COUNT = 1 + (FONT_LAST - FONT_FIRST + 1) + 1   # id 0 reserved, 95 ASCII, the bullet: 97
FONT_HDR = struct.Struct('<4sBBBBbBHHbBII')        # 24 bytes
FONT_BPP = (1, 2, 4, 8)

ICON_CELL = 24
ICON_NAMES = ('shift_off', 'shift_once', 'shift_lock', 'delete', 'eye_open', 'eye_shut', 'space', 'lock')
ICON_N = len(ICON_NAMES)
SS = 4                         # supersampling per axis

TICK_RATE = 16000
TICK_N = 400


def sets():
    printable = [chr(c) for c in range(0x21, 0x7F)]
    digits = '0123456789'
    other = ''.join(c for c in printable if not c.isalnum() and c not in SYMS_123)
    return [LETTERS, LETTERS.upper(), digits + SYMS_123, other]


def build(sts=None):
    sts = sets() if sts is None else sts
    return MAGIC + bytes([VERSION, len(sts)]) + bytes(len(s) for s in sts) + ''.join(sts).encode('ascii')


def parse(data):
    """The reader the firmware's loader mirrors; ValueError when refused."""
    if len(data) < 10 or data[:4] != MAGIC or data[4] != VERSION or data[5] != NSETS:
        raise ValueError('header')
    n = list(data[6:10])
    if any(x == 0 or x > SET_MAX for x in n) or len(data) != 10 + sum(n):
        raise ValueError('sizes')
    keys = data[10:]
    if any(b < 0x21 or b > 0x7E for b in keys):
        raise ValueError('key byte')
    out, p = [], 0
    for x in n:
        out.append(keys[p:p + x].decode('ascii'))
        p += x
    return out


# ------------------------------------------------------------------ fonts (OFA1)
def parse_conv(text):
    """lv_font_conv 1.5.3's C output (--format lvgl --no-compress) -> {dsc, bitmap, bpp, line_height, ...}. Refuses
    (ValueError) a file that is not the 97 glyphs in the two ranges fontasset.c's cmaps describe, kerning, compression
    or subpixel rendering."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)

    def block(name):
        m = re.search(r'\b%s\[\]\s*=\s*\{(.*?)\n\};' % name, text, re.S)
        if not m:
            raise ValueError('no %s[] in the C file' % name)
        return m.group(1)

    def field(name, pat=r'(-?\d+)'):
        m = re.search(r'\.%s\s*=\s*%s' % (name, pat), text)
        if not m:
            raise ValueError('no .%s in the C file' % name)
        return m.group(1)
    bitmap = bytes(int(t, 16) for t in re.findall(r'\b0x([0-9a-fA-F]{1,2})\b', block('glyph_bitmap')))
    dsc = [tuple(int(v) for v in m) for m in re.findall(
        r'\{\s*\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), '
        r'\.ofs_y = (-?\d+)\s*\}', block('glyph_dsc'))]
    cm = re.findall(r'\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*'
                    r'\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), \.type = (\w+)',
                    block('cmaps'))
    want = [('32', '95', '1', 'NULL', 'NULL', '0', 'LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY'),
            (str(FONT_EXTRA), '1', '96', 'NULL', 'NULL', '0', 'LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY')]
    if cm != want:
        raise ValueError('the character map is not 0x20..0x7E then U+2022 (%r)' % cm)
    if len(dsc) != FONT_COUNT:
        raise ValueError('%d glyph descriptors, want %d' % (len(dsc), FONT_COUNT))
    if field('kern_dsc', r'(\w+)') != 'NULL' or field('bitmap_format') != '0' or \
            field('subpx', r'(\w+)') != 'LV_FONT_SUBPX_NONE' or field('cmap_num') != '2':
        raise ValueError('kerning, compression, subpixel rendering or other cmaps: not this format')
    return {'dsc': dsc, 'bitmap': bitmap, 'bpp': int(field('bpp')), 'line_height': int(field('line_height')),
            'base_line': int(field('base_line')), 'underline_position': int(field('underline_position')),
            'underline_thickness': int(field('underline_thickness'))}


def glyph_bytes(w, h, bpp):
    return (w * h * bpp + 7) // 8


def font_build(f):
    """OFA1 bytes from parse_conv's dict (the format in the module comment)."""
    dsc_off = FONT_HDR.size
    bitmap_off = dsc_off + 8 * FONT_COUNT
    hdr = FONT_HDR.pack(FONT_MAGIC, FONT_VERSION, f['bpp'], 0, f['line_height'], f['base_line'], 0, FONT_FIRST,
                        FONT_COUNT, f['underline_position'], f['underline_thickness'], dsc_off, bitmap_off)
    body = b''.join(struct.pack('<IBBbb', idx | adv << 20, w, h, ox, oy) for idx, adv, w, h, ox, oy in f['dsc'])
    out = hdr + body + f['bitmap']
    out += bytes(-len(out) % 4)
    font_parse(out)   # what is written is what the firmware accepts
    return out


def font_parse(data):
    """The reader fontasset.c mirrors: every offset and count checked against the file's size. ValueError when
    refused."""
    if len(data) < FONT_HDR.size:
        raise ValueError('short header')
    (magic, ver, bpp, fmt, line_h, base, subpx, first, count, ul_pos, ul_th, dsc_off,
     bitmap_off) = FONT_HDR.unpack_from(data)
    if magic != FONT_MAGIC or ver != FONT_VERSION:
        raise ValueError('magic or version')
    if bpp not in FONT_BPP or fmt != 0 or subpx != 0 or first != FONT_FIRST or count != FONT_COUNT:
        raise ValueError('bpp, bitmap format, subpx, first or count')
    if dsc_off % 4 or bitmap_off % 4 or dsc_off < FONT_HDR.size:
        raise ValueError('a misaligned or overlapping offset')
    if dsc_off > len(data) or 8 * count > len(data) - dsc_off or bitmap_off < dsc_off + 8 * count or \
            bitmap_off > len(data):
        raise ValueError('the descriptor table or the bitmap runs past the file')
    blen = len(data) - bitmap_off
    dsc = []
    for i in range(count):
        v, w, h, ox, oy = struct.unpack_from('<IBBbb', data, dsc_off + 8 * i)
        idx, adv = v & 0xfffff, v >> 20
        if idx + glyph_bytes(w, h, bpp) > blen:
            raise ValueError('glyph %d\'s bitmap runs past the file' % i)
        dsc.append((idx, adv, w, h, ox, oy))
    if dsc[0] != (0, 0, 0, 0, 0, 0):
        raise ValueError('glyph id 0 is not reserved')
    return {'dsc': dsc, 'bitmap': data[bitmap_off:], 'bpp': bpp, 'line_height': line_h, 'base_line': base,
            'underline_position': ul_pos, 'underline_thickness': ul_th, 'dsc_off': dsc_off, 'bitmap_off': bitmap_off}


# ------------------------------------------------------------------ icons (drawn, 4 x 4 supersampled)
def _seg_d2(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    ll = dx * dx + dy * dy
    t = 0.0 if ll == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / ll))
    ex, ey = ax + t * dx - px, ay + t * dy - py
    return ex * ex + ey * ey


def _quad(p0, p1, p2, n=16):
    return [((1 - t) * (1 - t) * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) * (1 - t) * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]


def _inside(px, py, poly):
    c = False
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[i - 1]
        if (ay > py) != (by > py) and px < ax + (py - ay) * (bx - ax) / (by - ay):
            c = not c
    return c


def _icon_shapes(i):
    """Cell i's shapes, in cell pixels (centre 12, 12): ('stroke', points, width, closed), ('fill', polygon),
    ('ring', cx, cy, r, width, top_only), ('rect', x, y, w, h), ('clear', x, y, w, h). The mockup's own geometry."""
    c = ICON_CELL / 2

    def at(pts, ox, oy, k=1.0):
        return [(ox + k * x, oy + k * y) for x, y in pts]
    name = ICON_NAMES[i]
    if name.startswith('shift'):
        ox, oy = c, c - 1
        arrow = at([(0, -10), (9, 0), (4, 0), (4, 7), (-4, 7), (-4, 0), (-9, 0)], ox, oy, 0.94)
        sh = [('stroke', arrow, 1.8, True)]
        if name != 'shift_off':
            sh.append(('fill', arrow))
        if name == 'shift_lock':
            sh.append(('rect', ox - 5, oy + 10, 10, 2))
        return sh
    if name == 'delete':
        ox, oy, k = c + 0.5, c, 0.9
        return [('stroke', at([(-12, 0), (-5, -8), (11, -8), (11, 8), (-5, 8)], ox, oy, k), 1.8, True),
                ('stroke', at([(-1, -4), (7, 4)], ox, oy, k), 1.8, False),
                ('stroke', at([(7, -4), (-1, 4)], ox, oy, k), 1.8, False)]
    if name.startswith('eye'):
        k = 0.92   # the mockup's 22 px eye, kept off the cell's edges
        lid = _quad((c - 11 * k, c), (c, c - 10 * k), (c + 11 * k, c)) + \
            _quad((c + 11 * k, c), (c, c + 10 * k), (c - 11 * k, c))[1:]
        sh = [('stroke', lid, 1.8, False), ('ring', c, c, 3.6, 1.8, False)]
        if name == 'eye_shut':
            sh.append(('stroke', [(c - 10 * k, c + 9 * k), (c + 10 * k, c - 9 * k)], 1.8, False))
        return sh
    if name == 'space':
        return [('stroke', at([(-10, -3), (-10, 3), (10, 3), (10, -3)], c, c + 1), 1.6, False)]
    # lock: the netlist's lockGlyph (x centre, y baseline) at 1.45 x
    k, ox, oy = 1.45, c, c + 9.3
    return [('stroke', at([(-3.4, -7), (-3.4, -9.6)], ox, oy, k), 1.6 * k, False),
            ('stroke', at([(3.4, -9.6), (3.4, -7)], ox, oy, k), 1.6 * k, False),
            ('ring', ox, oy - 9.6 * k, 3.4 * k, 1.6 * k, True),
            ('rrect', ox - 5.5 * k, oy - 7.5 * k, 11 * k, 8.5 * k, 1.6 * k),
            ('clear', ox - 0.75 * k, oy - 5 * k, 1.5 * k, 3.2 * k)]


def _covered(px, py, shapes):
    hit = False
    for s in shapes:
        kind = s[0]
        if kind == 'stroke':
            pts, w, closed = s[1], s[2], s[3]
            r2 = (w / 2) * (w / 2)
            n = len(pts) if closed else len(pts) - 1
            if any(_seg_d2(px, py, *pts[j], *pts[(j + 1) % len(pts)]) <= r2 for j in range(n)):
                hit = True
        elif kind == 'fill':
            if _inside(px, py, s[1]):
                hit = True
        elif kind == 'ring':
            cx, cy, r, w, top = s[1:]
            d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy)
            if (r - w / 2) * (r - w / 2) <= d2 <= (r + w / 2) * (r + w / 2) and (not top or py <= cy):
                hit = True
        elif kind == 'rect':
            x, y, w, h = s[1:]
            if x <= px < x + w and y <= py < y + h:
                hit = True
        elif kind == 'rrect':
            x, y, w, h, r = s[1:]
            if x <= px < x + w and y <= py < y + h:
                qx = min(max(px, x + r), x + w - r)
                qy = min(max(py, y + r), y + h - r)
                if (px - qx) * (px - qx) + (py - qy) * (py - qy) <= r * r:
                    hit = True
        elif kind == 'clear':
            x, y, w, h = s[1:]
            if x <= px < x + w and y <= py < y + h:
                return False
    return hit


def icons_alpha():
    """The strip's alpha, rows of ICON_N x ICON_CELL bytes: each pixel the share of its 16 samples the shapes cover."""
    w, h = ICON_CELL * ICON_N, ICON_CELL
    rows = [[0] * w for _ in range(h)]
    for i in range(ICON_N):
        shapes = _icon_shapes(i)
        for y in range(h):
            for x in range(ICON_CELL):
                n = sum(_covered(x + (sx + 0.5) / SS, y + (sy + 0.5) / SS, shapes) for sy in range(SS) for sx in range(SS))
                rows[y][i * ICON_CELL + x] = (n * 255 + SS * SS // 2) // (SS * SS)
    return rows


def icons_build():
    w, h = ICON_CELL * ICON_N, ICON_CELL
    alpha = icons_alpha()
    colour = b''.join(struct.pack('<H', 0xffff if a else 0) for row in alpha for a in row)
    return struct.pack('<BBHHHHH', 0x19, 0x14, 0, w, h, 2 * w, 0) + colour + bytes(a for row in alpha for a in row)


# ------------------------------------------------------------------ the tick (synthesised, no samples)
def tick_samples():
    """An original detent click: a 1.5 ms LCG noise burst (a triangle envelope) rings two resonators (2.4 kHz, a short
    ring, and 900 Hz, the body), faded to zero over the last 4 ms. Basic float operations only."""
    # resonator coefficients: 2 r cos(2 pi f / 16000), written out (the cosines of 0.3 pi and 0.1125 pi)
    hi_c, hi_r = 2 * 0.985 * 0.5877852522924731, 0.985 * 0.985
    lo_c, lo_r = 2 * 0.975 * 0.9381913359224842, 0.975 * 0.975
    seed, burst, fade = 0x2545F491, 24, 64
    h1 = h2 = l1 = l2 = 0.0
    raw = []
    for n in range(TICK_N):
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        noise = ((seed >> 8) & 0xffff) / 32768.0 - 1.0
        env = 0.0 if n >= burst else (n / (burst / 2) if n < burst / 2 else (burst - n) / (burst / 2))
        x = noise * env
        h = x + hi_c * h1 - hi_r * h2
        lo = x + lo_c * l1 - lo_r * l2
        h2, h1, l2, l1 = h1, h, l1, lo
        v = 0.6 * h + 0.4 * lo
        if n >= TICK_N - fade:
            v *= (TICK_N - 1 - n) / (fade - 1)
        raw.append(v)
    peak = max(abs(v) for v in raw)
    k = 0.398 * 32767 / peak   # -8 dBFS
    return [int((v * k + 0.5) // 1) for v in raw]


def tick_build():
    s = tick_samples()
    return struct.pack('<%dh' % len(s), *s)


# ------------------------------------------------------------------ shared geometry, copies and manifest rows
def read_asset(name):
    with open(os.path.join(SRC_DIR, name), 'rb') as f:
        return f.read()


def crc_of(name):
    return '%08x' % zlib.crc32(read_asset(name))


def manifest_entry(name):
    """The row every app's manifest.json carries for a shared file (and, with the table's own keys, dw's and rd's
    layer tables): its kind, size and whole-file CRC-32."""
    data = read_asset(name)
    e = {'file': name, 'name': name.rsplit('.', 1)[0], 'cf': 'RAW', 'out': 'shared_ui', 'face': 'shared',
         'size': len(data), 'crc32': '%08x' % zlib.crc32(data), 'src': SHARED_SRC}
    if name == 'kb_icons.bin':
        e.update(cf='RGB565A8', w=ICON_CELL * ICON_N, h=ICON_CELL)
    elif name.endswith('.pcm'):
        e.update(cf='PCM', samples=len(data) // 2, rate=TICK_RATE, channels=1, format='s16le')
    return e


def meta_text():
    L = ['/* generated by tools/kb_assets.py meta -- do not edit. The rim keyboard\'s shared assets (watch_ui/assets, a',
         ' * byte-equal copy in every app): their names and geometry, so watch_ui names no app header. */',
         '#pragma once', '']
    for n in SHARED:
        L.append('#define KB_ASSET_%s "%s"' % (n.rsplit('.', 1)[0][3:].upper(), n))
    L += ['', '/* kb_icons.bin: one RGB565A8 strip of KB_ICON_N cells, KB_ICON_CELL px square, white (tint it) */',
          '#define KB_ICON_CELL %d' % ICON_CELL, '#define KB_ICON_N %d' % ICON_N,
          '#define KB_ICONS_W %d' % (ICON_CELL * ICON_N), '#define KB_ICONS_H %d' % ICON_CELL,
          'enum kb_icon {']
    L += ['    KB_ICON_%s = %d,' % (n.upper(), i) for i, n in enumerate(ICON_NAMES)]
    L += ['};', '', '/* kb_tick.pcm: s16le mono at 16 kHz */',
          '#define KB_TICK_SAMPLES %d' % (len(read_asset('kb_tick.pcm')) // 2), '',
          '/* the OFA1 fonts (kb_mono21.bin, kb_mono42.bin): Plex Mono 500, 0x20..0x7E and U+2022 */',
          '#define KB_FONT_GLYPHS %d' % FONT_COUNT, '']
    return '\n'.join(L)


def upsert_rows(entries, rows, after='bg_settings.bin'):
    """entries with the shared rows replaced in place, or inserted after `after` (else appended), in SHARED order."""
    out = [e for e in entries if e.get('file') not in SHARED]
    at = next((i + 1 for i, e in enumerate(out) if e.get('file') == after), len(out))
    return out[:at] + rows + out[at:]


def cmd_copy():
    rows = [manifest_entry(n) for n in SHARED]
    for app in APPS:
        d = APP_ASSETS % app
        for n in SHARED:
            shutil.copyfile(os.path.join(SRC_DIR, n), os.path.join(d, n))
        mp = os.path.join(d, 'manifest.json')
        man = json.load(open(mp))
        man['assets'] = upsert_rows(man['assets'], rows)
        with open(mp, 'w') as f:
            f.write(json.dumps(man, indent=1) + '\n')
        print('  %s: the five shared files and their manifest rows' % os.path.relpath(d, REPO))
    return 0


def fake_conv_source(seed=7, bpp=4):
    """An lv_font_conv C file's text (its layout, 1.5.3 --format lvgl --no-compress) for a made-up font of the 97
    glyphs: id 0 reserved, 0x20..0x7E, U+2022. Deterministic (an LCG); used to prove the parser and the packer."""
    x = seed

    def rnd(n):
        nonlocal x
        x = (x * 1103515245 + 12345) & 0x7fffffff
        return x % n
    dsc, bitmap = [(0, 0, 0, 0, 0, 0)], []
    for _ in range(FONT_COUNT - 1):
        w, h = rnd(20), rnd(30)
        if rnd(5) == 0:
            w = h = 0
        dsc.append((len(bitmap), 160 + rnd(200), w, h, rnd(9) - 4, rnd(12) - 3))
        bitmap += [rnd(256) for _ in range((w * h * bpp + 7) // 8)]
    L = ['/* Size: 21 px */', '#include "lvgl.h"', 'static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {',
         '    /* U+0020 " " */', '', '    /* U+002A "*" */']
    L += ['    ' + ', '.join('0x%x' % b for b in bitmap[i:i + 16]) + ',' for i in range(0, len(bitmap), 16)]
    L += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {']
    L += ['    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d}%s,' %
          (d + (' /* id = 0 reserved */' if i == 0 else '',)) for i, d in enumerate(dsc)]
    L += ['};', 'static const lv_font_fmt_txt_cmap_t cmaps[] =', '{', '    {',
          '        .range_start = 32, .range_length = 95, .glyph_id_start = 1,',
          '        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY',
          '    },', '    {', '        .range_start = 8226, .range_length = 1, .glyph_id_start = 96,',
          '        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY',
          '    }', '};', 'static const lv_font_fmt_txt_dsc_t font_dsc = {', '    .glyph_bitmap = glyph_bitmap,',
          '    .kern_dsc = NULL,', '    .kern_scale = 0,', '    .cmap_num = 2,', '    .bpp = %d,' % bpp,
          '    .kern_classes = 0,', '    .bitmap_format = 0,', '};', 'const lv_font_t font_x = {',
          '    .line_height = 22,          /*The maximum line height required by the font*/',
          '    .base_line = 4,             /*Baseline measured from the bottom of the line*/',
          '    .subpx = LV_FONT_SUBPX_NONE,', '    .underline_position = -3,', '    .underline_thickness = 2,', '};']
    return '\n'.join(L) + '\n', dsc, bitmap


def selftest_font(check):
    src, dsc, bitmap = fake_conv_source()
    conv = parse_conv(src)
    check(conv['dsc'] == dsc and conv['bitmap'] == bytes(bitmap), 'font: the lv_font_conv C arrays are read exactly')
    data = font_build(conv)
    got = font_parse(data)
    check(got['dsc'] == dsc, 'font: every descriptor round-trips')
    check(got['bitmap'][:len(bitmap)] == bytes(bitmap), 'font: every bitmap byte round-trips')
    check((got['line_height'], got['base_line'], got['underline_position'], got['underline_thickness'], got['bpp']) ==
          (22, 4, -3, 2, 4), 'font: the metrics round-trip')
    check(got['dsc'][0] == (0, 0, 0, 0, 0, 0), 'font: glyph id 0 is reserved')
    check(got['dsc_off'] % 4 == 0 and got['bitmap_off'] % 4 == 0, 'font: dsc_off and bitmap_off are 4-byte aligned')
    check(len(data) % 4 == 0, 'font: the file is a multiple of 4 bytes')
    off = struct.unpack_from('<II', data, 16)
    bad = [(data[:-1 - (len(data) - got['bitmap_off'] - len(bitmap))] if len(bitmap) else data[:-1], 'truncated bitmap'),
           (data[:got['bitmap_off'] - 1], 'truncated descriptors'), (data[:23], 'truncated header'),
           (b'OFA2' + data[4:], 'bad magic'), (data[:4] + b'\x02' + data[5:], 'version 2'),
           (data[:5] + b'\x03' + data[6:], 'bpp 3'), (data[:6] + b'\x01' + data[7:], 'compressed bitmap'),
           (data[:12] + struct.pack('<H', FONT_COUNT - 1) + data[14:], 'count 96'),
           (data[:10] + struct.pack('<H', 0x21) + data[12:], 'first 0x21'),
           (data[:16] + struct.pack('<II', off[0] + 2, off[1]) + data[24:], 'a misaligned dsc_off'),
           (data[:16] + struct.pack('<II', off[0], off[1] + 2) + data[24:], 'a misaligned bitmap_off'),
           (data[:16] + struct.pack('<II', off[0], 0xfffffffc) + data[24:], 'bitmap_off past the file'),
           (data[:16] + struct.pack('<II', 0xfffffff0, off[1]) + data[24:], 'dsc_off past the file'),
           (data[:off[0]] + struct.pack('<I', 1) + data[off[0] + 4:], 'glyph id 0 not reserved')]
    big = bytearray(data)
    struct.pack_into('<I', big, off[0] + 8 * 5, 0xfffff | (100 << 20))   # glyph 5's bitmap index at 1 MB - 1
    bad.append((bytes(big), 'a glyph bitmap running past the file'))
    for b, why in bad:
        try:
            font_parse(b)
            check(False, 'font: refused: ' + why)
        except ValueError:
            pass
    for name in ('kb_mono21.bin', 'kb_mono42.bin'):
        f = font_parse(read_asset(name))
        check(f['dsc'][0] == (0, 0, 0, 0, 0, 0) and f['bpp'] == 4, 'font: committed %s is a valid OFA1 font' % name)
        check(f['dsc'][ord('M') - 0x1f][2] > 0 and f['dsc'][FONT_COUNT - 1][2] > 0,
              'font: committed %s has an M and a bullet' % name)


def selftest_icons(check):
    data = icons_build()
    w, h = ICON_CELL * ICON_N, ICON_CELL
    check(len(ICON_NAMES) == 8 and ICON_N == 8, 'icons: 8 cells')
    check(data[:12] == struct.pack('<BBHHHHH', 0x19, 0x14, 0, w, h, 2 * w, 0), 'icons: an RGB565A8 %dx%d LVGL bin' % (w, h))
    check(len(data) == 12 + 3 * w * h, 'icons: the colour plane then the A8 plane')
    alpha = icons_alpha()
    check(data[12 + 2 * w * h:] == bytes(a for row in alpha for a in row), 'icons: the alpha plane is the drawing, exactly')
    for i, n in enumerate(ICON_NAMES):
        ink = sum(alpha[y][x] for y in range(h) for x in range(i * w // ICON_N, (i + 1) * w // ICON_N))
        check(ink > 255 * 20, 'icons: cell %d (%s) has ink' % (i, n))
        edge = [alpha[y][i * ICON_CELL] for y in range(h)] + [alpha[0][i * ICON_CELL + x] for x in range(ICON_CELL)]
        check(max(edge) < 128, 'icons: cell %d (%s) keeps off its left and top edges' % (i, n))
    check(read_asset('kb_icons.bin') == data, 'icons: the committed kb_icons.bin is what the generator writes')


def selftest_tick(check):
    data = tick_build()
    s = struct.unpack('<%dh' % (len(data) // 2), data)
    check(len(data) % 2 == 0 and 0 < len(s) <= 640, 'tick: at most 640 samples (40 ms at 16 kHz): %d' % len(s))
    check(max(abs(v) for v in s) <= int(32768 * 10 ** (-6 / 20)), 'tick: peak at most -6 dBFS (%d)' % max(abs(v) for v in s))
    check(max(abs(v) for v in s) > 3000, 'tick: audible')
    check(s[0] == 0 and s[-1] == 0, 'tick: zero at both ends')
    check(read_asset('kb_tick.pcm') == data, 'tick: the committed kb_tick.pcm is what the generator writes')


def selftest_shared(check):
    check(meta_text() == open(META_OUT).read(), 'shared: the committed kb_meta.h is what the generator writes')
    for app in APPS:
        for n in SHARED:
            p = os.path.join(APP_ASSETS % app, n)
            check(os.path.isfile(p) and open(p, 'rb').read() == read_asset(n),
                  'shared: %s holds a byte-equal %s' % (app, n))
        man = {e['file']: e for e in json.load(open(os.path.join(APP_ASSETS % app, 'manifest.json')))['assets']}
        for n in SHARED:
            check(man.get(n) == manifest_entry(n), 'shared: %s/manifest.json\'s %s row is the generator\'s' % (app, n))
    for table, key in ((DW_TABLE, 'asset'), (RD_TABLE, 'name')):
        rows = {r[key]: r for r in json.load(open(table))['rows']}
        for n in SHARED:
            r = rows.get(n.split('.')[0], {})
            check((r.get('file', n), r.get('crc32'), r.get('size')) == (n, crc_of(n), len(read_asset(n))),
                  'shared: %s\'s %s row names its crc32 and size' % (os.path.basename(table), n))


def selftest():
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
            print('FAIL: ' + what)

    data = build()
    got = parse(data)
    check(got == sets(), 'read back what was written')
    check([len(s) for s in got] == [26, 26, 26, 16], 'set sizes 26 26 26 16')
    check(got[0] == LETTERS and got[1] == LETTERS.upper(), 'letters in order')
    cover = set(got[0]) | set(got[1]) | set(got[2]) | set(got[3]) | {' '}
    check(cover == {chr(c) for c in range(0x20, 0x7F)}, 'the 95 printable characters are covered')
    check(len(set(got[2]) | set(got[3])) == 42, 'no symbol twice')
    check(len(data) == 10 + 94, 'file size 104')
    for bad, why in ((b'XKL1' + data[4:], 'magic'), (data[:4] + b'\x02' + data[5:], 'version'),
                     (data[:5] + b'\x03' + data[6:], 'nsets'), (data[:-1], 'short'), (data + b'a', 'one byte long'),
                     (data[:9] + b'\x00' + data[10:-16], 'n = 0'),
                     (data[:6] + b'\x21' + data[7:], 'n over 32'),
                     (data[:10] + b'\x20' + data[11:], 'key 0x20'), (data[:10] + b'\x7f' + data[11:], 'key 0x7f')):
        try:
            parse(bad)
            check(False, 'refused: ' + why)
        except ValueError:
            pass
    try:
        with open(DEFAULT_OUT, 'rb') as f:
            check(f.read() == data, 'the committed kb_lay.bin is what the generator writes')
    except OSError as e:
        check(False, 'committed kb_lay.bin unreadable: %s' % e.strerror)
    for name, case in (('font', selftest_font), ('icons', selftest_icons), ('tick', selftest_tick),
                       ('shared', selftest_shared)):
        try:
            case(check)
        except Exception as e:   # a case that cannot run is a failure, never a pass
            check(False, '%s: %s: %s' % (name, type(e).__name__, e))
    print('SELFTEST %s' % ('FAIL (%d)' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)
    print('wrote %s (%d B)' % (os.path.relpath(path), len(data)))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('selftest')
    sub.add_parser('copy')
    for name, default in (('layout', DEFAULT_OUT), ('icons', os.path.join(SRC_DIR, 'kb_icons.bin')),
                          ('tick', os.path.join(SRC_DIR, 'kb_tick.pcm')), ('meta', META_OUT)):
        sub.add_parser(name).add_argument('--out', default=default)
    fp = sub.add_parser('font')
    fp.add_argument('--c', required=True)
    fp.add_argument('--out', required=True)
    mp = sub.add_parser('manifest')
    mp.add_argument('--out', required=True)
    a = ap.parse_args(argv)
    if a.cmd == 'selftest':
        return selftest()
    if a.cmd == 'copy':
        return cmd_copy()
    if a.cmd == 'manifest':
        with open(a.out, 'w') as f:
            json.dump([manifest_entry(n) for n in SHARED], f, indent=1)
        return 0
    if a.cmd == 'meta':
        with open(a.out, 'w') as f:
            f.write(meta_text())
        print('wrote %s' % os.path.relpath(a.out))
        return 0
    if a.cmd == 'font':
        try:
            data = font_build(parse_conv(open(a.c).read()))
        except ValueError as e:
            print('kb_assets font: %s: %s' % (a.c, e), file=sys.stderr)
            return 1
        write(a.out, data)
        return 0
    write(a.out, {'layout': build, 'icons': icons_build, 'tick': tick_build}[a.cmd]())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
