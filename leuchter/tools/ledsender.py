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
    ./ledsender.py --host 192.168.1.101 --fade 3 solid ff0000
    ./ledsender.py --host 192.168.1.101 chase 00ff00
    ./ledsender.py --host 192.168.1.101            <- interactive menu

Run with --dry to see the frames without UDP.

Notes:

- chase (and every other pattern) is sent long enough that every LED has
  been on at least once - see sweep_frames().
- --loop / "loop" in the menu can be stopped by pressing the space bar
  (no Enter needed); on a real terminal the animation keeps running while
  no key is pressed.
- Ctrl+C, SIGTERM and "quit" exit properly: summary of the run, no
  traceback, exit code 0 when every frame got its 0x80 (else 1).
"""

import argparse
import os
import random
import select
import signal
import socket
import sys
import time
import tty
import colorsys
from datetime import datetime

MAGIC_A = 0xAA
MAGIC_B = 0xFF
NO_COMMAND = 0x00
HEALTH_BYTE = b"\x80"

MAX_LEDS = 150   # longest frame this sender can build (3 bytes per LED)

PATTERNS = ("solid", "rainbow", "fade", "chase", "blink", "random", "off")


def parse_hex(s):
    s = s.lstrip("#")
    if len(s) != 6 or not all(c in "0123456789abcdefABCDEF" for c in s):
        raise SystemExit(f"invalid color: {s} (use rrrgggbbb as rrggbb)")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def build_frame(leds, fade_byte=0):
    """Hyperion UDP LED frame: AA 00 FF <checksum> + RGB per LED (+ optional
    trailing fade byte: 50 ms units the LEUCHTER interpolates over, 0 = none)."""
    payload = b""
    for (r, g, b) in leds:
        payload += bytes((r, g, b))
    if fade_byte:
        payload += bytes((max(1, min(255, fade_byte)),))
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


def sweep_frames(name, n):
    """Frames a pattern needs so that every LED has been on at least once."""
    if name == "chase":
        return n + 4  # the 4-LED head visits every position, then runs off the end
    if name == "blink":
        return 2      # one lit frame, one black frame
    return 1


class StopKey:
    """Non-blocking "the space bar stops the run" watcher.

    The terminal is put into raw mode so a key is readable the moment it is
    typed (no Enter needed); select() tells without blocking whether a byte is
    already there, so the animation never waits for input. Only possible on a
    real terminal - with a piped stdin there is no line discipline to switch
    off, which is reported once by note()."""

    def __init__(self):
        self.fd = sys.stdin.fileno() if sys.stdin.isatty() else None
        self.old = None
        self.hit = False
        self.key = "space bar"

    def start(self):
        self.hit = False  # a new run starts with a fresh key state
        if self.fd is None or self.old is not None:
            return
        self.old = tty.setraw(self.fd)  # returns the settings to restore later

    def poll(self):
        """True once a stop key (space, Ctrl+C, Ctrl+Q) was seen."""
        if self.hit:
            return True
        if self.fd is None:
            return False
        try:
            if not select.select([self.fd], [], [], 0)[0]:
                return False
            ch = os.read(self.fd, 1)
        except OSError:  # EOF / no usable stdin any more
            self.fd = None
            return False
        names = {b" ": "space bar", b"\x03": "Ctrl+C", b"\x11": "Ctrl+Q"}
        if ch in names:
            self.hit = True
            self.key = names[ch]
        return self.hit

    def restore(self):
        """Give the terminal back (echo, line buffering, Ctrl+C again)."""
        if self.old is None:
            return
        try:
            tty.tcsetattr(self.fd, tty.TCSAFLUSH, self.old)
        except (OSError, TypeError):
            pass
        self.old = None

    def note(self):
        if self.fd is None:
            print("(stdin is not a terminal: space-bar stop disabled)")


class StopRun(Exception):
    """Raised when Ctrl+C / SIGTERM asks us to leave - exits with a summary."""


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


def run_pattern(name, args, st, log, key):
    """Send frames of one pattern; returns after st.frames (or forever if loop).

    Returns True when the run ended because the space bar was pressed."""
    need = {"solid": 1, "chase": 1, "blink": 1, "fade": 2}.get(name, 0)
    if name in ("solid", "chase", "fade"):  # blink may omit its color
        if len(args) < need:
            print(f"usage: {name} <colors as rrggbb> (need {need}, got {len(args)})")
            return False
    # never stop in the middle of a sweep: chase must light every LED once
    frames = max(st["frames"], sweep_frames(name, st["leds"]))
    if frames > st["frames"]:
        print(f"{name}: {st['leds']} leds -> full sweep needs {frames} frames "
              f"(--frames raised from {st['frames']} to {frames})")
    step = 0
    key.start()
    while True:
        leds = make_colors(name, args, st["leds"], step)
        frame = build_frame(leds, st.get("fade", 0))
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
        st["sent"] += 1
        if ack is None:
            st["missed"] += 1
        else:
            st["acked"] += 1
            st["lat_sum"] += ack
            st["lat_max"] = max(st["lat_max"], ack)
        if key.poll():
            print(f"({key.key} - stopping)")
            key.restore()
            return True
        time.sleep(1.0 / st["fps"])
        step += 1
        if st["loop"]:
            if step == frames:
                print(f"full sweep done ({frames} frames, every led was on once)")
            continue
        if step >= frames:
            key.restore()
            return False


def make_sender(st, log):
    """Sender with current settings; resend delay never exceeds half the frame interval."""
    interval = 1.0 / st["fps"] if st["fps"] > 0 else 0.2
    return Sender(st["host"], st["port"], log,
                  retries=st.get("retries", 2),
                  retry_delay=max(0.02, min(0.2, interval / 2)))


MENU = f"""
--- Leuchter menu ---------------------------------------------
Patterns: solid <c> | rainbow | fade <a> <b> | chase <c> |
          blink <c> | random | off          (colors as rrggbb)
Settings: fps <f> | leds <n 1..{MAX_LEDS}> | host <ip> | port <p> |
          frames <n> | loop            (loop = send forever)
          retries <n>        (resend frame if no ack, default 2)
          fade-sec <s>       (LEUCHTER-side fade: every frame blends into
                              the stripe over s seconds, 0 = instant; the
                              "fade" pattern above is a different thing -
                              it computes the mix on the SENDER side)
Other:    log      (show last 20 log lines) | help | quit
Example:  chase ff0000

A running pattern is stopped by the space bar (no Enter needed). chase
always runs long enough that every LED has been on once. quit / Ctrl+C
exit properly and print the run's summary.
---------------------------------------------------------------"""


def menu(st, log, key):
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
            print(f"loop: {'on' if st['loop'] else 'off'}"
                  f"{' - press the space bar to stop it' if st['loop'] else ''}")
        elif cmd in ("fps", "leds", "frames"):
            try:
                st[cmd] = float(parts[1]) if cmd == "fps" else int(parts[1])
                if cmd == "fps" and st["fps"] <= 0:
                    raise SystemExit("fps must be > 0")
                if cmd == "leds" and not 1 <= st["leds"] <= MAX_LEDS:
                    raise SystemExit(f"leds must be 1..{MAX_LEDS}")
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
        elif cmd == "fade-sec":
            try:
                sec = float(parts[1])
                if not 0.0 <= sec <= 25.5:
                    raise SystemExit("fade-sec must be 0..12.75 s")
                st["fade"] = int(round(sec * 20))
                print(f"fade = {sec:g}s on the leuchter (wire byte {st['fade']})")
            except (IndexError, ValueError):
                print("usage: fade-sec <seconds 0..12.75>")
        elif cmd in PATTERNS:
            if run_pattern(cmd, parts[1:], st, log, key):
                print(f"(the {key.key} stopped this pattern)")
        else:
            print(f"unknown command: {cmd} (type help)")


def summary(st, log, why=""):
    """Proper exit: stats of the whole run, no traceback."""
    print(("--- run summary " + why).rstrip())
    print(f"frames: {st['sent']} sent, {st['acked']} got 0x80, "
          f"{st['missed']} had no health byte")
    if st["acked"]:
        print(f"latency: {st['lat_sum'] / st['acked']:.1f} ms avg, "
              f"{st['lat_max']:.0f} ms max")
    if log.path:
        print(f"packet log: {log.path}")
    else:
        print("packet log: disabled (--log -)")


def main():
    ap = argparse.ArgumentParser(description="Send Hyperion UDP color frames to the Leuchter")
    ap.add_argument("--host", help="Leuchter IP")
    ap.add_argument("--port", type=int, default=1900)
    ap.add_argument("--leds", type=int, default=42)
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--loop", action="store_true", help="run forever (space bar stops it)")
    ap.add_argument("--frames", type=int, default=20,
                    help="frames per run (default 20; chase is stretched to a full sweep)")
    ap.add_argument("--dry", action="store_true", help="no UDP, print hex dump")
    ap.add_argument("--log", dest="logfile", default="ledsender.log",
                    help="packet log file (default ./ledsender.log, '-' = no logging)")
    ap.add_argument("--fade", type=float, default=0.0,
                    help="LEUCHTER-side fade: each frame blends in over this "
                        "many seconds (0 = instant, max 25.5)")
    ap.add_argument("--retries", type=int, default=2,
                    help="resend frame if no 0x80 came back (default 2, 0 = off)")
    ap.add_argument("pattern", nargs="?", choices=PATTERNS, help="omitted -> interactive menu")
    ap.add_argument("colors", nargs="*", help="colors as rrggbb")
    a = ap.parse_args()

    if not 1 <= a.leds <= MAX_LEDS:
        raise SystemExit(f"--leds must be 1..{MAX_LEDS}")
    if not 0.0 <= a.fade <= 12.75:
        raise SystemExit("--fade must be 0..12.75 seconds")

    log = PktLog(None if a.logfile == "-" else a.logfile)

    st = {"host": a.host or "127.0.0.1", "port": a.port, "leds": a.leds,
          "fps": a.fps, "loop": a.loop, "frames": a.frames, "dry": a.dry,
          "fade": int(round(a.fade * 20)),  # wire byte: units of 50 ms
          "retries": max(0, a.retries), "sender": None,
          "sent": 0, "acked": 0, "missed": 0, "lat_sum": 0.0, "lat_max": 0.0}
    if not a.dry:
        st["sender"] = make_sender(st, log)

    key = StopKey()
    key.note()

    def stop_run(signum, _frame):
        """Ctrl+C / SIGTERM from outside still exit properly."""
        raise StopRun(f"signal {signum}")

    signal.signal(signal.SIGINT, stop_run)
    signal.signal(signal.SIGTERM, stop_run)

    why = ""
    try:
        if a.pattern:
            if not a.host and not a.dry:
                print(f"WARNING: no --host given, sending to {st['host']}:{a.port} "
                      f"(probably wrong - add --host <ESP-IP>)")
            print(f"pattern={a.pattern} host={st['host']}:{a.port} leds={a.leds} "
                  f"fps={a.fps} retries={a.retries} log={a.logfile}")
            if run_pattern(a.pattern, a.colors, st, log, key):
                why = ("(stopped by the space bar)" if key.key == "space bar"
                       else f"({key.key} - stopping)")
        else:
            if not a.host:
                print("note: no --host given, menu starts with host=127.0.0.1")
            menu(st, log, key)
            why = "(menu quit)"
    except (KeyboardInterrupt, StopRun) as exc:
        why = f"(interrupted: {exc})" if str(exc) else "(interrupted: Ctrl+C)"

    key.restore()
    summary(st, log, why)
    # exit code: 0 when every frame got its 0x80, 1 when the strip missed some
    sys.exit(0 if st["missed"] == 0 else 1)


if __name__ == "__main__":
    main()
