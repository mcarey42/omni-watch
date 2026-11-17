#!/usr/bin/env python3
"""Record a QMI8658 accelerometer trace from the watch for the wake-classifier replay test.

Sends `trace on` to the debug console, collects `T,<ms>,<ax>,<ay>,<az>` lines (board frame, g, 25 Hz)
for --seconds, sends `trace off`, and writes firmware/test/host/traces/<name>.csv:

    # expect_wakes=N
    t_ms,ax,ay,az        (one row per sample, t_ms rebased to 0)

N is --expect, or typed by the operator after the recording if --expect is omitted. The device's own
EV_LIFT log lines seen during the recording are counted and printed as a cross-check.

Untethered (on-device RAM buffer, max 72 s):
  python3 tools/trace.py walk --arm --seconds 60 --delay 20   # sends `trace rec 60 20`, then unplug & go
  python3 tools/trace.py walk --from-dump --expect 0          # after re-plugging: `trace dump` -> CSV

The port comes from the git-ignored tools/watch.local (tools/watchport.py): --port must equal a listed by-id path
exactly, --watch NAME picks a listed watch, and with several listed one of them is required. Anything else is
refused (exit 2) before the port is opened.

Run inside the ESP-IDF environment (pyserial):  . ~/esp/v5.5.6/esp-idf/export.sh
Example: python3 tools/trace.py walk_60s --seconds 60 --expect 0 --delay 5
"""
import argparse
import pathlib
import re
import sys
import time

sys.dont_write_bytecode = True   # importing watchport leaves no tools/__pycache__ behind
import watchport  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent
TRACE_DIR = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "test" / "host" / "traces"
END = re.compile(r"TRACE END (\d+) lifts=(\d+)")
ROW = re.compile(r"T,(\d+),(-?\d+\.\d+),(-?\d+\.\d+),(-?\d+\.\d+)\s*$")
NAME = re.compile(r"^[A-Za-z0-9_-]+$")


def record(port, seconds):
    import serial   # pyserial, only after the port has resolved (a refusal never needs it)
    rows, lifts = [], 0
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(b"trace on\r\n")
        buf = b""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            buf += s.read(4096)
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode(errors="replace")
                m = ROW.search(line)
                if m:
                    rows.append((int(m.group(1)), m.group(2), m.group(3), m.group(4)))
                elif "EV_LIFT" in line:
                    lifts += 1
        s.write(b"trace off\r\n")
    return rows, lifts


def arm(port, seconds, delay):
    import serial
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(f"trace rec {int(seconds)} {int(delay)}\r\n".encode())
        end, buf = time.monotonic() + 3, b""
        while time.monotonic() < end and b"TRACE REC" not in buf:
            buf += s.read(4096)
    line = next((l for l in buf.decode(errors="replace").splitlines() if "TRACE REC" in l), "no reply")
    print(line.strip())
    if "ERR" in line or line == "no reply":
        sys.exit(1)


def dump(port):
    import serial
    rows = []
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(b"trace dump\r\n")
        buf, end = b"", time.monotonic() + 60
        while time.monotonic() < end:
            buf += s.read(4096)
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode(errors="replace")
                if "TRACE BUSY" in line:
                    sys.exit("device is still recording")
                m = END.search(line)
                if m:
                    if int(m.group(1)) != len(rows):
                        sys.exit(f"dump lost lines: device sent {m.group(1)}, got {len(rows)}")
                    return rows, int(m.group(2))
                m = ROW.search(line)
                if m:
                    rows.append((int(m.group(1)), m.group(2), m.group(3), m.group(4)))
    sys.exit("timed out waiting for TRACE END")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="trace name -> firmware/test/host/traces/<name>.csv")
    ap.add_argument("--seconds", type=float, help="recording length (live and --arm)")
    ap.add_argument("--arm", action="store_true", help="start an on-device RAM recording (trace rec) and exit")
    ap.add_argument("--from-dump", action="store_true", help="read the on-device recording (trace dump)")
    ap.add_argument("--expect", type=int, help="expected wake count (prompted for if omitted)")
    ap.add_argument("--delay", type=float, default=0, help="seconds before recording starts (countdown / rec delay)")
    ap.add_argument("--port", help="a by-id path listed in tools/watch.local, exactly")
    ap.add_argument("--watch", metavar="NAME", help="a watch named in tools/watch.local")
    ap.add_argument("--force", action="store_true", help="overwrite an existing trace")
    a = ap.parse_args()
    try:
        a.port = watchport.resolve(str(REPO), a.port, a.watch)
    except watchport.Refused as e:
        print(f"trace: refused: {e}", file=sys.stderr)
        sys.exit(2)

    if not NAME.match(a.name):
        sys.exit("name: letters, digits, _ and - only")
    out = TRACE_DIR / f"{a.name}.csv"
    if out.exists() and not a.force:
        sys.exit(f"{out} exists (use --force)")

    if a.arm:
        if a.seconds is None:
            sys.exit("--arm needs --seconds")
        arm(a.port, a.seconds, a.delay)
        return
    if a.from_dump:
        rows, lifts = dump(a.port)
        print(f"dump: {len(rows)} samples, device saw {lifts} EV_LIFT while recording", flush=True)
    else:
        if a.seconds is None:
            sys.exit("--seconds is required")
        for left in range(int(a.delay), 0, -1):
            print(f"starting in {left}...", flush=True)
            time.sleep(1)
        print(f"RECORDING {a.seconds:g} s", flush=True)
        rows, lifts = record(a.port, a.seconds)
        print(f"done: {len(rows)} samples, device logged {lifts} EV_LIFT", flush=True)
    if len(rows) < 2:
        sys.exit("no trace lines received (is the console running?)")

    expect = a.expect
    if expect is None:
        expect = int(input("expected wakes in this trace: ").strip())
    t0 = rows[0][0]
    with open(out, "w") as f:
        f.write(f"# expect_wakes={expect}\n")
        for t, x, y, z in rows:
            f.write(f"{t - t0},{x},{y},{z}\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
