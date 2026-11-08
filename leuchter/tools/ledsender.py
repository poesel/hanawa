#!/usr/bin/env python3
"""
Hanawa Leuchter - Terminal-Testprogramm

Sends Hyperion-compatible UDP color frames to the Leuchter (ESP8266)
and waits for the health byte 0x80 after every frame. Every sent and
received UDP packet is appended to a log file (default ./ledsender.log).

Only Python stdlib. Examples:

    ./ledsender.py --host 192.168.1.101 solid ff0000
    ./ledsender.py --host 192.168.1.101 --fps 30 --loop rainbow
    ./ledsender.py --host 192.168.1.101 --leds 30 fade ff0000 0000ff
    ./ledsender.py --host 192.168.1.101 chase 00ff00
    ./ledsender.py --host 192.168.1.101            <- interactive menu

Run with --dry to see the frames without UDP.
"""

import argparse
import random
import socket
import time
import colorsys
from datetime import datetime

MAGIC_A = 0xAA
MAGIC_B = 0xFF
NO_COMMAND = 0x00
HEALTH_BYTE = b"\x80"

PATTERNS = ("solid", "rainbow", "fade", "chase", "blink", "random", "off")


def parse_hex(s):
    s = s.lstrip("#")
    if len(s) != 6 or not all(c in "0123456789abcdefABCDEF" for c in s):
        raise SystemExit(f"invalid color: {s} (use rrrgggbbb as rrggbb)")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def build_frame(leds):
    """Hyperion UDP LED frame: AA 00 FF <checksum> + RGB per LED."""
    payload = b""
    for (r, g, b) in leds:
        payload += bytes((r, g, b))
    head = bytes([MAGIC_A, NO_COMMAND, MAGIC_B, 0])
    checksum = (head[0] + head[1] + head[2] + sum(payload)) & 0xFF
    return head[:3] + bytes([checksum]) + payload


def make_colors(name, args, n, step):
    """One animation frame as list of n (r,g,b) tuples."""
    if name == "solid":
        return [parse_hex(args[0])] * n
    if name == "off":
        return [(0, 0, 0)] * n
    if name == "rainbow":
        return [
            tuple(int(v * 255) for v in colorsys.hsv_to_rgb(((i / n) + step / 50) % 1.0, 1, 1))
            for i in range(n)
        ]
    if name == "fade":
        ca, cb = parse_hex(args[0]), parse_hex(args[1])
        t = abs((step % 60) - 30) / 30
        return [tuple(int(ca[k] + (cb[k] - ca[k]) * t) for k in range(3)) for _ in range(n)]
    if name == "chase":
        c = parse_hex(args[0])
        head = step % (n + 4)
        return [c if head <= i < head + 4 else (0, 0, 0) for i in range(n)]
    if name == "blink":
        c = parse_hex(args[0]) if args else (255, 0, 0)
        return [c if step % 2 else (0, 0, 0)] * n
    if name == "random":
        return [(random.randrange(256), random.randrange(256), random.randrange(256)) for _ in range(n)]
    raise SystemExit(f"unknown pattern {name}")


class PktLog:
    """Append every sent/received UDP packet to a log file."""

    def __init__(self, path):
        self.path = path

    def write(self, text):
        if self.path is None:
            return
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with open(self.path, "a") as f:
            f.write(f"{ts} {text}\n")

    def tx(self, data):
        self.write(f"TX  {len(data):3d} B  {data.hex(' ')}")

    def rx(self, data):
        self.write(f"RX  {len(data):3d} B  {data.hex(' ')}")

    def miss(self):
        self.write("RX    -   no health byte / timeout")

    def retry(self):
        self.write("RX    -   no ack yet, resending frame")


class Sender:
    def __init__(self, host, port, log, timeout=1.0, retries=2, retry_delay=0.2):
        self.host, self.port, self.log = host, port, log
        self.timeout = timeout
        self.retries = retries          # resend on missing ack
        self.retry_delay = retry_delay  # seconds before the resend

    def send(self, frame):
        """Send frame, resend up to self.retries times while waiting for 0x80.
        Returns latency ms of the successful attempt or None if all failed."""
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(self.timeout)
        try:
            for attempt in range(self.retries + 1):
                self.log.tx(frame)
                t0 = time.time()
                s.sendto(frame, (self.host, self.port))
                try:
                    data = s.recv(64)
                except OSError:
                    data = b""
                dt = (time.time() - t0) * 1000
                if data[:1] == HEALTH_BYTE:
                    self.log.rx(data)
                    return dt
                if data:
                    self.log.rx(data)
                if attempt < self.retries:
                    self.log.retry()
                    time.sleep(self.retry_delay)
            self.log.miss()
            return None
        finally:
            s.close()


def run_pattern(name, args, st, log):
    """Send frames of one pattern; returns after st.frames (or forever if loop)."""
    need = {"solid": 1, "chase": 1, "blink": 1, "fade": 2}.get(name, 0)
    if name in ("solid", "chase", "fade"):  # blink may omit its color
        if len(args) < need:
            print(f"usage: {name} <colors as rrggbb> (need {need}, got {len(args)})")
            return
    step = 0
    while True:
        leds = make_colors(name, args, st["leds"], step)
        frame = build_frame(leds)
        if st["dry"]:
            print(f"frame {step}: {frame.hex(' ')} ({len(frame)} bytes)")
            log.tx(frame)
            ack = 0.0
        else:
            ack = st["sender"].send(frame)
            if ack is None:
                print(f"frame {step}: NO health byte!")
            else:
                print(f"frame {step}: ok, ack 0x80 in {ack:.0f}ms")
        time.sleep(1.0 / st["fps"])
        step += 1
        if not st["loop"] and step >= st["frames"]:
            return


def make_sender(st, log):
    """Sender with current settings; resend delay never exceeds half the frame interval."""
    interval = 1.0 / st["fps"] if st["fps"] > 0 else 0.2
    return Sender(st["host"], st["port"], log,
                  retries=st.get("retries", 2),
                  retry_delay=max(0.02, min(0.2, interval / 2)))


MENU = """
--- Leuchter menu ---------------------------------------------
Patterns: solid <c> | rainbow | fade <a> <b> | chase <c> |
          blink <c> | random | off          (colors as rrggbb)
Settings: fps <f> | leds <n 1..99> | host <ip> | port <p> |
          frames <n> | loop            (loop = send forever)
          retries <n>        (resend frame if no ack, default 2)
Other:    log      (show last 20 log lines) | help | quit
Example:  chase ff0000
---------------------------------------------------------------"""


def menu(st, log):
    print(MENU)
    while True:
        try:
            line = input("leuchter> ").strip()
        except (EOFError, KeyboardInterrupt):
            return
        if not line:
            continue
        parts = line.split()
        cmd = parts[0].lower()
        if cmd in ("quit", "exit", "q"):
            return
        if cmd in ("help", "?", "menu"):
            print(MENU)
        elif cmd == "log":
            try:
                with open(log.path) as f:
                    for l in f.read().splitlines()[-20:]:
                        print(l)
            except FileNotFoundError:
                print("(no log yet)")
        elif cmd == "loop":
            st["loop"] = not st["loop"]
            print(f"loop: {'on' if st['loop'] else 'off'}")
        elif cmd in ("fps", "leds", "frames"):
            try:
                st[cmd] = float(parts[1]) if cmd == "fps" else int(parts[1])
                if cmd == "fps" and st["fps"] <= 0:
                    raise SystemExit("fps must be > 0")
                if cmd == "leds" and not 1 <= st["leds"] < 100:
                    raise SystemExit("leds must be 1..99")
                if cmd == "fps" and st["sender"] is not None:
                    st["sender"] = make_sender(st, log)  # keeps retry delay in sync
                print(f"{cmd} = {st[cmd]}")
            except (IndexError, ValueError):
                print(f"usage: {cmd} <number>")
        elif cmd == "host":
            if len(parts) > 1:
                st["host"] = parts[1]
                st["sender"] = make_sender(st, log)
            print(f"host = {st['host']}")
        elif cmd == "port":
            try:
                st["port"] = int(parts[1])
                st["sender"] = make_sender(st, log)
                print(f"port = {st['port']}")
            except (IndexError, ValueError):
                print("usage: port <number>")
        elif cmd == "retries":
            try:
                st["retries"] = max(0, int(parts[1]))
                if st["sender"] is not None:
                    st["sender"] = make_sender(st, log)
                print(f"retries = {st['retries']}")
            except (IndexError, ValueError):
                print("usage: retries <number>")
        elif cmd in PATTERNS:
            run_pattern(cmd, parts[1:], st, log)
        else:
            print(f"unknown command: {cmd} (type help)")


def main():
    ap = argparse.ArgumentParser(description="Send Hyperion UDP color frames to the Leuchter")
    ap.add_argument("--host", help="Leuchter IP")
    ap.add_argument("--port", type=int, default=1900)
    ap.add_argument("--leds", type=int, default=60)
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--loop", action="store_true", help="run forever")
    ap.add_argument("--frames", type=int, default=20, help="frames per run (default 20)")
    ap.add_argument("--dry", action="store_true", help="no UDP, print hex dump")
    ap.add_argument("--log", dest="logfile", default="ledsender.log",
                    help="packet log file (default ./ledsender.log, '-' = no logging)")
    ap.add_argument("--retries", type=int, default=2,
                    help="resend frame if no 0x80 came back (default 2, 0 = off)")
    ap.add_argument("pattern", nargs="?", choices=PATTERNS, help="omitted -> interactive menu")
    ap.add_argument("colors", nargs="*", help="colors as rrggbb")
    a = ap.parse_args()

    if not 1 <= a.leds < 100:
        raise SystemExit("--leds must be 1..99")

    log = PktLog(None if a.logfile == "-" else a.logfile)

    st = {"host": a.host or "127.0.0.1", "port": a.port, "leds": a.leds,
          "fps": a.fps, "loop": a.loop, "frames": a.frames, "dry": a.dry,
          "retries": max(0, a.retries), "sender": None}
    if not a.dry:
        st["sender"] = make_sender(st, log)

    if a.pattern:
        if not a.host and not a.dry:
            print(f"WARNING: no --host given, sending to {st['host']}:{a.port} "
                  f"(probably wrong - add --host <ESP-IP>)")
        print(f"pattern={a.pattern} host={st['host']}:{a.port} leds={a.leds} "
              f"fps={a.fps} retries={a.retries} log={a.logfile}")
        run_pattern(a.pattern, a.colors, st, log)
    else:
        if not a.host:
            print("note: no --host given, menu starts with host=127.0.0.1")
        menu(st, log)


if __name__ == "__main__":
    main()
