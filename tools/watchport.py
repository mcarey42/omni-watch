#!/usr/bin/env python3
"""The watch's serial port, from a git-ignored watch file (tools/watch.local), for every tool that talks to a watch.

    watchport.py resolve [--port P] [--watch NAME]

prints the chosen port's path and exits 0, or prints the reason on stderr and exits 2. Standard library only.

The file. Each line is `<path> [name]`; `#` starts a comment, and blank lines and CR are ignored. The path must start
with /dev/serial/by-id/ and contain no `..`, whitespace or trailing `/`. The name matches [a-z0-9_-]{1,16} and
defaults to watch<N> (N counts the entries from 1). Names and paths are unique. An empty or missing file is refused,
and a bad line is refused as `watch.local:<n>: ...`. For example:

    /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<your watch's serial>-if00 first

Where it is found, first match wins: $OMNI_WATCH_LOCAL (when set, only that file: a missing one or an empty value is
refused, never passed over), then <this checkout>/tools/watch.local, then the main checkout's tools/watch.local (found through the
git common dir, for the scratch worktrees the gates build in; GIT_DIR and its kin are not passed to that git call).

The choice. --port must equal a listed path exactly, as a string (no realpath, no /dev/ttyACM* name). --watch names
an entry; with both, they must agree. With neither, a file of one entry gives that entry, and a file of several is
refused: pass --watch <name>.
"""
import argparse
import os
import re
import subprocess
import sys

ENV = 'OMNI_WATCH_LOCAL'
BY_ID = '/dev/serial/by-id/'
NAME = re.compile(r'[a-z0-9_-]{1,16}')
MISSING = "no tools/watch.local: list your watch's /dev/serial/by-id/ path there"
REL = os.path.join('tools', 'watch.local')
# git variables that would point the main-checkout lookup at another repo: never passed to it
GIT_ENV = ('GIT_DIR', 'GIT_COMMON_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE')


class Refused(Exception):
    """str(e) is the one-line reason."""


def _main_checkout(repo):
    """The main checkout of the repo `repo` belongs to, or None (not a git checkout, or git missing)."""
    env = {k: v for k, v in os.environ.items() if k not in GIT_ENV}
    try:
        r = subprocess.run(['git', '-C', repo, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                           capture_output=True, text=True, timeout=30, env=env)
    except (OSError, subprocess.SubprocessError):
        return None
    common = r.stdout.strip()
    if r.returncode != 0 or not common:
        return None
    common = os.path.normpath(common)
    return os.path.dirname(common) if os.path.basename(common) == '.git' else None


def local_path(repo):
    """The watch file in C7's order, or None if no file exists (when $OMNI_WATCH_LOCAL is set, only it counts, and an
    empty value names no file)."""
    env = os.environ.get(ENV)
    if env is not None:
        return env if env and os.path.isfile(env) else None
    own = os.path.join(repo, REL)
    if os.path.isfile(own):
        return own
    main = _main_checkout(repo)
    if main:
        p = os.path.join(main, REL)
        if os.path.isfile(p):
            return p
    return None


def _check_path(path):
    if not path.startswith(BY_ID) or len(path) == len(BY_ID):
        return f'{path}: not a {BY_ID} path'
    if '..' in path:
        return f'{path}: contains ..'
    if path.endswith('/'):
        return f'{path}: trailing /'
    if any(c.isspace() for c in path):
        return f'{path!r}: contains whitespace'
    return None


def load(path):
    """[(name, by-id path)] in file order. Raises Refused for a missing or empty file or a bad line."""
    try:
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
    except FileNotFoundError:
        raise Refused(MISSING) from None
    except (OSError, UnicodeDecodeError) as e:
        raise Refused(f'watch.local: unreadable ({e})') from None
    out, names, paths = [], set(), set()
    for n, raw in enumerate(text.split('\n'), 1):
        words = raw.replace('\r', '').split('#', 1)[0].split()
        if not words:
            continue
        port = words[0]
        why = _check_path(port)
        if why:
            raise Refused(f'watch.local:{n}: {why}')
        if len(words) > 2:
            raise Refused(f'watch.local:{n}: name {" ".join(words[1:])!r}: want [a-z0-9_-]{{1,16}}')
        name = words[1] if len(words) == 2 else f'watch{len(out) + 1}'
        if not NAME.fullmatch(name):
            raise Refused(f'watch.local:{n}: name {name!r}: want [a-z0-9_-]{{1,16}}')
        if port in paths:
            raise Refused(f'watch.local:{n}: {port}: listed twice')
        if name in names:
            raise Refused(f'watch.local:{n}: name {name!r}: listed twice')
        paths.add(port)
        names.add(name)
        out.append((name, port))
    if not out:
        raise Refused(f"watch.local: empty ({path}): list your watch's /dev/serial/by-id/ path there")
    return out


def resolve(repo, port=None, watch=None):
    """The port to use: --port (exactly a listed path), --watch (a listed name), both agreeing, or the one entry."""
    path = local_path(repo)
    if path is None:
        env = os.environ.get(ENV)
        if env is None:
            raise Refused(MISSING)
        raise Refused(f'{ENV}={env}: no such file' if env else f'{ENV} is set but empty: name a watch file or unset it')
    entries = load(path)
    listed = [p for _, p in entries]
    by_name = dict(entries)
    if watch is not None and watch not in by_name:
        raise Refused(f'--watch {watch}: not listed (listed: {", ".join(by_name)})')
    if port is not None:
        if port not in listed:
            raise Refused(f'--port {port}: only {" or ".join(listed)}')
        if watch is not None and by_name[watch] != port:
            raise Refused(f'--port {port} is not --watch {watch} ({by_name[watch]})')
        return port
    if watch is not None:
        return by_name[watch]
    if len(entries) > 1:
        raise Refused('several watches listed: pass --watch <name>')
    return listed[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('resolve', help='print the chosen port')
    r.add_argument('--port')
    r.add_argument('--watch')
    a = ap.parse_args(argv)
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        print(resolve(repo, a.port, a.watch))
    except Refused as e:
        print(e, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
