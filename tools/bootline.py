#!/usr/bin/env python3
"""Wait for a line from the watch after the flash tool's boot (Stage 4b Task 3; preflight B26).

  bootline.py [--port P] [--watch NAME] --expect TEXT [--timeout S] [--verdict]

Waits up to 10 s for the port to come back after the reset, then reads the console log for a line containing TEXT.
The first-boot lines are often printed before the port reopens, so if the line has not shown after 5 s it sends
`state` once and looks in its reply too. The reset's USB re-enumeration can drop the port after it has opened (the
by-id node may still be the old one): a read error closes it and reopens within another 10 s window. Exit 0 prints
the line; 1 timed out (S seconds, default 30, counted from when the port first opened); 2 refused (the port does not
resolve through tools/watch.local: tools/watchport.py), or the port did not come back (at the start or after a drop).

--verdict (theme.sh, final review m5): the line alone is not success. bootline also waits for the boot's migration
and settings-origin lines (the boot log's `store: migration: ...` / `store: settings: ...`, or `state`'s `migration
...` / `settings ...`), and exits 3, naming the lines, if any line seen shows a failed migration (`failed:`), a
`read-only` settings origin, a `read-only boot`, or settings kept in RAM only. Exit 1 if the verdict lines did not
show within S.

BOOTLINE_REPLAY=<file> (host tests only): the lines are read from the file instead of the port, which is never
opened; it is announced on stderr.
"""
import argparse
import os
import re
import sys
import time

sys.dont_write_bytecode = True   # importing watchport leaves no tools/__pycache__ behind
import watchport  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT_WAIT = 10.0
STATE_AFTER = 5.0
# a boot's verdict lines: the boot log's "store: migration: X" / "store: settings: X" and `state`'s "migration X" /
# "settings X" (its "settings: brightness ... (X)" line too)
MIGRATION = re.compile(r'\bmigration:? (.*)$')
SETTINGS = re.compile(r'\bsettings:? (.*)$')


def bad_line(line):
    """Whether a boot line shows a failed migration or a read-only boot."""
    m = MIGRATION.search(line)
    if m and 'failed' in m.group(1):
        return True
    m = SETTINGS.search(line)
    if m and 'read-only' in m.group(1):
        return True
    return 'read-only boot' in line or 'kept in RAM only' in line


class PortLost(Exception):
    pass


def watch(ser, expect, timeout, now=time.monotonic, state_after=STATE_AFTER, reopen=None, verdict=None):
    """Read lines from `ser` (readline() with a short timeout, write()) until one contains `expect`; the line, or
    None after `timeout` s. Sends `state` once after `state_after` s. A read or write error (pyserial's
    SerialException is an OSError) closes `ser` and takes a new one from reopen(), which returns None when the port
    has not come back in its window: PortLost.
    verdict (a list): also wait until a migration line and a settings line have been seen (else None at the
    timeout), and append every bad_line seen to it."""
    t0 = now()
    sent = False
    found = None
    seen_mig = seen_set = False
    while now() - t0 < timeout:
        try:
            if not sent and now() - t0 >= state_after:
                ser.write(b'\nstate\n')
                sent = True
            line = ser.readline().decode('utf8', 'replace').strip()
        except OSError as e:
            try:
                ser.close()
            except OSError:
                pass
            ser = reopen() if reopen else None
            if ser is None:
                raise PortLost(f'the port dropped ({e}) and did not come back within {PORT_WAIT:.0f} s') from None
            continue
        if verdict is not None and line:
            seen_mig = seen_mig or bool(MIGRATION.search(line))
            seen_set = seen_set or bool(SETTINGS.search(line))
            if bad_line(line):
                verdict.append(line)
        if found is None and expect in line:
            found = line
        if found is not None and (verdict is None or (seen_mig and seen_set)):
            return found
    return None


class Replay:
    """BOOTLINE_REPLAY's serial: the file's lines, then read timeouts."""

    def __init__(self, path):
        with open(path, encoding='utf8') as f:
            self.lines = f.read().splitlines()

    def readline(self):
        if self.lines:
            return self.lines.pop(0).encode() + b'\r\n'
        time.sleep(0.05)
        return b''

    def write(self, b):
        pass

    def close(self):
        pass


def open_port(port, wait):
    import serial
    deadline = time.monotonic() + wait
    while True:
        try:
            if os.path.exists(port):
                return serial.Serial(port, 115200, timeout=0.5)
        except OSError:      # serial.SerialException included
            pass
        if time.monotonic() >= deadline:
            return None
        time.sleep(0.2)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port', help='a by-id path listed in tools/watch.local, exactly')
    ap.add_argument('--watch', metavar='NAME', help='a watch named in tools/watch.local')
    ap.add_argument('--expect', required=True)
    ap.add_argument('--timeout', type=float, default=30.0)
    ap.add_argument('--verdict', action='store_true')
    a = ap.parse_args(argv)
    try:
        a.port = watchport.resolve(REPO, a.port, a.watch)
    except watchport.Refused as e:
        print(f'bootline: refused: {e}', file=sys.stderr)
        return 2
    # Only beside the fake chip (the host test rig sets FAKE_ESPTOOL_DIR): a stray variable in a real shell must
    # never make a real swap read a file instead of the watch.
    replay = os.environ.get('BOOTLINE_REPLAY') if os.environ.get('FAKE_ESPTOOL_DIR') else None
    if os.environ.get('BOOTLINE_REPLAY') and not replay:
        print('bootline: BOOTLINE_REPLAY ignored (no fake chip): reading the port', file=sys.stderr)
    if replay:
        print(f'bootline: replaying {replay} (BOOTLINE_REPLAY), not the port', file=sys.stderr)
    ser = Replay(replay) if replay else open_port(a.port, PORT_WAIT)
    if ser is None:
        print(f'bootline: {a.port} did not come back within {PORT_WAIT:.0f} s', file=sys.stderr)
        return 2
    holder = [ser]

    def reopen():
        holder[0] = open_port(a.port, PORT_WAIT)
        return holder[0]

    bad = [] if a.verdict else None
    try:
        line = watch(ser, a.expect, a.timeout, reopen=reopen, verdict=bad)
    except PortLost as e:
        print(f'bootline: {e}', file=sys.stderr)
        return 2
    finally:
        if holder[0] is not None:
            holder[0].close()
    if bad:
        for b in bad:
            print(f'bootline: {b}', file=sys.stderr)
        print('bootline: FAILED: the new app booted read-only or its migration failed (lines above); nothing of '
              'the records is written on such a boot', file=sys.stderr)
        return 3
    if line is None:
        more = ' and its migration and settings lines' if a.verdict else ''
        print(f'bootline: no line with {a.expect!r}{more} within {a.timeout:.0f} s (log and `state`)',
              file=sys.stderr)
        return 1
    print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
