#!/usr/bin/env python3
"""The Mining Ship asset packer (rd wave 1 Task 7; spec section 12): LVGL v9 bins, A8 strips, the shared glyph atlases
(one plane, or the two-plane ATL2 of PR29) and the thumbnails, from what tools/rd_export.mjs writes; the assets
manifest, the generated rd/assets_meta.h, the budget and the round trip. The formats are tools/facepack.py's (shared
with dw); this file keeps rd's rows, names, advances and budget. Standard library only; its self-test runs under any
Python, ctest's included.

  rd_pack.py selftest
  rd_pack.py pack --table T --out O --assets A [--sfx J]   every row of T (tools/export/rd_layers.json) from O's
                                                           PNGs and O/layers.json into A, and A/manifest.json
  rd_pack.py meta --assets A --header H                    H: ASSET_* names and sizes, the atlases' cells
  rd_pack.py budget --assets A --partitions CSV --table T  fail above 9 000 000 B, warn above 7 000 000 B, report
                    [--fail N] [--warn N] [--report N]     above the wave-2 line of 6 000 000 B; a face's own files
                    [--face-cap N]                         (each row names its face, or "shared") fail above 500 000 B
  rd_pack.py check --assets A --out O                      the round trip: every file decoded against its PNG, every
                                                           manifest file present and nothing else in A

The table's rows: {name, face, kind, source, ...}. face is a key of rd/faces.h's ids (stasis ... mopbot) or "shared";
kind is one of KINDS (below); the file is <name>.bin; names are rd_ plus at most 12 characters, except committed bytes
(a crc32 row: the brass backgrounds, the Omni's own files, R112), which keep their names and are checked, never written.
  - shared_ui (rim keyboard Task 8): the keyboard's shared assets, committed bytes that tools/kb_assets.py copy writes
    (a byte-equal copy of firmware/components/watch_ui/assets' file; the row names its file, cf RAW, PCM or RGB565A8,
    size and whole-file crc32), checked and never written; named in watch_ui/kb_meta.h, not in rd/assets_meta.h.
  - atlas: an A8 atlas from the export's glyph images (cut to their ink), packed by facepack.build_atlas; "advance":
    "time" gives the digits one fixed advance (the mockups' timeMetrics: the widest digit x 0.96, rounded), the colon
    its own (its width x 1.05) and the dash 0.66 x the digit advance (the shared language's unset rule), each glyph
    centred in its advance as drawTime does; any other glyph (and "advance": "natural") takes its own width. Tracking is
    the face's, at draw (fk_atlas_text_tracked).
  - atlas2: the same with two planes per glyph (glow and core, one cell table; ATL2).
  - a8, rgb565, rgb565a8: an LVGL bin of <name>.png; a8_strip: an A8 strip of it (r0, r1, n_theta, cx, cy); thumb: a
    68 px RGB565A8 bin.
  - a8_rows (rd wave 2 Task 12): <name>.png's alpha as ARW1 (tools/facepack.py: each row cut to its extent), w x h as
    the row says (Curry Clock's steam loop, rd_cu_wisp).
  - max (rd wave 2 Task 3; an rgb565 row): the packed layer's largest channel, each RGB565 channel widened to 8 bits as
    the host harness widens it (c5 x 255 / 31, g6 x 255 / 63, floored), joins the manifest entry and rd/assets_meta.h
    as ASSET_<NAME>_MAX (Bay Mouth's star band, Drift Plate's tile: their dimming tests).
The drawn-text guard: a row's glyphs or text field matching tools/rd_terms.py's list is refused (spec section 13.2), as
is a glyph string the export reports. That module is private (public release Task 5, found through tools/terms.py): in
a public tree, where it is absent, the guard refuses the tests' neutral marker alone; anywhere else a missing module
stops the packer.
"""
import argparse
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facepack  # noqa: E402  (the shared formats)
from facepack import PackError  # noqa: E402
import terms  # noqa: E402  (the private term modules; a public tree has none)

REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
try:
    _RT = terms.load(REPO, 'rd_terms')   # the list, built from pieces there; the tests' marker too
except terms.TermsMissing as _e:
    sys.exit('rd_pack: %s' % _e)


def text_forbidden(s):
    """True when s holds a term of rd's list (or the tests' marker); in a public tree, only the neutral marker."""
    if _RT is None:
        return terms.NEUTRAL_MARK in s.lower()
    return _RT.text_forbidden(s)
FACES_H = os.path.join(REPO, 'firmware', 'apps', 'rd', 'components', 'rd_core', 'include', 'rd', 'faces.h')
NAME_RULE = facepack.NAME_RULE('rd_')
# kind: (cf, the manifest's out)
KINDS = {'rgb565': ('RGB565', 'bin'), 'rgb565a8': ('RGB565A8', 'bin'), 'a8': ('A8', 'bin'), 'a8_strip': ('A8', 'strip'),
         'atlas': ('A8', 'atlas'), 'atlas2': ('A8', 'atlas2'), 'thumb': ('RGB565A8', 'thumb'),
         'a8_rows': ('A8', 'rows'),
         'shared_ui': (None, 'shared_ui')}   # the keyboard's shared assets (rim keyboard Task 8): cf from the row
TEXT_FIELDS = ('glyphs', 'text')
# Each face's file prefix (rd_<prefix>_...): the spec's names (section 9.3, 10: st, by, dp, sc, hp, vm, cu, cb, bk) and,
# where it names none, ours (tm, dk, dc, mb, dg). A face's rows must carry it, a shared row must not, and the budget
# counts every file carrying it against that face's cap, listed in the table or not (M4).
FACE_PREFIX = {'stasis': 'st', 'terminal': 'tm', 'deck': 'dk', 'hull': 'hp', 'bunk': 'bk', 'curry': 'cu',
               'digital': 'dg', 'bay': 'by', 'drift': 'dp', 'console': 'dc', 'scope': 'sc', 'vending': 'vm',
               'bowl': 'cb', 'mopbot': 'mb'}
RESERVE = 256 * 1024     # R69: the budget's fail line stays at or below the assets partition less 256 KiB
LICENCE_NOTE = ('The rd_f_* atlases are rendered from SIL Open Font License 1.1 fonts (Saira Stencil One, VT323, Chakra '
                'Petch); their licences and copyright notices are tools/font_licences/*-OFL.txt in this repository and '
                'go with any distributed image.')
TIME_DIGITS = '0123456789'
DASH_K = 0.66            # the dash advance over the digit advance (the shared language, spec section 10.0)


def face_keys(path=FACES_H):
    """rd/faces.h's face ids as keys (RD_F_STASIS -> stasis), the counts excluded."""
    body = re.search(r'enum rd_face\s*\{(.*?)\}', open(path).read(), re.S)
    if not body:
        raise PackError('%s: no enum rd_face' % path)
    return {m.lower() for m in re.findall(r'\bRD_F_([A-Z]+)\b', body.group(1))}


# ------------------------------------------------------------------ the table and the text guard
def text_problems(obj, where='?'):
    """Every glyphs or text field (at any depth: an atlas's extra parts too) matching the list."""
    out = []
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in TEXT_FIELDS and isinstance(v, str) and text_forbidden(v):
                out.append('%s: a forbidden term in its %s field (tools/rd_terms.py)' % (where, k))
            elif isinstance(v, (dict, list)):
                out += text_problems(v, where)
    elif isinstance(obj, list):
        for v in obj:
            out += text_problems(v, where)
    return out


def face_of_name(name):
    """The face whose prefix the file or asset name carries (rd_st_lid.bin -> stasis), or None."""
    for face, p in FACE_PREFIX.items():
        if name.startswith('rd_%s_' % p):
            return face
    return None


def load_table(path, keys=None):
    t = json.load(open(path))
    keys = face_keys() if keys is None else keys
    seen = set()
    for r in t.get('rows', []):
        name = r.get('name', '?')
        for m in text_problems(r, name):
            raise PackError(m)
        if r.get('kind') not in KINDS:
            raise PackError('%s: kind %r (one of %s)' % (name, r.get('kind'), ', '.join(KINDS)))
        if r.get('face') not in keys | {'shared'}:
            raise PackError('%s: face %r is neither a key of rd/faces.h nor "shared"' % (name, r.get('face')))
        if not r.get('source'):
            raise PackError('%s: cites no source' % name)
        if 'crc32' not in r and not NAME_RULE.fullmatch(name):
            raise PackError('%s: names are rd_ + at most %d characters' % (name, facepack.NAME_MAX))
        pre = face_of_name(name)
        if r['face'] != 'shared' and pre != r['face']:
            raise PackError('%s: face %s\'s files are named rd_%s_...' % (name, r['face'], FACE_PREFIX.get(r['face'])))
        if r['face'] == 'shared' and pre:
            raise PackError('%s: a shared row carries face %s\'s prefix' % (name, pre))
        if name in seen:
            raise PackError('%s: two rows' % name)
        seen.add(name)
        if r['kind'] == 'shared_ui':   # committed bytes (tools/kb_assets.py copy), named in watch_ui/kb_meta.h
            want = name + ('.pcm' if r.get('cf') == 'PCM' else '.bin')
            if r.get('file') != want or r.get('cf') not in ('RAW', 'PCM', 'RGB565A8') or 'crc32' not in r or \
                    'size' not in r or r['face'] != 'shared':
                raise PackError('%s: a shared_ui row is a shared RAW, PCM or RGB565A8 file %s with its crc32 and size'
                                % (name, want))
            continue
        r['file'] = name + '.bin'
    return t


def entry(row, cf, out, w, h, extra=None):
    e = {'name': row['name'], 'file': row['file'], 'cf': cf, 'w': w, 'h': h, 'out': out, 'face': row['face'],
         'src': row['source']}
    for k in ('crc32', 'r0', 'r1', 'n_theta', 'cx', 'cy', 'origin'):
        if k in row:
            e[k] = row[k]
    e.update(extra or {})
    if out == 'strip':
        e['n_r'] = int(math.ceil(e['r1'] - e['r0']))
    return e


# ------------------------------------------------------------------ atlases
def merged_trim(planes, ox, oy):
    """Glyph images of one size (one per plane) cut to the union of their ink: (w, h, [rows per plane], ox, oy)."""
    w, h = planes[0][0], planes[0][1]
    if any((p[0], p[1]) != (w, h) for p in planes):
        raise PackError('the planes of one glyph differ in size')
    merged = [bytearray(w * 4) for _ in range(h)]
    for _, _, rows in planes:
        for y in range(h):
            for x in range(w):
                a = rows[y][4 * x + 3]
                if a > merged[y][4 * x + 3]:
                    merged[y][4 * x + 3] = a
    tw, th, _, tox, toy = facepack.glyph_trim(w, h, merged, ox, oy)
    x0, y0 = tox - ox, toy - oy
    return tw, th, [[bytearray(rows[y][4 * x0:4 * (x0 + tw)]) for y in range(y0, y0 + th)] for _, _, rows in planes], \
        tox, toy


def advances(row, rec, glyphs, own):
    """(fixed_advance, advance_override) for an atlas row: "time" (the digits fixed, the colon and the dash their own,
    any other glyph its own width) or "natural" (every glyph its own width)."""
    rule = row.get('advance', 'natural')
    if rule == 'natural':
        return None, {}
    if rule != 'time':
        raise PackError('%s: advance %r (time or natural)' % (row['name'], rule))
    if 'digit_adv' not in rec or 'colon_adv' not in rec:
        raise PackError('%s: the export gave no time metrics' % row['name'])
    fixed = int(round(rec['digit_adv']))
    over = {ch: int(round(a)) for ch, a in zip(glyphs, own) if ch not in TIME_DIGITS + ':-'}
    over[':'] = int(round(rec['colon_adv']))
    over['-'] = int(round(DASH_K * fixed))
    return fixed, over


def build_atlas_row(row, rec, outdir, planes):
    """(w, h, [plane rows], cells, glyphs, line_h) for an atlas (planes 1) or atlas2 (planes 2) row."""
    glyphs = rec.get('glyphs', '')
    for m in text_problems({'glyphs': glyphs}, row['name']):
        raise PackError(m)
    gl = rec.get('cells', [])
    if len(gl) != len(glyphs) or not glyphs:
        raise PackError('%s: the export gave %d glyph images for %d glyphs' % (row['name'], len(gl), len(glyphs)))
    imgs = [[] for _ in range(planes)]
    own = []
    for g in gl:
        files = [g['png']] if planes == 1 else [g['glow'], g['core']]
        pl = [facepack.png_read(os.path.join(outdir, f)) for f in files]
        tw, th, trows, tox, toy = merged_trim(pl, g['ox'], g['oy'])
        for k in range(planes):
            imgs[k].append((tw, th, trows[k], tox, toy, g['adv']))
        own.append(g['adv'])
    fixed, over = advances(row, rec, glyphs, own)
    kw = dict(fixed_advance=fixed, advance_override=over, maxw=row.get('maxw', 512), pad=1, centre=True, even=True)
    if planes == 1:
        w, h, rows, cells = facepack.build_atlas(imgs[0], glyphs, **kw)
        prow = [rows]
    else:
        w, h, glow, core, cells = facepack.build_atlas2(imgs[0], imgs[1], glyphs, **kw)
        prow = [glow, core]
    inked = [c for c in cells if c['h']]
    line_h = max(-c['oy'] for c in inked) + max(c['h'] + c['oy'] for c in inked)
    return w, h, prow, cells, glyphs, line_h


def max_channel(data):
    """The largest channel of an RGB565 LVGL bin (its 12-byte header, then the pixels), each channel widened to 8 bits
    by the host harness's rule: c5 x 255 // 31, g6 x 255 // 63."""
    m = 0
    for i in range(12, len(data) - 1, 2):
        v = data[i] | data[i + 1] << 8
        m = max(m, (v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    return m


def build_one(row, outdir, exported):
    """(bytes, manifest entry) for one row that is not committed bytes."""
    name, kind = row['name'], row['kind']
    cf, out = KINDS[kind]
    rec = exported.get(name)
    if rec is None:
        raise PackError('%s: the export has no record of it (run tools/rd_export.mjs)' % name)
    for m in text_problems(rec, name):
        raise PackError(m)
    if kind in ('atlas', 'atlas2'):
        planes = 1 if kind == 'atlas' else 2
        w, h, prow, cells, glyphs, line_h = build_atlas_row(row, rec, outdir, planes)
        png_rows = prow[0] if planes == 1 else prow[0] + prow[1]
        facepack.png_write(os.path.join(outdir, name + '.png'), w, len(png_rows), png_rows)
        extra = {'cells': cells, 'glyphs': glyphs, 'line_h': line_h}
        if planes == 1:
            data = facepack.pack_bin(w, h, prow[0], 'A8', name)
        else:
            data = facepack.pack_atlas2(w, h, prow[0], prow[1], cells, glyphs, line_h)
        return data, entry(row, cf, out, w, h, extra)
    png = os.path.join(outdir, name + '.png')
    if not os.path.isfile(png):
        raise PackError('%s: no %s (run tools/rd_export.mjs)' % (name, png))
    w, h, rows = facepack.png_read(png)
    if kind == 'a8_rows':   # rd wave 2 Task 12: ARW1, each row cut to its extent
        if (w, h) != (row.get('w'), row.get('h')):
            raise PackError('%s: the PNG is %dx%d, the row says %sx%s' % (name, w, h, row.get('w'), row.get('h')))
        return facepack.rows_pack(w, h, rows), entry(row, cf, out, w, h)
    if kind == 'a8_strip':
        data = facepack.unwrap(w, h, rows, 1, row.get('cx', 233), row.get('cy', 233), row['r0'], row['r1'],
                               row['n_theta'], True)
        return data, entry(row, cf, out, row['n_theta'], int(math.ceil(row['r1'] - row['r0'])))
    for k, v in (('w', w), ('h', h)):
        if k in row and row[k] != v:
            raise PackError('%s: the PNG is %dx%d, the row says %s %d' % (name, w, h, k, row[k]))
    data = facepack.pack_bin(w, h, rows, cf, name)
    if row.get('max'):
        if kind != 'rgb565':
            raise PackError('%s: max is an rgb565 row\'s key (its largest channel)' % name)
        return data, entry(row, cf, out, w, h, {'max': max_channel(data)})
    return data, entry(row, cf, out, w, h)


def cmd_pack(a):
    table = load_table(a.table)
    exported = {r['name']: r for r in json.load(open(os.path.join(a.out, 'layers.json')))['rows']}
    entries, staged = [], []
    for row in table['rows']:
        path = os.path.join(a.assets, row['file'])
        if row['kind'] == 'shared_ui':   # the keyboard's shared assets: tools/kb_assets.py copy's bytes, checked
            probs = facepack.shared_ui_problems(path, row)
            if probs:
                raise PackError('; '.join(probs))
            entries.append(facepack.shared_ui_entry(row['name'], row, row['source']))
            continue
        if 'crc32' in row:   # committed bytes (the brass backgrounds): checked, never rewritten
            cf = KINDS[row['kind']][0]
            e = entry(row, cf, 'bin', row['w'], row['h'])
            probs = facepack.committed_problems(path, e)
            if probs:
                raise PackError('; '.join(probs))
            entries.append(e)
            continue
        data, e = build_one(row, a.out, exported)
        if len(data) % 2:
            raise PackError('%s: %d bytes; every file is an even length (C6)' % (row['file'], len(data)))
        staged.append((path, data))
        entries.append(e)
        print('  %-18s %-6s %-9s %dx%d  %d B' % (row['file'], row['kind'], e['cf'], e['w'], e['h'], len(data)))
    for path, data in staged:    # nothing is written until every row has packed
        open(path, 'wb').write(data)
    if a.sfx:
        entries += json.load(open(a.sfx))
    facepack.write_manifest(os.path.join(a.assets, 'manifest.json'), 'tools/rd_assets.sh (tools/rd_pack.py)', entries,
                            notices=LICENCE_NOTE)


def cmd_check(a):
    bad, n = facepack.round_trip(a.assets, a.out)
    for b in bad:
        print('ASSETS BAD', b)
    if bad:
        return 1
    print('ASSETS OK %d (%s)' % (n, a.assets))
    return 0


def cmd_budget(a):
    rows = load_table(a.table)['rows']
    keys = face_keys()
    if set(FACE_PREFIX) != keys:
        raise PackError('FACE_PREFIX names %s, rd/faces.h %s' % (sorted(FACE_PREFIX), sorted(keys)))
    files = os.listdir(a.assets)
    listed = {r['file']: r['face'] for r in rows}
    # every file with a face's prefix counts against that face, listed or not (M4); a listed face row has it too
    face_of = {f: listed.get(f) if listed.get(f, 'shared') != 'shared' else (face_of_name(f) or 'shared') for f in files}
    caps = {k: a.face_cap for k in keys}
    rc, used, size, lines = facepack.budget(a.assets, a.partitions, a.warn, a.fail, caps,
                                            [{'file': f, 'face': k} for f, k in face_of.items()])
    print('  rd assets: %d files, %d B (fail above %d, warn above %d, report above %d; the partition is %d B)' % (
        len(files), used, a.fail, a.warn, a.report, size))
    if size is None or a.fail > size - RESERVE:
        lines.append('ASSETS BAD: the fail line %d B is above the assets partition (%s B) less 256 KiB (R69)'
                     % (a.fail, size))
        rc = 1
    per = {}
    for f, k in face_of.items():
        per[k] = per.get(k, 0) + os.path.getsize(os.path.join(a.assets, f))
    for k in sorted(per):
        print('    %-9s %9d B%s' % (k, per[k], '' if k == 'shared' else ' (cap %d)' % a.face_cap))
    for line in lines:
        print(line)
    if used > a.warn and used <= a.fail:
        print('  WARN: %d B is above the warn line of %d B' % (used, a.warn))
    if used > a.report:
        print('  REPORT: %d B is above the wave line of %d B (spec section 8: report it)' % (used, a.report))
    return rc


def cmd_meta(a):
    man = json.load(open(os.path.join(a.assets, 'manifest.json')))['assets']
    pre = ['/* generated by tools/rd_assets.sh (tools/rd_pack.py meta) from firmware/apps/rd/assets/manifest.json -- do not',
           ' * edit. The Mining Ship assets: names, sizes, the one-plane atlases\' glyphs and cells (rd wave 1 Task 7; spec',
           ' * section 12). A two-plane atlas (ATL2) carries its cells and glyphs in its file: rd/atlas2.h reads them. */']
    # an entry with max (an rgb565 row's largest channel, its row key max): ASSET_<NAME>_MAX after the entries
    tail = ['/* the largest channel of each layer whose row sets max (tools/rd_pack.py max_channel) */'] + \
        ['#define ASSET_%s_MAX %d' % (e['name'].upper(), e['max']) for e in man if 'max' in e] + [''] \
        if any('max' in e for e in man) else []
    n = facepack.write_assets_meta(a.header, 'rd', man, pre, tail)
    print('  %s: %d assets' % (os.path.relpath(a.header), n))


# ------------------------------------------------------------------ self-test
def selftest():
    import tempfile
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)
            print('SELFTEST FAIL', what)
    keys = face_keys()
    ok(len(keys) == 14 and {'stasis', 'digital', 'mopbot'} <= keys, 'face keys from rd/faces.h (%s)' % sorted(keys))
    with tempfile.TemporaryDirectory() as td:
        def table_with(*rows):
            p = os.path.join(td, 't.json')
            json.dump({'rows': list(rows)}, open(p, 'w'))
            return load_table(p, keys)
        good = {'name': 'rd_x', 'face': 'shared', 'kind': 'a8', 'source': 'test'}
        ok(table_with(good)['rows'][0]['file'] == 'rd_x.bin', 'a good row loads, its file <name>.bin')
        real = next(t for t, want, _ in _RT.rd_selftest_cases() if want) if _RT else terms.NEUTRAL_MARK
        ok(text_problems({'glyphs': real}, 'x') != [] and text_problems({'text': 'ok 10:09'}, 'x') == [],
           'the guard on a real term (the neutral marker in a public tree), in memory only')
        # whatever reaches a file here is the tests' neutral marker, never a real term (fix round 2)
        keep = os.environ.get('RD_TERMS_TEST_MARKER')
        os.environ['RD_TERMS_TEST_MARKER'] = sample = 'zzneutralmarkzz'
        ok(table_with(dict(good, name='rd_st_lid', face='stasis'))['rows'][0]['face'] == 'stasis', 'a face row')
        for bad, what in ((dict(good, face='lounge'), 'an unknown face'),
                          (dict(good, face='stasis'), 'a face row without its prefix'),
                          (dict(good, name='rd_cu_x'), 'a shared row with a face\'s prefix'), (dict(good, kind='png'), 'an unknown kind'),
                          (dict(good, name='rd_' + 'a' * 13), 'a long name'), (dict(good, name='dw_x'), 'another prefix'),
                          (dict(good, source=''), 'no source'), (dict(good, glyphs=sample), 'a forbidden glyph string'),
                          (dict(good, text='x ' + sample), 'a forbidden text'),
                          (dict(good, extra=[{'glyphs': sample}]), 'a forbidden glyph string in a part')):
            try:
                table_with(bad)
                ok(False, '%s is refused' % what)
            except PackError:
                pass
        try:
            table_with(good, good)
            ok(False, 'two rows of one name are refused')
        except PackError:
            pass
        ok(table_with(dict(good, name='bg_bezel', crc32='00000000'))['rows'][0]['file'] == 'bg_bezel.bin',
           'committed bytes keep their own name')

        # the time advances: digits fixed, the colon its own, the dash 0.66 x, an extra glyph its own width
        fx, over = advances({'name': 'rd_t', 'advance': 'time'}, {'digit_adv': 70.4, 'colon_adv': 29.6}, '01:-\x81',
                            [60.1, 31.0, 28.2, 40.0, 33.4])
        ok(fx == 70 and over == {':': 30, '-': 46, '\x81': 33}, 'time advances (%s, %s)' % (fx, over))
        ok(advances({'name': 'rd_n'}, {}, 'ab', [3.0, 4.0]) == (None, {}), 'natural advances')

        # an end-to-end atlas and atlas2 from synthetic glyph images: pack, the round trip, assets_meta.h
        out, assets = os.path.join(td, 'out'), os.path.join(td, 'assets')
        os.makedirs(os.path.join(out, 'g'))
        os.mkdir(assets)

        def gpng(f, w, h, ink):
            rows = [bytearray(w * 4) for _ in range(h)]
            for x, y, v in ink:
                rows[y][4 * x:4 * x + 4] = bytes((255, 255, 255, v))
            facepack.png_write(os.path.join(out, 'g', f), w, h, rows)
            return 'g/' + f
        cells1 = [{'ch': c, 'png': gpng('a%d.png' % i, 9, 12, [(2 + i, 3, 200), (4, 9, 90)]), 'ox': -3, 'oy': -10,
                   'adv': 6.0 + i} for i, c in enumerate('0:-')]
        cells2 = [{'ch': c, 'glow': gpng('g%d.png' % i, 8, 10, [(1, 1, 40), (6, 8, 30)]),
                   'core': gpng('c%d.png' % i, 8, 10, [(3, 4, 255)]), 'ox': -2, 'oy': -8, 'adv': 5.0}
                  for i, c in enumerate('A\xb7')]
        json.dump({'rows': [{'name': 'rd_t1', 'glyphs': '0:-', 'digit_adv': 8.2, 'colon_adv': 4.1, 'cells': cells1},
                            {'name': 'rd_t2', 'glyphs': 'A\xb7', 'cells': cells2}, {'name': 'rd_t3'}, {'name': 'rd_t4'}]},
                  open(os.path.join(out, 'layers.json'), 'w'))
        # wave 2 Task 3: max on an rgb565 row (the packed layer's largest channel, widened as the harness widens it)
        facepack.png_write(os.path.join(out, 'rd_t3.png'), 2, 1, [bytearray((180, 80, 31, 255, 20, 90, 200, 255))])
        facepack.png_write(os.path.join(out, 'rd_t4.png'), 2, 1, [bytearray((250, 250, 250, 255, 0, 0, 0, 255))])
        json.dump({'rows': [dict(good, name='rd_t1', kind='atlas', advance='time'),
                            dict(good, name='rd_t2', kind='atlas2'), dict(good, name='rd_t3', kind='rgb565', max=True),
                            dict(good, name='rd_t4', kind='rgb565')]}, open(os.path.join(td, 'tt.json'), 'w'))
        ns = argparse.Namespace(table=os.path.join(td, 'tt.json'), out=out, assets=assets, sfx=None)
        cmd_pack(ns)
        man = {e['name']: e for e in json.load(open(os.path.join(assets, 'manifest.json')))['assets']}
        c1 = man['rd_t1']['cells']
        ok([c['adv'] for c in c1] == [8, 4, 5], 'the atlas\'s time advances (%s)' % [c['adv'] for c in c1])
        ok(c1[0]['w'] == 3 and c1[0]['h'] == 7 and (c1[0]['ox'], c1[0]['oy']) == (-3 + 2 + 1, -10 + 3),
           'a glyph cut to its ink, its origin moved and centred (%s)' % c1[0])
        a2 = facepack.atlas2_unpack(open(os.path.join(assets, 'rd_t2.bin'), 'rb').read())
        ok(a2['cells'] == man['rd_t2']['cells'] and a2['glyphs'] == 'A\xb7' and a2['cells'][0]['w'] == 6,
           'the atlas2 cells: the union of both planes\' ink (%s)' % a2['cells'])
        ok(all(os.path.getsize(os.path.join(assets, f)) % 2 == 0 for f in os.listdir(assets) if f.endswith('.bin')),
           'every packed file even')
        ok(cmd_check(argparse.Namespace(assets=assets, out=out)) == 0, 'the round trip passes')
        hdr = os.path.join(td, 'meta.h')
        cmd_meta(argparse.Namespace(assets=assets, header=hdr))
        text = open(hdr).read()
        ok('RD_CELLS_RD_T1[3]' in text and 'RD_GLYPHS_RD_T1[] RD_META_UNUSED = "0:-"' in text and
           '#define ASSET_RD_T2_LINE_H' in text and 'RD_CELLS_RD_T2' not in text, 'assets_meta.h')
        # max: the second pixel (20, 90, 200) packs blue to 24 of 31 ((200 x 31 + 127) // 255), widened 24 x 255 // 31
        # = 197, above the first's red 180: the largest channel of the layer
        ok(man['rd_t3'].get('max') == 197 and 'max' not in man['rd_t4'], 'max joins only its row\'s entry (%s)'
           % man['rd_t3'].get('max'))
        ok(max_channel(open(os.path.join(assets, 'rd_t3.bin'), 'rb').read()) == 197, 'max_channel of the packed bin')
        ok('#define ASSET_RD_T3_MAX 197' in text and 'ASSET_RD_T4_MAX' not in text, 'ASSET_<NAME>_MAX in assets_meta.h')
        facepack.png_write(os.path.join(out, 'rd_t5.png'), 2, 1, [bytearray((255, 255, 255, 9, 255, 255, 255, 99))])
        try:
            build_one(dict(good, name='rd_t5', kind='a8', max=True, file='rd_t5.bin'), out, {'rd_t5': {}})
            ok(False, 'max on an a8 row is refused')
        except PackError as x:
            ok('max' in str(x), 'max on an a8 row is refused for its kind (%s)' % x)
        # wave 2 Task 12: an a8_rows row packs its PNG's alpha as ARW1 (rows cut to their extents) and round-trips; a
        # PNG of another size than the row's is refused
        facepack.png_write(os.path.join(out, 'rd_t6.png'), 3, 2,
                           [bytearray((255, 255, 255, 0, 255, 255, 255, 7, 255, 255, 255, 0)), bytearray(12)])
        d6, e6 = build_one(dict(good, name='rd_t6', kind='a8_rows', w=3, h=2, file='rd_t6.bin'), out, {'rd_t6': {}})
        ok(d6 == b'ARW1' + bytes((3, 0, 2, 0, 1, 2, 0, 0, 7, 0)) and e6['out'] == 'rows' and e6['cf'] == 'A8' and
           (e6['w'], e6['h']) == (3, 2), 'a8_rows packs ARW1 (%r)' % d6)
        ok(facepack.rows_problems(d6, 3, 2, facepack.png_read(os.path.join(out, 'rd_t6.png'))[2]) == [],
           'a8_rows round trip')
        try:
            build_one(dict(good, name='rd_t6', kind='a8_rows', w=3, h=3, file='rd_t6.bin'), out, {'rd_t6': {}})
            ok(False, 'an a8_rows PNG of another size is refused')
        except PackError:
            pass
        # nothing is written when a row fails: a forbidden glyph string the export reports
        lj = json.load(open(os.path.join(out, 'layers.json')))
        lj['rows'][1]['glyphs'] = sample
        json.dump(lj, open(os.path.join(out, 'layers.json'), 'w'))
        before = {f: open(os.path.join(assets, f), 'rb').read() for f in os.listdir(assets)}
        try:
            cmd_pack(ns)
            ok(False, 'a forbidden glyph string from the export is refused')
        except PackError:
            pass
        ok({f: open(os.path.join(assets, f), 'rb').read() for f in os.listdir(assets)} == before, 'nothing written')
        if keep is None:
            os.environ.pop('RD_TERMS_TEST_MARKER', None)
        else:
            os.environ['RD_TERMS_TEST_MARKER'] = keep
    print('rd_pack selftest: %s' % ('FAIL (%d)' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('selftest')
    p = sub.add_parser('pack')
    p.add_argument('--table', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--assets', required=True)
    p.add_argument('--sfx')
    p = sub.add_parser('check')
    p.add_argument('--assets', required=True)
    p.add_argument('--out', required=True)
    p = sub.add_parser('budget')
    p.add_argument('--assets', required=True)
    p.add_argument('--partitions', required=True)
    p.add_argument('--table', required=True)
    p.add_argument('--fail', type=int, default=9000000)
    p.add_argument('--warn', type=int, default=7000000)
    p.add_argument('--report', type=int, default=6000000)
    p.add_argument('--face-cap', type=int, default=500000)
    p = sub.add_parser('meta')
    p.add_argument('--assets', required=True)
    p.add_argument('--header', required=True)
    a = ap.parse_args()
    try:
        if a.cmd == 'selftest':
            return selftest()
        return {'pack': cmd_pack, 'check': cmd_check, 'budget': cmd_budget, 'meta': cmd_meta}[a.cmd](a) or 0
    except (PackError, OSError, ValueError, KeyError) as x:
        print('rd_pack: %s' % x, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
