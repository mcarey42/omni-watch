#!/usr/bin/env python3
"""Host tests for the safe-flash tools (Stage 4b Task 3): flash_keep_nvs.py, nvs_diff.py, theme.sh, bootline.py.

Run from the repo root with the ESP-IDF environment's python: `python -m unittest tools/test_flash_keep_nvs.py`
(ctest's test_flash_tools). Every esptool call goes to tools/fake_esptool.py over a sparse 32 MB file: no test
opens a serial port. The fixture by-id port is passed as a string only; the fake never opens it. setUpModule points
OMNI_WATCH_LOCAL at a temp watch file listing it, for every subprocess (tools/watchport.py; preflight M20).
"""
import json
import os
import shlex
import stat
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import nvs_fixture  # noqa: E402
import flash_keep_nvs  # noqa: E402

FIXTURE_PORT = '/dev/serial/by-id/usb-fixture_watch_serial-if00'
FIXTURE_PORT_2 = '/dev/serial/by-id/usb-fixture_second_serial-if00'
PORT = FIXTURE_PORT
FAKE = shlex.join([sys.executable, os.path.join(TOOLS, 'fake_esptool.py')])
TOOL = os.path.join(TOOLS, 'flash_keep_nvs.py')
NVS_DIFF = os.path.join(TOOLS, 'nvs_diff.py')
STAGE3 = os.path.join(REPO, 'firmware', 'test', 'host', 'nvs', 'stage3.csv')
SECRET = b'FAKE-SECRET-1234'
NEEDLES = [SECRET, SECRET.hex().encode(), SECRET.hex().upper().encode()]
FLASH_SIZE = 32 * 1024 * 1024
# A rollback build's four-row partition table (before firmware/common/partitions.csv): inlined so the test needs no
# old commit (final review I1: a clone of the public repo has one commit).
OLD_FOUR_ROW_CSV = """\
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, ,        4M,
assets,   data, spiffs,  ,        0xBF0000,
"""
BOOT_CMD = f'python -m esptool --chip esp32s3 -p {PORT} --before default_reset --after hard_reset run'
STATE = 'ROM download mode, NOT booted, screen dark'
NO_BOOT = 'Do NOT boot, reset or power-cycle the watch'
WATCH_ENV = 'OMNI_WATCH_LOCAL'
_SAVED = []


def setUpModule():
    """Every test and subprocess here sees a temp watch file listing FIXTURE_PORT, never the owner's."""
    if not os.path.exists(os.path.join(REPO, '.git')):   # final review m6: a download without .git
        raise unittest.SkipTest(f'no .git in {REPO}: flash_keep_nvs.py reads the frozen table from git HEAD; '
                                'the host tests are run from a git clone (README, Host tests)')
    d = tempfile.mkdtemp(prefix='flash-tools-watch-')
    p = os.path.join(d, 'watch.local')
    wr(p, FIXTURE_PORT + ' fixture\n')
    _SAVED.append((d, os.environ.get(WATCH_ENV)))
    os.environ[WATCH_ENV] = p


def tearDownModule():
    d, old = _SAVED.pop()
    subprocess.run(['rm', '-rf', d])
    if old is None:
        os.environ.pop(WATCH_ENV, None)
    else:
        os.environ[WATCH_ENV] = old


def two_watches(tmp):
    """A watch file listing both fixture ports, as `one` and `two`."""
    p = os.path.join(tmp, 'two-watches.local')
    wr(p, f'{FIXTURE_PORT} one\n{FIXTURE_PORT_2} two\n')
    return p


def rd(path, mode='rb'):
    with open(path, mode) as f:
        return f.read()


def wr(path, data):
    with open(path, 'wb' if isinstance(data, bytes) else 'w') as f:
        f.write(data)


_FROZEN = []


def frozen_table():
    """The frozen table's bytes (flash_keep_nvs.frozen_table: git HEAD's partitions.csv via gen_esp32part.py)."""
    if not _FROZEN:
        _FROZEN.append(flash_keep_nvs.frozen_table())
    return _FROZEN[0]


def gen_table(csv_text, tmp):
    """Any csv through gen_esp32part.py with the apps' options."""
    csv, out = os.path.join(tmp, 'other.csv'), os.path.join(tmp, 'other.bin')
    wr(csv, csv_text)
    gen = os.path.join(os.environ['IDF_PATH'], 'components', 'partition_table', 'gen_esp32part.py')
    subprocess.run([sys.executable, gen, *flash_keep_nvs.TABLE_GEN_ARGS, '--', csv, out], check=True)
    return rd(out)


def migrate_host():
    return os.environ.get('MIGRATE_HOST') or os.path.join(REPO, 'build-host', 'migrate_host')


class Rig:
    """A temp HOME, a fake chip (flash.bin with the stage3 NVS image at 0x9000) and an app build dir."""

    def __init__(self, tmp):
        self.tmp = tmp
        self.home = os.path.join(tmp, 'home')
        self.chip = os.path.join(tmp, 'chip')
        self.build = os.path.join(tmp, 'build')
        for d in (self.home, self.chip, self.build):
            os.makedirs(d)
        self.flash = os.path.join(self.chip, 'flash.bin')
        with open(self.flash, 'wb') as f:
            f.truncate(FLASH_SIZE)
        nvs_fixture.gen_image(STAGE3, os.path.join(tmp, 'nvs.bin'))
        self.nvs0 = rd(os.path.join(tmp, 'nvs.bin'))
        self.poke(0x9000, self.nvs0)
        self.files = {
            '0x0': ('bootloader/bootloader.bin', 0x5000, 0x11),
            '0x8000': ('partition_table/partition-table.bin', len(frozen_table()), frozen_table()),
            '0x10000': ('omni_watch.bin', 0x30000, 0x33),
            '0x410000': ('mmap_build/assets/assets/assets.bin', 0x21000, 0x44),
        }
        self.write_build()

    def write_build(self, files=None, write_flash_args=None):
        files = files or self.files
        for name, size, fill in files.values():
            p = os.path.join(self.build, name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(fill if isinstance(fill, bytes) else bytes([fill]) * size)
        fa = {
            'write_flash_args': write_flash_args or ['--flash_mode', 'dio', '--flash_size', '32MB',
                                                     '--flash_freq', '80m'],
            'flash_files': {off: v[0] for off, v in files.items()},
        }
        with open(os.path.join(self.build, 'flasher_args.json'), 'w') as f:
            json.dump(fa, f)

    def file_bytes(self, off):
        return rd(os.path.join(self.build, self.files[off][0]))

    def poke(self, off, data):
        with open(self.flash, 'r+b') as f:
            f.seek(off)
            f.write(data)

    def peek(self, off, n):
        with open(self.flash, 'rb') as f:
            f.seek(off)
            return f.read(n)

    def config(self, **kw):
        with open(os.path.join(self.chip, 'config.json'), 'w') as f:
            json.dump(kw, f)

    def env(self):
        return dict(os.environ, HOME=self.home, FAKE_ESPTOOL_DIR=self.chip, FLASH_KEEP_NVS_ESPTOOL=FAKE)

    def run(self, *args, port=PORT):
        cmd = [sys.executable, TOOL, '--port', port, *args, '--esptool', FAKE]
        return subprocess.run(cmd, env=self.env(), capture_output=True, text=True, cwd=self.tmp)

    def calls(self):
        p = os.path.join(self.chip, 'log.jsonl')
        if not os.path.exists(p):
            return []
        return [json.loads(line) for line in rd(p, 'r').splitlines()]

    def writes(self):
        return [c for c in self.calls() if c['cmd'] == 'write_flash']

    def nvs_with(self, extra_rows, flip=None):
        """Put stage3.csv's image plus extra CSV rows (and one byte flipped at `flip`) at 0x9000."""
        h = os.path.dirname(STAGE3)
        text = rd(STAGE3, 'r')
        for name in ('settings.hex', 'clockcal.hex', 'batrec.hex'):
            text = text.replace(name, os.path.join(h, name))
        c, img = os.path.join(self.tmp, 'nvs-x.csv'), os.path.join(self.tmp, 'nvs-x.bin')
        wr(c, text + extra_rows)
        nvs_fixture.gen_image(c, img)
        data = bytearray(rd(img))
        if flip is not None:
            data[flip] ^= 0xff
        self.nvs0 = bytes(data)
        self.poke(0x9000, self.nvs0)


def sub_index(argv):
    for i, a in enumerate(argv):
        if a in ('read_flash', 'verify_flash', 'write_flash', 'run'):
            return i
    raise AssertionError(f'no sub-command in {argv}')


def global_opts(argv):
    """The options before the sub-command, as {option: value}."""
    g = argv[:sub_index(argv)]
    return {g[i]: g[i + 1] for i in range(len(g) - 1) if g[i].startswith('-')}


class FlashKeepNvs(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.rig = Rig(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def assertRecovery(self, r, *want, boot_ok):
        """The failure's advice: the watch's state, the given phrases, and the boot command only when it is safe."""
        err = r.stderr
        self.assertIn(STATE, err)
        for w in want:
            self.assertIn(w, err)
        if boot_ok:
            self.assertIn(BOOT_CMD, err)
            self.assertNotIn(NO_BOOT, err)
        else:
            self.assertNotIn(BOOT_CMD, err)
            self.assertIn(NO_BOOT, err)
        for n in NEEDLES:
            self.assertNotIn(n.decode(), r.stdout + err)

    def assertRefused(self, r):
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertEqual(self.rig.calls(), [], 'an esptool call was made before the refusal')

    # ---- refusals (exit 2, no esptool call)

    def test_wrong_port_refused(self):
        self.assertRefused(self.rig.run('--build', self.rig.build, port='/dev/ttyACM0'))
        self.assertRefused(self.rig.run('--backup-only', port='/dev/ttyACM0'))

    def test_two_watches_need_watch_flag(self):
        env = dict(self.rig.env(), **{WATCH_ENV: two_watches(self.rig.tmp)})
        r = subprocess.run([sys.executable, TOOL, '--build', self.rig.build, '--dry-run', '--esptool', FAKE], env=env,
                           capture_output=True, text=True)
        self.assertRefused(r)
        self.assertIn('several watches listed: pass --watch <name>', r.stderr)
        r = subprocess.run([sys.executable, TOOL, '--port', FIXTURE_PORT, '--watch', 'two', '--build', self.rig.build,
                            '--dry-run', '--esptool', FAKE], env=env, capture_output=True, text=True)
        self.assertRefused(r)

    def test_watch_flag_selects(self):
        env = dict(self.rig.env(), **{WATCH_ENV: two_watches(self.rig.tmp)})
        r = subprocess.run([sys.executable, TOOL, '--watch', 'two', '--build', self.rig.build, '--dry-run',
                            '--esptool', FAKE], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn(f'port: {FIXTURE_PORT_2}', r.stdout.splitlines())
        self.assertEqual(self.rig.calls(), [])
        # and on a real (fake-chip) run every esptool call names it
        r = subprocess.run([sys.executable, TOOL, '--watch', 'two', '--backup-only', '--esptool', FAKE], env=env,
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(self.rig.calls())
        self.assertEqual({global_opts(c['argv']).get('-p') for c in self.rig.calls()}, {FIXTURE_PORT_2})

    def test_nvs_overlap_refused(self):
        cases = {
            'entry at 0x9000': {'0x9000': ('x.bin', 0x100, 0)},
            'end crosses 0x9000': {'0x0': ('bootloader/bootloader.bin', 0x9001, 0)},
            'table of 0x1001 B': {'0x8000': ('partition_table/partition-table.bin', 0x1001, 0)},
            'entry at 0xf000 (phy_init)': {'0xf000': ('phy.bin', 0x100, 0)},
            'entry at 0xfff0': {'0xfff0': ('x.bin', 0x10, 0)},
        }
        for name, change in cases.items():
            with self.subTest(name):
                files = dict(self.rig.files, **change)
                self.rig.write_build(files)
                r = self.rig.run('--build', self.rig.build)
                self.assertRefused(r)
                # the guard itself refuses, over [0x9000, 0x10000), before the known-offset check (final review m7)
                self.assertIn('reaches into NVS and phy_init [0x9000, 0x10000)', r.stderr)

    def test_guard_constant_covers_phy_init(self):
        self.assertEqual((flash_keep_nvs.NVS_OFF, flash_keep_nvs.GUARD_END), (0x9000, 0x10000))

    def test_table_of_0x1000_accepted_by_the_checks(self):
        self.rig.write_build(dict(self.rig.files, **{'0x8000': ('partition_table/partition-table.bin', 0x1000, 1)}))
        r = self.rig.run('--build', self.rig.build, '--dry-run', '--only', 'app,assets')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_table_write_only_of_the_frozen_table(self):
        """Final review m2: a plan that writes the table refuses (exit 2, no esptool call) unless the build's table is
        byte-equal to the frozen one, and names both; a rollback build's four-row table passes only with
        --only app,assets."""
        name = 'partition_table/partition-table.bin'
        four_rows = gen_table(OLD_FOUR_ROW_CSV, self.rig.tmp)
        self.assertNotEqual(four_rows, frozen_table())
        one_byte = bytearray(frozen_table())
        one_byte[0x40] ^= 1
        for label, data in (('the old four-row table', four_rows), ('one byte off', bytes(one_byte))):
            with self.subTest(label):
                log = os.path.join(self.rig.chip, 'log.jsonl')
                if os.path.exists(log):
                    os.unlink(log)
                self.rig.write_build(dict(self.rig.files, **{'0x8000': (name, len(data), data)}))
                for extra in ([], ['--only', 'table'], ['--only', 'app,table'], ['--dry-run']):
                    r = self.rig.run('--build', self.rig.build, *extra)
                    self.assertRefused(r)
                    self.assertIn('differs from the frozen table (git HEAD:firmware/common/partitions.csv', r.stderr)
                    self.assertIn(os.path.join(self.rig.build, name), r.stderr)
                r = self.rig.run('--build', self.rig.build, '--only', 'app,assets')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertRegex(r.stdout, r'regions: app (written|skipped) [0-9.]+, assets (written|skipped) [0-9.]+\n')
                self.assertFalse(any(c['argv'][-2] == '0x8000' for c in self.rig.writes()))
        # the frozen table passes and is named
        self.rig.write_build()
        r = self.rig.run('--build', self.rig.build, '--dry-run')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('equals the frozen table (git HEAD:firmware/common/partitions.csv)', r.stdout)

    def test_frozen_table_is_the_built_one(self):
        """The tool's frozen bytes are the apps' build output (when a build is present)."""
        found = 0
        for app in ('omni', 'dw', 'rd'):   # PR36
            p = os.path.join(REPO, 'firmware', 'apps', app, 'build', 'partition_table', 'partition-table.bin')
            if os.path.isfile(p):
                found += 1
                self.assertEqual(rd(p), frozen_table(), p)
        if not found:
            self.skipTest('no app build')

    def test_unknown_offset_refused(self):
        self.rig.write_build(dict(self.rig.files, **{'0x20000': ('extra.bin', 0x100, 0)}))
        self.assertRefused(self.rig.run('--build', self.rig.build))

    def test_size_limits_refused(self):
        for name, change in {
            'app over 4 MB': {'0x10000': ('omni_watch.bin', 0x400001, 0)},
            'assets past 16 MB': {'0x410000': ('mmap_build/assets/assets/assets.bin', 0xBF0001, 0)},
        }.items():
            with self.subTest(name):
                self.rig.write_build(dict(self.rig.files, **change))
                self.assertRefused(self.rig.run('--build', self.rig.build))

    def test_write_flash_args_beyond_mode_size_freq_refused(self):
        self.rig.write_build(write_flash_args=['--flash_mode', 'dio', '--erase' + '-all'])   # split for check_apps
        self.assertRefused(self.rig.run('--build', self.rig.build))

    def test_no_bootloader_option(self):
        r = self.rig.run('--build', self.rig.build, '--only', 'app,bootloader')
        self.assertRefused(r)
        r = self.rig.run('--build', self.rig.build, '--with-bootloader')
        self.assertRefused(r)

    # ---- the backup

    def test_unstable_backup_aborts(self):
        self.rig.config(unstable_read=True)
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'backup unstable', 'Nothing was written', boot_ok=True)

    def test_unparsable_backup_aborts(self):
        self.rig.poke(0x9000, bytes((i * 37 + 11) & 0xff for i in range(0x6000)))
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'backup unparsable', 'Nothing was written', boot_ok=True)

    # R129: the one allowed nvs_tool warning, an unused namespace named exactly `misc`

    def test_nvs_verdict_rules(self):
        import flash_keep_nvs as f
        clean = ['Page no. 0\tCRC32: OK', 'Page Empty']
        misc = 'Found unused namespace. Namespace index: 002 [misc]'
        self.assertEqual(f.nvs_tool_verdict(clean + [misc], 0), (0, 1))
        for other in ('Found unused namespace. Namespace index: 002 [phy]',
                      'Found unused namespace. Namespace index: 002 [misc2]',
                      'Found unused namespace. Namespace index: 002 [xmisc]',
                      'Found unused namespace. Namespace index: 002 [misc] extra',
                      'Entry #001 settings data (string, blob) has wrong CRC32! Written: 0 Generated: 1',
                      'Page no. 0 Original CRC32: 0 Generated CRC32: 1',
                      'Undefined namespace index! Namespace index: 002 [undefined]'):
            self.assertEqual(f.nvs_tool_verdict(clean + [misc, other], 0), (1, 1), other)
        self.assertEqual(f.nvs_tool_verdict(clean + [misc], 1), (1, 1))   # a failed run refuses whatever it said
        misc_003 = 'Found unused namespace. Namespace index: 003 [misc]'
        self.assertEqual(f.nvs_tool_verdict(clean + [misc, misc_003], 0), (1, 1))   # exactly one is allowed

    def assertNoSha(self):
        """After a refusal: no .sha256 at all (no empty or partial one), and the two reads kept."""
        nvsdir = os.path.join(self.rig.home, 'omni-watch-nvs')
        names = os.listdir(nvsdir)
        self.assertEqual([n for n in names if '.sha256' in n], [], names)
        self.assertEqual(len([n for n in names if n.endswith(('-a.bin', '-b.bin')) and not n.startswith('latest')]),
                         2, names)

    def test_unused_misc_namespace_accepted(self):
        self.rig.nvs_with('misc,namespace,,\n')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('unused namespace misc, allowed (R129)', r.stdout)
        self.assertEqual(len(self.rig.writes()), 3)   # app, assets, table (never the bootloader)

    def test_other_unused_namespace_refused(self):
        self.rig.nvs_with('phy,namespace,,\n')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'backup unparsable', 'Nothing was written', boot_ok=True)
        self.assertNoSha()

    def test_other_warning_beside_misc_refused(self):
        self.rig.nvs_with('misc,namespace,,\n', flip=8)   # page 0's header CRC broken, misc still unused
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertIn('1 problem line', r.stderr)
        self.assertNoSha()

    def test_sha_file_only_for_a_verified_backup(self):
        """Every refusal path after the reads leaves no .sha256; a later good backup writes one, 0600, whole."""
        self.rig.config(unstable_read=True)
        r = self.rig.run('--backup-only', '--label', 'sx')
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertNoSha()
        self.rig.config()
        self.rig.poke(0x9000, bytes((i * 37 + 11) & 0xff for i in range(0x6000)))
        r = self.rig.run('--backup-only', '--label', 'sy')
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        nvsdir = os.path.join(self.rig.home, 'omni-watch-nvs')
        self.assertEqual([n for n in os.listdir(nvsdir) if '.sha256' in n], [])
        self.rig.poke(0x9000, self.rig.nvs0)
        r = self.rig.run('--backup-only', '--label', 'sz')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        sha = [n for n in os.listdir(nvsdir) if '.sha256' in n]
        self.assertEqual(len(sha), 1, sha)
        self.assertTrue(sha[0].endswith('-sz.sha256'))
        p = os.path.join(nvsdir, sha[0])
        self.assertEqual(stat.S_IMODE(os.stat(p).st_mode), 0o600)
        import hashlib
        h = hashlib.sha256(self.rig.nvs0).hexdigest()
        lines = rd(p, 'r').splitlines()
        self.assertEqual(len(lines), 2)
        self.assertTrue(all(ln.startswith(h + '  ') for ln in lines))

    def test_missing_nvs_tool_refused(self):
        env = dict(self.rig.env(), IDF_PATH=os.path.join(self.rig.tmp, 'no-idf'))
        r = subprocess.run([sys.executable, TOOL, '--port', PORT, '--backup-only', '--esptool', FAKE], env=env,
                           capture_output=True, text=True)
        self.assertRefused(r)
        self.assertIn('nvs_tool.py not found', r.stderr)
        self.assertNotIn('unparsable', r.stderr)

    # ---- the flash

    def test_order_and_no_bootloader(self):
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual([c['addrs'] for c in self.rig.writes()], [[0x10000], [0x410000], [0x8000]])
        self.assertEqual(self.rig.peek(0, 16), b'\x00' * 16, 'the bootloader region was written')
        for off in ('0x10000', '0x410000', '0x8000'):
            data = self.rig.file_bytes(off)
            self.assertEqual(self.rig.peek(int(off, 16), len(data)), data)
        for c in self.rig.writes():   # write_flash_args passed through, after the sub-command
            argv = c['argv']
            i = sub_index(argv)
            self.assertEqual(argv[i + 1:i + 7], ['--flash_mode', 'dio', '--flash_size', '32MB', '--flash_freq', '80m'])

    def test_no_boot_until_compare(self):
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        calls = self.rig.calls()
        reads = [i for i, c in enumerate(calls) if c['cmd'] == 'read_flash']
        self.assertEqual(len(reads), 3, 'two backup reads and the read-back')
        readback = reads[2]
        for c in calls[:readback + 1]:
            g = global_opts(c['argv'])
            self.assertEqual(g.get('--after'), 'no_reset', c['argv'])
            self.assertEqual(g.get('--before'), 'default_reset', c['argv'])
            self.assertEqual((g.get('--chip'), g.get('-p'), g.get('-b')), ('esp32s3', PORT, '460800'), c['argv'])
            self.assertFalse(c['booted'], c['argv'])
        self.assertEqual(calls[readback]['boots_before'], 0, 'the fake booted before the read-back')
        booted = [i for i, c in enumerate(calls) if c['booted']]
        self.assertEqual(booted, [len(calls) - 1], 'exactly one boot, the last call')
        last = calls[-1]['argv']
        self.assertEqual(last[sub_index(last):], ['run'])
        self.assertEqual(global_opts(last).get('--after'), 'hard_reset')
        self.assertNotEqual(self.rig.peek(0x9000, 0x6000), self.rig.nvs0, 'the fake boot did not touch NVS')
        self.assertIn('NVS identical', r.stdout)

    def test_verify_skip(self):
        self.rig.poke(0x10000, self.rig.file_bytes('0x10000'))
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('skip app (verified)', r.stdout)
        self.assertEqual([c['addrs'] for c in self.rig.writes()], [[0x410000], [0x8000]])
        self.assertRegex(r.stdout, r'regions: app skipped [0-9.]+, assets written [0-9.]+, table written [0-9.]+')

    def test_only_selects_regions(self):
        r = self.rig.run('--build', self.rig.build, '--only', 'table,app')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual([c['addrs'] for c in self.rig.writes()], [[0x10000], [0x8000]])

    def test_nvs_changed_stops_before_boot(self):
        self.rig.config(corrupt_nvs_on_write=True)
        r = self.rig.run('--build', self.rig.build, '--label', 'cx')
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        nvsdir = os.path.join(self.rig.home, 'omni-watch-nvs')
        a = [f for f in os.listdir(nvsdir) if f.endswith('-cx-a.bin') and not f.startswith('latest')]
        self.assertEqual(len(a), 1)
        self.assertIn(os.path.join(nvsdir, a[0]), r.stdout + r.stderr)
        self.assertRecovery(r, 'regions written: app, assets, table', "needs the controller's explicit go",
                            boot_ok=False)

    def test_esptool_failure_exit5(self):
        self.rig.config(fail='write_flash')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertEqual(len(self.rig.writes()), 1)
        self.assertRecovery(r, 'write_flash app: app may be partly written; regions written before it: none',
                            'Run the same command again', boot_ok=False)

    def test_exit5_at_a_later_write_names_the_written(self):
        self.rig.poke(0x10000, self.rig.file_bytes('0x10000'))          # app verified and skipped
        self.rig.config(fail='write_flash')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertRecovery(r, 'write_flash assets', boot_ok=False)

    def test_exit5_at_the_readback(self):
        self.rig.config(fail_nvs_read=3)
        r = self.rig.run('--build', self.rig.build, '--label', 'rb')
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertEqual([c['addrs'] for c in self.rig.writes()], [[0x10000], [0x410000], [0x8000]])
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'NVS read-back', 'regions written: app, assets, table', 'NVS is unverified',
                            '-rb-a.bin', boot_ok=False)

    def test_exit5_at_the_backup_read(self):
        self.rig.config(fail_nvs_read=1)
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertRecovery(r, 'The failed step was the NVS backup', 'Nothing was written', boot_ok=True)

    def test_verify_failure_without_its_strings_exit5(self):
        self.rig.config(fail='verify_flash')     # exit 2 with only "A fatal error occurred: fake failure"
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [], 'a verify that did not report a mismatch led to a write')
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'verify_flash app; regions written before it: none', 'Nothing was written',
                            boot_ok=True)

    def test_boot_failure_exit5(self):
        self.rig.config(fail='run')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertRecovery(r, 'NVS read back identical', boot_ok=True)

    def test_boot_failure_after_backup_only(self):
        """Final review m8: nothing was written, and the advice says so."""
        self.rig.config(fail='run')
        r = self.rig.run('--backup-only', '--label', 'bf')
        self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        self.assertRecovery(r, 'Nothing was written (--backup-only: NVS was only read)', boot_ok=True)
        self.assertNotIn('Everything was written', r.stderr)

    def test_interrupt_during_a_write(self):
        self.rig.config(sigint_on='write_flash')
        r = self.rig.run('--build', self.rig.build)
        self.assertEqual(r.returncode, 130, r.stdout + r.stderr)
        self.assertFalse(any(c['booted'] for c in self.rig.calls()))
        self.assertRecovery(r, 'interrupted step was write_flash app', boot_ok=False)

    def test_esptool_override_is_printed(self):
        env = self.rig.env()                     # FLASH_KEEP_NVS_ESPTOOL=fake, no --esptool
        r = subprocess.run([sys.executable, TOOL, '--port', PORT, '--build', self.rig.build, '--dry-run'], env=env,
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn(f'esptool: {FAKE} (from FLASH_KEEP_NVS_ESPTOOL', r.stdout)
        env.pop('FLASH_KEEP_NVS_ESPTOOL')
        r = subprocess.run([sys.executable, TOOL, '--port', PORT, '--build', self.rig.build, '--dry-run'], env=env,
                           capture_output=True, text=True)
        self.assertNotIn('esptool:', r.stdout)

    def test_backup_only_boots(self):
        r = self.rig.run('--backup-only', '--label', 'bk')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.rig.writes(), [])
        calls = self.rig.calls()
        self.assertEqual([i for i, c in enumerate(calls) if c['booted']], [len(calls) - 1])
        nvsdir = os.path.join(self.rig.home, 'omni-watch-nvs')
        self.assertEqual(stat.S_IMODE(os.stat(nvsdir).st_mode), 0o700)
        paths = {}
        for s in 'ab':
            found = [f for f in os.listdir(nvsdir) if f.endswith(f'-bk-{s}.bin') and not f.startswith('latest')]
            self.assertEqual(len(found), 1)
            paths[s] = os.path.join(nvsdir, found[0])
            self.assertIn(paths[s], r.stdout)
            self.assertEqual(stat.S_IMODE(os.stat(paths[s]).st_mode), 0o600)
            self.assertEqual(rd(paths[s]), self.rig.nvs0)
            link = os.path.join(nvsdir, f'latest-bk-{s}.bin')
            self.assertTrue(os.path.islink(link))
            self.assertEqual(os.path.realpath(link), os.path.realpath(paths[s]))
        for f in os.listdir(nvsdir):   # every file 0600, the SHA file included (PD22), and none printed
            p = os.path.join(nvsdir, f)
            if not os.path.islink(p):
                self.assertEqual(stat.S_IMODE(os.stat(p).st_mode), 0o600, f)
        sha = [f for f in os.listdir(nvsdir) if f.endswith('.sha256')]
        self.assertEqual(len(sha), 1)
        import hashlib
        self.assertNotIn(hashlib.sha256(self.rig.nvs0).hexdigest(), r.stdout + r.stderr)
        # a second backup moves the symlink to the new pair
        r2 = self.rig.run('--backup-only', '--label', 'bk')
        self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
        self.assertNotEqual(os.path.realpath(os.path.join(nvsdir, 'latest-bk-a.bin')), os.path.realpath(paths['a']))

    def test_dry_run_calls_nothing(self):
        r = self.rig.run('--build', self.rig.build, '--dry-run')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.rig.calls(), [])
        self.assertFalse(os.path.exists(os.path.join(self.rig.home, 'omni-watch-nvs')))
        plan = [line.split()[1] for line in r.stdout.splitlines() if line.startswith('plan:')]
        self.assertEqual(plan, ['app', 'assets', 'table'])

    def test_real_esptool_accepts_the_option_placement(self):
        """The argv the tool builds parses under the real esptool v4.12 (sub-command help: no connection, and a
        port path that does not exist)."""
        sys.path.insert(0, TOOLS)
        import flash_keep_nvs
        for after, cmd in (('no_reset', 'read_flash'), ('no_reset', 'verify_flash'), ('no_reset', 'write_flash'),
                           ('hard_reset', 'run')):
            argv = flash_keep_nvs.esptool_argv([sys.executable, '-m', 'esptool'], '/nonexistent/port', after, [cmd])
            for opt in ('--chip', '-p', '-b', '--before', '--after'):   # -h would mask a misplaced option
                self.assertLess(argv.index(opt), argv.index(cmd), argv)
            self.assertEqual(argv[-1], cmd)
            r = subprocess.run(argv + ['-h'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            # control: every required argument given, only --after misplaced (after the sub-command and its
            # arguments): an argparse error before any connection
            img = os.path.join(self.rig.tmp, 'img.bin')
            wr(img, b'\xff' * 16)
            full = {'read_flash': ['0x9000', '0x6000', os.path.join(self.rig.tmp, 'out.bin')],
                    'verify_flash': ['0x10000', img],
                    'write_flash': ['--flash_mode', 'dio', '--flash_size', '32MB', '--flash_freq', '80m', '0x10000',
                                    img],
                    'run': []}[cmd]
            bad = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '-p', '/nonexistent/port', '-b', '460800',
                   '--before', 'default_reset', cmd, *full, '--after', after]
            r = subprocess.run(bad, capture_output=True, text=True)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn(f'unrecognized arguments: --after {after}', r.stderr)
            self.assertNotIn('required', r.stderr)

    def test_fake_models_boot(self):
        """Control: the fake boots (and writes NVS) on esptool's default --after."""
        env = self.rig.env()
        out = os.path.join(self.rig.tmp, 'r.bin')
        subprocess.run(shlex.split(FAKE) + ['-p', PORT, 'read_flash', '0x9000', '0x6000', out], env=env, check=True,
                       capture_output=True)
        self.assertTrue(self.rig.calls()[0]['booted'])
        self.assertNotEqual(self.rig.peek(0x9000, 0x6000), self.rig.nvs0)


class NvsDiff(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def diff(self, *args):
        r = subprocess.run([sys.executable, NVS_DIFF, *args], capture_output=True, text=True)
        out = (r.stdout + r.stderr).encode()
        for n in NEEDLES:
            self.assertNotIn(n, out, f'nvs_diff printed the secret: {args}')
        return r

    def test_nvs_diff_rules(self):
        if not os.path.exists(migrate_host()):
            self.fail(f'{migrate_host()} not built')
        out = os.path.join(self.tmp, 'mig')
        nvs_fixture.run(STAGE3, 'omni', out, migrate_host(), quiet=True)
        before, after = os.path.join(out, 'in.bin'), os.path.join(out, 'out.bin')

        r = self.diff(before, after, '--allow-added', 'watch,t_omni')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('watch:shared', r.stdout)
        self.assertEqual(self.diff(before, after).returncode, 1, 'an addition passed without --allow-added')
        self.assertEqual(self.diff(before, after, '--allow-added', 'watch,t_omni', '--require-same',
                                   'omni,nvs.net80211').returncode, 0)

        # omni/settings changed: --require-same omni fails and names the key
        d = os.path.join(self.tmp, 'chg')
        os.makedirs(d)
        nvsdir = os.path.dirname(STAGE3)
        for f in ('clockcal.hex', 'batrec.hex'):
            os.symlink(os.path.join(nvsdir, f), os.path.join(d, f))
        s = bytearray(nvs_fixture._read_hex(os.path.join(nvsdir, 'settings.hex')))
        s[38] = 46
        wr(os.path.join(d, 'settings.hex'), s.hex())
        csv_path = os.path.join(d, 'stage3.csv')
        wr(csv_path, rd(STAGE3, 'r'))
        changed = os.path.join(d, 'changed.bin')
        nvs_fixture.gen_image(csv_path, changed)
        r = self.diff(before, changed, '--require-same', 'omni')
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('omni:settings', r.stdout)
        r = self.diff(before, changed, '--allow-changed', 'omni/settings')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = self.diff(before, changed, '--allow-changed', 'omni/settings', '--require-same', 'omni')
        self.assertEqual(r.returncode, 1, '--require-same must win over --allow-changed')

        # nvs.net80211 compared by value, its key named, its value never printed
        wr(csv_path, rd(STAGE3, 'r').replace('46414b452d5345435245542d31323334',
                                                              '46414b452d5345435245542d31323335'))
        wifi = os.path.join(d, 'wifi.bin')
        nvs_fixture.gen_image(csv_path, wifi)
        r = self.diff(before, wifi, '--require-same', 'nvs.net80211')
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('nvs.net80211:sta.pswd', r.stdout)
        self.assertNotIn('FAKE-SECRET-1235', r.stdout + r.stderr)

        # identical images
        self.assertEqual(self.diff(before, before, '--require-same', 'omni,nvs.net80211').returncode, 0)

        # --list: keys with type and size, no values
        r = self.diff('--list', after)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(r.stdout, r'sta\.pswd\s+blob\s+16\b')
        self.assertRegex(r.stdout, r'settings\s+blob\s+62\b')
        self.assertIn('watch', r.stdout)
        self.assertNotIn('436f6c756d627573', r.stdout, '--list printed a value')

    def test_unparsable_image_exit2(self):
        bad = os.path.join(self.tmp, 'bad.bin')
        wr(bad, bytes((i * 37 + 11) & 0xff for i in range(0x6000)))
        self.assertEqual(self.diff('--list', bad).returncode, 2)


class ThemeSh(unittest.TestCase):
    def test_theme_dry_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            rig = Rig(tmp)
            bindir = os.path.join(tmp, 'bin')
            os.makedirs(bindir)
            marker = os.path.join(tmp, 'idf-ran')
            with open(os.path.join(bindir, 'idf.py'), 'w') as f:
                f.write(f'#!/bin/sh\necho "$@" > {shlex.quote(marker)}\nexit 1\n')
            os.chmod(os.path.join(bindir, 'idf.py'), 0o755)
            env = dict(rig.env(), PATH=bindir + os.pathsep + os.environ['PATH'])
            r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), 'omni', '--dry-run', '--no-build',
                                '--build-dir', rig.build], env=env, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            plan = [line.split()[1] for line in r.stdout.splitlines() if line.startswith('plan:')]
            self.assertEqual(plan, ['app', 'assets', 'table'])
            self.assertFalse(os.path.exists(marker), 'idf.py ran')
            self.assertEqual(rig.calls(), [], 'an esptool call was made')
            self.assertEqual(rig.peek(0x9000, 0x6000), rig.nvs0)

            # the build runs unless --no-build, and a failed build never flashes
            r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), 'omni', '--dry-run', '--build-dir',
                                rig.build], env=env, capture_output=True, text=True, cwd=tmp)
            self.assertNotEqual(r.returncode, 0)
            self.assertTrue(os.path.exists(marker))
            self.assertNotIn('plan:', r.stdout)
            # the directory built is the directory flashed (--build-dir D -> idf.py -B D)
            self.assertIn(f'-B {os.path.realpath(rig.build)} build', rd(marker, 'r'))

    def test_theme_build_paths(self):
        """rd wave 1 Task 5: each theme builds its own app into its own default build dir (omni and dw exactly as
        before), and a failed build flashes nothing."""
        with tempfile.TemporaryDirectory() as tmp:
            rig = Rig(tmp)
            bindir = os.path.join(tmp, 'bin')
            os.makedirs(bindir)
            marker = os.path.join(tmp, 'idf-ran')
            with open(os.path.join(bindir, 'idf.py'), 'w') as f:
                f.write(f'#!/bin/sh\necho "$@" > {shlex.quote(marker)}\nexit 1\n')
            os.chmod(os.path.join(bindir, 'idf.py'), 0o755)
            env = dict(rig.env(), PATH=bindir + os.pathsep + os.environ['PATH'])
            for theme in ('omni', 'dw', 'rd'):
                with self.subTest(theme):
                    r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), theme, '--dry-run'], env=env,
                                       capture_output=True, text=True, cwd=tmp)
                    self.assertNotEqual(r.returncode, 0)
                    app = os.path.join(REPO, 'firmware', 'apps', theme)
                    self.assertEqual(rd(marker, 'r').split(), ['-C', app, '-B', os.path.join(app, 'build'), 'build'])
                    self.assertNotIn('plan:', r.stdout)
            self.assertEqual(rig.calls(), [], 'an esptool call was made')

    def test_theme_rd_dry_run(self):
        """theme.sh rd --dry-run --no-build: the regions planned, "dry run", nothing flashed, NVS as it was."""
        with tempfile.TemporaryDirectory() as tmp:
            rig = Rig(tmp)
            r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), 'rd', '--dry-run', '--no-build',
                                '--build-dir', rig.build], env=rig.env(), capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            plan = [line.split()[1] for line in r.stdout.splitlines() if line.startswith('plan:')]
            self.assertEqual(plan, ['app', 'assets', 'table'])
            self.assertIn('swap to rd: dry run (nothing flashed)', r.stdout)
            self.assertEqual(rig.calls(), [], 'an esptool call was made')
            self.assertEqual(rig.peek(0x9000, 0x6000), rig.nvs0)

    def swap(self, log_lines, theme='omni', table=False):
        """A real (non-dry) theme.sh swap: the fake chip, the boot lines replayed by bootline (BOOTLINE_REPLAY). table:
        the frozen table already on the chip at 0x8000 (a real watch's case)."""
        tmp = tempfile.mkdtemp()
        self.addCleanup(lambda: subprocess.run(['rm', '-rf', tmp]))
        rig = Rig(tmp)
        if table:
            rig.poke(0x8000, frozen_table())
        log = os.path.join(tmp, 'boot.log')
        wr(log, '\n'.join(log_lines) + '\n')
        env = dict(rig.env(), BOOTLINE_REPLAY=log)
        r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), theme, '--no-build', '--build-dir', rig.build],
                           env=env, capture_output=True, text=True, cwd=tmp, timeout=120)
        return rig, r

    def test_theme_swap_rd(self):
        """rd wave 1 Task 5: the swap to rd keeps NVS at 0x9000 byte for byte up to the boot and writes neither the
        table (frozen, already on the chip) nor pack (16 MB up): only the app and the assets."""
        rig, r = self.swap(['I (900) store: migration: not needed', 'I (901) store: settings: watch v1',
                            'I (902) store: theme rd (was dw)'], theme='rd', table=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(r.stdout, r'swap to rd: [0-9.]+ s \(app written')
        self.assertIn('NVS identical', r.stdout)   # compared before the one boot (the fake app then adds a key)
        self.assertEqual([c['addrs'] for c in rig.writes()], [[0x10000], [0x410000]])
        self.assertEqual(rig.peek(0x8000, len(frozen_table())), frozen_table())

    def test_theme_swap_rd_wrong_theme_line(self):
        """The boot must name rd: a line for another theme fails the swap (bootline times out on the replay)."""
        rig, r = self.swap(['I (900) store: migration: not needed', 'I (901) store: settings: watch v1',
                            'I (902) store: theme dw (was rd)'], theme='rd', table=True)
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('theme.sh: swap to rd FAILED', r.stderr)
        self.assertIn('NVS identical', r.stdout)

    GOOD = ['I (900) store: migration: not needed', 'I (901) store: settings: watch v1',
            'I (902) store: theme omni (was dw)']

    def test_theme_swap_ok(self):
        rig, r = self.swap(self.GOOD)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(r.stdout, r'swap to omni: [0-9.]+ s \(app written')
        self.assertIn('NVS identical', r.stdout)

    def test_theme_swap_fails_on_a_bad_boot(self):
        """Final review m5: a failed migration or a read-only settings origin fails the swap, with a clear message,
        though the theme line is there."""
        cases = {
            'failed migration': ['I (900) store: migration: failed: local (shared skipped) ESP_ERR_NVS_NOT_ENOUGH_SPACE',
                                 'I (901) store: settings: read-only: migration failed',
                                 'I (902) store: theme omni (was dw)',
                                 'W (903) store: read-only boot: nothing is written until a reboot'],
            'failed shared only': ['I (900) store: migration: failed: shared', 'I (901) store: settings: watch v1',
                                   'I (902) store: theme omni (was dw)'],
            'read-only origin': ['I (900) store: migration: not needed',
                                 'I (901) store: settings: read-only: watch/shared v2',
                                 'I (902) store: theme omni (was dw)'],
            'only in the state reply': ['I (902) store: theme omni (was dw)', 'theme omni (was dw)',
                                        'settings read-only: t_omni/local v2', 'migration not needed'],
        }
        for name, lines in cases.items():
            with self.subTest(name):
                rig, r = self.swap(lines)
                self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
                self.assertIn('theme.sh: swap to omni FAILED', r.stderr)
                self.assertIn('booted read-only or its migration failed', r.stderr)
                self.assertNotIn('swap to omni:', r.stdout)
                self.assertIn('NVS identical', r.stdout)   # the flash itself was clean

    def test_theme_watch_flag(self):
        """Two watches listed: theme.sh refuses without --watch/--port, and --watch picks the port it passes on."""
        with tempfile.TemporaryDirectory() as tmp:
            rig = Rig(tmp)
            env = dict(rig.env(), **{WATCH_ENV: two_watches(tmp)})
            sh = ['bash', os.path.join(TOOLS, 'theme.sh'), 'omni', '--dry-run', '--no-build', '--build-dir', rig.build]
            r = subprocess.run(sh, env=env, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn('several watches listed: pass --watch <name>', r.stderr)
            r = subprocess.run(sh + ['--watch', 'two'], env=env, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn(f'port: {FIXTURE_PORT_2}', r.stdout.splitlines())
            r = subprocess.run(sh + ['--watch', 'two', '--port', FIXTURE_PORT], env=env, capture_output=True,
                               text=True, cwd=tmp)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertEqual(rig.calls(), [], 'an esptool call was made')

    def test_theme_port_is_stdout_only(self):
        """Review M3: theme.sh takes only watchport's stdout as the port; stderr output on success (a Python warning)
        never joins it, and on a refusal it is reported as the reason."""
        with tempfile.TemporaryDirectory() as tmp:
            rig = Rig(tmp)
            site = os.path.join(tmp, 'site')
            os.makedirs(site)
            wr(os.path.join(site, 'sitecustomize.py'), 'import sys\nsys.stderr.write("zzstderrnoise\\n")\n')
            env = dict(rig.env(), PYTHONPATH=site)
            sh = ['bash', os.path.join(TOOLS, 'theme.sh'), 'omni', '--dry-run', '--no-build', '--build-dir', rig.build]
            r = subprocess.run(sh, env=env, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn(f'port: {FIXTURE_PORT}', r.stdout.splitlines())
            env[WATCH_ENV] = two_watches(tmp)
            r = subprocess.run(sh, env=env, capture_output=True, text=True, cwd=tmp)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn('theme.sh: refused: ', r.stderr)
            self.assertIn('several watches listed: pass --watch <name>', r.stderr)
            self.assertEqual(rig.calls(), [], 'an esptool call was made')

    def test_theme_refuses(self):
        for args in (['omni', '--port', '/dev/ttyACM0', '--dry-run', '--no-build'], ['pink', '--dry-run'], [],
                     ['nope'], ['rd', 'dw', '--dry-run', '--no-build'], ['RD', '--dry-run'],
                     ['rd', '--port', '/dev/ttyACM1', '--dry-run', '--no-build']):
            with self.subTest(args):
                r = subprocess.run(['bash', os.path.join(TOOLS, 'theme.sh'), *args], capture_output=True, text=True,
                                   env=dict(os.environ, FLASH_KEEP_NVS_ESPTOOL='false'))
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)


class FakeSerial:
    """Scripted lines at given times on a fake clock; records writes."""

    def __init__(self, clock, timed_lines, state_reply=(), drop_at=None):
        self.clock, self.lines, self.state_reply, self.written = clock, list(timed_lines), list(state_reply), []
        self.drop_at, self.closed = drop_at, False

    def close(self):
        self.closed = True

    def readline(self):
        if self.drop_at is not None and self.clock.t >= self.drop_at:
            raise OSError(5, 'device reports readiness to read but returned no data')   # SerialException
        if self.lines and self.lines[0][0] <= self.clock.t:
            return self.lines.pop(0)[1].encode() + b'\r\n'
        self.clock.t += 0.5     # a read timeout
        return b''

    def write(self, b):
        self.written.append(b)
        if b'state' in b:
            self.lines += [(self.clock.t, s) for s in self.state_reply]


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


class Bootline(unittest.TestCase):
    def setUp(self):
        import bootline
        self.bootline = bootline

    def test_line_in_the_log(self):
        c = Clock()
        s = FakeSerial(c, [(0.5, 'I (123) boot: hello'), (1.0, 'I (200) watch: theme omni (was dw)')])
        self.assertEqual(self.bootline.watch(s, 'theme omni (was ', 30, now=c), 'I (200) watch: theme omni (was dw)')
        self.assertEqual(s.written, [])

    def test_state_after_5s(self):
        c = Clock()
        s = FakeSerial(c, [(0.5, 'noise')], state_reply=['theme: omni', 'theme omni (was omni) origin MIGRATED'])
        got = self.bootline.watch(s, 'theme omni (was ', 30, now=c)
        self.assertEqual(got, 'theme omni (was omni) origin MIGRATED')
        self.assertEqual(len([w for w in s.written if b'state' in w]), 1)
        self.assertGreaterEqual(c.t, 5.0)

    def test_timeout(self):
        c = Clock()
        s = FakeSerial(c, [])
        self.assertIsNone(self.bootline.watch(s, 'theme dw (was ', 30, now=c))
        self.assertGreaterEqual(c.t, 30.0)

    def test_port_drop_reopens(self):
        c = Clock()
        old = FakeSerial(c, [(0.5, 'I (10) boot: old')], drop_at=1.0)
        new = FakeSerial(c, [(0.0, 'I (300) watch: theme dw (was omni)')])
        opened = []

        def reopen():
            opened.append(1)
            return new
        self.assertEqual(self.bootline.watch(old, 'theme dw (was ', 30, now=c, reopen=reopen),
                         'I (300) watch: theme dw (was omni)')
        self.assertEqual(opened, [1])
        self.assertTrue(old.closed)

    def test_port_drop_not_back(self):
        c = Clock()
        old = FakeSerial(c, [], drop_at=1.0)
        with self.assertRaisesRegex(self.bootline.PortLost, 'did not come back'):
            self.bootline.watch(old, 'theme dw (was ', 30, now=c, reopen=lambda: None)

    def test_verdict_waits_for_the_verdict_lines(self):
        c = Clock()
        s = FakeSerial(c, [(0.5, 'I (902) store: theme omni (was omni)')],
                       state_reply=['theme omni (was omni)', 'settings watch v1', 'migration not needed'])
        bad = []
        self.assertEqual(self.bootline.watch(s, 'theme omni (was ', 30, now=c, verdict=bad),
                         'I (902) store: theme omni (was omni)')
        self.assertEqual(bad, [])
        self.assertGreaterEqual(c.t, 5.0)   # the boot log's verdict lines were missed: `state` gave them

    def test_verdict_collects_bad_lines(self):
        c = Clock()
        s = FakeSerial(c, [(0.1, 'I (900) store: migration: forced: failed: shared'),
                           (0.2, 'I (901) store: settings: read-only: migration failed'),
                           (0.3, 'I (902) store: theme omni (was omni)')])
        bad = []
        self.assertIsNotNone(self.bootline.watch(s, 'theme omni (was ', 30, now=c, verdict=bad))
        self.assertEqual(len(bad), 2)

    def test_verdict_timeout_without_its_lines(self):
        c = Clock()
        s = FakeSerial(c, [(0.1, 'I (902) store: theme omni (was omni)')])
        self.assertIsNone(self.bootline.watch(s, 'theme omni (was ', 30, now=c, verdict=[]))

    def test_bad_line_rules(self):
        b = self.bootline.bad_line
        for ok in ('I (1) store: migration: not needed', 'migration local done, shared done', 'settings watch v1',
                   'settings: brightness 45, sleep 15 s, 12 h, sound on vol 30, reveal 0, reduced motion off '
                   '(watch v1, city unknown: kept, default shown)', 'I (2) store: migrate force: shared done'):
            self.assertFalse(b(ok), ok)
        for bad in ('I (1) store: migration: failed: shared', 'migration forced: failed: local, shared',
                    'settings read-only: watch/shared v2', 'I (3) store: read-only boot: nothing is written',
                    'W (4) store: saved; kept in RAM only: read-only: t_omni/local v2'):
            self.assertTrue(b(bad), bad)

    def test_wrong_port_refused(self):
        r = subprocess.run([sys.executable, os.path.join(TOOLS, 'bootline.py'), '--port', '/dev/ttyACM0',
                            '--expect', 'x'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_two_watches_need_watch_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, **{WATCH_ENV: two_watches(tmp)})
            r = subprocess.run([sys.executable, os.path.join(TOOLS, 'bootline.py'), '--expect', 'x'], env=env,
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn('several watches listed: pass --watch <name>', r.stderr)


if __name__ == '__main__':
    unittest.main()
