#!/usr/bin/env python3
"""Tests for tools/gen_sfx_rd.py (rd wave 1 Task 8: the hourly chime, both voicings). Run from the repo root:
    python -m unittest tools/test_gen_sfx_rd.py
"""
import ast
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
SCRIPT = os.path.join(HERE, 'gen_sfx_rd.py')
ASSETS = os.path.join(ROOT, 'firmware', 'apps', 'rd', 'assets')
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import gen_sfx                                     # noqa: E402
import gen_sfx_rd                                  # noqa: E402

SR = 16000
CHIME_BYTES = 76800                                # 2.4 s of s16le mono at 16 kHz
FRAME = 800                                        # 50 ms frames: 20 Hz DFT bins


def run(*args):
    return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True,
                          env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


def slurp(path, mode='rb'):
    with open(path, mode) as f:
        return f.read()


def pcm(data):
    assert len(data) % 2 == 0
    return struct.unpack(f'<{len(data) // 2}h', data)


def band_shares(x, t0, t1, bands):
    """Energy shares of x over [t0, t1) s in 50 ms frames: a direct DFT of each frame's bins up to 2 kHz (20 Hz
    apart), against the frame's whole energy (Parseval). bands: (lo_hz, hi_hz) inclusive, each its share of the total."""
    tot = 0.0
    got = [0.0] * len(bands)
    kmax = 2000 // (SR // FRAME)
    cos = [math.cos(2 * math.pi * j / FRAME) for j in range(FRAME)]
    sin = [math.sin(2 * math.pi * j / FRAME) for j in range(FRAME)]
    for s0 in range(round(t0 * SR), round(t1 * SR) - FRAME + 1, FRAME):
        fr = x[s0:s0 + FRAME]
        tot += sum(v * v for v in fr)
        for k in range(kmax + 1):
            re = im = 0.0
            for i, v in enumerate(fr):
                j = (k * i) % FRAME
                re += v * cos[j]
                im -= v * sin[j]
            p = (re * re + im * im) * (1 if k == 0 else 2) / FRAME
            f = k * SR / FRAME
            for b, (lo, hi) in enumerate(bands):
                if lo <= f <= hi:
                    got[b] += p
    return [g / tot for g in got]


def onset_ms(x):
    """The stem's energy onset: the first sample at or above -40 dB of its own peak, in ms."""
    pk = max(abs(v) for v in x)
    for i, v in enumerate(x):
        if abs(v) >= 0.01 * pk:
            return 1000.0 * i / SR
    return None


class GenSfxRd(unittest.TestCase):
    def test_voicings_and_names(self):
        self.assertEqual(gen_sfx_rd.VOICINGS, {'audible': 'rd_chime.pcm', 'recipe': 'rd_chime_r.pcm'})
        self.assertEqual(sorted(gen_sfx_rd.NAMES), ['rd_chime', 'rd_chime_r'])   # check_apps' allowed sounds

    def test_render_deterministic(self):
        for v in gen_sfx_rd.VOICINGS:
            a, b = gen_sfx_rd.render(v), gen_sfx_rd.render(v)
            self.assertEqual(len(a), CHIME_BYTES, v)
            self.assertEqual(a, b, v)
        self.assertNotEqual(gen_sfx_rd.render('audible'), gen_sfx_rd.render('recipe'))

    def test_audible_band(self):
        """PR9: the default voicing's thrum is audible on a speaker that rolls off below about 300 Hz."""
        x = gen_sfx_rd.render_stem('audible', 'thrum')
        inb, low = band_shares(x, 0.2, 1.5, [(200, 2000), (0, 109.999)])
        print(f'\n  audible thrum 0.2-1.5 s: {100 * inb:.1f} % in 200-2000 Hz, {100 * low:.1f} % below 110 Hz')
        self.assertGreaterEqual(inb, 0.60)
        self.assertLessEqual(low, 0.05)

    def test_recipe_low(self):
        x = gen_sfx_rd.render_stem('recipe', 'thrum')
        inb, low = band_shares(x, 0.2, 1.5, [(200, 2000), (0, 199.999)])
        print(f'\n  recipe thrum 0.2-1.5 s: {100 * low:.1f} % below 200 Hz, {100 * inb:.1f} % in 200-2000 Hz')
        self.assertGreater(low, 0.50)

    def test_peak(self):
        for v in gen_sfx_rd.VOICINGS:
            s = pcm(gen_sfx_rd.render(v))
            db = 20 * math.log10(max(abs(x) for x in s) / 32767)
            self.assertAlmostEqual(db, gen_sfx.PEAK_DBFS, delta=0.1, msg=v)
            n = len(s)
            for i in list(range(80)) + list(range(n - 80, n)):   # the 5 ms edges
                self.assertLessEqual(abs(s[i]), gen_sfx.TARGET * gen_sfx.ramp(i, n) + 1, f'{v}[{i}]')

    def test_events_timing(self):
        """PR34: on the stems alone (the audible thrum's resonance would mask the mix's onsets)."""
        for v in gen_sfx_rd.VOICINGS:
            for stem, at in (('bip', 750.0), ('bop', 950.0), ('click', 2200.0)):
                x = gen_sfx_rd.render_stem(v, stem)
                self.assertEqual(len(x), CHIME_BYTES // 2, (v, stem))
                # 2.5 ms (rd wave 1 Task 8 review): the beeps' 4 ms exponential attack from -80 dB crosses the -40 dB
                # onset threshold about 1.8 ms after the event starts, so a 2 ms tolerance left 0.2 ms of margin; 2.5 ms
                # keeps the check (a misplaced event is off by tens of ms) without riding that edge
                self.assertAlmostEqual(onset_ms(x), at, delta=2.5, msg=(v, stem))

    def test_stems_sum_to_the_mix(self):
        for v in gen_sfx_rd.VOICINGS:
            mix = [sum(t) for t in zip(*(gen_sfx_rd.render_stem(v, s) for s in gen_sfx_rd.STEMS))]
            s = gen_sfx.finish(mix)
            self.assertEqual(struct.pack(f'<{len(s)}h', *s), gen_sfx_rd.render(v), v)

    def test_tail_fades_out(self):
        """The thrum is silent from 2.35 s and the click's ring dies: the last 5 % sits at -40 dBFS rms or below, so
        the 5 ms edge never cuts a sound off."""
        for v in gen_sfx_rd.VOICINGS:
            s = pcm(gen_sfx_rd.render(v))
            tail = s[-len(s) // 20:]
            rms = math.sqrt(sum(x * x for x in tail) / len(tail))
            self.assertLessEqual(20 * math.log10(max(rms, 1e-9) / 32767), -40.0, v)

    def test_no_audio_inputs(self):
        tree = ast.parse(slurp(SCRIPT, 'r'))
        mods = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                mods.update(a.name.split('.')[0] for a in node.names)
            elif isinstance(node, ast.ImportFrom):
                mods.add((node.module or '').split('.')[0])
        self.assertFalse(mods & {'wave', 'soundfile', 'audioread', 'aifc', 'sunau', 'sndhdr', 'pydub', 'scipy',
                                 'numpy'}, mods)

    def test_cli_render_compare_manifest(self):
        """render writes exactly the voicings' files; compare never writes; manifest asserts and writes only its json;
        the script opens nothing else (no input file of any kind)."""
        import builtins
        real, seen = builtins.open, []

        def spy(file, mode='r', *a, **k):
            seen.append((os.path.abspath(os.fspath(file)), mode))
            return real(file, mode, *a, **k)

        with tempfile.TemporaryDirectory() as out, tempfile.TemporaryDirectory() as t:
            outs = {os.path.abspath(os.path.join(out, f)) for f in gen_sfx_rd.VOICINGS.values()}
            j = os.path.abspath(os.path.join(t, 'sfx.json'))
            for argv, reads, writes in ((['render', out], set(), outs),
                                        (['compare', out], outs, set()),
                                        (['manifest', out, j], outs, {j})):
                seen.clear()
                builtins.open, old = spy, sys.argv
                sys.argv = ['gen_sfx_rd.py'] + argv
                try:
                    with contextlib.redirect_stdout(io.StringIO()) as so:
                        gen_sfx_rd.main()
                finally:
                    builtins.open, sys.argv = real, old
                for path, mode in seen:
                    if any(c in mode for c in 'wax+'):
                        self.assertIn(path, writes, (argv[0], path, mode))
                    else:
                        self.assertIn(path, reads | writes, (argv[0], path, mode))
                self.assertEqual({p for p, m in seen if 'w' in m}, writes, argv[0])
                if argv[0] == 'compare':
                    self.assertNotIn('WARN', so.getvalue())
            self.assertEqual(sorted(os.listdir(out)), sorted(gen_sfx_rd.VOICINGS.values()))
            for f in os.listdir(out):
                self.assertEqual(os.path.getsize(os.path.join(out, f)), CHIME_BYTES, f)
            ents = json.loads(slurp(j, 'r'))
            self.assertEqual([e['file'] for e in ents], ['rd_chime.pcm', 'rd_chime_r.pcm'])
            for e in ents:
                self.assertEqual((e['cf'], e['samples'], e['rate'], e['channels'], e['format']),
                                 ('PCM', CHIME_BYTES // 2, SR, 1, 's16le'))
            # one voicing alone
            with tempfile.TemporaryDirectory() as one:
                r = run('render', one, '--voicing', 'recipe')
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(os.listdir(one), ['rd_chime_r.pcm'])
            # a bad voicing or mode is refused
            self.assertNotEqual(run('render', t, '--voicing', 'loud').returncode, 0)
            self.assertNotEqual(run('play').returncode, 0)
            # manifest asserts: a file that is not the right length fails
            with open(os.path.join(out, 'rd_chime_r.pcm'), 'wb') as f:
                f.write(b'\x00\x00' * 100)
            self.assertNotEqual(run('manifest', out, j).returncode, 0)

    def test_committed_match(self):
        """The committed files are this generator's output, sample by sample within 2 LSB (rd wave 1 Task 8 review: a
        host whose libm rounds one ulp differently may move a sample by one or two; a wrong voicing or a stale file is
        off by far more). The byte-for-byte check stays rd_assets.sh --check's (compare: 'matches' or WARN)."""
        for v, name in gen_sfx_rd.VOICINGS.items():
            want = pcm(gen_sfx_rd.render(v))
            got = pcm(slurp(os.path.join(ASSETS, name), 'rb'))
            self.assertEqual(len(got), len(want), name)
            worst = max(abs(a - b) for a, b in zip(got, want))
            self.assertLessEqual(worst, 2, f'{name}: a sample {worst} LSB off the generator\'s')

if __name__ == '__main__':
    unittest.main()
