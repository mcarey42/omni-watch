#!/usr/bin/env bash
# Build the dw assets (firmware/apps/dw/assets/, committed) from the designers' mockups, read in place, and our
# own generic art (dw wave 1 Task 9, WD11, WD12; tools/assets.sh stays the Omni's and untouched).
#   1. tools/dw_export.mjs: headless Chrome draws every row of tools/export/dw_layers.json -> tools/export/out-dw/
#      (*.png, layers.json); two fresh runs must agree
#   1b. tools/dw_vortex.py gen: the generated rows (out gen, wave 2 Task 13): the Vortex textures' PNGs (three palettes,
#      Task 14) and its table (dw_v12_lut.dat) into tools/export/out-dw, deterministic (integer and basic IEEE
#      operations only)
#   2. tools/dw_pack.py pack: each row packed (RGB565 levels rounded as R86; the three backgrounds' rows carry crc32:
#      they stay the committed bytes, checked by pixel CRC); firmware/apps/dw/assets/manifest.json
#   3. tools/gen_sfx_dw.py manifest: the four sounds' PCM entries join the manifest (the committed PCM is kept;
#      gen_sfx_dw.py compare warns if this host would synthesise other bytes)
#   4. the budget: fail above 12 255 232 B (R69: the assets partition less 256 KiB), warn above 11 600 000 B (the
#      wave-2 line with Vortex, wave 2 Task 14; 10 900 000 B before it; dw_pack.py budget's defaults); counted as tools/assets.sh counts (32 B + 64 B per
#      file + the files)
#   5. firmware/apps/dw/components/dw_core/include/dw/assets_meta.h: ASSET_DW_* names and sizes, the #5 pane and lens
#      geometry, the atlases' cells (B8: dw_core cannot include an app header; the app includes it from there)
#   6. the round trip: every bin decoded against its PNG; every manifest file present and nothing else in the dir
#
# --overlay ART (the private art overlay's clone, R132): steps 1, 2 and 6 for the overlay's own rows (ART/layers.json,
#   named by ART/overlay.json), writing only ART/out/ (the intermediates, ignored there) and ART/assets/. Each overlay
#   file must equal its generic namesake in name, cf, width and height. The publishability guard (below) runs last.
# --check: steps 4 and 6 (6 needs a previous full run's tools/export/out-dw), gen_sfx_dw.py compare (a warning),
#   dw_vortex.py compare (a failure: the generated table and textures regenerated and compared byte for byte), and
#   the re-render check (G1): dw_generic.js is rendered again (two runs, which must agree) and the committed
#   dw_doors_m.bin, dw_b5_box.bin and dw_doors_c.bin (wave 2 Task 4) must equal it byte for byte.
# --check --overlay ART: all of that, the overlay's step 6, and the publishability guard (G1). Run it before every
#   push of either repo (Global Constraints, R133, R136 item 2).
# --guard --overlay ART: the publishability guard alone.
# The guard: for every file in the overlay (recursively, its .git excepted),
#   `git -C <main> cat-file -e $(git -C <main> hash-object --no-filters <file>)` must say "absent", so no overlay file's
#   blob is anywhere in the main repo's object store: any ref, any history. It fails closed by construction: it refuses
#   unless `git fsck --full --strict --no-dangling` passes; it refuses a shallow repo, a partial (promisor) clone, any
#   alternates (an objects/info/alternates file, or GIT_ALTERNATE_OBJECT_DIRECTORIES, GIT_OBJECT_DIRECTORY, GIT_DIR or
#   GIT_COMMON_DIR in the environment); every object-store entry must be readable; HEAD and its tree must be found
#   before and after; any git call that fails, or exits 1 with anything on stderr, refuses; a symlink anywhere in the
#   overlay refuses. Only `cat-file -e` exiting 1 with an empty stderr counts as absent.
#   The one exception (public release Task 9, plan C3, spec section 4.1): the blobs in OVERLAY_TRACKED_BLOBS below,
#   exactly, by full id: today's chime and the two thumbnails (dw_chime.pcm, dw_th_b5.bin, dw_th_d3.bin), tracked in
#   the main repo since their waves, which the private overlay now carries byte for byte so the private build keeps
#   them. Such a file prints `GUARD SHARED <file> (tracked before the overlay carried it; §4.1)`, not BAD. The list
#   is a constant of this file, as rd_assets.sh's SHARED_BLOBS is (R160): no environment variable or argument
#   extends it. Any other blob found stays BAD.
#   ITS LIMIT: it proves only that no byte-identical whole file of the overlay is in the main repo. Re-encoded,
#   cropped or re-exported art, art embedded in a larger file, or a filtered or LFS commit is not something it can see;
#   those are covered by the visual review (the owner's pre-public look) and check_apps built --publishable (the
#   generic image equals the committed generic files, and the re-render proves those are dw_generic.js's drawing).
# The overlay, and its assets/ and out/ resolved physically, must live outside every checkout of this repo (this
#   worktree, the main checkout, sibling worktrees) and its git common dir; assets/ and out/ must not be symlinks (N1).
# Environment (the tests' scratch copies): DW_ASSETS_DIR (default firmware/apps/dw/assets), DW_OUT (default
#   tools/export/out-dw), DW_MAIN_REPO (default this repo), DW_DESIGN (tools/dw_export.mjs), DW_FACE_ALIAS
#   (tools/dw_export.mjs: table face ids to mockup face ids, a JSON object or the path of one; the private build's map
#   is the overlay's export/face_alias.json), CHROME.
# Needs: node, python3, Chrome, npm --prefix tools ci, tools/fonts/ (tools/fonts.sh).
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1   # gen_sfx_dw.py imports gen_sfx: no tools/__pycache__ left behind

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ASSETS=${DW_ASSETS_DIR:-$ROOT/firmware/apps/dw/assets}
OUT=${DW_OUT:-$ROOT/tools/export/out-dw}
MAIN=${DW_MAIN_REPO:-$ROOT}
TABLE=$ROOT/tools/export/dw_layers.json
META=$ROOT/firmware/apps/dw/components/dw_core/include/dw/assets_meta.h
LVGLIMAGE=$ROOT/firmware/apps/dw/managed_components/lvgl__lvgl/scripts/LVGLImage.py
PY=${PYTHON:-python3}
PACK=("$PY" "$ROOT/tools/dw_pack.py")
# Public release Task 9 (C3, section 4.1): the only overlay blobs the main repo may hold, today's dw_chime.pcm,
# dw_th_b5.bin and dw_th_d3.bin, tracked since their waves (see the header). A constant: nothing extends it.
readonly OVERLAY_TRACKED_BLOBS="1528299e48303d5cc340eb22ec2e7262940aa979 344964b24d235a32b4d367b34c254de37c505452 5ead0bad58ad6b7e36b49c47724f23698dbbcc05"

MODE=full ART=
while [ $# -gt 0 ]; do
    case "$1" in
        --check) MODE=check ;;
        --guard) MODE=guard ;;
        --overlay) [ $# -ge 2 ] || { echo "--overlay needs the overlay's path" >&2; exit 2; }; ART=$(cd "$2" && pwd); shift ;;
        *) echo "usage: tools/dw_assets.sh [--check | --guard] [--overlay ART]" >&2; exit 2 ;;
    esac
    shift
done
# M4: the overlay lives outside every checkout of this repo: this worktree, the main checkout, any sibling worktree,
# and the git common dir itself (paths resolved physically).
repo_roots() {
    git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir
    git -C "$ROOT" worktree list --porcelain | sed -n 's/^worktree //p'
    echo "$ROOT"
}
if [ -n "$ART" ]; then
    ART=$(cd "$ART" && pwd -P)
    while read -r r; do
        [ -n "$r" ] && [ -d "$r" ] || continue
        r=$(cd "$r" && pwd -P)
        case "$ART/" in "$r"/*) echo "the overlay must not live inside this repo or any of its checkouts ($ART is under $r)" >&2; exit 2 ;; esac
    done < <(repo_roots)
    # N1: the overlay's own assets/ and out/ (where pack and export write) are no symlinks and resolve outside too.
    for sub in assets out; do
        [ -L "$ART/$sub" ] && { echo "the overlay's $sub/ must not be a symlink ($ART/$sub)" >&2; exit 2; }
        [ -e "$ART/$sub" ] || continue
        p=$(cd "$ART/$sub" && pwd -P)
        while read -r r; do
            [ -n "$r" ] && [ -d "$r" ] || continue
            r=$(cd "$r" && pwd -P)
            case "$p/" in "$r"/*) echo "the overlay's $sub/ resolves inside a checkout of this repo ($p is under $r)" >&2; exit 2 ;; esac
        done < <(repo_roots)
    done
    [ -f "$ART/overlay.json" ] || { echo "$ART has no overlay.json" >&2; exit 2; }
fi
[ -d "$ROOT/tools/node_modules/playwright-core" ] || npm --prefix "$ROOT/tools" ci --silent

BAD=0
fail() { echo "FAIL: $*"; BAD=1; }

budget() { "${PACK[@]}" budget --assets "$ASSETS" --partitions "$ROOT/firmware/common/partitions.csv" || fail "the budget"; }

guard() {   # the publishability guard (see the header): fails closed by construction
    local gerr gout rc n=0 shared=0 f h gbad=0 obj v
    gerr=$(mktemp) gout=$(mktemp)
    # g ARGS: git -C MAIN ARGS, stdout in $gout, stderr in $gerr, rc in $rc
    g() { rc=0; git -C "$MAIN" "$@" >"$gout" 2>"$gerr" || rc=$?; }
    # strict ARGS: g, and refuse unless rc 0 with an empty stderr
    strict() { g "$@"; [ "$rc" -eq 0 ] && [ ! -s "$gerr" ]; }
    refuse() { fail "guard: $* $(head -c 400 "$gerr" | tr '\n' ' ')"; rm -f "$gerr" "$gout"; }
    for v in GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_OBJECT_DIRECTORY GIT_DIR GIT_COMMON_DIR; do
        [ -z "${!v+x}" ] || { refuse "$v is set in the environment (unset it; the guard never trusts another store)"; return; }
    done
    strict rev-parse --git-dir || { refuse "$MAIN is not a git repository (rc $rc)"; return; }
    strict rev-parse --is-shallow-repository && [ "$(cat "$gout")" = false ] || { refuse "$MAIN is a shallow clone or unreadable"; return; }
    g config --get extensions.partialClone
    [ "$rc" -eq 1 ] && [ ! -s "$gerr" ] || { refuse "$MAIN is a partial clone (extensions.partialClone) or its config is unreadable"; return; }
    g config --get-regexp '^remote\..*\.promisor$'
    [ "$rc" -eq 1 ] && [ ! -s "$gerr" ] || { refuse "$MAIN has a promisor remote"; return; }
    strict rev-parse --path-format=absolute --git-path objects || { refuse "no object dir for $MAIN"; return; }
    obj=$(cat "$gout")
    [ -d "$obj" ] && [ -r "$obj" ] && [ -x "$obj" ] || { refuse "object store $obj is missing or unreadable"; return; }
    for f in "$obj/info/alternates" "$obj/info/http-alternates"; do
        [ ! -e "$f" ] && [ ! -L "$f" ] || { refuse "$MAIN borrows objects ($f): alternates are refused"; return; }
    done
    if [ -n "$(find "$obj/pack" -maxdepth 1 -name '*.promisor' -print -quit 2>/dev/null)" ]; then
        refuse "$MAIN has promisor packs (a partial clone)"; return
    fi
    v=$(find "$obj" \( -type d \( ! -readable -o ! -executable \) -o ! -type d ! -readable \) -print -quit 2>&1) \
        || { refuse "cannot read the object store $obj ($v)"; return; }
    [ -z "$v" ] || { refuse "unreadable entry in the object store: $v"; return; }
    # The object store is shared with the other worktrees, and another track's commit in progress can make one fsck
    # see an index's cache tree before its tree object lands ("missing tree", observed once). One retry; a pass is a
    # pass, two failures refuse.
    g fsck --full --strict --no-dangling --no-progress
    if [ "$rc" -ne 0 ]; then
        echo "  guard: fsck failed once (rc $rc: $(head -c 200 "$gout" | tr '\n' ' ')); retrying once"
        g fsck --full --strict --no-dangling --no-progress
    fi
    [ "$rc" -eq 0 ] || { refuse "git fsck --full --strict failed twice (rc $rc): $(head -c 300 "$gout" | tr '\n' ' ')"; return; }
    control() { strict cat-file -e HEAD && strict cat-file -e 'HEAD^{tree}'; }
    control || { refuse "git cannot read HEAD and its tree in $MAIN (the positive control, rc $rc)"; return; }
    v=$(find "$ART" -path "$ART/.git" -prune -o -type l -print -quit)
    [ -z "$v" ] || { refuse "a symlink in the overlay ($v): refused"; return; }
    while IFS= read -r -d '' f; do
        n=$((n + 1))
        strict hash-object --no-filters -- "$f" || { refuse "cannot hash $f (rc $rc)"; return; }
        h=$(cat "$gout")
        g cat-file -e "$h"
        if [ "$rc" -eq 0 ] && case " $OVERLAY_TRACKED_BLOBS " in *" $h "*) true ;; *) false ;; esac; then
            echo "  GUARD SHARED ${f#"$ART"/} (tracked before the overlay carried it; §4.1)"
            shared=$((shared + 1))
        elif [ "$rc" -eq 0 ]; then
            echo "GUARD BAD ${f#"$ART"/}: its blob $h exists in the main repo (a ref, its history or its object store)"
            BAD=1 gbad=1
        elif [ "$rc" -ne 1 ] || [ -s "$gerr" ]; then
            refuse "git error checking ${f#"$ART"/} (rc $rc)"; return
        fi
    done < <(find "$ART" -path "$ART/.git" -prune -o -type f -print0)
    control || { refuse "the positive control failed after the loop: the answers are not trusted (rc $rc)"; return; }
    rm -f "$gerr" "$gout"
    [ -n "$(find "$ART/assets" -type f -print -quit 2>/dev/null)" ] || { fail "guard: no overlay files under $ART/assets"; return; }
    if [ "$gbad" -eq 0 ] && [ "$shared" -eq 0 ]; then
        echo "GUARD OK: none of the overlay's $n files is a blob in $MAIN (fsck clean, no alternates, not shallow)"
    elif [ "$gbad" -eq 0 ]; then
        echo "GUARD OK: none of the overlay's $n files is a blob in $MAIN but the $shared allow-listed (fsck clean, no alternates, not shallow)"
    fi
}

rerender() {   # G1: the committed generic files are dw_generic.js's drawing, byte for byte
    local tmp
    tmp=$(mktemp -d)
    if node "$ROOT/tools/dw_export.mjs" --generic-only --out "$tmp" >/dev/null; then
        "${PACK[@]}" rerender --out "$tmp" --assets "$ASSETS" || BAD=1
    else
        fail "the generic re-render did not run (tools/dw_export.mjs --generic-only)"
    fi
    rm -rf "$tmp"
}

if [ "$MODE" = guard ]; then
    [ -n "$ART" ] || { echo "--guard needs --overlay ART" >&2; exit 2; }
    echo "== guard"; guard
    exit "$BAD"
fi
if [ "$MODE" = check ]; then
    [ -f "$OUT/layers.json" ] || { echo "no $OUT/layers.json: run tools/dw_assets.sh first" >&2; exit 1; }
    echo "== 4. budget";       budget
    echo "== sounds";          "$PY" "$ROOT/tools/gen_sfx_dw.py" compare "$ASSETS" || fail "gen_sfx_dw.py compare"
    echo "== generated";       "$PY" "$ROOT/tools/dw_vortex.py" compare "$ASSETS" || fail "dw_vortex.py compare"
    echo "== 6. round trip";   "${PACK[@]}" check --assets "$ASSETS" --out "$OUT" || BAD=1
    echo "== re-render";       rerender
    if [ -n "$ART" ]; then
        echo "== 6. overlay round trip"; "${PACK[@]}" check --overlay "$ART" --generic "$ASSETS" || BAD=1
        echo "== guard";         guard
    fi
    [ "$BAD" -eq 0 ] && echo "dw_assets --check: OK" || echo "dw_assets --check: FAILED"
    exit "$BAD"
fi

if [ -n "$ART" ]; then
    echo "== 1. export (overlay)"; node "$ROOT/tools/dw_export.mjs" --overlay "$ART"
    echo "== 2. pack (overlay)";   "${PACK[@]}" pack --overlay "$ART" --out "$ART/out" --generic "$ASSETS"
    echo "== 4. budget";           budget   # the overlay files equal their namesakes in size, so the staged set's
    echo "== 6. round trip";       "${PACK[@]}" check --overlay "$ART" --generic "$ASSETS" || BAD=1
    echo "== guard";               guard
    exit "$BAD"
fi

echo "== 1. export";        node "$ROOT/tools/dw_export.mjs" --out "$OUT"
echo "== 1b. generated";    "$PY" "$ROOT/tools/dw_vortex.py" gen --out "$OUT"
echo "== 2. pack";          "${PACK[@]}" pack --table "$TABLE" --out "$OUT" --assets "$ASSETS"
echo "== 3. sounds"
"$PY" "$ROOT/tools/gen_sfx_dw.py" compare "$ASSETS"
"$PY" "$ROOT/tools/gen_sfx_dw.py" manifest "$ASSETS" "$OUT/sfx.json"
"$PY" - "$ASSETS/manifest.json" "$OUT/sfx.json" <<'EOF'
import json, sys
man = json.load(open(sys.argv[1]))
man['assets'] += json.load(open(sys.argv[2]))
open(sys.argv[1], 'w').write(json.dumps(man, indent=1) + '\n')
EOF
VPY=$ROOT/tools/.venv-assets/bin/python   # tools/assets.sh's venv (pypng, lz4), when there
if [ -f "$LVGLIMAGE" ] && [ -x "$VPY" ] && "$VPY" -c 'import png, lz4' 2>/dev/null; then   # LVGL's own A8 layout
    tmp=$(mktemp -d)
    "$VPY" "$LVGLIMAGE" --ofmt BIN --cf A8 --compress NONE --name dw_glow -o "$tmp" "$OUT/dw_glow.png" >/dev/null
    cmp -s "$tmp/dw_glow.bin" "$ASSETS/dw_glow.bin" && echo "  dw_glow.bin: equal to LVGLImage.py's A8" \
        || fail "dw_glow.bin differs from LVGLImage.py's A8 output"
    rm -rf "$tmp"
fi
echo "== 4. budget";        budget
echo "== 5. assets_meta.h"; "${PACK[@]}" meta --assets "$ASSETS" --table "$TABLE" --header "$META"
echo "== 6. round trip";    "${PACK[@]}" check --assets "$ASSETS" --out "$OUT" || BAD=1
exit "$BAD"
