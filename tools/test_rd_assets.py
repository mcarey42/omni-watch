#!/usr/bin/env python3
"""Tests for the Mining Ship asset pipeline (rd wave 1 Task 7) and its publishability guard (ruling R159). Run from the
repo root:
    python -m unittest tools/test_rd_assets.py

  - the table (tools/export/rd_layers.json): every row names a face key from rd/faces.h's list or "shared", and every
    packed row's name passes the rd_ name rule (committed bytes, the brass backgrounds, keep their own names);
  - the drawn-text guard: rd_pack.py refuses a row whose text or glyph field is a piece-built forbidden sample, and
    rd_export.mjs refuses it too, exiting non-zero and writing nothing (PR37);
  - the committed atlases: rd_f_s132's 12 glyphs, its fixed advance and its dash at 0.66 x the digit's; rd_f_vt28's
    two planes of equal cells;
  - the budget: a face's own files over 500 000 B fail, 500 000 B pass;
  - facepack.build_atlas's advance override (PR29): without one the cells are today's, with one only that glyph's
    advance moves;
  - two fresh exports of the shared rows are byte-equal (needs node, tools/node_modules, Chrome and the design
    directory; skipped without them);
  - R159: the separator class in both twins, the binary short-token rule (word bounds, runs of 8 or more), the
    neutrality patterns catching an embedded theme prefix, and tools/rd_assets.sh --guard on scratch repos: it answers
    OK on a clean one; BAD on a design or research blob in the history, on another ref or dangling, on a forbidden
    term in what a push carries (the HEAD tree, the index, any blob since the merge base: R161) or in a commit message
    since the merge base with main, and on a shared blob not on the R160 allow-list; and it refuses (non-zero, never
    OK) a shallow, partial (promisor pack or a real --filter clone) or alternates checkout, an fsck failure, an
    environment override, a missing merge base, a missing, unreadable or symlinked design path;
  - the budget counts every file with a face's prefix (M4), its fail line under the partition less 256 KiB (R69), and
    the manifest points at the OFL notices (M6).
No term and no piece of one is in this file (public release Task 5): the term cases read rd_terms.test_corpus(), and
nothing written to disk holds one: files, tables and scratch repos carry the neutral MARK (rd_terms.test_marker) instead.
tools/rd_terms.py is private (found through tools/terms.py): in a public tree, where it is absent, the term and guard
cases skip; anywhere else a missing module fails the import.
Fix round 2 adds: replace refs (a blob and a graft) and info/grafts refused, git configuration from the environment
refused, a history-only assets binary scanned, and a blob scanned at every place it appears.
"""
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.dont_write_bytecode = True
import facepack  # noqa: E402
import terms  # noqa: E402

rd_terms = terms.load(ROOT, 'rd_terms')   # None in a public tree; TermsMissing anywhere else

TABLE = os.path.join(ROOT, 'tools', 'export', 'rd_layers.json')
ASSETS = os.path.join(ROOT, 'firmware', 'apps', 'rd', 'assets')
PACK = os.path.join(ROOT, 'tools', 'rd_pack.py')
SCRIPT = os.path.join(ROOT, 'tools', 'rd_assets.sh')
EXPORT = os.path.join(ROOT, 'tools', 'rd_export.mjs')
FACES_H = os.path.join(ROOT, 'firmware', 'apps', 'rd', 'components', 'rd_core', 'include', 'rd', 'faces.h')
PARTITIONS = os.path.join(ROOT, 'firmware', 'common', 'partitions.csv')
CHROME = os.environ.get('CHROME', '/usr/bin/google-chrome')
KINDS = ('rgb565', 'rgb565a8', 'a8', 'a8_strip', 'atlas', 'atlas2', 'thumb',
         'a8_rows',   # rd wave 2 Task 12: ARW1 (Curry Clock's steam loop, rd_cu_wisp)
         'shared_ui')  # rim keyboard Task 8: the keyboard's shared assets (committed bytes, watch_ui/assets)
# this task's rows (spec section 12.2; the brief's list)
SHARED_ROWS = ('rd_f_s132', 'rd_f_s132o', 'rd_f_s64', 'rd_f_s30', 'rd_f_vt28', 'rd_f_vt20', 'rd_f_vt132', 'rd_f_ch18',
               'rd_f_ch13', 'rd_grime', 'rd_th_stasis', 'rd_th_terminal', 'rd_th_deck', 'rd_th_hull', 'rd_th_bunk',
               'rd_th_curry', 'rd_th_digital', 'rd_th_bay', 'rd_th_drift', 'rd_th_console', 'rd_th_scope',
               'rd_th_vending', 'rd_th_bowl', 'rd_th_mopbot')


def face_keys():
    """rd/faces.h's face ids as keys: RD_F_STASIS -> stasis (the double-underscore counts excluded)."""
    text = get(FACES_H)
    body = re.search(r'enum rd_face\s*\{(.*?)\}', text, re.S).group(1)
    return {m.lower() for m in re.findall(r'\bRD_F_([A-Z]+)\b', body)}


MARK = terms.NEUTRAL_MARK   # the tests' neutral marker (rd_terms.test_marker): no real term is written anywhere


def put(path, data):
    with open(path, 'wb' if isinstance(data, bytes) else 'w') as f:
        f.write(data)


def get(path, mode='r'):
    with open(path, mode) as f:
        return f.read()


def run(args, env=None, **kw):
    e = dict(os.environ)
    for k in list(e):
        if k.startswith(('RD_', 'GIT_')):
            e.pop(k)
    e['RD_TERMS_TEST_MARKER'] = MARK
    e.update(env or {})
    r = subprocess.run(args, capture_output=True, text=True, env=e, cwd=ROOT, **kw)
    return r.returncode, r.stdout + r.stderr


def git(repo, *a, check=True, input=None):
    return subprocess.run(['git', '-C', repo, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'commit.gpgsign=false',
                           '-c', 'init.defaultBranch=main'] + list(a), check=check, capture_output=True, text=True,
                          input=input)


def export_ready():
    return (shutil.which('node') and os.path.isfile(CHROME)
            and os.path.isdir(os.path.join(ROOT, 'tools', 'node_modules', 'playwright-core'))
            and os.path.isfile(os.path.join(ROOT, 'tools', 'fonts', 'SairaStencilOne-Regular.ttf')))


class RdTable(unittest.TestCase):
    def test_rd_layers_rows_named(self):
        rows = json.loads(get(TABLE))['rows']
        keys = face_keys()
        self.assertEqual(len(keys), 14, keys)                 # ids 0-13 (spec section 6)
        rule = facepack.NAME_RULE('rd_')
        for r in rows:
            self.assertIn(r['face'], keys | {'shared'}, r['name'])
            self.assertIn(r['kind'], KINDS, r['name'])
            self.assertTrue(r.get('source'), '%s cites no source' % r['name'])
            if 'crc32' not in r:                              # committed bytes keep their own names
                self.assertIsNotNone(rule.fullmatch(r['name']), r['name'])
        names = [r['name'] for r in rows]
        self.assertEqual(len(names), len(set(names)))
        for n in SHARED_ROWS:
            self.assertIn(n, names)
            self.assertEqual(next(r for r in rows if r['name'] == n)['face'], 'shared', n)

    def test_rd_pack_refuses_term(self):
        with tempfile.TemporaryDirectory() as td:
            assets, out = os.path.join(td, 'assets'), os.path.join(td, 'out')
            os.mkdir(assets)
            os.mkdir(out)
            put(os.path.join(out, 'layers.json'), json.dumps({'rows': []}))
            for field in ('text', 'glyphs'):
                row = {'name': 'rd_x', 'face': 'shared', 'kind': 'a8', 'source': 'test', field: MARK}
                put(os.path.join(td, 't.json'), json.dumps({'rows': [row]}))
                rc, msg = run([sys.executable, PACK, 'pack', '--table', os.path.join(td, 't.json'), '--out', out,
                               '--assets', assets])
                self.assertNotEqual(rc, 0, msg)
                self.assertIn('forbidden', msg)
                self.assertEqual(os.listdir(assets), [], 'nothing written')

    def test_rd_atlas_cells(self):
        man = {e['name']: e for e in json.loads(get(os.path.join(ASSETS, 'manifest.json')))['assets']}
        s = man['rd_f_s132']
        self.assertEqual(s['glyphs'], '0123456789:-')
        self.assertEqual(len(s['cells']), 12)
        digit = {c['adv'] for c in s['cells'][:10]}
        self.assertEqual(len(digit), 1, 'a fixed digit advance')
        adv = digit.pop()
        self.assertEqual(s['cells'][11]['adv'], int(round(0.66 * adv)), 'the dash at 0.66 x the digit advance')
        data = get(os.path.join(ASSETS, 'rd_f_s132.bin'), 'rb')
        self.assertEqual(facepack.read_bin(os.path.join(ASSETS, 'rd_f_s132.bin'))[0], 'A8')
        self.assertEqual(len(data) % 2, 0)
        v = man['rd_f_vt28']
        self.assertEqual(v['out'], 'atlas2')
        a2 = facepack.atlas2_unpack(get(os.path.join(ASSETS, 'rd_f_vt28.bin'), 'rb'))
        self.assertEqual((a2['w'], a2['h']), (v['w'], v['h']))
        self.assertEqual(len(a2['glow']), v['w'] * v['h'])
        self.assertEqual(len(a2['core']), v['w'] * v['h'])
        self.assertEqual(a2['cells'], v['cells'], 'one cell table for both planes')
        self.assertNotEqual(a2['glow'], a2['core'])
        self.assertGreater(sum(1 for b in a2['glow'] if b), sum(1 for b in a2['core'] if b), 'the glow spreads wider')

    def test_rd_budget_face_cap(self):
        with tempfile.TemporaryDirectory() as td:
            assets = os.path.join(td, 'assets')
            os.mkdir(assets)
            row = {'name': 'rd_st_x', 'face': 'stasis', 'kind': 'a8', 'source': 'test'}
            put(os.path.join(td, 't.json'), json.dumps({'rows': [row]}))
            for size, want in ((500001, 1), (500000, 0)):
                with open(os.path.join(assets, 'rd_st_x.bin'), 'wb') as f:
                    f.truncate(size)
                rc, msg = run([sys.executable, PACK, 'budget', '--assets', assets, '--partitions', PARTITIONS,
                               '--table', os.path.join(td, 't.json')])
                self.assertEqual(rc, want, msg)
                self.assertEqual('face stasis is 500001 B' in msg, want == 1, msg)

    def test_rd_budget_counts_unlisted_face_files(self):
        """M4: every file carrying a face's prefix counts against its cap, whether the table lists it or not."""
        with tempfile.TemporaryDirectory() as td:
            assets = os.path.join(td, 'assets')
            os.mkdir(assets)
            row = {'name': 'rd_cu_lid', 'face': 'curry', 'kind': 'a8', 'source': 'test'}
            put(os.path.join(td, 't.json'), json.dumps({'rows': [row]}))
            for f, size in (('rd_cu_lid.bin', 300000), ('rd_cu_stray.bin', 200001)):
                with open(os.path.join(assets, f), 'wb') as fh:
                    fh.truncate(size)
            rc, msg = run([sys.executable, PACK, 'budget', '--assets', assets, '--partitions', PARTITIONS,
                           '--table', os.path.join(td, 't.json')])
            self.assertEqual(rc, 1, msg)
            self.assertIn('face curry is 500001 B', msg)
            os.remove(os.path.join(assets, 'rd_cu_lid.bin'))
            with open(os.path.join(assets, 'rd_cu_stray.bin'), 'wb') as fh:
                fh.truncate(500001)                                  # a table listing nothing for the face at all
            put(os.path.join(td, 't.json'), json.dumps({'rows': []}))
            rc, msg = run([sys.executable, PACK, 'budget', '--assets', assets, '--partitions', PARTITIONS,
                           '--table', os.path.join(td, 't.json')])
            self.assertEqual(rc, 1, msg)

    def test_rd_budget_fail_line_under_partition(self):
        """M4, R69: a fail line above the assets partition less 256 KiB is refused; the default is under it."""
        with tempfile.TemporaryDirectory() as td:
            assets = os.path.join(td, 'assets')
            os.mkdir(assets)
            put(os.path.join(td, 't.json'), json.dumps({'rows': []}))
            size = facepack.partition_size(PARTITIONS)
            self.assertLessEqual(9000000, size - 256 * 1024)
            for fail, want in ((size - 256 * 1024 + 1, 1), (size - 256 * 1024, 0)):
                rc, msg = run([sys.executable, PACK, 'budget', '--assets', assets, '--partitions', PARTITIONS,
                               '--table', os.path.join(td, 't.json'), '--fail', str(fail)])
                self.assertEqual(rc, want, msg)
                self.assertEqual('R69' in msg, want == 1, msg)

    def test_rd_manifest_licence_pointer(self):
        """M6: the manifest points at the committed OFL notices."""
        man = json.loads(get(os.path.join(ASSETS, 'manifest.json')))
        self.assertIn('tools/font_licences/', man.get('notices', ''))
        self.assertIn('SIL Open Font License 1.1', man['notices'])
        for f in ('SairaStencilOne-OFL.txt', 'VT323-OFL.txt', 'ChakraPetch-OFL.txt'):
            self.assertIn('SIL OPEN FONT LICENSE Version 1.1', get(os.path.join(ROOT, 'tools', 'font_licences', f)))

    def test_rd_budget_lines(self):
        """The report, warn and fail lines (6 000 000 at wave 2, 7 000 000, 9 000 000 B) are rd_pack.py budget's defaults."""
        with tempfile.TemporaryDirectory() as td:
            assets = os.path.join(td, 'assets')
            os.mkdir(assets)
            put(os.path.join(td, 't.json'), json.dumps({'rows': []}))
            for size, rc_want, word in ((6000000, 0, 'REPORT'), (7000000, 0, 'WARN'), (9000000, 1, 'ASSETS BAD')):
                with open(os.path.join(assets, 'x.bin'), 'wb') as f:
                    f.truncate(size)
                rc, msg = run([sys.executable, PACK, 'budget', '--assets', assets, '--partitions', PARTITIONS,
                               '--table', os.path.join(td, 't.json')])
                self.assertEqual(rc, rc_want, msg)
                self.assertIn(word, msg)

    def test_facepack_advance_override(self):
        def glyph(gw, gh):
            return gw, gh, [bytearray((255, 255, 255, 200) * gw) for _ in range(gh)]
        gl = [glyph(3, 5), glyph(4, 5), glyph(2, 3)]
        # today's cells (b5e9481): left to right, 1 px apart, baseline at the image's bottom, advance width + 1 or fixed
        today = [dict(x=0, y=0, w=3, h=5, ox=0, oy=-5, adv=4), dict(x=4, y=0, w=4, h=5, ox=0, oy=-5, adv=5),
                 dict(x=9, y=0, w=2, h=3, ox=0, oy=-3, adv=3)]
        w, h, rows, cells = facepack.build_atlas(gl, '01-')
        self.assertEqual((w, h, cells), (11, 5, today))
        _, _, _, fixed = facepack.build_atlas(gl, '01-', fixed_advance=9)
        self.assertEqual([c['adv'] for c in fixed], [9, 9, 9])
        _, _, rows2, over = facepack.build_atlas(gl, '01-', fixed_advance=9, advance_override={'-': 6})
        self.assertEqual([c['adv'] for c in over], [9, 9, 6])
        self.assertEqual([dict(c, adv=0) for c in over], [dict(c, adv=0) for c in fixed], 'only the advance moves')
        self.assertEqual([bytes(r) for r in rows2], [bytes(r) for r in rows])
        _, _, _, nat = facepack.build_atlas(gl, '01-', advance_override={'-': 6})
        self.assertEqual([c['adv'] for c in nat], [4, 5, 6])


@unittest.skipUnless(rd_terms, 'no tools/rd_terms.py (a public tree)')
class RdTerms(unittest.TestCase):
    """R159 (2)-(4): planted hits (the samples: rd_terms.test_corpus())."""

    def setUp(self):
        self.corpus = rd_terms.test_corpus()

    def test_separator_class_python(self):
        for sep, sample in zip(self.corpus['seps'], self.corpus['sep_hits']):
            self.assertTrue(rd_terms.RD_FORBIDDEN_TEXT.search(sample), repr(sep))
        self.assertEqual(len(self.corpus['sep_hits']), len(self.corpus['seps']))
        self.assertFalse(rd_terms.RD_FORBIDDEN_TEXT.search(self.corpus['sep_miss']), 'a colon is no separator')

    @unittest.skipUnless(shutil.which('node'), 'needs node')
    def test_separator_class_js(self):
        samples = self.corpus['sep_hits'] + [self.corpus['sep_miss']]
        js = ("import {RD_FORBIDDEN} from %s; console.log(JSON.stringify(%s.map(s => RD_FORBIDDEN.test(s))));"
              % (json.dumps('./tools/export/rd_terms.mjs'), json.dumps(samples)))
        rc, out = run(['node', '--input-type=module', '-e', js])
        self.assertEqual(rc, 0, out)
        self.assertEqual(json.loads(out), [True] * len(self.corpus['seps']) + [False])

    def test_bytes_short_token_bounds(self):
        for data, want, what in self.corpus['short_runs']:
            self.assertEqual(bool(rd_terms.rd_bytes_hits(data)), want, what)
        self.assertEqual(len(self.corpus['short_runs']), 5)
        self.assertEqual(rd_terms.SHORT_RUN_MIN, 8)
        # source text keeps letter bounds: the token after an underscore still hits there
        self.assertTrue(rd_terms.RD_FORBIDDEN_TEXT.search(self.corpus['short_text']))

    def test_docstring_calibration_note(self):
        self.assertIn('calibration', rd_terms.__doc__)


class RdNeutrality(unittest.TestCase):
    """R159 (4): the neutrality patterns (no list needed, so this runs in a public tree too)."""

    def test_neutrality_embedded_prefix(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('check_apps', os.path.join(ROOT, 'tools', 'check_apps.py'))
        ca = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ca)
        for text in ('static int s_dw_x;', 'g_rd_face = 1;', 'x = s_fk_y;', '_DW_N'):
            self.assertTrue(ca.NEUTRALITY.search(text), text)
        for text in ('static int s_dw_x;', 'g_rd_face = 1;', 'k_RD_N'):
            self.assertTrue(ca.FACEKIT_NEUTRALITY.search(text), text)
        for text in ('xdw_y', 'bird_x', '3rd_x'):
            self.assertFalse(ca.FACEKIT_NEUTRALITY.search(text), text)
        self.assertFalse(ca.FACEKIT_NEUTRALITY.search('int fk_x;'))


@unittest.skipUnless(rd_terms, 'no tools/rd_terms.py (a public tree: the guard exits 77)')
class RdGuard(unittest.TestCase):
    """tools/rd_assets.sh --guard on scratch repos (RD_MAIN_REPO, RD_DESIGN, RD_RESEARCH): never the real repo."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='rdguard-')
        self.design = os.path.join(self.tmp, 'design')
        self.research = os.path.join(self.tmp, 'research')
        os.makedirs(os.path.join(self.design, 'mockups'))
        os.makedirs(self.research)
        put(os.path.join(self.design, 'mockups', 'faces.js'), '// a mockup\nvar x = 1;\n')
        put(os.path.join(self.design, 'notes.md'), 'notes\n')
        put(os.path.join(self.research, 'brief.md'), 'a brief\n')
        self.repo = os.path.join(self.tmp, 'repo')
        os.makedirs(self.repo)
        git(self.repo, 'init', '-q')
        put(os.path.join(self.repo, 'a.txt'), 'hello\n')
        git(self.repo, 'add', 'a.txt')
        git(self.repo, 'commit', '-q', '-m', 'base')
        git(self.repo, 'checkout', '-q', '-b', 'work')
        put(os.path.join(self.repo, 'b.txt'), 'work\n')
        git(self.repo, 'add', 'b.txt')
        git(self.repo, 'commit', '-q', '-m', 'work')

    def tearDown(self):
        for root, dirs, files in os.walk(self.tmp):
            for n in dirs + files:
                p = os.path.join(root, n)
                if not os.path.islink(p):
                    os.chmod(p, 0o755)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def guard(self, repo=None, **env):
        e = {'RD_MAIN_REPO': repo or self.repo, 'RD_DESIGN': self.design, 'RD_RESEARCH': self.research}
        e.update(env)
        return run([SCRIPT, '--guard'], env=e)

    def assertRefused(self, rc, out, word=None):
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn('GUARD OK', out)
        if word:
            self.assertIn(word, out)

    def test_guard_clean_ok(self):
        rc, out = self.guard()
        self.assertEqual(rc, 0, out)
        self.assertIn('GUARD OK', out)
        self.assertIn('3 files', out)

    def test_guard_design_blob_in_history(self):
        shutil.copy(os.path.join(self.design, 'mockups', 'faces.js'), os.path.join(self.repo, 'x.js'))
        git(self.repo, 'add', 'x.js')
        git(self.repo, 'commit', '-q', '-m', 'oops')
        git(self.repo, 'rm', '-q', 'x.js')
        git(self.repo, 'commit', '-q', '-m', 'undo')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD mockups/faces.js')

    EARTH = os.path.join(ROOT, 'design', 'assets', 'earth-day-1024.jpg')

    def commit_on_main(self, path, src):
        """A commit on main (the base) holding src's bytes at path; work rebased onto it."""
        git(self.repo, 'checkout', '-q', 'main')
        os.makedirs(os.path.dirname(os.path.join(self.repo, path)), exist_ok=True)
        shutil.copy(src, os.path.join(self.repo, path))
        git(self.repo, 'add', path)
        git(self.repo, 'commit', '-q', '-m', 'the repo\'s own')
        git(self.repo, 'checkout', '-q', 'work')
        git(self.repo, 'rebase', '-q', 'main')

    @unittest.skipUnless(os.path.isfile(EARTH), 'needs the Omni\'s design/assets/earth-day-1024.jpg')
    def test_guard_allow_list_shared(self):
        """R160: one of the two allow-listed blobs (the Omni's earth texture, byte for byte) is SHARED, not BAD."""
        shutil.copy(self.EARTH, os.path.join(self.design, 'mockups', 'earth.jpg'))
        self.commit_on_main('design/assets/earth-day-1024.jpg', self.EARTH)
        rc, out = self.guard()
        self.assertEqual(rc, 0, out)
        self.assertIn('GUARD SHARED mockups/earth.jpg: blob 6f78f20bd45aff68aba95af47462ee3a3469ded4 is on the R160 '
                      'allow-list', out)
        self.assertIn('GUARD OK', out)

    def test_guard_third_shared_blob_bad(self):
        """R160: a design file the base tracks outside design/themes/ but not on the allow-list stays BAD, and no
        environment variable widens the list."""
        self.commit_on_main('design/assets/notes.md', os.path.join(self.design, 'notes.md'))
        for env in ({}, {'RD_GUARD_BASE': git(self.repo, 'rev-parse', 'main').stdout.strip()},
                    {'SHARED_BLOBS': git(self.repo, 'hash-object', os.path.join(self.design, 'notes.md')).stdout.strip()}):
            rc, out = self.guard(**env)
            self.assertRefused(rc, out, 'GUARD BAD notes.md')

    def test_guard_design_blob_on_another_ref(self):
        """M3: a design blob reachable only from another branch is BAD."""
        git(self.repo, 'checkout', '-q', '-b', 'side', 'main')
        shutil.copy(os.path.join(self.design, 'notes.md'), os.path.join(self.repo, 'n.md'))
        git(self.repo, 'add', 'n.md')
        git(self.repo, 'commit', '-q', '-m', 'side')
        git(self.repo, 'checkout', '-q', 'work')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD notes.md')

    def test_guard_design_blob_dangling(self):
        """M3: a design blob in the object store that nothing references is BAD."""
        git(self.repo, 'hash-object', '-w', os.path.join(self.research, 'brief.md'))
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD brief.md')

    def test_guard_research_blob(self):
        shutil.copy(os.path.join(self.research, 'brief.md'), os.path.join(self.repo, 'r.md'))
        git(self.repo, 'add', 'r.md')
        git(self.repo, 'commit', '-q', '-m', 'r')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD brief.md')

    def test_guard_tree_scan_planted(self):
        put(os.path.join(self.repo, 'c.md'), 'line\nthe ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'c')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'rd term in HEAD c.md:2')

    def test_guard_term_committed_then_dropped_from_index(self):
        """R161 (I1): a committed term stays in what is pushed after `git rm --cached`."""
        put(os.path.join(self.repo, 'c.md'), 'the ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'c')
        git(self.repo, 'rm', '-q', '--cached', 'c.md')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD rd term in HEAD c.md:1')

    def test_guard_term_in_history(self):
        """R161 (I2): a term added in one commit and removed in the next is still pushed."""
        put(os.path.join(self.repo, 'c.md'), 'the ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'c')
        put(os.path.join(self.repo, 'c.md'), 'clean\n')
        git(self.repo, 'commit', '-q', '-am', 'clean')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD rd term in history c.md:1')

    def test_guard_term_staged_only(self):
        """The index too: a staged term not yet committed."""
        put(os.path.join(self.repo, 'c.md'), 'the ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'GUARD BAD rd term in index c.md:1')

    def test_guard_binary_long_term(self):
        """M2: a tracked binary outside any assets image is scanned with the long terms; an assets image's payload is
        the built scan's, and a short token alone does not hit a binary."""
        put(os.path.join(self.repo, 'd.bin'), b'\x00\x01' + MARK.encode() + b'\x00')
        git(self.repo, 'add', 'd.bin')
        git(self.repo, 'commit', '-q', '-m', 'd')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'rd term in HEAD d.bin:0x')

    def test_guard_history_only_asset_binary(self):
        """Fix round 2: a binary that lived only in the history under an assets dir is scanned too."""
        os.makedirs(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets'))
        put(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets', 'p.bin'), b'\x00' + MARK.encode() + b'\x00')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '-q', '-m', 'p')
        git(self.repo, 'rm', '-q', 'firmware/apps/rd/assets/p.bin')
        git(self.repo, 'commit', '-q', '-m', 'gone')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'rd term in history firmware/apps/rd/assets/p.bin')

    def test_guard_blob_at_two_paths(self):
        """Fix round 2: a blob is scanned at every place it appears: under an assets dir and outside one, it is
        scanned (as the outside path)."""
        os.makedirs(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets'))
        data = b'\x00' + MARK.encode() + b'\x00'
        put(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets', 'p.bin'), data)
        put(os.path.join(self.repo, 'q.bin'), data)
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '-q', '-m', 'p')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'q.bin:0x')

    def test_guard_refuses_replace_blob(self):
        """Fix round 2: `git replace <marked> <clean>` would show the scan clean bytes while a push carries the
        marked ones: a non-empty refs/replace/ refuses."""
        put(os.path.join(self.repo, 'c.md'), 'the ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'c')
        marked = git(self.repo, 'rev-parse', 'HEAD:c.md').stdout.strip()
        clean = git(self.repo, 'hash-object', '-w', '--stdin', input='clean\n').stdout.strip()
        git(self.repo, 'replace', marked, clean)
        rc, out = self.guard()
        self.assertRefused(rc, out, 'replacement refs')

    def test_guard_refuses_replace_graft(self):
        put(os.path.join(self.repo, 'c.md'), 'the ' + MARK + '\n')
        git(self.repo, 'add', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'c')
        git(self.repo, 'rm', '-q', 'c.md')
        git(self.repo, 'commit', '-q', '-m', 'gone')
        git(self.repo, 'replace', '--graft', 'HEAD')                 # hides the marked commit from a view that follows it
        rc, out = self.guard()
        self.assertRefused(rc, out, 'replacement refs')

    def test_guard_refuses_info_grafts(self):
        head = git(self.repo, 'rev-parse', 'HEAD').stdout.strip()
        put(os.path.join(self.repo, '.git', 'info', 'grafts'), head + '\n')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'grafts')

    def test_guard_refuses_env_config(self):
        for env in ({'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'core.x', 'GIT_CONFIG_VALUE_0': 'y'},
                    {'GIT_CONFIG_KEY_0': 'core.x'}, {'GIT_CONFIG_PARAMETERS': "'core.x=y'"},
                    {'GIT_CONFIG_GLOBAL': '/dev/null'}, {'GIT_REPLACE_REF_BASE': 'refs/other/'},
                    {'GIT_NO_REPLACE_OBJECTS': '0'}, {'GIT_SHALLOW_FILE': '/dev/null'}):
            rc, out = self.guard(**env)
            self.assertRefused(rc, out, sorted(env)[0])
        rc, out = self.guard(GIT_NO_REPLACE_OBJECTS='1')            # the value the guard forces itself is accepted
        self.assertEqual(rc, 0, out)

    def test_guard_binary_rules_pass(self):
        os.makedirs(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets'))
        put(os.path.join(self.repo, 'firmware', 'apps', 'rd', 'assets', 'p.bin'),
            b'\x00' + MARK.encode() + b'\x00')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '-q', '-m', 'p')
        rc, out = self.guard()
        self.assertEqual(rc, 0, out)

    def test_guard_message_scan_planted(self):
        put(os.path.join(self.repo, 'e.txt'), 'e\n')
        git(self.repo, 'add', 'e.txt')
        git(self.repo, 'commit', '-q', '-m', 'feat: e\n\nabout ' + MARK)
        rc, out = self.guard()
        self.assertRefused(rc, out, 'rd term in a commit message')

    def test_guard_message_before_merge_base_not_scanned(self):
        git(self.repo, 'checkout', '-q', 'main')
        git(self.repo, 'commit', '-q', '--allow-empty', '-m', 'old ' + MARK)
        git(self.repo, 'checkout', '-q', 'work')
        rc, out = self.guard()
        self.assertEqual(rc, 0, out)

    def test_guard_refuses_without_main(self):
        git(self.repo, 'branch', '-q', '-D', 'main')
        rc, out = self.guard()
        self.assertRefused(rc, out, 'merge base')

    def test_guard_refuses_shallow(self):
        clone = os.path.join(self.tmp, 'shallow')
        git(self.tmp, 'clone', '-q', '--depth', '1', '--no-single-branch', 'file://' + self.repo, clone)
        rc, out = self.guard(clone)
        self.assertRefused(rc, out, 'shallow')

    def test_guard_refuses_partial(self):
        clone = os.path.join(self.tmp, 'partial')
        git(self.tmp, 'clone', '-q', '--no-checkout', 'file://' + self.repo, clone)
        pack = os.path.join(clone, '.git', 'objects', 'pack')
        os.makedirs(pack, exist_ok=True)
        open(os.path.join(pack, 'pack-0000000000000000000000000000000000000000.promisor'), 'w').close()
        rc, out = self.guard(clone)
        self.assertRefused(rc, out, 'partial')

    def test_guard_refuses_filter_clone(self):
        """M3: a real --filter=blob:none partial clone (served through the ext transport, whose upload-pack takes
        uploadpack.allowFilter on its command line: no config file is written). This git marks it with a promisor
        remote (remote.origin.promisor, partialclonefilter), the branch it refuses on."""
        clone = os.path.join(self.tmp, 'filtered')
        git(self.tmp, '-c', 'protocol.ext.allow=always', 'clone', '-q', '--no-checkout', '--filter=blob:none',
            'ext::git -c uploadpack.allowFilter=true upload-pack ' + self.repo, clone)
        self.assertEqual(git(clone, 'config', '--get', 'remote.origin.partialclonefilter').stdout.strip(), 'blob:none')
        rc, out = self.guard(clone)
        self.assertRefused(rc, out, 'partial')

    def test_guard_refuses_alternates(self):
        clone = os.path.join(self.tmp, 'shared')
        git(self.tmp, 'clone', '-q', '--shared', self.repo, clone)
        rc, out = self.guard(clone)
        self.assertRefused(rc, out, 'alternates')

    def test_guard_refuses_fsck_failure(self):
        """M3: corruption only fsck sees (an unreachable object; HEAD and its tree stay readable)."""
        sha = git(self.repo, 'hash-object', '-w', '--stdin', input='an unreachable blob\n').stdout.strip()
        p = os.path.join(self.repo, '.git', 'objects', sha[:2], sha[2:])
        os.chmod(p, 0o644)
        put(p, b'garbage')
        self.assertEqual(git(self.repo, 'cat-file', '-e', 'HEAD^{tree}', check=False).returncode, 0)
        rc, out = self.guard()
        self.assertRefused(rc, out, 'fsck')

    def test_guard_refuses_env_override(self):
        rc, out = self.guard(GIT_DIR=os.path.join(self.repo, '.git'))
        self.assertRefused(rc, out, 'GIT_DIR')

    def test_guard_refuses_missing_design(self):
        rc, out = self.guard(RD_DESIGN=os.path.join(self.tmp, 'nowhere'))
        self.assertRefused(rc, out)

    def test_guard_refuses_unreadable_file(self):
        p = os.path.join(self.design, 'notes.md')
        os.chmod(p, 0)
        if os.access(p, os.R_OK):
            self.skipTest('running as a user who reads anything')
        rc, out = self.guard()
        self.assertRefused(rc, out)

    def test_guard_refuses_symlink(self):
        os.symlink(os.path.join(self.tmp, 'repo', 'a.txt'), os.path.join(self.research, 'link'))
        rc, out = self.guard()
        self.assertRefused(rc, out, 'symlink')

    def test_design_symlink_root_scanned(self):
        """public release Task 6: a design or research root that is itself a symlink (the owner's design/themes/rd and
        research/rd point at the real packages) is scanned through: pass 1 sees a planted file behind it, both when
        RD_DESIGN/RD_RESEARCH name the links and when they are unset and the roots are derived by theme id."""
        shutil.copy(os.path.join(self.design, 'notes.md'), os.path.join(self.repo, 'n.md'))
        shutil.copy(os.path.join(self.research, 'brief.md'), os.path.join(self.repo, 'b.md'))
        git(self.repo, 'add', 'n.md', 'b.md')
        git(self.repo, 'commit', '-q', '-m', 'planted')
        dl, rl = os.path.join(self.tmp, 'design-link'), os.path.join(self.tmp, 'research-link')
        os.symlink(self.design, dl)
        os.symlink(self.research, rl)
        rc, out = self.guard(RD_DESIGN=dl, RD_RESEARCH=rl)
        self.assertRefused(rc, out, 'GUARD BAD notes.md')
        self.assertIn('GUARD BAD brief.md', out)
        # the derived roots: a checkout of the script whose design/themes/rd and research/rd are symlinks
        os.makedirs(os.path.join(self.repo, 'tools'))
        for f in ('rd_assets.sh', 'rd_terms.py', 'terms.py'):
            shutil.copy2(os.path.join(ROOT, 'tools', f), os.path.join(self.repo, 'tools', f))
        os.makedirs(os.path.join(self.repo, 'design', 'themes'))
        os.makedirs(os.path.join(self.repo, 'research'))
        os.symlink(self.design, os.path.join(self.repo, 'design', 'themes', 'rd'))
        os.symlink(self.research, os.path.join(self.repo, 'research', 'rd'))
        rc, out = run([os.path.join(self.repo, 'tools', 'rd_assets.sh'), '--guard'], env={'RD_MAIN_REPO': self.repo})
        self.assertRefused(rc, out, 'GUARD BAD notes.md')
        self.assertIn('GUARD BAD brief.md', out)


@unittest.skipUnless(export_ready(), 'needs node, tools/node_modules, Chrome and tools/fonts/ (tools/fonts.sh)')
class RdExport(unittest.TestCase):
    def test_rd_export_deterministic(self):
        with tempfile.TemporaryDirectory() as td:
            outs = []
            for k in (1, 2):
                d = os.path.join(td, 'o%d' % k)
                rc, msg = run(['node', EXPORT, '--shared-only', '--out', d], timeout=1200)
                self.assertEqual(rc, 0, msg)
                outs.append(d)
            files = sorted(os.path.relpath(os.path.join(r, f), outs[0]) for r, _, fs in os.walk(outs[0]) for f in fs)
            self.assertGreater(len(files), 50)
            for f in files:
                self.assertEqual(get(os.path.join(outs[0], f), 'rb'), get(os.path.join(outs[1], f), 'rb'), f)
            files2 = sorted(os.path.relpath(os.path.join(r, f), outs[1]) for r, _, fs in os.walk(outs[1]) for f in fs)
            self.assertEqual(files, files2)

    def test_rd_export_refuses_term(self):
        with tempfile.TemporaryDirectory() as td:
            for field in ('glyphs', 'text'):
                row = {'name': 'rd_x', 'face': 'shared', 'kind': 'atlas', 'source': 'test', 'font': 'Chakra Petch',
                       'weight': 600, 'size': 13, 'draw': 'fill', 'glyphs': 'AB'}
                row[field] = MARK
                put(os.path.join(td, 't.json'), json.dumps({'pinned': json.loads(get(TABLE))['pinned'], 'rows': [row]}))
                out = os.path.join(td, 'out')
                rc, msg = run(['node', EXPORT, '--table', os.path.join(td, 't.json'), '--out', out], timeout=600)
                self.assertNotEqual(rc, 0, msg)
                self.assertIn('forbidden', msg)
                self.assertFalse(os.path.exists(out), 'nothing written')
            # the page side: drawn text matching the list throws, glyph by glyph too (rd_export.js)
            rc, msg = run(['node', EXPORT, '--selftest'], timeout=600)
            self.assertEqual(rc, 0, msg)
            self.assertIn('rd_export selftest: OK', msg)


if __name__ == '__main__':
    unittest.main()
