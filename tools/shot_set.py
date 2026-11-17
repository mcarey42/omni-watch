#!/usr/bin/env python3
"""Capture and compare a theme's screen set (rim keyboard and Settings track, Task 1: step 1's pixel check, K2).

    python tools/shot_set.py capture --theme T --at ISO --out DIR --end-face KEY [--control] [--only NAME,...]
                                     [--port P] [--watch NAME]
    python tools/shot_set.py compare A B [--diff-dir D]
    python tools/shot_set.py selftest

--port / --watch: the port comes from the git-ignored tools/watch.local (tools/watchport.py, as tools/flash_keep_nvs.py
and tools/theme.sh take it). --port must equal a listed by-id path exactly; any other path is refused, the
/dev/ttyACM* it resolves to included (standing rules: never another /dev/ttyACM*). --watch NAME picks a listed watch;
with several listed, one of the two is required.

capture (the watch's console; tools/shot.py's reader takes each `shot`):
  - checks `state` reads `theme T` and records its reduced-motion value and `awake hold` line. Then `awake hold`,
    `setting motion on` (reduced motion for the whole capture, so ambient motion is still: I7) and `inject lift`.
  - --end-face KEY (required) is the face shown at entry, which the operator names (no console readout shows it).
    It is also the home face the picker, quick settings and Settings are opened over (recorded in the manifest).
  - the screens: one per face key the theme lists (`face` with no argument prints them) as face-<key> (a key not in
    the carousel is recorded as skipped), then picker (`inject open picker`), quick (`inject open quick`) and
    settings (`inject open settings`), each opened over the home face (`face <home>` closes any screen; its FACE
    reply is checked).
  - for each screen: `settime <at>` (as unix seconds), open, 2.5 s for the reveal to end, then twice "settime, 0.4 s,
    shot". A pair that differs is retaken up to twice; a pair still unstable after that is recorded with the last
    pair's differing rectangles (`unstable`, for the report: it never excuses a difference in compare). A pair of
    different sizes is a capture error. DIR gets <name>.png (the first shot of the final pair) and manifest.json.
  - --control (R203, the A/A control; the baseline A is captured this way): each face is captured twice back to back
    on the same build, A1 (<name>.png) and A2 (<name>.a2.png, the row's `control`); the other screens once. The
    A1/A2 rectangles are printed per face.
  - --only NAME,... retakes those screens into an existing DIR (same theme, time, home face and control mode).
  - the restores, at the end, always, each on its own (one failure never skips another), each read back:
      face <end-face>        its `FACE <key>` reply (no console readout of the shown face exists: the close's
                             byte-identical t_<id>/local check is the final confirmation);
      reduced motion         `setting motion <prior>`, then `state` must read it back;
      awake hold             `awake release` (unless the hold was on at entry), then `state` must read `off`;
      inject lift            posts EV_LIFT, which leaves no state behind: nothing to restore (reported).
    Each restore's result is printed. Any failure: `RESTORE FAILED`, a `LEFT CHANGED:` line per item, exit 3.
  Every console command is sent after a bare newline: the console drops the first character while the chip sleeps.
  It prints a reminder to run `net sync`, since it used `settime`.

compare A B: each manifest must carry the expected screen list capture recorded (expected_names: the theme's face
keys, then picker, quick and settings), and the two lists must agree. A screen expected but not captured on either
side (a run cut short), or captured but not expected, is `missing` / `extra` and fails, by name. Then each screen,
pixel for pixel, B against A (A1).
  - Faces (R203): A must be a --control capture with an A2 for every face; a face without one is `missing` and fails,
    even when identical. A B/A1 difference rectangle is excused only when it lies wholly inside one rectangle of A's
    control: where A1 and A2 differ, unioned (R204) with A1's and A2's own 0.4 s shot-pair instability rectangles
    (`unstable` in the row and in its `control`), all the same build's measured noise (seconds markers, sweeps).
    Excused rectangles are reported apart (`excused (A/A)`, which passes); any rectangle partly or wholly outside
    them is STATIC and fails.
  - Every other screen (picker, quick, settings) is exact: no A/A control is consulted, nothing is excused.
A difference is also excused only inside the regions
ANIMATED_REGIONS declares for that theme and screen (a fixed table, never inferred from the captures or from the
difference itself); those screens read `animated (declared)` and pass. Any differing pixel outside them makes the
screen `STATIC`: D/diff-<name>.png (B dimmed, differing pixels magenta; D defaults to B) and the static pixels'
rectangles (x y w h, pixels) are printed. A size mismatch is `STATIC (size)`; picker, quick and settings taken over
different home faces are `STATIC (home face ...)`. A screen missing on one side, whose PNG is absent, or with a
capture error is `missing`; one not in the carousel on both sides is reported as skipped and passes. Exit 0 when
nothing is STATIC or missing (the last line reads PASS), 1 otherwise (STATIC DIFFERENCE / MISSING), 2 on a usage
error.

selftest: the port pin; the PNG codec; the rectangles; compare end to end (identical; a change only in a declared
animated region passes; a planted static change on a face with animation fails; an unstable capture never excuses;
size, missing, skipped and home rows); the restores (each checked, one failure never skipping the others). Prints
SELFTEST OK.
"""
import argparse
import calendar
import json
import os
import struct
import sys
import tempfile
import time
import zlib

sys.dont_write_bytecode = True   # importing watchport leaves no tools/__pycache__ behind
import watchport  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SELFTEST_PORT = '/dev/serial/by-id/usb-fixture_watch_serial-if00'   # the self-test's temp watch file lists it
REVEAL_S = 2.5        # a screen's reveal has ended by then (the brief's figure)
SETTLE_S = 0.4        # after the second settime, before the shot: one tick and a repaint
RETAKES = 2           # an unstable pair is retaken up to twice
MERGE_GAP = 4         # differing pixels within this many px join one rectangle
SCREENS = ('picker', 'quick', 'settings')

# The only regions where a difference between two captures is excused, per theme and screen name:
# (x, y, w, h, reason). Empty: with reduced motion on and the clock set before each shot, no screen is known to
# animate, so every difference is STATIC and goes to the controller. A region is added here only by a controller
# ruling, citing it in the reason.
ANIMATED_REGIONS = {
    'omni': {},
    'dw': {},
    'rd': {
        'face-stasis': [(231, 306, 83, 23, 'runtime slept_s state (R163); rd atlas text (R205)')],
    },
}


def expected_names(keys):
    """The screen set for a theme whose `face` command lists `keys`: capture takes it, compare requires it."""
    return ['face-' + k for k in keys] + list(SCREENS)


def resolve_port(port, watch=None):
    """(the port, None) through tools/watch.local (tools/watchport.py), or (None, why it is refused)."""
    try:
        return watchport.resolve(REPO, port, watch), None
    except watchport.Refused as e:
        return None, 'refused: %s' % e


def port_refusal(port):
    """None when `port` is a by-id path listed in tools/watch.local, written exactly; otherwise why it is refused."""
    if port is None:
        return 'refused: no port'
    return resolve_port(port)[1]


# ---- PNG (8-bit RGB or RGBA, any filter), standard library only ----

def png_write(path, w, h, rgb):
    """rgb: bytes, w*h*3."""
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    stride = w * 3
    raw = b''.join(b'\x00' + rgb[y * stride:(y + 1) * stride] for y in range(h))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def png_read(path):
    """-> (w, h, rgb bytes)."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s: not a PNG' % path)
    pos, idat, w = 8, [], None
    while pos < len(data):
        n, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError('%s: only 8-bit RGB/RGBA, not interlaced' % path)
            bpp = 3 if ctype == 2 else 4
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break
    if w is None:
        raise ValueError('%s: no IHDR' % path)
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
            elif ft != 0:
                raise ValueError('%s: bad filter %d' % (path, ft))
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if bpp == 4:
        out = bytearray(b for i in range(0, len(out), 4) for b in out[i:i + 3])
    return w, h, bytes(out)


# ---- comparison ----

def diff_mask(a, b):
    """-> None when the sizes differ, else (mask bytearray w*h, count) of the differing pixels."""
    (wa, ha, pa), (wb, hb, pb) = a, b
    if (wa, ha) != (wb, hb):
        return None
    mask = bytearray(wa * ha)
    total = 0
    for i in range(wa * ha):
        j = 3 * i
        if pa[j:j + 3] != pb[j:j + 3]:
            mask[i] = 1
            total += 1
    return mask, total


def diff_rects(a, b):
    """a, b: (w, h, rgb). -> None when the sizes differ, else (pixel count, [(x, y, w, h, n), ...]) with the
    differing pixels grouped into rectangles (pixels within MERGE_GAP join)."""
    r = diff_mask(a, b)
    if r is None:
        return None
    mask, total = r
    return total, (mask_rects(mask, a[0], a[1]) if total else [])


def mask_rects(diff, w, h):
    """The set pixels of `diff` (w*h) as rectangles [(x, y, w, h, n), ...], top to bottom."""
    boxes = []   # 8-connected components: [x0, y0, x1, y1, n]
    seen = bytearray(w * h)
    for start in range(w * h):
        if not diff[start] or seen[start]:
            continue
        seen[start] = 1
        stack = [start]
        x0 = x1 = start % w
        y0 = y1 = start // w
        n = 0
        while stack:
            i = stack.pop()
            n += 1
            x, y = i % w, i // w
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            for ny in (y - 1, y, y + 1):
                if ny < 0 or ny >= h:
                    continue
                for nx in (x - 1, x, x + 1):
                    if 0 <= nx < w:
                        k = ny * w + nx
                        if diff[k] and not seen[k]:
                            seen[k] = 1
                            stack.append(k)
        boxes.append([x0, y0, x1, y1, n])
    merged = True
    while merged:   # join boxes whose gap is at most MERGE_GAP pixels on both axes
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a0, b0 = boxes[i], boxes[j]
                gx = max(0, b0[0] - a0[2] - 1, a0[0] - b0[2] - 1)
                gy = max(0, b0[1] - a0[3] - 1, a0[1] - b0[3] - 1)
                if gx <= MERGE_GAP and gy <= MERGE_GAP:
                    boxes[i] = [min(a0[0], b0[0]), min(a0[1], b0[1]), max(a0[2], b0[2]), max(a0[3], b0[3]),
                                a0[4] + b0[4]]
                    del boxes[j]
                    merged = True
                    break
            if merged:
                break
    rects = sorted((x0, y0, x1 - x0 + 1, y1 - y0 + 1, n) for x0, y0, x1, y1, n in boxes)
    rects.sort(key=lambda r: (r[1], r[0]))
    return rects


class _FakeCon:
    """A console for the restore self-tests: `replies` maps a command to its reply lines; `state` lines follow what
    the restores set; `fail` names commands whose reply is lost."""

    def __init__(self, motion='on', hold='ON since 3 s, expires in 1797 s', fail=(), stuck=()):
        self.motion, self.hold, self.fail, self.stuck, self.sent = motion, hold, set(fail), set(stuck), []

    def cmd(self, line, wait=1.0, until=None):
        self.sent.append(line)
        if line in self.fail:
            return ['some log line from another task']
        if line.startswith('face '):
            return ['FACE %s (carousel 0)' % line[5:]]
        if line.startswith('setting motion '):
            if line not in self.stuck:
                self.motion = line.split()[-1]
            return ['SETTING reduced motion %s' % line.split()[-1]]
        if line == 'awake release':
            if line not in self.stuck:
                self.hold = 'off'
            return ['AWAKE released']
        if line == 'state':
            return ['theme t (was none)',
                    'settings: brightness 45, sleep 15 s, 12 h, sound on vol 30, reveal 0, reduced motion %s (nvs)'
                    % self.motion, 'awake hold: %s' % self.hold]
        return []


def selftest():
    fails = []

    def check(name, cond):
        if not cond:
            fails.append(name)
            print('FAIL %s' % name)

    # I1: the port, against a temp watch file (OMNI_WATCH_LOCAL), never the owner's
    with tempfile.TemporaryDirectory() as d:
        wl = os.path.join(d, 'watch.local')
        other = SELFTEST_PORT.replace('fixture_watch', 'fixture_other')
        old_env = os.environ.get(watchport.ENV)
        try:
            with open(wl, 'w') as f:
                f.write('%s one\n' % SELFTEST_PORT)
            os.environ[watchport.ENV] = wl
            check('port: the by-id path is accepted', port_refusal(SELFTEST_PORT) is None)
            check('port: one watch listed: the default', resolve_port(None) == (SELFTEST_PORT, None))
            for bad in ('/dev/ttyACM7', '/dev/ttyACM0', os.path.realpath(SELFTEST_PORT)
                        if os.path.realpath(SELFTEST_PORT) != SELFTEST_PORT else '/dev/ttyACM9', SELFTEST_PORT + '/',
                        SELFTEST_PORT.replace('fixture_watch', 'fixture_watcH'), 'ttyACM0',
                        '/dev/serial/by-id/../../ttyACM7', None):
                check('port: %s refused' % bad, port_refusal(bad) is not None)
            with open(wl, 'w') as f:
                f.write('%s one\n%s two\n' % (SELFTEST_PORT, other))
            check('port: two listed: --watch two', resolve_port(None, 'two') == (other, None))
            check('port: two listed: neither flag refused', resolve_port(None)[0] is None)
            check('port: --port and --watch disagree: refused', resolve_port(SELFTEST_PORT, 'two')[0] is None)
        finally:
            if old_env is None:
                os.environ.pop(watchport.ENV, None)
            else:
                os.environ[watchport.ENV] = old_env

    w, h = 24, 16
    base = bytes((x * 7 + y * 3 + c) & 255 for y in range(h) for x in range(w) for c in range(3))

    def planted(*pts):
        out = bytearray(base)
        for x, y in pts:
            out[(y * w + x) * 3 + 1] ^= 0x10
        return bytes(out)

    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'a.png')
        png_write(p, w, h, base)
        check('png round trip', png_read(p) == (w, h, base))
        a = (w, h, base)
        check('identical: no difference', diff_rects(a, (w, h, base)) == (0, []))
        r = diff_rects(a, (w, h, planted((9, 5))))
        check('one pixel off: one 1x1 rectangle at 9,5', r == (1, [(9, 5, 1, 1, 1)]))
        check('size mismatch: None', diff_rects(a, (w + 1, h, base + bytes(3 * h))) is None)
        two = bytearray(base)
        for x, y in ((1, 1), (2, 2), (20, 12)):
            two[(y * w + x) * 3] ^= 0x80
        check('two clusters: near pixels merge, far ones do not',
              diff_rects(a, (w, h, bytes(two))) == (3, [(1, 1, 2, 2, 2), (20, 12, 1, 1, 1)]))

        # compare end to end. face-x declares one animated region (8,4 3x3); face-y and the screens declare none.
        regions = {'face-x': [(8, 4, 3, 3, 'test: a declared animated region')]}
        da, db = os.path.join(d, 'A'), os.path.join(d, 'B')

        def setup(b_imgs, b_rows=None, b_home='k', a_rows=None, a_exp=None, b_exp=None, a2=None, a_ctl=True):
            """A is a control capture (--control): each face has an A2 shot, base unless `a2` gives one."""
            for dd in (da, db):
                if os.path.isdir(dd):
                    for f in os.listdir(dd):
                        os.remove(os.path.join(dd, f))
                else:
                    os.mkdir(dd)
            names = ('face-x', 'face-y', 'picker')
            for n in names:
                png_write(os.path.join(da, n + '.png'), w, h, base)
            for n, img in b_imgs.items():
                if img is not None:
                    png_write(os.path.join(db, n + '.png'), *img)
            rows = {n: {'status': 'ok'} for n in names}
            arows = json.loads(json.dumps(a_rows or rows))
            for n, row in arows.items():
                if a_ctl and n.startswith('face-') and row.get('status') == 'ok':
                    row['control'] = {'status': 'ok', 'png': n + '.a2.png'}
            for n, img in dict({'face-x': a, 'face-y': a}, **(a2 or {})).items():
                png_write(os.path.join(da, n + '.a2.png'), *img)
                if n in arows and not n.startswith('face-'):   # a control on a non-face screen: never used
                    arows[n]['control'] = {'status': 'ok', 'png': n + '.a2.png'}
            exp = list(names)
            write_manifest(da, {'theme': 't', 'at': 'T', 'home': 'k', 'control': a_ctl, 'expected': a_exp or exp,
                                'screens': arows})
            write_manifest(db, {'theme': 't', 'at': 'T', 'home': b_home, 'expected': b_exp or exp,
                                'screens': b_rows or rows})
            return compare_dirs(da, db, db, regions)

        same = {'face-x': a, 'face-y': a, 'picker': a}
        rc, lines = setup(same)
        check('compare: exit 0 when all identical', rc == 0)
        check('compare: no diff image for a same screen', not os.path.exists(os.path.join(db, 'diff-face-x.png')))

        rc, lines = setup(dict(same, **{'face-x': (w, h, planted((9, 5)))}))
        check('compare: a change only inside the declared animated region passes', rc == 0)
        check('compare: ... and is reported as animated (declared)',
              any(l.startswith('face-x animated (declared)') for l in lines))

        rc, lines = setup(dict(same, **{'face-x': (w, h, planted((9, 5), (20, 12)))}))
        check('compare: a planted static change on a face with animation FAILS', rc == 1)
        check('compare: ... STATIC, with only the static pixel\'s rectangle',
              any(l.startswith('face-x STATIC') and '20 12 1 1' in l and '9 5 1 1' not in l for l in lines))
        check('compare: diff-face-x.png written', os.path.isfile(os.path.join(db, 'diff-face-x.png')))

        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((9, 5)))}))
        check('compare: the same pixel on a face that declares nothing FAILS', rc == 1)

        rows = {n: {'status': 'ok', 'unstable': [[0, 0, w, h]]} for n in ('face-x', 'face-y', 'picker')}
        rc, lines = setup(dict(same, **{'picker': (w, h, planted((20, 12)))}), b_rows=rows, a_rows=rows)
        check('compare: a capture\'s own unstable pair never excuses a difference', rc == 1 and any(
            l.startswith('picker STATIC') for l in lines))

        rc, lines = setup(dict(same, **{'picker': (w + 1, h, base + bytes(3 * h))}))
        check('compare: a size mismatch is STATIC (size)', rc == 1 and any(
            l.startswith('picker STATIC (size)') for l in lines))

        rc, lines = setup(dict(same, **{'picker': None}))
        check('compare: ok in the manifest but no PNG is missing, not a crash', rc == 1 and any(
            l.startswith('picker missing') for l in lines))

        b_rows = {'face-x': {'status': 'ok'}, 'face-y': {'status': 'ok'}}
        rc, lines = setup(same, b_rows=b_rows)
        check('compare: a screen absent from B is missing', rc == 1 and any(
            l.startswith('picker missing in B') for l in lines))

        rows = {'face-x': {'status': 'ok'}, 'face-y': {'status': 'not in carousel'}, 'picker': {'status': 'ok'}}
        rc, lines = setup(same, b_rows=rows, a_rows=rows)
        check('compare: skipped on both sides is reported and passes', rc == 0 and any(
            l.startswith('face-y skipped on both') for l in lines))

        rows = {'face-x': {'status': 'ok'}, 'face-y': {'status': 'ok'}, 'picker': {'status': 'error: no reply'}}
        rc, lines = setup(same, b_rows=rows, a_rows=rows)
        check('compare: a capture error on both sides is missing, never skipped', rc == 1 and any(
            l.startswith('picker missing') for l in lines))

        # R203: the A/A control. face-y's A2 differs from A1 in a 6x6 block at 10,4 (a moving seconds marker, say)
        aa = {'face-y': (w, h, planted(*[(x, y) for x in range(10, 16) for y in range(4, 10)]))}
        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((12, 6)))}), a2=aa)
        check('control: an A/B difference inside the A/A rects passes', rc == 0 and any(
            l.startswith('face-y excused (A/A)') and '12 6 1 1' in l for l in lines))
        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((12, 6), (17, 6)))}), a2=aa)
        check('control: an A/B difference partly outside the A/A rects FAILS', rc == 1 and any(
            l.startswith('face-y STATIC') and '12 6 6 1' in l for l in lines))
        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((12, 6), (21, 14)))}), a2=aa)
        check('control: excused and failing rects are reported apart', rc == 1 and any(
            l.startswith('face-y STATIC') and '21 14 1 1' in l and 'excused (A/A): 12 6 1 1' in l for l in lines))
        rc, lines = setup(dict(same, **{'picker': (w, h, planted((12, 6)))}),
                          a2=dict(aa, picker=aa['face-y']))
        check('control: a non-face screen difference FAILS even where its A/A differs', rc == 1 and any(
            l.startswith('picker STATIC') for l in lines))
        # R204: A's own shot-pair instability rects (A1's row, A2's control row) join the control
        pr = {'face-x': {'status': 'ok'}, 'face-y': {'status': 'ok', 'unstable': [[10, 4, 6, 6]]},
              'picker': {'status': 'ok'}}
        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((12, 6)))}), a_rows=pr)
        check('R204: an A/B difference inside only A1\'s pair rects passes', rc == 0 and any(
            l.startswith('face-y excused (A/A)') and 'A pair rects: 10 4 6 6' in l for l in lines))
        rc, lines = setup(dict(same, **{'face-y': (w, h, planted((21, 14)))}), a_rows=pr, a2=aa)
        check('R204: an A/B difference outside both the A/A and the pair rects FAILS', rc == 1 and any(
            l.startswith('face-y STATIC') and '21 14 1 1' in l for l in lines))
        rc, lines = setup(dict(same, **{'picker': (w, h, planted((12, 6)))}),
                          a_rows=dict(pr, picker={'status': 'ok', 'unstable': [[10, 4, 6, 6]]}))
        check('R204: a non-face screen difference FAILS even inside its own pair rects', rc == 1 and any(
            l.startswith('picker STATIC') for l in lines))
        rc, lines = setup(same, a_ctl=False)
        check('control: a face with no A/A control FAILS, even when identical', rc == 1 and any(
            l.startswith('face-x missing: no A/A control') for l in lines))

        early = {'face-x': {'status': 'ok'}, 'face-y': {'status': 'ok'}}   # both runs stopped before picker
        rc, lines = setup(same, b_rows=early, a_rows=early)
        check('compare: both captures stopping early at the same screen FAILS, naming it', rc == 1 and any(
            l.startswith('picker missing in A') for l in lines) and any(l.startswith('picker missing in B')
                                                                         for l in lines))
        rows = {n: {'status': 'ok'} for n in ('face-x', 'face-y', 'picker', 'face-z')}
        rc, lines = setup(same, b_rows=rows)
        check('compare: an extra screen FAILS, naming it', rc == 1 and any(
            l.startswith('face-z extra') for l in lines))
        rc, lines = setup(same, b_exp=['face-x', 'face-y', 'picker', 'quick'])
        check('compare: expected lists that differ FAIL, naming the screen', rc == 1 and any(
            l.startswith('quick missing') for l in lines))
        write_manifest(da, {'theme': 't', 'at': 'T', 'home': 'k', 'screens': {n: {'status': 'ok'} for n in (
            'face-x', 'face-y', 'picker')}})
        rc, lines = compare_dirs(da, db, db, regions)
        check('compare: a manifest without its expected list FAILS', rc == 1 and any(
            'no expected screen list' in l for l in lines))
        check('expected_names: the faces, then picker, quick and settings',
              expected_names(['a', 'b']) == ['face-a', 'face-b', 'picker', 'quick', 'settings'])

        rc, lines = setup(same, b_home='other')
        check('compare: different home faces fail the screens taken over them', rc == 1 and any(
            l.startswith('picker STATIC (home face') for l in lines))

    # I2: the restores, each checked on its own; one failure never skips the others
    con = _FakeCon()
    res = restore_all(con, end_face='k', prior_motion='off', prior_hold=False)
    check('restore: all three confirmed', all(ok for _, ok, _ in res) and len(res) == 4)
    check('restore: motion read back off', con.motion == 'off')
    check('restore: hold read back off', con.hold == 'off')
    con = _FakeCon(fail={'face k'})
    res = restore_all(con, end_face='k', prior_motion='off', prior_hold=False)
    bad = [what for what, ok, _ in res if not ok]
    check('restore: a lost face reply fails the face restore only', bad == ['face'])
    check('restore: ... and the motion and hold restores still ran',
          'setting motion off' in con.sent and 'awake release' in con.sent)
    con = _FakeCon(stuck={'setting motion off'})
    res = restore_all(con, end_face='k', prior_motion='off', prior_hold=False)
    check('restore: motion that does not read back fails', [w_ for w_, ok, _ in res if not ok] == ['reduced motion'])
    con = _FakeCon(stuck={'awake release'})
    res = restore_all(con, end_face='k', prior_motion='off', prior_hold=False)
    check('restore: a hold still on fails', [w_ for w_, ok, _ in res if not ok] == ['awake hold'])
    con = _FakeCon()
    res = restore_all(con, end_face='k', prior_motion='on', prior_hold=True)
    check('restore: a hold found on at entry is left on', 'awake release' not in con.sent and all(
        ok for _, ok, _ in res))

    if fails:
        print('SELFTEST FAILED (%d)' % len(fails))
        return 1
    print('SELFTEST OK')
    return 0


def write_manifest(d, m):
    with open(os.path.join(d, 'manifest.json'), 'w') as f:
        json.dump(m, f, indent=1, sort_keys=True)
        f.write('\n')


def read_manifest(d):
    with open(os.path.join(d, 'manifest.json')) as f:
        return json.load(f)


def diff_png(path, a, b):
    """B dimmed to a quarter, the pixels that differ from A magenta (same sizes)."""
    (_, _, pa), (w, h, pb) = a, b
    out = bytearray(w * h * 3)
    for i in range(w * h):
        j = 3 * i
        if pa[j:j + 3] != pb[j:j + 3]:
            out[j:j + 3] = b'\xff\x00\xff'
        else:
            out[j] = pb[j] >> 2
            out[j + 1] = pb[j + 1] >> 2
            out[j + 2] = pb[j + 2] >> 2
    png_write(path, w, h, bytes(out))


def fmt_rects(rects):
    return '; '.join('%d %d %d %d (%d px)' % r for r in rects)


def rect_inside(r, regions):
    """True when rectangle r (x, y, w, h, ...) lies wholly inside one of `regions`."""
    x, y, w, h = r[:4]
    return any(x >= rx and y >= ry and x + w <= rx + rw and y + h <= ry + rh for rx, ry, rw, rh in
               (q[:4] for q in regions))


def compare_dirs(da, db, diff_dir, regions=None):
    """-> (exit code, report lines). `regions`: {screen: [(x, y, w, h, reason)]}; default ANIMATED_REGIONS for A's
    theme. Prints nothing itself."""
    ma, mb = read_manifest(da), read_manifest(db)
    sa, sb = ma.get('screens', {}), mb.get('screens', {})
    if regions is None:
        regions = ANIMATED_REGIONS.get(ma.get('theme'), {})
    lines = []
    counts = {'same': 0, 'animated': 0, 'excused': 0, 'STATIC': 0, 'missing': 0, 'skipped': 0}
    if ma.get('theme') != mb.get('theme'):
        lines.append('note: themes differ (%s, %s)' % (ma.get('theme'), mb.get('theme')))
    if ma.get('at') != mb.get('at'):
        lines.append('note: capture times differ (%s, %s)' % (ma.get('at'), mb.get('at')))
    home_differs = ma.get('home') != mb.get('home')
    ea, eb = ma.get('expected'), mb.get('expected')
    for side, e in (('A', ea), ('B', eb)):
        if not e:
            lines.append('%s: no expected screen list in its manifest' % side)
            counts['missing'] += 1
    ea, eb = set(ea or []), set(eb or [])
    for name in sorted(ea | eb | set(sa) | set(sb), key=lambda n: (not n.startswith('face-'), n)):
        ra, rb = sa.get(name), sb.get(name)
        if name not in ea | eb:
            lines.append('%s extra: not in the expected screen list (%s)' % (
                name, ' and '.join(s for s, r in (('A', ra), ('B', rb)) if r is not None)))
            counts['missing'] += 1
            continue
        if (name in ea) != (name in eb):
            lines.append('%s missing: expected by %s only' % (name, 'A' if name in ea else 'B'))
            counts['missing'] += 1
            continue
        if ra is None or rb is None:
            for side, r in (('A', ra), ('B', rb)):
                if r is None:
                    lines.append('%s missing in %s (expected, not captured)' % (name, side))
            counts['missing'] += 1
            continue
        for side, row in (('A', ra), ('B', rb)):
            if row.get('unstable'):
                lines.append('note: %s %s was unstable at capture (%s); this excuses nothing' % (
                    name, side, '; '.join('%d %d %d %d' % tuple(x[:4]) for x in row['unstable'])))
        if ra.get('status') != 'ok' or rb.get('status') != 'ok':
            if ra.get('status') == rb.get('status') == 'not in carousel':   # a capture error never passes
                lines.append('%s skipped on both (%s)' % (name, ra.get('status')))
                counts['skipped'] += 1
            else:
                lines.append('%s missing: A %s, B %s' % (name, ra.get('status'), rb.get('status')))
                counts['missing'] += 1
            continue
        if home_differs and name in SCREENS:
            lines.append('%s STATIC (home face A %s, B %s: taken over different faces)' % (
                name, ma.get('home'), mb.get('home')))
            counts['STATIC'] += 1
            continue
        ia2 = None
        if name.startswith('face-'):   # R203: every face needs A's A/A control (capture --control)
            c = ra.get('control') if ma.get('control') else None
            p2 = os.path.join(da, c['png']) if c and c.get('png') else None
            if not c or c.get('status') != 'ok' or not os.path.isfile(p2):
                lines.append('%s missing: no A/A control in A (capture A with --control; R203)' % name)
                counts['missing'] += 1
                continue
            ia2 = png_read(p2)
        pa_, pb_ = os.path.join(da, name + '.png'), os.path.join(db, name + '.png')
        absent = [s for s, p in (('A', pa_), ('B', pb_)) if not os.path.isfile(p)]
        if absent:
            lines.append('%s missing: no PNG in %s' % (name, ' and '.join(absent)))
            counts['missing'] += 1
            continue
        ia, ib = png_read(pa_), png_read(pb_)
        r = diff_mask(ia, ib)
        if r is None:
            lines.append('%s STATIC (size) A %dx%d, B %dx%d' % (name, ia[0], ia[1], ib[0], ib[1]))
            counts['STATIC'] += 1
            continue
        mask, total = r
        if total == 0:
            lines.append('%s same' % name)
            counts['same'] += 1
            continue
        w, h = ia[0], ia[1]
        static = bytearray(mask)
        for reg in regions.get(name, []):
            rx, ry, rw, rh = reg[:4]
            for y in range(max(0, ry), min(h, ry + rh)):
                for x in range(max(0, rx), min(w, rx + rw)):
                    static[y * w + x] = 0
        n_static = sum(static)
        dpath = os.path.join(diff_dir, 'diff-%s.png' % name)
        diff_png(dpath, ia, ib)
        if n_static == 0:
            counts['animated'] += 1
            lines.append('%s animated (declared) %d px, all inside %s -> %s' % (
                name, total, '; '.join('%d %d %d %d (%s)' % tuple(x) for x in regions[name]), dpath))
            continue
        rects = mask_rects(static, w, h)
        excused = []
        if ia2 is not None:   # a face: B vs A1 rects excused only inside a rect where A1 and A2 differ
            aa = diff_rects(ia, ia2)
            if aa is None:
                lines.append('%s missing: A2 is %dx%d, A1 %dx%d' % (name, ia2[0], ia2[1], w, h))
                counts['missing'] += 1
                continue
            aa_rects = aa[1]
            # R204: the control is the union of the A1/A2 rects and every A-side capture's own shot-pair
            # instability rects (A1's row and A2's control row), all measured on the A build
            pair_rects = [tuple(q[:4]) for q in (ra.get('unstable') or []) + (ra['control'].get('unstable') or [])]
            ctl = list(aa_rects) + pair_rects
            excused = [r for r in rects if rect_inside(r, ctl)]
            rects = [r for r in rects if not rect_inside(r, ctl)]
            if not rects:
                counts['excused'] += 1
                lines.append('%s excused (A/A) %d rect(s): %s; A/A rects: %s; A pair rects: %s -> %s' % (
                    name, len(excused), fmt_rects(excused), fmt_rects(aa_rects) or 'none',
                    '; '.join('%d %d %d %d' % q for q in pair_rects) or 'none', dpath))
                continue
        counts['STATIC'] += 1
        lines.append('%s STATIC %d px in %d rect(s) outside any declared region%s: %s%s -> %s' % (
            name, sum(r[4] for r in rects), len(rects), ' or A control rect' if ia2 is not None else '',
            fmt_rects(rects), '; excused (A/A): %s' % fmt_rects(excused) if excused else '', dpath))
    fail = counts['STATIC'] + counts['missing']
    verdict = 'PASS' if fail == 0 else ('STATIC DIFFERENCE' if counts['STATIC'] else 'MISSING')
    lines.append('SUMMARY same %d, animated (declared) %d, excused (A/A) %d, STATIC %d, missing %d, skipped %d: %s'
                 % (counts['same'], counts['animated'], counts['excused'], counts['STATIC'], counts['missing'],
                    counts['skipped'], verdict))
    return (0 if fail == 0 else 1), lines


# ---- capture (the watch's console) ----

class Console:
    """One command at a time, the port opened per command as tools/shot.py opens it. Only a listed port."""

    def __init__(self, port):
        why = port_refusal(port)
        if why:
            raise ValueError(why)
        import serial   # pyserial: only capture needs it
        self.serial = serial
        self.port = port

    def cmd(self, line, wait=1.0, until=None):
        """A bare newline, 100 ms, then the command. Returns the lines read for `wait` s, or up to the first line
        that starts with `until`."""
        out = []
        with self.serial.Serial(self.port, 115200, timeout=0.2) as ser:
            ser.reset_input_buffer()
            ser.write(b'\n')
            time.sleep(0.1)
            ser.write(line.encode('ascii') + b'\n')
            deadline = time.time() + wait
            while time.time() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                text = raw.decode('ascii', 'replace').strip()
                if text:
                    out.append(text)
                    if until and text.startswith(until):
                        break
        return out

    def shot(self):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import shot   # tools/shot.py's reader
        for attempt in range(3):   # as shot.py: a log line inside a row loses that row, so retake
            try:
                w, h, rows = shot.capture(self.port, 60)
                return w, h, b''.join(rows)
            except (RuntimeError, TimeoutError) as e:
                if attempt == 2:
                    raise
                print('  retrying shot: %s' % e, file=sys.stderr)


def iso_to_unix(at):
    t = at[:-1] if at.endswith('Z') else at
    return calendar.timegm(time.strptime(t, '%Y-%m-%dT%H:%M:%S'))


def face_keys(con):
    for line in con.cmd('face', wait=1.5, until='usage: face'):
        if line.startswith('usage: face'):
            rest = line[len('usage: face'):].strip()
            return [k for k in rest.split('|') if k]
    raise RuntimeError('`face` printed no key list')


def state_value(lines, prefix):
    for line in lines:
        if line.startswith(prefix):
            return line
    return None


def reply_line(lines, prefixes):
    """Only the command's own reply line (never another task's log line), or None."""
    for line in lines:
        if line.startswith(prefixes):
            return line
    return None


def open_face(con, key):
    """`face <key>`: True when the reply is `FACE <key> (carousel n)`."""
    r = reply_line(con.cmd('face %s' % key, wait=1.5, until=('FACE',)), ('FACE',))
    return r is not None and r.startswith('FACE %s (carousel' % key)


def read_motion(con):
    line = state_value(con.cmd('state', wait=3.0), 'settings: ')
    if line is None or 'reduced motion ' not in line:
        return None
    return line.split('reduced motion ')[1].split()[0]


def read_hold(con):
    """True (on), False (off) or None (no `awake hold:` line)."""
    line = state_value(con.cmd('state', wait=3.0), 'awake hold: ')
    if line is None:
        return None
    return not line.startswith('awake hold: off')


def restore_all(con, end_face, prior_motion, prior_hold):
    """Each restore on its own, each read back. -> [(what, ok, detail)]."""
    res = []

    def step(what, fn):
        try:
            ok, detail = fn()
        except Exception as e:   # a serial error in one restore must not skip the others
            ok, detail = False, 'error: %s' % e
        res.append((what, ok, detail))

    def face():
        if open_face(con, end_face):
            return True, 'FACE %s confirmed (the close\'s t_<id>/local check is the final word)' % end_face
        return False, 'no `FACE %s` reply: the shown face may not be %s' % (end_face, end_face)

    def motion():
        con.cmd('setting motion %s' % prior_motion, until='SETTING')
        for _ in range(3):
            v = read_motion(con)
            if v == prior_motion:
                return True, 'state reads reduced motion %s' % v
            time.sleep(0.5)
        return False, 'state reads reduced motion %s, want %s' % (v, prior_motion)

    def hold():
        if prior_hold:
            return (True, 'on at entry: left held') if read_hold(con) else (False, 'on at entry, now off')
        con.cmd('awake release', until='AWAKE')
        v = read_hold(con)
        return (True, 'state reads awake hold: off') if v is False else (
            False, 'state reads awake hold %s, want off' % ('on' if v else '(no line)'))

    step('face', face)
    step('reduced motion', motion)
    step('awake hold', hold)
    res.append(('inject lift', True, 'EV_LIFT leaves no state behind: nothing to restore'))
    return res


def take(con, name, open_cmd, at_unix, home):
    """One screen: settime, open, the reveal, then a stable pair of shots. -> (status, rgb tuple, unstable)."""
    con.cmd('settime %d' % at_unix, until='SETTIME')
    if home:
        if not open_face(con, home):   # closes any screen (depth 1)
            return 'error: no FACE reply for the home face %s' % home, None, []
        time.sleep(REVEAL_S)
        con.cmd('settime %d' % at_unix, until='SETTIME')
    if open_cmd.startswith('face '):
        reply = reply_line(con.cmd(open_cmd, wait=1.5, until=('FACE',)), ('FACE',))
        if reply is not None and reply.startswith('FACE not in carousel'):
            return 'not in carousel', None, []
        ok = reply is not None and reply.startswith('FACE %s (carousel' % open_cmd[5:])
    else:
        reply = reply_line(con.cmd(open_cmd, wait=1.5, until=('INJECT',)), ('INJECT',))
        ok = reply == 'INJECT open %s' % open_cmd.split()[-1]
    if not ok:
        return 'error: no reply to %r' % open_cmd, None, []
    time.sleep(REVEAL_S)
    for attempt in range(1 + RETAKES):
        pair = []
        for _ in range(2):
            con.cmd('settime %d' % at_unix, until='SETTIME')
            time.sleep(SETTLE_S)
            pair.append(con.shot())
        r = diff_rects(pair[0], pair[1])
        if r is None:
            return 'error: the pair differs in size (%dx%d, %dx%d)' % (pair[0][0], pair[0][1], pair[1][0],
                                                                        pair[1][1]), None, []
        if r[0] == 0:
            print('  %s: stable pair (attempt %d)' % (name, attempt + 1))
            return 'ok', pair[0], []
        print('  %s: the pair differs (attempt %d): %s' % (name, attempt + 1, fmt_rects(r[1])))
    return 'ok', pair[0], [list(x[:4]) for x in r[1]]   # the final attempt's rectangles only


def capture(a):
    con = Console(a.port)
    at_unix = iso_to_unix(a.at)
    os.makedirs(a.out, exist_ok=True)
    mpath = os.path.join(a.out, 'manifest.json')
    only = [n for n in (a.only or '').split(',') if n]
    if only:
        m = read_manifest(a.out)
        if (m.get('theme'), m.get('at'), m.get('home'), bool(m.get('control'))) != (a.theme, a.at, a.end_face,
                                                                                    bool(a.control)):
            print('refused: %s was captured for theme %s at %s over face %s, control %s' % (
                mpath, m.get('theme'), m.get('at'), m.get('home'), bool(m.get('control'))))
            return 2
    else:
        m = {'theme': a.theme, 'at': a.at, 'home': a.end_face, 'control': bool(a.control), 'screens': {}}
    st = con.cmd('state', wait=3.0)
    th = state_value(st, 'theme ')
    if th is None or th.split()[1] != a.theme:
        print('refused: `state` reads %r, not theme %s' % (th, a.theme))
        return 2
    mot = state_value(st, 'settings: ')
    hold = state_value(st, 'awake hold: ')
    if mot is None or 'reduced motion ' not in mot or hold is None:
        print('refused: `state` gave no reduced-motion value or no awake hold line (a console-off build?)')
        return 2
    prior_motion = mot.split('reduced motion ')[1].split()[0]
    prior_hold = not hold.startswith('awake hold: off')
    print('theme %s; reduced motion was %s; awake hold was %s' % (a.theme, prior_motion,
                                                                   'on' if prior_hold else 'off'))
    keys = face_keys(con)
    print('faces: %s' % ' '.join(keys))
    if a.end_face not in keys:
        print('refused: --end-face %s is not one of %s' % (a.end_face, ' '.join(keys)))
        return 2
    names = expected_names(keys)
    if only and m.get('expected') != names:
        print('refused: %s expects %s, the watch lists %s' % (mpath, m.get('expected'), names))
        return 2
    m['expected'] = names
    for n in only:
        if n not in names:
            print('refused: --only %s is not one of %s' % (n, ' '.join(names)))
            return 2
    try:
        con.cmd('awake hold', until='AWAKE')
        con.cmd('setting motion on', until='SETTING')
        con.cmd('inject lift', until='INJECT')
        time.sleep(REVEAL_S)
        for name in (only or names):
            if name.startswith('face-'):
                status, img, unstable = take(con, name, 'face %s' % name[5:], at_unix, None)
            else:
                status, img, unstable = take(con, name, 'inject open %s' % name, at_unix, a.end_face)
            row = {'status': status}
            if unstable:
                row['unstable'] = unstable
            if a.control and name.startswith('face-') and status == 'ok':
                # R203: the A/A control, the same face captured again at once on the same build (A2)
                s2, img2, u2 = take(con, name + ' (A2)', 'face %s' % name[5:], at_unix, None)
                row['control'] = {'status': s2, 'png': name + '.a2.png'}
                if u2:   # R204: A2's own shot-pair instability is control evidence too
                    row['control']['unstable'] = u2
                if img2 is not None:
                    png_write(os.path.join(a.out, name + '.a2.png'), *img2)
                    aa = diff_rects(img, img2)
                    print('  %s A/A: %s' % (name, 'size differs' if aa is None else (
                        fmt_rects(aa[1]) or 'identical')))
            m['screens'][name] = row
            if img is not None:
                png_write(os.path.join(a.out, name + '.png'), *img)
            print('%s %s%s' % (name, status, ' (unstable %s)' % unstable if unstable else ''))
            write_manifest(a.out, m)
    finally:
        res = restore_all(con, a.end_face, prior_motion, prior_hold)
        for what, ok, detail in res:
            print('restore %s: %s (%s)' % (what, 'ok' if ok else 'FAILED', detail))
        print('REMINDER: settime was used: run `net sync` (the close)')
    failed = [(what, detail) for what, ok, detail in res if not ok]
    if failed:
        print('RESTORE FAILED')
        for what, detail in failed:
            print('LEFT CHANGED: %s: %s' % (what, detail))
        return 3
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('selftest')
    c = sub.add_parser('capture')
    c.add_argument('--port', help='a by-id path listed in tools/watch.local, exactly')
    c.add_argument('--watch', metavar='NAME', help='a watch named in tools/watch.local')
    c.add_argument('--theme', required=True, choices=('omni', 'dw', 'rd'))
    c.add_argument('--at', required=True, help='UTC, e.g. 2026-11-09T15:00:00Z')
    c.add_argument('--out', required=True)
    c.add_argument('--end-face', metavar='KEY', required=True,
                   help='the face shown at entry: the tool ends on it, and opens the screens over it')
    c.add_argument('--only', metavar='NAME,...', help='retake these screens into an existing --out')
    c.add_argument('--control', action='store_true',
                   help='R203: each face captured twice back to back (A1, A2: the A/A control); other screens once')
    k = sub.add_parser('compare')
    k.add_argument('a')
    k.add_argument('b')
    k.add_argument('--diff-dir', help='where diff-<name>.png go (default: B)')
    a = ap.parse_args(argv)
    if a.cmd == 'selftest':
        return selftest()
    if a.cmd == 'capture':
        a.port, why = resolve_port(a.port, a.watch)
        if why:
            print(why)
            return 2
        try:
            iso_to_unix(a.at)
        except ValueError:
            print('--at %r: want YYYY-MM-DDTHH:MM:SSZ' % a.at)
            return 2
        return capture(a)
    for d in (a.a, a.b):
        if not os.path.isfile(os.path.join(d, 'manifest.json')):
            print('%s: no manifest.json' % d)
            return 2
    rc, lines = compare_dirs(a.a, a.b, a.diff_dir or a.b)
    for line in lines:
        print(line)
    return rc


if __name__ == '__main__':
    sys.exit(main())
