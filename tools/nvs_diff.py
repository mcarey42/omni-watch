#!/usr/bin/env python3
"""Compare two NVS images by key, never printing a value (Stage 4b Task 3; preflight B24, B26).

  nvs_diff.py A.bin B.bin [--allow-added NS,..] [--allow-changed NS/KEY,..] [--require-same NS,..]
      Prints every added, removed and changed ns:key with its type and size, each marked ok or NOT ALLOWED, then
      the verdict. Entries are compared by (type, key, SHA-256 of the value), in memory; nvs.net80211 (the Wi-Fi
      password) the same way, and no value, prefix or hash is ever printed for any namespace. Exit 1 on any
      difference not allowed: an addition outside --allow-added, a change outside --allow-changed, any removal, and
      any difference at all in a --require-same namespace (the key names are listed first).
  nvs_diff.py --list A.bin
      Each namespace's keys with type and size, no values.

Images are parsed with nvs_parser through tools/nvs_fixture.py (multi-chunk blobs joined, CRCs checked). Exit 2 when
an image does not parse. Run with the ESP-IDF environment's python.
"""
import argparse
import hashlib
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nvs_fixture  # noqa: E402


def load(path):
    with open(path, 'rb') as f:
        entries, _ = nvs_fixture.parse_image(f.read())
    return {(ns, key): (t, len(v), hashlib.sha256(v).digest()) for ns, key, t, v in entries}


def csv_set(s):
    return {x.strip() for x in (s or '').split(',') if x.strip()}


def list_image(path):
    img = load(path)
    for ns in sorted({ns for ns, _ in img}):
        print(ns)
        for (n, key), (t, size, _) in sorted(img.items()):
            if n == ns:
                print(f'  {key:<15} {t:<4} {size}')
    return 0


def diff(a_path, b_path, allow_added, allow_changed, require_same):
    a, b = load(a_path), load(b_path)
    diffs = []    # (kind, ns, key, description)
    for k in sorted(set(a) | set(b)):
        ns, key = k
        if k not in a:
            diffs.append(('added', ns, key, f'{b[k][0]} {b[k][1]} B'))
        elif k not in b:
            diffs.append(('removed', ns, key, f'{a[k][0]} {a[k][1]} B'))
        elif a[k] != b[k]:
            diffs.append(('changed', ns, key, f'{a[k][0]} {a[k][1]} B -> {b[k][0]} {b[k][1]} B'))
    if not diffs:
        print('identical: no key added, removed or changed')
        return 0
    bad = []
    for kind, ns, key, desc in diffs:
        ok = (kind == 'added' and ns in allow_added) or (kind == 'changed' and f'{ns}/{key}' in allow_changed)
        if ns in require_same:
            ok = False
        print(f'{kind:<8} {ns}:{key} ({desc}){"" if ok else "  NOT ALLOWED"}')
        if not ok:
            bad.append((kind, ns, key))
    for ns in sorted(require_same):
        keys = [f'{n}:{k}' for _, n, k, _ in diffs if n == ns]
        if keys:
            print(f'require-same {ns}: differs in {", ".join(keys)}')
    if bad:
        print(f'nvs_diff: {len(bad)} difference(s) not allowed')
        return 1
    print(f'nvs_diff: {len(diffs)} difference(s), all allowed')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--list', metavar='A.bin')
    ap.add_argument('images', nargs='*')
    ap.add_argument('--allow-added', metavar='NS,..')
    ap.add_argument('--allow-changed', metavar='NS/KEY,..')
    ap.add_argument('--require-same', metavar='NS,..')
    a = ap.parse_args(argv)
    if (a.list is not None and a.images) or (a.list is None and len(a.images) != 2):
        ap.error('either --list A.bin, or A.bin B.bin')
    try:
        if a.list:
            return list_image(a.list)
        return diff(a.images[0], a.images[1], csv_set(a.allow_added), csv_set(a.allow_changed),
                    csv_set(a.require_same))
    except (nvs_fixture.FixtureError, OSError) as e:
        print(f'nvs_diff: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
