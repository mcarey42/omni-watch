#!/usr/bin/env python3
"""A fake esptool over a sparse 32 MB flash file, for tools/test_flash_keep_nvs.py (Stage 4b Task 3).

  FAKE_ESPTOOL_DIR=D fake_esptool.py [esptool v4.12 global options] <read_flash|verify_flash|write_flash|run> ...

D holds flash.bin (the chip's flash), log.jsonl (one line per call), and optionally config.json (faults):
  {"unstable_read": true}        the second read_flash returns one byte flipped
  {"corrupt_nvs_on_write": true} every write_flash also changes a byte of NVS (0x9000)
  {"fail": "<command>"}          that command fails with esptool's exit 2 ("A fatal error occurred")
  {"fail_nvs_read": N}           the Nth read_flash of 0x9000 fails the same way (3: the read-back)
  {"sigint_on": "<command>"}     that command sends SIGINT to its caller (a Ctrl-C) and waits to be killed

It models the chip, not only the command line (preflight A7, C5): any call whose `--after` is not `no_reset`
(esptool's default is hard_reset), and any `run`, boots the app once the command has completed, and the booted app
writes a new key into its NVS, as a migrating app would. A failed command does not boot (esptool skips the
post-operation reset on an exception). The global options are parsed as esptool parses them: before the
sub-command; an `--after` after the sub-command is an argparse error (exit 2).
"""
import argparse
import hashlib
import json
import os
import signal
import sys
import tempfile
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nvs_fixture  # noqa: E402

FLASH_SIZE = 32 * 1024 * 1024
NVS_OFF, NVS_SIZE = 0x9000, 0x6000
SECTOR = 0x1000


def _parser():
    ap = argparse.ArgumentParser(prog='fake_esptool')
    ap.add_argument('--chip', '-c', default='auto')
    ap.add_argument('--port', '-p')
    ap.add_argument('--baud', '-b', type=int, default=115200)
    ap.add_argument('--before', choices=['default_reset', 'usb_reset', 'no_reset', 'no_reset_no_sync'],
                    default='default_reset')
    ap.add_argument('--after', '-a', choices=['hard_reset', 'soft_reset', 'no_reset', 'no_reset_stub',
                                              'watchdog_reset'], default='hard_reset')
    ap.add_argument('--no-stub', action='store_true')
    sub = ap.add_subparsers(dest='cmd', required=True)

    def spi(p, size_only=False):
        p.add_argument('--flash_size', '-fs')
        if not size_only:
            p.add_argument('--flash_mode', '-fm')
            p.add_argument('--flash_freq', '-ff')

    r = sub.add_parser('read_flash')
    spi(r, size_only=True)
    r.add_argument('--no-progress', '-p', action='store_true')
    r.add_argument('address', type=lambda x: int(x, 0))
    r.add_argument('size', type=lambda x: int(x, 0))
    r.add_argument('filename')
    v = sub.add_parser('verify_flash')
    spi(v)
    v.add_argument('--diff', '-d', choices=['no', 'yes'], default='no')
    v.add_argument('addr_filename', nargs='+')
    w = sub.add_parser('write_flash')
    spi(w)
    w.add_argument('--no-progress', '-p', action='store_true')
    w.add_argument('addr_filename', nargs='+')
    sub.add_parser('run')
    return ap


def _pairs(items):
    if len(items) % 2:
        sys.exit('fake_esptool: address/filename pairs expected')
    return [(int(items[i], 0), items[i + 1]) for i in range(0, len(items), 2)]


def _load(path, default):
    if not os.path.exists(path):
        return default
    with open(path) as f:
        return json.load(f)


def _bytes(path):
    with open(path, 'rb') as f:
        return f.read()


class Chip:
    def __init__(self, d):
        self.d = d
        self.flash_path = os.path.join(d, 'flash.bin')
        self.state_path = os.path.join(d, 'state.json')
        cfg = os.path.join(d, 'config.json')
        self.cfg = _load(cfg, {})
        self.state = _load(self.state_path, {'boots': 0, 'reads': 0})

    def read(self, off, n):
        with open(self.flash_path, 'rb') as f:
            f.seek(off)
            b = f.read(n)
        return b + b'\x00' * (n - len(b))

    def write(self, off, data):
        with open(self.flash_path, 'r+b') as f:
            f.seek(off)
            f.write(data)

    def nvs_sha(self):
        return hashlib.sha256(self.read(NVS_OFF, NVS_SIZE)).hexdigest()

    def boot(self):
        """The app runs and writes a key into NVS (a migration). Garbage NVS: a marker in its last bytes."""
        self.state['boots'] += 1
        n = self.state['boots']
        nvs = self.read(NVS_OFF, NVS_SIZE)
        try:
            entries, _ = nvs_fixture.parse_image(nvs)
            entries.append(('fakeapp', f'boot{n}', 'u8', b'\x01'))
            with tempfile.TemporaryDirectory() as t:
                nvs_fixture.write_csv(entries, os.path.join(t, 'b.csv'))
                nvs_fixture.gen_image(os.path.join(t, 'b.csv'), os.path.join(t, 'b.bin'))
                new = _bytes(os.path.join(t, 'b.bin'))
        except nvs_fixture.FixtureError:
            new = nvs[:-16] + b'BOOTED%010d' % n
        self.write(NVS_OFF, new)

    def save(self):
        with open(self.state_path, 'w') as f:
            json.dump(self.state, f)


def main(argv):
    d = os.environ.get('FAKE_ESPTOOL_DIR')
    if not d:
        sys.exit('fake_esptool: FAKE_ESPTOOL_DIR not set')
    chip = Chip(d)
    entry = {'argv': argv, 'boots_before': chip.state['boots'], 'nvs_sha_before': chip.nvs_sha()}
    def log(**kw):
        entry.update(kw)
        with open(os.path.join(d, 'log.jsonl'), 'a') as f:
            f.write(json.dumps(entry) + '\n')

    try:
        a = _parser().parse_args(argv)
    except SystemExit as e:
        log(cmd=None, rc=e.code, booted=False)
        raise
    entry.update(cmd=a.cmd, after=a.after, before=a.before)
    rc = 0
    nvs_read = a.cmd == 'read_flash' and a.address == NVS_OFF
    if nvs_read:
        chip.state['reads'] += 1
    if chip.cfg.get('sigint_on') == a.cmd:
        log(rc=None, booted=False, sigint=True)
        chip.save()
        os.kill(os.getppid(), signal.SIGINT)
        time.sleep(30)
        return 2
    if chip.cfg.get('fail') == a.cmd or (nvs_read and chip.cfg.get('fail_nvs_read') == chip.state['reads']):
        print('\nA fatal error occurred: fake failure')
        rc = 2
    elif a.cmd == 'read_flash':
        data = chip.read(a.address, a.size)
        if nvs_read and chip.cfg.get('unstable_read') and chip.state['reads'] == 2:
            data = bytes([data[0] ^ 0x01]) + data[1:]
        with open(a.filename, 'wb') as f:
            f.write(data)
        entry['addrs'] = [a.address]
        print(f'Read {a.size} bytes at {a.address:#x}')
    elif a.cmd == 'verify_flash':
        pairs = _pairs(a.addr_filename)
        entry['addrs'] = [p[0] for p in pairs]
        bad = False
        for off, fn in pairs:
            img = _bytes(fn)
            print(f'Verifying {len(img):#x} ({len(img)}) bytes @ {off:#010x} in flash against {fn}...')
            if chip.read(off, len(img)) == img:
                print('-- verify OK (digest matched)')
            else:
                print('-- verify FAILED (digest mismatch)')
                bad = True
        if bad:
            print('\nA fatal error occurred: Verify failed.')
            rc = 2
    elif a.cmd == 'write_flash':
        pairs = _pairs(a.addr_filename)
        entry['addrs'] = [p[0] for p in pairs]
        for off, fn in pairs:
            img = _bytes(fn)
            end = (off + len(img) + SECTOR - 1) // SECTOR * SECTOR
            chip.write(off, b'\xff' * (end - off))     # esptool erases whole sectors
            chip.write(off, img)
            if chip.cfg.get('corrupt_nvs_on_write'):
                b = chip.read(NVS_OFF + 0x100, 1)
                chip.write(NVS_OFF + 0x100, bytes([b[0] ^ 0xff]))
            print(f'Wrote {len(img)} bytes at {off:#010x}')
    elif a.cmd == 'run':
        pass
    booted = rc == 0 and (a.cmd == 'run' or a.after != 'no_reset')
    if booted:
        chip.boot()
        print('Hard resetting via RTS pin...')
    else:
        print('Staying in bootloader.')
    chip.save()
    log(rc=rc, booted=booted, nvs_sha_after=chip.nvs_sha())
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
