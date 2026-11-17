#!/usr/bin/env python3
"""Host tests for tools/watchport.py: the watch file's rules, the port and --watch choice, and where the file is found.

Run from the repo root: `python -m unittest tools/test_watchport.py` (ctest's test_watchport). Temp dirs only: no
test opens a serial port, and every test points OMNI_WATCH_LOCAL at a temp file (or a scratch repo), so none can
reach the owner's tools/watch.local.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import watchport  # noqa: E402

A = '/dev/serial/by-id/usb-a-if00'
B = '/dev/serial/by-id/usb-b-if00'
FIXTURE_PORT = '/dev/serial/by-id/usb-fixture_watch_serial-if00'
# The test modules that run a port tool (theme.sh, bootline.py, trace.py, shot.py, shot_set.py, flash_keep_nvs.py)
# or watchport itself: each sets OMNI_WATCH_LOCAL in setUpModule (preflight M20).
PORT_TEST_MODULES = ('test_flash_keep_nvs', 'test_watchport')
PORT_TOOL = re.compile(r'theme\.sh|bootline|trace\.py|shot_set|shot\.py|flash_keep_nvs|watchport')

_SAVED = []


def setUpModule():
    """Every test in this module sees a temp watch file, never the owner's."""
    d = tempfile.mkdtemp(prefix='watchport-test-')
    p = os.path.join(d, 'watch.local')
    with open(p, 'w') as f:
        f.write(FIXTURE_PORT + '\n')
    _SAVED.append((d, os.environ.get(watchport.ENV)))
    os.environ[watchport.ENV] = p


def tearDownModule():
    d, old = _SAVED.pop()
    shutil.rmtree(d, ignore_errors=True)
    if old is None:
        os.environ.pop(watchport.ENV, None)
    else:
        os.environ[watchport.ENV] = old


class Tmp(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def file(self, text, name='watch.local'):
        p = os.path.join(self.tmp, name)
        with open(p, 'w', newline='') as f:
            f.write(text)
        return p

    def use(self, text):
        """OMNI_WATCH_LOCAL -> a temp file with `text`, for this test only."""
        p = self.file(text)
        old = os.environ.get(watchport.ENV)
        os.environ[watchport.ENV] = p
        self.addCleanup(lambda: os.environ.__setitem__(watchport.ENV, old) if old is not None
                        else os.environ.pop(watchport.ENV, None))
        return p


class LoadRules(Tmp):
    def test_comments_blank_crlf(self):
        p = self.file('# x\r\n\r\n/dev/serial/by-id/usb-a-if00 one\r\n')
        self.assertEqual(watchport.load(p), [('one', A)])

    def test_default_names(self):
        p = self.file(f'{A}\n  # a comment\n{B}   # trailing comment\n')
        self.assertEqual(watchport.load(p), [('watch1', A), ('watch2', B)])

    def test_bad_lines(self):
        cases = {
            'ttyACM': (f'{A}\n/dev/ttyACM7\n', 2),
            'dot-dot': ('/dev/serial/by-id/x/../y\n', 1),
            'trailing slash': (f'{A}/\n', 1),
            'duplicate path': (f'{A} one\n\n{A} two\n', 3),
            'duplicate name': (f'{A} one\n{B} one\n', 2),
            'bad name': (f'{A} Bad Name\n', 1),
        }
        for what, (text, n) in cases.items():
            with self.subTest(what):
                p = self.file(text)
                with self.assertRaises(watchport.Refused) as cm:
                    watchport.load(p)
                self.assertTrue(str(cm.exception).startswith(f'watch.local:{n}: '), str(cm.exception))
                self.assertNotIn('\n', str(cm.exception))

    def test_empty_refused(self):
        for text in ('', '# only a comment\n\n'):
            with self.subTest(repr(text)):
                with self.assertRaises(watchport.Refused):
                    watchport.load(self.file(text))

    def test_missing_refused(self):
        with self.assertRaises(watchport.Refused) as cm:
            watchport.load(os.path.join(self.tmp, 'nope'))
        self.assertIn("no tools/watch.local: list your watch's /dev/serial/by-id/ path there", str(cm.exception))


class ResolveRules(Tmp):
    def test_one_entry_default(self):
        self.use(f'{A} one\n')
        self.assertEqual(watchport.resolve(REPO), A)

    def test_two_entries_need_watch(self):
        self.use(f'{A} one\n{B} two\n')
        with self.assertRaises(watchport.Refused) as cm:
            watchport.resolve(REPO)
        self.assertEqual(str(cm.exception), 'several watches listed: pass --watch <name>')

    def test_watch_by_name(self):
        self.use(f'{A} one\n{B} two\n')
        self.assertEqual(watchport.resolve(REPO, watch='two'), B)
        self.assertEqual(watchport.resolve(REPO, watch='one'), A)
        self.assertEqual(watchport.resolve(REPO, port=B), B)

    def test_port_exact(self):
        """The listed string is accepted; anything else that names the same node is refused (the realpath target of a
        real symlink: test_port_exact_realpath_target)."""
        self.use(f'{A} one\n')
        self.assertEqual(watchport.resolve(REPO, port=A), A)
        for bad in (A + '/', '/dev/serial/by-id//usb-a-if00', '/dev/serial/by-id/./usb-a-if00',
                    '/dev/serial/by-id/../by-id/usb-a-if00', '/dev/ttyACM0', 'usb-a-if00', ' ' + A):
            with self.subTest(bad):
                with self.assertRaises(watchport.Refused):
                    watchport.resolve(REPO, port=bad)

    def test_port_exact_realpath_target(self):
        """A listed path that is a symlink: its realpath target (the /dev/ttyACM* it names) is refused."""
        target = os.path.join(self.tmp, 'ttyACM5')
        open(target, 'w').close()
        link = os.path.join(self.tmp, 'usb-link-if00')
        os.symlink(target, link)
        p = self.use(f'{A} one\n')
        old = watchport.BY_ID
        watchport.BY_ID = self.tmp + '/'   # let a temp dir stand in for /dev/serial/by-id/
        self.addCleanup(setattr, watchport, 'BY_ID', old)
        with open(p, 'w') as f:
            f.write(f'{link} one\n')
        self.assertEqual(watchport.resolve(REPO, port=link), link)
        with self.assertRaises(watchport.Refused):
            watchport.resolve(REPO, port=os.path.realpath(link))

    def test_port_and_watch_disagree(self):
        self.use(f'{A} one\n{B} two\n')
        self.assertEqual(watchport.resolve(REPO, port=B, watch='two'), B)
        with self.assertRaises(watchport.Refused):
            watchport.resolve(REPO, port=A, watch='two')

    def test_unknown_watch(self):
        self.use(f'{A} one\n{B} two\n')
        with self.assertRaises(watchport.Refused) as cm:
            watchport.resolve(REPO, watch='three')
        self.assertIn('three', str(cm.exception))

    def test_cli(self):
        p = self.use(f'{A} one\n{B} two\n')
        env = dict(os.environ, **{watchport.ENV: p})
        cli = [sys.executable, os.path.join(TOOLS, 'watchport.py'), 'resolve']
        r = subprocess.run(cli + ['--watch', 'two'], env=env, capture_output=True, text=True)
        self.assertEqual((r.returncode, r.stdout, r.stderr), (0, B + '\n', ''))
        r = subprocess.run(cli, env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        self.assertEqual(r.stdout, '')
        self.assertIn('several watches listed: pass --watch <name>', r.stderr)
        r = subprocess.run(cli + ['--port', '/dev/ttyACM0'], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)


def git(*args, cwd):
    subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'init.defaultBranch=main', *args],
                   cwd=cwd, check=True, capture_output=True)


def scratch_repo(tmp):
    """A scratch `git init` main checkout with one commit, and a linked worktree of it: (main, worktree)."""
    main, wt = os.path.join(tmp, 'main'), os.path.join(tmp, 'wt')
    os.makedirs(os.path.join(main, 'tools'))
    with open(os.path.join(main, 'README'), 'w') as f:
        f.write('x\n')
    git('init', '-q', cwd=main)
    git('add', 'README', cwd=main)
    git('commit', '-q', '-m', 'x', cwd=main)
    git('worktree', 'add', '-q', '--detach', wt, cwd=main)
    os.makedirs(os.path.join(wt, 'tools'), exist_ok=True)
    return main, wt


def env_unset():
    old = os.environ.pop(watchport.ENV, None)

    def restore():
        if old is not None:
            os.environ[watchport.ENV] = old
    return restore


class LocateRules(Tmp):
    def test_env_wins(self):
        main, wt = scratch_repo(self.tmp)
        with open(os.path.join(wt, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{A}\n')
        p = self.use(f'{B}\n')
        self.assertEqual(watchport.local_path(wt), p)
        self.assertEqual(watchport.resolve(wt), B)
        # set but missing: no fall-through to the checkout's file
        os.environ[watchport.ENV] = os.path.join(self.tmp, 'missing')
        self.assertIsNone(watchport.local_path(wt))
        with self.assertRaises(watchport.Refused):
            watchport.resolve(wt)

    def test_empty_env_refused(self):
        """An empty OMNI_WATCH_LOCAL is refused, never taken as unset (review M2)."""
        main, wt = scratch_repo(self.tmp)
        with open(os.path.join(main, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{A}\n')
        self.use('')
        os.environ[watchport.ENV] = ''
        self.assertIsNone(watchport.local_path(wt))
        with self.assertRaises(watchport.Refused) as cm:
            watchport.resolve(wt)
        self.assertIn('empty', str(cm.exception))

    def test_git_env_ignored(self):
        """GIT_DIR, GIT_COMMON_DIR, GIT_WORK_TREE and GIT_INDEX_FILE in the caller's env never steer the main-checkout
        lookup to another repo (review M1)."""
        main, wt = scratch_repo(self.tmp)
        other = os.path.join(self.tmp, 'other')
        os.makedirs(os.path.join(other, 'tools'))
        git('init', '-q', cwd=other)
        with open(os.path.join(other, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{B} other\n')
        self.addCleanup(env_unset())
        keys = ('GIT_DIR', 'GIT_COMMON_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE')
        saved = {k: os.environ.get(k) for k in keys}

        def restore():
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        self.addCleanup(restore)
        os.environ.update(GIT_DIR=os.path.join(other, '.git'), GIT_COMMON_DIR=os.path.join(other, '.git'),
                          GIT_WORK_TREE=other, GIT_INDEX_FILE=os.path.join(other, '.git', 'index'))
        self.assertIsNone(watchport.local_path(wt))
        with open(os.path.join(main, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{A} one\n')
        self.assertEqual(watchport.resolve(wt), A)

    def test_checkout_then_main_checkout(self):
        main, wt = scratch_repo(self.tmp)
        self.addCleanup(env_unset())
        self.assertIsNone(watchport.local_path(wt))
        with self.assertRaises(watchport.Refused) as cm:
            watchport.resolve(wt)
        self.assertIn('no tools/watch.local', str(cm.exception))
        with open(os.path.join(main, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{A} one\n')
        self.assertEqual(os.path.realpath(watchport.local_path(wt)),
                         os.path.realpath(os.path.join(main, 'tools', 'watch.local')))
        self.assertEqual(watchport.resolve(wt), A)
        self.assertEqual(watchport.resolve(main), A)
        with open(os.path.join(wt, 'tools', 'watch.local'), 'w') as f:   # the checkout's own file comes first
            f.write(f'{B} two\n')
        self.assertEqual(watchport.resolve(wt), B)


FAKE_SERIAL = '''import os
open(os.environ['FAKE_SERIAL_MARK'], 'a').write('imported\\n')
class Serial:
    def __init__(self, *a, **k):
        open(os.environ['FAKE_SERIAL_MARK'], 'a').write('opened %s\\n' % (a[:1] or [k.get('port')])[0])
        raise OSError('fake serial: no port is ever opened')
'''


class ToolCli(Tmp):
    """trace.py, shot.py and shot_set.py capture refuse from the command line (exit 2) before pyserial is even imported: a
    fake `serial` module on PYTHONPATH records any import or open (review M4)."""

    def run_tool(self, argv, watch_text):
        fake = os.path.join(self.tmp, 'fake')
        os.makedirs(fake, exist_ok=True)
        with open(os.path.join(fake, 'serial.py'), 'w') as f:
            f.write(FAKE_SERIAL)
        mark = os.path.join(self.tmp, 'serial-mark')
        if os.path.exists(mark):
            os.remove(mark)
        env = dict(os.environ, PYTHONPATH=fake, FAKE_SERIAL_MARK=mark, PYTHONDONTWRITEBYTECODE='1',
                   **{watchport.ENV: self.file(watch_text)})
        r = subprocess.run([sys.executable, *argv], env=env, capture_output=True, text=True, cwd=self.tmp, timeout=60)
        touched = open(mark).read() if os.path.exists(mark) else ''
        return r, touched

    def test_trace_refuses(self):
        trace = os.path.join(TOOLS, 'trace.py')
        two = f'{A} one\n{B} two\n'
        cases = {
            'two listed, no flag': (['zz_refusal', '--seconds', '1'], 'several watches listed: pass --watch <name>'),
            'ttyACM port': (['zz_refusal', '--seconds', '1', '--port', '/dev/ttyACM0'], '--port /dev/ttyACM0: only'),
            'unknown watch': (['zz_refusal', '--seconds', '1', '--watch', 'three'], '--watch three'),
            'disagree': (['zz_refusal', '--seconds', '1', '--port', A, '--watch', 'two'], 'is not --watch two'),
        }
        for what, (args, msg) in cases.items():
            with self.subTest(what):
                r, touched = self.run_tool([trace, *args], two)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertTrue(r.stderr.startswith('trace: refused: '), r.stderr)
                self.assertIn(msg, r.stderr)
                self.assertEqual(touched, '', 'pyserial was imported or opened before the refusal')
        self.assertFalse(os.path.exists(os.path.join(REPO, 'firmware', 'test', 'host', 'traces', 'zz_refusal.csv')))

    def test_shot_refuses(self):
        """shot.py (final review m4): --port only as a listed path, exactly; --watch a listed name; refused before
        pyserial is imported, and no --out written."""
        shot = os.path.join(TOOLS, 'shot.py')
        out = os.path.join(self.tmp, 'out.png')
        two = f'{A} one\n{B} two\n'
        cases = {
            'two listed, no flag': ([], 'several watches listed: pass --watch <name>'),
            'ttyACM port': (['--port', '/dev/ttyACM0'], '--port /dev/ttyACM0: only'),
            'port plus /': (['--port', A + '/'], 'only'),
            'unknown watch': (['--watch', 'three'], '--watch three'),
            'disagree': (['--port', A, '--watch', 'two'], 'is not --watch two'),
            'bg, ttyACM port': (['--bg', '0', '--port', '/dev/ttyACM0'], '--port /dev/ttyACM0: only'),
        }
        for what, (args, msg) in cases.items():
            with self.subTest(what):
                r, touched = self.run_tool([shot, '--out', out, *args], two)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertTrue(r.stderr.startswith('shot: refused: '), r.stderr)
                self.assertIn(msg, r.stderr)
                self.assertEqual(touched, '', 'pyserial was imported or opened before the refusal')
                self.assertFalse(os.path.exists(out))

    def test_shot_opens_the_listed_port(self):
        """shot.py goes on to open exactly the resolved port: --port as listed, --watch's path, or the one entry (the
        fake serial records the port and refuses to open it)."""
        shot = os.path.join(TOOLS, 'shot.py')
        out = os.path.join(self.tmp, 'out.png')
        cases = {
            '--port listed': (f'{A} one\n{B} two\n', ['--port', B], B),
            '--watch': (f'{A} one\n{B} two\n', ['--watch', 'one'], A),
            'one entry, no flag': (f'{A}\n', [], A),
        }
        for what, (text, args, port) in cases.items():
            with self.subTest(what):
                r, touched = self.run_tool([shot, '--out', out, '--timeout', '1', *args], text)
                self.assertNotEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertIn('fake serial: no port is ever opened', r.stderr)
                self.assertEqual(touched, f'imported\nopened {port}\n')
                self.assertFalse(os.path.exists(out))

    def test_shot_set_capture_refuses(self):
        shot = os.path.join(TOOLS, 'shot_set.py')
        base = ['capture', '--theme', 'omni', '--at', '2026-01-01T00:00:00Z', '--out', os.path.join(self.tmp, 'out'),
                '--end-face', 'x']
        two = f'{A} one\n{B} two\n'
        cases = {
            'two listed, no flag': ([], 'several watches listed: pass --watch <name>'),
            'ttyACM port': (['--port', '/dev/ttyACM0'], '--port /dev/ttyACM0: only'),
            'port plus /': (['--port', A + '/'], 'only'),
            'unknown watch': (['--watch', 'three'], '--watch three'),
            'disagree': (['--port', A, '--watch', 'two'], 'is not --watch two'),
        }
        for what, (args, msg) in cases.items():
            with self.subTest(what):
                r, touched = self.run_tool([shot, *base, *args], two)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertTrue(r.stdout.startswith('refused: '), r.stdout)
                self.assertIn(msg, r.stdout)
                self.assertEqual(touched, '', 'pyserial was imported or opened before the refusal')
                self.assertFalse(os.path.exists(os.path.join(self.tmp, 'out')))


class SuiteIsolation(Tmp):
    def test_suite_never_reads_real_file(self):
        """Each port-tool test module, imported in a fresh process with OMNI_WATCH_LOCAL unset from a worktree whose
        main checkout holds a poisoned tools/watch.local, points OMNI_WATCH_LOCAL at its own temp file in
        setUpModule, so none of its tests or subprocesses can reach the main checkout's file."""
        for name in os.listdir(TOOLS):   # a new test module that runs a port tool must be listed here
            if name.startswith('test_') and name.endswith('.py'):
                with open(os.path.join(TOOLS, name)) as f:
                    if PORT_TOOL.search(f.read()):
                        self.assertIn(name[:-3], PORT_TEST_MODULES, f'{name} runs a port tool: list it')
        main, wt = scratch_repo(self.tmp)
        poison = '/dev/serial/by-id/usb-poisoned_main_checkout-if00'
        with open(os.path.join(main, 'tools', 'watch.local'), 'w') as f:
            f.write(f'{poison} poison\n')
        for name in os.listdir(TOOLS):
            if name.endswith('.py'):
                shutil.copy(os.path.join(TOOLS, name), os.path.join(wt, 'tools', name))
        env = {k: v for k, v in os.environ.items() if k != watchport.ENV}
        env['PYTHONDONTWRITEBYTECODE'] = '1'
        probe = ('import json, os, sys; sys.dont_write_bytecode = True; sys.path.insert(0, "tools"); '
                 'import watchport; before = watchport.resolve("."); '
                 'm = __import__(sys.argv[1]); m.setUpModule(); '
                 'p = os.environ.get(watchport.ENV); '
                 'print(json.dumps([before, p, watchport.local_path("."), watchport.resolve(".")])); '
                 'getattr(m, "tearDownModule", lambda: None)()')
        for mod in PORT_TEST_MODULES:
            with self.subTest(mod):
                r = subprocess.run([sys.executable, '-c', probe, mod], cwd=wt, env=env, capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                before, p, found, port = json.loads(r.stdout.strip().splitlines()[-1])
                self.assertEqual(before, poison, 'control: the poisoned main-checkout file is what C7 finds')
                self.assertIsNotNone(p, f'{mod}.setUpModule left {watchport.ENV} unset')
                self.assertEqual(found, p)
                self.assertNotEqual(os.path.realpath(p), os.path.realpath(os.path.join(main, 'tools', 'watch.local')))
                self.assertNotEqual(port, poison)


if __name__ == '__main__':
    unittest.main()
