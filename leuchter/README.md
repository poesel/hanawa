# Hanawa Leuchter

Der **Leuchter** empfängt per-LED colors on the ESP8266 via UDP (Hyperion-Protokol), drives the APA102 LED stripe and sends back health byte `0x80` after every applied frame. See [PLAN.md](PLAN.md) for protocol and plan.

## Hardware

- ESP8266 (**Wemos D1 mini v2.1.0**) with an **APA102 LED stripe** (42 LEDs), driven
  over the ESP's built-in **SPI/HSPI unit**, which is fixed to two pads:
  - clock wire → `GPIO14` (pad **D5**)
  - data wire → `GPIO13` (pad **D7**)

  The stripe's two wires are connected in exactly that arrangement. (Measured with
  `probe_pins/`: originally they sat the other way round, and the stripe's wires
  have since been swapped - the HSPI unit is not re-targetable, see PLAN.md.)
- 5V supply for the ESP, separate PSU for the LEDs, WLAN network with the Sucher.

## Setup

WLAN credentials live in **`src/secrets.h`**, which is .git-ignored and therefore
not committed. Create it from the template and fill in your values:

```bash
cp src/secrets.h.example src/secrets.h   # then edit SSID + password
```

Other settings in `src/config.h`:

```c
#define LED_COUNT     42            // n < 100, must match the sender
#define UDP_PORT      1900          // Hyperion default
```

`LED_COUNT` is also passed as a build flag in `platformio.ini`
(`-DLED_COUNT=42`), and that define wins over `config.h` - change both places
together, otherwise the receiver truncates the frame and rejects it over a
checksum mismatch.

## Building and flashing

PlatformIO via the virtualenv in the repo root (system-pip is locked). Accessing the
serial device `/dev/ttyUSB0` needs the `dialout` group - if the group change is not
active in the current shell, wrap the command in `sg dialout "..."`:

```bash
cd ..
.venv/bin/pio run -d leuchter                        # compile
sg dialout "/home/.../.venv/bin/pio run -d leuchter -t upload"   # flash (eboot)
```

`-d leuchter` selects the main environment (`env.d1mini`); helper sketches need
`-d leuchter/<dir> -e <env>` (see the table below).

At startup the ESP prints a boot banner, initialises the stripe, runs a **boot test
cycle** (white → red → green → blue → off, 1.5 s each - the fastest proof that the
SPI writes reach the stripe), then reports WLAN status (`3 = CONNECTED`,
`1 = SSID not found`, `6 = WRONG PASSWORD`) and "listening for UDP at :1900".

## OTA (Over-The-Air updates)

The sketch carries `ArduinoOTA` (bundled in the ESP8266 core), so after **one last
serial flash** new builds can be pushed over WLAN - no cable, no `sg dialout`:

```bash
cd ..
.venv/bin/pio run -d leuchter -e d1mini -t upload   # once, needs /dev/ttyUSB0
.venv/bin/pio run -d leuchter -e ota -t upload --upload-port 192.168.0.107
```

`env:ota` is the same sources with `upload_protocol = espota`; the ESP's IP is
printed on its console at boot. ArduinoOTA listens on its own port **8266**
(`OTA_PORT` in `config.h`), so it does not clash with the UDP receiver at
`:1900`. Expect the ESP's console to print `OTA start: sketch`, progress,
`OTA finished, rebooting into new sketch` and then the normal boot banner of
the new build.

Caveats:

- The build that is currently on the ESP has no `ArduinoOTA`, so it cannot be
  replaced over the network - the chicken-and-egg first upload must be serial.
- No `setPassword()`/`setPasswordHash()` is used, so any device on the LAN that
  knows the hostname `hanawa-leuchter` can flash it.
- ArduinoOTA writes the new sketch into the free flash **behind** the running
  one, so both copies have to fit (`ESP.getSketchSize() + new ≤ ~1 MB`). With
  OTA support the build sits at ~320 KB, i.e. keep it below roughly 500 KB.
- Cost of OTA support: ~46 KB flash, ~600 B RAM (30.7 % of flash in total).
- A failed upload leaves the old sketch running; the ESP is not bricked.
## Helper sketches for hardware debugging

Two throw-away sub-projects sit next to the sketch; each has its own
`platformio.ini` and can be flashed with `pio run -d <dir> -e <env> -t upload`:

| Directory | Purpose |
|---|---|
| `test_apa102/` | SPI + APA102 only (no WiFi, no UDP): endless `white → red → off → green → blue → off` cycle, 2 Hz. Proves whether the stripe reacts to the ESP's SPI writes at all. |
| `probe_pins/` | Bit-bangs a native APA102 frame to **every** GPIO pair the D1 mini exposes and shows a different (colour, number of lit LEDs) per pair, printed to stdout as well. Used to find out on which physical pads the stripe really sits (the D1 mini v2 generation does not necessarily keep the GPIO numbering of the older boards). |

The stripe keeps its last valid frame over a re-flash, so an "always on"
stripe does not prove that the current firmware writes to it.

## UDP protocol (Hyperion-compatible)

See [PLAN.md](PLAN.md). Short version:

- Sender sends `AA 00 FF <chk> R G B ...` (3 bytes per LED) to port 1900.
- ESP applies the frame to the stripe, then answers with a one-byte UDP packet `0x80`.
- Invalid magic/checksum: packet rejected, no `0x80` sent, logged on stdout.

## Terminal test program

`tools/ledsender.py` (Python 3, stdlib only):

```bash
./tools/ledsender.py --host 192.168.0.107 solid ff0000     # --leds defaults to 42
./tools/ledsender.py --host 192.168.0.107 --fps 25 --loop rainbow
./tools/ledsender.py --host 192.168.0.107 --leds 42 fade ff0000 0000ff
./tools/ledsender.py --host 192.168.0.107 chase 00ff00
```

Patterns: `solid`, `rainbow`, `fade`, `chase`, `blink`, `random`, `off`;
`--loop` / `--frames N` / `--fps F` / `--retries N` (resend a frame if the
`0x80` health byte doesn't come back, default 2, `0` = off); `--dry` prints
the hex dump without UDP. UDP is fire-and-forget, so occasional datagram loss
happens - retries make the frame rate reliable.
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
- ESP prints `invalid UDP packet (N bytes), rejected` and `ledsender.py` reports
  `NO health byte`: the LED count disagrees - `--leds` must equal the `LED_COUNT`
  the *flashed* binary was built with (see the `platformio.ini` build-flag gotcha).
- Stripe does not change color although acks are fine: the stripe keeps its last
  valid frame across re-flashes, so re-run the boot test cycle (white/red/green/blue)
  and check that the clock wire is really on GPIO14 (D5) and the data wire on
  GPIO13 (D7).
- Watching the ESP's console: `tools/serialwatch.py` (needs pyserial from the
  repo's `.venv` plus the `dialout` group):

  ```bash
  sg dialout "/home/markus/Projekte/Hanawa/.venv/bin/python -u leuchter/tools/serialwatch.py --secs 20"
  ```
