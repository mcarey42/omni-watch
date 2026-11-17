#!/usr/bin/env python3
"""Capture a screenshot from the watch over serial and save it as PNG.

Sends `shot` to the firmware console, reads `SHOT w h`, h base64 RGB565 rows,
`SHOT END`, and writes an RGB PNG using only the standard library + pyserial.

--bg N instead sends `theme dump N` (a brass background, R112), reads `BG N w h`,
h rows and `BG END <crc32>`, checks the CRC-32 of the pixels, and writes an LVGL
v9 RGB565 .bin (the 12-byte header tools/assets.sh writes, then the raw
little-endian pixels), e.g. tools/shot.py --bg 0 --out bg_bezel.bin.

--port / --watch: the port comes from the git-ignored tools/watch.local (tools/watchport.py, as tools/trace.py and
tools/shot_set.py): --port must equal a listed /dev/serial/by-id/ path exactly, --watch names an entry, and with
neither a file of one entry gives that entry. Anything else is refused (exit 2) before pyserial is imported.
"""
import argparse
import base64
import binascii
import os
import struct
import sys
import time
import zlib

sys.dont_write_bytecode = True   # importing watchport leaves no tools/__pycache__ behind
import watchport  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_png(path, w, h, rows_rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows_rgb)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def rgb565_row_to_rgb(row, w):
    out = bytearray(w * 3)
    for x in range(w):
        v = row[2 * x] | (row[2 * x + 1] << 8)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        out[3 * x] = (r << 3) | (r >> 2)
        out[3 * x + 1] = (g << 2) | (g >> 4)
        out[3 * x + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def capture(port, timeout):
    import serial   # here, not at the top: a refused port never imports pyserial
    with serial.Serial(port, 115200, timeout=1) as ser:
        ser.reset_input_buffer()
        ser.write(b"\nshot\n")
        deadline = time.time() + timeout
        w = h = None
        rows = []
        while time.time() < deadline:
            line = ser.readline().decode("ascii", "replace").strip()
            if not line:
                continue
            if w is None:
                parts = line.split()
                if len(parts) == 3 and parts[0] == "SHOT" and parts[1].isdigit():
                    w, h = int(parts[1]), int(parts[2])
                    row_len = (w * 2 + 2) // 3 * 4
                elif line == "SHOT ERR":
                    raise RuntimeError("firmware reported SHOT ERR")
                continue
            if line == "SHOT END":
                break
            # Log lines from other tasks can land between the rows: keep only lines that decode to a row.
            if len(line) != row_len:
                continue
            try:
                row = base64.b64decode(line, validate=True)
            except binascii.Error:
                continue
            rows.append(rgb565_row_to_rgb(row, w))
        else:
            raise TimeoutError("timed out waiting for screenshot")
    if w is None or len(rows) != h:
        raise RuntimeError("incomplete screenshot: %d of %s rows" % (len(rows), h))
    return w, h, rows


def capture_bg(port, n, timeout):
    """`theme dump n`: returns (w, h, raw RGB565 bytes); the device's CRC-32 of the pixels must match."""
    import serial
    with serial.Serial(port, 115200, timeout=1) as ser:
        ser.reset_input_buffer()
        ser.write(b"\ntheme dump %d\n" % n)
        deadline = time.time() + timeout
        w = h = None
        rows = []
        while time.time() < deadline:
            line = ser.readline().decode("ascii", "replace").strip()
            if not line:
                continue
            parts = line.split()
            if w is None:
                if len(parts) == 4 and parts[0] == "BG" and parts[1] == str(n) and parts[2].isdigit():
                    w, h = int(parts[2]), int(parts[3])
                    row_len = (w * 2 + 2) // 3 * 4
                elif line == "BG ERR":
                    raise RuntimeError("firmware reported BG ERR")
                continue
            if len(parts) == 3 and parts[:2] == ["BG", "END"]:
                crc = int(parts[2], 16)
                break
            if len(line) != row_len:   # a log line from another task
                continue
            try:
                rows.append(base64.b64decode(line, validate=True))
            except binascii.Error:
                continue
        else:
            raise TimeoutError("timed out waiting for the background")
    if w is None or len(rows) != h:
        raise RuntimeError("incomplete background: %d of %s rows" % (len(rows), h))
    px = b"".join(rows)
    if zlib.crc32(px) & 0xFFFFFFFF != crc:
        raise RuntimeError("CRC %08x, the device says %08x" % (zlib.crc32(px) & 0xFFFFFFFF, crc))
    return w, h, px


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="a by-id path listed in tools/watch.local, exactly")
    ap.add_argument("--watch", metavar="NAME", help="a watch named in tools/watch.local")
    ap.add_argument("--out", required=True)
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--lit", action="store_true",
                    help="also print the lit-pixel fraction (any channel > 8), e.g. the glance must be < 5 %%")
    ap.add_argument("--bg", type=int, choices=(0, 1, 2),
                    help="dump brass background N (theme dump) to --out as an LVGL RGB565 .bin instead")
    a = ap.parse_args()
    try:
        a.port = watchport.resolve(REPO, a.port, a.watch)
    except watchport.Refused as e:
        print(f"shot: refused: {e}", file=sys.stderr)
        return 2
    if a.bg is not None:
        for attempt in range(3):
            try:
                w, h, px = capture_bg(a.port, a.bg, a.timeout)
                break
            except (RuntimeError, TimeoutError) as e:   # a dropped or interleaved row: try again
                if attempt == 2:
                    raise
                print("retrying: %s" % e, file=sys.stderr)
        with open(a.out, "wb") as f:   # magic 0x19, cf RGB565 0x12, flags, w, h, stride, reserved (assets.sh)
            f.write(struct.pack("<BBHHHHH", 0x19, 0x12, 0, w, h, w * 2, 0) + px)
        print("wrote %s (%dx%d, %d bytes, pixel crc32 %08x)" % (a.out, w, h, 12 + len(px), zlib.crc32(px) & 0xFFFFFFFF))
        return 0
    for attempt in range(3):  # a log line written in the middle of a row loses that row: try again
        try:
            w, h, rows = capture(a.port, a.timeout)
            break
        except RuntimeError as e:
            if attempt == 2:
                raise
            print("retrying: %s" % e, file=sys.stderr)
    write_png(a.out, w, h, rows)
    print("wrote %s (%dx%d)" % (a.out, w, h))
    if a.lit:
        lit = sum(1 for r in rows for i in range(0, len(r), 3) if max(r[i], r[i + 1], r[i + 2]) > 8)
        print("lit %.2f %% (%d of %d pixels, any channel > 8)" % (100.0 * lit / (w * h), lit, w * h))


if __name__ == "__main__":
    sys.exit(main())
