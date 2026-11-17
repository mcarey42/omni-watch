#!/usr/bin/env python3
"""The golden NVS image for the omni-to-watch migration (Stage 4b Task 2; spec §5 test 3, R124 item 4).

  nvs_fixture.py make  [--host H] [--overwrite]
      Writes the fixture's value files (firmware/test/host/nvs/*.hex) once, from `migrate_host --emit-fixture`.
      They are committed; `check` never regenerates them (preflight C4). A committed file that differs from the
      emitted one stops `make` (nothing written) unless --overwrite is given.
  nvs_fixture.py run <csv|bin> --theme <omni|dw> [--fail-after N] [--force] --out D [--host H]
      CSV -> image (nvs_partition_gen's NVS writer) -> parse (IDF nvs_parser) -> kv lines -> migrate_host -> CSV ->
      a regenerated image D/out.bin -> its dump D/out.dump.txt. Every intermediate goes under D, created mode 0700,
      files 0600: nothing in /tmp or the working directory (preflight B6). Run over a real backup, D holds the Wi-Fi
      password: keep it out of the repo. The dumps print every namespace but omni, watch and t_omni as
      (type, key, length) only: no value, no hash, no prefix.
  nvs_fixture.py check --host H [--work W] [--update]
      Pins the fixture's omni/settings against a hand-written byte table, runs every scenario and diffs each dump
      with its committed firmware/test/host/nvs/expected_<scenario>.txt. --update (the explicit re-bless) writes
      them instead, and only after every scenario and secret_confined have passed: read each by eye before
      committing.

Images are read with nvs_parser (NVS_Partition), never with `nvs_tool -d minimal` text: multi-chunk blobs are joined
through their blob_index (chunk_start, chunk_count), integer entries keep their type. Anything the parse cannot
account for (a page that is not Active/Full/Empty, a bad CRC, a duplicate key, a missing chunk) stops the run.

Images are built with the NVS class of esp_idf_nvs_partition_gen (what `nvs_partition_gen.py generate` runs), driven
row by row from the CSV. Not the CLI's own CSV loop: it injects three made-up entries (sta.apinfo, sta.pmk,
sta.apsw) after any `sta.ssid` or `sta.pswd` row, and for those two keys takes a `file` row's path as the value
(esp_idf_nvs_partition_gen write_entry). Over a real nvs.net80211 that would corrupt the Wi-Fi namespace.
"""
import argparse
import csv
import hashlib
import io
import os
import shutil
import stat
import struct
import subprocess
import sys

sys.dont_write_bytecode = True   # no __pycache__ beside anything we import

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NVS_DIR = os.path.join(REPO, 'firmware', 'test', 'host', 'nvs')
NVS_SIZE = 0x6000                # the nvs partition: 0x9000, 24 KB (6 pages)
OWNED = ('omni', 'watch', 't_omni')   # printed in full; every other namespace by type and length only
REDACTED = ('pop',)                   # owned keys holding a secret (the provisioning proof of possession): type and length only
MIGRATED = ('watch', 't_omni')        # the only namespaces a run may change
SECRET = b'FAKE-SECRET-1234'          # the fixture's synthetic sta.pswd (never the owner's)

INT_TYPES = {  # line type: (nvs_parser type, width, signed, CSV encoding)
    'u8': ('uint8_t', 1, False), 'i8': ('int8_t', 1, True), 'u16': ('uint16_t', 2, False),
    'i16': ('int16_t', 2, True), 'u32': ('uint32_t', 4, False), 'i32': ('int32_t', 4, True),
    'u64': ('uint64_t', 8, False), 'i64': ('int64_t', 8, True),
}
PARSER_INT = {v[0]: k for k, v in INT_TYPES.items()}

# The v2 omni/settings blob of the sample values, written by hand (settings.h's layout), not by the encoder:
# Columbus US, brightness 45, sleep 15 s, 12 h, sound on, volume 30, reduced motion off, reveal flip,
# carousel {0, 1, 2, 3, 7}.
SETTINGS_V2_SAMPLE = bytes(
    [0x02, 0x00] +                                                      # 0  version 2
    [0x43, 0x6f, 0x6c, 0x75, 0x6d, 0x62, 0x75, 0x73] + [0x00] * 24 +    # 2  name[32] "Columbus"
    [0x55, 0x53, 0x00, 0x00] +                                          # 34 country[4] "US"
    [45, 15, 0, 1, 30, 0, 0] +                                          # 38 brightness .. reveal_style
    [0, 1, 2, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] +                  # 45 carousel[16]
    [5])                                                                # 61 carousel_len


class FixtureError(Exception):
    pass


def _idf_imports():
    idf = os.environ.get('IDF_PATH')
    if not idf:
        raise FixtureError('IDF_PATH not set: source ~/esp/v5.5.6/esp-idf/export.sh')
    sys.path.insert(0, os.path.join(idf, 'components', 'nvs_flash', 'nvs_partition_tool'))
    try:
        import nvs_parser  # noqa: E402
        from esp_idf_nvs_partition_gen import nvs_partition_gen  # noqa: E402
    except ImportError as e:
        raise FixtureError(f'{e}: run with the ESP-IDF environment\'s python') from None
    return nvs_parser, nvs_partition_gen


# ------------------------------------------------------------------ images

def gen_image(csv_path, out_path):
    """CSV (nvs_partition_gen's format: key,type,encoding,value) -> image. `file` rows are relative to the CSV."""
    _, g = _idf_imports()
    base = os.path.dirname(os.path.abspath(csv_path))
    size, read_only = g.check_size(hex(NVS_SIZE))
    buf = io.BytesIO()
    with open(csv_path, newline='', encoding='utf8') as f:
        rows = list(csv.DictReader(line for line in f if not line.startswith('#')))
    with g.nvs_open(buf, size, g.Page.VERSION2, read_only=read_only) as nvs:
        for row in rows:
            key, typ, enc, val = row['key'], row['type'], row['encoding'], row['value']
            if len(key) > 15:
                raise FixtureError(f'{csv_path}: key {key!r} over 15 characters')
            if typ == 'namespace':
                nvs.write_namespace(key)
                continue
            if typ == 'file':
                with open(os.path.join(base, val), 'rb') as vf:
                    val = vf.read()
            elif typ != 'data':
                raise FixtureError(f'{csv_path}: row {key}: type {typ!r}')
            try:
                # the namespace explicitly: with None, write_entry takes the newest namespace, so a CSV that comes
                # back to an earlier namespace would write into the wrong one
                nvs.write_entry(key, val, enc, nvs.get_namespace_idx())
            except g.InputError as e:
                raise FixtureError(f'{csv_path}: row {key}: {e}') from None
    with open(out_path, 'wb') as f:
        f.write(buf.getvalue())


def parse_image(data):
    """Image bytes -> (entries, chunks). entries: list of (ns, key, type, value bytes), the image's order; type is
    a line type (blob str u8 ...). chunks: {(ns, key): chunk_count} for multi-chunk blobs."""
    p, _ = _idf_imports()
    part = p.NVS_Partition('nvs', bytearray(data))
    ns_names = {}
    heads = []            # (ns index, key, parser type, entry)
    data_chunks = {}      # (ns index, key, chunk_index) -> bytes
    for page in part.pages:
        status = page.header['status']
        if status == 'Empty':
            continue
        where = f'page at {page.start_address:#x}'
        if status not in ('Active', 'Full'):
            raise FixtureError(f'{where}: status {status}')
        if page.header['crc']['original'] != page.header['crc']['computed']:
            raise FixtureError(f'{where}: header CRC')
        for e in page.entries:
            if e.state != 'Written':
                continue
            md = e.metadata
            if e.key is None or md['crc']['original'] != md['crc']['computed']:
                raise FixtureError(f'{where}: entry {e.index}: bad key or CRC')
            if md['span'] > 1 and md['crc']['data_original'] != md['crc']['data_computed']:
                raise FixtureError(f'{where}: entry {e.index} ({e.key}): data CRC')
            t = md['type']
            if md['namespace'] == 0:
                if t != 'uint8_t':
                    raise FixtureError(f'{where}: entry {e.index}: namespace 0 holds {t}')
                ns_names[e.data['value']] = e.key
            elif t == 'blob_data':
                body = b''.join(bytes(c.raw) for c in e.children)[: e.data['size']]
                k = (md['namespace'], e.key, md['chunk_index'])
                if k in data_chunks:
                    raise FixtureError(f'{where}: duplicate chunk {e.key}#{md["chunk_index"]}')
                data_chunks[k] = body
            elif t in PARSER_INT or t in ('string', 'blob', 'blob_index'):
                heads.append((md['namespace'], e.key, t, e))
            else:
                raise FixtureError(f'{where}: entry {e.index} ({e.key}): type {t}')

    entries, chunks, used, seen = [], {}, set(), set()
    for nsi, key, t, e in heads:
        if nsi not in ns_names:
            raise FixtureError(f'{key}: namespace index {nsi} is not defined')
        ns = ns_names[nsi]
        if (ns, key) in seen:
            raise FixtureError(f'{ns}/{key}: written twice')
        seen.add((ns, key))
        if t in PARSER_INT:
            lt = PARSER_INT[t]
            _, width, signed = INT_TYPES[lt]
            val = e.data['value'].to_bytes(width, 'little', signed=signed)
        elif t in ('string', 'blob'):    # single-entry string, or a blob in the old one-page format
            val = b''.join(bytes(c.raw) for c in e.children)[: e.data['size']]
            if t == 'string':
                if not val.endswith(b'\x00'):
                    raise FixtureError(f'{ns}/{key}: string without its NUL')
                val, lt = val[:-1], 'str'
            else:
                lt = 'blob'
        else:                            # blob_index: join chunk_start .. chunk_start + chunk_count - 1
            start, count = e.data['chunk_start'], e.data['chunk_count']
            parts = []
            for ci in range(start, start + count):
                if (nsi, key, ci) not in data_chunks:
                    raise FixtureError(f'{ns}/{key}: chunk {ci} missing')
                parts.append(data_chunks[(nsi, key, ci)])
                used.add((nsi, key, ci))
            val, lt = b''.join(parts), 'blob'
            if len(val) != e.data['size']:
                raise FixtureError(f'{ns}/{key}: chunks hold {len(val)} B, index says {e.data["size"]}')
            if count > 1:
                chunks[(ns, key)] = count
        entries.append((ns, key, lt, val))
    orphans = [k for k in data_chunks if k not in used]
    if orphans:   # IDF erases these at init (an interrupted rewrite); count only, never their contents
        print(f'note: {len(orphans)} orphan blob chunk(s) ignored', file=sys.stderr)
    return entries, chunks


# ------------------------------------------------------------------ kv lines and CSV

def kv_lines(entries):
    return ''.join(f'{ns} {key} {t} {val.hex() if val else "-"}\n' for ns, key, t, val in entries)


def read_kv(path):
    out = []
    with open(path, encoding='ascii') as f:
        for n, line in enumerate(f, 1):
            parts = line.split()
            if not parts:
                continue
            if len(parts) != 4:
                raise FixtureError(f'{path}:{n}: expected 4 fields')
            ns, key, t, hx = parts
            out.append((ns, key, t, b'' if hx == '-' else bytes.fromhex(hx)))
    return out


def write_csv(entries, path):
    """kv entries -> CSV, grouped by namespace in first-seen order, values inline (no file rows)."""
    order = []
    for ns, *_ in entries:
        if ns not in order:
            order.append(ns)
    with open(path, 'w', newline='', encoding='utf8') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['key', 'type', 'encoding', 'value'])
        for ns in order:
            w.writerow([ns, 'namespace', '', ''])
            for ens, key, t, val in entries:
                if ens != ns:
                    continue
                if key.startswith('#'):
                    raise FixtureError(f'{ns}/{key}: a key the CSV reader would skip as a comment')
                if t == 'blob':
                    w.writerow([key, 'data', 'hex2bin', val.hex()])
                elif t == 'str':
                    try:
                        s = val.decode('utf8')
                    except UnicodeDecodeError:
                        raise FixtureError(f'{ns}/{key}: a string nvs_partition_gen cannot write back') from None
                    if '\x00' in s or '\n' in s or '\r' in s:   # the CSV reader drops lines starting with '#'
                        raise FixtureError(f'{ns}/{key}: a string nvs_partition_gen cannot write back')
                    w.writerow([key, 'data', 'string', s])
                elif t in INT_TYPES:
                    _, _, signed = INT_TYPES[t]
                    w.writerow([key, 'data', t, str(int.from_bytes(val, 'little', signed=signed))])
                else:
                    raise FixtureError(f'{ns}/{key}: type {t}')


# ------------------------------------------------------------------ the dump

def _cstr(b):
    return b.split(b'\x00', 1)[0].decode('ascii', 'replace')


def _carousel(b, n):
    return '[' + ','.join(str(x) for x in b[: min(n, 16)]) + ']'


def decode(ns, key, t, v):
    """A one-line reading of the records the migration reads or writes (independent of the C codecs)."""
    if t == 'str':
        return f'"{v.decode("utf8", "replace")}"'
    if t != 'blob':
        return None
    ver = int.from_bytes(v[:2], 'little') if len(v) >= 2 else None
    if key == 'settings' and ns == 'omni':
        if ver == 2 and len(v) == 62:
            f = v[38:]
            return (f'settings v2: {_cstr(v[2:34])}/{_cstr(v[34:38])} brightness {f[0]} sleep {f[1]} h24 {f[2]} '
                    f'sound {f[3]} volume {f[4]} reduced {f[5]} reveal {f[6]} carousel {_carousel(f[7:23], f[23])} '
                    f'len {f[23]}')
        if ver == 1 and len(v) == 28:
            f = v[4:]
            return (f'settings v1: city index {int.from_bytes(v[2:4], "little")} brightness {f[0]} sleep {f[1]} '
                    f'h24 {f[2]} sound {f[3]} volume {f[4]} reduced {f[5]} reveal {f[6]} '
                    f'carousel {_carousel(f[7:23], f[23])} len {f[23]}')
        return f'settings: version {ver}, {len(v)} B (unusable)'
    if key in ('shared', 'local') and len(v) >= 4:
        size = int.from_bytes(v[2:4], 'little')
        head = f'{key} v{ver} size {size}'
        if ver != 1:
            return f'{head} (a version this build does not read)'
        if key == 'shared' and size >= 46:
            f = v[40:46]
            return (f'{head}: {_cstr(v[4:36])}/{_cstr(v[36:40])} brightness {f[0]} sleep {f[1]} h24 {f[2]} '
                    f'sound {f[3]} volume {f[4]} reduced {f[5]} tail {size - 46}')
        if key == 'local' and size >= 23:
            return (f'{head}: reveal {v[4]} carousel {_carousel(v[6:22], v[5])} len {v[5]} face {v[22]} '
                    f'tail {size - 23}')
        return f'{head} (short)'
    if key == 'clockcal' and len(v) >= 13:
        rate, interval = struct.unpack_from('<iI', v, 2)
        return (f'clockcal v{ver}, {len(v)} B: rate {rate} ppm interval {interval} s n_est {v[10]} log_n {v[11]} '
                f'log_head {v[12]}')
    if key == 'batrec' and len(v) == 114:
        r = v[2:]
        def snap(o):
            return f'pct {struct.unpack_from("<b", r, o + 16)[0]} mv {struct.unpack_from("<h", r, o + 14)[0]}'
        lift, touch, glance = struct.unpack_from('<III', r, 96)
        return (f'batrec v{ver}, {len(v)} B: start {snap(0)} end {snap(64)} wakes lift {lift} touch {touch} '
                f'glance {glance} on_battery {r[108]} boot {struct.unpack_from("<H", r, 110)[0]}')
    return None


def dump(entries):
    """Sorted by (namespace, key). omni, watch, t_omni in full with a decoded reading; any other namespace by type
    and length only (nvs.net80211 holds the Wi-Fi password: not even a hash of it, which could be guessed offline).
    Equality of those values is checked in memory, on the full bytes (`run`)."""
    out = []
    for ns, key, t, v in sorted(entries, key=lambda e: (e[0], e[1])):
        if ns in OWNED and key not in REDACTED:
            out.append(f'{ns} {key} {t} {len(v)} {v.hex() if v else "-"}\n')
            d = decode(ns, key, t, v)
            if d:
                out.append(f'    = {d}\n')
        else:
            out.append(f'{ns} {key} {t} {len(v)}\n')
    return ''.join(out)


def by_ns(entries, keep):
    return sorted((e for e in entries if keep(e[0])), key=lambda e: (e[0], e[1]))


# ------------------------------------------------------------------ run

def _make_private_dir(d):
    if os.path.lexists(d):
        if not os.path.isdir(d) or os.path.islink(d) or os.listdir(d):
            raise FixtureError(f'{d}: exists and is not an empty directory')
    else:
        os.makedirs(d)
    os.chmod(d, 0o700)


def _write(path, text):
    with open(path, 'w', encoding='ascii', newline='\n') as f:
        f.write(text)


def default_host():
    return os.path.join(REPO, 'build-host', 'migrate_host')


def run(src, theme, out, host, fail_after=None, force=False, quiet=False):
    """The pipeline; returns (input entries, output entries, input chunks, migrate_host's report)."""
    old_umask = os.umask(0o077)
    try:
        out = os.path.abspath(out)
        _make_private_dir(out)
        if src.endswith('.csv'):
            img = os.path.join(out, 'in.bin')
            gen_image(src, img)
        else:
            img = src
        with open(img, 'rb') as f:
            ins, chunks = parse_image(f.read())
        _write(os.path.join(out, 'in.kv'), kv_lines(ins))
        _write(os.path.join(out, 'in.dump.txt'), dump(ins))
        cmd = [os.path.abspath(host), '--in', 'in.kv', '--out', 'out.kv', '--theme', theme]
        if fail_after is not None:
            cmd += ['--fail-after', str(fail_after)]
        if force:
            cmd.append('--force')
        r = subprocess.run(cmd, cwd=out, capture_output=True, text=True)
        _write(os.path.join(out, 'migrate.log'), r.stdout + r.stderr)
        if r.returncode != 0:
            raise FixtureError(f'migrate_host exited {r.returncode}: {r.stderr.strip()}')
        outs_kv = read_kv(os.path.join(out, 'out.kv'))
        write_csv(outs_kv, os.path.join(out, 'out.csv'))
        gen_image(os.path.join(out, 'out.csv'), os.path.join(out, 'out.bin'))
        with open(os.path.join(out, 'out.bin'), 'rb') as f:
            outs, _ = parse_image(f.read())
        _write(os.path.join(out, 'out.dump.txt'), dump(outs))

        # the regenerated image holds exactly what migrate_host wrote, and nothing outside watch/t_omni moved
        if sorted(outs) != sorted(outs_kv):
            raise FixtureError('the regenerated image differs from migrate_host\'s output')
        def untouched(ns):
            return ns not in MIGRATED
        if by_ns(ins, untouched) != by_ns(outs, untouched):
            raise FixtureError('an entry outside watch/t_omni changed (type, key or value)')
        if not quiet:
            print(r.stdout.strip())
            for ns in sorted({e[0] for e in ins if untouched(e[0])}):
                n = sum(1 for e in ins if e[0] == ns)
                print(f'unchanged: {ns} ({n} entries, type/key/value)')
            print(f'out: {out}/out.bin, dump {out}/out.dump.txt')
        return ins, outs, chunks, r.stdout
    finally:
        os.umask(old_umask)


# ------------------------------------------------------------------ check

def _scenario_runs(work, host):
    """Each scenario's final dump, plus its own assertions. Yields (name, dump text)."""
    stage3 = os.path.join(NVS_DIR, 'stage3.csv')

    def go(name, src, theme, **kw):
        return run(src, theme, os.path.join(work, name), host, quiet=True, **kw)

    ins, outs, _, _ = go('omni', stage3, 'omni')
    omni_dump = dump(outs)
    yield 'omni', omni_dump

    go('dw_then_omni/1', stage3, 'dw')
    _, outs, _, rep = go('dw_then_omni/2', os.path.join(work, 'dw_then_omni/1/out.bin'), 'omni')
    if 'shared NOT_NEEDED' not in rep:
        raise FixtureError('dw_then_omni: the Omni\'s shared part ran again')
    yield 'dw_then_omni', dump(outs)

    for n in range(5):
        name = f'interrupted_{n}'
        _, mid, _, rep = go(f'{name}/1', stage3, 'omni', fail_after=n)
        if 'FAILED' not in rep or any(e[:2] == ('watch', 'shared') for e in mid):
            raise FixtureError(f'{name}: the first run was not interrupted before the marker')
        _, outs, _, _ = go(f'{name}/2', os.path.join(work, f'{name}/1/out.bin'), 'omni')
        d = dump(outs)
        if d != omni_dump:
            raise FixtureError(f'{name}: differs from the uninterrupted run')
        yield name, d

    ins, outs, _, rep = go('newer_shared', os.path.join(NVS_DIR, 'stage3_newer_shared.csv'), 'omni')
    want = [e for e in ins if e[:2] == ('watch', 'shared')]
    if not want or want != [e for e in outs if e[:2] == ('watch', 'shared')] or 'shared NOT_NEEDED' not in rep:
        raise FixtureError('newer_shared: watch/shared changed')
    yield 'newer_shared', dump(outs)

    ins, outs, chunks, _ = go('chunked', os.path.join(NVS_DIR, 'stage3_chunked.csv'), 'omni')
    if chunks.get(('omni', 'clockcal')) != 2:
        raise FixtureError(f'chunked: omni/clockcal has chunk_count {chunks.get(("omni", "clockcal"))}, not 2')
    def mig(entries):
        return by_ns(entries, lambda ns: ns in MIGRATED)
    _, omni_outs, _, _ = go('chunked_ref', stage3, 'omni')
    if mig(outs) != mig(omni_outs):
        raise FixtureError('chunked: watch/t_omni differ from the omni scenario\'s')
    yield 'chunked', dump(outs)


def _file_digest(p):
    try:
        with open(p, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except (IsADirectoryError, FileNotFoundError):
        return None


def _tree_state(work, host):
    """What a run must not change: the repo working tree (`git status --porcelain --untracked-files=all`, with the
    content of each listed file) and the build tree (every file's size and mtime), both outside the work dir."""
    build = os.path.dirname(os.path.abspath(host))

    def outside(p):
        return os.path.commonpath([p, work]) != work

    r = subprocess.run(['git', '-C', REPO, 'status', '--porcelain', '--untracked-files=all', '-z'],
                       capture_output=True, check=True)
    repo = {}
    for item in r.stdout.decode('utf8', 'surrogateescape').split('\0'):
        if not item:
            continue
        p = os.path.join(REPO, item[3:])
        if not outside(p):
            continue
        in_build = os.path.commonpath([p, build]) == build
        repo[item] = None if in_build else _file_digest(p)   # the build tree is compared below
    tree = {}
    for root, dirs, files in os.walk(build):
        dirs[:] = [d for d in dirs if outside(os.path.join(root, d))]
        for name in files:
            p = os.path.join(root, name)
            st = os.lstat(p)
            tree[p] = (st.st_size, st.st_mtime_ns)
    return repo, tree


def _secret_confined(work, host):
    """`run` over the fixture, from an empty working directory with TMPDIR pointed at an empty directory: the
    synthetic password appears in no file outside D, D is 0700 and its files 0600, the dumps do not print it, and
    the run leaves no new or changed file in the repo working tree or the build tree."""
    base = os.path.join(work, 'secret_confined')
    cwd, tmp, d = (os.path.join(base, x) for x in ('cwd', 'tmp', 'D'))
    os.makedirs(cwd)
    os.makedirs(tmp)
    before = sorted(os.listdir(NVS_DIR))
    trees_before = _tree_state(work, host)
    env = dict(os.environ, TMPDIR=tmp, TMP=tmp, TEMP=tmp)
    r = subprocess.run([sys.executable, os.path.abspath(__file__), 'run', os.path.join(NVS_DIR, 'stage3.csv'),
                        '--theme', 'omni', '--out', d, '--host', os.path.abspath(host)],
                       cwd=cwd, env=env, capture_output=True)
    if r.returncode != 0:
        raise FixtureError(f'secret_confined: run failed: {r.stderr.decode(errors="replace")}')
    needles = [SECRET, SECRET.hex().encode(), SECRET.hex().upper().encode()]
    if any(n in r.stdout + r.stderr for n in needles):
        raise FixtureError('secret_confined: the password was printed')
    if stat.S_IMODE(os.stat(d).st_mode) != 0o700:
        raise FixtureError(f'secret_confined: D is mode {stat.S_IMODE(os.stat(d).st_mode):o}')
    found_in_d = False
    for root, _, files in os.walk(base):
        for name in files:
            p = os.path.join(root, name)
            with open(p, 'rb') as f:
                body = f.read()
            hit = any(n in body for n in needles)
            inside = os.path.commonpath([p, d]) == d
            if hit and not inside:
                raise FixtureError(f'secret_confined: the password is in {p}')
            if inside:
                found_in_d = found_in_d or hit
                if stat.S_IMODE(os.stat(p).st_mode) & 0o077:
                    raise FixtureError(f'secret_confined: {p} is readable by others')
                if hit and name.endswith('dump.txt'):
                    raise FixtureError(f'secret_confined: the dump {name} prints the password')
    if not found_in_d:
        raise FixtureError('secret_confined: the password is not in D at all (the test proves nothing)')
    if sorted(os.listdir(NVS_DIR)) != before:
        raise FixtureError('secret_confined: run wrote into the fixture directory')
    repo_after, tree_after = _tree_state(work, host)
    repo_before, tree_before = trees_before
    changed = sorted(set(repo_after.items()) ^ set(repo_before.items()))
    if changed:
        raise FixtureError(f'secret_confined: the repo working tree changed: {[k for k, _ in changed][:5]}')
    changed = sorted(set(tree_after.items()) ^ set(tree_before.items()))
    if changed:
        raise FixtureError(f'secret_confined: the build tree changed: {[k for k, _ in changed][:5]}')


def check(host, work, update):
    got = _read_hex(os.path.join(NVS_DIR, 'settings.hex'))
    if got != SETTINGS_V2_SAMPLE:
        raise FixtureError('settings.hex is not the hand-written sample v2 blob')
    for name, size in (('clockcal.hex', 274), ('batrec.hex', 114), ('shared_v2.hex', 50)):
        if len(_read_hex(os.path.join(NVS_DIR, name))) != size:
            raise FixtureError(f'{name}: not {size} B')
    work = os.path.abspath(work)
    marker = os.path.join(work, '.nvs_fixture_work')
    if os.path.exists(work):
        if not os.path.exists(marker):
            raise FixtureError(f'{work}: exists and is not a work directory of this tool')
        shutil.rmtree(work)
    os.makedirs(work)
    open(marker, 'w').close()
    failed = 0
    dumps = list(_scenario_runs(work, host))   # every scenario's own assertions, before anything is written
    _secret_confined(work, host)
    print('ok   secret_confined')
    print('ok   nvs.net80211 and omni equal by (type, key, value) in every scenario')
    if update:
        for name, text in dumps:
            path = os.path.join(NVS_DIR, f'expected_{name}.txt')
            old = open(path, encoding='ascii').read() if os.path.exists(path) else None
            _write(path, text)
            print(f'wrote {os.path.relpath(path, REPO)}{" (unchanged)" if old == text else ""}')
        return
    for name, text in dumps:
        path = os.path.join(NVS_DIR, f'expected_{name}.txt')
        want = open(path, encoding='ascii').read() if os.path.exists(path) else None
        if want != text:
            failed += 1
            print(f'FAIL {name}: the dump differs from {os.path.relpath(path, REPO)}')
            import difflib
            sys.stdout.writelines(difflib.unified_diff((want or '').splitlines(True), text.splitlines(True),
                                                       'expected', 'got'))
        else:
            print(f'ok   {name}')
    if failed:
        raise FixtureError(f'{failed} scenario(s) differ')


def _read_hex(path):
    with open(path, encoding='ascii') as f:
        return bytes.fromhex(f.read().strip())


VALUE_FILES = ('settings.hex', 'clockcal.hex', 'batrec.hex', 'shared_v2.hex')


def make(host, overwrite):
    """Emits into a private directory beside migrate_host, then copies into the fixture: a committed file that
    differs stops it before anything is written, unless overwrite."""
    stage = os.path.join(os.path.dirname(os.path.abspath(host)), 'nvs_fixture_make')
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage, mode=0o700)
    r = subprocess.run([os.path.abspath(host), '--emit-fixture', stage])
    if r.returncode != 0:
        raise FixtureError('migrate_host --emit-fixture failed')
    differ = []
    for name in VALUE_FILES:
        dst = os.path.join(NVS_DIR, name)
        if os.path.exists(dst) and _file_digest(dst) != _file_digest(os.path.join(stage, name)):
            differ.append(name)
    if differ and not overwrite:
        raise FixtureError(f'{", ".join(differ)}: differ from the committed files; nothing written '
                           '(--overwrite replaces them, and changes the golden fixture)')
    for name in VALUE_FILES:
        dst = os.path.join(NVS_DIR, name)
        if not os.path.exists(dst) or name in differ:
            shutil.copyfile(os.path.join(stage, name), dst)
            print(f'wrote {os.path.relpath(dst, REPO)}')
        else:
            print(f'same  {os.path.relpath(dst, REPO)}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('make')
    m.add_argument('--host', default=default_host())
    m.add_argument('--overwrite', action='store_true', help='replace committed value files that differ')
    r = sub.add_parser('run')
    r.add_argument('src', help='a CSV (nvs_partition_gen format) or an image (.bin)')
    r.add_argument('--theme', required=True, choices=['omni', 'dw'])
    r.add_argument('--fail-after', type=int)
    r.add_argument('--force', action='store_true')
    r.add_argument('--out', required=True)
    r.add_argument('--host', default=default_host())
    c = sub.add_parser('check')
    c.add_argument('--host', default=default_host())
    c.add_argument('--work', help='scratch for the scenario runs (default: beside migrate_host)')
    c.add_argument('--update', action='store_true', help='write the expected dumps instead of comparing')
    a = ap.parse_args()
    try:
        if a.cmd == 'make':
            make(a.host, a.overwrite)
        elif a.cmd == 'run':
            run(a.src, a.theme, a.out, a.host, a.fail_after, a.force)
        elif not os.path.exists(os.path.join(REPO, '.git')):   # final review m6: a download without .git
            print(f'nvs_fixture: SKIPPED: no .git in {REPO}: check compares `git status` before and after a run; '
                  'the host tests are run from a git clone (README, Host tests)')
            return 77
        else:
            check(a.host, a.work or os.path.join(os.path.dirname(os.path.abspath(a.host)), 'nvs_fixture_work'),
                  a.update)
    except FixtureError as e:
        print(f'nvs_fixture: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
