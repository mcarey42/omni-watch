#!/usr/bin/env python3
"""tools/release_check.py, the release guard (public release Task 14), on temp trees and invented words only.

The fixture terms are invented ("zebra quokka" as a term, "zzq" as a word); no real term list is read here. A fixture
that must hold a quoted piece pair is assembled at run time, so this file never matches the guard's piece pattern.

    python -m unittest tools/test_release_check.py
"""
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import release_check as rc  # noqa: E402

TOOL = os.path.join(HERE, 'release_check.py')
Q = chr(39)   # a single quote: the piece fixtures are built with it, never written out


def terms_text(*extra):
    return '\n'.join(['# fixture terms', 'term\tzebra quokka', 'word\tzzq'] + list(extra)) + '\n'


class Fixture(unittest.TestCase):
    """A temp dir holding TREE (a minimal public tree: the marker and an exclude list) and a terms file beside it."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.tree = os.path.join(self.root, 'tree')
        self.put('PUBLIC_TREE', 'marker\n')
        self.put('tools/public_exclude.txt', '# fixture\ndocs/superpowers/**\n')
        self.terms_path = os.path.join(self.root, 'terms.txt')
        self.set_terms()

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, data, base=None):
        p = os.path.join(base or self.tree, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data.encode() if isinstance(data, str) else data)
        return p

    def set_terms(self, *extra):
        with open(self.terms_path, 'w') as f:
            f.write(terms_text(*extra))
        self.t = rc.load_terms(self.terms_path)

    def problems(self, overlay=None):
        return rc.tree_problems(self.tree, self.t, overlay)

    def run_cli(self, *args):
        return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True)


def fixture_terms():
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(terms_text())
    try:
        return rc.load_terms(f.name)
    finally:
        os.unlink(f.name)


class TextSeparators(unittest.TestCase):
    def test_each_separator_hits_at_the_start_line(self):
        t = fixture_terms()
        for text in ('zebra quokka', 'ZEBRA_QUOKKA', 'zebra-quokka', 'zebra\nquokka'):
            got = [ln for ln, _ in rc.text_hits('first\n' + text + '\nlast\n', t)]
            self.assertEqual(got, [2], repr(text))

    def test_other_words_miss(self):
        t = fixture_terms()
        self.assertEqual(rc.text_hits('zebra ; quokka\nzebra=quokka\nzebra (quokka)\n', t), [])

    def test_quotes_commas_and_colons_separate_too(self):   # review finding 4
        t = fixture_terms()
        q = chr(34)
        for text in (q + 'zebra ' + q + ' ' + q + 'quokka' + q, q + 'zebra' + q + ' + ' + q + 'quokka' + q,
                     Q + 'zebra' + Q + ', ' + Q + 'quokka' + Q, 'zebra, quokka', 'zebra: quokka', 'zebra:quokka',
                     'x = ' + q + 'zebra ' + q + '\n    ' + q + 'quokka' + q):
            got = [ln for ln, _ in rc.text_hits('first\n' + text + '\n', t)]
            self.assertEqual(got, [2], repr(text))

    def test_in_binaries_too(self):
        t = fixture_terms()
        self.assertTrue(rc.bytes_hits(b'\x00"zebra","quokka"\x00', t))


class TextLeaders(unittest.TestCase):
    def test_a_term_wrapped_behind_a_leader_hits(self):   # preflight I5
        t = fixture_terms()
        for text in ('zebra\n * quokka', 'zebra\n# quokka', 'zebra\n// quokka', 'zebra\n-- quokka', 'zebra\n; quokka',
                     'zebra\n" quokka', 'zebra\n' + Q + ' quokka'):
            got = [ln for ln, _ in rc.text_hits('x\n' + text + '\n', t)]
            self.assertEqual(got, [2], repr(text))


class TextWordBounds(unittest.TestCase):
    def test_word_bounds(self):   # preflight I4
        t = fixture_terms()
        self.assertTrue(rc.text_hits('a zzq b', t))
        self.assertFalse(rc.text_hits('azzqb', t))
        self.assertFalse(rc.text_hits('x 7zzq5 y', t))
        self.assertTrue(rc.text_hits('the zzq said', t))
        self.assertFalse(rc.text_hits('x_zzq', t))
        self.assertTrue(rc.text_hits('(ZZQ)', t))


class BinaryRuns(unittest.TestCase):
    def test_word_needs_a_run_of_eight(self):
        t = fixture_terms()
        self.assertEqual(rc.bytes_hits(b'\x00a zzq\x00', t), [])            # a 5-character run
        self.assertTrue(rc.bytes_hits(b'\x00abc zzq !\x00', t))           # a 9-character run, word-bounded
        self.assertEqual(rc.bytes_hits(b'\x00abczzqdef\x00', t), [])       # 9 characters, not bounded

    def test_term_hits_in_a_run_of_four(self):
        t = rc.load_terms_text('term\tquok\n', 'fixture')
        self.assertEqual([o for o, _ in rc.bytes_hits(b'\xff\x00quok\x01', t)], [2])
        self.assertEqual(rc.bytes_hits(b'\xffquo\x00k\x01', t), [])


class Paths(Fixture):
    def test_a_file_named_with_a_term_hits(self):
        self.put('src/x-zebraquokka.c', 'int x;\n')
        self.assertTrue(any('x-zebraquokka.c' in p and 'path' in p for p in self.problems()), self.problems())

    def test_a_clean_tree_passes(self):
        self.put('src/a.c', 'int x;\n')
        self.put('assets/a.bin', b'\x00\x01\x02abcdefgh\x00')
        self.assertEqual(self.problems(), [])


class Allow(Fixture):
    def test_the_hash_matches_then_one_more_byte_hits(self):
        body = b'the zzq said\n'
        self.put('LICENSE', body)
        self.set_terms('allow\tLICENSE\t%s\tthe fixture owner' % hashlib.sha256(body).hexdigest())
        self.assertEqual(self.problems(), [])
        self.assertIn('LICENSE', ''.join(self.t.used))
        self.put('LICENSE', body + b'x')
        self.assertTrue(any(p.startswith('LICENSE') for p in self.problems()), self.problems())

    def test_the_allow_is_printed_with_its_reason(self):
        body = b'the zzq said\n'
        self.put('LICENSE', body)
        self.set_terms('allow\tLICENSE\t%s\tthe fixture owner' % hashlib.sha256(body).hexdigest())
        r = self.run_cli('--terms', self.terms_path, self.tree)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('the fixture owner', r.stdout)


class Excluded(Fixture):
    def test_an_excluded_path_fails(self):
        self.put('docs/superpowers/x.md', 'notes\n')
        self.assertTrue(any('docs/superpowers/x.md' in p and 'exclude' in p for p in self.problems()), self.problems())

    def test_the_globs(self):
        rx = rc.glob_regex
        self.assertTrue(rx('docs/superpowers/**').fullmatch('docs/superpowers/a/b.md'))
        self.assertTrue(rx('docs/*-acceptance.md').fullmatch('docs/s3-acceptance.md'))
        self.assertFalse(rx('docs/*-acceptance.md').fullmatch('docs/x/s3-acceptance.md'))
        self.assertTrue(rx('design/notes-*.md').fullmatch('design/notes-omni.md'))
        self.assertFalse(rx('design/notes-*.md').fullmatch('design/sub/notes-omni.md'))
        self.assertTrue(rx('**/x.png').fullmatch('a/b/x.png'))
        self.assertTrue(rx('**/x.png').fullmatch('x.png'))


class NoMarker(Fixture):
    def test_a_missing_marker_fails(self):
        os.unlink(os.path.join(self.tree, 'PUBLIC_TREE'))
        self.assertTrue(any('PUBLIC_TREE' in p for p in self.problems()), self.problems())


class Secrets(Fixture):
    def test_a_pem_header_fails(self):
        self.put('k.txt', '-' * 5 + 'BEGIN RSA PRIVATE KEY' + '-' * 5 + '\nabc\n')
        self.assertTrue(any('k.txt' in p and 'secret' in p for p in self.problems()), self.problems())

    def test_a_token_fails(self):
        self.put('t.json', '{"a": "gh' + 'p_' + 'A1b2' * 9 + '"}\n')
        self.put('a.bin', b'\x00\x01AK' + b'IA' + b'ABCDEFGHIJKLMNOP\x00')
        got = self.problems()
        self.assertTrue(any(p.startswith('t.json') for p in got), got)
        self.assertTrue(any(p.startswith('a.bin') for p in got), got)


class Overlay(Fixture):
    def test_a_file_equal_to_an_overlay_file_fails(self):
        art = os.path.join(self.root, 'art')
        self.put('assets/x.bin', b'\x19same bytes', base=art)
        self.put('out/y.bin', b'\x19out bytes', base=art)
        self.put('.git/z.bin', b'\x19git bytes', base=art)
        self.put('firmware/x.bin', b'\x19same bytes')
        self.put('firmware/y.bin', b'\x19out bytes')
        self.put('firmware/z.bin', b'\x19git bytes')
        got = self.problems(overlay=art)
        self.assertEqual([p.split(':')[0] for p in got], ['firmware/x.bin'], got)

    def test_two_empty_files_do_not_count(self):   # M11
        art = os.path.join(self.root, 'art')
        self.put('empty', b'', base=art)
        self.put('firmware/empty.txt', b'')
        self.assertEqual(self.problems(overlay=art), [])


class Pieces(Fixture):
    PY = 'x = ' + Q + 'zeb' + Q + ' + ' + Q + 'raq' + Q + '\n'

    def test_quoted_pieces_and_shell_appends_fail(self):   # K1
        self.put('tools/a.py', self.PY)
        self.put('tools/b.sh', 'n1+=ab\n')
        got = self.problems()
        self.assertTrue(any(p.startswith('tools/a.py') and 'piece' in p for p in got), got)
        self.assertTrue(any(p.startswith('tools/b.sh') and 'piece' in p for p in got), got)

    def test_with_an_allow_they_pass(self):
        self.put('tools/a.py', self.PY)
        self.put('tools/b.sh', 'n1+=ab\n')
        sha = lambda s: hashlib.sha256(s.encode()).hexdigest()
        self.set_terms('allow\ttools/a.py\t%s\tan ordinary concatenation, read by hand' % sha(self.PY),
                       'allow\ttools/b.sh\t%s\tan ordinary append, read by hand' % sha('n1+=ab\n'))
        self.assertEqual(self.problems(), [])

    def test_an_append_in_c_passes(self):
        self.put('src/a.c', 'a+=b;\n')
        self.put('src/b.mjs', 'a+=b;\n')
        self.assertEqual(self.problems(), [])


class Symlinks(Fixture):   # review finding 2: a symlink's name and target are scanned, and it is never followed
    def test_a_symlinked_dir_named_with_a_term(self):
        os.makedirs(os.path.join(self.root, 'elsewhere'))
        os.symlink(os.path.join(self.root, 'elsewhere'), os.path.join(self.tree, 'zebraquokka'))
        got = self.problems()
        self.assertTrue(any(p.startswith('zebraquokka: path') for p in got), got)

    def test_a_symlink_target_holding_a_term(self):
        os.symlink('../zebra_quokka/x', os.path.join(self.tree, 'link'))
        got = self.problems()
        self.assertTrue(any(p.startswith('link:1: term') for p in got), got)

    def test_a_symlink_is_not_followed(self):
        outside = self.put('outside/x.txt', 'the zzq said\n', base=self.root)
        os.symlink(outside, os.path.join(self.tree, 'f'))
        os.symlink(os.path.dirname(outside), os.path.join(self.tree, 'd'))
        self.assertEqual(self.problems(), [])


class SkipsGit(Fixture):
    def test_a_hit_under_dot_git_is_ignored(self):   # I6
        self.put('.git/objects/zebraquokka', 'the zzq said\n')
        self.put('.git/HEAD', 'zebra quokka\n')
        self.assertEqual(self.problems(), [])


class Refusals(Fixture):
    def test_no_terms(self):
        r = self.run_cli(self.tree)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_terms_inside_the_tree(self):
        inside = self.put('terms.txt', terms_text())
        r = self.run_cli('--terms', inside, self.tree)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_a_bad_line(self):
        for bad in ('wurd\tzzq', 'term', 'regex\t(unclosed', 'allow\tLICENSE\tnothex\twhy', 'allow\tLICENSE\t' + 'a' * 64,
                    'word\ttwo words'):
            with open(self.terms_path, 'w') as f:
                f.write(terms_text(bad))
            r = self.run_cli('--terms', self.terms_path, self.tree)
            self.assertEqual(r.returncode, 2, '%r: %s' % (bad, r.stdout + r.stderr))

    def test_no_entries(self):   # review finding 3
        for text in ('', '# only comments\n\n', 'allow\tLICENSE\t%s\twhy\n' % ('a' * 64)):
            with open(self.terms_path, 'w') as f:
                f.write(text)
            r = self.run_cli('--terms', self.terms_path, self.tree)
            self.assertEqual(r.returncode, 2, '%r: %s' % (text, r.stdout + r.stderr))

    def test_a_missing_tree(self):
        r = self.run_cli('--terms', self.terms_path, os.path.join(self.root, 'nope'))
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_the_exit_codes(self):
        self.assertEqual(self.run_cli('--terms', self.terms_path, self.tree).returncode, 0)
        self.put('a.txt', 'the zzq said\n')
        r = self.run_cli('--terms', self.terms_path, self.tree)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertNotIn('zzq', r.stdout + r.stderr)   # the report names the terms file's line, never the term


class Also(Fixture):
    def test_also_files_are_scanned_as_binaries(self):
        img = self.put('img.bin', b'\x00\x01xx zebra quokka xx\x00', base=self.root)
        r = self.run_cli('--terms', self.terms_path, '--also', img, self.tree)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)


class Selftest(unittest.TestCase):
    def test_selftest_passes(self):
        r = subprocess.run([sys.executable, TOOL, '--selftest'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == '__main__':
    unittest.main()
