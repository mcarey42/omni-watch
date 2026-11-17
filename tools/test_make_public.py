#!/usr/bin/env python3
"""tools/make_public.sh (public release Task 14), against a scratch repo with a fixture terms file.

Every run happens in a temp dir: a scratch git repo holding copies of make_public.sh and release_check.py, an invented
terms file beside it, and an OUTDIR under the same temp dir. Nothing here touches this checkout's history, creates a
remote or runs gh (a fake gh on PATH records any call).

    python -m unittest tools/test_make_public.py
"""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, 'make_public.sh')
GUARD = os.path.join(HERE, 'release_check.py')

EXCLUDE = '# fixture\ndocs/superpowers/**\nresearch/**\ndesign/shots/bad-*.png  # the fixture\'s excluded shot\n'


def git(repo, *args, env=None):
    r = subprocess.run(['git', '-C', repo] + list(args), capture_output=True, text=True, env=env)
    if r.returncode:
        raise AssertionError('git %s: %s' % (' '.join(args), r.stderr))
    return r.stdout


class Scratch(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.realpath(self.tmp.name)
        self.repo = os.path.join(self.root, 'repo')
        os.makedirs(os.path.join(self.repo, 'tools'))
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
        self.env.update(GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_NOSYSTEM='1', PYTHONDONTWRITEBYTECODE='1',
                        PUBLIC_AUTHOR_NAME='Pub Lisher', PUBLIC_AUTHOR_EMAIL='pub@example.invalid',
                        PUBLIC_REPO='someone/thing')
        # a fake gh that records any call
        self.bin = os.path.join(self.root, 'bin')
        os.makedirs(self.bin)
        self.gh_log = os.path.join(self.root, 'gh.log')
        with open(os.path.join(self.bin, 'gh'), 'w') as f:
            f.write('#!/bin/sh\necho "$@" >> %s\n' % self.gh_log)
        os.chmod(os.path.join(self.bin, 'gh'), 0o755)
        self.env['PATH'] = self.bin + os.pathsep + self.env.get('PATH', '')
        genv = dict(self.env, GIT_AUTHOR_NAME='Scratch', GIT_AUTHOR_EMAIL='s@example.invalid',
                    GIT_COMMITTER_NAME='Scratch', GIT_COMMITTER_EMAIL='s@example.invalid')
        self.genv = genv
        git(self.repo, 'init', '-q', '-b', 'main', env=genv)
        shutil.copy(SCRIPT, os.path.join(self.repo, 'tools', 'make_public.sh'))
        shutil.copy(GUARD, os.path.join(self.repo, 'tools', 'release_check.py'))
        self.write('tools/public_exclude.txt', EXCLUDE)
        self.write('README.md', 'a public readme\n')
        self.write('src/a.c', 'int a;\n')
        self.write('docs/superpowers/plan.md', 'a private plan\n')
        self.write('research/x/notes.md', 'private notes\n')
        self.write('design/shots/bad-1.png', b'\x89PNG\x00\x01')
        self.write('design/shots/good-1.png', b'\x89PNG\x00\x02')
        self.commit('initial')
        self.terms = os.path.join(self.root, 'terms.txt')
        with open(self.terms, 'w') as f:
            f.write('term\tmongoose lark\nword\txqv\n')
        self.out = os.path.join(self.root, 'out')

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, data):
        p = os.path.join(self.repo, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data.encode() if isinstance(data, str) else data)

    def commit(self, msg):
        git(self.repo, 'add', '-A', env=self.genv)
        git(self.repo, 'commit', '-q', '-m', msg, env=self.genv)

    def run_tool(self, *args, env=None, out=None):
        argv = ['bash', os.path.join(self.repo, 'tools', 'make_public.sh'), '--terms', self.terms] + list(args)
        argv.append(out or self.out)
        return subprocess.run(argv, capture_output=True, text=True, env=env or self.env, cwd=self.root)

    def files(self, top):
        got = set()
        for d, dirs, fs in os.walk(top):
            dirs[:] = [x for x in dirs if not (d == top and x == '.git')]
            got.update(os.path.relpath(os.path.join(d, f), top) for f in fs)
        return got


class Export(Scratch):
    def test_exports_ref_not_worktree(self):
        self.write('uncommitted.txt', 'not in the ref\n')
        self.write('README.md', 'an uncommitted edit\n')
        r = self.run_tool('--ref', 'main')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn('uncommitted.txt', self.files(self.out))
        with open(os.path.join(self.out, 'README.md')) as f:
            self.assertEqual(f.read(), 'a public readme\n')

    def test_excludes(self):
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        got = self.files(self.out)
        self.assertEqual(got, {'README.md', 'src/a.c', 'design/shots/good-1.png', 'tools/make_public.sh',
                               'tools/release_check.py', 'tools/public_exclude.txt', 'PUBLIC_TREE'})

    def test_default_ref_is_main(self):
        git(self.repo, 'checkout', '-q', '-b', 'side', env=self.genv)
        self.write('side.txt', 'only on side\n')
        self.commit('side')
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn('side.txt', self.files(self.out))

    def test_exclude_list_from_ref(self):   # I3
        self.write('tools/public_exclude.txt', '# emptied, uncommitted\n')
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        got = self.files(self.out)
        self.assertNotIn('docs/superpowers/plan.md', got)
        self.assertNotIn('research/x/notes.md', got)
        self.assertNotIn('design/shots/bad-1.png', got)

    def test_commit_identity_and_no_remote(self):   # M9
        env = dict(self.env, GIT_AUTHOR_NAME='Wrong Name', GIT_COMMITTER_EMAIL='wrong@example.invalid',
                   GIT_AUTHOR_DATE='2001-01-01T00:00:00+0530', TZ='Asia/Kolkata')
        r = self.run_tool(env=env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        fmt = '%an|%ae|%cn|%ce|%ad|%cd|%s'
        log = git(self.out, 'log', '--format=' + fmt, '--date=raw', env=self.env).strip().splitlines()
        self.assertEqual(len(log), 1, log)
        an, ae, cn, ce, ad, cd, s = log[0].split('|')
        self.assertEqual((an, ae, cn, ce, s), ('Pub Lisher', 'pub@example.invalid', 'Pub Lisher', 'pub@example.invalid',
                                               'Initial public release'))
        self.assertTrue(ad.endswith('+0000') and cd.endswith('+0000'), (ad, cd))
        self.assertGreater(int(ad.split()[0]), 1700000000)
        self.assertEqual(git(self.out, 'remote', env=self.env).strip(), '')
        self.assertEqual(git(self.out, 'status', '--porcelain', env=self.env).strip(), '')
        self.assertEqual(git(self.out, 'branch', '--show-current', env=self.env).strip(), 'main')

    def test_inherited_git_env_steers_nothing(self):   # review finding 6
        decoy = os.path.join(self.root, 'decoy')
        os.makedirs(decoy)
        git(decoy, 'init', '-q', '-b', 'main', env=self.genv)
        with open(os.path.join(decoy, 'decoy.txt'), 'w') as f:
            f.write('decoy\n')
        git(decoy, 'add', '-A', env=self.genv)
        git(decoy, 'commit', '-q', '-m', 'decoy', env=self.genv)
        home = os.path.join(self.root, 'home')
        tmpl = os.path.join(self.root, 'tmpl')
        os.makedirs(home)
        os.makedirs(tmpl)
        with open(os.path.join(tmpl, 'sentinel'), 'w') as f:
            f.write('from the template\n')
        with open(os.path.join(home, '.gitconfig'), 'w') as f:
            f.write('[init]\n\ttemplateDir = %s\n[i18n]\n\tcommitEncoding = ISO-8859-1\n' % tmpl)
        env = {k: v for k, v in self.env.items() if k not in ('GIT_CONFIG_GLOBAL',)}
        env.update(HOME=home, GIT_DIR=os.path.join(decoy, '.git'), GIT_WORK_TREE=decoy,
                   GIT_INDEX_FILE=os.path.join(self.root, 'stray-index'))
        r = self.run_tool(env=env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        got = self.files(self.out)
        self.assertIn('README.md', got)
        self.assertNotIn('decoy.txt', got)
        self.assertFalse(os.path.exists(os.path.join(self.out, '.git', 'sentinel')), 'the global template applied')
        enc = git(self.out, 'cat-file', 'commit', 'HEAD', env=self.env)
        self.assertNotIn('encoding', enc)
        self.assertEqual(git(decoy, 'log', '--format=%s', env=self.genv).strip(), 'decoy')

    def test_prints_not_runs(self):
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('gh repo create someone/thing --public --source %s --push' % self.out, r.stdout)
        self.assertFalse(os.path.exists(self.gh_log), 'gh was run')

    def test_marker_written(self):
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(os.path.join(self.out, 'PUBLIC_TREE')) as f:
            self.assertEqual(f.read(), 'Exported by tools/make_public.sh. Term scans that need private lists are '
                                       'skipped here.\n')


class Stops(Scratch):
    def test_guard_failure_stops_before_init(self):
        self.write('src/b.c', '/* the xqv said */\n')
        self.commit('a hit')
        r = self.run_tool()
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(os.path.isfile(os.path.join(self.out, 'src/b.c')), 'the tree stays for inspection')
        self.assertFalse(os.path.exists(os.path.join(self.out, '.git')))
        self.assertFalse(os.path.exists(self.gh_log))

    def test_file_count_matches(self):   # M9: a tree .gitignore that would drop a file fails the export
        self.write('.gitignore', '*.log\n')
        self.write('keep.log', 'tracked despite the pattern\n')
        git(self.repo, 'add', '-f', 'keep.log', env=self.genv)
        self.commit('a tracked ignored file')
        r = self.run_tool()
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('count', (r.stdout + r.stderr).lower())


class Refusals(Scratch):
    def assertRefused(self, r, out=None):
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(os.path.join(out or self.out, '.git')))

    def test_refuses_nonempty_out(self):
        os.makedirs(self.out)
        with open(os.path.join(self.out, 'x'), 'w') as f:
            f.write('x')
        r = self.run_tool()
        self.assertRefused(r)
        self.assertEqual(os.listdir(self.out), ['x'])

    def test_refuses_out_inside_checkout(self):
        inside = os.path.join(self.repo, 'export')
        r = self.run_tool(out=inside)
        self.assertRefused(r, inside)
        self.assertFalse(os.path.exists(inside))
        # a sibling worktree counts as a checkout too
        wt = os.path.join(self.root, 'wt')
        git(self.repo, 'worktree', 'add', '-q', '--detach', wt, env=self.genv)
        r = self.run_tool(out=os.path.join(wt, 'sub', 'export'))
        self.assertRefused(r, os.path.join(wt, 'sub', 'export'))
        self.assertFalse(os.path.exists(os.path.join(wt, 'sub')))

    def test_refuses_missing_env(self):
        for k in ('PUBLIC_AUTHOR_NAME', 'PUBLIC_AUTHOR_EMAIL', 'PUBLIC_REPO'):
            env = {a: b for a, b in self.env.items() if a != k}
            r = self.run_tool(env=env)
            self.assertRefused(r)
            self.assertFalse(os.path.exists(self.out), k)

    def test_refuses_bad_ref(self):
        r = self.run_tool('--ref', 'no-such-ref')
        self.assertRefused(r)
        self.assertFalse(os.path.exists(self.out))

    def test_refuses_no_terms(self):
        argv = ['bash', os.path.join(self.repo, 'tools', 'make_public.sh'), self.out]
        r = subprocess.run(argv, capture_output=True, text=True, env=self.env, cwd=self.root)
        self.assertRefused(r)
        self.assertFalse(os.path.exists(self.out))


if __name__ == '__main__':
    unittest.main()
