#!/usr/bin/env python3
"""Tests for tools/gen_sfx_dw.py (dw wave 1 Task 8). Run from the repo root:
    python -m unittest tools/test_gen_sfx_dw.py
"""
import ast
import cmath
import contextlib
import io
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPT = os.path.join(HERE, 'gen_sfx_dw.py')
ASSETS = os.path.join(ROOT, 'firmware', 'apps', 'dw', 'assets')
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import gen_sfx_dw                                  # noqa: E402
import terms                                       # noqa: E402

DW_TERMS = terms.load(ROOT, 'dw_terms')            # None in a public tree (the test skips); TermsMissing elsewhere

LIMITS = {name: limit for name, _, limit in gen_sfx_dw.EFFECTS}   # the script's own limits, not a copy
SR = 16000


def run(*args):
    return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True,
                          env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


def slurp(path, mode='rb'):
    with open(path, mode) as f:
        return f.read()


def samples(path):
    data = slurp(path)
    assert len(data) % 2 == 0
    return struct.unpack(f'<{len(data) // 2}h', data)


def fft(x):
    n = len(x)
    if n == 1:
        return x
    ev, od = fft(x[0::2]), fft(x[1::2])
    t = [cmath.exp(-2j * math.pi * k / n) * od[k] for k in range(n // 2)]
    return [ev[k] + t[k] for k in range(n // 2)] + [ev[k] - t[k] for k in range(n // 2)]


class GenSfxDw(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = cls.tmp.name
        r = run('write', cls.out)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_lengths_and_peaks(self):
        import gen_sfx
        for name, limit in LIMITS.items():
            s = samples(os.path.join(self.out, name + '.pcm'))
            n = len(s)
            self.assertLessEqual(n / SR, limit, name)
            self.assertGreater(n, 0.5 * limit * SR if name != 'dw_tick' else 100, name)
            db = 20 * math.log10(max(abs(v) for v in s) / 32767)
            self.assertAlmostEqual(db, -3.0, delta=0.1, msg=name)
            for i in list(range(80)) + list(range(n - 80, n)):
                self.assertLessEqual(abs(s[i]), gen_sfx.TARGET * gen_sfx.ramp(i, n) + 1, f'{name}[{i}]')

    def test_no_audio_inputs(self):
        tree = ast.parse(slurp(SCRIPT, 'r'))
        mods = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                mods.update(a.name.split('.')[0] for a in node.names)
            elif isinstance(node, ast.ImportFrom):
                mods.add((node.module or '').split('.')[0])
        self.assertFalse(mods & {'wave', 'soundfile', 'audioread', 'aifc', 'sunau', 'sndhdr', 'pydub', 'scipy'}, mods)
        # write: exactly the four outputs, nothing else
        self.assertEqual(sorted(os.listdir(self.out)), sorted(n + '.pcm' for n in LIMITS))
        # compare never writes; manifest writes only its json
        before = {f: os.stat(os.path.join(self.out, f)).st_mtime_ns for f in os.listdir(self.out)}
        self.assertEqual(run('compare', self.out).returncode, 0)
        with tempfile.TemporaryDirectory() as t:
            j = os.path.join(t, 'sfx.json')
            r = run('manifest', self.out, j)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(os.listdir(t), ['sfx.json'])
            ents = json.loads(slurp(j, 'r'))
            self.assertEqual([e['file'] for e in ents], [n + '.pcm' for n in LIMITS])
            for e in ents:
                self.assertEqual((e['cf'], e['rate'], e['channels'], e['format']), ('PCM', SR, 1, 's16le'))
        after = {f: os.stat(os.path.join(self.out, f)).st_mtime_ns for f in os.listdir(self.out)}
        self.assertEqual(before, after)

    def test_open_audit(self):
        """The script opens nothing but the committed outputs (compare, manifest) and the files it writes."""
        import builtins
        real, seen = builtins.open, []

        def spy(file, mode='r', *a, **k):
            seen.append((os.path.abspath(os.fspath(file)), mode))
            return real(file, mode, *a, **k)

        outs = {os.path.abspath(os.path.join(self.out, n + '.pcm')) for n in LIMITS}
        with tempfile.TemporaryDirectory() as t:
            j = os.path.abspath(os.path.join(t, 'sfx.json'))
            for argv, reads, writes in ((['write', self.out], set(), outs),
                                        (['compare', self.out], outs, set()),
                                        (['manifest', self.out, j], outs, {j})):
                seen.clear()
                builtins.open, old = spy, sys.argv
                sys.argv = ['gen_sfx_dw.py'] + argv
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        gen_sfx_dw.main()
                finally:
                    builtins.open, sys.argv = real, old
                for path, mode in seen:
                    if any(c in mode for c in 'wax+'):
                        self.assertIn(path, writes, (argv[0], path, mode))
                    else:
                        self.assertIn(path, reads | writes, (argv[0], path, mode))
                self.assertTrue({p for p, m in seen if 'w' in m} == writes, argv[0])

    def test_tails_fade_out(self):
        """The chime and the latch fall silent before their last 5 %: the 5 ms edge never cuts them off."""
        for name in ('dw_chime', 'dw_latch'):
            s = samples(os.path.join(self.out, name + '.pcm'))
            tail = s[-len(s) // 20:]
            rms = math.sqrt(sum(v * v for v in tail) / len(tail))
            self.assertLessEqual(20 * math.log10(max(rms, 1e-9) / 32767), -40.0, name)

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertEqual(run('write', t).returncode, 0)
            for n in LIMITS:
                self.assertEqual(slurp(os.path.join(t, n + '.pcm')),
                                 slurp(os.path.join(self.out, n + '.pcm')), n)

    def test_committed_match_or_warn(self):
        r = run('compare', ASSETS)
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = [l for l in r.stdout.splitlines() if l.strip()]
        self.assertEqual(len(lines), 4)
        for l in lines:
            self.assertTrue('matches the generator' in l or 'WARN' in l, l)
            self.assertNotIn('missing', l)

    def chime_samples(self):
        return [float(v) for v in samples(os.path.join(self.out, 'dw_chime.pcm'))]

    def test_chime_length(self):
        n = len(samples(os.path.join(self.out, 'dw_chime.pcm')))
        self.assertEqual(n, 44800)
        self.assertLessEqual(n, 48000)
        self.assertEqual(LIMITS['dw_chime'], 3.0)

    def test_chime_peak(self):
        s = self.chime_samples()
        self.assertAlmostEqual(20 * math.log10(max(abs(v) for v in s) / 32767), -3.0, delta=0.05)

    def test_chime_tail(self):
        s = self.chime_samples()
        tail = s[-len(s) // 20:]
        rms = math.sqrt(sum(v * v for v in tail) / len(tail))
        self.assertLessEqual(20 * math.log10(max(rms, 1e-9) / 32767), -40.0)

    def test_chime_fundamental(self):
        """The strongest line of 0.1-0.6 s is the 330 Hz fundamental (above the speaker's roll-off)."""
        s = self.chime_samples()[int(0.1 * SR):int(0.6 * SR)]
        n = 1 << 15
        spec = fft(s + [0.0] * (n - len(s)))
        k = max(range(1, n // 2), key=lambda j: abs(spec[j]))
        self.assertAlmostEqual(k * SR / n, 330.0, delta=5.0)

    def test_chime_deterministic(self):
        a = gen_sfx_dw.finish(gen_sfx_dw.chime())
        b = gen_sfx_dw.finish(gen_sfx_dw.chime())
        self.assertEqual(a, b)

    def test_committed_chime(self):
        want = gen_sfx_dw.finish(gen_sfx_dw.chime())
        self.assertEqual(slurp(os.path.join(ASSETS, 'dw_chime.pcm')), struct.pack(f'<{len(want)}h', *want))

    def test_chime_plan_fits(self):
        n = len(samples(os.path.join(self.out, 'dw_chime.pcm')))
        p = len(samples(os.path.join(self.out, 'dw_ping.pcm')))
        self.assertLessEqual(n / SR * 1000, 3440)
        self.assertLessEqual(n / SR + 11 * 0.360 + p / SR, 7.6)

    @unittest.skipUnless(DW_TERMS, 'tools/dw_terms.py (private) is absent: nothing to check against')
    def test_chime_text_neutral(self):
        text = slurp(SCRIPT, 'r').lower()
        self.assertTrue(DW_TERMS.OLD_CHIME_WORDS)
        for w in DW_TERMS.OLD_CHIME_WORDS:
            self.assertNotIn(w, text)


if __name__ == '__main__':
    unittest.main()
