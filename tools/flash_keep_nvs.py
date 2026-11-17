#!/usr/bin/env python3
"""Flash the Omni Watch without touching its NVS (Stage 4b Task 3; R124 item 1, preflight A7/B10/E2/E3).

  flash_keep_nvs.py [--port P] [--watch NAME] (--backup-only | --build DIR) [--only app,assets,table] [--label L]
                    [--esptool CMD] [--dry-run]

  1. The port comes from the git-ignored tools/watch.local (tools/watchport.py): P must equal a listed by-id path
     exactly, NAME picks a listed watch, and with several listed one of them is required. Every flasher_args.json offset must be a known one,
     and no file, its end rounded up to 4 KB (esptool erases whole sectors), may reach into [0x9000, 0x10000): NVS
     and phy_init. The app is at most 4 MB, the assets end at or below 0x1000000. There is no bootloader option
     (preflight B10). A plan that writes the table needs the build's partition-table.bin byte-equal to the frozen
     table: the committed firmware/common/partitions.csv (git HEAD) through IDF's gen_esp32part.py with the apps'
     options (R124 item 6, final review m2). A rollback build carries its own table: flash it with --only app,assets.
  2. A backup: `read_flash 0x9000 0x6000` twice, compared byte for byte, kept as
     ~/omni-watch-nvs/<UTC>-<label>-{a,b}.bin (directory 0700, files 0600), with latest-<label>-{a,b}.bin symlinks
     and the SHA-256 in <UTC>-<label>.sha256 (0600, never printed: PD22). `nvs_tool.py -i -d none` must report a
     clean partition; its one allowed warning (R129) is `Found unused namespace` for namespace `misc`, which IDF's
     Wi-Fi creates and may leave without keys. Any other warning, or another unused namespace, refuses. The .sha256
     is created only once both reads match and the check passes, so on every exit it holds a verified backup's hash
     or does not exist.
  3. For app, assets, table in that order: `verify_flash` (MD5 on the chip); a match prints `skip <region>
     (verified)`, otherwise `write_flash <write_flash_args> <offset> <file>`.
  4. `read_flash 0x9000 0x6000` again, compared with backup a. Equal: the one call that boots the app. Different:
     exit 4, the chip left in download mode.

Every esptool call is `<esptool> --chip esp32s3 -p P -b 460800 --before default_reset --after no_reset <command>`
(global options before the sub-command), so nothing boots before the NVS compare. The one exception is the final
`--after hard_reset run`. --backup-only ends with that boot too, so the watch is back on its app.

--esptool (default: $FLASH_KEEP_NVS_ESPTOOL, else `python -m esptool`) replaces the esptool command, for the
host tests' fake. --dry-run checks everything, prints the plan and makes no esptool call.

Exit: 0 ok; 2 refused before any esptool call; 3 backup unstable or unparsable (nothing written); 4 NVS changed (not
booted); 5 an esptool failure (not booted); 130 interrupted (not booted). Every failure after the first esptool call
prints the watch's state (download mode, not booted, screen dark) and the safe next step for the phase it stopped in
(`recovery`); none of it carries a value from NVS.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
import traceback

sys.dont_write_bytecode = True   # importing watchport leaves no tools/__pycache__ behind
import watchport  # noqa: E402

NVS_OFF, NVS_SIZE = 0x9000, 0x6000
NVS_END = NVS_OFF + NVS_SIZE
GUARD_END = 0x10000                      # no write in [NVS_OFF, GUARD_END): NVS and phy_init (0xF000)
SECTOR = 0x1000
KNOWN = {0x0: 'bootloader', 0x8000: 'table', 0x10000: 'app', 0x410000: 'assets'}
OFFSET = {v: k for k, v in KNOWN.items()}
ORDER = ('app', 'assets', 'table')       # the write order; the bootloader is never written
APP_MAX = 4 * 1024 * 1024
ASSETS_END_MAX = 0x1000000
# write_flash_args from flasher_args.json: only these, as pairs (an erase option would wipe NVS)
WRITE_ARGS = {'--flash_mode', '--flash_size', '--flash_freq'}
BACKUP_DIR = '~/omni-watch-nvs'
NVS_TOOL_CLEAN = re.compile(r'^(Page no\. \d+\s+CRC32: OK|Page Empty)$')
# R129: the only warning that does not refuse. Exactly namespace `misc` (IDF/Wi-Fi creates it, sometimes keyless).
NVS_TOOL_ALLOWED = re.compile(r'^Found unused namespace\. Namespace index: \d{3} \[misc\]$')

EXIT_REFUSED, EXIT_BACKUP, EXIT_NVS_CHANGED, EXIT_ESPTOOL = 2, 3, 4, 5
REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TABLE_CSV = 'firmware/common/partitions.csv'
TABLE_SOURCE = f'git HEAD:{TABLE_CSV}'
# the apps' build options for the table (build.ninja's gen_esp32part.py line)
TABLE_GEN_ARGS = ['-q', '--offset', '0x8000', '--primary-bootloader-offset', '0x0', '--flash-size', '32MB']


BOOT_CMD = 'python -m esptool --chip esp32s3 -p {port} --before default_reset --after hard_reset run'
STATE = 'watch state: ROM download mode, NOT booted, screen dark.'


class Progress:
    """Where the run is, for the recovery advice: phase is setup, backup, verify, write, readback or boot."""

    def __init__(self):
        self.phase, self.region, self.written, self.backup_a = 'setup', None, [], None
        self.backup_only = False
        self.port = None     # the resolved port, for BOOT_CMD

    def at(self, phase, region=None):
        self.phase, self.region = phase, region


def recovery(code, p):
    """The lines printed after a failure: the watch's state and the one safe next step. Paths only, no values."""
    if p.phase == 'setup':
        return []        # refused before any esptool call: the watch was not touched
    written = ', '.join(p.written) if p.written else 'none'
    boot_cmd = BOOT_CMD.format(port=p.port)
    boot_ok = [f'Nothing was written. Booting the unchanged app is safe: {boot_cmd}']
    not_boot = ['Do NOT boot, reset or power-cycle the watch (a reset or a power cycle starts the app).']
    out = [STATE]
    if code == EXIT_NVS_CHANGED:
        out += not_boot + [
            f'NVS changed during the flash (regions written: {written}). The app would start on the changed NVS.',
            f'Report to the controller with backup a ({p.backup_a}) and the read-back. Restoring backup a to 0x9000 '
            'needs the controller\'s explicit go; this tool never does it.']
        return out
    what = 'interrupted' if code == 130 else 'failed'
    if code == EXIT_BACKUP:
        return out + boot_ok + ['Report the backup failure to the controller before running the tool again.']
    if p.phase == 'backup':
        return out + [f'The {what} step was the NVS backup.'] + boot_ok + [
            'Then run the same command again for a fresh backup.']
    if p.phase == 'verify':
        out.append(f'The {what} step was verify_flash {p.region}; regions written before it: {written}.')
        if not p.written:
            return out + boot_ok + ['Then run the same command again.']
        return out + not_boot + ['Run the same command again: verified regions are skipped, and NVS is read back '
                                 'and compared before the one boot.']
    if p.phase == 'write':
        return out + not_boot + [
            f'The {what} step was write_flash {p.region}: {p.region} may be partly written; regions written before '
            f'it: {written}.',
            'Run the same command again: verified regions are skipped, and NVS is read back and compared before the '
            'one boot.']
    if p.phase == 'readback':
        return out + not_boot + [
            f'The {what} step was the NVS read-back, after the writes (regions written: {written}): NVS is unverified.',
            f'Report to the controller with backup a ({p.backup_a}). A re-run of the same command backs up and reads '
            'back again before booting, but against its own fresh backup: the controller diffs that with backup a '
            'first (tools/nvs_diff.py).']
    if p.phase == 'boot':
        if p.backup_only:
            return out + [f'Nothing was written (--backup-only: NVS was only read); only the {what} boot call '
                          f'remains. Booting the unchanged app is safe: {boot_cmd}']
        return out + [f'Everything was written (regions written: {written}) and NVS read back identical; only the '
                      f'{what} boot call remains. Booting is safe: {boot_cmd}']
    return out


class Stop(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


def refuse(msg):
    raise Stop(EXIT_REFUSED, f'refused: {msg}')


def esptool_argv(base, port, after, rest):
    """The global options before the sub-command (esptool v4.12's parser)."""
    return [*base, '--chip', 'esp32s3', '-p', port, '-b', '460800', '--before', 'default_reset', '--after', after,
            *rest]


def round_up(n):
    return (n + SECTOR - 1) // SECTOR * SECTOR


# ------------------------------------------------------------------ the plan (no device contact)

def load_plan(build, regions):
    """flasher_args.json -> (write_flash_args, [(region, offset, path, size)] in ORDER). Raises Stop(2)."""
    fa_path = os.path.join(build, 'flasher_args.json')
    try:
        with open(fa_path, encoding='utf8') as f:
            fa = json.load(f)
        files = fa['flash_files']
        wargs = fa['write_flash_args']
    except (OSError, ValueError, KeyError, TypeError) as e:
        refuse(f'{fa_path}: {e}')
    if not isinstance(wargs, list) or len(wargs) % 2 or not all(isinstance(a, str) for a in wargs):
        refuse(f'write_flash_args {wargs!r}: not option/value pairs')
    for opt, val in zip(wargs[::2], wargs[1::2]):
        if opt not in WRITE_ARGS or not re.fullmatch(r'[0-9A-Za-z]+', val):
            refuse(f'write_flash_args: {opt} {val}: only {", ".join(sorted(WRITE_ARGS))} are passed through')
    found = {}
    for off_s, rel in files.items():
        try:
            off = int(off_s, 0)
        except (TypeError, ValueError):
            refuse(f'flash_files offset {off_s!r}')
        path = os.path.join(build, rel)
        try:
            size = os.path.getsize(path)
        except OSError as e:
            refuse(f'{off_s}: {e}')
        end = round_up(off + size)
        if off < GUARD_END and end > NVS_OFF:
            refuse(f'{off_s} {rel}: [{off:#x}, {end:#x}) reaches into NVS and phy_init [{NVS_OFF:#x}, {GUARD_END:#x})')
        if off not in KNOWN:
            refuse(f'flash_files offset {off_s} ({rel}) is not one of '
                   f'{", ".join(hex(k) for k in sorted(KNOWN))}')
        found[KNOWN[off]] = (off, path, size, end)
    if 'app' in found and found['app'][2] > APP_MAX:
        refuse(f'the app is {found["app"][2]} B, over 4 MB')
    if 'assets' in found and found['assets'][3] > ASSETS_END_MAX:
        refuse(f'the assets end at {found["assets"][3]:#x}, past {ASSETS_END_MAX:#x}')
    plan = []
    for r in ORDER:
        if r not in regions:
            continue
        if r not in found:
            refuse(f'{r} ({OFFSET[r]:#x}) is not in {fa_path}')
        off, path, size, _ = found[r]
        plan.append((r, off, path, size))
    for r, _, path, _ in plan:
        if r == 'table':
            check_table(path)
    return wargs, plan


def frozen_table():
    """The frozen table's bytes: TABLE_SOURCE (the committed csv, never the working tree's) through IDF's
    gen_esp32part.py with TABLE_GEN_ARGS. Raises Stop(2) if either is unavailable."""
    try:
        r = subprocess.run(['git', '-C', REPO, 'show', f'HEAD:{TABLE_CSV}'], capture_output=True)
    except OSError as e:
        refuse(f'the frozen table ({TABLE_SOURCE}) could not be read: git: {e}')
    if r.returncode != 0:
        refuse(f'the frozen table ({TABLE_SOURCE}) could not be read: {r.stderr.decode(errors="replace").strip()}')
    gen = os.path.join(os.environ.get('IDF_PATH', ''), 'components', 'partition_table', 'gen_esp32part.py')
    if not os.environ.get('IDF_PATH') or not os.path.isfile(gen):
        refuse(f'gen_esp32part.py not found at {gen}: it builds the frozen table to compare the table write with; '
               'source ~/esp/v5.5.6/esp-idf/export.sh')
    with tempfile.TemporaryDirectory() as d:
        csv, out = os.path.join(d, 'partitions.csv'), os.path.join(d, 'table.bin')
        with open(csv, 'wb') as f:
            f.write(r.stdout)
        g = subprocess.run([sys.executable, gen, *TABLE_GEN_ARGS, '--', csv, out], capture_output=True, text=True)
        if g.returncode != 0 or not os.path.isfile(out):
            refuse(f'gen_esp32part.py failed on {TABLE_SOURCE}: {(g.stdout + g.stderr).strip()}')
        with open(out, 'rb') as f:
            return f.read()


def check_table(path):
    """A table write only of the frozen table's exact bytes (R124 item 6: the table is written at most once, and a
    rollback flashes app and assets only)."""
    with open(path, 'rb') as f:
        data = f.read()
    if data != frozen_table():
        refuse(f'the table to write ({path}) differs from the frozen table ({TABLE_SOURCE} via gen_esp32part.py); '
               'a rollback build carries its own table: use --only app,assets')
    print(f'table: {path} equals the frozen table ({TABLE_SOURCE})')


def parse_only(s):
    want = [x.strip() for x in s.split(',') if x.strip()]
    bad = [x for x in want if x not in ORDER]
    if bad or not want:
        refuse(f'--only {s}: regions are {",".join(ORDER)} (there is no bootloader option)')
    return set(want)


# ------------------------------------------------------------------ esptool

class Esptool:
    def __init__(self, base, port):
        self.base, self.port = base, port

    def call(self, rest, after='no_reset', ok=(0,)):
        argv = esptool_argv(self.base, self.port, after, rest)
        r = subprocess.run(argv, capture_output=True, text=True)
        if r.returncode not in ok:
            tail = (r.stdout + r.stderr).strip().splitlines()[-12:]
            raise Stop(EXIT_ESPTOOL, f'esptool {rest[0]} failed (exit {r.returncode}):\n  ' + '\n  '.join(tail))
        return r

    def read_nvs(self, path):
        self.call(['read_flash', hex(NVS_OFF), hex(NVS_SIZE), path])
        with open(path, 'rb') as f:
            data = f.read()
        if len(data) != NVS_SIZE:
            raise Stop(EXIT_ESPTOOL, f'{path}: read {len(data)} B, not {NVS_SIZE}')
        return data

    def boot(self):
        self.call(['run'], after='hard_reset')


# ------------------------------------------------------------------ the backup

def _create_private(path):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.close(fd)


def backup_dir():
    d = os.path.expanduser(BACKUP_DIR)
    os.makedirs(d, mode=0o700, exist_ok=True)
    os.chmod(d, 0o700)
    return d


def new_stem(d, label):
    ts = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    for n in range(100):
        stem = os.path.join(d, f'{ts}{"" if n == 0 else f"-{n}"}-{label}')
        created = []
        if os.path.lexists(f'{stem}.sha256'):
            continue     # taken; the .sha256 itself is not reserved here, only written once the backup verifies
        try:
            for p in (f'{stem}-a.bin', f'{stem}-b.bin'):
                _create_private(p)
                created.append(p)
        except FileExistsError:
            for p in created:    # this attempt's own empty files; whatever was there before stays
                os.unlink(p)
            continue
        return stem
    refuse(f'no free backup name for {ts}-{label} in {d}')


def nvs_tool_verdict(lines, returncode):
    """(problems, allowed): the count of nvs_tool lines that refuse, and of R129's allowed ones. A clean page report is
    neither. A non-zero exit is at least one problem."""
    lines = [ln.strip() for ln in lines if ln.strip()]
    allowed = sum(1 for ln in lines if NVS_TOOL_ALLOWED.match(ln))
    bad = sum(1 for ln in lines if not NVS_TOOL_CLEAN.match(ln) and not NVS_TOOL_ALLOWED.match(ln))
    if allowed > 1:      # R129 allows exactly one unused `misc`; a second is abnormal
        bad, allowed = bad + allowed - 1, 1
    return (max(1, bad) if returncode != 0 else bad), allowed


def nvs_tool_problems(path):
    """`nvs_tool.py -i -d none` over the image (it exits 0 either way): nvs_tool_verdict of its lines. Its lines are
    not printed: the CRC lines carry checksums of entry data, the password's included."""
    r = subprocess.run([sys.executable, nvs_tool_path(), '-i', '-d', 'none', '--color', 'never', path],
                       capture_output=True, text=True)
    return nvs_tool_verdict((r.stdout + r.stderr).splitlines(), r.returncode)


def _symlink(target, link):
    tmp = f'{link}.tmp{os.getpid()}'
    os.symlink(os.path.basename(target), tmp)
    os.replace(tmp, link)


def write_sha(stem, a, a_path, b_path):
    """<stem>.sha256 (0600), created only here, after the backup verified: written whole to a private temp file, then
    linked into place (never over an existing file), so no exit leaves it empty or partial."""
    sha = hashlib.sha256(a).hexdigest()
    final, tmp = f'{stem}.sha256', f'{stem}.sha256.tmp{os.getpid()}'
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, 'w', encoding='ascii') as f:
            f.write(f'{sha}  {os.path.basename(a_path)}\n{sha}  {os.path.basename(b_path)}\n')
            f.flush()
            os.fsync(f.fileno())
        os.link(tmp, final)
    finally:
        os.unlink(tmp)


def take_backup(et, label, prog):
    d = backup_dir()
    stem = new_stem(d, label)
    a_path, b_path = f'{stem}-a.bin', f'{stem}-b.bin'
    prog.at('backup')
    prog.backup_a = a_path
    a = et.read_nvs(a_path)
    b = et.read_nvs(b_path)
    print(f'backup a: {a_path}')
    print(f'backup b: {b_path}')
    if a != b:
        n = sum(1 for x, y in zip(a, b) if x != y)
        raise Stop(EXIT_BACKUP, f'backup unstable: the two reads differ in {n} byte(s); nothing written, not booted '
                                f'(kept: {a_path}, {b_path})')
    problems, allowed = nvs_tool_problems(a_path)
    if problems:
        raise Stop(EXIT_BACKUP, f'backup unparsable: nvs_tool.py -i reports {problems} problem line(s) in {a_path}; '
                                'nothing written, not booted. Inspect it with '
                                f'`nvs_tool.py -i -d none {a_path}`: it names keys and prints CRC32s of their data, '
                                'the password\'s included, so keep its output out of the ledger.')
    if allowed:
        print('nvs_tool: unused namespace misc, allowed (R129)')
    write_sha(stem, a, a_path, b_path)
    for s, p in (('a', a_path), ('b', b_path)):
        _symlink(p, os.path.join(d, f'latest-{label}-{s}.bin'))
    print(f'backup stable and parsed ({NVS_SIZE} B twice, identical); latest-{label}-a.bin updated')
    return stem, a


# ------------------------------------------------------------------ the flash

def flash_regions(et, wargs, plan, prog):
    results = []
    for region, off, path, size in plan:
        prog.at('verify', region)
        t0 = time.monotonic()
        r = et.call(['verify_flash', *wargs, hex(off), path], ok=(0, 2))
        out = r.stdout + r.stderr
        if r.returncode == 0 and '-- verify OK (digest matched)' in out:
            dt = time.monotonic() - t0
            print(f'skip {region} (verified)')
            results.append((region, 'skipped', dt))
            continue
        if r.returncode != 2 or '-- verify FAILED (digest mismatch)' not in out or 'Verify failed.' not in out:
            tail = out.strip().splitlines()[-12:]
            raise Stop(EXIT_ESPTOOL, f'esptool verify_flash {region} failed (exit {r.returncode}):\n  '
                       + '\n  '.join(tail))
        print(f'write {region} {off:#x} {os.path.basename(path)} ({size} B)', flush=True)
        t0 = time.monotonic()
        prog.at('write', region)
        et.call(['write_flash', *wargs, hex(off), path])
        prog.written.append(region)
        dt = time.monotonic() - t0
        print(f'  {region} written in {dt:.1f} s')
        results.append((region, 'written', dt))
    return results


def nvs_tool_path():
    return os.path.join(os.environ.get('IDF_PATH', ''), 'components', 'nvs_flash', 'nvs_partition_tool',
                        'nvs_tool.py')


def run(a, prog):
    try:
        a.port = watchport.resolve(REPO, a.port, a.watch)
    except watchport.Refused as e:
        refuse(str(e))
    prog.port = a.port
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', a.label):
        refuse(f'--label {a.label!r}: letters, digits, . _ - only')
    if a.backup_only and a.only is not None:
        refuse('--only with --backup-only')
    plan, wargs = [], []
    if a.build:
        regions = parse_only(a.only) if a.only is not None else set(ORDER)
        wargs, plan = load_plan(a.build, regions)
    default = [sys.executable, '-m', 'esptool']
    base = shlex.split(a.esptool or os.environ.get('FLASH_KEEP_NVS_ESPTOOL') or '') or default
    if base != default:   # an exported FLASH_KEEP_NVS_ESPTOOL must never pass silently for a real flash
        src = '--esptool' if a.esptool else 'FLASH_KEEP_NVS_ESPTOOL'
        print(f'esptool: {shlex.join(base)} (from {src}, not the default python -m esptool)')
    if a.dry_run:
        print(f'port: {a.port}')
        print(f'backup: would read {NVS_OFF:#x} {NVS_SIZE:#x} twice -> {BACKUP_DIR}/<UTC>-{a.label}-{{a,b}}.bin')
        for region, off, path, size in plan:
            print(f'plan: {region} {off:#x} {os.path.relpath(path, a.build)} ({size} B), verify then write')
        print('dry run: no esptool call, nothing read or written')
        return 0
    if not os.environ.get('IDF_PATH'):
        refuse('IDF_PATH not set (nvs_tool.py checks the backup): source ~/esp/v5.5.6/esp-idf/export.sh')
    if not os.path.isfile(nvs_tool_path()):
        refuse(f'nvs_tool.py not found at {nvs_tool_path()} (IDF_PATH={os.environ["IDF_PATH"]}): it checks the '
               'backup; source ~/esp/v5.5.6/esp-idf/export.sh')
    et = Esptool(base, a.port)
    t_start = time.monotonic()
    stem, nvs_a = take_backup(et, a.label, prog)
    if a.backup_only:
        prog.backup_only = True
        prog.at('boot')
        et.boot()
        print('booted (no write)')
        return 0
    results = flash_regions(et, wargs, plan, prog)
    after_path = f'{stem}-after.bin'
    prog.at('readback')
    _create_private(after_path)
    if et.read_nvs(after_path) != nvs_a:
        raise Stop(EXIT_NVS_CHANGED, f'NVS CHANGED: the read-back {after_path} differs from the backup '
                                     f'{stem}-a.bin. Not booted: the chip is left in download mode. See the keys '
                                     f'with `tools/nvs_diff.py {stem}-a.bin {after_path}` (no values).')
    print('NVS read back identical to backup a: booting', flush=True)
    prog.at('boot')
    et.boot()
    total = time.monotonic() - t_start
    print('summary:')
    for region, what, dt in results:
        print(f'  {region:<7} {what:<8} {dt:6.1f} s')
    print(f'  total {total:.1f} s')
    print('NVS identical')
    print('regions: ' + ', '.join(f'{r} {w} {dt:.1f}' for r, w, dt in results))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port', help='a by-id path listed in tools/watch.local, exactly')
    ap.add_argument('--watch', metavar='NAME', help='a watch named in tools/watch.local')
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--backup-only', action='store_true')
    mode.add_argument('--build', metavar='DIR')
    ap.add_argument('--only', metavar='app,assets,table')
    ap.add_argument('--label', default='flash')
    ap.add_argument('--esptool', metavar='CMD')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args(argv)
    os.umask(0o077)
    prog = Progress()
    try:
        return run(a, prog)
    except Stop as e:
        code = e.code
        print(f'flash_keep_nvs: {e}', file=sys.stderr)
    except KeyboardInterrupt:
        code = 130
        print('flash_keep_nvs: interrupted', file=sys.stderr)
    except Exception:    # any other error (disk full, ...) still owes the recovery advice
        code = 1
        traceback.print_exc()
    for line in recovery(code, prog):
        print(f'flash_keep_nvs: {line}', file=sys.stderr)
    return code


if __name__ == '__main__':
    sys.exit(main())
