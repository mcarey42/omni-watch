#!/usr/bin/env bash
# Build the assets partition's contents (firmware/apps/omni/assets/, committed) from the mockup code and the earth maps.
#   1. npm --prefix tools ci
#   2. tools/export_layers.mjs: headless Chrome draws each layer -> tools/export/out/*.png + manifest.json
#   3. tools/gen_tex.mjs: earth512.tex (earth1024.tex retired, R69); --grade omni: omni512.tex (the Omni globe, Task 4)
#   3b. tools/ringpack (Stage 3 Task 2): the Omni ring strips omni_strip{0,1,2}.bin from the exported engr_<k>.rgba, and
#      the prebuilt dial map omni_dialmap.bin (R80);
#      and the timer's ring LUT tmr_lut.bin (R139: the face loads it instead of building it at create);
#      its manifest entries (tools/export/out/ringpack.json) join the manifest in step 4
#   3c. the sound effects sfx_{latch,whoosh,surge,chime,alarm}.pcm (Stage 3 Task 13, the alarm Stage 5; tools/gen_sfx.py
#      synthesises them from scratch). The committed files are the assets (their bytes depend on the host's libm): step
#      3 keeps them across its rm -rf, an effect with no committed file yet is written alone (gen_sfx.py write --only),
#      gen_sfx.py compare warns if this host would synthesise other bytes, and only --sfx regenerates them all.
#      gen_sfx.py manifest asserts each (length, peak, edges) and writes its PCM entries (tools/export/out/sfx.json),
#      which join the manifest in step 4
#   3d. the three brass backgrounds bg_{bezel,picker,settings}.bin (R112: flash assets instead of a PSRAM prerender).
#      The committed files are the assets: the Stage 3 device's own bytes, captured with tools/shot.py --bg (R115;
#      "src": "capture"), as host libm and FMA contraction need not reproduce them. Step 3 keeps them across its rm -rf;
#      tools/bgpack renders them here from omni_core's brassbg.c and a difference is a warning, never a rewrite; only
#      --bg replaces them with bgpack's output ("src": "bgpack": an explicit change of bytes, for the controller to rule
#      on first). Each entry carries the pixel CRC-32 (zlib's, = the device's esp_rom_crc32_le) the files must have;
#      the entries (tools/export/out/bg.json) join the manifest in step 4, whose packing and round trip skip them
#   3e. the rim keyboard's five shared assets (rim keyboard Task 8): kb_lay.bin, kb_mono21.bin, kb_mono42.bin,
#      kb_icons.bin and kb_tick.pcm, copied from firmware/components/watch_ui/assets (their source, D5; tools/fonts.sh and
#      tools/kb_assets.py write them) and their manifest entries (tools/kb_assets.py manifest: "out" "shared_ui", the
#      size and whole-file CRC-32) joined in step 4; check_apps static holds every app's copy byte-equal to the source
#   4. LVGLImage.py --ofmt BIN --cf <manifest cf> --compress NONE for each PNG -> firmware/apps/omni/assets/<name>.bin (D5);
#      RGB565 (opaque) and RGB565A8 images are packed here instead, each colour channel rounded to the nearest 5/6-bit
#      level (R86; RGB565A8 too, Stage 3 Task 7): LVGLImage's RGB565 paths take c*255 >> 8 before truncating, a whole
#      level low on the dark brass. RGB565A8 keeps LVGLImage's layout: the colour plane (unpremultiplied), then the
#      alpha plane
#   4b. hard check (R69): the files fit the assets partition of partitions.csv less 256 KiB of headroom; prints the
#      total and the bytes left
#   5. firmware/apps/omni/main/ui/assets_meta.h: sizes, box origins, pivots, the chosen globe texture
#   6. round trip: every .bin decoded back and compared with its PNG (ARGB8888 and A8 alpha exact, RGB565 and
#      RGB565A8 exactly the rounded levels, RGB565A8's alpha exact); every manifest file present, nothing else in
#      firmware/apps/omni/assets
# The manifest is typed by "cf" (C4): the LVGL images ARGB8888, RGB565, RGB565A8 and A8 (packed by step 4 and checked
# against their PNGs; an entry with "crc32" is not packed: its header, size and pixel CRC-32 are checked instead), and the raw kinds RAW_TEX (w x h x 4: a texture), STRIP (the ORS1 header, then stride x h x 3),
# RLE (omni/rle.h frames), PMAP (the OPM1 dial map, ring.h), TLUT (the OTL1 timer ring LUT, tmr_ring.h: header, size and
# payload digest checked) and PCM (s16le mono at 16 kHz, at most 3 s: 96 000 B; its
# entry has "samples" for w and h).
#   7. identity (Stage 5 Task 5 m1): every file in firmware/apps/omni/assets and every generated font .c (both font
#      dirs, tools/fonts.sh) has the SHA-256 that tools/identity.sha256 (committed) lists, and no such file is unlisted.
#      A byte change from another lv_font_conv, fontTools, Chrome or libm fails here instead of passing unseen; a change
#      meant to happen is recorded with --rehash (and its diff of identity.sha256 is the reviewer's list).
# Usage: tools/assets.sh [--check] [--sfx] [--bg] [--identity] [--rehash]   --check runs steps 4b, 6 and 7 only (6
#   needs a previous full run's tools/export/out), plus gen_sfx.py compare (a warning, never a rewrite); --sfx (full
#   run only): regenerate the sound effects with gen_sfx.py write instead of keeping the committed ones; --bg (full run
#   only): replace the committed backgrounds with bgpack's output (step 3d); --identity: step 7 alone (tools/fonts.sh
#   runs it); --rehash: rewrite tools/identity.sha256 from the files as they are, then step 7.
# Needs: node, python3, Chrome (CHROME, default /usr/bin/google-chrome), tools/fonts/ (tools/fonts.sh).
# Also needs a prior build of the Omni app (idf.py -C firmware/apps/omni build): step 4's LVGLImage.py comes from that
# build's managed components (firmware/apps/omni/managed_components/lvgl__lvgl, fetched by the component manager).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/tools/export/out
ASSETS=$ROOT/firmware/apps/omni/assets
META=$ROOT/firmware/apps/omni/main/ui/assets_meta.h
LVGLIMAGE=$ROOT/firmware/apps/omni/managed_components/lvgl__lvgl/scripts/LVGLImage.py
VENV=$ROOT/tools/.venv-assets
PYDEPS="pypng==0.20220715.0 lz4==4.4.4"     # what LVGLImage.py imports

# The globe texture, chosen by Task 5's PSRAM budget (plan D2, R48/C7). Measured 2026-10-23: psram_free 4 996 836 B on
# the digital face, so 3 948 260 B is allowed; the analog peak with earth1024 was 4 635 454 B (spin 4 472 574 B) -> 512.
# Full numbers in the Task 5 commit. earth1024.tex is no longer built (R69).
GLOBE_TEX=earth512.tex

PY=$VENV/bin/python
venv() {   # not for --identity / --rehash (the host suite's assets_identity runs those with no network)
    if [ ! -x "$PY" ] || ! "$PY" -c 'import png, lz4' 2>/dev/null; then
        python3 -m venv "$VENV"
        "$VENV/bin/pip" install -q $PYDEPS
    fi
}

check() {
    "$PY" - "$OUT" "$ASSETS" <<'EOF'
import json, os, sys, zlib, png
out, assets = sys.argv[1], sys.argv[2]
man = json.load(open(os.path.join(assets, 'manifest.json')))
CF = {'ARGB8888': 0x10, 'RGB565': 0x12, 'RGB565A8': 0x14, 'A8': 0x0e}
BPP = {0x10: 4, 0x12: 2, 0x14: 2, 0x0e: 1}
u16 = lambda b, i: b[i] | b[i + 1] << 8
def rle_size(b):   # omni/rle.h: u16 w, h, ox, oy; per row u16 nruns, per run u16 skip, u16 len, len x 3 B
    o, h = 8, u16(b, 2)
    for _ in range(h):
        if o + 2 > len(b): return -1
        n, o = u16(b, o), o + 2
        for _ in range(n):
            if o + 4 > len(b): return -1
            o += 4 + 3 * u16(b, o + 2)
    return o
bad, n = [], 0
for a in man['assets']:
    f = os.path.join(assets, a['file'])
    if not os.path.isfile(f):
        bad.append(f'{a["file"]}: missing'); continue
    data = open(f, 'rb').read()
    n += 1
    if a.get('out') == 'shared_ui':   # step 3e: the keyboard's shared assets, by size and whole-file CRC-32
        hd = (data[0], data[1], *[int.from_bytes(data[i:i + 2], 'little') for i in (4, 6, 8)]) if len(data) >= 12 else ()
        if len(data) != a.get('size') or f'{zlib.crc32(data):08x}' != a.get('crc32') or \
                (a['cf'] == 'PCM' and (len(data) % 2 or a.get('samples') != len(data) // 2)) or \
                (a['cf'] == 'RGB565A8' and (hd != (0x19, 0x14, a.get('w'), a.get('h'), 2 * a.get('w', 0)) or
                                            len(data) != 12 + 3 * a['w'] * a['h'])) or \
                a['cf'] not in ('RAW', 'PCM', 'RGB565A8'):
            bad.append(f'{a["file"]}: not the shared {a["cf"]} file of {a.get("size")} bytes with CRC-32 {a.get("crc32")} '
                       f'({len(data)} bytes, CRC-32 {zlib.crc32(data):08x})')
        continue
    if 'crc32' in a:   # committed bytes (step 3d): the LVGL header, the size and the pixel CRC-32, no PNG
        magic, cf, w, h, stride = (data[0], data[1], *[int.from_bytes(data[i:i + 2], 'little') for i in (4, 6, 8)]) \
            if len(data) >= 12 else (0, 0, 0, 0, 0)
        if magic != 0x19 or cf != CF[a['cf']] or (w, h) != (a['w'], a['h']) or \
                stride != w * BPP[cf] or len(data) != 12 + stride * h or f'{zlib.crc32(data[12:]):08x}' != a['crc32']:
            bad.append(f'{a["file"]}: not the committed {a["w"]}x{a["h"]} {a["cf"]} image with pixel CRC {a["crc32"]} '
                       f'({len(data)} bytes, pixel CRC {zlib.crc32(data[12:]):08x})')
        continue
    if a['cf'] == 'RAW_TEX':
        if len(data) != a['w'] * a['h'] * 4: bad.append(f'{a["file"]}: {len(data)} bytes, expected {a["w"] * a["h"] * 4}')
        continue
    if a['cf'] == 'STRIP':   # "ORS1", u16 n_theta, u8 r_min, u8 n_r, u8 level, u8 0, u16 stride, 4 zero; rgb565, a8
        nt, nr, st = (u16(data, 4), data[7], u16(data, 10)) if len(data) >= 16 else (0, 0, 0)
        if data[:4] != b'ORS1' or (nt, nr, st) != (a['w'], a['h'], a['stride']) or nt & (nt - 1) or \
                data[8] != a['level'] or len(data) != 16 + st * nr * 3:
            bad.append(f'{a["file"]}: not an ORS1 strip of {a["w"]} (stride {a["stride"]}) x {a["h"]} level {a["level"]} '
                       f'({len(data)} bytes)')
        continue
    if a['cf'] == 'PMAP':    # "OPM1", u16 version, u16 n_theta, u32 nspans, npx, code bytes, file size (ring.h)
        if len(data) < 72 or data[:4] != b'OPM1' or u16(data, 6) != a['n_theta'] or \
                int.from_bytes(data[20:24], 'little') != len(data) or \
                len(data) != 72 + 20 * int.from_bytes(data[8:12], 'little') + int.from_bytes(data[16:20], 'little'):
            bad.append(f'{a["file"]}: not an OPM1 dial map at N {a["n_theta"]} ({len(data)} bytes)')
        continue
    if a['cf'] == 'TLUT':  # "OTL1", u16 box, u8 r0, u8 r1, u32 n, u32 0; idx u32[n], ang u16[n], rq u8[n] (tmr_ring.h)
        npx, h = (int.from_bytes(data[8:12], 'little') if len(data) >= 16 else -1), 1469598103934665603
        for b in data[16:]: h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
        if data[:4] != b'OTL1' or u16(data, 4) != a['w'] or a['w'] != a['h'] or npx != a.get('n') or \
                len(data) != 16 + 7 * npx or f'{h:016x}' != a.get('digest'):
            bad.append(f'{a["file"]}: not the OTL1 timer LUT of {a.get("n")} px for a {a["w"]} box with digest '
                       f'{a.get("digest")} ({len(data)} bytes, payload digest {h:016x})')
        continue
    if a['cf'] == 'RLE':
        if len(data) < 8 or rle_size(data) != len(data): bad.append(f'{a["file"]}: malformed RLE frame')
        continue
    if a['cf'] == 'PCM':   # .get: a hand-added entry without the fields is reported, not a traceback (T13 M6)
        ns, rate, ch = a.get('samples'), a.get('rate'), a.get('channels')
        if len(data) % 2 or len(data) > 3 * 32000 or not isinstance(ns, int) or len(data) != 2 * ns or rate != 16000 \
                or ch != 1:
            bad.append(f'{a["file"]}: {len(data)} bytes of PCM, manifest samples {ns} rate {rate} channels {ch} '
                       f'(want <= 96000 even bytes = 2 x samples, s16le mono 16 kHz)')
        continue
    magic, cf, w, h, stride = data[0], data[1], *[int.from_bytes(data[i:i + 2], 'little') for i in (4, 6, 8)]
    if magic != 0x19 or cf != CF[a['cf']] or (w, h) != (a['w'], a['h']) or stride != w * BPP[cf]:
        bad.append(f'{a["file"]}: header magic {magic:#x} cf {cf:#x} {w}x{h} stride {stride}'); continue
    want = 12 + stride * h + (w * h if cf == 0x14 else 0)
    if len(data) != want:
        bad.append(f'{a["file"]}: {len(data)} bytes, expected {want}'); continue
    pw, ph, rows, _ = png.Reader(filename=os.path.join(out, a['name'] + '.png')).asRGBA8()
    if (pw, ph) != (w, h):
        bad.append(f'{a["file"]}: PNG is {pw}x{ph}'); continue
    px, errs = data[12:], 0
    for y, row in enumerate(rows):
        for x in range(w):
            r, g, b, al = row[4 * x:4 * x + 4]
            if cf == 0x10:
                i = y * stride + 4 * x
                ok = tuple(px[i:i + 4]) == (b, g, r, al)                     # exact, BGRA in memory
            elif cf == 0x0e:
                ok = px[y * stride + x] == al                                    # A8: the alpha exact
            else:
                i = y * stride + 2 * x
                v = px[i] | px[i + 1] << 8
                rv = ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255
                if cf == 0x12:   # RGB565: exactly the rounded levels (R86), opaque by construction
                    ok = al == 255 and v == rv
                else:            # RGB565A8: the colour exactly the rounded levels (Task 7), the alpha exact
                    ok = v == rv and px[stride * h + y * w + x] == al
            errs += not ok
    if errs: bad.append(f'{a["file"]}: {errs} pixels differ from {a["name"]}.png')
listed = {a['file'] for a in man['assets']} | {'manifest.json'}
for f in sorted(set(os.listdir(assets)) - listed): bad.append(f'{f}: not in manifest.json')
for line in bad: print('ASSETS BAD', line)
if bad: sys.exit(1)
print(f'ASSETS OK {n}')
EOF
}

# Step 4b (R69): everything in firmware/apps/omni/assets must fit the "assets" partition in firmware/common/partitions.csv, less 256 KiB of
# headroom. The packer adds a 32-byte header and a table entry per file (name, size, offset: counted generously as 64 B).
budget() {
    "$PY" - "$ASSETS" "$ROOT/firmware/common/partitions.csv" <<'EOF2'
import os, sys
assets, csv = sys.argv[1:3]
size = next(int(c[4].strip(), 0) for c in (l.split(',') for l in open(csv) if not l.startswith('#')) if c[0].strip() == 'assets')
limit = size - 256 * 1024
files = os.listdir(assets)
used = 32 + sum(64 + os.path.getsize(os.path.join(assets, f)) for f in files)
print(f'  assets partition {size} bytes, budget {limit} (partition - 256 KiB): total {used} bytes, {limit - used} left')
if used > limit:
    print(f'ASSETS BAD: {used} bytes exceed the {limit}-byte budget (assets partition {size} - 256 KiB)', file=sys.stderr); sys.exit(1)
EOF2
}

# Step 7: the committed identity list (paths relative to the repo root, sha256sum's format, sorted).
IDLIST=$ROOT/tools/identity.sha256
identity_files() {
    (cd "$ROOT" && find firmware/apps/omni/assets firmware/components/watch_ui/fonts firmware/apps/omni/main/ui/fonts \
        -maxdepth 1 -type f \( -path 'firmware/apps/omni/assets/*' -o -name 'font_*.c' \) | LC_ALL=C sort)
}
identity() {
    [ -f "$IDLIST" ] || { echo "IDENTITY BAD: no $IDLIST (tools/assets.sh --rehash writes it)" >&2; return 1; }
    local bad=0 listed have
    listed=$(awk '{print $2}' "$IDLIST" | LC_ALL=C sort)
    have=$(identity_files)
    while read -r f; do [ -n "$f" ] && { echo "IDENTITY BAD $f: not in tools/identity.sha256"; bad=1; }; done \
        < <(LC_ALL=C comm -13 <(echo "$listed") <(echo "$have"))
    while read -r f; do [ -n "$f" ] && { echo "IDENTITY BAD $f: listed, missing"; bad=1; }; done \
        < <(LC_ALL=C comm -23 <(echo "$listed") <(echo "$have"))
    while read -r line; do echo "IDENTITY BAD $line"; bad=1; done \
        < <(cd "$ROOT" && sha256sum --quiet -c "$IDLIST" 2>/dev/null | grep -v '^sha256sum:' || true)
    [ "$bad" = 0 ] || { echo "IDENTITY BAD: bytes changed; if meant, tools/assets.sh --rehash and review the diff" >&2; return 1; }
    echo "IDENTITY OK $(wc -l < "$IDLIST") files"
}

MODE= SFX=keep BG=keep
for a in "$@"; do
    case "$a" in
        --check) MODE=check ;;
        --identity) MODE=identity ;;
        --rehash) MODE=rehash ;;
        --sfx) SFX=regen ;;
        --bg) BG=regen ;;
        *) echo "usage: tools/assets.sh [--check] [--sfx] [--bg] [--identity] [--rehash]" >&2; exit 2 ;;
    esac
done
if [ "$MODE" = rehash ]; then
    identity_files | (cd "$ROOT" && xargs sha256sum) > "$IDLIST"
    identity
    exit
fi
if [ "$MODE" = identity ]; then
    identity
    exit
fi
venv
if [ "$MODE" = check ]; then
    [ -f "$OUT/manifest.json" ] || { echo "no $OUT: run tools/assets.sh first" >&2; exit 1; }
    budget
    "$PY" "$ROOT/tools/gen_sfx.py" compare "$ASSETS"
    check
    identity
    exit
fi

echo "== 1. npm ci";        npm --prefix "$ROOT/tools" ci --silent
echo "== 2. export";        CHROME=${CHROME:-/usr/bin/google-chrome} node "$ROOT/tools/export_layers.mjs"
echo "== 3. textures"
KEEP=$(mktemp -d); trap 'rm -rf "$KEEP"' EXIT
cp "$ASSETS"/sfx_*.pcm "$KEEP"/ 2>/dev/null || true   # the committed sounds (step 3c) survive the rm -rf
cp "$ASSETS"/bg_*.bin "$KEEP"/ 2>/dev/null || true    # and the committed backgrounds (step 3d), with their entries
cp "$ASSETS"/manifest.json "$KEEP"/manifest.json 2>/dev/null || true
rm -rf "$ASSETS"; node "$ROOT/tools/gen_tex.mjs"; node "$ROOT/tools/gen_tex.mjs" --grade omni
echo "== 3b. ringpack"
cmake -S "$ROOT/tools/ringpack" -B "$ROOT/tools/ringpack/build" >/dev/null
cmake --build "$ROOT/tools/ringpack/build" >/dev/null
"$ROOT/tools/ringpack/build/ringpack" "$OUT" "$ASSETS"
echo "== 3c. sounds ($SFX)"
if [ "$SFX" = regen ]; then
    "$PY" "$ROOT/tools/gen_sfx.py" write "$ASSETS"
else
    cp "$KEEP"/sfx_*.pcm "$ASSETS"/ 2>/dev/null || { echo "no committed sfx_*.pcm: run tools/assets.sh --sfx" >&2; exit 1; }
    # an effect in gen_sfx.py's EFFECTS with no committed file yet (Stage 5's sfx_alarm) is synthesised alone: the
    # committed ones are never rewritten
    MISSING=$("$PY" -B -c 'import os, sys; sys.path.insert(0, sys.argv[1]); import gen_sfx
print(" ".join(n for n, _ in gen_sfx.EFFECTS if not os.path.isfile(os.path.join(sys.argv[2], n + ".pcm"))))' \
        "$ROOT/tools" "$ASSETS")
    if [ -n "$MISSING" ]; then
        # shellcheck disable=SC2086   # the names are word-split on purpose
        "$PY" "$ROOT/tools/gen_sfx.py" write "$ASSETS" --only $MISSING
    fi
    "$PY" "$ROOT/tools/gen_sfx.py" compare "$ASSETS"
fi
"$PY" "$ROOT/tools/gen_sfx.py" manifest "$ASSETS" "$OUT/sfx.json"
echo "== 3d. backgrounds ($BG)"
cmake -S "$ROOT/tools/bgpack" -B "$ROOT/tools/bgpack/build" >/dev/null
cmake --build "$ROOT/tools/bgpack/build" >/dev/null
mkdir -p "$OUT/bg"; "$ROOT/tools/bgpack/build/bgpack" "$OUT/bg" >/dev/null
if [ "$BG" = regen ]; then
    cp "$OUT"/bg/bg_*.bin "$ASSETS"/
else
    cp "$KEEP"/bg_*.bin "$ASSETS"/ 2>/dev/null || { echo "no committed bg_*.bin: run tools/assets.sh --bg" >&2; exit 1; }
fi
"$PY" - "$ASSETS" "$OUT/bg" "$KEEP/manifest.json" "$OUT/bg.json" "$BG" <<'EOF5'
import json, os, struct, sys, zlib
assets, packed, old_man, out, mode = sys.argv[1:6]
# R115: the Stage 3 device's pixel CRCs, the default when the committed manifest has no entry yet
R115 = {'bg_bezel.bin': '5cfc94d2', 'bg_picker.bin': '9fe68698', 'bg_settings.bin': '9ec99c26'}
old = {a['file']: a for a in json.load(open(old_man))['assets']} if os.path.isfile(old_man) else {}
entries = []
for f in ('bg_bezel.bin', 'bg_picker.bin', 'bg_settings.bin'):
    data, ref = open(os.path.join(assets, f), 'rb').read(), open(os.path.join(packed, f), 'rb').read()
    crc = f'{zlib.crc32(data[12:]):08x}'
    if mode == 'regen':
        src = 'bgpack'
        print(f'  {f}: bgpack output, pixel CRC {crc} (--bg: a change of bytes, R115)')
    else:
        want = old.get(f, {}).get('crc32', R115[f])
        src = old.get(f, {}).get('src', 'capture')
        if crc != want: sys.exit(f'{f}: pixel CRC {crc}, the committed entry says {want}')
        if data == ref:
            print(f'  {f}: matches bgpack ({src}, pixel CRC {crc})')
        else:
            n = len(data) // 2 - 6   # both are 466 x 466 RGB565 after the 12-byte header (bgpack, and crc checked)
            a, b = struct.unpack(f'<{n}H', data[12:]), struct.unpack(f'<{n}H', ref[12:])
            diff = [(x, y) for x, y in zip(a, b) if x != y]
            lvl = max((max(abs((x >> 11) - (y >> 11)), abs((x >> 5 & 63) - (y >> 5 & 63)), abs((x & 31) - (y & 31)))
                       for x, y in diff), default=0)
            print(f'  WARN {f}: bgpack makes other bytes ({len(diff)} px differ, by up to {lvl} level(s); this host\'s libm '
                  f'and FMA are not the device\'s): the committed {src} bytes are kept (R115)')
    entries.append({'file': f, 'name': f[:-4], 'cf': 'RGB565', 'w': 466, 'h': 466, 'src': src, 'crc32': crc})
json.dump(entries, open(out, 'w'), indent=1)
EOF5
echo "== 3e. the keyboard's shared assets"
for f in kb_lay.bin kb_mono21.bin kb_mono42.bin kb_icons.bin kb_tick.pcm; do
    cp "$ROOT/firmware/components/watch_ui/assets/$f" "$ASSETS/$f"
done
"$PY" "$ROOT/tools/kb_assets.py" manifest --out "$OUT/kb.json"
echo "== 4. LVGLImage.py"
"$PY" - "$OUT/manifest.json" "$OUT/ringpack.json" "$OUT/sfx.json" "$OUT/bg.json" "$OUT/kb.json" "$ASSETS/manifest.json" <<'EOF3'
import json, sys
man = json.load(open(sys.argv[1]))
man['assets'] += json.load(open(sys.argv[2]))   # ringpack's strips (step 3b)
man['assets'] += json.load(open(sys.argv[3]))   # the sounds (step 3c)
man['assets'] += json.load(open(sys.argv[4]))   # the backgrounds (step 3d)
man['assets'] += json.load(open(sys.argv[5]))   # the keyboard's shared assets (step 3e)
open(sys.argv[6], 'w').write(json.dumps(man, indent=1) + '\n')
EOF3
"$PY" -c 'import json,sys; [print(a["name"], a["cf"]) for a in json.load(open(sys.argv[1]))["assets"] if a["cf"] in ("ARGB8888", "RGB565", "RGB565A8", "A8") and "crc32" not in a]' \
    "$ASSETS/manifest.json" | while read -r name cf; do
    if [ "$cf" = RGB565 ] || [ "$cf" = RGB565A8 ]; then   # R86: rounded, the LVGL v9 header LVGLImage writes
        "$PY" - "$OUT/$name.png" "$ASSETS/$name.bin" "$cf" <<'EOF4'
import struct, sys, png
w, h, rows, _ = png.Reader(filename=sys.argv[1]).asRGBA8()
a8 = sys.argv[3] == 'RGB565A8'
out = bytearray(struct.pack('<BBHHHHH', 0x19, 0x14 if a8 else 0x12, 0, w, h, w * 2, 0))   # magic cf flags w h stride 0
alpha = bytearray()
for row in rows:
    for x in range(w):
        r, g, b, a = row[4 * x:4 * x + 4]
        if a != 255 and not a8: sys.exit(f'{sys.argv[1]}: RGB565 must be opaque ({x}: alpha {a})')
        out += struct.pack('<H', ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255)
        alpha.append(a)
open(sys.argv[2], 'wb').write(out + (alpha if a8 else b''))
EOF4
    else
        "$PY" "$LVGLIMAGE" --ofmt BIN --cf "$cf" --compress NONE --name "$name" -o "$ASSETS" "$OUT/$name.png" >/dev/null
    fi
    echo "  $name.bin $cf"
done
echo "== 4b. partition budget"; budget
echo "== 5. assets_meta.h"
"$PY" - "$ASSETS/manifest.json" "$META" "$GLOBE_TEX" <<'EOF'
import json, sys
man, meta, tex = json.load(open(sys.argv[1])), sys.argv[2], sys.argv[3]
L = ['/* generated by tools/assets.sh from firmware/apps/omni/assets/manifest.json -- do not edit */',
     '/* Geometry of the assets in the "assets" partition: sizes, on-screen box origins (_X/_Y, the image\'s top-left',
     ' * at rest), pivots (_PX/_PY, the image pixel that sits on the rotation or anchor point) and shadow offsets. */',
     '#pragma once', '']
for a in man['assets']:
    N = 'ASSET_' + a['name'].upper()
    L.append(f'#define {N} "{a["file"]}"')
    if a['cf'] == 'PCM':   # a sound: its length (s16le mono at 16 kHz)
        L += [f'#define {N}_SAMPLES {a["samples"]}', '']
        continue
    if a['cf'] == 'RAW':   # a shared file with no image geometry (step 3e: the layout, the fonts)
        L.append('')
        continue
    L.append(f'#define {N}_W {a["w"]}')
    L.append(f'#define {N}_H {a["h"]}')
    for k, sfx in (('origin', ('X', 'Y')), ('pivot', ('PX', 'PY')), ('offset', ('DX', 'DY'))):
        if k in a:
            L.append(f'#define {N}_{sfx[0]} {a[k][0]}')
            L.append(f'#define {N}_{sfx[1]} {a[k][1]}')
    L.append('')
t = next(a for a in man['assets'] if a['file'] == tex)
L += ['/* The globe texture the faces load, chosen by Task 5\'s PSRAM budget (plan D2, R48/C7; tools/assets.sh). */',
      f'#define ASSET_GLOBE_TEX "{tex}"', f'#define ASSET_GLOBE_TEX_W {t["w"]}', f'#define ASSET_GLOBE_TEX_H {t["h"]}', '']
open(meta, 'w').write('\n'.join(L))
print(f'  {meta.split("/firmware/")[1]}: {len(man["assets"])} assets, globe texture {tex}')
EOF
echo "== 6. round trip";    check
echo "== 7. identity";      identity
