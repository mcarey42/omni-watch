#!/usr/bin/env python3
"""The dw asset packer (dw wave 1 Task 9): LVGL v9 bins, ORS1 strips and glyph atlases from the PNGs
tools/dw_export.mjs writes, the assets manifest, the generated dw/assets_meta.h, and the round trip. Standard library
only; the formats themselves (PNG codec, bins, strips, atlas checks, manifest, assets_meta.h, round trip, budget) are
tools/facepack.py's, shared by every theme: this file keeps dw's rows, names, swap rules and overlay logic.
Its self-test runs under any Python, ctest's included.

  dw_pack.py selftest
  dw_pack.py pack --table T --out O --assets A [--sfx J]    pack every row of T (dw_layers.json) from O/*.png and
                                                            O/layers.json into A, and write A/manifest.json
  dw_pack.py pack --overlay ART --out ART/out --generic A   the overlay's rows (ART/layers.json) into ART/assets only;
                                                            each must equal its generic namesake in A (name, cf, w, h)
  dw_pack.py meta --assets A --table T --header H           H: ASSET_DW_* names and sizes, the #5 pane and lens
                                                            geometry, atlas cells (static const fk_cell_t)
  dw_pack.py budget --assets A --partitions CSV [--fail N] [--warn N]
                                                            fail above 12 255 232 B (R69), warn above 11 600 000 B
                                                            (the wave-2 line with Vortex, wave 2 Task 14) by default
  dw_pack.py check --assets A --out O                       the round trip: every bin decoded against its PNG, every
                                                            manifest file present and nothing else in A
  dw_pack.py check --overlay ART --generic A                the same for ART/assets against ART/out, and the
                                                            namesake rule; the files overlay.json replaces with no
                                                            layers.json row are checked verbatim against their twins
                                                            (a .bin by header and size, a .pcm an even 2..110 080 B)
  dw_pack.py rerender --out O --assets A                    the generic rows packed from O (a fresh render of
                                                            dw_generic.js) must equal A's committed bytes (G1)

Formats (firmware/facekit/face_kit/include/fk/compose.h, fk/annulus.h):
  - LVGL v9 .bin: u8 magic 0x19, u8 cf, u16 flags 0, u16 w, h, stride (w x bpp), u16 0, then the planes. RGB565 (0x12,
    opaque), RGB565A8 (0x14: the colour plane, unpremultiplied, then the A8 plane), A8 (0x0E: the alpha plane alone).
    Every colour channel is rounded to its nearest 5/6-bit level (R86): (c x 31 + 127) // 255.
  - ORS1 strip (omni/ring.h): "ORS1", u16 n_theta, u8 r_min, u8 n_r, u8 level 0, u8 0, u16 stride (n_theta + 32),
    4 zero bytes, then stride x n_r RGB565 and stride x n_r A8. Column 0 is 12 o'clock, columns run clockwise, row 0
    is r0 (compose.h, C12); entry (ri, th) samples the canvas at radius r0 + ri + 0.5 and angle (th + 0.5) 2 pi / n,
    bilinear in premultiplied colour.
  - A8 strip (Task 15: a one-colour script ring, #14's): the same unwrap's alpha alone, packed as an LVGL A8 bin
    n_theta wide and n_r tall (row ri is radius r0 + ri, column th the angle as above; no pad entries); dw_core's
    fk_annulus_draw_a8 tints it. Its manifest entry and assets_meta.h rows are a strip's (NT, R0, NR).
  - Atlas: one RGB565A8 image (or A8: alpha only, the face tints it); its cells (x, y, w, h, ox, oy, adv; a wrapped
    atlas has several lines of cells) and glyph string go into assets_meta.h.
  - crop and radial rows (Task 13) pack as bins; geometry.r4 (#4's lit roundel boxes) goes into assets_meta.h.
  - layers and drawn rows (Task 14) pack as bins; a row's origin (where a cropped bin sits on the screen) goes into
    assets_meta.h as ASSET_<NAME>_X and _Y.
  - shared_ui rows (rim keyboard Task 8) are the keyboard's shared assets, committed bytes that tools/kb_assets.py copy
    writes (a byte-equal copy of firmware/components/watch_ui/assets' file): checked by size and whole-file CRC-32,
    never written here, and named in watch_ui/kb_meta.h rather than dw/assets_meta.h.
  - gen rows (wave 2 Task 13) are a generator's output (the row's "gen", tools/dw_vortex.py, run by dw_assets.sh before
    the pack): an RGB565 bin from its PNG (the round trip as any bin), or cf DAT, a table packed as it is (file
    <asset>.dat, an even length; exempt from the PNG round trip, compared by regenerating it in dw_assets.sh --check).
The drawn-text guard: any "glyphs" or "text" field matching the trademark pattern (tools/dw_terms.py's, private;
public release Task 5) fails. In a public tree (tools/terms.py), where that module is absent, the guard refuses the
tests' neutral marker alone; anywhere else a missing module stops the packer.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facepack  # noqa: E402  (the shared formats: PNG codec, bins, strips, atlas checks, manifest, round trip, budget)
import terms  # noqa: E402  (the private term modules; a public tree has none)
from facepack import (CF, PackError, atlas_problems, committed_problems, entry_of, entry_problems,  # noqa: E402
                      pack_bin, png_read, unwrap)

NAME_RULE = facepack.NAME_RULE('dw_')     # dw_ + at most 12 characters (R121, D12)
# spec section 14: the show's name in any spelling, the time machine's name, the broadcaster and the sign's word (D9):
# tools/dw_terms.py's FORBIDDEN_TEXT; in a public tree only the tests' neutral marker
try:
    _DT = terms.load(os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')), 'dw_terms')
except terms.TermsMissing as _e:
    sys.exit('dw_pack: %s' % _e)
FORBIDDEN = _DT.FORBIDDEN_TEXT if _DT else re.compile(re.escape(terms.NEUTRAL_MARK), re.I)


# ------------------------------------------------------------------ the text guard
def text_problems(row):
    out = []
    for k in ('glyphs', 'text'):
        v = row.get(k)
        if isinstance(v, str) and FORBIDDEN.search(v):
            out.append('%s: a forbidden word in its %s field' % (row.get('asset', '?'), k))
    return out


# ------------------------------------------------------------------ table and manifest
def load_table(path):
    t = json.load(open(path))
    for r in t['rows']:
        name, want = r['asset'], r['asset'] + {'PCM': '.pcm', 'DAT': '.dat'}.get(r.get('cf'), '.bin')
        if r.get('file') != want:
            raise PackError('%s: file %r, want %r' % (name, r.get('file'), want))
        if r.get('out') == 'shared_ui':   # the keyboard's shared assets (rim keyboard Task 8): committed bytes
            if r.get('cf') not in ('RAW', 'PCM', 'RGB565A8') or 'crc32' not in r or 'size' not in r:
                raise PackError('%s: a shared_ui row is RAW, PCM or RGB565A8 with its crc32 and size' % name)
            continue
        if 'crc32' not in r and not NAME_RULE.fullmatch(name):
            raise PackError('%s: names are dw_ + at most %d characters' % (name, facepack.NAME_MAX))
        if r.get('out') not in ('bin', 'strip', 'atlas', 'generic', 'thumb', 'crop', 'radial', 'layers', 'drawn', 'gen'):
            raise PackError('%s: out %r' % (name, r.get('out')))
        if r.get('cf') not in CF and not (r.get('cf') == 'DAT' and r.get('out') == 'gen'):
            raise PackError('%s: cf %r' % (name, r.get('cf')))   # DAT: a generated table, only on a gen row
        for m in text_problems(r):
            raise PackError(m)
    return t


def build_one(row, outdir, exported):
    """(bytes, manifest extras) for one non-crc32 row, from outdir/<asset>.png and the export's record."""
    name = row['asset']
    if row['out'] == 'gen' and row['cf'] == 'DAT':   # a generated table (tools/dw_vortex.py gen), packed as it is
        p = os.path.join(outdir, row['file'])
        if not os.path.isfile(p):
            raise PackError('%s: no %s (run %s gen)' % (name, p, row.get('gen', 'its generator')))
        return open(p, 'rb').read(), {}
    png = os.path.join(outdir, name + '.png')
    if not os.path.isfile(png):
        raise PackError('%s: no %s (run tools/dw_export.mjs)' % (name, png))
    w, h, rows = png_read(png)
    ex = exported.get(name, {})
    if row['out'] == 'strip':
        for m in text_problems(ex):
            raise PackError(m)
        return unwrap(w, h, rows, row.get('scale', 1), row.get('cx', 233), row.get('cy', 233), row['r0'], row['r1'],
                      row['n_theta'], row['cf'] == 'A8'), {}
    if (w, h) != (row['w'], row['h']):
        raise PackError('%s: the PNG is %dx%d, the row says %dx%d' % (name, w, h, row['w'], row['h']))
    extra = {}
    if row['out'] == 'atlas':
        for m in text_problems(ex) + text_problems(row):
            raise PackError(m)
        probs = atlas_problems(w, h, rows, ex.get('cells', []), ex.get('glyphs', ''))
        if probs:
            raise PackError('%s: %s' % (name, '; '.join(probs)))
        extra = {'cells': ex['cells'], 'glyphs': ex['glyphs'], 'line_h': ex.get('line_h', h)}
    return pack_bin(w, h, rows, row['cf'], name), extra


def cmd_pack(a):
    if a.overlay:
        return pack_overlay(a)
    table = load_table(a.table)
    exported = {r['asset']: r for r in json.load(open(os.path.join(a.out, 'layers.json')))['rows']}
    entries = []
    for row in table['rows']:
        path = os.path.join(a.assets, row['file'])
        if row['out'] == 'shared_ui':   # the keyboard's shared assets: tools/kb_assets.py copy's bytes, checked
            probs = facepack.shared_ui_problems(path, row)
            if probs:
                raise PackError('; '.join(probs))
            entries.append(facepack.shared_ui_entry(row['asset'], row, row['src']))
            continue
        if 'crc32' in row:   # committed bytes (the brass backgrounds): checked, never rewritten
            probs = committed_problems(path, row)
            if probs:
                raise PackError('; '.join(probs))
            entries.append(entry_of(row, {}))
            continue
        data, extra = build_one(row, a.out, exported)
        if len(data) % 2:
            raise PackError('%s: %d bytes; every file is an even length' % (row['file'], len(data)))
        open(path, 'wb').write(data)
        entries.append(entry_of(row, extra))
        print('  %-16s %-9s %dx%d  %d B' % (row['file'], row['cf'], row['w'], row['h'], len(data)))
    if a.sfx:
        entries += json.load(open(a.sfx))
    facepack.write_manifest(os.path.join(a.assets, 'manifest.json'), 'tools/dw_assets.sh (tools/dw_pack.py)', entries)


REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def repo_roots():
    """Every checkout of this repo (git worktree list: this one, the main checkout, siblings) and its common dir,
    resolved physically."""
    import subprocess
    git = lambda *a: subprocess.run(['git', '-C', REPO] + list(a), capture_output=True, text=True).stdout
    roots = [REPO, git('rev-parse', '--path-format=absolute', '--git-common-dir').strip()]
    roots += [l[9:] for l in git('worktree', 'list', '--porcelain').splitlines() if l.startswith('worktree ')]
    return [os.path.realpath(r) for r in roots if r and os.path.exists(r)]


def overlay_dir(art, sub):
    """ART/sub, refused if it is a symlink or resolves inside any checkout of this repo (review N1): the overlay's
    writes must never land in the main repo."""
    p = os.path.join(art, sub)
    if os.path.islink(p):
        raise PackError('the overlay\'s %s/ must not be a symlink (%s)' % (sub, p))
    real = os.path.realpath(p)
    for r in repo_roots():
        if real == r or real.startswith(r + os.sep):
            raise PackError('the overlay\'s %s/ resolves inside a checkout of this repo (%s is under %s)' % (sub, real, r))
    return p


def overlay_rows(art, generic):
    """The overlay's rows, each checked against its generic namesake (name, cf, w, h) and overlay.json: the rows are
    distinct and a subset of its replaces (public release Task 9: the files without a row are overlay_verbatim's)."""
    ov = json.load(open(os.path.join(art, 'overlay.json')))
    t = load_table(os.path.join(art, ov.get('layers', 'layers.json')))
    gman = {e['file']: e for e in json.load(open(os.path.join(generic, 'manifest.json')))['assets']}
    names = [r['file'] for r in t['rows']]
    reps = ov.get('replaces', [])
    if not isinstance(reps, list) or not set(names) <= set(reps) or len(set(names)) != len(names):
        raise PackError('overlay: layers.json rows %s are not a subset of overlay.json\'s replaces %s' % (names, reps))
    for r in t['rows']:
        g = gman.get(r['file'])
        if g is None or g['out'] != 'generic':
            raise PackError('overlay: %s has no generic namesake in %s' % (r['file'], generic))
        if (r['cf'], r['w'], r['h']) != (g['cf'], g['w'], g['h']):
            raise PackError('overlay: %s is %s %dx%d, its generic namesake %s %dx%d' % (r['file'], r['cf'], r['w'],
                                                                                    r['h'], g['cf'], g['w'], g['h']))
    return t['rows']


OVERLAY_PCM_MAX = 110080   # a swapped sound's size: the chime's slot, DW_CHIME_CHIME_MAX_MS 3 440 ms at 16 kHz s16le (C2)


def overlay_verbatim_names(art):
    """The overlay's replaces that no layers.json row draws: files the overlay carries verbatim (public release
    Task 9: the thumbnails and the chime as they were)."""
    ov = json.load(open(os.path.join(art, 'overlay.json')))
    t = load_table(os.path.join(art, ov.get('layers', 'layers.json')))
    rows = {r['file'] for r in t['rows']}
    return [n for n in ov.get('replaces', []) if n not in rows]


def overlay_verbatim(art, generic):
    """The problems of the overlay's verbatim files (overlay_verbatim_names), each against its generic twin in
    `generic`: a .bin by its LVGL header (magic, cf, w, h, stride) and size, as main/dw_stage.cmake stages it; a .pcm
    an even size of 2 to OVERLAY_PCM_MAX bytes (C2). Empty when every one is sound."""
    out = []
    for n in overlay_verbatim_names(art):
        p, g = os.path.join(art, 'assets', n), os.path.join(generic, n)
        if not os.path.isfile(p):
            out.append('%s: missing from the overlay\'s assets/' % n)
        elif not os.path.isfile(g):
            out.append('%s: no generic twin in %s' % (n, generic))
        elif n.endswith('.pcm'):
            size = os.path.getsize(p)
            if size < 2 or size > OVERLAY_PCM_MAX or size % 2:
                out.append('%s: %d B; a swapped sound is an even 2 to %d B' % (n, size, OVERLAY_PCM_MAX))
        elif n.endswith('.bin'):
            o, gb = open(p, 'rb').read(), open(g, 'rb').read()
            if len(o) != len(gb) or o[:2] != gb[:2] or o[4:10] != gb[4:10]:
                out.append('%s: not its generic twin\'s LVGL header (magic, cf, w, h, stride) and size' % n)
        else:
            out.append('%s: a verbatim swap file is a .bin or a .pcm' % n)
    return out


def pack_overlay(a):
    rows = overlay_rows(a.overlay, a.generic)
    exported = {}
    lj = os.path.join(a.out, 'layers.json')
    if os.path.isfile(lj):
        exported = {r['asset']: r for r in json.load(open(lj))['rows']}
    overlay_dir(a.overlay, 'out')
    dst = overlay_dir(a.overlay, 'assets')
    os.makedirs(dst, exist_ok=True)
    for r in rows:
        data, _ = build_one(r, a.out, exported)
        target = os.path.join(dst, r['file'])
        if os.path.islink(target):
            raise PackError('%s is a symlink: refused (N1)' % target)
        open(target, 'wb').write(data)
        print('  overlay %-16s %-9s %dx%d  %d B' % (r['file'], r['cf'], r['w'], r['h'], len(data)))


# ------------------------------------------------------------------ the round trip (facepack.round_trip)
def cmd_check(a):
    bad = []
    if a.overlay:
        try:
            rows = overlay_rows(a.overlay, a.generic)
        except (PackError, OSError, ValueError, KeyError) as x:
            print('OVERLAY BAD', x)
            return 1
        try:
            assets, pngdir = overlay_dir(a.overlay, 'assets'), overlay_dir(a.overlay, 'out')
        except PackError as x:
            print('OVERLAY BAD', x)
            return 1
        try:
            verbatim = overlay_verbatim_names(a.overlay)
            bad += overlay_verbatim(a.overlay, a.generic)
        except (PackError, OSError, ValueError, KeyError) as x:
            print('OVERLAY BAD', x)
            return 1
        for r in rows:
            p = os.path.join(assets, r['file'])
            if not os.path.isfile(p):
                bad.append('%s: missing' % r['file'])
                continue
            e = dict(r, name=r['asset'])
            g = open(os.path.join(a.generic, r['file']), 'rb').read()
            o = open(p, 'rb').read()
            if len(o) != len(g) or o[:10] != g[:10]:
                bad.append('%s: not its generic namesake\'s cf, size and length' % r['file'])
                continue
            bad += entry_problems(e, p, pngdir)
        n = len({r['file'] for r in rows} | set(verbatim))
        for f in sorted(set(os.listdir(assets)) - {r['file'] for r in rows} - set(verbatim)):
            bad.append('%s: not in the manifest' % f)
    else:
        assets = a.assets
        bad, n = facepack.round_trip(assets, a.out)
    for b in bad:
        print('ASSETS BAD', b)
    if bad:
        return 1
    print('ASSETS OK %d (%s)' % (n, assets))
    return 0


def cmd_rerender(a):
    """G1: the committed generic files equal a fresh render, byte for byte."""
    man = json.load(open(os.path.join(a.assets, 'manifest.json')))['assets']
    bad, n = [], 0
    for e in man:
        if e.get('out') != 'generic':
            continue
        n += 1
        data, _ = build_one(dict(e, asset=e['name']), a.out, {})
        if data != open(os.path.join(a.assets, e['file']), 'rb').read():
            bad.append(e['file'])
    for f in bad:
        print('RE-RENDER BAD %s: the committed bytes are not dw_generic.js\'s drawing' % f)
    if bad or not n:
        if not n:
            print('RE-RENDER BAD: no generic rows in the manifest')
        return 1
    print('RE-RENDER OK %d (the committed generic files are dw_generic.js\'s drawing, byte for byte)' % n)
    return 0


def cmd_budget(a):
    rc, used, size, lines = facepack.budget(a.assets, a.partitions, a.warn, a.fail)
    print('  dw assets: %d files, %d B (fail above %d, warn above %d; the partition is %d B)' % (
        len(os.listdir(a.assets)), used, a.fail, a.warn, size))
    for line in lines:
        print(line)
    if not rc and used > a.warn:
        print('  WARN: %d B is above the wave-2 line of %d B' % (used, a.warn))
    return rc


# ------------------------------------------------------------------ assets_meta.h
def cmd_meta(a):
    man = json.load(open(os.path.join(a.assets, 'manifest.json')))['assets']
    geo = json.load(open(a.table)).get('geometry', {})
    pre = ['/* generated by tools/dw_assets.sh (tools/dw_pack.py meta) from firmware/apps/dw/assets/manifest.json and',
           ' * tools/export/dw_layers.json -- do not edit. The dw assets: names, sizes, the #5 pane and lens geometry',
           ' * shared by both art sets, and the atlases\' cells (dw wave 1 Task 9; in dw_core, B8). */']
    L = []
    b5 = geo.get('b5')
    if b5:
        L += ['/* #5: the 2 x 3 panes per window and the lantern lens, where dw_b5_pane and dw_b5_lamp sit in both art sets',
              ' * (%s; dw_generic.js draws its front from these, A15/D6). */' % b5['src']]
        for k, v in (('LENS', b5['lens']), ('SIGN', b5['sign'])):
            L += ['#define DW_B5_%s_%s %d' % (k, s, x) for s, x in zip(('X', 'Y', 'W', 'H'), v)]
        L.append('#define DW_B5_PANE_N %d' % len(b5['panes']))
        L.append('static const int16_t DW_B5_PANES[%d][4] DW_META_UNUSED = {' % len(b5['panes']))
        L += ['    {%d, %d, %d, %d},' % tuple(p) for p in b5['panes']]
        L += ['};', '']
    r4 = geo.get('r4')
    if r4:
        L += ['/* #4: the lit roundels\' boxes (dw_r4_lit\'s cells, DW_R4_LIT_CELL square), hour 1 first (%s). */' % r4['src'],
              '#define DW_R4_LIT_CELL %d' % r4['cell'],
              'static const int16_t DW_R4_LIT_AT[12][2] DW_META_UNUSED = {']
        L += ['    {%d, %d},' % tuple(p) for p in r4['at']]
        L += ['};', '']
    facepack.write_assets_meta(a.header, 'dw', man, pre, L)
    print('  %s: %d assets' % (os.path.relpath(a.header), len(man)))


# ------------------------------------------------------------------ self-test
def selftest():
    import tempfile
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)
            print('SELFTEST FAIL', what)
    with tempfile.TemporaryDirectory() as td:
        # the format tests (PNG, bins, strips, atlas cells) are facepack's: python tools/facepack.py selftest
        # 4a. gen rows (wave 2 Task 13): a DAT table only on a gen row, named .dat, an even length, no PNG round trip
        def table_with(row):
            tp = os.path.join(td, 't.json')
            open(tp, 'w').write(json.dumps({'rows': [row]}))
            return load_table(tp)
        gr = {'asset': 'dw_x_lut', 'file': 'dw_x_lut.dat', 'w': 2, 'h': 2, 'cf': 'DAT', 'out': 'gen'}
        ok(len(table_with(gr)['rows']) == 1, 'a gen DAT row loads')
        for bad_row, what in ((dict(gr, file='dw_x_lut.bin'), 'a DAT row named .bin'), (dict(gr, out='bin'), 'DAT on a bin row'),
                              (dict(gr, out='gen2'), 'an unknown out')):
            try:
                table_with(bad_row)
                ok(False, '%s is refused' % what)
            except PackError:
                pass
        open(os.path.join(td, 'dw_x_lut.dat'), 'wb').write(b'DWL1\x02\x00\x02\x00' + bytes(12))
        data, _ = build_one(gr, td, {})
        ok(len(data) == 20, 'a gen DAT row packs its file as it is')
        e = dict(gr, name='dw_x_lut')
        ok(entry_problems(e, os.path.join(td, 'dw_x_lut.dat'), td) == [], 'a DAT entry needs no PNG')
        open(os.path.join(td, 'dw_x_lut.dat'), 'wb').write(bytes(21))
        ok(entry_problems(e, os.path.join(td, 'dw_x_lut.dat'), td) != [], 'an odd DAT file is caught')

        # 5. the overlay (public release Task 9): layer rows a subset of replaces; the rest verbatim (.bin by header and
        # size, .pcm an even 2..110 080 B), each with a generic twin
        gen, art = os.path.join(td, 'gen'), os.path.join(td, 'art')
        os.makedirs(gen)
        os.makedirs(os.path.join(art, 'assets'))
        th = b'\x19\x14\x00\x00\x44\x00\x44\x00\x88\x00\x00\x00' + bytes(68 * 68 * 3)
        man = [{'file': 'dw_doors_m.bin', 'out': 'generic', 'cf': 'RGB565', 'w': 4, 'h': 2},
               {'file': 'dw_th_b5.bin', 'out': 'bin', 'cf': 'RGB565A8', 'w': 68, 'h': 68},
               {'file': 'dw_chime.pcm', 'out': 'pcm'}]
        json.dump({'assets': man}, open(os.path.join(gen, 'manifest.json'), 'w'))
        open(os.path.join(gen, 'dw_th_b5.bin'), 'wb').write(th)
        open(os.path.join(gen, 'dw_chime.pcm'), 'wb').write(bytes(64))
        row = {'asset': 'dw_doors_m', 'file': 'dw_doors_m.bin', 'face': 'x', 'fn': 'x', 'w': 4, 'h': 2, 'cf': 'RGB565',
               'out': 'bin'}
        json.dump({'rows': [row]}, open(os.path.join(art, 'layers.json'), 'w'))

        def ov(replaces, files):
            json.dump({'theme': 'dw', 'replaces': replaces, 'layers': 'layers.json'},
                      open(os.path.join(art, 'over' + 'lay.json'), 'w'))
            for f in os.listdir(os.path.join(art, 'assets')):
                os.remove(os.path.join(art, 'assets', f))
            for n, data in files.items():
                open(os.path.join(art, 'assets', n), 'wb').write(data)
        three = ['dw_doors_m.bin', 'dw_th_b5.bin', 'dw_chime.pcm']
        ov(three, {'dw_th_b5.bin': th[:-1] + b'\x01', 'dw_chime.pcm': bytes(102400)})
        try:
            ok([r['file'] for r in overlay_rows(art, gen)] == ['dw_doors_m.bin'], 'layer rows a subset of replaces')
            ok(overlay_verbatim(art, gen) == [], 'a thumbnail and a chime verbatim: %s' % overlay_verbatim(art, gen))
            for files, what in (({'dw_th_b5.bin': th[:4] + b'\x43' + th[5:], 'dw_chime.pcm': bytes(64)},
                                 'a 67-wide thumbnail'),
                                ({'dw_th_b5.bin': th + b'\x00\x00', 'dw_chime.pcm': bytes(64)}, 'a longer thumbnail'),
                                ({'dw_th_b5.bin': th, 'dw_chime.pcm': bytes(110082)}, 'a 110 082 B chime'),
                                ({'dw_th_b5.bin': th, 'dw_chime.pcm': bytes(65)}, 'an odd chime'),
                                ({'dw_th_b5.bin': th}, 'a missing chime')):
                ov(three, files)
                probs = overlay_verbatim(art, gen)
                ok(len(probs) == 1, '%s is caught once: %s' % (what, probs))
        except (PackError, NameError) as x:
            ok(False, 'the overlay rows and verbatim files: %s' % x)
        ov(['dw_th_b5.bin'], {'dw_th_b5.bin': th})
        try:
            overlay_rows(art, gen)
            ok(False, 'a layer row outside replaces is refused')
        except PackError:
            pass

        # 4. the drawn-text guard on glyphs and text
        for word in (_DT.pack_words() if _DT else (terms.NEUTRAL_MARK, 'x ' + terms.NEUTRAL_MARK.upper())):
            ok(text_problems({'asset': 'dw_x', 'glyphs': word}) != [], 'the guard catches %r' % word)
        ok(text_problems({'asset': 'dw_x', 'text': 'call box 10:09'}) == [], 'the guard passes "call box"')
    print('dw_pack selftest: %s' % ('FAIL (%d)' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('selftest')
    p = sub.add_parser('pack')
    p.add_argument('--table')
    p.add_argument('--out', required=True)
    p.add_argument('--assets')
    p.add_argument('--sfx')
    p.add_argument('--overlay')
    p.add_argument('--generic')
    p = sub.add_parser('check')
    p.add_argument('--assets')
    p.add_argument('--out')
    p.add_argument('--overlay')
    p.add_argument('--generic')
    p = sub.add_parser('rerender')
    p.add_argument('--out', required=True)
    p.add_argument('--assets', required=True)
    p = sub.add_parser('budget')
    p.add_argument('--assets', required=True)
    p.add_argument('--partitions', required=True)
    p.add_argument('--fail', type=int, default=12255232)
    p.add_argument('--warn', type=int, default=11600000)   # the wave-2 line with Vortex (wave 2 Task 14; 10 900 000 before)
    p = sub.add_parser('meta')
    p.add_argument('--assets', required=True)
    p.add_argument('--table', required=True)
    p.add_argument('--header', required=True)
    a = ap.parse_args()
    try:
        if a.cmd == 'selftest':
            return selftest()
        return {'pack': cmd_pack, 'check': cmd_check, 'rerender': cmd_rerender, 'budget': cmd_budget,
                'meta': cmd_meta}[a.cmd](a) or 0
    except PackError as x:
        print('dw_pack: %s' % x, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
