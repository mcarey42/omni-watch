#!/usr/bin/env bash
# Generate the LVGL fonts from the OFL TTFs in google/fonts (pinned commit), into two places (Stage 4b Task 5's census):
# the shared ones, which watch_ui's screens, digital face and glance use, into firmware/components/watch_ui/fonts/, and
# the ones only the Omni's faces use (OMNI_ONLY below) into firmware/apps/omni/main/ui/fonts/ (declared in its
# ui/omni_fonts.h). A font any watch_ui file uses is shared, even if an Omni face uses it too.
# Only Cormorant SC, IBM Plex Sans and IBM Plex Mono, at the sizes the mockups use (global constraints).
# The Mining Ship theme's three families (rd wave 1 Task 7, spec section 12.1: Saira Stencil One, VT323 and Chakra Petch
# 600) are fetched too, but not converted: tools/rd_export.mjs draws their glyph atlases in Chrome from these TTFs. Each
# comes with its OFL.txt, which must be the SIL Open Font License 1.1 and equal the copy committed under
# tools/font_licences/ (PR30: tools/fonts/ is git-ignored, so the verified texts live there); a mismatch fails here.
#
# Downloads go to tools/fonts/ (git-ignored). IBM Plex Sans ships only as a variable font there, so the
# static weights are cut from it with fontTools' instancer (pinned, in a venv under tools/fonts/).
#
# Weights follow what the browser actually rendered for the mockups: index.html loads Cormorant SC at 500 and
# 700 only, so the mockup's "600" Cormorant text was drawn in Bold (CSS picks 700 for a missing 600).
#
# Usage: tools/fonts.sh            (needs curl, python3, node/npx); ends with tools/assets.sh --identity
set -euo pipefail

GF_COMMIT=45e3d5269e4a17d823f6d84470bc084dc5ae46f6   # google/fonts main, 2026-10-21
LV_FONT_CONV=lv_font_conv@1.5.3
FONTTOOLS=fonttools==4.60.1

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DL=$ROOT/tools/fonts
OUT_SHARED=$ROOT/firmware/components/watch_ui/fonts
OUT_OMNI=$ROOT/firmware/apps/omni/main/ui/fonts
OMNI_ONLY=" font_cormorant_700_13 font_cormorant_700_19 font_cormorant_700_24 font_cormorant_700_28 "
OMNI_ONLY+="font_plex_mono_500_16 font_plex_sans_500_11 font_plex_sans_500_50 font_plex_sans_600_13 font_plex_sans_600_16 "
OMNI_ONLY+="font_plex_mono_500_26 font_plex_sans_500_21 font_plex_sans_500_30 font_plex_sans_500_40 "   # Stage 5
mkdir -p "$DL" "$OUT_SHARED" "$OUT_OMNI"

fetch() { # <repo path> <local name>
    [ -s "$DL/$2" ] || curl -fsSL -o "$DL/$2" "https://raw.githubusercontent.com/google/fonts/$GF_COMMIT/$1"
}
fetch ofl/cormorantsc/CormorantSC-Medium.ttf CormorantSC-Medium.ttf
fetch ofl/cormorantsc/CormorantSC-Bold.ttf CormorantSC-Bold.ttf
fetch "ofl/ibmplexsans/IBMPlexSans%5Bwdth,wght%5D.ttf" IBMPlexSans-VF.ttf
fetch ofl/ibmplexmono/IBMPlexMono-Medium.ttf IBMPlexMono-Medium.ttf
fetch ofl/ibmplexmono/IBMPlexMono-Regular.ttf IBMPlexMono-Regular.ttf
# the Mining Ship theme's families and their licences (rd wave 1 Task 7)
fetch ofl/sairastencilone/SairaStencilOne-Regular.ttf SairaStencilOne-Regular.ttf
fetch ofl/sairastencilone/OFL.txt SairaStencilOne-OFL.txt
fetch ofl/vt323/VT323-Regular.ttf VT323-Regular.ttf
fetch ofl/vt323/OFL.txt VT323-OFL.txt
fetch ofl/chakrapetch/ChakraPetch-SemiBold.ttf ChakraPetch-SemiBold.ttf
fetch ofl/chakrapetch/OFL.txt ChakraPetch-OFL.txt
for lic in SairaStencilOne-OFL.txt VT323-OFL.txt ChakraPetch-OFL.txt; do
    grep -q 'SIL OPEN FONT LICENSE Version 1.1' "$DL/$lic" || { echo "tools/fonts/$lic is not the SIL OFL 1.1" >&2; exit 1; }
    cmp -s "$DL/$lic" "$ROOT/tools/font_licences/$lic" \
        || { echo "tools/fonts/$lic differs from the committed tools/font_licences/$lic" >&2; exit 1; }
done
echo "  the Mining Ship fonts: three SIL OFL 1.1 licences, equal to tools/font_licences/"

if [ ! -x "$DL/.venv/bin/fonttools" ]; then
    python3 -m venv "$DL/.venv"
    "$DL/.venv/bin/pip" install -q "$FONTTOOLS"
fi
for w in 400:Regular 500:Medium 600:SemiBold; do
    f=$DL/IBMPlexSans-${w#*:}.ttf
    [ -s "$f" ] || "$DL/.venv/bin/fonttools" varLib.instancer -q -o "$f" "$DL/IBMPlexSans-VF.ttf" wght=${w%:*} wdth=100
done

ASCII=0x20-0x7E
EXTRA="·–—°✓"          # middle dot, en and em dash, degree, check (where the face has them)
QUOTES="“”’"           # curly quotes around a network name, apostrophe
DIGITS="0123456789:-"   # the big time numerals ("--:--" when the clock is unset)
NAV="‹›…"               # "BOOT ‹ back", "Network ›", "Joining…"

gen() { # <name> <ttf> <size> <lv_font_conv range/symbol args...>
    local name=$1 ttf=$2 size=$3 out=$OUT_SHARED
    shift 3
    case "$OMNI_ONLY" in *" $name "*) out=$OUT_OMNI ;; esac
    # relative paths, so the "Opts:" line in the generated file is the same on every machine
    local rel
    rel=$(realpath --relative-to="$out" "$DL")
    (cd "$out" && npx -y "$LV_FONT_CONV" --font "$rel/$ttf" "$@" --size "$size" --bpp 4 \
        --format lvgl --no-compress --lv-font-name "$name" -o "$name.c")
    echo "  $name -> ${out#$ROOT/}"
}

echo "generating into $OUT_SHARED and $OUT_OMNI"
# digital face
gen font_cormorant_700_25 CormorantSC-Bold.ttf 25 -r $ASCII --symbols "$EXTRA"      # weekday
gen font_cormorant_500_17 CormorantSC-Medium.ttf 17 -r $ASCII --symbols "$EXTRA"    # date
gen font_plex_sans_500_118 IBMPlexSans-Medium.ttf 118 --symbols "$DIGITS"           # time
gen font_plex_sans_600_15 IBMPlexSans-SemiBold.ttf 15 -r $ASCII --symbols "$EXTRA"  # AM / PM
gen font_plex_sans_500_10 IBMPlexSans-Medium.ttf 10 -r $ASCII --symbols "$EXTRA"    # home city
gen font_plex_mono_500_15 IBMPlexMono-Medium.ttf 15 -r $ASCII --symbols "$EXTRA"    # seconds
gen font_plex_mono_500_13 IBMPlexMono-Medium.ttf 13 -r $ASCII --symbols "$EXTRA"    # complications
# glance
gen font_plex_sans_500_76 IBMPlexSans-Medium.ttf 76 --symbols "$DIGITS"             # glance time
gen font_cormorant_700_15 CormorantSC-Bold.ttf 15 -r $ASCII --symbols "$EXTRA"      # glance date
# setup screens (faces-setup.js wifi-qr, and the error screens in its house style)
gen font_cormorant_700_26 CormorantSC-Bold.ttf 26 -r $ASCII --symbols "$EXTRA"      # "Scan to set up"
gen font_cormorant_700_30 CormorantSC-Bold.ttf 30 -r $ASCII --symbols "$EXTRA"      # step headings
gen font_cormorant_700_70 CormorantSC-Bold.ttf 70 --symbols "$DIGITS"               # time on "Time set"
gen font_plex_sans_400_14 IBMPlexSans-Regular.ttf 14 -r $ASCII --symbols "$EXTRA$QUOTES" # captions
gen font_plex_sans_400_13 IBMPlexSans-Regular.ttf 13 -r $ASCII --symbols "$EXTRA$QUOTES" # joining detail
gen font_plex_sans_500_16 IBMPlexSans-Medium.ttf 16 -r $ASCII --symbols "$EXTRA$QUOTES"  # network name
gen font_plex_sans_500_14 IBMPlexSans-Medium.ttf 14 -r $ASCII --symbols "$EXTRA$NAV" # pill buttons
gen font_plex_sans_600_10 IBMPlexSans-SemiBold.ttf 10 -r $ASCII --symbols "$EXTRA"  # "STEP 1 OF 4"
gen font_plex_mono_400_11 IBMPlexMono-Regular.ttf 11 -r $ASCII --symbols "$EXTRA"   # app / BLE name lines
# face picker (faces-setup.js nav-map)
gen font_cormorant_700_32 CormorantSC-Bold.ttf 32 -r $ASCII --symbols "$EXTRA"      # face title
gen font_cormorant_700_20 CormorantSC-Bold.ttf 20 -r $ASCII --symbols "$EXTRA"      # "Open" (and "Set up")
gen font_cormorant_700_19 CormorantSC-Bold.ttf 19 -r $ASCII --symbols "$EXTRA"      # thumbnail initials (0.6 r); globe-live date (·)
gen font_cormorant_700_16 CormorantSC-Bold.ttf 16 -r $ASCII                         # thumbnail initials at rest
gen font_plex_sans_500_15 IBMPlexSans-Medium.ttf 15 -r $ASCII --symbols "$EXTRA"    # "Planned"
gen font_plex_sans_500_12 IBMPlexSans-Medium.ttf 12 --symbols "↻⇆"                  # help-line icons
gen font_plex_mono_500_16 IBMPlexMono-Medium.ttf 16 --symbols "${DIGITS}apm—"       # digital thumbnail time; globe-live sun margins
# settings drum (faces-setup.js settings) and quick settings
gen font_plex_sans_500_17 IBMPlexSans-Medium.ttf 17 -r $ASCII --symbols "$EXTRA$QUOTES$NAV" # row in the window
gen font_plex_sans_400_12 IBMPlexSans-Regular.ttf 12 -r $ASCII --symbols "$EXTRA$NAV" # drum rows, help lines
gen font_plex_sans_400_11 IBMPlexSans-Regular.ttf 11 -r $ASCII --symbols "$EXTRA$NAV" # drum rows further out
gen font_plex_sans_500_9 IBMPlexSans-Medium.ttf 9 -r $ASCII --symbols "$EXTRA"      # rim captions ("TURN THE RIM")
gen font_plex_mono_400_10 IBMPlexMono-Regular.ttf 10 -r $ASCII --symbols "$EXTRA$NAV" # "BOOT ‹ back"
# globe-live face (faces-practical.js globe-live)
gen font_plex_sans_500_50 IBMPlexSans-Medium.ttf 50 --symbols "$DIGITS"             # time
gen font_plex_sans_600_13 IBMPlexSans-SemiBold.ttf 13 -r $ASCII                     # AM / PM
gen font_plex_sans_500_11 IBMPlexSans-Medium.ttf 11 -r $ASCII --symbols "$EXTRA"    # home city (10.5 px)
# globe-spin face (faces-practical.js globe-spin: 600 24px FD quiet, 28px loud)
gen font_cormorant_700_24 CormorantSC-Bold.ttf 24 -r $ASCII                         # time, quiet
gen font_cormorant_700_28 CormorantSC-Bold.ttf 28 -r $ASCII                         # time, loud (after a tap)
# globe-analog face (faces-practical.js globe-analog: 600 16px FS date)
gen font_plex_sans_600_16 IBMPlexSans-SemiBold.ttf 16 --symbols "0123456789-"       # date window ("--" unset)
# omni dial (faces-omni.js: the travel caption, 700 13px FD, and Task 8's glance): capitals, digits and space only
gen font_cormorant_700_13 CormorantSC-Bold.ttf 13 --symbols "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
# Stage 5 faces (faces-practical.js world, almanac, timer): digit subsets only
gen font_plex_mono_500_26 IBMPlexMono-Medium.ttf 26 --symbols ".0123456789"          # stopwatch hundredths
gen font_plex_sans_500_21 IBMPlexSans-Medium.ttf 21 --symbols "0123456789:-"         # world cartouche ("--:--" unset)
gen font_plex_sans_500_30 IBMPlexSans-Medium.ttf 30 --symbols "0123456789hm -"       # almanac day length
gen font_plex_sans_500_40 IBMPlexSans-Medium.ttf 40 --symbols "0123456789°−"         # sun panel altitude
# the rim keyboard's two asset fonts (rim keyboard Task 8; spec section 10: in the assets partition, not in code): Plex
# Mono 500 at 21 px (the ring and the field) and 42 px (the loupe), ASCII and the bullet, the same lv_font_conv flags as
# the Plex Mono subsets above, into a temporary C file that tools/kb_assets.py font packs as OFA1
# (firmware/components/watch_ui/assets/kb_mono{21,42}.bin); tools/kb_assets.py copy puts them in each app
KB_TMP=$(mktemp -d); trap 'rm -rf "$KB_TMP"' EXIT
for size in 21 42; do
    rel=$(realpath --relative-to="$KB_TMP" "$DL")
    (cd "$KB_TMP" && npx -y "$LV_FONT_CONV" --font "$rel/IBMPlexMono-Medium.ttf" -r $ASCII --symbols "•" --size $size \
        --bpp 4 --format lvgl --no-compress --lv-font-name "font_plex_mono_500_$size" -o "kb_mono$size.c")
    python3 "$ROOT/tools/kb_assets.py" font --c "$KB_TMP/kb_mono$size.c" \
        --out "$ROOT/firmware/components/watch_ui/assets/kb_mono$size.bin"
done
python3 "$ROOT/tools/kb_assets.py" copy
# the generated fonts must match tools/identity.sha256 byte for byte (Stage 5 Task 5 m1): a different lv_font_conv,
# fontTools or font download fails here; a change meant to happen is recorded with tools/assets.sh --rehash
"$ROOT/tools/assets.sh" --identity
