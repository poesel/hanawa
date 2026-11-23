"""sucher3 - Hyperion UDP-Sender (same protocol as sucher2/sucher/src/udp_sender.cpp)

One UDP datagram per frame to LEUCHTER_IP:1900:
  [0] 0xAA [1] 0x00 [2] 0xFF [3] checksum (low byte of sum of bytes[0..2]
      + all color bytes + optional fade byte) [4..] RGB triplets, LED 0..n-1
  optional tail byte [4+3n] = FADE in units of 100 ms (1..255 = 0.1 s ..
      25.5 s, 0/absent = instant): the LEUCHTER animates current -> new
      colors itself, no need to stream the whole fade over the wire.

Packing (final stripe mount, LED 0 at TOP RIGHT, clockwise):
  Right (top->bottom), Bottom (right->left reversed),
  Left (bottom->top reversed), Top (left->right)

Brightness (0..100 %) scales every triplet BEFORE packing; 0 % sends black
frames -> stripe really off.

The Leuchter answers each applied frame with one datagram 0x80 back to the
sender's IP on the same port 1900. To receive those we BIND the sending
socket to local port 1900 as well: a Python datagram socket that is bound
locally uses that port for outgoing packets AND receives incoming ones on
the same socket. (This mirrors the cam's WiFiUDP tx/ack pair.)
"""
import socket
import struct


class HyperionSender:
    def __init__(self, leuchter_ip="192.168.0.107", port=1900, max_leds=150):
        self.leuchter_ip = leuchter_ip
        self.port = port
        self.max_leds = max_leds
        self.brightness = 100  # percent, 0 = off
        self.sent = 0
        self.acked = 0
        self.failed = 0
        # one bound socket: local port 1900 for TX *and* for the 0x80 acks.
        # Bound lazily so a second HyperionSender (e.g. dev vs real target)
        # can exist without clashing on the port.
        self.sock = None

    def _ensure_socket(self):
        if self.sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind(("0.0.0.0", self.port))
            self.sock.settimeout(0.0)
        return self.sock

    def set_brightness(self, percent):
        self.brightness = max(0, min(100, int(percent)))

    def send_frame(self, colors, fade100ms=0):
        """colors: list of (r,g,b) in sucher2's clockwise order (see header).
        fade100ms: optional tail byte in units of 100 ms (1..255) -> the
        LEUCHTER fades current->new itself; 0 = old wire format, instant.
        Returns True if the datagram was sent."""
        n = len(colors)
        if n <= 0:
            return False
        if n > self.max_leds:
            print(f"[UDP] WARN: {n} triplets > max_leds {self.max_leds}, "
                  "frame not sent")
            self.failed += 1
            return False

        pct = self.brightness
        pkt = bytearray(b"\xAA\x00\xFF\x00")
        for (r, g, b) in colors:
            pkt += bytes(((r * pct) // 100, (g * pct) // 100,
                         (b * pct) // 100))
        if fade100ms:
            pkt.append(max(1, min(255, int(fade100ms))))
        pkt[3] = sum(pkt[0:3] + pkt[4:]) & 0xFF

        try:
            self._ensure_socket().sendto(pkt, (self.leuchter_ip, self.port))
        except OSError as e:
            print(f"[UDP] send failed: {e}")
            self.failed += 1
            return False
        self.sent += 1
        return True

    def poll_ack(self):
        """Non-blocking: count a 0x80 health byte if one arrived."""
        if self.sock is None:
            return False
        try:
            data = self.sock.recv(64)
        except OSError:
            return False
        if data and data[0] == 0x80:
            self.acked += 1
            return True
        return False

    def stats(self):
        return {"sent": self.sent, "acked": self.acked,
                "failed": self.failed, "brightness": self.brightness}
