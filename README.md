# Omni Watch

Three watch-face firmwares for the Waveshare ESP32-S3-Touch-AMOLED-1.75C, a round 466 x 466 AMOLED board.
Each theme is its own firmware, built from its own app directory, and they share the board support, the display and
input plumbing, the settings and Wi-Fi setup screens, and a host-side test suite.

- **Omni** (`firmware/apps/omni`): a brass dial inspired by a 1980s television prop. Engraved date rings turn round a
  globe, under a hinged lid and an iris, with a set of practical faces beside it: digital, live and spinning globes, a
  globe with hands, world time, almanac (daylight and moon) and a countdown timer.
- **Time Traveller** (`firmware/apps/dw`): a second set of faces in green and brass, with an animated doorway reveal
  and a clock-strike chime.
- **Mining Ship** (`firmware/apps/rd`): an industrial set of faces: hull plating, stencilled lettering and phosphor
  read-outs.

The settings are the same in every theme: home city (from a table of 186), brightness, sleep timeout, 12 or 24 hour
time, sound and volume, reduced motion, the face reveal and the order of the face carousel. The clock is set over
Wi-Fi by SNTP. You give the watch your network either from a phone, by scanning the QR code it shows with Espressif's
BLE provisioning app, or on the watch itself, by picking the network from a scan and typing the password on the rim
keyboard. Wi-Fi is on only while provisioning or syncing.

## Hardware

- Waveshare ESP32-S3-Touch-AMOLED-1.75C: ESP32-S3 with 8 MB octal PSRAM, a 1.75" 466 x 466 AMOLED (CO5300, QSPI),
  CST9217 capacitive touch, an AXP2101 power manager with a battery gauge, a QMI8658 IMU and a speaker.
- **32 MB of flash is required.** The builds target 32 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_32MB` in
  `firmware/common/sdkconfig.defaults`), and the partition table (`firmware/common/partitions.csv`) has NVS, a 4 MB
  app and an assets partition of about 12 MB in the first 16 MB, plus a 16 MB `pack` partition at 0x1000000. Waveshare's
  documentation is not consistent (32 MB in its README and wiki, a 16 MB part on the schematic), so check yours first:
  `esptool.py -p /dev/serial/by-id/<your board> flash_id` prints "Detected flash size". 16 MB boards are not supported
  as-is: the image fails at boot (ESP-IDF stops when the chip's flash is smaller than the size in the image header).
- The art and sounds live in the assets partition as an `esp_mmap_assets` image, mapped into memory at run time.
- The board's BOOT button is the back button. Dragging a finger round the screen's rim works as a bezel.

## Building

You need ESP-IDF v5.5.6. Source its `export.sh`, then build a theme by its app directory:

```sh
. $IDF_PATH/export.sh            # your ESP-IDF v5.5.6's export.sh

idf.py -C firmware/apps/omni build     # Omni
idf.py -C firmware/apps/dw build       # Time Traveller
idf.py -C firmware/apps/rd build       # Mining Ship
```

The first build fetches the components listed in each app's `dependencies.lock` (LVGL, the Waveshare board support
package and Espressif components) through the IDF Component Manager. Each build writes its `sdkconfig` beside the app
and its output to `firmware/apps/<app>/build`.

The build options are in `idf.py -C firmware/apps/<app> menuconfig`, in the theme's own menu and the shared "Watch UI"
menu. For example, `CONFIG_OMNI_DEBUG_CONSOLE` is the USB serial console (on by default) and `CONFIG_OMNI_DISP_DIRECT`
the display's direct path for full-screen motion (on by default).

## Flashing

On a new board with 32 MB of flash (see Hardware), flash everything once with the standard IDF command:

```sh
idf.py -C firmware/apps/omni -p /dev/serial/by-id/<your board> flash
```

After that, use `tools/theme.sh` to switch themes or update one. It keeps the NVS partition, which holds your settings
and your Wi-Fi credentials. It builds the app, backs up NVS twice and compares the copies, then writes only the app,
assets and partition table regions that differ. Before it boots the new firmware, it checks that NVS is unchanged.

The tools that talk to a watch only use the ports you list in `tools/watch.local`. This file is git-ignored. Each
line is a `/dev/serial/by-id/` path, optionally followed by a name:

```
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<serial>-if00 mywatch
```

Then:

```sh
tools/theme.sh omni              # build, flash (NVS kept) and check the boot line
tools/theme.sh rd --watch mywatch
tools/theme.sh dw --dry-run      # build and print the flash plan only
```

If you list one watch, it is used. If you list several, pass `--watch <name>` or `--port <path>`. A port that is not
listed is refused. `tools/flash_keep_nvs.py` is the flashing step on its own (`--build <dir>`, or `--backup-only`).
It keeps its backups in `~/omni-watch-nvs/`.

## Host tests

The face logic, the renderers, the settings and storage code and the tools have a host test suite (CMake, Unity from
ESP-IDF, CTest):

```sh
. $IDF_PATH/export.sh            # your ESP-IDF v5.5.6's export.sh
cmake -S firmware/test/host -B build-host
cmake --build build-host -j
ctest --test-dir build-host -j8 --output-on-failure
```

It builds at `-O2` with assertions kept. For a sanitizer run, add
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"` to the first command (using a separate build
directory). Tests that need something you don't have are skipped or left out: for example the asset re-render checks,
which need Node, `tools/node_modules` and Chrome. `python3 tools/check_apps.py static` runs the repository's static
checks on their own. Run the host tests from a git clone: the tests that read `git ls-files` or `git show` skip in a
tree without `.git` (a downloaded archive).

## Assets

The art in `firmware/apps/*/assets` is committed, packed and ready to flash, so no asset step is needed to build.
The pipelines that made it are in `tools/`. They need Node (`npm --prefix tools ci`), Chrome and the fonts from
`tools/fonts.sh`.

- **Omni:** `tools/assets.sh` redraws the art from the mockups in `design/mockups` and the NASA earth maps in
  `design/assets`. `tools/assets.sh --identity` checks every committed asset and generated font against the hashes in
  `tools/identity.sha256`.
- **Time Traveller and Mining Ship:** most of the art was drawn from the designers' source files, which are not
  public, so `tools/dw_assets.sh` and `tools/rd_assets.sh` cannot regenerate it: for these themes the committed files
  are the source. The exception is the Time Traveller theme's generic call box and doors, which this repository's own
  code draws (`tools/export/dw_generic.js`). `tools/dw_assets.sh --check` draws them again and compares them with the
  committed files byte for byte.
- **Sounds:** every theme's sounds are synthesised from scratch by `tools/gen_sfx.py`, `tools/gen_sfx_dw.py` and
  `tools/gen_sfx_rd.py`. Each one's `compare` command reports whether this host reproduces the committed bytes.

## Disclaimer

This is an unofficial fan work, not affiliated with or endorsed by any rights holder. Waveshare and Espressif are
named only to identify the hardware and the SDK.

## Licence

The code is under the MIT licence: see `LICENSE`. Third-party fonts, imagery, the ported AXP2101 code and the
components fetched at build time keep their own licences: see `NOTICE` and `LICENSES/`.
