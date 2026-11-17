#!/usr/bin/env bash
# Swap the watch to a theme's firmware, keeping NVS (Stage 4b Task 3).
#
#   tools/theme.sh <omni|dw|rd> [--port P] [--watch NAME] [--dry-run] [--no-build] [--build-dir D]
#
# 1. idf.py -C firmware/apps/<t> -B D build (unless --no-build; a failed build stops here: set -e). The directory
#    built is always the directory flashed.
# 2. tools/flash_keep_nvs.py --build D --label swap-<t>: app, assets and table, each skipped when verify_flash
#    matches (MD5 on the chip); NVS backed up first and compared before the one boot. --dry-run passes through
#    (no esptool call) and ends the swap there.
# 3. tools/bootline.py --expect "theme <t> (was " --timeout 30 --verdict: the theme line, the migration line and the
#    settings-origin line. A failed migration (`failed:`) or a `read-only` settings origin fails the swap (final
#    review m5): theme.sh exits with bootline's status (3) and says so; the swap line is not printed.
# 4. swap to <t>: <s> s (app <written|skipped> <s>, assets ... <s>, table ...)
#
# The port comes from the git-ignored tools/watch.local: `python3 tools/watchport.py resolve [--port P] [--watch NAME]`
# picks it (P must equal a listed by-id path exactly; with several watches listed, --watch or --port is required),
# before the build, and passes it as --port to flash_keep_nvs.py and bootline.py, which check it again. D defaults
# to firmware/apps/<t>/build.
# Run with the ESP-IDF environment sourced (. ~/esp/v5.5.6/esp-idf/export.sh). Exit 2 refused; otherwise the
# failing step's exit status (flash_keep_nvs: 3 backup, 4 NVS changed, 5 esptool; bootline: 1 timed out, 2 port,
# 3 the new app booted read-only or its migration failed).
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

usage() {
    echo "usage: tools/theme.sh <omni|dw|rd> [--port P] [--watch NAME] [--dry-run] [--no-build] [--build-dir D]" >&2
    exit 2
}

theme='' wp=() dry_run=0 build=1 build_dir=''
while [ $# -gt 0 ]; do
    case $1 in
        omni|dw|rd) [ -z "$theme" ] || usage; theme=$1 ;;
        --port|--watch) [ $# -ge 2 ] || usage; wp+=("$1" "$2"); shift ;;
        --dry-run) dry_run=1 ;;
        --no-build) build=0 ;;
        --build-dir) [ $# -ge 2 ] || usage; build_dir=$2; shift ;;
        *) usage ;;
    esac
    shift
done
[ -n "$theme" ] || usage
wp_err=$(mktemp)
if ! port=$(python3 "$REPO/tools/watchport.py" resolve ${wp[@]+"${wp[@]}"} 2>"$wp_err"); then
    echo "theme.sh: refused: $(cat "$wp_err")" >&2
    rm -f "$wp_err"
    exit 2
fi
cat "$wp_err" >&2   # anything else it printed (a warning) is shown, never taken as part of the port
rm -f "$wp_err"
build_dir=${build_dir:-$REPO/firmware/apps/$theme/build}
build_dir=$(realpath -m -- "$build_dir")   # one absolute path for idf.py -B and the flash

if [ "$build" = 1 ]; then
    idf.py -C "$REPO/firmware/apps/$theme" -B "$build_dir" build
fi

flash=(python "$REPO/tools/flash_keep_nvs.py" --port "$port" --build "$build_dir" --label "swap-$theme")
if [ "$dry_run" = 1 ]; then
    "${flash[@]}" --dry-run
    echo "swap to $theme: dry run (nothing flashed)"
    exit 0
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT
t0=$(date +%s.%N)
"${flash[@]}" | tee "$log"
rc=0
python "$REPO/tools/bootline.py" --port "$port" --expect "theme $theme (was " --timeout 30 --verdict || rc=$?
if [ "$rc" != 0 ]; then
    case $rc in
        3) why="the new app booted read-only or its migration failed" ;;
        1) why="its theme, migration and settings lines did not all show" ;;
        *) why="the port did not come back" ;;
    esac
    echo "theme.sh: swap to $theme FAILED: flashed (NVS identical), but $why (bootline exit $rc, lines above)" >&2
    exit "$rc"
fi
t1=$(date +%s.%N)
regions=$(sed -n 's/^regions: //p' "$log")
echo "swap to $theme: $(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", b - a }') s ($regions)"
