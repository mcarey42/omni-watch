#!/usr/bin/env bash
# The public export (public release Task 14): a ref of this repo, minus tools/public_exclude.txt's paths, checked by
# the release guard, as a fresh repo with one commit. It never creates a remote and never pushes: the publish command
# is printed for the owner to run.
#
#   tools/make_public.sh --terms FILE [--overlay ART] [--ref REF] OUTDIR
#   env (required): PUBLIC_AUTHOR_NAME, PUBLIC_AUTHOR_EMAIL, PUBLIC_REPO
#
#   1. Refused (exit 2) before anything is written: a missing env value or --terms file, a ref that does not resolve,
#      an OUTDIR that is not empty or lies inside any checkout of this repo (this one, the main checkout, any sibling
#      worktree, the git common dir).
#   2. The exclude list is read from the ref (git show REF:tools/public_exclude.txt), never from the working tree, and
#      `git archive REF` minus those globs is unpacked into OUTDIR.
#   3. OUTDIR/PUBLIC_TREE marks the tree as an export.
#   4. The tree's own tools/release_check.py runs on it with the private terms file (and the overlay); a failure stops
#      here and the tree stays for inspection.
#   5. git init (branch main), add -A, a check that every extracted file is in the index (no .gitignore drops one),
#      and one commit "Initial public release" by PUBLIC_AUTHOR_NAME <PUBLIC_AUTHOR_EMAIL>, dated now at +0000, with
#      no global or system git config; the new repo has no remote. Every inherited GIT_* variable is unset at the
#      start, so none steers the export either.
#   6. The publish command is printed, not run.
set -euo pipefail
# no inherited GIT_* variable (GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE, GIT_CONFIG_*, an identity) steers any git call here
while IFS= read -r v; do unset "$v"; done < <(compgen -e | grep '^GIT_' || true)

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
refuse() { echo "make_public: refused: $*" >&2; exit 2; }
usage="usage: tools/make_public.sh --terms FILE [--overlay ART] [--ref REF] OUTDIR"

TERMS='' ART='' REF=main OUT=''
while [ $# -gt 0 ]; do
    case "$1" in
        --terms) [ $# -ge 2 ] || refuse "$usage"; TERMS=$2; shift ;;
        --overlay) [ $# -ge 2 ] || refuse "$usage"; ART=$2; shift ;;
        --ref) [ $# -ge 2 ] || refuse "$usage"; REF=$2; shift ;;
        -*) refuse "$usage" ;;
        *) [ -z "$OUT" ] || refuse "$usage"; OUT=$1 ;;
    esac
    shift
done
[ -n "$OUT" ] || refuse "$usage"
[ -n "$TERMS" ] || refuse "no --terms: the guard needs the private terms file"
[ -f "$TERMS" ] || refuse "the terms file $TERMS is missing"
TERMS=$(realpath "$TERMS")
if [ -n "$ART" ]; then
    [ -d "$ART" ] || refuse "the overlay $ART is missing"
    ART=$(cd "$ART" && pwd -P)
fi
for v in PUBLIC_AUTHOR_NAME PUBLIC_AUTHOR_EMAIL PUBLIC_REPO; do
    [ -n "${!v:-}" ] || refuse "$v is not set"
done
COMMIT=$(git -C "$ROOT" rev-parse --verify --quiet "$REF^{commit}") || refuse "the ref $REF does not resolve"

# OUTDIR: empty or absent, and outside every checkout of this repo (resolved physically, missing parts included)
OUT=$(realpath -m "$OUT")
if [ -e "$OUT" ] || [ -L "$OUT" ]; then
    [ -d "$OUT" ] && [ ! -L "$OUT" ] || refuse "$OUT exists and is not a directory"
    [ -z "$(ls -A "$OUT")" ] || refuse "$OUT is not empty"
fi
repo_roots() {
    git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir
    git -C "$ROOT" worktree list --porcelain | sed -n 's/^worktree //p'
    echo "$ROOT"
}
while read -r r; do
    [ -n "$r" ] && [ -d "$r" ] || continue
    r=$(cd "$r" && pwd -P)
    case "$OUT/" in "$r"/*) refuse "$OUT lies inside a checkout of this repo ($r)" ;; esac
done < <(repo_roots)
case "$TERMS" in "$OUT"/*) refuse "the terms file lies inside $OUT" ;; esac

# the exclude list, from the ref
EXCL=$(git -C "$ROOT" show "$COMMIT:tools/public_exclude.txt") || refuse "$REF has no tools/public_exclude.txt"
SPECS=()
while IFS= read -r line; do
    line=$(printf '%s' "$line" | sed -E 's/(^|[[:space:]]+)#.*$//; s/^[[:space:]]+//; s/[[:space:]]+$//')
    [ -n "$line" ] || continue
    case "$line" in :*|!*|/*) refuse "tools/public_exclude.txt: $line is not a plain glob" ;; esac
    SPECS+=(":(exclude,glob)$line")
done <<< "$EXCL"

mkdir -p "$OUT"
git -C "$ROOT" archive --format=tar "$COMMIT" -- . "${SPECS[@]}" | tar -x -C "$OUT"
NFILES=$(find "$OUT" \( -type f -o -type l \) | wc -l)
printf 'Exported by tools/make_public.sh. Term scans that need private lists are skipped here.\n' > "$OUT/PUBLIC_TREE"
echo "make_public: $REF ($COMMIT) exported to $OUT: $NFILES files, ${#SPECS[@]} exclude globs"

GUARD=(python3 -B "$OUT/tools/release_check.py" --terms "$TERMS")
[ -z "$ART" ] || GUARD+=(--overlay "$ART")
if ! PYTHONDONTWRITEBYTECODE=1 "${GUARD[@]}" "$OUT"; then
    echo "make_public: the release guard failed; $OUT is left for inspection (no repo was made)" >&2
    exit 1
fi

# a fresh repo, with only the public identity and no user or system config (no template, encoding or excludes)
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
NOW="$(date -u +%s) +0000"
export GIT_AUTHOR_NAME=$PUBLIC_AUTHOR_NAME GIT_COMMITTER_NAME=$PUBLIC_AUTHOR_NAME
export GIT_AUTHOR_EMAIL=$PUBLIC_AUTHOR_EMAIL GIT_COMMITTER_EMAIL=$PUBLIC_AUTHOR_EMAIL
export GIT_AUTHOR_DATE=$NOW GIT_COMMITTER_DATE=$NOW
G=(git -C "$OUT" -c core.hooksPath=/dev/null -c commit.gpgsign=false -c core.autocrlf=false -c core.safecrlf=false)
"${G[@]}" init -q -b main
"${G[@]}" add -A
INDEXED=$("${G[@]}" ls-files | wc -l)
WANT=$((NFILES + 1))
if [ "$INDEXED" -ne "$WANT" ]; then
    echo "make_public: file count: $INDEXED in the index, $WANT extracted (with PUBLIC_TREE): a .gitignore in the" \
         "tree drops a file; no commit made" >&2
    exit 1
fi
"${G[@]}" commit -q -m "Initial public release"
[ -z "$("${G[@]}" remote)" ] || { echo "make_public: the new repo has a remote" >&2; exit 1; }
echo "make_public: $OUT holds one commit ($("${G[@]}" rev-parse --short HEAD)), $INDEXED files, no remote."
echo "To publish (not run here):"
echo "  gh repo create $PUBLIC_REPO --public --source $OUT --push"
