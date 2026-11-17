#!/usr/bin/env python3
"""Tests for tools/dw_assets.sh --check (dw wave 1 Task 9, G1). Run from the repo root:
    python -m unittest tools/test_dw_assets.py

Each runs the real script on scratch copies (DW_ASSETS_DIR, DW_MAIN_REPO, a scratch overlay) and never writes the
repo. They need a previous full run (tools/export/out-dw/layers.json), node with tools/node_modules, and Chrome; a test
whose inputs are missing is skipped.
  - the re-render check fails when a committed generic file is changed by one byte;
  - --overlay fails on a scratch overlay whose doors file is 465 px wide (the namesake rule);
  - --overlay fails when a scratch main repo has an overlay blob committed in its history (the guard), and passes
    when it does not;
  - the guard fails closed (I1): an unreadable object store or object dir in a scratch clone never gives GUARD OK;
  - an overlay under another checkout of this repo is refused (M4);
  - fix round 2 (`--guard`, scratch repos only): a truncated idx, a garbage idx, a truncated pack, a deleted pack, a
    shallow clone, nested alternates, an env alternate and an assets/ symlink into a checkout each refuse; a healthy
    repo still answers BAD and OK.
  - wave 2 Task 4 (the Classic doors, the overlay's third file): the re-render check catches one byte of the committed
    dw_doors_c.bin; --overlay fails on a scratch overlay whose dw_doors_c.bin is 465 px wide; the guard still answers
    BAD for an overlay dw_doors_c.bin blob in a scratch main repo's history (and fails closed on a broken one); the
    budget warns above 10 900 000 B (11 600 000 B once Vortex lands, wave 2 Task 14) and fails above 12 255 232 B (synthetic, sparse sizes; class DwBudget); the
    private build's staging (main/dw_stage.cmake, MODE=validate at configure and MODE=stage) refuses an overlay whose
    replaces names dw_extra.bin and accepts the three swap files (class DwStage; needs cmake only).
  - public release Task 6 (class DwExportTerms, scratch trees with a PUBLIC_TREE marker and no dw_terms.mjs): the
    page self-test passes on the neutral mark alone and catches it; DW_FACE_ALIAS maps a table face id to a fixture
    mockup's, and without it the export fails naming the table's id.
  - public release Task 9: an overlay file equal to a blob on dw_assets.sh's OVERLAY_TRACKED_BLOBS (the thumbnails and
    the chime, tracked before the overlay carried them) is GUARD SHARED and passes; one not on the list stays BAD.
"""
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SCRIPT = os.path.join(ROOT, 'tools', 'dw_assets.sh')
ASSETS = os.path.join(ROOT, 'firmware', 'apps', 'dw', 'assets')
PACK = os.path.join(ROOT, 'tools', 'dw_pack.py')
STAGE = os.path.join(ROOT, 'firmware', 'apps', 'dw', 'main', 'dw_stage.cmake')
PARTITIONS = os.path.join(ROOT, 'firmware', 'common', 'partitions.csv')
SWAP = ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin')   # the files an overlay may replace (R132, Task 4)
OUT = os.path.join(ROOT, 'tools', 'export', 'out-dw')
CHROME = os.environ.get('CHROME', '/usr/bin/google-chrome')


def ready():
    return (os.path.isfile(os.path.join(OUT, 'layers.json')) and os.path.isfile(CHROME) and shutil.which('node')
            and os.path.isdir(os.path.join(ROOT, 'tools', 'node_modules', 'playwright-core'))
            and os.path.isfile(os.path.join(ASSETS, 'dw_doors_m.bin')))


def run(args, **env):
    e = dict(os.environ, **env)
    e.pop('DW_ART_OVERLAY', None)
    r = subprocess.run([SCRIPT] + args, capture_output=True, text=True, env=e, cwd=ROOT)
    return r.returncode, r.stdout + r.stderr


def git(repo, *a):
    subprocess.run(['git', '-C', repo, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'commit.gpgsign=false'] + list(a),
                   check=True, capture_output=True)


@unittest.skipUnless(ready(), 'needs a full tools/dw_assets.sh run, node, tools/node_modules and Chrome')
class DwAssetsCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='dwassets-')
        self.assets = os.path.join(self.tmp, 'assets')
        shutil.copytree(ASSETS, self.assets)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def overlay(self, width=466, names=('dw_doors_m.bin',), narrow='dw_doors_m.bin'):
        """A scratch overlay replacing each of `names` with a file of its generic namesake's header and length, not
        its bytes; `narrow` is `width` px wide instead of 466."""
        art = os.path.join(self.tmp, 'art')
        os.makedirs(os.path.join(art, 'assets'))
        os.makedirs(os.path.join(art, 'out'))
        json.dump({'theme': 'dw', 'replaces': list(names), 'layers': 'layers.json'},
                  open(os.path.join(art, 'over' + 'lay.json'), 'w'))
        rows = []
        for n in names:
            w = width if n == narrow else 466
            rows.append({'asset': n[:-4], 'file': n, 'face': 'x', 'fn': 'x', 'w': w, 'h': 466, 'cf': 'RGB565',
                         'out': 'bin'})
            data = bytearray(open(os.path.join(ASSETS, n), 'rb').read())
            struct.pack_into('<HHH', data, 4, w, 466, w * 2)
            data[100] ^= 0x55                               # not the generic bytes
            data[101] ^= len(rows)                          # nor another overlay file's
            del data[12 + w * 2 * 466:]
            open(os.path.join(art, 'assets', n), 'wb').write(data)
        json.dump({'rows': rows}, open(os.path.join(art, 'layers.json'), 'w'))
        return art

    def test_rerender_catches_one_byte(self):
        p = os.path.join(self.assets, 'dw_doors_m.bin')
        data = bytearray(open(p, 'rb').read())
        data[12 + 2 * (233 * 466 + 100)] ^= 1               # one byte of one pixel
        open(p, 'wb').write(data)
        rc, out = run(['--check'], DW_ASSETS_DIR=self.assets)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('RE-RENDER BAD dw_doors_m.bin', out)
        self.assertNotIn('RE-RENDER BAD dw_b5_box.bin', out)

    def test_rerender_passes_unchanged(self):
        rc, out = run(['--check'], DW_ASSETS_DIR=self.assets)
        self.assertEqual(rc, 0, out)
        self.assertIn('RE-RENDER OK 3', out)

    def test_rerender_catches_one_byte_classic_doors(self):
        """Task 4: the generic Classic doors are dw_generic.js's doors_c(), byte for byte."""
        p = os.path.join(self.assets, 'dw_doors_c.bin')
        data = bytearray(open(p, 'rb').read())
        data[12 + 2 * (300 * 466 + 333) + 1] ^= 0x80         # one byte of one pixel
        open(p, 'wb').write(data)
        rc, out = run(['--check'], DW_ASSETS_DIR=self.assets)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('RE-RENDER BAD dw_doors_c.bin', out)
        self.assertNotIn('RE-RENDER BAD dw_doors_m.bin', out)
        self.assertNotIn('RE-RENDER BAD dw_b5_box.bin', out)

    def test_overlay_465_wide_fails(self):
        art = self.overlay(465)
        repo = os.path.join(self.tmp, 'main')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('OVERLAY BAD', out)
        self.assertIn('465x466', out)

    def test_overlay_classic_doors_465_wide_fails(self):
        """Task 4: the namesake rule holds for the third file: an overlay dw_doors_c.bin 465 px wide is refused."""
        art = self.overlay(465, SWAP, narrow='dw_doors_c.bin')
        repo = os.path.join(self.tmp, 'main')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('OVERLAY BAD', out)
        self.assertIn('dw_doors_c.bin is RGB565 465x466', out)

    def test_guard_classic_doors_blob(self):
        """Task 4: with the three-file overlay, the guard answers OK on a clean scratch repo and BAD for the overlay
        dw_doors_c.bin's blob in its history (committed on a branch, then removed), naming that file alone."""
        art = self.overlay(names=SWAP)
        repo = os.path.join(self.tmp, 'main')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        open(os.path.join(repo, 'a.txt'), 'w').write('x')
        git(repo, 'add', 'a.txt')
        git(repo, 'commit', '-qm', 'one')
        rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertEqual(rc, 0, out)
        self.assertIn('GUARD OK', out)
        self.assertIn("none of the overlay's 5 files", out)   # overlay.json, layers.json and the three assets
        git(repo, 'checkout', '-qb', 'slip')
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_c.bin'), os.path.join(repo, 'doors.bin'))
        git(repo, 'add', 'doors.bin')
        git(repo, 'commit', '-qm', 'slip')
        git(repo, 'rm', '-q', 'doors.bin')
        git(repo, 'commit', '-qm', 'undo')
        for args in (['--guard', '--overlay', art], ['--check', '--overlay', art]):
            rc, out = run(args, DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
            self.assertNotEqual(rc, 0, out)
            self.assertIn('GUARD BAD assets/dw_doors_c.bin', out)
            self.assertNotIn('GUARD BAD assets/dw_doors_m.bin', out)
            self.assertNotIn('GUARD BAD assets/dw_b5_box.bin', out)
            self.assertNotIn('GUARD OK', out)

    def test_guard_allows_tracked_blob(self):
        """Public release Task 9: an overlay file whose blob is on dw_assets.sh's OVERLAY_TRACKED_BLOBS (a thumbnail
        tracked before the overlay carried it) is GUARD SHARED, not BAD, and the guard passes; an overlay file not on
        the list stays BAD, and no environment variable widens the list."""
        listed = '344964b24d235a32b4d367b34c254de37c505452'   # dw_th_b5.bin as tracked since its wave
        blob = subprocess.run(['git', '-C', ROOT, 'cat-file', 'blob', listed], capture_output=True)
        if blob.returncode != 0:
            self.skipTest('this checkout has no blob %s' % listed)
        art = self.overlay()
        open(os.path.join(art, 'assets', 'dw_th_b5.bin'), 'wb').write(blob.stdout)
        repo = os.path.join(self.tmp, 'main')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        open(os.path.join(repo, 'th.bin'), 'wb').write(blob.stdout)
        git(repo, 'add', 'th.bin')
        git(repo, 'commit', '-qm', 'tracked')
        rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertEqual(rc, 0, out)
        self.assertIn('GUARD SHARED assets/dw_th_b5.bin (tracked before the overlay carried it; §4.1)', out)
        self.assertNotIn('GUARD BAD', out)
        self.assertIn('GUARD OK', out)
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_m.bin'), os.path.join(repo, 'x.bin'))
        git(repo, 'add', 'x.bin')
        git(repo, 'commit', '-qm', 'slip')
        doors = subprocess.run(['git', '-C', repo, 'hash-object', 'x.bin'], capture_output=True, text=True).stdout.strip()
        for env in ({}, {'OVERLAY_TRACKED_BLOBS': doors}):
            rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo, **env)
            self.assertNotEqual(rc, 0, out)
            self.assertIn('GUARD BAD assets/dw_doors_m.bin', out)
            self.assertIn('GUARD SHARED assets/dw_th_b5.bin', out)
            self.assertNotIn('GUARD OK', out)

    def test_guard_classic_doors_fails_closed(self):
        """Task 4: the three-file overlay changes nothing about failing closed: the dw_doors_c.bin blob in an older
        pack whose index is truncated is refused, never answered OK."""
        art = self.overlay(names=SWAP)
        repo = os.path.join(self.tmp, 'two')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_c.bin'), os.path.join(repo, 'x.bin'))
        git(repo, 'add', 'x.bin')
        git(repo, 'commit', '-qm', 'one')
        git(repo, 'repack', '-q', '-d')
        packdir = os.path.join(repo, '.git', 'objects', 'pack')
        idx = [os.path.join(packdir, f) for f in os.listdir(packdir) if f.endswith('.idx')][0]
        open(os.path.join(repo, 'y.txt'), 'w').write('y')
        git(repo, 'add', 'y.txt')
        git(repo, 'commit', '-qm', 'two')
        git(repo, 'repack', '-q', '-d')
        os.chmod(idx, 0o644)
        with open(idx, 'r+b') as f:
            f.truncate(100)
        self.assert_refused(art, repo)

    def test_guard(self):
        art = self.overlay()
        repo = os.path.join(self.tmp, 'main')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        open(os.path.join(repo, 'a.txt'), 'w').write('x')
        git(repo, 'add', 'a.txt')
        git(repo, 'commit', '-qm', 'one')
        rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertIn('GUARD OK', out)
        # the overlay's blob committed once, on a branch, then deleted from the tree: still in the history
        git(repo, 'checkout', '-qb', 'slip')
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_m.bin'), os.path.join(repo, 'x.bin'))
        git(repo, 'add', 'x.bin')
        git(repo, 'commit', '-qm', 'slip')
        git(repo, 'rm', '-q', 'x.bin')
        git(repo, 'commit', '-qm', 'undo')
        rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('GUARD BAD assets/dw_doors_m.bin', out)

    def slipped_clone(self, art):
        """A scratch main repo holding the overlay's blob in its history, cloned (its own copy of every object)."""
        src = os.path.join(self.tmp, 'src')
        os.makedirs(src)
        git(src, 'init', '-q')
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_m.bin'), os.path.join(src, 'x.bin'))
        git(src, 'add', 'x.bin')
        git(src, 'commit', '-qm', 'slip')
        clone = os.path.join(self.tmp, 'clone')
        subprocess.run(['git', 'clone', '-q', '--no-local', src, clone], check=True, capture_output=True)
        return clone

    def test_guard_fails_closed_on_unreadable_objects(self):
        """I1: git reads an unreadable object entry as a missing object (cat-file -e exits 1); the guard must refuse,
        never print GUARD OK, though the blob is in the clone."""
        art = self.overlay()
        clone = self.slipped_clone(art)
        objs = os.path.join(clone, '.git', 'objects')
        locked = [os.path.join(r, n) for r, ds, fs in os.walk(objs) for n in ds + fs
                  if os.path.relpath(r, objs).split(os.sep)[0] != 'info' and n != 'info']
        modes = {p: os.stat(p).st_mode for p in locked}
        try:
            for p in sorted(locked, key=len, reverse=True):
                os.chmod(p, 0)
            rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=clone)
        finally:
            for p in sorted(locked, key=len):
                os.chmod(p, modes[p])
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn('GUARD OK', out)
        self.assertIn('guard: unreadable entry in the object store', out)

    def test_guard_fails_closed_on_unreadable_object_dir(self):
        art = self.overlay()
        clone = self.slipped_clone(art)
        objs = os.path.join(clone, '.git', 'objects')
        mode = os.stat(objs).st_mode
        try:
            os.chmod(objs, 0)
            rc, out = run(['--check', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=clone)
        finally:
            os.chmod(objs, mode)
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn('GUARD OK', out)
        self.assertIn('FAIL: guard:', out)

    def test_overlay_inside_a_sibling_checkout_refused(self):
        """M4: an overlay under any checkout of this repo (here the main checkout or a sibling worktree) is refused."""
        roots = subprocess.run(['git', '-C', ROOT, 'worktree', 'list', '--porcelain'], capture_output=True,
                               text=True).stdout
        others = [l[9:] for l in roots.splitlines() if l.startswith('worktree ') and
                  os.path.realpath(l[9:]) != ROOT and os.path.isdir(l[9:])]
        if not others:
            self.skipTest('no other checkout of this repo')
        rc, out = run(['--check', '--overlay', others[0]])
        self.assertEqual(rc, 2, out)
        self.assertIn('must not live inside this repo or any of its checkouts', out)

    # ---- fix round 2: the guard fails closed by construction (fsck, no shallow, no alternates, symlinks refused).
    # Every case runs `dw_assets.sh --guard --overlay ART` against a scratch repo in the temp dir; neither real repo
    # is ever written.
    def two_pack_repo(self, art):
        """A scratch repo whose older pack holds the overlay's blob and whose newer pack holds HEAD; returns (repo,
        the older pack's path without its extension)."""
        repo = os.path.join(self.tmp, 'two')
        os.makedirs(repo)
        git(repo, 'init', '-q')
        shutil.copy(os.path.join(art, 'assets', 'dw_doors_m.bin'), os.path.join(repo, 'x.bin'))
        git(repo, 'add', 'x.bin')
        git(repo, 'commit', '-qm', 'one')
        git(repo, 'repack', '-q', '-d')
        packdir = os.path.join(repo, '.git', 'objects', 'pack')
        old = [os.path.join(packdir, f[:-5]) for f in os.listdir(packdir) if f.endswith('.pack')]
        self.assertEqual(len(old), 1)
        open(os.path.join(repo, 'y.txt'), 'w').write('y')
        git(repo, 'add', 'y.txt')
        git(repo, 'commit', '-qm', 'two')
        git(repo, 'repack', '-q', '-d')            # incremental: the new objects only, in a second pack
        self.assertEqual(len([f for f in os.listdir(packdir) if f.endswith('.pack')]), 2)
        return repo, old[0]

    def assert_refused(self, art, repo, **env):
        rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo, **env)
        self.assertNotEqual(rc, 0, out)
        self.assertNotIn('GUARD OK', out)
        self.assertNotIn('GUARD BAD', out)          # refused before any answer, not answered
        self.assertTrue('FAIL: guard:' in out or 'must not' in out or 'resolves inside' in out, out)
        return out

    def test_guard_mode_answers_on_a_clean_repo(self):
        """The control: on a healthy scratch repo the guard answers, BAD with the blob in history and OK without."""
        art = self.overlay()
        repo, _ = self.two_pack_repo(art)
        rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=repo)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('GUARD BAD assets/dw_doors_m.bin', out)
        clean = os.path.join(self.tmp, 'clean')
        os.makedirs(clean)
        git(clean, 'init', '-q')
        open(os.path.join(clean, 'a.txt'), 'w').write('a')
        git(clean, 'add', 'a.txt')
        git(clean, 'commit', '-qm', 'a')
        rc, out = run(['--guard', '--overlay', art], DW_ASSETS_DIR=self.assets, DW_MAIN_REPO=clean)
        self.assertEqual(rc, 0, out)
        self.assertIn('GUARD OK', out)

    def test_guard_refuses_a_truncated_idx(self):
        art = self.overlay()
        repo, old = self.two_pack_repo(art)
        os.chmod(old + '.idx', 0o644)
        with open(old + '.idx', 'r+b') as f:
            f.truncate(100)
        self.assert_refused(art, repo)

    def test_guard_refuses_a_garbage_idx(self):
        art = self.overlay()
        repo, old = self.two_pack_repo(art)
        os.chmod(old + '.idx', 0o644)
        n = os.path.getsize(old + '.idx')
        with open(old + '.idx', 'wb') as f:
            f.write(bytes((i * 37 + 11) & 255 for i in range(n)))
        self.assert_refused(art, repo)

    def test_guard_refuses_a_truncated_pack(self):
        art = self.overlay()
        repo, old = self.two_pack_repo(art)
        os.chmod(old + '.pack', 0o644)
        with open(old + '.pack', 'r+b') as f:
            f.truncate(os.path.getsize(old + '.pack') // 2)
        self.assert_refused(art, repo)

    def test_guard_refuses_a_deleted_pack(self):
        art = self.overlay()
        repo, old = self.two_pack_repo(art)
        os.remove(old + '.pack')
        self.assert_refused(art, repo)

    def test_guard_refuses_a_shallow_clone(self):
        art = self.overlay()
        repo, _ = self.two_pack_repo(art)
        shallow = os.path.join(self.tmp, 'shallow')
        subprocess.run(['git', 'clone', '-q', '--depth', '1', 'file://' + repo, shallow], check=True, capture_output=True)
        out = self.assert_refused(art, shallow)
        self.assertIn('shallow', out)

    def test_guard_refuses_nested_alternates(self):
        art = self.overlay()
        repo, _ = self.two_pack_repo(art)
        x = os.path.join(self.tmp, 'x')
        m = os.path.join(self.tmp, 'm')
        subprocess.run(['git', 'clone', '-q', '--shared', repo, x], check=True, capture_output=True)
        subprocess.run(['git', 'clone', '-q', '--shared', x, m], check=True, capture_output=True)   # m -> x -> repo
        out = self.assert_refused(art, m)
        self.assertIn('alternates are refused', out)

    def test_guard_refuses_an_env_alternate(self):
        art = self.overlay()
        repo, _ = self.two_pack_repo(art)
        out = self.assert_refused(art, repo, GIT_ALTERNATE_OBJECT_DIRECTORIES=os.path.join(self.tmp, 'elsewhere'))
        self.assertIn('GIT_ALTERNATE_OBJECT_DIRECTORIES is set', out)

    def test_overlay_assets_symlink_into_a_checkout_refused(self):
        """N1: an overlay outside every checkout whose assets/ links into one (here this worktree's generic assets,
        which a full --overlay run would overwrite) is refused before anything runs."""
        art = self.overlay()
        shutil.rmtree(os.path.join(art, 'assets'))
        os.symlink(ASSETS, os.path.join(art, 'assets'))
        before = {f: os.path.getmtime(os.path.join(ASSETS, f)) for f in os.listdir(ASSETS)}
        for args in (['--guard', '--overlay', art], ['--check', '--overlay', art]):
            rc, out = run(args, DW_ASSETS_DIR=self.assets)
            self.assertEqual(rc, 2, out)
            self.assertIn("the overlay's assets/ must not be a symlink", out)
        self.assertEqual(before, {f: os.path.getmtime(os.path.join(ASSETS, f)) for f in os.listdir(ASSETS)})


class DwBudget(unittest.TestCase):
    """Task 4: the wave-2 budget lines, on synthetic sizes (one sparse file; tools/assets.sh's count: 32 B, then
    64 B and the length per file). tools/dw_assets.sh runs `dw_pack.py budget` with its defaults."""
    def used(self, total):
        tmp = tempfile.mkdtemp(prefix='dwbudget-')
        self.addCleanup(shutil.rmtree, tmp, True)
        with open(os.path.join(tmp, 'big.bin'), 'wb') as f:
            f.truncate(total - 32 - 64)
        r = subprocess.run([os.environ.get('PYTHON', 'python3'), PACK, 'budget', '--assets', tmp,
                            '--partitions', PARTITIONS], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def test_dw_assets_uses_the_defaults(self):
        text = open(SCRIPT).read()
        call = [l for l in text.splitlines() if 'budget --assets' in l]
        self.assertEqual(len(call), 1, call)
        self.assertNotIn('--warn', call[0])
        self.assertNotIn('--fail', call[0])

    def test_warn_line(self):
        rc, out = self.used(11600000)   # wave 2 Task 14: the line with Vortex
        self.assertEqual(rc, 0, out)
        self.assertNotIn('WARN', out)
        rc, out = self.used(11600001)
        self.assertEqual(rc, 0, out)
        self.assertIn('WARN: 11600001 B is above the wave-2 line of 11600000 B', out)

    def test_fail_line(self):
        rc, out = self.used(12255232)
        self.assertEqual(rc, 0, out)
        self.assertNotIn('ASSETS BAD', out)
        rc, out = self.used(12255233)
        self.assertEqual(rc, 1, out)
        self.assertIn('ASSETS BAD: 12255233 B exceed the 12255232-byte budget', out)


@unittest.skipUnless(shutil.which('cmake'), 'needs cmake')
class DwStage(unittest.TestCase):
    """Task 4: the private build's allow-list (main/dw_stage.cmake): MODE=validate, which main/CMakeLists.txt runs at
    configure, and MODE=stage, which the build runs. Scratch copies only."""
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='dwstage-')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.src = os.path.join(self.tmp, 'generic')
        os.makedirs(self.src)
        for n in SWAP + ('dw_glow.bin',):
            shutil.copy(os.path.join(ASSETS, n), self.src)

    def art(self, names):
        art = os.path.join(self.tmp, 'art')
        shutil.rmtree(art, ignore_errors=True)
        os.makedirs(os.path.join(art, 'assets'))
        json.dump({'theme': 'dw', 'replaces': list(names), 'layers': 'layers.json'},
                  open(os.path.join(art, 'over' + 'lay.json'), 'w'))
        for n in names:
            src = os.path.join(self.src, n if os.path.isfile(os.path.join(self.src, n)) else 'dw_doors_m.bin')
            data = bytearray(open(src, 'rb').read())
            data[200] ^= 0x33
            open(os.path.join(art, 'assets', n), 'wb').write(data)
            if not os.path.isfile(os.path.join(self.src, n)):
                shutil.copy(src, os.path.join(self.src, n))   # a generic namesake too: only the list refuses it
        return art

    def cmake(self, mode, art):
        stage = os.path.join(self.tmp, 'stage', 'assets')
        r = subprocess.run(['cmake', '-DMODE=' + mode, '-DSRC=' + self.src, '-DART=' + art, '-DSTAGE=' + stage,
                            '-P', STAGE], capture_output=True, text=True)
        return r.returncode, ' '.join((r.stdout + r.stderr).split()), stage   # CMake wraps its messages

    def test_extra_name_refused(self):
        art = self.art(SWAP + ('dw_extra.bin',))
        for mode in ('validate', 'stage'):
            rc, out, stage = self.cmake(mode, art)
            self.assertNotEqual(rc, 0, out)
            self.assertIn('replaces dw_extra.bin: an overlay may replace only', out)
            self.assertFalse(os.path.isfile(os.path.join(stage, 'dw_extra.bin')), mode)

    def test_three_swap_files_staged(self):
        art = self.art(SWAP)
        rc, out, _ = self.cmake('validate', art)
        self.assertEqual(rc, 0, out)
        rc, out, stage = self.cmake('stage', art)
        self.assertEqual(rc, 0, out)
        for n in SWAP:
            self.assertEqual(open(os.path.join(stage, n), 'rb').read(), open(os.path.join(art, 'assets', n), 'rb').read(), n)
        self.assertEqual(open(os.path.join(stage, 'dw_glow.bin'), 'rb').read(),
                         open(os.path.join(self.src, 'dw_glow.bin'), 'rb').read())


MARK = 'zzneutralmarkzz'   # tools/terms.py's NEUTRAL_MARK: the only word these tests ever draw as forbidden
EXPORT_FILES = ('tools/dw_export.mjs', 'tools/export/terms.mjs', 'tools/export/dw_export.html',
                'tools/export/dw_export.js', 'tools/export/dw_generic.js', 'tools/export/dw_layers.json')
# a fixture mockup: one face, fixture-face, whose builder buildFixture() makes one 8 x 8 canvas
FIXTURE_FACE = '''(function () {
  function buildFixture() {
    const c = document.createElement('canvas'); c.width = 8; c.height = 8;
    const g = c.getContext('2d'); g.fillStyle = '#0f0'; g.fillRect(0, 0, 8, 8);
    return c;
  }
  let cv = null;
  window.OMNI_FACES.push({ id: 'fixture-face', init() {}, frame(g) { if (!cv) cv = buildFixture(); g.drawImage(cv, 0, 0); } });
})();
'''


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


def export_ready():
    return (os.path.isfile(CHROME) and shutil.which('node')
            and os.path.isdir(os.path.join(ROOT, 'tools', 'node_modules', 'playwright-core'))
            and os.path.isfile(os.path.join(ROOT, 'tools', 'fonts', 'CormorantSC-Bold.ttf')))


@unittest.skipUnless(export_ready(), 'needs node, tools/node_modules, Chrome and tools/fonts/ (tools/fonts.sh)')
class DwExportTerms(unittest.TestCase):
    """public release Task 6: dw_export.mjs takes its drawn-text list from tools/export/dw_terms.mjs (private) and the
    face aliases from DW_FACE_ALIAS. Each runs the export from a scratch tree (the export's own files, a PUBLIC_TREE
    marker, no dw_terms.mjs), so only the neutral mark is forbidden there."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='dwexport-')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.tree = os.path.join(self.tmp, 'tree')
        for p in EXPORT_FILES:
            if os.path.isfile(os.path.join(ROOT, p)):
                os.makedirs(os.path.dirname(os.path.join(self.tree, p)), exist_ok=True)
                shutil.copy2(os.path.join(ROOT, p), os.path.join(self.tree, p))
        for d in ('node_modules', 'fonts'):
            os.symlink(os.path.join(ROOT, 'tools', d), os.path.join(self.tree, 'tools', d))
        write(os.path.join(self.tree, 'PUBLIC_TREE'), '')

    def export(self, args, **env):
        e = {k: v for k, v in os.environ.items() if not k.startswith(('DW_', 'GIT_'))}
        e.update(env)
        r = subprocess.run(['node', os.path.join(self.tree, 'tools', 'dw_export.mjs')] + args, capture_output=True,
                           text=True, env=e, cwd=self.tree, timeout=600)
        return r.returncode, r.stdout + r.stderr

    def test_selftest_neutral_only(self):
        self.assertFalse(os.path.exists(os.path.join(self.tree, 'tools', 'export', 'dw_terms.mjs')))
        rc, out = self.export(['--selftest'])
        self.assertEqual(rc, 0, out)
        self.assertIn('dw_export selftest: OK', out)
        self.assertIn('ok   fillText of the neutral mark', out)
        self.assertIn('ok   strokeText of the neutral mark', out)
        self.assertIn('ok   fillText of the neutral mark one glyph per call (15 glyphs) caught at its last glyph', out)

    def test_alias_applied(self):
        design = os.path.join(self.tmp, 'design')
        os.makedirs(os.path.join(design, 'mockups'))
        write(os.path.join(design, 'mockups', 'faces-reveal.js'), FIXTURE_FACE)
        for n in ('faces-classic.js', 'faces-modern.js'):
            write(os.path.join(design, 'mockups', n), '/* empty */\n')
        os.makedirs(os.path.join(self.tree, 'design', 'mockups'))
        write(os.path.join(self.tree, 'design', 'mockups', 'faces-practical.js'), '/* empty */\n')
        with open(os.path.join(ROOT, 'tools', 'export', 'dw_layers.json')) as f:
            pinned = json.load(f)['pinned']
        row = {'asset': 'dw_fx', 'file': 'dw_fx.bin', 'face': 'neutral', 'fn': 'buildFixture', 'w': 8, 'h': 8,
               'cf': 'RGB565', 'out': 'bin', 'times': [0]}
        write(os.path.join(self.tree, 'tools', 'export', 'dw_layers.json'),
              json.dumps({'pinned': pinned, 'geometry': {}, 'rows': [row]}))
        out_dir = os.path.join(self.tmp, 'out')
        rc, out = self.export(['--out', out_dir], DW_DESIGN=design, DW_FACE_ALIAS='{"neutral": "fixture-face"}')
        self.assertEqual(rc, 0, out)
        self.assertTrue(os.path.isfile(os.path.join(out_dir, 'dw_fx.png')), out)
        shutil.rmtree(out_dir)
        rc, out = self.export(['--out', out_dir], DW_DESIGN=design)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('neutral', out)
        self.assertFalse(os.path.exists(out_dir), 'nothing written')


if __name__ == '__main__':
    unittest.main()
