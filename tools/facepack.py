#!/usr/bin/env python3
"""The shared asset formats: what every theme's packer writes and reads. Standard library only (its own PNG codec), so
the self-test runs under any Python, ctest's included.

  facepack.py selftest

A theme's packer imports this module and keeps only its own rows, names, swap rules and overlay logic. Nothing here
names a theme: a prefix, a table or a text check arrives as an argument.

Formats (firmware/facekit/face_kit/include/fk/compose.h, fk/annulus.h):
  - LVGL v9 .bin: u8 magic 0x19, u8 cf, u16 flags 0, u16 w, h, stride (w x bpp), u16 0, then the planes. RGB565 (0x12,
    opaque), RGB565A8 (0x14: the colour plane, unpremultiplied, then the A8 plane), A8 (0x0E: the alpha plane alone).
    Every colour channel is rounded to its nearest 5/6-bit level (R86): (c x 31 + 127) // 255.
  - ORS1 strip (fk/annulus.h): "ORS1", u16 n_theta, u8 r_min, u8 n_r, u8 level 0, u8 0, u16 stride (n_theta + 32),
    4 zero bytes, then stride x n_r RGB565 and stride x n_r A8. Column 0 is 12 o'clock, columns run clockwise, row 0
    is r0 (compose.h, C12); entry (ri, th) samples the canvas at radius r0 + ri + 0.5 and angle (th + 0.5) 2 pi / n,
    bilinear in premultiplied colour.
  - A8 strip (a one-colour ring): the same unwrap's alpha alone, packed as an LVGL A8 bin n_theta wide and n_r tall
    (row ri is radius r0 + ri, column th the angle as above; no pad entries); the face's annulus drawing tints it.
  - Atlas: one RGB565A8 image (or A8: alpha only, the face tints it); its cells (x, y, w, h, ox, oy, adv; a wrapped
    atlas has several lines of cells) and glyph string go into the generated assets_meta.h.
  - Two-plane atlas (ATL2, PR29): one file holding a glow plane and a core plane of the same cells, self-describing, so
    the C side reads it as two ordinary atlases (one cell table, one glyph string, two A8 images):
      "ATL2", u16 w, u16 h (one plane's), u16 n (cells), u16 line_h, u16 glyph_bytes (even), u16 0,
      n cells of 7 int16 (x, y, w, h, ox, oy, adv), the glyph string (Latin-1, NUL-terminated, padded to glyph_bytes),
      then the glow plane (w x h A8) and the core plane (w x h A8). Every part has an even length, so the file has too.
  - A8 rows (ARW1, rd wave 2 Task 12: an A8 image kept only within each row's extent): "ARW1", u16 w, u16 h, then h
    extents (u8 x0, u8 x1: the row's first and one past its last non-zero byte; 0, 0 for an empty row), then each row's
    x1 - x0 bytes in turn, the file padded with one zero byte to an even length. w <= 255.

Every file is an even length (C6: the next file in the assets image stays 2-aligned); write_bin and pack_atlas2 refuse
an odd one, and a caller packing with pack_bin checks it.
"""
import json
import math
import os
import re
import struct
import sys
import zlib

CF = {'RGB565': 0x12, 'RGB565A8': 0x14, 'A8': 0x0E}
BPP = {0x12: 2, 0x14: 2, 0x0E: 1}
NAME_MAX = 12                     # a name is its theme's prefix plus at most this many characters


def NAME_RULE(prefix):
    """The compiled rule for an asset name: prefix, then at most NAME_MAX characters."""
    return re.compile(re.escape(prefix) + '.{0,%d}' % NAME_MAX, re.S)


class PackError(Exception):
    pass


# ------------------------------------------------------------------ PNG (8-bit RGBA, non-interlaced; any filter)
def png_read(path):
    """(w, h, rows): rows a list of bytearrays of w x 4 unpremultiplied RGBA bytes."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise PackError('%s: not a PNG' % path)
    i, idat, w = 8, bytearray(), None
    while i < len(data):
        n, kind = struct.unpack('>I4s', data[i:i + 8])
        body = data[i + 8:i + 8 + n]
        i += 12 + n
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise PackError('%s: only 8-bit RGB/RGBA non-interlaced PNGs (depth %d, type %d)' % (path, depth, ctype))
            ch = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
    if w is None:
        raise PackError('%s: no IHDR' % path)
    raw, stride, rows, prev = zlib.decompress(bytes(idat)), w * ch, [], bytearray(w * ch)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for x in range(ch, stride):
                line[x] = (line[x] + line[x - ch]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                line[x] = (line[x] + ((line[x - ch] if x >= ch else 0) + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b, c = prev[x], prev[x - ch] if x >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f:
            raise PackError('%s: bad filter %d' % (path, f))
        prev = line
        if ch == 3:
            rgba = bytearray(w * 4)
            for x in range(w):
                rgba[4 * x:4 * x + 3], rgba[4 * x + 3] = line[3 * x:3 * x + 3], 255
            line = rgba
        rows.append(line)
    return w, h, rows


def png_write(path, w, h, rows, filt=0):
    """An 8-bit RGBA PNG (the self-test's; filt: one filter type for every row, encoded here)."""
    raw, prev = bytearray(), bytearray(w * 4)
    for row in rows:
        row = bytearray(row)
        out = bytearray(row)
        for x in range(w * 4):
            a = row[x - 4] if x >= 4 else 0
            b, c = prev[x], prev[x - 4] if x >= 4 else 0
            if filt == 1:
                out[x] = (row[x] - a) & 255
            elif filt == 2:
                out[x] = (row[x] - b) & 255
            elif filt == 3:
                out[x] = (row[x] - (a + b) // 2) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                out[x] = (row[x] - (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        raw += bytes([filt]) + out
        prev = row

    def chunk(k, b):
        return struct.pack('>I', len(b)) + k + b + struct.pack('>I', zlib.crc32(k + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


# ------------------------------------------------------------------ LVGL bins
def rgb565(r, g, b):
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255


def lvgl_header(cf, w, h):
    return struct.pack('<BBHHHHH', 0x19, CF[cf], 0, w, h, w * BPP[CF[cf]], 0)


def pack_bin(w, h, rows, cf, what):
    """The LVGL v9 bin of rows in cf (R86 rounding; RGB565 must be opaque)."""
    col, alpha = bytearray(), bytearray()
    for y, row in enumerate(rows):
        for x in range(w):
            r, g, b, a = row[4 * x:4 * x + 4]
            if cf == 'RGB565' and a != 255:
                raise PackError('%s: RGB565 must be opaque (%d, %d: alpha %d)' % (what, x, y, a))
            if cf != 'A8':
                col += struct.pack('<H', rgb565(r, g, b))
            alpha.append(a)
    return lvgl_header(cf, w, h) + bytes(col) + (bytes(alpha) if cf in ('RGB565A8', 'A8') else b'')


def bin_problems(data, cf, w, h, rows):
    """The round trip of one bin against its PNG rows: the header, the size, every pixel (RGB565 levels exact, alpha
    exact). Returns a list of messages."""
    if len(data) < 12:
        return ['%d bytes' % len(data)]
    magic, c, flags, bw, bh, stride = struct.unpack('<BBHHHH', data[:10])
    if (magic, c, flags, bw, bh, stride) != (0x19, CF[cf], 0, w, h, w * BPP[CF[cf]]):
        return ['header magic %#x cf %#x flags %#x %dx%d stride %d, want %s %dx%d' % (magic, c, flags, bw, bh, stride,
                                                                                   cf, w, h)]
    want = 12 + w * h * {'RGB565': 2, 'RGB565A8': 3, 'A8': 1}[cf]
    if len(data) != want:
        return ['%d bytes, want %d' % (len(data), want)]
    if rows is None:
        return []
    errs, ap = 0, 12 + (w * h * 2 if cf != 'A8' else 0)
    for y, row in enumerate(rows):
        for x in range(w):
            r, g, b, a = row[4 * x:4 * x + 4]
            i = y * w + x
            if cf != 'A8' and struct.unpack_from('<H', data, 12 + 2 * i)[0] != rgb565(r, g, b):
                errs += 1
            elif cf == 'RGB565' and a != 255:
                errs += 1
            elif cf != 'RGB565' and data[ap + i] != a:
                errs += 1
    return ['%d pixels differ from the PNG' % errs] if errs else []


def write_bin(path, cf, w, h, rgb=None, a8=None):
    """Write an LVGL v9 bin. rgb: the colour plane (w x h little-endian RGB565 words), a8: the alpha plane (w x h
    bytes); RGB565 takes rgb alone, RGB565A8 both, A8 a8 alone. An odd total length is refused (C6): A8 and RGB565A8
    need an even w x h."""
    if cf not in CF:
        raise PackError('%s: cf %r' % (path, cf))
    want_rgb, want_a8 = cf != 'A8', cf != 'RGB565'
    if (rgb is not None) != want_rgb or (a8 is not None) != want_a8:
        raise PackError('%s: %s takes %s' % (path, cf, ' and '.join(
            n for n, wanted in (('rgb', want_rgb), ('a8', want_a8)) if wanted)))
    if rgb is not None and len(rgb) != 2 * w * h or a8 is not None and len(a8) != w * h:
        raise PackError('%s: the planes are not %dx%d' % (path, w, h))
    data = lvgl_header(cf, w, h) + (bytes(rgb) if want_rgb else b'') + (bytes(a8) if want_a8 else b'')
    if len(data) % 2:
        raise PackError('%s: %s %dx%d is %d bytes; every file is an even length (an odd w x h needs another size)'
                        % (path, cf, w, h, len(data)))
    open(path, 'wb').write(data)


def read_bin(path):
    """(cf, w, h, rgb, a8): write_bin's arguments back; a plane the format lacks is None."""
    data = open(path, 'rb').read()
    if len(data) < 12:
        raise PackError('%s: %d bytes' % (path, len(data)))
    magic, c, flags, w, h, stride = struct.unpack('<BBHHHH', data[:10])
    names = [k for k, v in CF.items() if v == c]
    if magic != 0x19 or flags or not names or stride != w * BPP[c]:
        raise PackError('%s: not an LVGL v9 bin header' % path)
    cf = names[0]
    n = w * h
    if len(data) != 12 + n * {'RGB565': 2, 'RGB565A8': 3, 'A8': 1}[cf]:
        raise PackError('%s: %d bytes for %s %dx%d' % (path, len(data), cf, w, h))
    rgb = data[12:12 + 2 * n] if cf != 'A8' else None
    a8 = data[len(data) - n:] if cf != 'RGB565' else None
    return cf, w, h, rgb, a8


# ------------------------------------------------------------------ ORS1 strips
def unwrap(w, h, rows, scale, cx, cy, r0, r1, n_theta, a8=False):
    """ORS1 bytes: the annulus [r0, r1) about (cx, cy) (screen px; the canvas is scale x) unwrapped clockwise from
    12 o'clock (compose.h's convention, C12). a8: its alpha alone as an LVGL A8 bin, n_theta x n_r (Task 15)."""
    if n_theta < 4 or n_theta & (n_theta - 1) or not 0 <= r0 < r1 or r0 > 255:
        raise PackError('strip: n_theta %d (a power of two >= 4), r0 %r, r1 %r' % (n_theta, r0, r1))
    nr = int(math.ceil(r1 - r0))
    stride = n_theta if a8 else n_theta + 32
    if nr > 255:
        raise PackError('strip: %d radius rows (at most 255)' % nr)

    def px(x, y):
        if 0 <= x < w and 0 <= y < h:
            r, g, b, a = rows[y][4 * x:4 * x + 4]
            return r * a, g * a, b * a, a
        return 0, 0, 0, 0
    col, alpha = bytearray(stride * nr * 2), bytearray(stride * nr)
    for ri in range(nr):
        rad = r0 + ri + 0.5
        for th in range(n_theta):
            ang = (th + 0.5) * 2 * math.pi / n_theta
            fx = (cx + rad * math.sin(ang)) * scale - 0.5      # pixel-centre coordinates of the canvas
            fy = (cy - rad * math.cos(ang)) * scale - 0.5
            x0, y0 = int(math.floor(fx)), int(math.floor(fy))
            tx, ty = fx - x0, fy - y0
            acc = [0.0] * 4
            for (dx, dy, wt) in ((0, 0, (1 - tx) * (1 - ty)), (1, 0, tx * (1 - ty)), (0, 1, (1 - tx) * ty),
                                 (1, 1, tx * ty)):
                p = px(x0 + dx, y0 + dy)
                for k in range(4):
                    acc[k] += p[k] * wt
            a = acc[3]
            ai = int(a + 0.5)
            if ai:
                r, g, b = (min(255, int(acc[k] / a + 0.5)) for k in range(3))
                struct.pack_into('<H', col, 2 * (ri * stride + th), rgb565(r, g, b))
                alpha[ri * stride + th] = min(255, ai)
    if a8:
        return lvgl_header('A8', n_theta, nr) + bytes(alpha)
    hdr = b'ORS1' + struct.pack('<HBBBBH', n_theta, int(r0), nr, 0, 0, stride) + bytes(4)
    return hdr + bytes(col) + bytes(alpha)


def strip_problems(data, n_theta, nr, cf='RGB565A8'):
    if cf == 'A8':
        return bin_problems(data, 'A8', n_theta, nr, None)
    stride = n_theta + 32
    if len(data) < 16 or data[:4] != b'ORS1':
        return ['not an ORS1 strip']
    nt, _, n, lvl, _, st = struct.unpack('<HBBBBH', data[4:12])
    if (nt, n, lvl, st) != (n_theta, nr, 0, stride) or len(data) != 16 + stride * nr * 3:
        return ['ORS1 n_theta %d n_r %d level %d stride %d, %d bytes; want %d, %d, 0, %d' % (nt, n, lvl, st, len(data),
                                                                                         n_theta, nr, stride)]
    return []


def strip_a8(png, cx, cy, r0, r1, n_theta):
    """The A8 strip bytes of a PNG file (canvas scale 1): its annulus [r0, r1) about (cx, cy), alpha alone."""
    w, h, rows = png_read(png)
    return unwrap(w, h, rows, 1, cx, cy, r0, r1, n_theta, True)


# ------------------------------------------------------------------ A8 rows (ARW1)
def rows_pack(w, h, rows):
    """ARW1 bytes of an RGBA image's alpha: each row cut to its extent (the format above)."""
    if not 0 < w <= 255 or not 0 < h <= 0xFFFF or len(rows) != h:
        raise PackError('ARW1: %dx%d (w 1..255, h 1..65535)' % (w, len(rows)))
    ext, body = bytearray(), bytearray()
    for row in rows:
        a = [row[4 * x + 3] for x in range(w)]
        nz = [x for x in range(w) if a[x]]
        x0, x1 = (nz[0], nz[-1] + 1) if nz else (0, 0)
        ext += bytes((x0, x1))
        body += bytes(a[x0:x1])
    data = b'ARW1' + struct.pack('<HH', w, h) + bytes(ext) + bytes(body)
    return data + (b'\0' if len(data) % 2 else b'')


def rows_unpack(data):
    """(w, h, [each row's w alpha bytes]) of ARW1 bytes; PackError on a bad file."""
    if len(data) < 8 or data[:4] != b'ARW1':
        raise PackError('not an ARW1 file')
    w, h = struct.unpack_from('<HH', data, 4)
    if not 0 < w <= 255 or len(data) < 8 + 2 * h:
        raise PackError('ARW1 %dx%d in %d bytes' % (w, h, len(data)))
    at, out = 8 + 2 * h, []
    for y in range(h):
        x0, x1 = data[8 + 2 * y], data[9 + 2 * y]
        if x0 > x1 or x1 > w or (x0 == x1 and x0):
            raise PackError('ARW1 row %d: extent [%d, %d) in %d' % (y, x0, x1, w))
        if at + x1 - x0 > len(data):
            raise PackError('ARW1 row %d runs past the file' % y)
        r = bytearray(w)
        r[x0:x1] = data[at:at + x1 - x0]
        if x1 > x0 and (not r[x0] or not r[x1 - 1]):
            raise PackError('ARW1 row %d: its extent [%d, %d) is not tight' % (y, x0, x1))
        out.append(bytes(r))
        at += x1 - x0
    if len(data) - at not in (0, 1) or (len(data) - at == 1 and data[-1]) or len(data) % 2:
        raise PackError('ARW1: %d bytes after the rows (want the even pad alone)' % (len(data) - at))
    return w, h, out


def rows_problems(data, w, h, rows):
    """The round trip of an ARW1 file against its PNG rows: the header, every row's extent and every byte."""
    try:
        bw, bh, back = rows_unpack(data)
    except PackError as e:
        return [str(e)]
    if (bw, bh) != (w, h):
        return ['ARW1 %dx%d, want %dx%d' % (bw, bh, w, h)]
    if rows is not None and data != rows_pack(w, h, rows):
        return ['the rows are not the PNG\'s alpha']
    return []


# ------------------------------------------------------------------ atlases
def atlas_problems(w, h, rows, cells, glyphs):
    """The cell table against the atlas: one cell per glyph, inside the image, no two overlapping, every inked pixel
    inside a cell, and every cell of a visible glyph inked."""
    out = []
    if len(cells) != len(glyphs):
        out.append('%d cells for %d glyphs' % (len(cells), len(glyphs)))
    owner = {}
    for i, c in enumerate(cells):
        x, y, cw, ch = c['x'], c['y'], c['w'], c['h']
        if x < 0 or y < 0 or cw < 0 or ch < 0 or x + cw > w or y + ch > h:
            out.append('cell %d (%r) outside the %dx%d atlas' % (i, glyphs[i:i + 1], w, h))
            continue
        for yy in range(y, y + ch):
            for xx in range(x, x + cw):
                if (xx, yy) in owner:
                    out.append('cells %d and %d overlap' % (owner[(xx, yy)], i))
                    break
                owner[(xx, yy)] = i
            else:
                continue
            break
    inked = [0] * len(cells)
    stray = 0
    for yy in range(h):
        row = rows[yy]
        for xx in range(w):
            if row[4 * xx + 3]:
                i = owner.get((xx, yy))
                if i is None:
                    stray += 1
                else:
                    inked[i] += 1
    if stray:
        out.append('%d inked pixels outside every cell' % stray)
    for i, g in enumerate(glyphs[:len(cells)]):
        if not g.isspace() and not inked[i]:
            out.append('cell %d (%r) holds no ink' % (i, g))
    return out


def glyph_trim(w, h, rows, ox, oy):
    """A glyph image cut to its inked box (alpha > 0), its origin moved with it: (w, h, rows, ox, oy), where (ox, oy)
    is where the image's top-left sits from the pen and baseline. An uninked glyph (a space) becomes 0 x 0."""
    ys = [y for y in range(h) if any(rows[y][4 * x + 3] for x in range(w))]
    if not ys:
        return 0, 0, [], ox, oy
    xs = [x for x in range(w) if any(rows[y][4 * x + 3] for y in ys)]
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    return x1 - x0, y1 - y0, [bytearray(rows[y][4 * x0:4 * x1]) for y in range(y0, y1)], ox + x0, oy + y0


def build_atlas(glyph_pngs, glyphs, fixed_advance=None, advance_override=None, maxw=512, pad=1, centre=False,
                even=False):
    """Pack glyph images into one atlas. glyph_pngs: one entry per character of glyphs, each (w, h, rows) (rows as
    png_read gives them), (w, h, rows, ox, oy) or (w, h, rows, ox, oy, adv); they are placed left to right in lines,
    wrapping at maxw, pad px apart. Returns (w, h, rows, cells), a cell being {x, y, w, h, ox, oy, adv} (fk/compose.h:
    the box lands with its top-left at (pen + ox, baseline + oy); adv moves the pen).
      - The origin: a glyph given as (w, h, rows) has its baseline at its image's bottom edge and its pen at its left
        edge (ox 0, oy -h), right for a bare box; a real font's glyph (a descender, a blur or a pad around the ink)
        brings its own (ox, oy), measured where it was drawn (rd wave 1 Task 7 confirmed the bare convention wrong for
        those: the font's baseline is not the image's bottom, nor its pen the image's left edge).
      - The advance: advance_override[ch] if given, else fixed_advance if given, else the glyph's own adv (rounded),
        else its width plus pad (the bare convention). advance_override maps a glyph to its own advance over
        fixed_advance (PR29: a stencil time's dash at 0.66 x the digit advance); without it the cells are exactly
        those of the call without the argument.
      - centre: a glyph whose final advance differs from its own adv sits centred in it (ox moved by
        round((advance - adv) / 2)), as a tabular time centres each glyph in its fixed slot; needs the glyph's adv.
      - even: the atlas width rounded up to an even number, so an A8 or RGB565A8 file of it has an even length (C6).
    """
    if len(glyph_pngs) != len(glyphs):
        raise PackError('%d glyph images for %d glyphs' % (len(glyph_pngs), len(glyphs)))
    over = advance_override or {}
    cells, x, y, line_h, wide = [], 0, 0, 0, 0
    for g, ch in zip(glyph_pngs, glyphs):
        if len(g) not in (3, 5, 6):
            raise PackError('glyph %r: (w, h, rows[, ox, oy[, adv]]), not %d items' % (ch, len(g)))
        gw, gh = g[0], g[1]
        ox, oy = (g[3], g[4]) if len(g) >= 5 else (0, -gh)
        own = g[5] if len(g) == 6 else None
        if gw > maxw:
            raise PackError('a glyph %d px wide does not fit an atlas %d px wide' % (gw, maxw))
        if x and x + gw > maxw:
            x, y, line_h = 0, y + line_h + pad, 0
        if ch in over:
            adv = over[ch]
        elif fixed_advance is not None:
            adv = fixed_advance
        elif own is not None:
            adv = int(round(own))
        else:
            adv = gw + pad
        if centre:
            if own is None:
                raise PackError('glyph %r: centring needs its own advance' % ch)
            ox += int(round((adv - own) / 2.0))
        cells.append({'x': x, 'y': y, 'w': gw, 'h': gh, 'ox': int(ox), 'oy': int(oy), 'adv': int(adv)})
        x += gw + pad
        wide, line_h = max(wide, x - pad), max(line_h, gh)
    w, h = max(wide, 1), max(y + line_h, 1)
    if even and w % 2:
        w += 1
    rows = [bytearray(w * 4) for _ in range(h)]
    paste_cells(rows, cells, glyph_pngs)
    return w, h, rows, cells


def paste_cells(rows, cells, glyph_pngs):
    """Each glyph image into its cell of rows (the atlas's): build_atlas's placing, and a second plane's (atlas2)."""
    for g, c in zip(glyph_pngs, cells):
        gw, gh, grows = g[0], g[1], g[2]
        if (gw, gh) != (c['w'], c['h']):
            raise PackError('a %dx%d glyph for a %dx%d cell' % (gw, gh, c['w'], c['h']))
        for gy in range(gh):
            rows[c['y'] + gy][4 * c['x']:4 * (c['x'] + gw)] = grows[gy][:4 * gw]


def build_atlas2(glow_pngs, core_pngs, glyphs, **kw):
    """A two-plane atlas: the glow glyphs packed by build_atlas (its arguments), the core glyphs (each the size of its
    glow twin, the same origin) pasted into the same cells. Returns (w, h, glow_rows, core_rows, cells)."""
    if len(core_pngs) != len(glow_pngs):
        raise PackError('%d core glyphs for %d glow glyphs' % (len(core_pngs), len(glow_pngs)))
    w, h, glow, cells = build_atlas(glow_pngs, glyphs, **kw)
    core = [bytearray(w * 4) for _ in range(h)]
    paste_cells(core, cells, core_pngs)
    return w, h, glow, core, cells


ATL2_MAGIC = b'ATL2'
CELL_KEYS = ('x', 'y', 'w', 'h', 'ox', 'oy', 'adv')


def glyph_bytes(glyphs):
    """A glyph string as the C side holds it: Latin-1, one byte per glyph (fk_atlas_t.glyphs)."""
    try:
        return glyphs.encode('latin-1')
    except UnicodeEncodeError:
        raise PackError('glyphs %r: one Latin-1 byte per glyph' % glyphs)


def pack_atlas2(w, h, glow_rows, core_rows, cells, glyphs, line_h):
    """The ATL2 bytes (the module docstring's layout) of two w x h planes (their alpha), the cells and the glyphs."""
    g = glyph_bytes(glyphs)
    if len(cells) != len(g) or not cells or b'\0' in g or not 0 < w < 65536 or not 0 < h < 65536:
        raise PackError('atlas2: %d cells for %d glyphs, %dx%d' % (len(cells), len(g), w, h))
    gb = (len(g) + 2) & ~1                                   # the NUL, then padded to even
    out = bytearray(ATL2_MAGIC + struct.pack('<HHHHHH', w, h, len(cells), line_h, gb, 0))
    for c in cells:
        out += struct.pack('<7h', *(c[k] for k in CELL_KEYS))
    out += g + bytes(gb - len(g))
    for rows in (glow_rows, core_rows):
        if len(rows) != h or any(len(r) < 4 * w for r in rows):
            raise PackError('atlas2: a plane is not %dx%d' % (w, h))
        out += bytes(r[4 * x + 3] for r in rows for x in range(w))
    return bytes(out)


def atlas2_unpack(data):
    """An ATL2 file's parts: {w, h, line_h, cells, glyphs, glow, core} (planes as bytes), or PackError."""
    if len(data) < 16 or data[:4] != ATL2_MAGIC:
        raise PackError('not an ATL2 atlas')
    w, h, n, line_h, gb, res = struct.unpack('<HHHHHH', data[4:16])
    at = 16 + 14 * n
    if res or gb % 2 or gb < n + 1 or not n or not w or not h or len(data) != at + gb + 2 * w * h:
        raise PackError('ATL2 %dx%d, %d cells, glyph bytes %d, reserved %d: %d bytes' % (w, h, n, gb, res, len(data)))
    cells = [dict(zip(CELL_KEYS, struct.unpack_from('<7h', data, 16 + 14 * i))) for i in range(n)]
    g = data[at:at + gb]
    if g[n] != 0 or b'\0' in g[:n]:
        raise PackError('ATL2: the glyph string is not %d bytes and a NUL' % n)
    planes = at + gb
    return {'w': w, 'h': h, 'line_h': line_h, 'cells': cells, 'glyphs': g[:n].decode('latin-1'),
            'glow': data[planes:planes + w * h], 'core': data[planes + w * h:]}


def atlas2_problems(data, e, rows):
    """An ATL2 file against its manifest entry e (w, h, cells, glyphs, line_h) and its PNG rows (2h tall: the glow
    plane's alpha over the core plane's): the header, every alpha byte, and each plane's cells (atlas_problems)."""
    try:
        a = atlas2_unpack(data)
    except PackError as x:
        return [str(x)]
    out = []
    for k in ('w', 'h', 'cells', 'glyphs', 'line_h'):
        if a[k] != e.get(k):
            out.append('ATL2 %s %r, the manifest says %r' % (k, a[k], e.get(k)))
    if out:
        return out
    w, h = a['w'], a['h']
    if rows is None or len(rows) != 2 * h:
        return ['the PNG is not %dx%d (two planes)' % (w, 2 * h)]
    for name, plane, prow in (('glow', a['glow'], rows[:h]), ('core', a['core'], rows[h:])):
        if plane != bytes(r[4 * x + 3] for r in prow for x in range(w)):
            out.append('the %s plane is not the PNG\'s' % name)
        out += ['%s plane: %s' % (name, p) for p in atlas_problems(w, h, prow, a['cells'], a['glyphs'])]
    return out


# ------------------------------------------------------------------ manifest, committed bytes and the round trip
def entry_of(row, extra):
    e = {k: row[k] for k in ('file', 'cf', 'w', 'h', 'out', 'face', 'fn', 'crc32', 'src', 'origin') if k in row}
    e['name'] = row['asset']
    for k in ('r0', 'r1', 'n_theta', 'scale', 'glyphs', 'cells', 'line_h'):
        if k in extra:
            e[k] = extra[k]
        elif k in row:
            e[k] = row[k]
    if row['out'] == 'strip':
        e['n_r'] = int(math.ceil(e['r1'] - e['r0']))
    return e


def committed_problems(path, row):
    """A crc32 row: the committed file's header, size and pixel CRC-32."""
    if not os.path.isfile(path):
        return ['%s: missing' % row['file']]
    data = open(path, 'rb').read()
    probs = bin_problems(data, row['cf'], row['w'], row['h'], None)
    if not probs and '%08x' % zlib.crc32(data[12:]) != row['crc32']:
        probs = ['pixel CRC %08x, the table says %s' % (zlib.crc32(data[12:]), row['crc32'])]
    return ['%s: %s' % (row['file'], p) for p in probs]


SHARED_UI_EXTRA = ('w', 'h', 'samples', 'rate', 'channels', 'format')


def shared_ui_entry(name, row, src):
    """The manifest entry of a shared_ui row (the rim keyboard's shared assets, Task 8: committed bytes, a byte-equal
    copy of firmware/components/watch_ui/assets' file, written there by tools/kb_assets.py), in the key order
    tools/kb_assets.py copy writes."""
    e = {'file': row['file'], 'name': name, 'cf': row['cf'], 'out': 'shared_ui', 'face': 'shared',
         'size': row['size'], 'crc32': row['crc32'], 'src': src}
    e.update((k, row[k]) for k in SHARED_UI_EXTRA if k in row)
    return e


def shared_ui_problems(path, e):
    """A shared_ui entry: the committed file's size and whole-file CRC-32 (zlib's), and for the icon strip its LVGL
    RGB565A8 header, for the sound its length in samples. The copies' equality with the source is check_apps'."""
    if not os.path.isfile(path):
        return ['%s: missing' % e.get('file')]
    data = open(path, 'rb').read()
    probs = []
    if len(data) != e.get('size') or '%08x' % zlib.crc32(data) != e.get('crc32'):
        probs.append('%d bytes, CRC-32 %08x; the entry says %r bytes, %r' % (len(data), zlib.crc32(data), e.get('size'),
                                                                           e.get('crc32')))
    if e.get('cf') == 'PCM' and (len(data) % 2 or e.get('samples') != len(data) // 2 or e.get('rate') != 16000 or
                                 e.get('channels') != 1 or len(data) > 128000):
        probs.append('not s16le mono 16 kHz PCM of %r samples' % e.get('samples'))
    elif e.get('cf') == 'RGB565A8':
        if not (isinstance(e.get('w'), int) and isinstance(e.get('h'), int)):
            probs.append('an RGB565A8 entry without its w and h')
        else:
            probs += bin_problems(data, 'RGB565A8', e['w'], e['h'], None)
    elif e.get('cf') not in ('RAW', 'PCM', 'RGB565A8'):
        probs.append('cf %r (RAW, PCM or RGB565A8)' % e.get('cf'))
    return ['%s: %s' % (e.get('file'), p) for p in probs]


def write_manifest(path, generator, entries, notices=None):
    """The manifest: {generator, [notices,] assets}. notices (a theme's licence pointer) is written only when given, so
    a caller without one writes the bytes it always did."""
    m = {'generator': generator}
    if notices is not None:
        m['notices'] = notices
    m['assets'] = entries
    open(path, 'w').write(json.dumps(m, indent=1) + '\n')


def entry_problems(e, path, pngdir):
    if e.get('out') == 'shared_ui':
        return shared_ui_problems(path, e)
    data = open(path, 'rb').read()
    if 'crc32' in e:
        return committed_problems(path, e)
    if e['cf'] == 'PCM':
        ns = e.get('samples')
        if len(data) % 2 or not isinstance(ns, int) or len(data) != 2 * ns or len(data) > 128000:
            return ['%s: %d bytes of PCM, manifest samples %r (<= 128000 even bytes)' % (e['file'], len(data), ns)]
        return []
    if len(data) % 2:
        return ['%s: an odd length (%d)' % (e['file'], len(data))]
    if e['cf'] == 'DAT':   # a generated table: no PNG; its generator regenerates and compares it
        return []
    png = os.path.join(pngdir, e['name'] + '.png')
    if not os.path.isfile(png):
        return ['%s: no %s to compare with' % (e['file'], png)]
    w, h, rows = png_read(png)
    if e['out'] == 'atlas2':
        if (w, h) != (e['w'], 2 * e['h']):
            return ['%s: the PNG is %dx%d, not two %dx%d planes' % (e['file'], w, h, e['w'], e['h'])]
        return ['%s: %s' % (e['file'], p) for p in atlas2_problems(data, e, rows)]
    if e['out'] == 'rows':   # ARW1 (rd wave 2 Task 12)
        if (w, h) != (e['w'], e['h']):
            return ['%s: the PNG is %dx%d' % (e['file'], w, h)]
        return ['%s: %s' % (e['file'], p) for p in rows_problems(data, e['w'], e['h'], rows)]
    if e['out'] == 'strip':
        probs = strip_problems(data, e['n_theta'], e['n_r'], e['cf'])
        if not probs and data != unwrap(w, h, rows, e.get('scale', 1), e.get('cx', 233), e.get('cy', 233), e['r0'],
                                        e['r1'], e['n_theta'], e['cf'] == 'A8'):
            probs = ['the strip is not the PNG\'s unwrap']
        return ['%s: %s' % (e['file'], p) for p in probs]
    if (w, h) != (e['w'], e['h']):
        return ['%s: the PNG is %dx%d' % (e['file'], w, h)]
    probs = bin_problems(data, e['cf'], e['w'], e['h'], rows)
    if e['out'] == 'atlas':
        probs += atlas_problems(w, h, rows, e['cells'], e['glyphs'])
    return ['%s: %s' % (e['file'], p) for p in probs]


def round_trip(assets_dir, out_dir):
    """The manifest's every file against its PNG in out_dir, and nothing in assets_dir the manifest does not list.
    Returns (problems, number of files listed)."""
    man = json.load(open(os.path.join(assets_dir, 'manifest.json')))['assets']
    listed = {e['file'] for e in man} | {'manifest.json'}
    bad = []
    for e in man:
        p = os.path.join(assets_dir, e['file'])
        if not os.path.isfile(p):
            bad.append('%s: missing' % e['file'])
            continue
        bad += entry_problems(e, p, out_dir)
    for f in sorted(set(os.listdir(assets_dir)) - listed):
        bad.append('%s: not in the manifest' % f)
    return bad, len(listed - {'manifest.json'})


def partition_size(partitions, label='assets'):
    """The size of the partition named label in a partition CSV (None if there is none)."""
    size = None
    for line in open(partitions):
        c = [x.strip() for x in line.split('#', 1)[0].split(',')]
        if len(c) >= 5 and c[0] == label:
            size = int(c[4], 0)
    return size


def budget(assets_dir, partitions, warn, fail, face_caps=None, rows=None):
    """The assets image's size as tools/assets.sh counts it (32 B + 64 B per file + the file's length) against warn
    and fail. face_caps: {face: cap in bytes}, with rows (table rows naming each file's face) a per-face cap too.
    Returns (rc, used, partition size, lines): rc 1 when over fail or a face cap, lines the failure messages; the
    caller says what being over warn means."""
    size = partition_size(partitions)
    files = os.listdir(assets_dir)
    used = 32 + sum(64 + os.path.getsize(os.path.join(assets_dir, f)) for f in files)
    lines, rc = [], 0
    if used > fail:
        lines.append('ASSETS BAD: %d B exceed the %d-byte budget' % (used, fail))
        rc = 1
    if face_caps:
        face_of = {r['file']: r.get('face') for r in rows or []}
        per = {}
        for f in files:
            face = face_of.get(f)
            if face in face_caps:
                per[face] = per.get(face, 0) + os.path.getsize(os.path.join(assets_dir, f))
        for face, cap in sorted(face_caps.items()):
            if per.get(face, 0) > cap:
                lines.append('ASSETS BAD: face %s is %d B, over its %d-byte cap' % (face, per[face], cap))
                rc = 1
    return rc, used, size, lines


def write_assets_meta(path, prefix, entries, preamble, trailer=()):
    """The generated C header: names and sizes of every manifest entry (ASSET_<NAME>, _W, _H, _X/_Y for an origin,
    _PX/_PY for a pivot; a strip's _NT, _R0, _NR; a PCM file's _SAMPLES), and an atlas's line height, glyph string and
    cells (<PREFIX>_GLYPHS_<NAME>, <PREFIX>_CELLS_<NAME>, static const fk_cell_t); a two-plane atlas's plane size and
    line height (its cells and glyphs are in its file). preamble: the lines before the
    include guard (the file's comment), trailer: the lines after the entries. The file is rewritten only when its text
    changes. Returns the number of entries."""
    P = prefix.upper()
    unused = P + '_META_UNUSED'
    L = list(preamble) + ['#pragma once', '#include <stdint.h>', '#include "fk/compose.h"', '',
                          '#if defined(__GNUC__)', '#define %s __attribute__((unused))' % unused, '#else',
                          '#define %s' % unused, '#endif', '']
    for e in entries:
        if e.get('out') == 'shared_ui':   # the keyboard's shared assets: watch_ui/kb_meta.h names them, no theme header
            continue
        N = 'ASSET_' + e['name'].upper()
        L.append('#define %s "%s"' % (N, e['file']))
        if e['cf'] == 'PCM':
            L += ['#define %s_SAMPLES %d' % (N, e['samples']), '']
            continue
        if e.get('out') == 'strip':
            L += ['#define %s_NT %d' % (N, e['n_theta']), '#define %s_R0 %d' % (N, int(e['r0'])),
                  '#define %s_NR %d' % (N, e['n_r']), '']
            continue
        L += ['#define %s_W %d' % (N, e['w']), '#define %s_H %d' % (N, e['h'])]
        for k, (sx, sy) in (('origin', ('X', 'Y')), ('pivot', ('PX', 'PY'))):
            if k in e:
                L += ['#define %s_%s %d' % (N, sx, e[k][0]), '#define %s_%s %d' % (N, sy, e[k][1])]
        if e.get('out') == 'atlas2':   # two planes of w x h; the cells and glyphs are in the file (ATL2)
            L.append('#define %s_LINE_H %d' % (N, e['line_h']))
        if e.get('out') == 'atlas':
            M = e['name'].upper()
            esc = ''.join('\\%03o' % ord(ch) if ch in '"\\' or not 32 <= ord(ch) < 127 else ch for ch in e['glyphs'])
            L.append('#define %s_LINE_H %d' % (N, e['line_h']))
            L.append('static const char %s_GLYPHS_%s[] %s = "%s";' % (P, M, unused, esc))
            L.append('static const fk_cell_t %s_CELLS_%s[%d] %s = {' % (P, M, len(e['cells']), unused))
            for c in e['cells']:
                L.append('    {%d, %d, %d, %d, %d, %d, %d},' % tuple(c[k] for k in ('x', 'y', 'w', 'h', 'ox', 'oy', 'adv')))
            L.append('};')
        L.append('')
    L += list(trailer)
    text = '\n'.join(L)
    if not os.path.isfile(path) or open(path).read() != text:
        open(path, 'w').write(text)
    return len(entries)


# ------------------------------------------------------------------ self-test
def selftest():
    import tempfile
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)
            print('SELFTEST FAIL', what)
    with tempfile.TemporaryDirectory() as td:
        # 1. a 3x2 RGB565A8 PNG round-trips exactly (colours on 5/6-bit levels; every PNG filter type decoded)
        px = [[(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0)], [(8, 4, 8, 1), (123, 125, 66, 254), (255, 255, 255, 77)]]
        lv = [[(round(round(r * 31 / 255) * 255 / 31), round(round(g * 63 / 255) * 255 / 63),
                round(round(b * 31 / 255) * 255 / 31), a) for r, g, b, a in row] for row in px]
        rows = [bytearray(v for p in row for v in p) for row in lv]
        for f in range(5):
            p = os.path.join(td, 'f%d.png' % f)
            png_write(p, 3, 2, rows, f)
            w, h, back = png_read(p)
            ok((w, h) == (3, 2) and [bytes(r) for r in back] == [bytes(r) for r in rows], 'PNG filter %d round trip' % f)
        data = pack_bin(3, 2, rows, 'RGB565A8', 'selftest')
        ok(len(data) == 12 + 3 * 2 * 3 and data[1] == 0x14, 'RGB565A8 layout')
        ok(bin_problems(data, 'RGB565A8', 3, 2, rows) == [], 'RGB565A8 round trip')
        dec = []
        for i in range(6):
            v = struct.unpack_from('<H', data, 12 + 2 * i)[0]
            r5, g6, b5 = v >> 11, v >> 5 & 63, v & 31
            dec.append((round(r5 * 255 / 31), round(g6 * 255 / 63), round(b5 * 255 / 31), data[12 + 12 + i]))
        ok(dec == [p for row in lv for p in row], 'RGB565A8 decodes to the PNG exactly')
        bad = bytearray(data)
        bad[12 + 12 + 4] ^= 1
        ok(bin_problems(bytes(bad), 'RGB565A8', 3, 2, rows) != [], 'a changed alpha is caught')
        try:
            pack_bin(3, 2, rows, 'RGB565', 'selftest')
            ok(False, 'RGB565 refuses a translucent PNG')
        except PackError:
            pass
        a8 = pack_bin(3, 2, rows, 'A8', 'selftest')
        ok(a8[1] == 0x0E and a8[12:] == bytes(p[3] for row in lv for p in row), 'A8 is the alpha plane alone')

        # 1b. write_bin / read_bin: each format writes, reads back and compares, on fixed synthetic planes
        colour = b''.join(struct.pack('<H', rgb565(*p[:3])) for row in lv for p in row)
        alpha = bytes(p[3] for row in lv for p in row)
        for cf, kw, want in (('RGB565', dict(rgb=colour), (colour, None)),
                             ('RGB565A8', dict(rgb=colour, a8=alpha), (colour, alpha)),
                             ('A8', dict(a8=alpha), (None, alpha))):
            p = os.path.join(td, cf + '.bin')
            write_bin(p, cf, 3, 2, **kw)
            ok(read_bin(p) == (cf, 3, 2) + want, '%s write_bin / read_bin round trip' % cf)
        ok(open(os.path.join(td, 'RGB565A8.bin'), 'rb').read() == data, 'write_bin equals pack_bin')
        for cf, kw in (('RGB565', {}), ('RGB565', dict(rgb=colour[:-2])), ('A8', dict(rgb=colour, a8=alpha)),
                       ('A8', dict(a8=alpha[:-1])), ('RGB565A8', dict(rgb=colour)), ('XYZ', {})):
            try:
                write_bin(os.path.join(td, 'x.bin'), cf, 3, 2, **kw)
                ok(False, 'write_bin refuses %s %s' % (cf, sorted(kw)))
            except PackError:
                pass
        short = os.path.join(td, 'short.bin')
        open(short, 'wb').write(data[:-1])
        try:
            read_bin(short)
            ok(False, 'read_bin refuses a truncated bin')
        except PackError:
            pass

        # 2. the polar unwrap: a spoke at 3 o'clock lands at column n_theta / 4
        n, S = 256, 120
        sp = [bytearray(S * 4) for _ in range(S)]
        for x in range(64, S):
            sp[60][4 * x:4 * x + 4] = bytes((255, 255, 255, 255))   # row 60's centres are 0.5 px below the centre
        st = unwrap(S, S, sp, 1, 60, 60, 20, 50, n)
        ok(strip_problems(st, n, 30) == [], 'ORS1 header and size')
        nr, stride = 30, n + 32
        ap = 16 + stride * nr * 2
        cols = {max(range(n), key=lambda t: st[ap + ri * stride + t]) for ri in range(nr)
                if 60 + 20 + ri + 1 <= S and max(st[ap + ri * stride:ap + ri * stride + n])}
        ok(cols == {n // 4}, 'spoke at 3 o\'clock -> column %d (got %s)' % (n // 4, sorted(cols)))
        top = [bytearray(S * 4) for _ in range(S)]
        for y in range(0, 59):
            top[y][4 * 60:4 * 60 + 4] = bytes((255, 0, 0, 255))
        st = unwrap(S, S, top, 1, 60, 60, 20, 50, n)
        cols = {max(range(n), key=lambda t: st[ap + ri * stride + t]) for ri in range(nr)}
        ok(cols == {0}, '12 o\'clock -> column 0 (got %s)' % sorted(cols))
        st8 = unwrap(S, S, top, 1, 60, 60, 20, 50, n, a8=True)   # the A8 strip is the ORS1 alpha plane
        ok(strip_problems(st8, n, 30, 'A8') == [] and st8[1] == 0x0E, 'A8 strip header and size')
        ok(all(st8[12 + ri * n:12 + ri * n + n] == st[ap + ri * stride:ap + ri * stride + n] for ri in range(nr)),
           'the A8 strip is the ORS1 strip\'s alpha, row for row')
        tp = os.path.join(td, 'top.png')
        png_write(tp, S, S, top)
        ok(strip_a8(tp, 60, 60, 20, 50, n) == st8, 'strip_a8 reads a PNG and gives the A8 strip')

        # 3. an atlas cell table matches its glyph boxes
        W, H = 12, 6
        at = [bytearray(W * 4) for _ in range(H)]
        for x, y in ((1, 1), (2, 3), (7, 2), (9, 4)):
            at[y][4 * x + 3] = 200
        cells = [dict(x=0, y=0, w=5, h=6, ox=0, oy=-5, adv=5), dict(x=6, y=0, w=6, h=6, ox=0, oy=-5, adv=6)]
        ok(atlas_problems(W, H, at, cells, 'AB') == [], 'atlas cells match')
        ok(atlas_problems(W, H, at, [cells[0], dict(cells[1], x=7, w=2)], 'AB') != [], 'ink outside a cell is caught')
        ok(atlas_problems(W, H, at, [cells[0], dict(cells[1], x=4)], 'AB') != [], 'overlapping cells are caught')
        ok(atlas_problems(W, H, at, cells, 'A') != [], 'a glyph count mismatch is caught')

        # 3b. build_atlas: glyph images packed into one atlas, a cell table that passes atlas_problems
        def glyph(gw, gh, a):
            return gw, gh, [bytearray((200, 100, 50, a) * gw) for _ in range(gh)]
        gl = [glyph(3, 5, 255), glyph(4, 5, 255), glyph(2, 5, 255), glyph(5, 5, 255)]
        aw, ah, arows, acells = build_atlas(gl, 'abcd', maxw=10)
        ok(atlas_problems(aw, ah, arows, acells, 'abcd') == [] and aw <= 10, 'build_atlas: a valid wrapped atlas')
        ok(len({c['y'] for c in acells}) > 1, 'build_atlas: wraps at maxw')
        ok([c['adv'] for c in acells] == [4, 5, 3, 6], 'build_atlas: natural advance is width + 1')
        _, _, _, fcells = build_atlas(gl, 'abcd', fixed_advance=7, maxw=10)
        ok({c['adv'] for c in fcells} == {7}, 'build_atlas: fixed_advance')
        try:
            build_atlas(gl, 'abc')
            ok(False, 'build_atlas refuses a glyph count mismatch')
        except PackError:
            pass
        try:
            build_atlas([glyph(20, 5, 255)], 'a', maxw=10)
            ok(False, 'build_atlas refuses a glyph wider than the atlas')
        except PackError:
            pass

        # 3c. real-glyph conventions (rd wave 1 Task 7): an origin and an advance of its own, centring, even width,
        # the advance override (PR29), the trim
        g6 = [(3, 5) + (glyph(3, 5, 255)[2], -2, -9, 6.4), (2, 7) + (glyph(2, 7, 255)[2], -1, -10, 3.0)]
        _, _, _, c6 = build_atlas(g6, 'ab')
        ok([(c['ox'], c['oy'], c['adv']) for c in c6] == [(-2, -9, 6), (-1, -10, 3)], 'build_atlas: a glyph\'s own origin and advance')
        _, _, _, cc = build_atlas(g6, 'ab', fixed_advance=8, centre=True)
        ok([(c['ox'], c['adv']) for c in cc] == [(-2 + 1, 8), (-1 + 2, 8)], 'build_atlas: centred in a fixed advance (%s)' % cc)
        _, _, _, co = build_atlas(g6, 'ab', fixed_advance=8, advance_override={'b': 5}, centre=True)
        ok([(c['ox'], c['adv']) for c in co] == [(-1, 8), (0, 5)], 'build_atlas: the override, centred (%s)' % co)
        ew = build_atlas([glyph(3, 5, 255)], 'a', even=True)[0]
        ok(ew == 4 and build_atlas([glyph(3, 5, 255)], 'a')[0] == 3, 'build_atlas: even rounds the width up')
        try:
            build_atlas(g6[:1], 'a', centre=True) and build_atlas([glyph(3, 5, 255)], 'a', centre=True)
            ok(False, 'build_atlas: centring a glyph without its own advance is refused')
        except PackError:
            pass
        tr = [bytearray(6 * 4) for _ in range(5)]
        tr[1][4 * 2 + 3] = tr[3][4 * 4 + 3] = 9
        tw, th, trows, tox, toy = glyph_trim(6, 5, tr, -1, -4)
        ok((tw, th, tox, toy) == (3, 3, 1, -3) and trows[0][3] == 9 and trows[2][4 * 2 + 3] == 9, 'glyph_trim')
        ok(glyph_trim(6, 5, [bytearray(24) for _ in range(5)], 0, 0)[:2] == (0, 0), 'glyph_trim: an uninked glyph')
        try:
            write_bin(os.path.join(td, 'odd.bin'), 'A8', 3, 3, a8=bytes(9))
            ok(False, 'write_bin refuses an odd length (C6)')
        except PackError:
            pass

        # 3d. ATL2: two planes of one cell table; packed, unpacked, checked; the C layout's offsets
        gw = [glyph(4, 6, 90), glyph(3, 6, 90)]
        gc = [(4, 6, [bytearray(b for x in range(4) for b in (255, 255, 255, 255 if 1 <= x <= 2 else 0)) for _ in range(6)]),
              (3, 6, [bytearray(b for x in range(3) for b in (255, 255, 255, 200 if x == 1 else 0)) for _ in range(6)])]
        aw, ah, glow, core, a2c = build_atlas2(gw, gc, 'A\xb7', even=True)
        a2 = pack_atlas2(aw, ah, glow, core, a2c, 'A\xb7', ah)
        ok(len(a2) % 2 == 0 and a2[:4] == b'ATL2' and len(a2) == 16 + 14 * 2 + 4 + 2 * aw * ah, 'ATL2 layout')
        ok(a2[16 + 28:16 + 28 + 3] == b'A\xb7\x00', 'ATL2 glyphs in Latin-1')
        u = atlas2_unpack(a2)
        ok(u['cells'] == a2c and u['glyphs'] == 'A\xb7' and u['glow'] != u['core'] and (u['w'], u['h']) == (aw, ah),
           'ATL2 round trip')
        ent = dict(name='xy_a2', file='xy_a2.bin', cf='A8', w=aw, h=ah, out='atlas2', cells=a2c, glyphs='A\xb7', line_h=ah)
        both = glow + core
        ok(atlas2_problems(a2, ent, both) == [], 'atlas2_problems passes (%s)' % atlas2_problems(a2, ent, both))
        bad2 = bytearray(a2)
        bad2[-1] ^= 1
        ok(atlas2_problems(bytes(bad2), ent, both) != [], 'atlas2_problems: a changed core byte')
        ok(atlas2_problems(a2, dict(ent, line_h=1), both) != [], 'atlas2_problems: a manifest mismatch')
        for cut in (a2[:-1], a2[:15], b'XTL2' + a2[4:]):
            try:
                atlas2_unpack(cut)
                ok(False, 'atlas2_unpack refuses a bad file')
            except PackError:
                pass
        try:
            pack_atlas2(aw, ah, glow, core, a2c, 'A\u20ac', ah)
            ok(False, 'pack_atlas2 refuses a glyph outside Latin-1')
        except PackError:
            pass

        # 4. NAME_RULE: the prefix plus at most 12 characters
        rule = NAME_RULE('xy_')
        ok(rule.fullmatch('xy_st_lidf') is not None and rule.fullmatch('xy_' + 'a' * 12) is not None, 'NAME_RULE accepts')
        ok(rule.fullmatch('xy_' + 'a' * 13) is None and rule.fullmatch('zz_abc') is None, 'NAME_RULE refuses')

        # 5. the manifest, assets_meta.h, the budget and the round trip, on a small asset set
        assets, pngs = os.path.join(td, 'assets'), os.path.join(td, 'png')
        os.mkdir(assets)
        os.mkdir(pngs)
        png_write(os.path.join(pngs, 'xy_one.png'), 3, 2, rows)
        open(os.path.join(assets, 'xy_one.bin'), 'wb').write(data)
        png_write(os.path.join(pngs, 'xy_at.png'), W, H, at)
        open(os.path.join(assets, 'xy_at.bin'), 'wb').write(pack_bin(W, H, at, 'A8', 'xy_at'))
        entries = [dict(name='xy_one', file='xy_one.bin', cf='RGB565A8', w=3, h=2, out='bin', origin=[4, 5]),
                   dict(name='xy_at', file='xy_at.bin', cf='A8', w=W, h=H, out='atlas', cells=cells, glyphs='A"',
                        line_h=6)]
        man = os.path.join(assets, 'manifest.json')
        write_manifest(man, 'selftest', entries)
        ok(json.load(open(man)) == {'generator': 'selftest', 'assets': entries}, 'write_manifest')
        png_write(os.path.join(pngs, 'xy_a2.png'), aw, 2 * ah, both)
        open(os.path.join(assets, 'xy_a2.bin'), 'wb').write(a2)
        entries.append(ent)
        write_manifest(man, 'selftest', entries)
        bad, count = round_trip(assets, pngs)
        ok(bad == [] and count == 3, 'round_trip passes a good set (%s)' % bad)
        open(os.path.join(assets, 'extra.bin'), 'wb').write(bytes(4))
        ok(any('not in the manifest' in b for b in round_trip(assets, pngs)[0]), 'round_trip: a stray file')
        os.remove(os.path.join(assets, 'extra.bin'))
        flip = bytearray(data)
        flip[14] ^= 0x10
        open(os.path.join(assets, 'xy_one.bin'), 'wb').write(bytes(flip))
        ok(round_trip(assets, pngs)[0] != [], 'round_trip: a changed pixel')
        open(os.path.join(assets, 'xy_one.bin'), 'wb').write(data)
        hdr = os.path.join(td, 'meta.h')
        n1 = write_assets_meta(hdr, 'xy', entries, ['/* selftest */'], ['/* tail */'])
        text = open(hdr).read()
        ok('#define ASSET_XY_A2_LINE_H %d' % ah in text and 'XY_CELLS_XY_A2' not in text, 'assets_meta.h: an atlas2')
        ok(n1 == 3 and '#define ASSET_XY_ONE "xy_one.bin"' in text and '#define ASSET_XY_ONE_W 3' in text and
           '#define ASSET_XY_ONE_X 4' in text and '#define ASSET_XY_AT_LINE_H 6' in text, 'assets_meta.h defines')
        ok('XY_GLYPHS_XY_AT[] XY_META_UNUSED = "A\\042"' in text and 'XY_CELLS_XY_AT[2]' in text and
           text.startswith('/* selftest */\n#pragma once') and text.endswith('/* tail */'), 'assets_meta.h atlas and frame')
        stamp = os.stat(hdr).st_mtime_ns
        write_assets_meta(hdr, 'xy', entries, ['/* selftest */'], ['/* tail */'])
        ok(os.stat(hdr).st_mtime_ns == stamp, 'assets_meta.h is not rewritten when unchanged')
        parts = os.path.join(td, 'parts.csv')
        open(parts, 'w').write('nvs, data, nvs, 0x9000, 0x6000\nassets, data, spiffs, 0x10000, 0x1000 # c\n')
        used = 32 + sum(64 + os.path.getsize(os.path.join(assets, f)) for f in os.listdir(assets))
        r = budget(assets, parts, used + 1, used + 2)
        ok(r[0] == 0 and r[1] == used and r[2] == 4096, 'budget: under both lines')
        ok(budget(assets, parts, used - 1, used + 2)[0] == 0 and budget(assets, parts, used - 2, used - 1)[0] == 1,
           'budget: warn passes, fail fails')
        ok(budget(assets, parts, 10 ** 9, 10 ** 9, face_caps={'a': 1}, rows=[dict(file='xy_one.bin', face='a')])[0] == 1,
           'budget: a face over its cap fails')
    # ARW1 (rd wave 2 Task 12): rows cut to their extents round-trip; a changed byte, a loose extent, a short file and
    # an odd pad are caught
    ar = [bytearray(4 * 5) for _ in range(4)]
    for y, xs in enumerate(([], [2], [0, 1, 4], [1, 3])):
        for x in xs:
            ar[y][4 * x:4 * x + 4] = bytes((255, 255, 255, 10 * (x + 1) + y))
    ad = rows_pack(5, 4, ar)
    ok(ad[:8] == b'ARW1' + struct.pack('<HH', 5, 4) and ad[8:16] == bytes((0, 0, 2, 3, 0, 5, 1, 4)) and
       len(ad) == 16 + 1 + 5 + 3 + 1, 'ARW1 layout (extents, bytes, the even pad)')
    ok(rows_problems(ad, 5, 4, ar) == [] and rows_unpack(ad)[2][2] == bytes((12, 22, 0, 0, 52)), 'ARW1 round trip')
    bad = bytearray(ad)
    bad[17] ^= 1
    ok(rows_problems(bytes(bad), 5, 4, ar) != [], 'ARW1: a changed byte is caught')
    bad = bytearray(ad)
    bad[10] = 1
    ok(rows_problems(bytes(bad), 5, 4, None) != [], 'ARW1: a loose extent is caught')
    ok(rows_problems(ad[:-2], 5, 4, None) != [] and rows_problems(ad + b'\0\0', 5, 4, None) != [],
       'ARW1: a short or long file is caught')
    ok(rows_problems(ad, 5, 3, None) != [], 'ARW1: the wrong size is caught')
    # the shared_ui entries (rim keyboard Task 8): size and whole-file CRC-32; a sound's samples; an icon strip's header
    with tempfile.TemporaryDirectory() as td:
        raw, pcm = os.path.join(td, 'kb_x.bin'), os.path.join(td, 'kb_x.pcm')
        open(raw, 'wb').write(b'OFA1rest')
        open(pcm, 'wb').write(bytes(8))
        er = {'file': 'kb_x.bin', 'cf': 'RAW', 'out': 'shared_ui', 'size': 8, 'crc32': '%08x' % zlib.crc32(b'OFA1rest')}
        ep = {'file': 'kb_x.pcm', 'cf': 'PCM', 'out': 'shared_ui', 'size': 8, 'crc32': '%08x' % zlib.crc32(bytes(8)),
              'samples': 4, 'rate': 16000, 'channels': 1}
        ok(entry_problems(er, raw, td) == [] and entry_problems(ep, pcm, td) == [], 'shared_ui: good entries pass')
        ok(entry_problems(dict(er, crc32='00000000'), raw, td) != [], 'shared_ui: a CRC other than the file\'s is caught')
        ok(entry_problems(dict(er, size=9), raw, td) != [], 'shared_ui: a size other than the file\'s is caught')
        ok(entry_problems(dict(ep, samples=5), pcm, td) != [], 'shared_ui: a sound\'s wrong length is caught')
        ok(entry_problems(dict(er, cf='RGB565A8', w=1, h=1), raw, td) != [], 'shared_ui: a bad icon header is caught')
        ok(shared_ui_entry('kb_x', dict(ep, w=3), 's')['src'] == 's', 'shared_ui: the entry carries its source')
    print('facepack selftest: %s' % ('FAIL (%d)' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def main():
    if sys.argv[1:] == ['selftest']:
        return selftest()
    print('usage: facepack.py selftest', file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main())
