# Hanawa Leuchter

Der **Leuchter** empfängt per-LED colors on the ESP8266 via UDP (Hyperion-Protokol), drives the APA102 LED stripe and sends back health byte `0x80` after every applied frame. See [PLAN.md](PLAN.md) for protocol and plan.

## Hardware

- ESP8266 (Wemos D1 mini) with APA102 LED stripe, connected via **SPI** to the ESP:
  - data pin: `LED_SPI_DATA_PIN` = GPIO16 (D0)
  - clock pin: `LED_SPI_CLOCK_PIN` = GPIO5 (D1)
- 5V supply for the ESP, separate PSU for the LEDs, WLAN network with the Sucher.

## Setup

WLAN credentials live in **`src/secrets.h`**, which is .git-ignored and therefore
not committed. Create it from the template and fill in your values:

```bash
cp src/secrets.h.example src/secrets.h   # then edit SSID + password
```

Other settings in `src/config.h`:

```c
#define LED_COUNT     60            // n < 100, must match the sender
#define UDP_PORT      1900          // Hyperion default
```

## Building and flashing

PlatformIO via the virtualenv in the repo root (system-pip is locked):

```bash
cd ..              # repo root
.venv/bin/pio pkg install          # first time only
cd leuchter
../.venv/bin/pio run               # compile
../.venv/bin/pio run -a upload     # flash ESP8266 (D1 mini, eboot)
../.venv/bin/pio run -a device     # monitor ESP
```

At startup the ESP prints: WLAN connect, local IP, "listening for UDP at :1900".

## UDP protocol (Hyperion-compatible)

See [PLAN.md](PLAN.md). Short version:

- Sender sends `AA 00 FF <chk> R G B ...` (3 bytes per LED) to port 1900.
- ESP applies the frame to the stripe, then answers with a one-byte UDP packet `0x80`.
- Invalid magic/checksum: packet rejected, no `0x80` sent, logged on stdout.

## Terminal test program

`tools/ledsender.py` (Python 3, stdlib only):

```bash
./tools/ledsender.py --host 192.168.1.101 solid ff0000
./tools/ledsender.py --host 192.168.1.101 --fps 25 --loop rainbow
./tools/ledsender.py --host 192.168.1.101 --leds 60 fade ff0000 0000ff
./tools/ledsender.py --host 192.168.1.101 chase 00ff00
```

Patterns: `solid`, `rainbow`, `fade`, `chase`, `blink`, `random`, `off`;
`--loop` / `--frames N` / `--fps F`; `--dry` prints the hex dump without UDP.
For each frame it prints the round-trip time of the `0x80` health byte.

Without `pattern` argument the program opens an **interactive menu** where
you can type the patterns directly plus settings and logging:

```
leuchter> fps 30
leuchter> chase ff0000
leuchter> frames 10
leuchter> log        <- shows last 20 log lines
leuchter> quit
```

Every sent (`TX`) and received (`RX`) UDP packet is appended with timestamp
and hex bytes to a **log file** (default `ledsender.log`, `--log <path>` to
change, `--log -` to disable).

Without an ESP8266 at hand you can test the whole protocol on PC: start a
UDP receiver on port 1900 which answers `0x80` and use `--host 127.0.0.1`.

## Troubleshooting

- "UDP port failed": port 1900 conflict or socket exhaustion on the ESP.
- Ack times > 50 ms: usually WLAN hiccup, not UDP.
- Checksum mismatch between `ledsender.py` and a sender: ensure `--leds` and
  `LED_COUNT` match the number of RGB triplets sent.
