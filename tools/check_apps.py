#!/usr/bin/env python3
"""The theme apps' drift and identity checks (Stage 4b Task 7).

    python tools/check_apps.py static
    python tools/check_apps.py built [--require] [--dw-build DIR] [--rd-build DIR] [--publishable]
    python tools/check_apps.py terms

static (sources only, no build needed) fails on any of:
  - firmware/common/partitions.csv other than the frozen five rows (spec section 4: nothing below 16 MB ever moves);
  - an app whose effective CONFIG_PARTITION_TABLE_CUSTOM_FILENAME is not common/partitions.csv, or whose
    sdkconfig.defaults.theme sets a key outside the allow-list (the symbols the apps' main/Kconfig.projbuild declare);
  - dependency versions that differ between the apps' dependencies.lock files (preflight A14);
  - a watch_ui neutrality hit (watch_ui never names the Omni's views or headers, plan PD1, nor the dw
    theme's dw_/DW_ names, dw wave 1 Task 3);
  - an NVS call under firmware/apps/dw (the dw firmware never calls nvs_* itself: the store owns NVS, dw wave 1
    Task 3);
  - an NVS or raw flash erase call in our tree (firmware/apps, firmware/components, firmware/common, tools; never
    managed_components, whose third-party examples erase), or a tool-side erase there: esptool's chip or region
    erase, its erase-everything write option, or idf.py's erase target;
  - a .pcm under apps/dw/assets that tools/gen_sfx_dw.py does not name, a named one missing, or one over 128000 bytes, or any other audio
    file there (dw wave 1 Task 8: the four synthesised sounds only);
  - a dw background that is not byte-equal to the Omni's;
  - (rim keyboard Task 8) firmware/components/watch_ui/assets holding other than the keyboard's five shared assets
    (SHARED_UI_ASSETS), or an app (omni, dw, rd) whose copy of one is missing or not byte-equal to it; kb_tick.pcm is
    the one sound the dw and rd sound rules allow beside their own;
  - (rim keyboard Task 10, spec section 7.5) the Wi-Fi restore call other than exactly once across the erase scan's
    scope, inside net.c's `case CMD_FORGET:` block, or the manager's wrapper for it called anywhere (RESTORE_*);
  - (rim keyboard Task 10, I3) in net.c, scr_keyboard.c, debug_console.c and scr_settings.c, a printf/ESP_LOG*/snprintf/fprintf/puts
    call printing a string (%s, %.*s) whose arguments name a secret buffer (SECRET_NAMES); an snprintf into one of
    them is skipped;
  - apps/dw/main/dw_stage.cmake's allowed list other than DW_SWAP (the swap files, wave 2 Task 4);
  - a forbidden name (tools/dw_terms.py's pattern) in the text of firmware/apps/dw/**, firmware/components/**,
    firmware/test/host/**, tools/dw_*, tools/export/dw_*, tools/gen_sfx_dw.py and tools/test_gen_sfx_dw.py
    (dw wave 1 Task 9, D9).
  It also runs the self-tests: the forbidden-string scans, the prompt, the sound rule, --publishable, the descriptor
  checksum and the mmap image reader.
  rd wave 1 Task 5 (spec section 13.3) adds, for the third theme (rd, the Mining Ship):
  - the rd self-tests first (PR8): rd_terms_calibrated, then rd_terms_zero_hits_today, which is the rd scan itself:
    tools/rd_terms.py's list over the text of firmware/apps/rd/** (assets' text files too), firmware/facekit/**,
    firmware/components/**, firmware/test/host/**, tools/rd_*, tools/export/rd_*, tools/gen_sfx_rd.py,
    tools/test_*rd*, tools/facepack.py and the rd docs (docs/superpowers/specs/*rd-theme*, docs/superpowers/plans/*rd*,
    docs/rd-*.md); then rd_terms_agree (the Python and JS lists give one verdict on every sample; skipped, never
    passed, without node: PR20, and `check_apps.py terms` exits 77 then), rd_terms_not_self_matching,
    tracked_design_fails, rd_overlay_fails, facekit_neutral, rd_nvs_call_fails and facekit_pure_comments;
  - the dw scan's scope also covers firmware/facekit/** and tools/facepack.py;
  - watch_ui neutrality also forbids rd_, RD_ and fk_ names;
  - face_kit neutrality: no dw_, DW_, rd_ or RD_ name, nor any Omni view name or header, in any file under
    firmware/facekit (whole lines, comments included, as watch_ui's);
  - (R159) both neutrality rules bound a name by no letter or digit before it, not \b, so an embedded one (s_dw_x)
    is caught;
  - face_kit purity: no esp_*, freertos/ or lvgl.h in its top-level files (firmware/facekit/face_kit, not ui/), read
    from #include lines and identifiers outside comments (PR19: comments may name esp_timer);
  - an NVS call under firmware/apps/rd; the rd sound rule (rd wave 1 Task 8: exactly the .pcm files tools/gen_sfx_rd.py's
    VOICINGS name, rd_chime.pcm and rd_chime_r.pcm, under apps/rd/assets, and no other audio); an rd background not
    byte-equal to the Omni's;
  - an overlay descriptor naming rd (rd has no overlay: every rd build is generic), an overlay-form file in rd's tree,
    or the overlay variable in rd's CMake code;
  - the tracked-path guard (PR7): any tracked path under design/themes/, or under research/ outside
    RESEARCH_TRACKED_AT_BASE (the research/ paths tracked at e62bc30, read from tools/research_tracked.txt, one per
    line; empty without that file). Fail-closed: a failed `git ls-files` fails.

The term lists (public release Task 5) live only in the private modules tools/dw_terms.py and tools/rd_terms.py (the
public export leaves them and tools/research_tracked.txt out), loaded through tools/terms.py. In a public tree (a
PUBLIC_TREE file at the root) a missing module skips: static prints one SKIP line for the term scans and their
self-tests, built one for the dw and rd list scans of the images (every other check runs), and terms exits 77.
Anywhere else a missing module fails, as before. A module present is always used, and static fails when PUBLIC_TREE
sits beside one, or beside tools/export/dw_terms.mjs, tools/export/rd_terms.mjs or tools/research_tracked.txt (a stray
marker never disables a scan; preflight M14, public release Task 6).

built (needs the default build of every app; exit 77 when one is missing, unless --require) fails on any of:
  - partition-table.bin not byte-identical across every app build directory found;
  - generated firmware/apps/*/sdkconfig files that differ outside the allow-list;
  - a forbidden name in omni_watch_dw.bin or the dw assets.bin (spec section 14), scanned as `strings -n 4` would
    see it, case-insensitive: the show's name in any spelling (the words with any spaces, hyphens or underscores
    between, and the abbreviated title), the time machine's name and the sign's word as substrings, the broadcaster's initials only as a word
    (preflight B21: a raw 3-byte match hits about once by chance in 2 MB). The payloads of the image's .pcm files are
    skipped (synthesised sound is not text);
  - a dw assets image file at an odd address (dw_img_from_bin rejects one);
  - a dw descriptor whose compiled assets_files/assets_checksum differ from that build dir's mmap_generate_assets.h
    (G3; read from the elf by xtensa-esp32s3-elf-gdb);
  - an Omni symbol in omni_watch_dw.elf (xtensa-esp32s3-elf-nm);
  - a dw_ or DW_ symbol in omni_watch.elf (dw_core and the dw app never reach the Omni image, dw wave 1 Task 3);
  - the Omni's console prompt (the bytes "omni" + "> ") in omni_watch_dw.bin: the prompt is built at run time from the
    theme id, so no image holds the literal and there is no positive half (the dw reads "dw> " on the device).

rd wave 1 Task 5 adds to built (spec section 13.3): the rd build (--rd-build DIR, default firmware/apps/rd/build) in the
table comparison; rd's descriptor (RD_THEME) checksum against its own mmap_generate_assets.h; its image alignment; no
Omni, dw_ or DW_ symbol in omni_watch_rd.elf; no rd_ or RD_ symbol in the Omni or dw elfs and no fk_ symbol in the
Omni's; neither the rd prompt ("rd" + "> ") nor the Omni's as a literal in omni_watch_rd.bin; omni_watch_rd.bin, the rd
image, omni_watch_dw.bin and the dw image scanned against both lists (rd's by tools/rd_terms.py's rules: the 3-letter
tokens word-bounded and only in printable runs of 8 or more (R159), and the assets images' .bin and .pcm payloads
blanked, PR8 d, PR35); the
Omni's omni_watch.bin against rd's list; and the rd image equal to firmware/apps/rd/assets file for file (rd is always
generic, so this runs on every rd build, --publishable or not).

--dw-build DIR runs the dw checks on DIR instead of firmware/apps/dw/build (the private build: build-private).
--publishable (the generic build only) also fails unless DIR/dw_art.txt reads "generic", every file in the assets
image is byte-equal to the committed one in firmware/apps/dw/assets (and the sets are equal), and no tracked file has
an overlay descriptor's form (its name, or a JSON object with "theme", "replaces" and "layers"; R132, G1, G5).
On a build whose dw_art.txt reads "overlay ..." (build-private), every file in the assets image must be the committed
one except the swap files (DW_SWAP: dw_doors_m.bin, dw_b5_box.bin, dw_doors_c.bin from wave 2 Task 4; dw_th_b5.bin,
dw_th_d3.bin and dw_chime.pcm from public release Task 9), and the sets must be equal. The self-tests also check an overlay descriptor's "replaces" against DW_SWAP.

    python tools/check_apps.py terms     rd_terms_agree alone: exit 0 agree, 1 disagree, 77 without node (PR20) or
                                         in a public tree

Exit 0 pass, 1 fail (each failure printed as FAIL: ...), 77 skipped (built without --require, a build missing).
"""
import argparse
import filecmp
import importlib.util
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import terms  # noqa: E402  (finds the private term modules; a public tree has none)

REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
FW = os.path.join(REPO, 'firmware')
APPS_DIR = os.path.join(FW, 'apps')
COMMON = os.path.join(FW, 'common')
TABLE = os.path.join(COMMON, 'partitions.csv')
REQUIRED_APPS = ('omni', 'dw', 'rd')
SKIP = 77
NO_GIT_NOTE = 'the host tests are run from a git clone (README, Host tests)'

# (name, type, subtype, offset, size, flags): the frozen table, parsed values; no row carries a flag
FROZEN = [
    ('nvs', 'data', 'nvs', 0x9000, 0x6000, ''),
    ('phy_init', 'data', 'phy', 0xf000, 0x1000, ''),
    ('factory', 'app', 'factory', 0x10000, 0x400000, ''),
    ('assets', 'data', 'spiffs', 0x410000, 0xBF0000, ''),
    ('pack', 'data', 0x40, 0x1000000, 0x1000000, ''),
]

# Task 5 step 3's pattern, anchored (preflight A13): omni_globe_* is the shared core's, not the Omni's
# dw wave 1 Task 3: nor the dw theme's names (dw_core's dw_*/DW_*).
# R159 (4): the names are bounded by no letter or digit before them, not \b, so an embedded one (s_dw_x, the static
# prefix's underscore being a word character) is caught too.
_NB = r'(?<![A-Za-z0-9])'
OMNI_VIEWS = _NB + r'(dial_view|globe_view|globe_face|face_omni|FACE_OMNI)|assets_meta\.h|omni_faces\.h|omni_sfx\.h'
# rd wave 1 Task 5 (spec section 13.3): nor the Mining Ship theme's (rd_core's rd_*/RD_*) or face_kit's (fk_*) names
NEUTRALITY = re.compile(OMNI_VIEWS + '|' + _NB + r'(dw|DW|rd|RD|fk)_\w+')
# face_kit (firmware/facekit) is shared by dw and rd and names neither theme, nor the Omni's views (rd wave 1 Task 5)
FACEKIT_NEUTRALITY = re.compile(OMNI_VIEWS + '|' + _NB + r'(dw|DW|rd|RD)_\w+')
# face_kit's top level is pure (the omni_core rule): no esp_*, freertos/ or lvgl.h, read from #include lines and from
# identifiers in the code with its comments blanked (PR19: moved comments name esp_timer and esp_rom_crc32_le)
FACEKIT_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](esp_|freertos/|lvgl\.h)')
FACEKIT_IMPURE_ID = re.compile(r'\besp_\w+')
# Any NVS call under firmware/apps/dw (dw wave 1 Task 3) or firmware/apps/rd (rd wave 1 Task 5): a theme app reaches NVS
# only through the store.
DW_NVS_CALL = re.compile(r'\bnvs_\w+\s*\(')
NVS_CALL_APPS = ('dw', 'rd')
# Every NVS erase call: the nvs_ prefix, any word characters, then "erase" (the nvs_flash family's erase, erase
# partition and erase partition_ptr; erase key and erase all; any future one). Split so this file never matches
# its own scan, and no comment here spells a call out.
# Raw flash erases too (a partition-range or region erase can wipe NVS as surely), and the tool-side ones (final
# review m4): esptool's whole-chip and region erase commands and its write option that erases everything first, and
# idf.py's erase target, in either spelling (underscore or hyphen).
ERASE = re.compile(r'\bnvs' + r'_\w*erase\w*|\b(esp_partition|esp_flash|spi_flash)' + r'_erase\w*'
                   r'|\berase' + r'[_-](flash|region|all)\b')
ERASE_SCOPE = ('firmware/apps', 'firmware/components', 'firmware/common', 'tools')
# The Wi-Fi restore (it drops the saved network) is called from exactly one place, net.c's CMD_FORGET (spec section 7.5,
# Task 10), and the manager's wrapper round it never: counted over ERASE_SCOPE, every line (comments too, as `git grep`
# counts them). Split, like ERASE, so this file never counts its own patterns (Minor 15).
RESTORE_CALL = re.compile(r'\besp_wifi' + r'_restore\s*\(')
RESTORE_WRAPPER = re.compile(r'\bnetwork_prov_mgr_reset' + r'_wifi_provisioning\s*\(')
RESTORE_HOME = 'firmware/components/net/net.c'
RESTORE_CASE = 'CMD_FORGET'
CASE_LABEL = re.compile(r'\b(?:case\s+(\w+)|default)\s*:')
# The secret guard (I3): a printing call whose format prints a string and whose arguments name one of these. An
# snprintf whose destination names one is skipped (it builds the secret, it does not show it: net.c's PoP builder).
SECRET_FILES = ('firmware/components/net/net.c', 'firmware/components/watch_ui/scr_keyboard.c',
                'firmware/components/watch_ui/debug_console.c',
                'firmware/components/watch_ui/scr_settings.c')   # Task 14: the About sheet (spec 8.4: never a secret)
SECRET_NAMES = re.compile(r'\b(?:s_trial_pass|s_snap|s_pop|pop|pass)\b|\.password\b|\bedit\.buf\b')
# An snprintf into one of these exact names (its destination's leading identifier) builds it and is skipped
SECRET_DEST = ('s_trial_pass', 's_snap', 's_pop', 'pop', 'pass')
PRINT_CALL = re.compile(r'\b(printf|snprintf|fprintf|puts|esp_rom_printf|ets_printf|ESP_LOG_BUFFER_\w+|'
                        r'ESP_(?:EARLY_|DRAM_)?LOG[A-Z])\s*\(')
# a conversion that shows the argument's value: a string, a character, or a number (per-byte hex and the like)
PRINT_STR = re.compile(r'%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t)?[sScxXdiuo]')
# The only directories the tree scans skip: build and fetched output at its known locations (relative to the repo;
# the app-level ones for every app). A directory with one of these names anywhere else is scanned. Binary files are
# skipped by content.
SKIP_FIXED = ('tools/node_modules', 'tools/.venv-assets', 'tools/export/out', 'tools/export/out-rd', 'tools/fonts',
              '.git')
SKIP_PER_APP = ('managed_components', 'assets')   # plus build/ and build-*/ in each app directory
DW_BACKGROUNDS = ('bg_bezel.bin', 'bg_picker.bin', 'bg_settings.bin')
# The rim keyboard's shared assets (Task 8): their source is watch_ui/assets (D5), which holds exactly these; each app's
# assets directory holds a byte-equal copy (tools/kb_assets.py copy). kb_tick.pcm is the one sound the dw and rd sound
# rule allows beside their own (an original click, synthesised by tools/kb_assets.py tick).
SHARED_UI_ASSETS = ('kb_lay.bin', 'kb_mono21.bin', 'kb_mono42.bin', 'kb_icons.bin', 'kb_tick.pcm')
SHARED_UI_SRC = os.path.join(FW, 'components', 'watch_ui', 'assets')
SHARED_UI_SOUNDS = ('kb_tick.pcm',)
OMNI_SYMBOL = re.compile(r'^(omni_dial_|dial_view|dv_|globe_view|globe_face|face_omni|omni_lid|omni_iris|omni_travel|'
                         r'omni_tmr_|omni_alm_|omni_wt_|omni_tz_offset|face_timer|face_almanac|face_world|'
                         r'FACE_(TIMER|ALMANAC|WORLD)_VT|tmr_svc|timer_glance|alm_cache)')

PRINTABLE_RUN = re.compile(rb'[\x20-\x7e\t]{4,}')   # `strings -n 4`
# spec section 14 (dw wave 1 Task 9, D9): the dw list (FORBIDDEN, FORBIDDEN_TEXT and the self-test cases) lives in
# tools/dw_terms.py, the rd list in tools/rd_terms.py; both private, loaded through tools/terms.py (public release Task 5).
TERM_MODULES = ('dw_terms', 'rd_terms')
# The other private-only files (public release Task 6): the JS twins of the term modules and the research allow-list.
# Like a term module, any of them beside a PUBLIC_TREE marker fails static.
PRIVATE_ONLY_FILES = ('tools/export/dw_terms.mjs', 'tools/export/rd_terms.mjs', 'tools/research_tracked.txt')
SKIP_STATIC_TERMS = 'SKIP: dw_terms/rd_terms absent (a public tree): the term scans and their self-tests'
# The static trademark scan's scope: the text of these trees and globs (relative to the repo; binary files skipped).
# The whole host-test tree is scanned (the final review, I1): shared tests carry the dw track's text too.
# rd wave 1 Task 5 (spec section 13.2): face_kit (moved dw code) and the face packer too.
DW_TEXT_TREES = ('firmware/apps/dw', 'firmware/components', 'firmware/test/host', 'firmware/facekit')
DW_TEXT_GLOBS = ('tools/dw_*', 'tools/export/dw_*', 'tools/gen_sfx_dw.py',
                 'tools/test_gen_sfx_dw.py', 'tools/facepack.py')
# The rd scan's scope (spec section 13.2, rd wave 1 Task 5): tools/rd_terms.py's list over these trees (apps/rd/assets'
# text files too) and globs, the rd docs included.
RD_TEXT_TREES = ('firmware/apps/rd', 'firmware/facekit', 'firmware/components', 'firmware/test/host')
RD_TEXT_GLOBS = ('tools/rd_*', 'tools/export/rd_*', 'tools/gen_sfx_rd.py', 'tools/test_*rd*', 'tools/facepack.py',
                 'docs/superpowers/specs/*rd-theme*', 'docs/superpowers/plans/*rd*', 'docs/rd-*.md')
# The swap files (R132, WD20): the only files an overlay may replace; generic twins live in apps/dw/assets. Wave 2
# Task 4 adds the Classic doors; public release Task 9 the two thumbnails and the chime, which the private overlay
# ships as they were (main/dw_stage.cmake's allowed list is the same six, in this order).
DW_SWAP = ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin', 'dw_th_b5.bin', 'dw_th_d3.bin', 'dw_chime.pcm')
# The swap files dw_layers.json's "out": "generic" rows draw (tools/export/dw_generic.js; preflight I1): the call-box
# doors and front. The thumbnails and the chime have their own generic rows and generator.
DW_SWAP_GENERIC = ('dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin')
# An overlay's descriptor, recognised by its form (G5): the name, or a JSON object with these keys.
OVERLAY_JSON = 'over' + 'lay.json'
OVERLAY_KEYS = ('theme', 'replaces', 'layers')
DW_ART_FILE = 'dw_art.txt'          # written by firmware/apps/dw/main/CMakeLists.txt: "generic" or "overlay <sha>"
MMAP_HEADER = 'mmap_generate_assets.h'   # per build dir (IMPORT_INC_PATH, G3)
DW_IMAGE = 'mmap_build/assets/assets/assets.bin'
# The Omni's console prompt as a byte run, built from pieces so this file never holds it either.
OMNI_PROMPT = b'omni' + b'> '
# rd's, the same way: built at run time from the theme id, so no image holds it (matched with no letter, digit or _
# before it, so a longer word ending in the two letters is not the prompt).
RD_PROMPT = re.compile(rb'(?<![A-Za-z0-9_])' + b'rd' + b'> ')
RD_IMAGE = DW_IMAGE
# The tracked-path guard (PR7): no design package is ever tracked; research/ only as it stood at e62bc30 (.gitignore's
# /research/ does not untrack these). A literal list, private (tools/research_tracked.txt): a new path under research/
# fails until someone rules on it.
DESIGN_THEMES = 'design/themes/'
RESEARCH = 'research/'
RESEARCH_LIST = os.path.join(REPO, 'tools', 'research_tracked.txt')


def research_tracked():
    """tools/research_tracked.txt's paths (one per line; blank lines and # comments skipped), or () without it."""
    if not os.path.isfile(RESEARCH_LIST):
        return ()
    with open(RESEARCH_LIST, encoding='utf-8') as f:
        return tuple(ln.strip() for ln in f if ln.strip() and not ln.lstrip().startswith('#'))


RESEARCH_TRACKED_AT_BASE = research_tracked()


class Checks:
    def __init__(self):
        self.failures = []

    def fail(self, msg):
        self.failures.append(msg)
        print('FAIL: ' + msg)


def rel(p):
    return os.path.relpath(p, REPO)


def apps():
    """Every app directory under firmware/apps (one with a CMakeLists.txt), by name."""
    if not os.path.isdir(APPS_DIR):
        return {}
    return {n: os.path.join(APPS_DIR, n) for n in sorted(os.listdir(APPS_DIR))
            if os.path.isfile(os.path.join(APPS_DIR, n, 'CMakeLists.txt'))}


def present_apps(c):
    found = apps()
    for name in REQUIRED_APPS:
        if name not in found:
            c.fail('app firmware/apps/%s is missing' % name)
    return found


# ------------------------------------------------------------------ parsers
def parse_num(s):
    s = s.strip()
    m = re.fullmatch(r'(0x[0-9a-fA-F]+|\d+)([KkMm]?)', s)
    if not m:
        return s.lower()
    n = int(m.group(1), 0)
    return n * {'': 1, 'k': 1024, 'm': 1024 * 1024}[m.group(2).lower()]


def parse_table(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            cols = [x.strip() for x in line.split(',')]
            cols += [''] * (6 - len(cols))
            name, typ, sub, off, size = cols[:5]
            flags = ':'.join(sorted(f for c in cols[5:] for f in re.split(r'[\s:]+', c.lower()) if f))
            rows.append((name, typ.lower(), parse_num(sub), parse_num(off) if off else None, parse_num(size), flags))
    return rows


def parse_sdkconfig(path):
    """KEY -> value; '# CONFIG_X is not set' -> 'n'. Other comments and blank lines are ignored."""
    out = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            m = re.fullmatch(r'# (CONFIG_\w+) is not set', line)
            if m:
                out[m.group(1)] = 'n'
                continue
            if not line or line.startswith('#'):
                continue
            k, _, v = line.partition('=')
            out[k.strip()] = v.strip()
    return out


def allow_list():
    """CONFIG_<sym> for every `config <sym>` an app's main/Kconfig.projbuild declares (plan PD3: derived, not kept)."""
    keys = set()
    for d in apps().values():
        kp = os.path.join(d, 'main', 'Kconfig.projbuild')
        if os.path.isfile(kp):
            with open(kp) as f:
                for line in f:
                    m = re.match(r'\s*(?:menu)?config\s+(\w+)', line)
                    if m:
                        keys.add('CONFIG_' + m.group(1))
    return keys


def lock_versions(path):
    """component -> version from a dependencies.lock (the 4-space `version:` under each 2-space component)."""
    out, comp, in_deps = {}, None, False
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if re.match(r'^\S', line):
                in_deps = line.startswith('dependencies:')
                comp = None
                continue
            if not in_deps:
                continue
            m = re.match(r'^  (\S[^:]*):\s*$', line)
            if m:
                comp = m.group(1)
                continue
            m = re.match(r'^    version:\s*(.+)$', line)
            if m and comp:
                out[comp] = m.group(1).strip().strip("'\"")
    return out


def forbidden_hits(data, pat):
    """Forbidden names (pat: tools/dw_terms.py's FORBIDDEN) in the printable runs (>= 4 bytes) of data, as (offset,
    text)."""
    hits = []
    for run in PRINTABLE_RUN.finditer(data):
        for m in pat.finditer(run.group(0)):
            hits.append((run.start() + m.start(), run.group(0)[max(0, m.start() - 16):m.end() + 16].decode('ascii')))
    return hits


def text_hits(rt, pat, text):
    """Forbidden names (pat: tools/dw_terms.py's FORBIDDEN_TEXT) in source text, as (line number, line): each line
    alone, then the whole text folded by rt.folded_hits (R164, R167: a name wrapped across a line break, behind any run
    of comment or quote leaders too, reported at the line where it starts; one fold, tools/rd_terms.py's, for dw's list
    as for rd's)."""
    hits = {i: line.strip() for i, line in enumerate(text.splitlines(), 1) if pat.search(line)}
    for i, line in rt.folded_hits(text, pat):
        hits.setdefault(i, line)
    return sorted(hits.items())


def mmap_entries(img):
    """The esp_mmap_assets image's files as (name, data address in the image, size): spiffs_assets_gen.py's layout,
    a 32-byte header ("MMAP", version, name length, file count, checksum, length, 8 reserved), then per file the
    name (name length bytes), u32 size, u32 offset, u16 w, h; each file's data follows a 2-byte 0x5A5A marker."""
    if len(img) < 32 or img[:4] != b'MMAP':
        raise ValueError('not an esp_mmap_assets image')
    nl, n = int.from_bytes(img[8:12], 'little'), int.from_bytes(img[12:16], 'little')
    base, out = 32 + n * (nl + 12), []
    for i in range(n):
        e = img[32 + i * (nl + 12):32 + (i + 1) * (nl + 12)]
        name = e[:nl].split(b'\0', 1)[0].decode()
        size, off = int.from_bytes(e[nl:nl + 4], 'little'), int.from_bytes(e[nl + 4:nl + 8], 'little')
        out.append((name, base + off + 2, size))
    return out


def build_mmap_image(files, align=64):
    """A small image in spiffs_assets_gen.py's layout (the self-test's; files: (name, data))."""
    nl = (max(len(f) for f, _ in files) + 1 + 3) & ~3
    base, data, table = 32 + len(files) * (nl + 12), bytearray(), bytearray()
    for name, b in files:
        data += b'\0' * ((-(base + len(data) + 2)) % align)
        table += name.encode().ljust(nl, b'\0') + len(b).to_bytes(4, 'little') + len(data).to_bytes(4, 'little') + bytes(4)
        data += b'\x5a\x5a' + b
    body = bytes(table + data)
    return (b'MMAP' + (0x10000).to_bytes(4, 'little') + nl.to_bytes(4, 'little') + len(files).to_bytes(4, 'little') +
            (sum(body) & 0xFFFF).to_bytes(4, 'little') + len(body).to_bytes(4, 'little') + bytes(8) + body)


def image_forbidden_hits(img, pat):
    """forbidden_hits over the dw assets image with every .pcm entry's payload blanked (synthesised noise and tones
    are not text, and 2 MB of them hit a 3-byte word by chance; Task 9)."""
    return forbidden_hits(blanked(img, ('.pcm',)), pat)


def parse_mmap_header(text):
    """(file count, checksum) from a generated mmap_generate_assets.h, or None."""
    f = re.search(r'#define\s+MMAP_ASSETS_FILES\s+(\d+)', text)
    k = re.search(r'#define\s+MMAP_ASSETS_CHECKSUM\s+(0x[0-9A-Fa-f]+|\d+)', text)
    return (int(f.group(1)), int(k.group(1), 0)) if f and k else None


def checksum_problems(desc, header):
    """desc: the descriptor's compiled (assets_files, assets_checksum); header: that build dir's (G3)."""
    if desc is None or header is None:
        return ['descriptor %r or header %r unreadable' % (desc, header)]
    if tuple(desc) != tuple(header):
        return ['descriptor assets_files/assets_checksum %d/0x%04X, the build dir\'s %s says %d/0x%04X'
                % (desc[0], desc[1], MMAP_HEADER, header[0], header[1])]
    return []


def overlay_form(path, text):
    """True for an overlay descriptor: the name, or a JSON object carrying every OVERLAY_KEYS key."""
    if os.path.basename(path) == OVERLAY_JSON:
        return True
    try:
        import json
        obj = json.loads(text)
    except ValueError:
        return False
    return isinstance(obj, dict) and all(k in obj for k in OVERLAY_KEYS)


def publishable_problems(art, packed, committed, tracked_json):
    """--publishable (G1): art is dw_art.txt's text; packed and committed map file name -> bytes (the image's files,
    firmware/apps/dw/assets' .bin, .pcm and .dat); tracked_json is (path, text) for every tracked .json file."""
    out = []
    if art.strip() != 'generic':
        out.append('%s reads %r, not "generic"' % (DW_ART_FILE, art.strip()))
    for n in sorted(set(packed) | set(committed)):
        if n not in committed:
            out.append('packed file %s is not committed in firmware/apps/dw/assets' % n)
        elif n not in packed:
            out.append('committed file %s is not in the image' % n)
        elif packed[n] != committed[n]:
            out.append('packed file %s differs from the committed one' % n)
    for path, text in tracked_json:
        if overlay_form(path, text):
            out.append('tracked file %s has an overlay descriptor\'s form' % path)
    return out


def cmake_code(text):
    """text with its CMake comments blanked (wave 2 Task 6, the Task 5 review's M2; brackets: wave 2 Task 7, the Task 6
    review's M2). A line comment is a # outside a quoted or bracket argument, to the end of its line. A bracket comment
    is # followed at once by a bracket opening, [ then n = signs then [, and runs to the first ] with the same n = signs
    then ], across lines; it is blanked but its newlines are kept, so line numbers stay. A bracket argument ([=*[ ...
    ]=*]) is kept whole: a # or a " inside it is literal. A # inside "..." is kept (an escaped quote does not end the
    string). An unclosed bracket runs to the end of the text (a comment is blanked, an argument kept): fail-safe for
    the rule that reads the result, which then sees an unclosed argument's text as code."""
    open_re = re.compile(r'\[(=*)\[')
    out, i, n, quoted, esc = [], 0, len(text), False, False
    while i < n:
        ch = text[i]
        if quoted:
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                quoted = False
            out.append(ch)
            i += 1
        elif ch == '"':
            quoted = True
            out.append(ch)
            i += 1
        elif ch == '#':
            m = open_re.match(text, i + 1)
            if m:   # a bracket comment: blanked to its close, newlines kept
                close = text.find(']' + m.group(1) + ']', m.end())
                stop = n if close < 0 else close + len(m.group(1)) + 2
                out.append('\n' * text.count('\n', i, stop))
            else:   # a line comment: blanked to the end of its line
                nl = text.find('\n', i)
                stop = n if nl < 0 else nl
            i = stop
        elif ch == '[' and open_re.match(text, i):   # a bracket argument: kept whole
            m = open_re.match(text, i)
            close = text.find(']' + m.group(1) + ']', m.end())
            stop = n if close < 0 else close + len(m.group(1)) + 2
            out.append(text[i:stop])
            i = stop
        else:
            out.append(ch)
            i += 1
    return ''.join(out)


def stage_allowed_problems(text):
    """dw_stage.cmake's allow-list (Task 4 review M3): exactly one set(allowed ...), and every other mention of
    `allowed` only reads it (${allowed}, IN_LIST allowed), so nothing appends to, replaces or unsets the list. Comments
    are not code (wave 2 Task 6): they are blanked first (cmake_code)."""
    text = cmake_code(text)
    sets = re.findall(r'^set\(allowed [^)]*\)', text, re.M)
    out = [] if len(sets) == 1 else ['%d set(allowed ...) lines (exactly one)' % len(sets)]
    rest = re.sub(r'^set\(allowed [^)]*\)', '', text, count=1, flags=re.M)
    rest = re.sub(r'\$\{allowed\}|\bIN_LIST\s+allowed\b', '', rest)
    for n, line in enumerate(rest.splitlines(), 1):
        if re.search(r'\ballowed\b', line):
            out.append('`allowed` outside its set() at line %d: %s' % (n, line.strip()))
    return out


def generic_rows_problems(rows):
    """dw_layers.json's generic rows (Task 4 review M2): their files are DW_SWAP_GENERIC, no more and no fewer."""
    got = sorted(r.get('file') or '' for r in rows if isinstance(r, dict) and r.get('out') == 'generic')
    return [] if got == sorted(DW_SWAP_GENERIC) else ['the generic rows draw %s, DW_SWAP_GENERIC is %s'
                                                      % (got, list(DW_SWAP_GENERIC))]


def replaces_problems(obj):
    """An overlay descriptor's "replaces" (wave 2 Task 4): a non-empty list of distinct names, each in DW_SWAP."""
    reps = obj.get('replaces') if isinstance(obj, dict) else None
    if not isinstance(reps, list) or not reps:
        return ['"replaces" is not a non-empty list: %r' % (reps,)]
    out = ['replaces %r: an overlay may replace only %s' % (n, ', '.join(DW_SWAP)) for n in reps if n not in DW_SWAP]
    out += ['replaces %r twice' % n for n in sorted(set(n for n in reps if isinstance(n, str) and reps.count(n) > 1))]
    return out


def swap_problems(packed, committed):
    """A private (overlay) build's image (wave 2 Task 4): the committed set exactly, every file byte-equal to the
    committed one but the swap files (DW_SWAP), which the overlay may have replaced."""
    out = []
    for n in sorted(set(packed) | set(committed)):
        if n not in committed:
            out.append('packed file %s is not committed in firmware/apps/dw/assets' % n)
        elif n not in packed:
            out.append('committed file %s is not in the image' % n)
        elif packed[n] != committed[n] and n not in DW_SWAP:
            out.append('packed file %s differs from the committed one and is no swap file (%s)' % (n, ', '.join(DW_SWAP)))
    return out


def omni_prompt_hits(data):
    """Offsets of the Omni's console prompt in data."""
    return [m.start() for m in re.finditer(re.escape(OMNI_PROMPT), data)]


# ------------------------------------------------------------------ rd wave 1 Task 5
PIXEL_EXTS = ('.bin', '.pcm')   # an assets image's pixel and sound payloads, blanked before rd's scan (PR35)


def load_terms(c, mode):
    """(dw_terms, rd_terms) through tools/terms.py: each module whenever its file exists; None in a public tree; outside
    one, a missing module is a failure (None, recorded on c). static (mode) also fails when the PUBLIC_TREE marker
    sits beside a module (preflight M14: a stray marker never disables a scan, and never passes silently)."""
    mods = []
    for name in TERM_MODULES:
        try:
            mods.append(terms.load(REPO, name))
        except terms.TermsMissing:
            c.fail('%s missing: tools/%s.py (the %s list; its scans and self-tests cannot run)' % (name, name, name[:2]))
            mods.append(None)
        if mode == 'static' and terms.public_tree(REPO) and os.path.isfile(terms.module_path(REPO, name)):
            c.fail('%s beside tools/%s.py: a public tree holds no term module, and a private one no marker (remove '
                   'whichever is stray)' % (terms.MARKER, name))
    if mode == 'static' and terms.public_tree(REPO):   # Task 6: the other private-only files, likewise
        for rel in PRIVATE_ONLY_FILES:
            if os.path.isfile(os.path.join(REPO, *rel.split('/'))):
                c.fail('%s beside %s: a public tree holds no private-only file, and a private one no marker (remove '
                       'whichever is stray)' % (terms.MARKER, rel))
    return tuple(mods)


def blanked(img, exts):
    """An esp_mmap_assets image with every entry's payload whose name ends in one of exts zeroed."""
    buf = bytearray(img)
    for name, at, size in mmap_entries(img):
        if name.lower().endswith(exts):
            buf[at:at + size] = bytes(size)
    return bytes(buf)


def c_code(text):
    """C source with its comments blanked (a block comment's newlines kept, so line numbers stay); string and character
    literals are kept whole, so a comment opener inside one is literal. An unclosed comment runs to the end."""
    out, i, n = [], 0, len(text)
    while i < n:
        ch = text[i]
        if ch in '"\'':
            j = i + 1
            while j < n and text[j] != ch and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        else:
            out.append(ch)
            i += 1
    return ''.join(out)


def facekit_neutral_problems(text):
    """face_kit neutrality: (line number, line) for every line naming a theme (dw_, DW_, rd_, RD_) or an Omni view."""
    return [(i, line.strip()) for i, line in enumerate(text.splitlines(), 1) if FACEKIT_NEUTRALITY.search(line)]


def facekit_pure_problems(text):
    """face_kit purity (PR19): (line number, line) for an #include of esp_*, freertos/ or lvgl.h, or an esp_ identifier,
    in the code with its comments blanked."""
    return [(i, line.strip()) for i, line in enumerate(c_code(text).splitlines(), 1)
            if FACEKIT_INCLUDE.match(line) or FACEKIT_IMPURE_ID.search(line)]


def nvs_call_problems(text):
    """(line number, line) for every NVS call (a theme app reaches NVS only through the store)."""
    return [(i, line.strip()) for i, line in enumerate(text.splitlines(), 1) if DW_NVS_CALL.search(line)]


def tracked_path_problems(paths, allow=None):
    """The tracked-path guard (PR7): a message for every path under design/themes/, and every path under research/ that
    is not in allow (default RESEARCH_TRACKED_AT_BASE). The prefixes are compared without case (a checkout on a
    case-blind disk)."""
    allow, out = set(RESEARCH_TRACKED_AT_BASE if allow is None else allow), []
    for p in paths:
        low = p.lower()
        if low.startswith(DESIGN_THEMES):
            out.append('tracked file under %s (never tracked): %s' % (DESIGN_THEMES, p))
        elif low.startswith(RESEARCH) and p not in allow:
            out.append('tracked file under %s outside the e62bc30 allow-list: %s' % (RESEARCH, p))
    return out


def git_tracked_paths():
    """Every path `git ls-files` lists (the index: a staged file counts), or None when git fails (the guard then
    fails: it is fail-closed)."""
    try:
        r = subprocess.run(['git', '-C', REPO, 'ls-files', '-z'], capture_output=True)
    except OSError:
        return None
    if r.returncode != 0:
        return None
    return [p for p in r.stdout.decode('utf-8', 'surrogateescape').split('\0') if p]


RD_TREE = ('firmware/apps/rd/', 'tools/rd_', 'tools/export/rd_')


def rd_overlay_problems(tracked, cmake=()):
    """rd has no overlay (R153): a message for every tracked JSON object whose "theme" is rd and which carries
    "replaces" or "layers", every overlay-form file in rd's tree, and every use of the overlay variable in rd's CMake
    code (cmake: (path, text), comments blanked first)."""
    import json
    out = []
    for path, text in tracked:
        try:
            obj = json.loads(text)
        except ValueError:
            obj = None
        theme = str(obj.get('theme', '')).lower() if isinstance(obj, dict) else ''
        if theme == 'rd' and ('replaces' in obj or 'layers' in obj):
            out.append('%s is an overlay descriptor naming rd (rd has no overlay)' % path)
        elif path.startswith(RD_TREE) and overlay_form(path, text):
            out.append('%s has an overlay descriptor\'s form in rd\'s tree' % path)
    for path, text in cmake:
        for i, line in enumerate(cmake_code(text).splitlines(), 1):
            if 'ART_OVERLAY' in line:
                out.append('%s:%d: rd\'s CMake reads an art overlay: %s' % (path, i, line.strip()))
    return out


def rd_terms_agree(c, rt):
    """rd_terms_agree: tools/export/rd_terms.mjs (node) and tools/rd_terms.py give the same verdict on every corpus
    sample, from the same source and the same corpus; every category sample hits and every near-miss misses. Returns
    0, 1 (failed) or SKIP (no node: skipped, never passed; PR20)."""
    import json
    bad = len(c.failures)
    for text, want, what in rt.rd_selftest_cases():
        if rt.rd_forbidden(text) != want:
            c.fail('rd_terms_agree: Python %s %s' % (what, 'not caught' if want else 'caught'))
    node = shutil.which('node')
    if not node:
        print('SKIP: rd_terms_agree: node is not on PATH (the JS half skipped, not passed)')
        return SKIP if len(c.failures) == bad else 1
    r = subprocess.run([node, os.path.join(REPO, 'tools', 'export', 'rd_terms.mjs'), '--selftest'],
                       capture_output=True, text=True)
    try:
        js = json.loads(r.stdout) if r.returncode == 0 else None
    except ValueError:
        js = None
    if not isinstance(js, dict):
        c.fail('rd_terms_agree: rd_terms.mjs --selftest exit %d: %s' % (r.returncode, (r.stderr or r.stdout).strip()))
        return 1
    corpus = rt.rd_selftest_corpus()
    if (js.get('fold') or '').replace('\\/', '/') != rt.FOLD.pattern:
        c.fail('rd_terms_agree: rd_terms.mjs folds with another pattern than rd_terms.py')
    if js.get('source') != rt.RD_FORBIDDEN_SRC.decode():
        c.fail('rd_terms_agree: rd_terms.mjs builds another pattern than rd_terms.py')
    if js.get('corpus') != corpus:
        c.fail('rd_terms_agree: rd_terms.mjs\'s corpus is not rd_terms.py\'s (%d and %d samples)'
               % (len(js.get('corpus') or []), len(corpus)))
    else:
        for i, (text, hit) in enumerate(zip(corpus, js.get('hits') or [])):
            if bool(hit) != rt.rd_forbidden(text):
                c.fail('rd_terms_agree: sample %d: JS says %s, Python %s' % (i, bool(hit), not hit))
        if len(js.get('hits') or []) != len(corpus):
            c.fail('rd_terms_agree: rd_terms.mjs gave %d verdicts for %d samples' % (len(js.get('hits') or []),
                                                                                    len(corpus)))
    return 0 if len(c.failures) == bad else 1


def rd_terms_calibrated(c, rt):
    """rd_terms_calibrated (PR8): a lone creator surname misses and the pair hits; the common-word character name is
    not in the list; in the built scan (R159) a 3-letter token misses inside a printable run shorter than 8 characters
    and beside a word character (a digit or an underscore), and hits word-bounded in a run of 8 or more (while a
    4-letter term hits in a 4-character run)."""
    texts, runs = rt.calibration_cases()   # the samples, from pieces in tools/rd_terms.py (public release Task 5)
    for text, want, what in texts:
        if bool(rt.RD_FORBIDDEN_TEXT.search(text)) != want:
            c.fail('rd_terms_calibrated: %s %s' % (what, 'not caught' if want else 'caught'))
    if re.search(r'(?<![a-z])' + rt.calibration_excluded() + r'(?![a-z])', rt.RD_FORBIDDEN_SRC.decode(), re.I):
        c.fail('rd_terms_calibrated: the common-word character name is in the list')
    for data, want, what in runs:
        if bool(rt.rd_bytes_hits(data)) != want:
            c.fail('rd_terms_calibrated: %s %s' % (what, 'not caught' if want else 'caught'))
    # pixel and sound payloads are blanked before the scan (PR35): only the .dat file's term is left to hit
    files = rt.calibration_image_files()
    img = build_mmap_image(files)
    got = rt.rd_bytes_hits(blanked(img, PIXEL_EXTS))
    if len(got) != 1 or len(rt.rd_bytes_hits(img)) != 3:
        c.fail('rd_terms_calibrated: the image blanking (%r)' % got)
    print('rd calibration: %d texts, %d runs, %d image files' % (len(texts), len(runs), len(files)))


def text_files(trees, globs, assets_app=None, repo=REPO):
    """The files a text scan reads: trees (skipping build output as the other scans do), the text files anywhere under
    firmware/apps/<assets_app>/assets, then globs (each sorted). A tree or glob that matches nothing adds nothing: the
    docs globs match nothing in a public tree, which leaves docs/ out (text_files_selftest pins this for the trees and
    globs; the assets_app branch reads the real APPS_DIR, not repo)."""
    import glob
    seen = []
    for top in trees:
        seen += list(our_files(os.path.join(repo, top)))
    if assets_app:
        a = os.path.join(APPS_DIR, assets_app, 'assets')
        seen += sorted(os.path.join(root, f) for root, _, files in os.walk(a) for f in files)
    for g in globs:
        seen += sorted(p for p in glob.glob(os.path.join(repo, g)) if os.path.isfile(p))
    return list(dict.fromkeys(seen))


def text_files_selftest(c):
    """text_files_tolerates_absence (public release Task 13): RD_TEXT_TREES/RD_TEXT_GLOBS and DW_TEXT_TREES/
    DW_TEXT_GLOBS, read against a temp tree that holds only tools/rd_x.py (no docs/, none of the scanned trees, as a
    public export has no docs/superpowers or docs/*-acceptance.md), list just that one file (rd) and nothing (dw), and
    raise nothing. assets_app is None here: the assets branch is not covered."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        one = os.path.join(tmp, 'tools', 'rd_x.py')
        os.makedirs(os.path.dirname(one))
        open(one, 'w').close()
        try:
            rd = text_files(RD_TEXT_TREES, RD_TEXT_GLOBS, None, repo=tmp)
            dw = text_files(DW_TEXT_TREES, DW_TEXT_GLOBS, None, repo=tmp)
        except Exception as e:  # noqa: BLE001
            c.fail('text_files_tolerates_absence: raised %s: %s' % (type(e).__name__, e))
            return
        if rd != [one] or dw != []:
            c.fail('text_files_tolerates_absence: rd %r, dw %r (want only %s, then nothing)' % (rd, dw, one))


def read_text(p):
    """A file's text, or None for a binary file (a NUL in its first 8 KiB)."""
    with open(p, 'rb') as f:
        data = f.read()
    return None if b'\0' in data[:8192] else data.decode('utf-8', 'replace')


def rd_terms_zero_hits_today(c, rt):
    """rd_terms_zero_hits_today (PR8 e), which is also the static rd scan: tools/rd_terms.py's list over RD_TEXT_TREES
    (apps/rd/assets' text files too) and RD_TEXT_GLOBS, the plan and the spec included. Any hit fails."""
    files = text_files(RD_TEXT_TREES, RD_TEXT_GLOBS, 'rd')
    n = 0
    for p in files:
        text = read_text(p)
        if text is None:
            continue
        n += 1
        for i, line in rt.rd_text_hits(text):
            c.fail('rd term: %s:%d: %s' % (rel(p), i, line))
    print('rd scan: %d text files' % n)


SELF_MATCH_FILES = ('tools/rd_terms.py', 'tools/export/rd_terms.mjs', 'tools/check_apps.py', 'tools/dw_terms.py',
                    'tools/terms.py', 'tools/export/dw_terms.mjs', 'tools/export/terms.mjs')


def rd_selftests(c, dt, rt):
    """The rest of the rd term self-tests (Task 5 step 1): the twins agree, and no file holding a list (nor this one)
    matches either list (dt: tools/dw_terms.py, or None when it is missing: then rd's list alone)."""
    rd_terms_agree(c, rt)
    n = 0
    for name in SELF_MATCH_FILES:
        p = os.path.join(REPO, *name.split('/'))
        if not os.path.isfile(p):
            if not terms.public_tree(REPO):
                c.fail('rd_terms_not_self_matching: %s is missing' % name)
            continue
        n += 1
        text = read_text(p) or ''
        for i, line in rt.rd_text_hits(text) + (text_hits(rt, dt.FORBIDDEN_TEXT, text) if dt else []):
            c.fail('rd_terms_not_self_matching: %s:%d: %s' % (name, i, line))
    print('rd_terms_not_self_matching: %d files' % n)


def rule_selftests(c):
    """The rd wave 1 Task 5 self-tests that need no list (they run in a public tree too): tracked_design_fails,
    rd_overlay_fails, facekit_neutral, rd_nvs_call_fails, facekit_pure_comments and the rd sound rule."""
    research = RESEARCH_TRACKED_AT_BASE
    if not research and terms.public_tree(REPO):   # a public tree has no allow-list: a neutral stand-in for the test
        research = ('research/board/x.md', 'research/omni/images/y.jpg')
    allowed = [p for p in research if p.startswith('research/board/')][:1] + \
              [p for p in research if p.startswith('research/omni/')][:1]
    for paths, want, what in [
            (['firmware/x.c', 'design/themes/x/notes.md'], True, 'a design package file'),
            (['Design/Themes/x/a.png'], True, 'a design package file, other case'),
            (['research/newtheme/x.md'], True, 'a new research path'),
            (['firmware/x.c'] + allowed, False, 'only allow-listed research paths'),
            (['design/BRIEF.md', 'design/mockups/index.html'], False, 'the Omni\'s design files'),
            (['docs/research/x.md', 'tools/design/themes.py'], False, 'the words elsewhere in a path')]:
        if len(allowed) != 2 or bool(tracked_path_problems(paths, research)) != want:
            c.fail('tracked_design_fails: %s %s' % (what, 'not caught' if want else 'caught'))
    if (RESEARCH_TRACKED_AT_BASE or not terms.public_tree(REPO)) and \
            (len(RESEARCH_TRACKED_AT_BASE) != 47 or len(set(RESEARCH_TRACKED_AT_BASE)) != 47):
        c.fail('tracked_design_fails: RESEARCH_TRACKED_AT_BASE (tools/research_tracked.txt) holds %d paths, e62bc30 '
               'tracked 47' % len(set(RESEARCH_TRACKED_AT_BASE)))
    over = '{"theme": "rd", "replaces": ["rd_x.bin"], "layers": "l.json"}'
    for tracked, cmake, want, what in [
            ([('a/b.json', over)], [], True, 'a descriptor naming rd'),
            ([('a/b.json', '{"theme": "RD", "replaces": ["x.bin"]}')], [], True, 'a partial descriptor naming RD'),
            ([('firmware/apps/rd/' + OVERLAY_JSON, '{}')], [], True, 'an overlay-named file in rd\'s tree'),
            ([('a/b.json', over.replace('"rd"', '"dw"'))], [], False, 'dw\'s descriptor form'),
            ([('a/b.json', '{"theme": "rd", "faces": 6}')], [], False, 'an ordinary json naming rd'),
            ([], [('firmware/apps/rd/main/CMakeLists.txt', 'if(DW_ART_OVERLAY)\nendif()\n')], True,
             'rd\'s CMake reading the overlay'),
            ([], [('firmware/apps/rd/main/CMakeLists.txt', '# no DW_ART_OVERLAY here\n')], False,
             'a CMake comment naming it')]:
        if bool(rd_overlay_problems(tracked, cmake)) != want:
            c.fail('rd_overlay_fails: %s %s' % (what, 'not caught' if want else 'caught'))
    for text, want, what in [('void f(void) { dw_blit(); }\n', True, 'dw_blit'), ('int rd_x;\n', True, 'rd_x'),
                             ('#define RD_N 3\n', True, 'RD_N'), ('/* see dial_view */\n', True, 'an Omni view'),
                             ('static int s_dw_x;\n', True, 's_dw_x (R159: an embedded prefix)'),
                             ('static int g_RD_N;\n', True, 'g_RD_N (R159)'), ('int s_dial_view;\n', True, 's_dial_view'),
                             ('int bird_x, x3rd_y;\n', False, 'a letter or digit before the letters'),
                             ('int fk_x; /* the draw */\n', False, 'face_kit\'s own name')]:
        if bool(facekit_neutral_problems(text)) != want:
            c.fail('facekit_neutral: %s %s' % (what, 'not caught' if want else 'caught'))
    for text, want, what in [('static int x = nvs_get_u8(h, "k", &v);\n', True, 'nvs_get_u8('),
                             ('/* the store owns NVS: no nvs_* here */\n', False, 'nvs_* in prose'),
                             ('x = store_local_ext(b, n);\n', False, 'a store call')]:
        if bool(nvs_call_problems(text)) != want:
            c.fail('rd_nvs_call_fails: %s %s' % (what, 'not caught' if want else 'caught'))
    for text, want, what in [
            ('/* a free-running count, as esp_timer\'s */\nint x;\n', False, 'esp_timer in a block comment'),
            ('int x;   // esp_rom_crc32_le\'s CRC\n', False, 'esp_rom_crc32_le in a line comment'),
            ('/* Pure: no esp_*, freertos or LVGL.\n * no lvgl.h */\n', False, 'the purity note over two lines'),
            ('#include "esp_timer.h"\n', True, 'an esp_timer.h include'),
            ('#include <freertos/FreeRTOS.h>\n', True, 'a freertos include'),
            ('# include "lvgl.h"\n', True, 'an lvgl.h include'),
            ('int64_t t = esp_timer_get_time();\n', True, 'a call to esp_timer_get_time'),
            ('const char *s = "/* x */"; int t = esp_log_ts();\n', True, 'a call after a string holding a comment'),
            ('#include "fk/face.h"\n', False, 'face_kit\'s own include')]:
        if bool(facekit_pure_problems(text)) != want:
            c.fail('facekit_pure_comments: %s %s' % (what, 'not caught' if want else 'caught'))
    # the rd sound rule: with no names (a theme without its tool) any audio file fails and the backgrounds pass; with
    # gen_sfx_rd.py's two voicings (Task 8), exactly those two
    for files, want, what in [([('bg_bezel.bin', 10 ** 6)], 0, 'the backgrounds'),
                              ([('rd_chime.pcm', 6000)], 1, 'an unnamed .pcm'),
                              ([('sub/x.wav', 10)], 1, 'a .wav in a subdirectory')]:
        if len(dw_sound_problems(files, set(), 'rd')) != want:
            c.fail('rd sound rule self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    rd_names = dw_pcm_names('rd')
    if rd_names != {'rd_chime.pcm', 'rd_chime_r.pcm'}:
        c.fail('rd sound rule self-test: gen_sfx_rd.py names %s, not the two voicings' % sorted(rd_names))
    two = [('rd_chime.pcm', 76800), ('rd_chime_r.pcm', 76800), ('bg_bezel.bin', 10 ** 6)]
    for files, want, what in [(two, 0, 'the two voicings'),
                              (two[:1], 1, 'the recipe voicing missing'),
                              (two + [('rd_ping.pcm', 100)], 1, 'a third .pcm'),
                              (two + [('rd_chime.wav', 100)], 1, 'a .wav beside them'),
                              ([('rd_chime.pcm', 128002), two[1]], 1, 'one over the size cap')]:
        if len(dw_sound_problems(files, rd_names, 'rd')) != want:
            c.fail('rd sound rule self-test (two voicings): %s %s' % (what, 'not caught' if want else 'caught'))


DW_PCM_MAX = 128000
AUDIO_EXT = ('.pcm', '.wav', '.wave', '.mp3', '.ogg', '.oga', '.opus', '.flac', '.aac', '.m4a', '.aif', '.aiff',
             '.wma', '.mid', '.midi', '.raw', '.snd', '.au', '.amr', '.ape', '.webm')


def dw_pcm_names(theme='dw'):
    """The file names tools/gen_sfx_<theme>.py synthesises: dw's NAMES, each with .pcm; rd's VOICINGS' files (rd wave 1
    Task 8: rd_chime.pcm, the audible default, and rd_chime_r.pcm, the recipe). A theme other than dw without its tool
    has none: every audio file under its assets then fails."""
    tool = os.path.join(REPO, 'tools', 'gen_sfx_%s.py' % theme)
    if theme != 'dw' and not os.path.isfile(tool):
        return set()
    spec = importlib.util.spec_from_file_location('gen_sfx_' + theme, tool)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.join(REPO, 'tools'))
    keep, sys.dont_write_bytecode = sys.dont_write_bytecode, True   # no tools/__pycache__ left behind
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.dont_write_bytecode = keep
        sys.path.pop(0)
    if theme == 'rd':
        return set(mod.VOICINGS.values())
    return {n + '.pcm' for n in mod.NAMES}


def dw_sound_problems(files, names, theme='dw'):
    """files: (relative name, size in bytes) under apps/<theme>/assets. Returns a message per violation: a named .pcm
    that is missing, a .pcm that gen_sfx_<theme>.py does not name (nor SHARED_UI_SOUNDS, the keyboard's tick, at the
    top level), one over DW_PCM_MAX, or any other audio file."""
    out = ['sound missing from the %s assets: %s' % (theme, n) for n in sorted(names - {f for f, _ in files})]
    for f, size in files:
        base = os.path.basename(f)
        if not base.lower().endswith(AUDIO_EXT):
            continue
        if not ((base in names or base in SHARED_UI_SOUNDS) and f == base):
            out.append('sound in the %s assets that gen_sfx_%s.py does not name: %s' % (theme, theme, f))
        elif size > DW_PCM_MAX:
            out.append('sound in the %s assets over %d bytes: %s (%d)' % (theme, DW_PCM_MAX, f, size))
    return out


def restore_problems(files):
    """The restore guard: files is [(repo-relative path, text)] over the scope. A message per problem: a wrapper call,
    a restore call outside RESTORE_HOME or outside its `case CMD_FORGET:` block (the nearest case label above it in the
    code with comments blanked, with no function's closing brace between), or a count other than one."""
    out, hits = [], []
    for path, text in files:
        code = c_code(text).splitlines()
        for i, line in enumerate(text.splitlines(), 1):
            if RESTORE_WRAPPER.search(line):
                out.append('restore wrapper called: %s:%d: %s' % (path, i, line.strip()))
            if not RESTORE_CALL.search(line):
                continue
            hits.append((path, i))
            label = None
            for j in range(i - 1, -1, -1):
                cl = code[j] if j < len(code) else ''
                if j < i - 1 and cl.startswith('}'):
                    break   # left the function: no case encloses the call
                m = CASE_LABEL.search(cl)
                if m:
                    label = m.group(1) or 'default'
                    break
            if path != RESTORE_HOME or label != RESTORE_CASE:
                out.append('restore call outside %s case %s: %s:%d: %s' % (RESTORE_HOME, RESTORE_CASE, path, i,
                                                                           line.strip()))
    if len(hits) != 1:
        out.append('restore call count %d, want exactly 1 (%s)' % (len(hits), ', '.join('%s:%d' % h for h in hits)))
    return out


def call_args(code, start):
    """The top-level arguments of the call whose '(' is at code[start], as strings (string literals kept whole), and
    the index after its ')'; (None, start) when it does not close."""
    args, depth, i, cur = [], 0, start, []
    n = len(code)
    while i < n:
        ch = code[i]
        if ch in '"\'':
            j = i + 1
            while j < n and code[j] != ch:
                j += 2 if code[j] == '\\' else 1
            cur.append(code[i:j + 1])
            i = j + 1
            continue
        if ch in '([{':
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif ch in ')]}':
            depth -= 1
            if depth == 0:
                args.append(''.join(cur).strip())
                return args, i + 1
        elif ch == ',' and depth == 1:
            args.append(''.join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    return None, start


def secret_problems(path, text):
    """The secret guard (I3; fix round 1): (line, call text) for each printing call that shows a value (%s, %.*s,
    %c, %x, %02x, %d, ...; puts, the ESP_LOG_BUFFER_* family, or a format that is not a literal, always) with an
    argument naming SECRET_NAMES. An snprintf whose destination's leading identifier is exactly one of SECRET_DEST is
    skipped (it builds the secret: the PoP builder)."""
    code = c_code(text)
    out = []
    for m in PRINT_CALL.finditer(code):
        fn = m.group(1)
        args, end = call_args(code, m.end() - 1)
        if args is None:
            continue
        if fn == 'snprintf':
            dest = re.match(r'\s*\(?\s*(?:\(\s*char\s*\*\s*\))?\s*([A-Za-z_]\w*)\b(?!\s*[.\[]|\s*->)',
                            args[0]) if args else None
            if dest and dest.group(1) in SECRET_DEST:
                continue   # builds the secret (the PoP builder), does not show it
            args = args[1:]
        elif fn == 'fprintf' or fn.startswith('ESP_'):
            args = args[1:]   # the stream, the tag
        lits = [a for a in args if a.startswith('"')]
        fmt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', ' '.join(lits)))
        prints_str = fn == 'puts' or fn.startswith('ESP_LOG_BUFFER_') or not lits or bool(PRINT_STR.search(fmt))
        named = [a for a in args if not a.startswith('"') and SECRET_NAMES.search(a)]
        if prints_str and named:
            out.append((code.count('\n', 0, m.start()) + 1, ' '.join(code[m.start():end].split())))
    return out


def secret_restore_selftest(c):
    """The restore and secret guards catch what they must and pass what they must (Minor 15: the call names are
    split here as in the patterns)."""
    call = 'esp_wifi' + '_restore'
    wrap = 'network_prov_mgr_reset' + '_wifi_provisioning'
    net = ('static void handle(const cmd_t *c)\n{\n    switch (c->kind) {\n    case CMD_SYNC: do_sync(); break;\n'
           '    case CMD_FORGET:\n        if (wifi_up() == ESP_OK) {\n            %s(); /* drops it */\n'
           '            wifi_off();\n        }\n        break;\n    case CMD_FORCE: break;\n    }\n}\n' % call)
    good = [(RESTORE_HOME, net), ('firmware/components/watch_ui/x.c', 'int x;\n')]
    if restore_problems(good):
        c.fail('restore self-test: the one call in CMD_FORGET caught: %r' % restore_problems(good))
    cases = [
        ('a second call elsewhere', good + [('firmware/components/watch_ui/x.c', 'void f(void) { %s(); }\n' % call)]),
        ('a call moved into another case', [(RESTORE_HOME, net.replace('    case CMD_FORGET:\n', '    case CMD_X:\n'))]),
        ('a call moved below the switch', [(RESTORE_HOME, net.replace('%s(); /* drops it */' % call, 'x();') +
                                            'static void g(void)\n{\n    %s();\n}\n' % call)]),
        ('a call in another file only', [(RESTORE_HOME, net.replace(call, 'x')),
                                         ('firmware/components/net/other.c', '    case CMD_FORGET: %s();\n' % call)]),
        ('no call at all', [(RESTORE_HOME, net.replace(call, 'x'))]),
        ('a wrapper call', good + [('firmware/components/net/y.c', 'void f(void) { %s(); }\n' % wrap)]),
        ('a wrapper call in a tool', good + [('tools/z.py', 'run("%s ()")\n' % wrap)]),
    ]
    for what, files in cases:
        if not restore_problems(files):
            c.fail('restore self-test: %s not caught' % what)
    hits = [
        'printf("%s\\n", s_trial_pass);', 'ESP_LOGI(TAG, "snap %.*s", 32, (char *)s_snap.sta.ssid);',
        'printf("pop %s", s_pop);', 'printf("pop %s", pop);', 'ESP_LOGW(TAG, "%s", pass);',
        'printf("%-8s", cfg.sta.password);', 'fprintf(stderr, "%s", edit.buf);', 'puts(pass);',
        'printf(pass);', 'snprintf(out, sizeof out, "x%s", pop);', 'ESP_LOGI(TAG, "a" "%s" "b", s_pop);',
        'ESP_LOG_BUFFER_HEX(TAG, s_trial_pass, n);', 'ESP_LOG_BUFFER_CHAR(TAG, pass, len);',
        'ESP_LOG_BUFFER_HEXDUMP(TAG, &s_snap, sizeof s_snap, ESP_LOG_INFO);',
        'ESP_LOG_BUFFER_HEX_LEVEL(TAG, s_pop, 32, ESP_LOG_WARN);', 'ESP_LOG_BUFFER_CHAR_LEVEL(TAG, pop, 32, ESP_LOG_INFO);',
        'esp_rom_printf("%s", s_trial_pass);', 'ets_printf("%s\\n", pass);', 'printf("%c", pass[i]);',
        'printf("%02x", s_pop[i]);', 'ESP_LOGI(TAG, "%x", (unsigned)s_trial_pass[0]);',
        'snprintf(s_snapshot_copy, sizeof s_snapshot_copy, "%s", s_trial_pass);',
        'snprintf(pass_buf, n, "%s", pass);', 'snprintf(cfg.sta.password, 64, "%s", pass);',
    ]
    for h in hits:
        if len(secret_problems('t.c', 'void f(void)\n{\n    %s\n}\n' % h)) != 1:
            c.fail('secret self-test: not caught: %s' % h)
    allowed = [
        'printf("len %u\\n", pass_len);', 'ESP_LOGI(TAG, "pass %u", (unsigned)pass_len);',
        'snprintf(pop + 2 * i, 3, "%02x", b[i]);', 'snprintf(pass, sizeof pass, "%s", src);',
        'printf("%s\\n", net_state_name(st));', '/* printf("%s", pass); */', 'printf("%d", pop_count);',
        'ESP_LOGI(TAG, "trial: reason %u", r);', 'snprintf(pop + 2 * i, 3, "%02x", b[i]);',
        'snprintf((char *)pass, 65, "%s", src);', 'ESP_LOG_BUFFER_HEX(TAG, b, n);', 'printf("%02x", b[i]);',
        'esp_rom_printf("%u", pass_len);',
    ]
    for a in allowed:
        if secret_problems('t.c', 'void f(void)\n{\n    %s\n}\n' % a):
            c.fail('secret self-test: allowed line caught: %s' % a)


def shared_ui_problems(src, app_dirs):
    """A message per problem: src (watch_ui/assets) holding other than SHARED_UI_ASSETS, or an app directory whose copy
    of one is missing or not byte-equal to the source's."""
    have = sorted(os.listdir(src)) if os.path.isdir(src) else []
    out = ['%s: missing from %s' % (n, rel(src)) for n in SHARED_UI_ASSETS if n not in have]
    out += ['%s: in %s but not a shared asset (SHARED_UI_ASSETS)' % (n, rel(src)) for n in have
            if n not in SHARED_UI_ASSETS]
    for d in app_dirs:
        for n in SHARED_UI_ASSETS:
            a, b = os.path.join(src, n), os.path.join(d, n)
            if os.path.isfile(a) and not (os.path.isfile(b) and filecmp.cmp(a, b, shallow=False)):
                out.append('%s is not byte-equal to %s' % (rel(b), rel(a)))
    return out


def shared_ui_selftest(c):
    """SHARED_UI_ASSETS byte-equality across the four directories, and kb_tick.pcm in the dw and rd sound rule."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, 'src')
        apps = [os.path.join(td, a) for a in REQUIRED_APPS]
        for d in [src] + apps:
            os.makedirs(d)
            for i, n in enumerate(SHARED_UI_ASSETS):
                open(os.path.join(d, n), 'wb').write(bytes([i]) * 10)
        if shared_ui_problems(src, apps):
            c.fail('shared asset self-test: four equal directories caught: %r' % shared_ui_problems(src, apps))
        os.remove(os.path.join(apps[1], SHARED_UI_ASSETS[2]))
        if len(shared_ui_problems(src, apps)) != 1:
            c.fail('shared asset self-test: a missing copy not caught')
        open(os.path.join(apps[1], SHARED_UI_ASSETS[2]), 'wb').write(bytes([2]) * 10)
        open(os.path.join(apps[2], SHARED_UI_ASSETS[4]), 'r+b').write(b'\xff')
        if len(shared_ui_problems(src, apps)) != 1:
            c.fail('shared asset self-test: a one-byte change not caught')
        open(os.path.join(apps[2], SHARED_UI_ASSETS[4]), 'wb').write(bytes([4]) * 10)
        os.remove(os.path.join(src, SHARED_UI_ASSETS[0]))
        if len(shared_ui_problems(src, apps)) != 1:
            c.fail('shared asset self-test: a source missing not caught')
        open(os.path.join(src, SHARED_UI_ASSETS[0]), 'wb').write(bytes([0]) * 10)
        open(os.path.join(src, 'kb_extra.bin'), 'wb').write(b'x')
        if len(shared_ui_problems(src, apps)) != 1:
            c.fail('shared asset self-test: a sixth source file not caught')
    for theme, names in (('dw', {'dw_tick.pcm'}), ('rd', {'rd_chime.pcm'})):
        base = [(n, 6000) for n in sorted(names)]
        for files, want, what in [(base + [('kb_tick.pcm', 800)], 0, 'kb_tick.pcm'),
                                  (base + [('kb_tock.pcm', 800)], 1, 'another new .pcm'),
                                  (base + [('sub/kb_tick.pcm', 800)], 1, 'kb_tick.pcm in a subdirectory'),
                                  (base + [('kb_tick.pcm', DW_PCM_MAX + 2)], 1, 'kb_tick.pcm over the size cap')]:
            if len(dw_sound_problems(files, names, theme)) != want:
                c.fail('shared sound self-test (%s): %s %s' % (theme, what, 'not caught' if want else 'caught'))


def scan_selftest(c):
    names = {'dw_chime.pcm', 'dw_ping.pcm', 'dw_latch.pcm', 'dw_tick.pcm'}
    four = [(n, 6000) for n in sorted(names)]
    sounds = [
        (four, 0, 'the four named files'),
        (four + [('x.pcm', 100)], 1, 'a stray x.pcm'),
        (four + [('dw_chime.wav', 100)], 1, 'a .wav'),
        (four + [('DW_PING.MP3', 100)], 1, 'an upper-case .mp3'),
        (four + [('sub/dw_tick.pcm', 100)], 1, 'a named file in a subdirectory'),
        ([('dw_chime.pcm', DW_PCM_MAX), ('dw_ping.pcm', DW_PCM_MAX + 2),
          ('dw_latch.pcm', 100), ('dw_tick.pcm', 100)], 1, 'one file over the size cap'),
        (four + [('bg_bezel.bin', 10 ** 6)], 0, 'a background'),
        (four[1:], 1, 'a missing named file'),
        ([], 4, 'no sounds at all'),
    ]
    for files, want, what in sounds:
        if len(dw_sound_problems(files, names)) != want:
            c.fail('sound rule self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    prompts = [
        (b'\x00' + OMNI_PROMPT + b'\x00', True, 'the Omni prompt as a NUL-delimited string'),
        (b'xx' + OMNI_PROMPT, True, 'the Omni prompt at the end of the data'),
        (b'dw> \x00', False, 'the dw prompt'),
        (b'omni>\x00omni \x00', False, 'the pieces without the space'),
        (b'omni_boot> ', False, 'another name ending in "> "'),
    ]
    for data, want, what in prompts:
        if bool(omni_prompt_hits(data)) != want:
            c.fail('prompt self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    # --publishable (dw wave 1 Task 9, G1): the art marker, the packed files against the committed ones, overlay forms
    good = {'dw_doors_m.bin': b'\x19generic', 'dw_chime.pcm': b'\x00\x01', 'dw_ping.pcm': b'\x00\x03'}
    pubs = [
        ('generic\n', dict(good), [], 0, 'a generic build'),
        ('overlay 1a2b3c4\n', dict(good), [], 1, 'dw_art.txt reading overlay'),
        ('', dict(good), [], 1, 'an empty dw_art.txt'),
        ('generic\n', dict(good, **{'dw_doors_m.bin': b'\x19other'}), [], 1, 'a packed file unlike the committed one'),
        ('generic\n', dict(good, **{'x.bin': b'\x19'}), [], 1, 'a packed file that is not committed'),
        ('generic\n', {k: v for k, v in good.items() if k != 'dw_chime.pcm'}, [], 1, 'a committed file not packed'),
        ('generic\n', dict(good), [('a/b.json', '{"theme": "dw", "replaces": ["dw_doors_m.bin"], "layers": "l.json"}')],
         1, 'a tracked file in overlay.json\'s form'),
        ('generic\n', dict(good), [('x/' + OVERLAY_JSON, '{}')], 1, 'a tracked file named like overlay.json'),
        ('generic\n', dict(good), [('t/dw_layers.json', '{"rows": []}')], 0, 'an ordinary tracked json'),
    ]
    for art, packed, tracked, want, what in pubs:
        if len(publishable_problems(art, packed, good, tracked)) != want:
            c.fail('publishable self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    # wave 2 Task 4: the Classic doors (dw_doors_c.bin) are the overlay's third file, and still publishable-checked
    good3 = dict(good, **{'dw_doors_c.bin': b'\x19blue', 'dw_b5_box.bin': b'\x19front'})
    if publishable_problems('generic\n', dict(good3), good3, []):
        c.fail('publishable self-test: the three swap files equal to the committed ones caught')
    if len(publishable_problems('generic\n', dict(good3, **{'dw_doors_c.bin': b'\x19call'}), good3, [])) != 1:
        c.fail('publishable self-test: a packed dw_doors_c.bin unlike the committed one not caught')
    # the overlay's descriptor (its replaces) and a private image's swapped files: DW_SWAP alone (R132)
    reps = [
        (['dw_doors_m.bin', 'dw_b5_box.bin'], 0, 'the wave-1 pair'),
        (['dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin'], 0, 'the three swap files'),
        (['dw_doors_c.bin'], 0, 'the Classic doors alone'),
        (list(DW_SWAP), 0, 'the six swap files (public release Task 9)'),
        (['dw_th_b5.bin', 'dw_chime.pcm'], 0, 'a thumbnail and the chime alone'),
        (['dw_doors_m.bin', 'dw_b5_box.bin', 'dw_doors_c.bin', 'dw_extra.bin'], 1, 'a fourth, new name'),
        (['dw_ping.pcm'], 1, 'a sound that is no swap file'),
        (['dw_doors_x.bin'], 1, 'a new name alone'),
        (['dw_glow.bin'], 1, 'a generic file that is no swap file'),
        (['dw_doors_c.bin', 'dw_doors_c.bin'], 1, 'a name listed twice'),
        ([], 1, 'an empty list'),
        ('dw_doors_c.bin', 1, 'a string, not a list'),
    ]
    for replaces, want, what in reps:
        if len(replaces_problems({'theme': 'dw', 'replaces': replaces, 'layers': 'l.json'})) != want:
            c.fail('overlay replaces self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    swaps = [
        (dict(good3), 0, 'a private image equal to the committed files'),
        (dict(good3, **{'dw_doors_c.bin': b'\x19call', 'dw_doors_m.bin': b'\x19box'}), 0, 'two swap files replaced'),
        (dict(good3, **{'dw_ping.pcm': b'\x00\x02'}), 1, 'a non-swap file replaced'),
        (dict(good3, **{'dw_chime.pcm': b'\x00\x02'}), 0, 'the chime replaced (public release Task 9)'),
        (dict(good3, **{'dw_extra.bin': b'\x19'}), 1, 'an extra file in the image'),
        ({k: v for k, v in good3.items() if k != 'dw_doors_c.bin'}, 1, 'a committed file missing from the image'),
    ]
    for packed, want, what in swaps:
        if len(swap_problems(packed, good3)) != want:
            c.fail('private image self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    hdr = '#define MMAP_ASSETS_FILES           22\n#define MMAP_ASSETS_CHECKSUM        0x1A2B\n'
    sums = [((22, 0x1A2B), 0, 'the descriptor equal to the header'),
            ((22, 0x1A2C), 1, 'a descriptor checksum other than the header\'s'),
            ((21, 0x1A2B), 1, 'a descriptor file count other than the header\'s'),
            (None, 1, 'no descriptor values')]
    for desc, want, what in sums:
        if len(checksum_problems(desc, parse_mmap_header(hdr))) != want:
            c.fail('descriptor checksum self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    # the mmap image: every file at an even (64-aligned) address (the scan's half is dw_term_selftest's)
    img = build_mmap_image([('a.bin', b'\x19' * 7), ('b.pcm', b'xx zzneutralmarkzz xx'), ('c.bin', b'\x00the x\x00')])
    ents = mmap_entries(img)
    if [e[0] for e in ents] != ['a.bin', 'b.pcm', 'c.bin'] or any(e[1] % 64 for e in ents):
        c.fail('mmap self-test: entries %r' % ents)


def dw_term_selftest(c, dt, rt):
    """The dw list's self-tests (dt: tools/dw_terms.py; rt: tools/rd_terms.py, whose fold the text scan shares): the
    byte scan over printable runs, the text scan, a name wrapped across a line break, and the mmap image's .pcm payload
    skipped by the scan. The cases live in tools/dw_terms.py (public release Task 5)."""
    cases = dt.scan_cases()
    for data, want, what in cases:
        if bool(forbidden_hits(data, dt.FORBIDDEN)) != want:
            c.fail('scan self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    texts = dt.text_cases()
    for t, want in texts:
        if bool(text_hits(rt, dt.FORBIDDEN_TEXT, 'x = "%s"\n' % t)) != want:
            c.fail('text scan self-test: %r %s' % (t, 'not caught' if want else 'caught'))
    # R164: a name wrapped across a line break, plain and behind each comment or quote leader, is caught at the line
    # where it starts (line 2 here); the ordinary words wrapped are not
    wrapped = dt.wrapped_cases()
    for t, want, what in wrapped:
        got = [i for i, _ in text_hits(rt, dt.FORBIDDEN_TEXT, t)]
        if want != bool(got) or (want and got != [2]):
            c.fail('text scan self-test: a name wrapped behind %s: lines %r' % (what, got))
    # the mmap image: .pcm payloads skipped by the scan, the .bin's name found
    files = dt.mmap_files()
    img = build_mmap_image(files)
    ents = mmap_entries(img)
    if [e[0] for e in ents] != ['a.bin', 'b.pcm', 'c.bin'] or any(e[1] % 64 for e in ents):
        c.fail('mmap self-test: entries %r' % ents)
    if len(image_forbidden_hits(img, dt.FORBIDDEN)) != 1:
        c.fail('mmap self-test: the .pcm payload not skipped, or the .bin hit missed (%r)'
               % image_forbidden_hits(img, dt.FORBIDDEN))
    print('dw term self-tests: %d byte cases, %d text cases, %d wrapped cases, %d mmap files'
          % (len(cases), len(texts), len(wrapped), len(files)))


def skipped_dirs():
    out = {os.path.join(REPO, p) for p in SKIP_FIXED}
    for d in apps().values():
        out.update(os.path.join(d, p) for p in SKIP_PER_APP)
        out.update(os.path.join(d, b) for b in os.listdir(d) if b == 'build' or b.startswith('build-'))
    return out


def our_files(top):
    skip = skipped_dirs()
    for root, dirs, files in os.walk(top):
        dirs[:] = [d for d in dirs if os.path.join(root, d) not in skip]
        for f in files:
            yield os.path.join(root, f)


def dw_text_files():
    """The files the trademark scan reads: DW_TEXT_TREES (skipping build output as the other scans do, but reading
    apps/dw/assets' text files), then DW_TEXT_GLOBS."""
    return text_files(DW_TEXT_TREES, DW_TEXT_GLOBS, 'dw')


# ------------------------------------------------------------------ static
def check_static(c):
    # rd wave 1 Task 5: the rd list's calibration and its zero-hit scan over today's tree run first (PR8). The lists
    # come from the private modules (public release Task 5): a public tree skips their scans, anywhere else a missing
    # one fails (load_terms records it). A module that raises is a failure too, never a pass.
    dt, rt = load_terms(c, 'static')
    if dt is None and rt is None and terms.public_tree(REPO):
        print(SKIP_STATIC_TERMS)
    try:
        if rt is not None:
            rd_terms_calibrated(c, rt)
            rd_terms_zero_hits_today(c, rt)
            rd_selftests(c, dt, rt)
            if dt is not None:   # dw's text scan shares rd_terms' fold
                dw_term_selftest(c, dt, rt)
    except Exception as e:  # noqa: BLE001  (a broken module fails the run; it never skips a scan silently)
        c.fail('a term module raised %s: %s (the term scans did not finish)' % (type(e).__name__, e))
        dt = rt = None
    scan_selftest(c)
    text_files_selftest(c)
    rule_selftests(c)
    shared_ui_selftest(c)
    secret_restore_selftest(c)

    # the tracked-path guard (PR7): fail-closed, a git failure fails
    paths = git_tracked_paths()
    if paths is None:
        c.fail('tracked-path guard: `git ls-files` failed, so no tracked design or research file can be ruled out')
    else:
        for msg in tracked_path_problems(paths):
            c.fail('tracked-path guard: ' + msg)
        rd_cmake = [(rel(p), open(p).read()) for p in our_files(os.path.join(APPS_DIR, 'rd'))
                    if os.path.basename(p) == 'CMakeLists.txt' or p.endswith('.cmake')] \
            if os.path.isdir(os.path.join(APPS_DIR, 'rd')) else []
        for msg in rd_overlay_problems(tracked_json(), rd_cmake):
            c.fail('rd overlay: ' + msg)

    # the frozen table
    if not os.path.isfile(TABLE):
        c.fail('%s is missing' % rel(TABLE))
    else:
        rows = parse_table(TABLE)
        if rows != FROZEN:
            c.fail('%s is not the frozen table:\n  got  %s\n  want %s' % (rel(TABLE), rows, FROZEN))

    found = present_apps(c)
    allowed = allow_list()

    # the swap files: the private build's staging allows exactly DW_SWAP (wave 2 Task 4)
    stage = os.path.join(APPS_DIR, 'dw', 'main', 'dw_stage.cmake')
    if os.path.isfile(stage):
        m = re.search(r'^set\(allowed ([^)]*)\)', open(stage).read(), re.M)
        if not m or tuple(m.group(1).split()) != DW_SWAP:
            c.fail('%s allows %s, check_apps\'s DW_SWAP is %s' % (rel(stage), m.group(1).split() if m else None,
                                                                   list(DW_SWAP)))
        for p in stage_allowed_problems(open(stage).read()):
            c.fail('%s: %s' % (rel(stage), p))
    layers = os.path.join(REPO, 'tools', 'export', 'dw_layers.json')
    if os.path.isfile(layers):
        import json
        for p in generic_rows_problems(json.load(open(layers)).get('rows', [])):
            c.fail('%s: %s' % (rel(layers), p))
    for text, want, what in [
            ('set(allowed a b)\nif(NOT n IN_LIST allowed)\n  message("${allowed}")\n', False, 'one set and two reads'),
            ('set(allowed a b)\nlist(APPEND allowed c)\n', True, 'an append'),
            ('set(allowed a b)\nset(allowed c)\n', True, 'a second set'),
            ('set(allowed a)\nunset(allowed)\n', True, 'an unset'),
            ('# no list\n', True, 'no set'),
            # wave 2 Task 6 (the Task 5 review's M2): comments are not code
            ('set(allowed a b)\n# the allowed list is read, never written\n', False, 'a comment naming it'),
            ('set(allowed a b)  # the swap files\nif(NOT n IN_LIST allowed)\nendif()\n', False, 'a trailing comment'),
            ('set(allowed a b)\nlist(APPEND allowed c)  # appended\n', True, 'an append with a comment'),
            ('set(allowed a b)\nmessage("#1" allowed)\n', True, 'a # inside a string'),
            # wave 2 Task 7 (the Task 6 review's M2): bracket comments are not code; a bracket argument is
            ('#[[\nset(allowed z)\n]]\nset(allowed a b)\n', False, 'a set inside a bracket comment'),
            ('set(allowed a b)\n#[[ list(APPEND allowed c)\n   unset(allowed) ]]\n', False, 'a bracket comment over lines'),
            ('set(allowed a b)\n#[==[ allowed ]] still ]=] inside\nlist(APPEND allowed c) ]==]\n', False,
             'a bracket comment with = signs, closed only by its own'),
            ('set(allowed a b)\n#[==[ x ]==] list(APPEND allowed c)\n', True, 'code after a bracket comment closes'),
            ('set(allowed a b)\nmessage([[ # ]] allowed)\n', True, 'a # inside a bracket argument'),
            ('set(allowed a b)\nmessage([=[\n#]] ]=] allowed)\n', True, 'a bracket argument over lines'),
            ('set(allowed a b)\n# [[ not a bracket\nlist(APPEND allowed c)\n', True, 'a line comment ending its line'),
            ('set(allowed a b)\n#[=x\nlist(APPEND allowed c)\n', True, 'a line comment that only looks bracketed')]:
        if bool(stage_allowed_problems(text)) != want:
            c.fail('allow-list rule self-test: %s %s' % (what, 'not caught' if want else 'caught'))
    for rows, ok, what in [
            ([{'out': 'generic', 'file': n} for n in DW_SWAP_GENERIC] + [{'out': 'bin', 'file': 'x.bin'}], True,
             'DW_SWAP_GENERIC'),
            ([{'out': 'generic', 'file': n} for n in DW_SWAP_GENERIC[1:]], False, 'one swap file missing'),
            ([{'out': 'generic', 'file': n} for n in DW_SWAP_GENERIC + ('dw_x.bin',)], False, 'an extra generic row'),
            # public release Task 9 (preflight I1): the thumbnails and the chime are swap files with no generic row
            ([{'out': 'generic', 'file': n} for n in DW_SWAP], False, 'every DW_SWAP file as a generic row'),
            # wave 2 Task 6 (the Task 5 review's M3): a generic row with no file is reported, not a TypeError
            ([{'out': 'generic', 'file': n} for n in DW_SWAP_GENERIC] + [{'out': 'generic'}], False,
             'a row with no file'),
            ([{'out': 'generic', 'file': None}] + [{'out': 'generic', 'file': n} for n in DW_SWAP_GENERIC[1:]], False,
             'a row with a null file')]:
        try:
            problems = generic_rows_problems(rows)
        except TypeError as e:
            c.fail('generic rows self-test: %s raised %s' % (what, e))
            continue
        if (not problems) != ok:
            c.fail('generic rows self-test: %s %s' % (what, 'caught' if ok else 'not caught'))

    # each app's table, and what its theme defaults may set
    common_defaults = parse_sdkconfig(os.path.join(COMMON, 'sdkconfig.defaults'))
    for name, d in found.items():
        theme = os.path.join(d, 'sdkconfig.defaults.theme')
        if not os.path.isfile(theme):
            c.fail('%s is missing' % rel(theme))
            continue
        tdef = parse_sdkconfig(theme)
        for k in sorted(set(tdef) - allowed):
            c.fail('%s sets %s, outside the allow-list %s' % (rel(theme), k, sorted(allowed)))
        eff = dict(common_defaults, **tdef)   # SDKCONFIG_DEFAULTS order: common, then the theme's
        fn = eff.get('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME', '').strip('"')
        if eff.get('CONFIG_PARTITION_TABLE_CUSTOM') != 'y' or not fn or \
                os.path.realpath(os.path.join(d, fn)) != os.path.realpath(TABLE):
            c.fail('app %s: the partition table %r does not resolve to %s' % (name, fn, rel(TABLE)))
        with open(os.path.join(d, 'CMakeLists.txt')) as f:
            cm = f.read()
        if '../../common/sdkconfig.defaults;' not in cm or 'sdkconfig.defaults.theme' not in cm:
            c.fail('app %s: CMakeLists.txt does not take common/sdkconfig.defaults then sdkconfig.defaults.theme' % name)

    # dependency versions
    locks = {}
    for name, d in found.items():
        p = os.path.join(d, 'dependencies.lock')
        if not os.path.isfile(p):
            c.fail('%s is missing' % rel(p))
        else:
            locks[name] = lock_versions(p)
    names = sorted(locks)
    for other in names[1:]:
        a, b = locks[names[0]], locks[other]
        for comp in sorted(set(a) | set(b)):
            if a.get(comp) != b.get(comp):
                c.fail('dependency %s: %s has %s, %s has %s' % (comp, names[0], a.get(comp), other, b.get(comp)))

    # watch_ui neutrality
    for p in our_files(os.path.join(FW, 'components', 'watch_ui')):
        with open(p, 'rb') as f:
            text = f.read().decode('utf-8', 'replace')
        for i, line in enumerate(text.splitlines(), 1):
            if NEUTRALITY.search(line):
                c.fail('watch_ui neutrality: %s:%d: %s' % (rel(p), i, line.strip()))

    # face_kit: neutral everywhere under firmware/facekit, pure at its top level (rd wave 1 Task 5)
    fk = os.path.join(FW, 'facekit')
    for p in our_files(fk):
        text = read_text(p)
        if text is None:
            continue
        for i, line in facekit_neutral_problems(text):
            c.fail('face_kit neutrality: %s:%d: %s' % (rel(p), i, line))
        top = os.path.relpath(p, os.path.join(fk, 'face_kit'))
        if not top.startswith('..') and not top.startswith('ui' + os.sep) and p.endswith(('.c', '.h')):
            for i, line in facekit_pure_problems(text):
                c.fail('face_kit purity: %s:%d: %s' % (rel(p), i, line))

    # the theme firmwares never call NVS themselves
    for app in NVS_CALL_APPS:
        if app not in found:
            continue
        for p in our_files(found[app]):
            text = read_text(p)
            if text is None:
                continue   # binary
            for i, line in nvs_call_problems(text):
                c.fail('NVS call in the %s app: %s:%d: %s' % (app, rel(p), i, line))

    # NVS erase calls, our tree only
    for top in ERASE_SCOPE:
        for p in our_files(os.path.join(REPO, top)):
            with open(p, 'rb') as f:
                data = f.read()
            if b'\0' in data[:8192]:
                continue   # binary
            for i, line in enumerate(data.decode('utf-8', 'replace').splitlines(), 1):
                if ERASE.search(line):
                    c.fail('NVS erase: %s:%d: %s' % (rel(p), i, line.strip()))

    # the Wi-Fi restore: once, in net.c's CMD_FORGET (spec section 7.5); the manager's wrapper nowhere
    scope = []
    for top in ERASE_SCOPE:
        for p in our_files(os.path.join(REPO, top)):
            with open(p, 'rb') as f:
                data = f.read()
            if b'\0' not in data[:8192]:
                scope.append((rel(p).replace(os.sep, '/'), data.decode('utf-8', 'replace')))
    for msg in restore_problems(scope):
        c.fail(msg)

    # the secret guard (I3): no password, PoP or snapshot printed as a string
    for sf in SECRET_FILES:
        p = os.path.join(REPO, sf)
        if os.path.isfile(p):
            for i, call in secret_problems(sf, read_text(p) or ''):
                c.fail('secret printed: %s:%d: %s' % (sf, i, call))

    # the trademark scan over the dw image's sources, its tests and its tools (spec section 14, D9)
    # (dw's fold is tools/rd_terms.py's; with either module missing, the failure is already recorded, or this is a
    # public tree, and this is skipped)
    n = 0
    for p in (dw_text_files() if rt is not None and dt is not None else []):
        with open(p, 'rb') as f:
            data = f.read()
        if b'\0' in data[:8192]:
            continue   # binary
        n += 1
        for i, line in text_hits(rt, dt.FORBIDDEN_TEXT, data.decode('utf-8', 'replace')):
            c.fail('forbidden name: %s:%d: %s' % (rel(p), i, line))
    if dt is not None and rt is not None:
        print('dw scan: %d text files' % n)

    # the dw and rd assets: only the synthesised sounds, the Omni's backgrounds
    for theme in ('dw', 'rd'):
        if theme not in found or 'omni' not in found:
            continue
        ta = os.path.join(found[theme], 'assets')
        listing = [(os.path.relpath(os.path.join(root, f), ta), os.path.getsize(os.path.join(root, f)))
                   for root, _, files in os.walk(ta) for f in files]
        for msg in dw_sound_problems(listing, dw_pcm_names(theme), theme):
            c.fail(msg)
        for bg in DW_BACKGROUNDS:
            a, b = os.path.join(found['omni'], 'assets', bg), os.path.join(ta, bg)
            if not (os.path.isfile(a) and os.path.isfile(b) and filecmp.cmp(a, b, shallow=False)):
                c.fail('%s is not byte-equal to %s' % (rel(b), rel(a)))

    # the keyboard's shared assets (rim keyboard Task 8): watch_ui/assets is the source, each app a byte-equal copy
    for msg in shared_ui_problems(SHARED_UI_SRC, [os.path.join(APPS_DIR, a, 'assets') for a in REQUIRED_APPS]):
        c.fail(msg)


# ------------------------------------------------------------------ built
def descriptor_values(elf, sym='DW_THEME'):
    """(assets_files, assets_checksum) compiled into the descriptor sym (DW_THEME, RD_THEME), read from the elf by the
    IDF's gdb (no target)."""
    gdb = shutil.which('xtensa-esp32s3-elf-gdb')
    if not gdb:
        return None
    r = subprocess.run([gdb, '-batch', '-nx', '-ex', 'print/u %s.assets_files' % sym,
                        '-ex', 'print/u %s.assets_checksum' % sym, elf], capture_output=True, text=True)
    vals = re.findall(r'^\$\d+ = (\d+)$', r.stdout, re.M)
    return (int(vals[0]), int(vals[1])) if len(vals) == 2 else None


def committed_dw_assets(theme='dw'):
    """firmware/apps/<theme>/assets' packed kinds (the MMAP_FILE_SUPPORT_FORMAT ".bin,.pcm,.dat"), name -> bytes."""
    d = os.path.join(APPS_DIR, theme, 'assets')
    out = {}
    for f in sorted(os.listdir(d)):
        if f.lower().endswith(('.bin', '.pcm', '.dat')):
            with open(os.path.join(d, f), 'rb') as fh:
                out[f] = fh.read()
    return out


def tracked_json():
    """(path, text) for every tracked .json file and every tracked file named like an overlay descriptor."""
    r = subprocess.run(['git', '-C', REPO, 'ls-files', '-z'], check=True, capture_output=True)
    out = []
    for p in r.stdout.decode().split('\0'):
        if p and (p.endswith('.json') or os.path.basename(p) == OVERLAY_JSON) and os.path.isfile(os.path.join(REPO, p)):
            with open(os.path.join(REPO, p), 'rb') as f:
                out.append((p, f.read().decode('utf-8', 'replace')))
    return out


def nm_symbols(c, elf):
    """(symbol, nm line) for every symbol in elf, or None (a failure) without the IDF's nm."""
    nm = shutil.which('xtensa-esp32s3-elf-nm')
    if not nm:
        c.fail('xtensa-esp32s3-elf-nm not on PATH (source the ESP-IDF export.sh)')
        return None
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    return [(line.split()[-1], line.strip()) for line in out.splitlines() if line.split()]


def rd_list_hits(c, rt, path, data, image=False):
    """rd's list over an app image, or an assets image with its pixel and sound payloads blanked first (PR35); nothing
    without the list (rt None: a public tree, reported once by check_built)."""
    if rt is None:
        return
    for off, text in rt.rd_bytes_hits(blanked(data, PIXEL_EXTS) if image else data):
        c.fail('rd term in %s at 0x%x: %r' % (rel(path), off, text))


def dw_list_hits(c, dt, path, data, image=False):
    """dw's list (dt: tools/dw_terms.py) over an app image, or an assets image with its .pcm payloads blanked; nothing
    without the list (a public tree, reported once by check_built)."""
    if dt is None:
        return
    for off, text in (image_forbidden_hits(data, dt.FORBIDDEN) if image else forbidden_hits(data, dt.FORBIDDEN)):
        c.fail('forbidden name in %s at 0x%x: %r' % (rel(path), off, text))


def check_dw_build(c, bdir, publishable, dt=None, rt=None):
    """The dw checks on one dw build directory: forbidden names in its app and assets image (.pcm payloads skipped),
    every image file at an even address, no Omni prompt, no Omni symbol, and the descriptor's compiled checksum equal
    to the dir's own mmap_generate_assets.h (G3). publishable: also --publishable's (G1). rd wave 1 Task 5: rd's list
    too (rt, tools/rd_terms.py), and no rd_ or RD_ symbol. dt or rt None (a public tree): that list's scans skip."""
    app, img_path = os.path.join(bdir, 'omni_watch_dw.bin'), os.path.join(bdir, DW_IMAGE)
    with open(app, 'rb') as f:
        data = f.read()
    dw_list_hits(c, dt, app, data)
    rd_list_hits(c, rt, app, data)
    # the dw image does not carry the Omni's prompt (it is built at run time from the theme id)
    for off in omni_prompt_hits(data):
        c.fail('the Omni console prompt in %s at 0x%x' % (rel(app), off))
    with open(img_path, 'rb') as f:
        img = f.read()
    dw_list_hits(c, dt, img_path, img, image=True)
    rd_list_hits(c, rt, img_path, img, image=True)
    entries = mmap_entries(img)
    for name, at, size in entries:
        if at % 2:   # dw_img_from_bin rejects an odd address (Task 6); the packer aligns to 64
            c.fail('%s: %s at the odd image offset 0x%x' % (rel(img_path), name, at))
    print('dw build %s: %d files in the assets image' % (rel(bdir), len(entries)))

    hdr = os.path.join(bdir, MMAP_HEADER)
    header = None
    if os.path.isfile(hdr):
        with open(hdr) as f:
            header = parse_mmap_header(f.read())
    elf = os.path.join(bdir, 'omni_watch_dw.elf')
    for msg in checksum_problems(descriptor_values(elf), header):
        c.fail('%s: %s' % (rel(bdir), msg))

    syms = nm_symbols(c, elf)
    if syms is None:
        return
    # no Omni code in the dw image, and no rd code (rd wave 1 Task 5)
    for sym, line in syms:
        if OMNI_SYMBOL.match(sym):
            c.fail('Omni symbol in %s: %s' % (rel(elf), line))
        if sym.startswith(('rd_', 'RD_')):
            c.fail('rd symbol in %s: %s' % (rel(elf), line))

    art = os.path.join(bdir, DW_ART_FILE)
    text = open(art).read() if os.path.isfile(art) else ''
    packed = {name: img[at:at + size] for name, at, size in entries}
    if publishable:
        for msg in publishable_problems(text, packed, committed_dw_assets(), tracked_json()):
            c.fail('--publishable %s: %s' % (rel(bdir), msg))
    elif text.startswith('overlay'):   # a private build: only the swap files may differ (wave 2 Task 4)
        committed = committed_dw_assets()
        probs = swap_problems(packed, committed)
        for msg in probs:
            c.fail('%s (an overlay build): %s' % (rel(bdir), msg))
        if not probs:
            swapped = sorted(n for n in DW_SWAP if n in packed and packed[n] != committed[n])
            print('dw build %s: overlay files %s, every other file the committed one' % (rel(bdir),
                                                                                          ', '.join(swapped) or 'none'))


def rd_image_problems(packed, committed):
    """An rd image (rd wave 1 Task 5): the committed set exactly, every file byte-equal (rd has no overlay)."""
    out = []
    for n in sorted(set(packed) | set(committed)):
        if n not in committed:
            out.append('packed file %s is not committed in firmware/apps/rd/assets' % n)
        elif n not in packed:
            out.append('committed file %s is not in the image' % n)
        elif packed[n] != committed[n]:
            out.append('packed file %s differs from the committed one' % n)
    return out


def check_rd_build(c, bdir, dt, rt):
    """The rd checks on one rd build directory (rd wave 1 Task 5, spec section 13.3): both lists over its app and its
    assets image (the image's .bin and .pcm payloads blanked; rd's 3-letter tokens word-bounded, in runs of 8 or more,
    R159), neither
    the rd prompt nor the Omni's as a literal, every image file at an even address, the descriptor's compiled checksum
    equal to the dir's own mmap_generate_assets.h, no Omni, dw_ or DW_ symbol, and the image equal to the committed
    firmware/apps/rd/assets file for file (every rd build is generic and publishable)."""
    app, img_path = os.path.join(bdir, 'omni_watch_rd.bin'), os.path.join(bdir, RD_IMAGE)
    with open(app, 'rb') as f:
        data = f.read()
    dw_list_hits(c, dt, app, data)
    rd_list_hits(c, rt, app, data)
    for off in omni_prompt_hits(data):
        c.fail('the Omni console prompt in %s at 0x%x' % (rel(app), off))
    for m in RD_PROMPT.finditer(data):
        c.fail('the rd console prompt as a literal in %s at 0x%x' % (rel(app), m.start()))
    with open(img_path, 'rb') as f:
        img = f.read()
    dw_list_hits(c, dt, img_path, blanked(img, PIXEL_EXTS))
    rd_list_hits(c, rt, img_path, img, image=True)
    entries = mmap_entries(img)
    for name, at, size in entries:
        if at % 2:
            c.fail('%s: %s at the odd image offset 0x%x' % (rel(img_path), name, at))
    packed = {name: img[at:at + size] for name, at, size in entries}
    for msg in rd_image_problems(packed, committed_dw_assets('rd')):
        c.fail('%s (rd is always generic): %s' % (rel(bdir), msg))
    print('rd build %s: %d files in the assets image, each the committed one' % (rel(bdir), len(entries)))

    hdr = os.path.join(bdir, MMAP_HEADER)
    header = None
    if os.path.isfile(hdr):
        with open(hdr) as f:
            header = parse_mmap_header(f.read())
    elf = os.path.join(bdir, 'omni_watch_rd.elf')
    for msg in checksum_problems(descriptor_values(elf, 'RD_THEME'), header):
        c.fail('%s: %s' % (rel(bdir), msg))
    syms = nm_symbols(c, elf)
    for sym, line in syms or []:
        if OMNI_SYMBOL.match(sym):
            c.fail('Omni symbol in %s: %s' % (rel(elf), line))
        if sym.startswith(('dw_', 'DW_')):
            c.fail('dw symbol in %s: %s' % (rel(elf), line))


def check_built(c, require, dw_build=None, publishable=False, rd_build=None):
    found = present_apps(c)
    if c.failures:
        return
    dt, rt = load_terms(c, 'built')
    if c.failures:   # a private checkout missing a list (load_terms recorded it)
        return
    dw_dir = os.path.realpath(dw_build) if dw_build else os.path.join(found['dw'], 'build')
    rd_dir = os.path.realpath(rd_build) if rd_build else os.path.join(found['rd'], 'build')
    if publishable and dw_dir != os.path.join(found['dw'], 'build'):
        c.fail('--publishable checks the generic build, %s, not %s' % (rel(os.path.join(found['dw'], 'build')),
                                                                       rel(dw_dir)))
        return
    need = [os.path.join(found['omni'], p) for p in ('build/partition_table/partition-table.bin', 'sdkconfig',
                                                     'build/omni_watch.bin', 'build/omni_watch.elf')]
    need += [os.path.join(found['dw'], 'sdkconfig')]
    need += [os.path.join(dw_dir, p) for p in ('partition_table/partition-table.bin', 'omni_watch_dw.bin',
                                               'omni_watch_dw.elf', DW_IMAGE, MMAP_HEADER)]
    if publishable:
        need.append(os.path.join(dw_dir, DW_ART_FILE))
    need += [os.path.join(rd_dir, p) for p in ('partition_table/partition-table.bin', 'omni_watch_rd.bin',
                                               'omni_watch_rd.elf', RD_IMAGE, MMAP_HEADER)]
    missing = [p for p in need if not os.path.isfile(p)]
    for name, d in found.items():
        if name not in ('omni', 'dw') and not os.path.isfile(os.path.join(d, 'sdkconfig')):
            missing.append(os.path.join(d, 'sdkconfig'))
    if missing:
        msg = 'build missing: ' + ', '.join(rel(p) for p in missing)
        if require:
            c.fail(msg)
            return
        print('SKIP: ' + msg)
        sys.exit(SKIP)

    # partition tables: every build directory of every app, and the --dw-build and --rd-build directories
    tables = sorted(os.path.join(d, b, 'partition_table', 'partition-table.bin')
                    for d in found.values() for b in os.listdir(d) if b == 'build' or b.startswith('build-'))
    tables += [os.path.join(x, 'partition_table', 'partition-table.bin') for x in (dw_dir, rd_dir)]
    tables = list(dict.fromkeys(t for t in tables if os.path.isfile(t)))
    for t in tables[1:]:
        if not filecmp.cmp(tables[0], t, shallow=False):
            c.fail('%s differs from %s' % (rel(t), rel(tables[0])))
    print('partition tables compared: %d (%s)' % (len(tables), ', '.join(rel(t) for t in tables)))

    # generated sdkconfigs, outside the allow-list
    allowed = allow_list()
    cfgs = {n: {k: v for k, v in parse_sdkconfig(os.path.join(d, 'sdkconfig')).items() if k not in allowed}
            for n, d in found.items()}
    names = sorted(cfgs)
    for other in names[1:]:
        a, b = cfgs[names[0]], cfgs[other]
        for k in sorted(set(a) | set(b)):
            if a.get(k) != b.get(k):
                c.fail('sdkconfig %s: %s=%s, %s=%s' % (k, names[0], a.get(k), other, b.get(k)))

    # a public tree (tools/terms.py): the list scans skip, reported once; every other check runs (preflight I2)
    absent = [n for n, m in zip(TERM_MODULES, (dt, rt)) if m is None]
    if absent:
        print('SKIP: %s absent (a public tree): the %s list scans of the images'
              % ('/'.join(absent), ' and '.join(n[:2] for n in absent)))
    check_dw_build(c, dw_dir, publishable, dt, rt)
    check_rd_build(c, rd_dir, dt, rt)

    # the Omni's image holds no rd term (rd wave 1 Task 5, PR8 e: the shared city table's time-zone strings pass)
    omni_bin = os.path.join(found['omni'], 'build', 'omni_watch.bin')
    with open(omni_bin, 'rb') as f:
        rd_list_hits(c, rt, omni_bin, f.read())

    # no dw, rd or face_kit code in the Omni image
    elf = os.path.join(found['omni'], 'build', 'omni_watch.elf')
    for sym, line in nm_symbols(c, elf) or []:
        if sym.startswith(('dw_', 'DW_')):
            c.fail('dw symbol in %s: %s' % (rel(elf), line))
        if sym.startswith(('rd_', 'RD_', 'fk_')):
            c.fail('rd or face_kit symbol in %s: %s' % (rel(elf), line))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('mode', choices=('static', 'built', 'terms'))
    ap.add_argument('--require', action='store_true', help='built: a missing build fails instead of skipping (77)')
    ap.add_argument('--dw-build', metavar='DIR', help='built: the dw build directory to check (default '
                    'firmware/apps/dw/build; e.g. firmware/apps/dw/build-private)')
    ap.add_argument('--rd-build', metavar='DIR', help='built: the rd build directory to check (default '
                    'firmware/apps/rd/build; e.g. firmware/apps/rd/build-nocon)')
    ap.add_argument('--publishable', action='store_true', help='built: the generic build is publishable (G1)')
    a = ap.parse_args()
    c = Checks()
    if a.mode == 'terms':   # rd_terms_agree alone, for its own CTest (PR20: 77 without node or in a public tree)
        try:
            rt = terms.load(REPO, 'rd_terms')
        except terms.TermsMissing:
            c.fail('rd_terms missing: tools/rd_terms.py')
            return 1
        if rt is None:
            print('SKIP: rd_terms absent (a public tree): rd_terms_agree')
            print('check_apps terms: SKIPPED (a public tree)')
            return SKIP
        rc = rd_terms_agree(c, rt)
        print('check_apps terms: %s' % {0: 'OK', SKIP: 'SKIPPED (no node)'}.get(rc, 'FAIL'))
        return rc
    if a.mode == 'static':
        if not os.path.exists(os.path.join(REPO, '.git')):   # final review m6: a download without .git
            print('SKIP: no .git in %s: the static checks read `git ls-files`; %s' % (REPO, NO_GIT_NOTE))
            print('check_apps static: SKIPPED (no git clone)')
            return SKIP
        check_static(c)
    else:
        check_built(c, a.require, a.dw_build, a.publishable, a.rd_build)
    if c.failures:
        print('check_apps %s: %d failure(s)' % (a.mode, len(c.failures)))
        return 1
    print('check_apps %s: OK (%s)' % (a.mode, ', '.join(sorted(apps()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
