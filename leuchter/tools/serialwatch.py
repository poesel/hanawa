#!/usr/bin/env python3
"""Watch the ESP8266's serial console (115200, /dev/ttyUSB0) and print it as text.

pyserial comes from the repo's .venv (PlatformIO needs it). The serial device
belongs to the `dialout` group, so from a shell where that group is not active
yet, wrap the call:

    sg dialout "../.venv/bin/python -u tools/serialwatch.py"

Ctrl-C stops it. --secs N stops after N seconds instead (handy while a `pio run
-t upload` runs in parallel: start this first, sleep a bit, then flash - the
boot banner of the new firmware shows up right after the upload).
"""

import argparse
import sys
import time

import serial  # pyserial


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dev", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--secs", type=int, default=0, help="0 = run until Ctrl-C")
    a = ap.parse_args()

    s = serial.Serial(a.dev, a.baud, timeout=0.2)
    end = time.time() + a.secs if a.secs else None
    while True:
        data = s.read(s.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode("latin1"))
            sys.stdout.flush()
        if end and time.time() >= end:
            break


if __name__ == "__main__":
    main()
