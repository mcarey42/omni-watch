#!/usr/bin/env python3
"""Tests for firmware/apps/dw/main/dw_stage.cmake (public release Task 9): the overlay's display names (MODE=names,
checked by MODE=validate too), the thumbnails and the chime as swap files (MODE=stage), on scratch overlays and
synthetic generic files only. Needs cmake. Run from the repo root:
    python -m unittest tools/test_dw_stage.py

  - NamesOk: every known key, a 24-character value and an apostrophe; the header's text pinned for one entry;
  - NamesBad: a '"', a '\\', a non-ASCII byte, 25 characters, the empty string, a leading space, an unknown face key,
    an unknown key, names as a list and 33 members each refused, naming the key;
  - NamesAbsent: no names, no header; with names, a rerun leaves the header's stamp alone;
  - NamesRemoved: a header from a run with names is deleted by a run without (preflight M16);
  - NamesTrigraph: a value holding ?? is refused (M16);
  - GenericRowsStillThree: check_apps' generic-row rule passes the real dw_layers.json with DW_SWAP at six (I1);
  - PcmSwap: a 102 400 B chime stages; 110 082 B and an odd size are refused;
  - ThumbSwap: a 68 x 68 RGB565A8 thumbnail stages; a 67-wide one is refused;
  - OldOverlay: the original three swap files and no names validate and stage;
  - KnownKeysMatchTable: dw_stage.cmake's name_keys are DW_FACE_TABLE's keys plus the four groups.
"""
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
STAGE = os.path.join(ROOT, 'firmware', 'apps', 'dw', 'main', 'dw_stage.cmake')
PREFS = os.path.join(ROOT, 'firmware', 'apps', 'dw', 'components', 'dw_core', 'include', 'dw', 'prefs.h')
LAYERS = os.path.join(ROOT, 'tools', 'export', 'dw_layers.json')
SWAP3 = ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin')
GROUPS = ('modern', 'classic', 'circles', 'faces')


def lvgl(w, h, cf=0x12, bpp=2):
    """A synthetic LVGL v9 bin: magic 0x19, cf, flags 0, w, h, stride, then w*h*bpp pixel bytes."""
    return struct.pack('<BBHHHHH', 0x19, cf, 0, w, h, w * bpp, 0) + bytes([0x5A]) * (w * h * bpp)


def thumb(w=68, h=68):
    """A 68 x 68 RGB565A8 thumbnail is 12 + 68*68*3 = 13 884 B."""
    return struct.pack('<BBHHHHH', 0x19, 0x14, 0, w, h, w * 2, 0) + bytes([0x33]) * (w * h * 3)


def table_keys():
    """DW_FACE_TABLE's keys (dw/prefs.h), in table order."""
    text = open(PREFS).read()
    table = text[text.index('#define DW_FACE_TABLE(X)'):text.index('extern const char *const DW_FACE_KEYS')]
    return re.findall(r'X\(DW_F_\w+,\s*(\w+),', table)


@unittest.skipUnless(shutil.which('cmake'), 'needs cmake')
class DwStageNames(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='dwstage-names-')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.out = os.path.join(self.tmp, 'b', 'dw_names', 'dw_names_overlay.h')

    def art(self, names=None, replaces=SWAP3, drop_names=False):
        art = os.path.join(self.tmp, 'art')
        os.makedirs(os.path.join(art, 'assets'), exist_ok=True)
        d = {'theme': 'dw', 'replaces': list(replaces), 'layers': 'layers.json'}
        if names is not None:
            d['names'] = names
        with open(os.path.join(art, 'over' + 'lay.json'), 'w', encoding='utf-8') as f:
            f.write(json.dumps(d, ensure_ascii=False))
        return art

    def cmake(self, mode, art, **defs):
        args = ['cmake', '-DMODE=' + mode, '-DART=' + art]
        args += ['-D%s=%s' % kv for kv in defs.items()]
        r = subprocess.run(args + ['-P', STAGE], capture_output=True, text=True)
        return r.returncode, ' '.join((r.stdout + r.stderr).split())

    def names(self, art):
        return self.cmake('names', art, OUT=self.out)

    def all_names(self):
        n = {'theme': 'Zz Theme'}
        n.update({'face:' + k: 'Zz ' + k.title() for k in table_keys()})
        n.update({'group:' + g: 'Zz ' + g.title() for g in GROUPS})
        return n

    def test_names_ok(self):
        n = self.all_names()
        n['face:circles'] = 'Zz Script'
        n['face:rotor'] = 'Abcdefghijklmnopqrstuvwx'   # 24 characters
        n['group:circles'] = "Zz's group"
        self.assertEqual(len(n['face:rotor']), 24)
        art = self.art(n)
        rc, out = self.cmake('validate', art)
        self.assertEqual(rc, 0, out)
        rc, out = self.names(art)
        self.assertEqual(rc, 0, out)
        text = open(self.out).read()
        self.assertIn('#define DW_NAME_FACE_CIRCLES "Zz Script"\n', text)
        self.assertIn('#define DW_NAME_FACE_ROTOR "Abcdefghijklmnopqrstuvwx"\n', text)
        self.assertIn('#define DW_NAME_GROUP_CIRCLES "Zz\'s group"\n', text)
        self.assertIn('#define DW_NAME_THEME "Zz Theme"\n', text)
        defs = re.findall(r'^#define (DW_NAME_\w+) ', text, re.M)
        self.assertEqual(len(defs), 1 + len(table_keys()) + 4)
        self.assertTrue(text.startswith('/*'), text)   # the do-not-edit comment first
        self.assertIn('do not edit', text.lower())
        self.assertNotIn(';', text.split('*/')[0])   # one string, not a ';'-joined list
        self.assertNotIn('\n;', text)

    def test_names_sorted_by_key(self):
        art = self.art({'theme': 'T', 'group:faces': 'G', 'face:wall': 'W', 'face:amber': 'A'})
        rc, out = self.names(art)
        self.assertEqual(rc, 0, out)
        defs = re.findall(r'^#define (DW_NAME_\w+) ', open(self.out).read(), re.M)
        self.assertEqual(defs, ['DW_NAME_FACE_AMBER', 'DW_NAME_FACE_WALL', 'DW_NAME_GROUP_FACES', 'DW_NAME_THEME'])

    def test_names_bad(self):
        many = {'face:' + k: 'x' for k in table_keys()}
        many.update({'group:' + g: 'x' for g in GROUPS})
        many['theme'] = 'x'
        cases = [
            ({'face:rotor': 'a"b'}, 'face:rotor'),
            ({'face:rotor': 'a\\b'}, 'face:rotor'),
            ({'face:rotor': 'café'}, 'face:rotor'),
            ({'theme': 'A' * 25}, 'theme'),
            ({'group:modern': ''}, 'group:modern'),
            ({'face:wall': ' Wall'}, 'face:wall'),
            ({'face:wall': 'Wall '}, 'face:wall'),
            ({'face:nosuch': 'X'}, 'face:nosuch'),
            ({'colour': 'X'}, 'colour'),
            ({'face:wall': 7}, 'face:wall'),
            (['theme', 'X'], 'names'),
            (dict(many, **{'face:x%d' % i: 'x' for i in range(33 - len(many))}), 'names'),
        ]
        self.assertEqual(len(cases[-1][0]), 33)
        for names, key in cases:
            art = self.art(names)
            for mode in ('validate', 'names'):
                rc, out = self.cmake(mode, art, OUT=self.out)
                self.assertNotEqual(rc, 0, '%s %r: %s' % (mode, names, out))
                self.assertIn(key, out, '%s %r' % (mode, names))
                self.assertFalse(os.path.exists(self.out), '%s %r wrote a header' % (mode, names))

    def test_names_trigraph(self):
        art = self.art({'face:wall': 'a??b'})
        for mode in ('validate', 'names'):
            rc, out = self.cmake(mode, art, OUT=self.out)
            self.assertNotEqual(rc, 0, out)
            self.assertIn('face:wall', out)
        self.assertFalse(os.path.exists(self.out))
        rc, out = self.names(self.art({'face:wall': 'a?b?'}))
        self.assertEqual(rc, 0, out)

    def test_names_absent(self):
        art = self.art()
        rc, out = self.names(art)
        self.assertEqual(rc, 0, out)
        self.assertFalse(os.path.exists(self.out))
        self.assertFalse(os.path.exists(os.path.dirname(self.out)) and os.listdir(os.path.dirname(self.out)))
        # with names, a rerun leaves the header (and so its stamp) alone
        art = self.art({'theme': 'Zz'})
        rc, out = self.names(art)
        self.assertEqual(rc, 0, out)
        os.utime(self.out, ns=(1_000_000_000_000_000_000, 1_000_000_000_000_000_000))
        rc, out = self.names(art)
        self.assertEqual(rc, 0, out)
        self.assertEqual(os.stat(self.out).st_mtime_ns, 1_000_000_000_000_000_000)
        # a changed value rewrites it
        rc, out = self.names(self.art({'theme': 'Zy'}))
        self.assertEqual(rc, 0, out)
        self.assertIn('"Zy"', open(self.out).read())

    def test_names_removed(self):
        rc, out = self.names(self.art({'theme': 'Zz'}))
        self.assertEqual(rc, 0, out)
        self.assertTrue(os.path.isfile(self.out))
        rc, out = self.names(self.art())
        self.assertEqual(rc, 0, out)
        self.assertFalse(os.path.exists(self.out))


@unittest.skipUnless(shutil.which('cmake'), 'needs cmake')
class DwStageSwaps(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='dwstage-swaps-')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.src = os.path.join(self.tmp, 'generic')
        os.makedirs(self.src)
        self.generic = {
            'dw_doors_m.bin': lvgl(466, 20), 'dw_b5_box.bin': lvgl(120, 30), 'dw_doors_c.bin': lvgl(466, 21),
            'dw_th_b5.bin': thumb(), 'dw_th_d3.bin': thumb(), 'dw_chime.pcm': bytes(102400),
            'dw_ping.pcm': bytes(6400), 'dw_glow.bin': lvgl(10, 10, cf=0x0e, bpp=1),
        }
        for n, data in self.generic.items():
            open(os.path.join(self.src, n), 'wb').write(data)

    def art(self, files, names=None):
        """files: {name: bytes}; replaces lists them in order."""
        art = os.path.join(self.tmp, 'art')
        shutil.rmtree(art, ignore_errors=True)
        os.makedirs(os.path.join(art, 'assets'))
        d = {'theme': 'dw', 'replaces': list(files), 'layers': 'layers.json'}
        if names is not None:
            d['names'] = names
        json.dump(d, open(os.path.join(art, 'over' + 'lay.json'), 'w'))
        for n, data in files.items():
            open(os.path.join(art, 'assets', n), 'wb').write(data)
        return art

    def cmake(self, mode, art):
        stage = os.path.join(self.tmp, 'stage', 'assets')
        r = subprocess.run(['cmake', '-DMODE=' + mode, '-DSRC=' + self.src, '-DART=' + art, '-DSTAGE=' + stage,
                            '-P', STAGE], capture_output=True, text=True)
        return r.returncode, ' '.join((r.stdout + r.stderr).split()), stage

    def staged(self, art, files):
        for mode in ('validate', 'stage'):
            rc, out, stage = self.cmake(mode, art)
            self.assertEqual(rc, 0, '%s: %s' % (mode, out))
        for n, data in files.items():
            self.assertEqual(open(os.path.join(stage, n), 'rb').read(), data, n)
        for n in set(self.generic) - set(files):
            self.assertEqual(open(os.path.join(stage, n), 'rb').read(), self.generic[n], n)

    def refused(self, art, name):
        rc, out, stage = self.cmake('stage', art)
        self.assertNotEqual(rc, 0, out)
        self.assertIn(name, out)
        return out

    def test_pcm_swap(self):
        chime = bytes([1, 2]) * 51200
        self.assertEqual(len(chime), 102400)
        self.staged(self.art({'dw_chime.pcm': chime}), {'dw_chime.pcm': chime})
        self.staged(self.art({'dw_chime.pcm': bytes(110080)}), {'dw_chime.pcm': bytes(110080)})
        self.staged(self.art({'dw_chime.pcm': bytes(2)}), {'dw_chime.pcm': bytes(2)})
        for data in (bytes(110082), bytes(1001), b''):
            self.refused(self.art({'dw_chime.pcm': data}), 'dw_chime.pcm')
        os.remove(os.path.join(self.src, 'dw_chime.pcm'))   # no generic twin
        self.refused(self.art({'dw_chime.pcm': chime}), 'dw_chime.pcm')

    def test_pcm_not_listed_refused(self):
        rc, out, _ = self.cmake('validate', self.art({'dw_ping.pcm': bytes(6400)}))
        self.assertNotEqual(rc, 0, out)
        self.assertIn('replaces dw_ping.pcm: an overlay may replace only', out)

    def test_thumb_swap(self):
        th = bytearray(thumb())
        th[100] ^= 0x55
        th = bytes(th)
        self.assertEqual(len(th), 13884)
        self.staged(self.art({'dw_th_b5.bin': th, 'dw_th_d3.bin': th}), {'dw_th_b5.bin': th, 'dw_th_d3.bin': th})
        narrow = thumb(67) + bytes(13884 - len(thumb(67)))   # the same length, a 67-wide header
        self.refused(self.art({'dw_th_d3.bin': narrow}), 'dw_th_d3.bin')

    def test_all_six(self):
        files = {n: self.generic[n][:-1] + b'\x01' for n in ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin',
                                                              'dw_th_b5.bin', 'dw_th_d3.bin', 'dw_chime.pcm')}
        self.staged(self.art(files, names={'theme': 'Zz'}), files)

    def test_old_overlay(self):
        files = {n: self.generic[n][:-1] + b'\x07' for n in SWAP3}
        self.staged(self.art(files), files)


class DwStageTables(unittest.TestCase):
    def test_known_keys_match_table(self):
        m = re.search(r'^set\(name_keys ([^)]*)\)', open(STAGE).read(), re.M)
        self.assertIsNotNone(m, 'dw_stage.cmake has no set(name_keys ...)')
        keys = m.group(1).split()
        want = ['theme'] + ['face:' + k for k in table_keys()] + ['group:' + g for g in GROUPS]
        self.assertEqual(len(table_keys()), 16)
        self.assertEqual(sorted(keys), sorted(want))
        self.assertEqual(len(keys), len(set(keys)))

    def test_generic_rows_still_three(self):
        spec = importlib.util.spec_from_file_location('check_apps_t9', os.path.join(ROOT, 'tools', 'check_apps.py'))
        ca = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        spec.loader.exec_module(ca)
        self.assertEqual(ca.DW_SWAP, ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin', 'dw_th_b5.bin',
                                      'dw_th_d3.bin', 'dw_chime.pcm'))
        self.assertEqual(ca.DW_SWAP_GENERIC, SWAP3)
        rows = json.load(open(LAYERS))['rows']
        self.assertEqual(ca.generic_rows_problems(rows), [])
        self.assertNotEqual(ca.generic_rows_problems([{'out': 'generic', 'file': n} for n in ca.DW_SWAP]), [])
        self.assertEqual(ca.replaces_problems({'replaces': list(ca.DW_SWAP)}), [])


if __name__ == '__main__':
    unittest.main()
