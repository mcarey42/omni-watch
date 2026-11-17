#!/usr/bin/env bash
# Build the Mining Ship assets (firmware/apps/rd/assets/, committed) from the designers' mockups, read in place
# (rd wave 1 Task 7; spec section 12.1: dw_assets.sh's steps):
#   1. tools/rd_export.mjs: headless Chrome draws every row of tools/export/rd_layers.json -> tools/export/out-rd/
#      (PNGs, glyph images, layers.json); two fresh runs must agree
#   2. tools/rd_pack.py pack: each row packed (the atlases by facepack.build_atlas, the two-plane ones as ATL2; the
#      brass backgrounds' rows carry crc32: they stay the committed bytes, checked by pixel CRC);
#      firmware/apps/rd/assets/manifest.json
#   3. tools/gen_sfx_rd.py manifest: the chime's two PCM files (rd_chime.pcm, the audible default voicing, and
#      rd_chime_r.pcm, the recipe as written; Task 8) asserted (length, peak, edges) and their entries joined to the
#      manifest. The .pcm files are committed: the generator only rewrites them on `gen_sfx_rd.py render`
#   4. the budget: fail above 9 000 000 B (at most the partition less 256 KiB, R69), warn above 7 000 000 B, report
#      above the wave-2 line of 6 000 000 B (wave 1's was 4 500 000 B), and fail any face whose own files (every file carrying its prefix,
#      rd_<prefix>_, listed in the table or not) pass 500 000 B; counted as tools/assets.sh
#      counts (32 B + 64 B per file + the files)
#   5. firmware/apps/rd/components/rd_core/include/rd/assets_meta.h: ASSET_* names and sizes, the atlases' cells
#   6. the round trip: every file decoded against its PNG; every manifest file present and nothing else in the dir
#
# --check: steps 4 and 6 (6 needs a previous full run's tools/export/out-rd), step 5 regenerated to a temporary file
#   and compared with the committed header (M5), and the sound compare (Task 8: gen_sfx_rd.py compare synthesises both
#   voicings in memory and WARNs, never fails, on a file this host does not reproduce byte for byte).
# --guard: the publishability guard (R133, R153; ruling R159), fail-closed. Run it before every push.
#   1. Every file under the design directory ($RD_DESIGN) and the research directory ($RD_RESEARCH), recursively:
#      `git cat-file -e $(git hash-object --no-filters <file>)` in this repo ($RD_MAIN_REPO) must say "absent", so no
#      design or research file's blob is anywhere in the object store: any ref, any history. dw's guard logic: it
#      refuses unless `git fsck --full --strict --no-dangling` passes (one retry, as dw's); it refuses a shallow repo,
#      a partial (promisor) clone, any alternates (an objects/info/alternates file, or GIT_ALTERNATE_OBJECT_DIRECTORIES,
#      GIT_OBJECT_DIRECTORY, GIT_DIR, GIT_COMMON_DIR, GIT_INDEX_FILE or GIT_WORK_TREE in the environment); every
#      object-store entry must be readable; HEAD and its tree must be found before and after; any git call that fails,
#      or exits 1 with anything on stderr, refuses; a missing or unreadable directory, an unreadable file, a find that
#      fails, or a symlink anywhere under either directory refuses. Only `cat-file -e` exiting 1 with an empty stderr
#      counts as absent.
#   2. What a push carries (R161): every blob of the HEAD tree, of the index, and of `git rev-list --objects
#      <merge base>..HEAD` (so a term committed and then removed, or committed and dropped from the index, is found),
#      text against tools/rd_terms.py's list, binaries outside any theme's assets image against its long terms.
#   3. The commit messages from the merge base with main to HEAD against the same list.
#   Passes 2 and 3 refuse (never pass) when git cannot answer, as when there is no merge base with main.
#   The list is private (public release Task 5): without tools/rd_terms.py the guard does not run. In a public tree (a
#   PUBLIC_TREE file at the root) it prints "no rd list (a public tree)" and exits 77; anywhere else "rd list missing:
#   tools/rd_terms.py" and exits 2 (refused, never a pass).
#   Fix round 2: every git call runs with replace objects off (--no-replace-objects, GIT_NO_REPLACE_OBJECTS=1), as
#   pack-objects does; a non-empty refs/replace/, an info/grafts file, git configuration from the environment
#   (GIT_CONFIG_COUNT, _KEY_*, _VALUE_*, _PARAMETERS, _GLOBAL, _SYSTEM, _NOSYSTEM), GIT_REPLACE_REF_BASE,
#   GIT_SHALLOW_FILE or GIT_NO_REPLACE_OBJECTS other than 1 each refuse.
#   The one exception to pass 1 (SHARED, not BAD; ruling R160): the blobs in SHARED_BLOBS below, exactly, by full id:
#   the Omni's two NASA earth textures (tracked at design/assets/ and design/mockups/assets/ since b58285a), which the rd
#   mockups' assets/ folder holds byte for byte. The list is a constant of this file: no environment variable or
#   argument extends it. Any other blob found stays BAD.
#   ITS LIMIT (as dw's): pass 1 proves only that no byte-identical whole file of either directory is in the repo.
#   Re-encoded, cropped or re-exported art, or art inside a larger file, is not something it can see; the text passes,
#   check_apps' built scans and the owner's review before any sharing (spec section 13.4) cover what they can.
# Environment (the tests' scratch repos): RD_MAIN_REPO (default this repo), RD_DESIGN and RD_RESEARCH (default
#   design/themes/rd and research/rd in the main checkout, by theme id (public release Task 6: the owner's checkout
#   links them to the packages); each root is resolved with `cd ... && pwd -P` before any find, so a symlinked root is
#   scanned through), RD_ASSETS_DIR (default firmware/apps/rd/assets), RD_OUT (default tools/export/out-rd), CHROME.
# Needs: node, python3, Chrome, npm --prefix tools ci, tools/fonts/ (tools/fonts.sh).
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ASSETS=${RD_ASSETS_DIR:-$ROOT/firmware/apps/rd/assets}
OUT=${RD_OUT:-$ROOT/tools/export/out-rd}
MAIN=${RD_MAIN_REPO:-$ROOT}
# R160: the only design-package blobs the repo may hold (the Omni's earth textures, byte for byte; see the header)
readonly SHARED_BLOBS="6f78f20bd45aff68aba95af47462ee3a3469ded4 e01c4d65c256d49d995d1073fe1e7b96a4c8c462"
TABLE=$ROOT/tools/export/rd_layers.json
META=$ROOT/firmware/apps/rd/components/rd_core/include/rd/assets_meta.h
PY=${PYTHON:-python3}
PACK=("$PY" "$ROOT/tools/rd_pack.py")
SFX=$ROOT/tools/gen_sfx_rd.py

MODE=full
while [ $# -gt 0 ]; do
    case "$1" in
        --check) MODE=check ;;
        --guard) MODE=guard ;;
        *) echo "usage: tools/rd_assets.sh [--check | --guard]" >&2; exit 2 ;;
    esac
    shift
done

BAD=0
fail() { echo "FAIL: $*"; BAD=1; }

theme_dir() {   # <design/themes | research>: the theme's directory under the main checkout, by theme id
    local common
    common=$(git --no-replace-objects -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || return 1
    echo "$(dirname "$common")/$1/rd"
}

guard() {   # the publishability guard (see the header): fails closed by construction
    local gerr gout ferr flist rc n=0 f h gbad=0 obj v root dirs=() d label shared=0
    gerr=$(mktemp) gout=$(mktemp) ferr=$(mktemp) flist=$(mktemp)
    g() { rc=0; git --no-replace-objects -C "$MAIN" "$@" >"$gout" 2>"$gerr" || rc=$?; }
    strict() { g "$@"; [ "$rc" -eq 0 ] && [ ! -s "$gerr" ]; }
    refuse() { fail "guard: $* $(head -c 400 "$gerr" | tr '\n' ' ')"; echo "GUARD REFUSED"; rm -f "$gerr" "$gout" "$ferr" "$flist"; }
    for v in GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_OBJECT_DIRECTORY GIT_DIR GIT_COMMON_DIR GIT_INDEX_FILE GIT_WORK_TREE \
             GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM GIT_CONFIG_NOSYSTEM \
             GIT_REPLACE_REF_BASE GIT_SHALLOW_FILE $(compgen -v | grep -E '^GIT_CONFIG_(KEY|VALUE)_' || true); do
        [ -z "${!v+x}" ] || { refuse "$v is set in the environment (unset it; the guard never trusts another store" \
                                     "or configuration from the environment)"; return; }
    done
    # fix round 2: pack-objects ignores replace refs and grafts, so every git call here does too (forced, and only
    # this value is accepted), and a repo holding either is refused outright
    [ -z "${GIT_NO_REPLACE_OBJECTS+x}" ] || [ "$GIT_NO_REPLACE_OBJECTS" = 1 ] \
        || { refuse "GIT_NO_REPLACE_OBJECTS is set to '$GIT_NO_REPLACE_OBJECTS' (the guard forces 1)"; return; }
    export GIT_NO_REPLACE_OBJECTS=1
    for label in RD_DESIGN RD_RESEARCH; do
        d=${!label:-}
        if [ -z "$d" ]; then
            d=$(theme_dir "$([ "$label" = RD_DESIGN ] && echo design/themes || echo research)") \
                || { refuse "$label is unset and this is no git checkout to derive it from"; return; }
        fi
        [ -d "$d" ] && [ -r "$d" ] && [ -x "$d" ] || { refuse "$label ($d) is missing or unreadable: nothing can be proven"; return; }
        dirs+=("$(cd "$d" && pwd -P)")
    done
    strict rev-parse --git-dir || { refuse "$MAIN is not a git repository (rc $rc)"; return; }
    strict rev-parse --is-shallow-repository && [ "$(cat "$gout")" = false ] || { refuse "$MAIN is a shallow clone or unreadable"; return; }
    g config --get extensions.partialClone
    [ "$rc" -eq 1 ] && [ ! -s "$gerr" ] || { refuse "$MAIN is a partial clone (extensions.partialClone) or its config is unreadable"; return; }
    g config --get-regexp '^remote\..*\.promisor$'
    [ "$rc" -eq 1 ] && [ ! -s "$gerr" ] || { refuse "$MAIN has a promisor remote (a partial clone)"; return; }
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
    # see an index's cache tree before its tree object lands (dw's guard observed it once). One retry; a pass is a
    # pass, two failures refuse.
    g fsck --full --strict --no-dangling --no-progress
    if [ "$rc" -ne 0 ]; then
        echo "  guard: fsck failed once (rc $rc: $(head -c 200 "$gout" | tr '\n' ' ')); retrying once"
        g fsck --full --strict --no-dangling --no-progress
    fi
    [ "$rc" -eq 0 ] || { refuse "git fsck --full --strict failed twice (rc $rc): $(head -c 300 "$gout" | tr '\n' ' ')"; return; }
    control() { strict cat-file -e HEAD && strict cat-file -e 'HEAD^{tree}'; }
    control || { refuse "git cannot read HEAD and its tree in $MAIN (the positive control, rc $rc)"; return; }
    strict for-each-ref --format='%(refname)' refs/replace/ || { refuse "cannot list refs/replace/ (rc $rc)"; return; }
    [ ! -s "$gout" ] || { refuse "$MAIN has replacement refs ($(head -c 200 "$gout" | tr '\n' ' ')): a push ignores" \
                                 "them, so the scan would read other bytes; refused"; return; }
    strict rev-parse --path-format=absolute --git-common-dir || { refuse "no common dir for $MAIN"; return; }
    v="$(cat "$gout")/info/grafts"
    [ ! -e "$v" ] && [ ! -L "$v" ] || { refuse "$v exists (grafts: a push ignores them); refused"; return; }
    for root in "${dirs[@]}"; do
        v=$(find "$root" -type l -print -quit 2>&1) || { refuse "cannot walk $root ($v)"; return; }
        [ -z "$v" ] || { refuse "a symlink under a guarded directory ($v): refused"; return; }
        : >"$flist"
        find "$root" -type f -print0 >"$flist" 2>"$ferr" && [ ! -s "$ferr" ] \
            || { refuse "cannot walk $root: $(head -c 300 "$ferr" | tr '\n' ' ')"; return; }
        while IFS= read -r -d '' f; do
            n=$((n + 1))
            [ -r "$f" ] || { refuse "cannot read $f"; return; }
            strict hash-object --no-filters -- "$f" || { refuse "cannot hash $f (rc $rc)"; return; }
            h=$(cat "$gout")
            g cat-file -e "$h"
            if [ "$rc" -eq 0 ] && case " $SHARED_BLOBS " in *" $h "*) true ;; *) false ;; esac; then
                echo "  GUARD SHARED ${f#"$root"/}: blob $h is on the R160 allow-list (the Omni's own earth texture)"
                shared=$((shared + 1))
            elif [ "$rc" -eq 0 ]; then
                echo "GUARD BAD ${f#"$root"/}: its blob $h exists in $MAIN (a ref, its history or its object store)"
                BAD=1 gbad=1
            elif [ "$rc" -ne 1 ] || [ -s "$gerr" ]; then
                refuse "git error checking ${f#"$root"/} (rc $rc)"; return
            fi
        done <"$flist"
    done
    control || { refuse "the positive control failed after the loop: the answers are not trusted (rc $rc)"; return; }
    [ "$n" -gt 0 ] || { refuse "no files under the guarded directories: nothing was proven"; return; }
    rm -f "$gerr" "$gout" "$ferr" "$flist"
    # R159, R161: what a push carries (HEAD tree, index, history since the merge base) and the commit messages
    for v in pushed messages; do
        rc=0
        "$PY" "$ROOT/tools/rd_terms.py" "--$v" "$MAIN" || rc=$?
        case $rc in
            0) ;;
            1) BAD=1 gbad=1 ;;
            *) fail "guard: the $v scan could not run (rc $rc)"; echo "GUARD REFUSED"; return ;;
        esac
    done
    [ "$gbad" -ne 0 ] || echo "GUARD OK: none of the $n files under the design and research directories is a blob in $MAIN" \
        "but the $shared on the R160 allow-list (fsck clean, no alternates, not shallow, not partial); no rd term in" \
        "what a push carries or in the commit messages"
}

budget() {
    # the wave-2 report line (rd wave 2 plan, Budgets): 6 000 000 B
    "${PACK[@]}" budget --assets "$ASSETS" --partitions "$ROOT/firmware/common/partitions.csv" --table "$TABLE" \
        --report 6000000 || fail "the budget"
}

if [ "$MODE" = guard ]; then
    if [ ! -f "$ROOT/tools/rd_terms.py" ]; then   # preflight M15: an explicit if, never a pass
        if [ -f "$ROOT/PUBLIC_TREE" ]; then
            echo "no rd list (a public tree)"
            exit 77
        fi
        echo "rd list missing: tools/rd_terms.py"
        exit 2
    fi
    echo "== guard"; guard
    exit "$BAD"
fi
[ -d "$ROOT/tools/node_modules/playwright-core" ] || [ "$MODE" = check ] || npm --prefix "$ROOT/tools" ci --silent
if [ "$MODE" = check ]; then
    [ -f "$OUT/layers.json" ] || { echo "no $OUT/layers.json: run tools/rd_assets.sh first" >&2; exit 1; }
    echo "== 4. budget";       budget
    echo "== sounds";          "$PY" "$SFX" compare "$ASSETS" || fail "gen_sfx_rd.py compare"
    echo "== 5. assets_meta.h (regenerated and compared)"
    tmeta=$(mktemp)
    if "${PACK[@]}" meta --assets "$ASSETS" --header "$tmeta" >/dev/null && cmp -s "$tmeta" "$META"; then
        echo "  ${META#"$ROOT"/} equals a fresh regeneration from the manifest"
    else
        fail "${META#"$ROOT"/} is not what the manifest regenerates (run tools/rd_assets.sh)"
        diff "$META" "$tmeta" | head -20 || true
    fi
    rm -f "$tmeta"
    echo "== 6. round trip";   "${PACK[@]}" check --assets "$ASSETS" --out "$OUT" || BAD=1
    [ "$BAD" -eq 0 ] && echo "rd_assets --check: OK" || echo "rd_assets --check: FAILED"
    exit "$BAD"
fi

echo "== 1. export";        node "$ROOT/tools/rd_export.mjs" --out "$OUT"
echo "== 2. pack and 3. sounds"
"$PY" "$SFX" manifest "$ASSETS" "$OUT/sfx.json"
"${PACK[@]}" pack --table "$TABLE" --out "$OUT" --assets "$ASSETS" --sfx "$OUT/sfx.json"
echo "  the sounds joined the manifest (tools/gen_sfx_rd.py)"
echo "== 4. budget";        budget
echo "== 5. assets_meta.h"; "${PACK[@]}" meta --assets "$ASSETS" --header "$META"
echo "== 6. round trip";    "${PACK[@]}" check --assets "$ASSETS" --out "$OUT" || BAD=1
exit "$BAD"
