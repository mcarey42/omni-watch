#!/usr/bin/env python3
"""Host tests for a public tree: the tools with the private term modules absent (tools/terms.py's rules).

Run from the repo root: `python -m unittest tools/test_check_apps_public.py` (ctest's test_check_apps_public). Each
class copies this checkout's firmware/ and tools/ (tracked and new files, without the private-only modules) into a
scratch git repo and runs the copied tools there, so REPO is the scratch, never this checkout:
  - with a PUBLIC_TREE marker: check_apps static ends with its one SKIP line for the term scans and passes; terms exits
    77; built passes over fixture build dirs with one SKIP line for the dw and rd list scans of the images (preflight
    I2); the packers' self-tests pass on the neutral marker alone; tools/rd_assets.sh --guard exits 77; the rd asset
    tests pass with their term cases skipped;
  - the marker beside a term module, a JS term module or the research allow-list fails check_apps static (preflight
    M14);
  - without the marker (a private checkout missing its modules): static fails on each missing module, and
    tools/rd_assets.sh --guard exits 2 (preflight M15).
Every file written here is neutral (zzneutralmarkzz or invented bytes).
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
# the private-only files (the public export excludes them)
PRIVATE = ('tools/dw_terms.py', 'tools/rd_terms.py', 'tools/research_tracked.txt', 'tools/export/dw_terms.mjs',
           'tools/export/rd_terms.mjs')
SKIP_STATIC = 'SKIP: dw_terms/rd_terms absent (a public tree): the term scans and their self-tests'
SKIP_BUILT = 'SKIP: dw_terms/rd_terms absent (a public tree): the dw and rd list scans of the images'
MARKER = 'PUBLIC_TREE'


def git(repo, *a):
    return subprocess.run(['git', '-C', repo, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'commit.gpgsign=false',
                           '-c', 'init.defaultBranch=main'] + list(a), check=True, capture_output=True, text=True)


def scratch_tree():
    """A scratch git repo holding this checkout's firmware/ and tools/ (as on disk, new files too) without the
    private-only files, committed."""
    td = tempfile.mkdtemp(prefix='public-tree-')
    r = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--',
                        'firmware', 'tools'], check=True, capture_output=True)
    for p in sorted(set(r.stdout.decode().split('\0'))):
        src = os.path.join(ROOT, p)
        if not p or p in PRIVATE or not os.path.isfile(src):
            continue
        dst = os.path.join(td, p)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    git(td, 'init', '-q')
    git(td, 'add', '-A')
    git(td, 'commit', '-q', '-m', 'scratch')
    return td


def run(repo, args, env=None, timeout=900):
    e = {k: v for k, v in os.environ.items() if not k.startswith(('RD_', 'GIT_', 'DW_'))}
    e['PYTHONDONTWRITEBYTECODE'] = '1'
    e.update(env or {})
    r = subprocess.run(args, capture_output=True, text=True, env=e, cwd=repo, timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def setUpModule():
    if not os.path.exists(os.path.join(ROOT, '.git')):   # final review m6: a download without .git
        raise unittest.SkipTest(f'no .git in {ROOT}: the scratch trees are copied from `git ls-files`; '
                                'the host tests are run from a git clone (README, Host tests)')


def check_apps_module():
    spec = importlib.util.spec_from_file_location('check_apps_for_fixtures', os.path.join(TOOLS, 'check_apps.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb' if isinstance(data, bytes) else 'w') as f:
        f.write(data)


def fixture_builds(repo):
    """Build dirs as check_apps built reads them (the self-tests' image builder), and a bin dir of stand-ins for the
    IDF's nm (one neutral symbol) and gdb (prints the 'elf', which holds the descriptor's two values)."""
    ca = check_apps_module()
    apps = os.path.join(repo, 'firmware', 'apps')
    table = b'\xaa\x50' + bytes(30)
    for app in ('omni', 'dw', 'rd'):
        put(os.path.join(apps, app, 'sdkconfig'), 'CONFIG_FIXTURE=y\n')
        put(os.path.join(apps, app, 'build', 'partition_table', 'partition-table.bin'), table)
    put(os.path.join(apps, 'omni', 'build', 'omni_watch.bin'), b'\x00neutral app image\x00')
    put(os.path.join(apps, 'omni', 'build', 'omni_watch.elf'), '')
    for app, files in (('dw', [('dw_a.bin', b'\x19' * 7), ('dw_b.pcm', b'\x00\x01' * 8)]),
                       ('rd', sorted(ca.committed_dw_assets('rd').items()))):
        b = os.path.join(apps, app, 'build')
        img = ca.build_mmap_image(files)
        put(os.path.join(b, ca.DW_IMAGE), img)
        put(os.path.join(b, 'omni_watch_%s.bin' % app), b'\x00neutral %s image\x00' % app.encode())
        n, k = len(files), int.from_bytes(img[16:20], 'little')
        put(os.path.join(b, ca.MMAP_HEADER), '#define MMAP_ASSETS_FILES %d\n#define MMAP_ASSETS_CHECKSUM 0x%X\n' % (n, k))
        put(os.path.join(b, 'omni_watch_%s.elf' % app), '$1 = %d\n$2 = %d\n' % (n, k))
    fake = os.path.join(repo, 'fakebin')
    put(os.path.join(fake, 'xtensa-esp32s3-elf-nm'), '#!/bin/sh\necho "00000000 T app_main"\n')
    put(os.path.join(fake, 'xtensa-esp32s3-elf-gdb'), '#!/bin/sh\nfor a; do e=$a; done\ncat "$e"\n')
    for t in os.listdir(fake):
        os.chmod(os.path.join(fake, t), 0o755)
    return fake


class PublicTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo = scratch_tree()
        put(os.path.join(cls.repo, MARKER), 'a public tree\n')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.repo, ignore_errors=True)

    def check_apps(self, *a, env=None):
        return run(self.repo, [sys.executable, 'tools/check_apps.py'] + list(a), env)

    def test_static_skips_term_scans(self):
        rc, out = self.check_apps('static')
        self.assertIn(SKIP_STATIC, out.splitlines())
        self.assertEqual(out.count('SKIP: '), 1, out)
        self.assertNotIn('terms missing', out)
        self.assertNotIn('Traceback', out)
        self.assertEqual(rc, 0, out)
        self.assertIn('check_apps static: OK', out)

    def test_terms_exits_77(self):
        rc, out = self.check_apps('terms')
        self.assertEqual(rc, 77, out)
        self.assertIn('a public tree', out)

    def test_built_skips_list_scans(self):
        fake = fixture_builds(self.repo)
        try:
            rc, out = self.check_apps('built', '--require', env={'PATH': fake + os.pathsep + os.environ['PATH']})
        finally:
            for app in ('omni', 'dw', 'rd'):
                shutil.rmtree(os.path.join(self.repo, 'firmware', 'apps', app, 'build'), ignore_errors=True)
                os.remove(os.path.join(self.repo, 'firmware', 'apps', app, 'sdkconfig'))
            shutil.rmtree(fake, ignore_errors=True)
        self.assertIn(SKIP_BUILT, out.splitlines())
        self.assertEqual(out.count('SKIP: '), 1, out)
        self.assertIn('rd build', out)        # the checks other than the list scans ran
        self.assertIn('dw build', out)
        self.assertEqual(rc, 0, out)

    def test_marker_beside_module_fails(self):
        p = os.path.join(self.repo, 'tools', 'rd_terms.py')
        put(p, '"""A neutral fixture standing in for a term module."""\nWORDS = ("zzneutralmarkzz",)\n')
        try:
            rc, out = self.check_apps('static')
        finally:
            os.remove(p)
        self.assertNotEqual(rc, 0, out)
        self.assertIn('FAIL: ' + MARKER, out)
        self.assertIn('tools/rd_terms.py', out)
        self.assertNotIn(SKIP_STATIC, out)

    def test_marker_beside_private_file_fails(self):
        """Task 5 review, carried to Task 6: the JS term modules and the research allow-list are private-only too, so
        each beside the marker fails static as well."""
        for rel, text in (('tools/export/rd_terms.mjs', '// a neutral fixture\nexport const W = "zzneutralmarkzz";\n'),
                          ('tools/export/dw_terms.mjs', '// a neutral fixture\nexport const W = "zzneutralmarkzz";\n'),
                          ('tools/research_tracked.txt', 'research/zzneutralmarkzz.md\n')):
            p = os.path.join(self.repo, *rel.split('/'))
            put(p, text)
            try:
                rc, out = self.check_apps('static')
            finally:
                os.remove(p)
            self.assertNotEqual(rc, 0, '%s: %s' % (rel, out))
            self.assertIn('FAIL: ' + MARKER + ' beside ' + rel, out)

    def test_packers_selftests_on_the_neutral_mark(self):
        for tool in ('tools/dw_pack.py', 'tools/rd_pack.py'):
            rc, out = run(self.repo, [sys.executable, tool, 'selftest'])
            self.assertEqual(rc, 0, '%s: %s' % (tool, out))
            self.assertIn('selftest: OK', out)

    def test_rd_guard_exits_77(self):
        rc, out = run(self.repo, ['bash', 'tools/rd_assets.sh', '--guard'])
        self.assertEqual(rc, 77, out)
        self.assertIn('no rd list (a public tree)', out)

    def test_rd_asset_tests_skip_term_cases(self):
        rc, out = run(self.repo, [sys.executable, '-m', 'unittest', 'tools/test_rd_assets.py'], timeout=1800)
        self.assertEqual(rc, 0, out)
        self.assertIn('skipped', out)


class PrivateMissing(unittest.TestCase):
    """No marker and no modules: a private checkout missing its lists fails, as today."""

    @classmethod
    def setUpClass(cls):
        cls.repo = scratch_tree()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.repo, ignore_errors=True)

    def test_static_fails_on_each_missing_module(self):
        rc, out = run(self.repo, [sys.executable, 'tools/check_apps.py', 'static'])
        self.assertEqual(rc, 1, out)
        self.assertIn('FAIL: rd_terms missing: tools/rd_terms.py', out)
        self.assertIn('FAIL: dw_terms missing: tools/dw_terms.py', out)
        self.assertNotIn(SKIP_STATIC, out)

    def test_terms_fails(self):
        rc, out = run(self.repo, [sys.executable, 'tools/check_apps.py', 'terms'])
        self.assertEqual(rc, 1, out)

    def test_rd_guard_exits_2(self):
        rc, out = run(self.repo, ['bash', 'tools/rd_assets.sh', '--guard'])
        self.assertEqual(rc, 2, out)
        self.assertIn('rd list missing: tools/rd_terms.py', out)

    def test_packers_refuse_to_run(self):
        for tool in ('tools/dw_pack.py', 'tools/rd_pack.py'):
            rc, out = run(self.repo, [sys.executable, tool, 'selftest'])
            self.assertNotEqual(rc, 0, '%s: %s' % (tool, out))
            self.assertIn('_terms.py', out)


if __name__ == '__main__':
    unittest.main()
