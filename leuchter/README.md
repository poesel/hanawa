# Hanawa Leuchter

Der **Leuchter** empfängt per-LED colors on the ESP8266 via UDP (Hyperion-Protokol), drives the APA102 LED stripe and sends back health byte `0x80` after every applied frame. See [PLAN.md](PLAN.md) for protocol and plan.

## Hardware

- ESP8266 (**Wemos D1 mini v2.1.0**) with an **APA102 LED stripe** (42 LEDs on the
  workbench, 108 LEDs of several soldered-together segments at the final position), driven
  over the ESP's built-in **SPI/HSPI unit**, which is fixed to two pads:
  - clock wire → `GPIO14` (pad **D5**)
  - data wire → `GPIO13` (pad **D7**)

  The stripe's two wires are connected in exactly that arrangement. (Measured with
  `probe_pins/`: originally they sat the other way round, and the stripe's wires
  have since been swapped - the HSPI unit is not re-targetable, see PLAN.md.)
  The 42 LEDs only describe the bench stripe; the firmware addresses however many
  LEDs the received frame carries (up to 150), so a longer or shorter stripe works
  with the same binary. The wiring was re-checked at the final position with
  `wire_test/`: its white phase (clock = GPIO14) blinked the first 10 LEDs, its
  red phase (clock = GPIO13) left the stripe dark → the same clock=GPIO14 / data=GPIO13
  arrangement as on the bench. The fault there was the stripe's **power supply**,
  not its wiring.
- 5V supply for the ESP, separate PSU for the LEDs, WLAN network with the Sucher.

## Setup

WLAN credentials live in **`src/secrets.h`**, which is .git-ignored and therefore
not committed. Create it from the template and fill in your values:

```bash
cp src/secrets.h.example src/secrets.h   # then edit SSID + password
```

Other settings in `src/config.h`:

```c
#define MAX_LEDS      150           // capacity: longest frame we accept
#define UDP_PORT      1900          // Hyperion default
```

There is **no LED-count define any more**: the number of LEDs a frame addresses is
the number of RGB triplets in the datagram, i.e. whatever the sender was configured
with (`--leds`, Hyperion's `ledCount`). `MAX_LEDS` (also passed as a build flag
`-DMAX_LEDS=150` in `platformio.ini`, which wins over the `#ifndef` fallback in
`config.h`) only bounds the receive buffer and the driver's pixel buffer
(3 bytes per LED). A stripe of any length up to 150 works with the same binary,
and changing the stripe needs no recompile. If a build flag and `config.h`
disagree, the build flag wins - change both places together.

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
  Once an OTA-capable build is running, everything (including the helper sketches
  below, which carry `upload_protocol = espota` in their own `platformio.ini`) can
  go wireless:

  ```bash
  .venv/bin/pio run -d leuchter/wire_test -e ota -t upload --upload-port 192.168.0.107
  ```
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
| `wire_test/` | OTA-only (the ESP has no serial at its final place): bit-bangs the first 10 LEDs of the 108-LED stripe white/off, and swaps the clock/data roles of GPIO14 and GPIO13 every 10 s. White blink non-stop = wiring as the production sketch assumes, red blink non-stop = the two stripe wires are swapped, nothing at all = not a wiring problem (check the strip's PSU / the first solder joint). Keeps `ArduinoOTA`, so the production build can be pushed back over the air. Measured at the final position: 10 white blinks during the GPIO14-clock phase, the swapped (red) phase left the stripe dark → the clock wire really sits on GPIO14, and the fault was the stripe's own power supply. |

The stripe keeps its last valid frame over a re-flash, so an "always on"
stripe does not prove that the current firmware writes to it.

## UDP protocol (Hyperion-compatible)

See [PLAN.md](PLAN.md). Short version:

- Sender sends `AA 00 FF <chk> R G B ...` (3 bytes per LED) to port 1900.
- ESP applies the frame to the stripe, then answers with a one-byte UDP packet `0x80`.
- Invalid magic/checksum: packet rejected, no `0x80` sent, logged on stdout.
- The receiver accepts frames of **1..`MAX_LEDS` (150) LEDs** and addresses exactly
  as many LEDs as the datagram carries triplets - the frame's own length decides,
  the binary does not know the stripe. The checksum is verified over *every*
  triplet of the received payload. Blocks beyond the physical stripe are dropped by
  the stripe's connector; LEDs behind a shorter frame stay black. The console prints
  e.g. `frame: 150 LEDs -> ack 0x80`.
- The stripe cannot tell the ESP how long it is (APA102 is a one-way pass-through,
  no read-back channel), which is why nothing has to know that number.

## Terminal test program

`tools/ledsender.py` (Python 3, stdlib only):

```bash
./tools/ledsender.py --host 192.168.0.107 solid ff0000     # --leds defaults to 42
./tools/ledsender.py --host 192.168.0.107 --leds 150 solid ff0000   # clipped to the stripe
./tools/ledsender.py --host 192.168.0.107 --fps 25 --loop rainbow
./tools/ledsender.py --host 192.168.0.107 --leds 42 fade ff0000 0000ff
./tools/ledsender.py --host 192.168.0.107 chase 00ff00
```

`--leds` takes **1..150** (`MAX_LEDS` in the script) and defaults to 42, the
stripe currently mounted. The ESP's receiver accepts the same range, and it does
not care how long the stripe is: the sender's `--leds` is what decides how many
LEDs light up. Above 150 the ESP rejects the datagram and the sender reports
`NO health byte`.

Patterns: `solid`, `rainbow`, `fade`, `chase`, `blink`, `random`, `off`;
`--loop` / `--frames N` / `--fps F` / `--retries N` (resend a frame if the
`0x80` health byte doesn't come back, default 2, `0` = off); `--dry` prints
the hex dump without UDP. UDP is fire-and-forget, so occasional datagram loss
happens - retries make the frame rate reliable.
For each frame it prints the round-trip time of the `0x80` health byte.

Two things the tool does on its own:

- **Full sweep**: a pattern is never stopped halfway, so every LED gets lit once.
  `chase` moves a 4-LED head along the stripe, therefore `--frames` is raised
  automatically and says so:
  `chase: 10 leds -> full sweep needs 14 frames (--frames raised from 3 to 14)`.
  `blink` gets at least 2 frames (one lit, one black). In `--loop` mode the
  first round prints `full sweep done (N frames, every led was on once)`.
- **Stop key**: a running pattern is stopped by pressing the **space bar** - no
  Enter needed, and the animation keeps running while no key is pressed
  (`Ctrl+C`/`Ctrl+Q` work as well). This needs a real terminal; with a piped
  stdin the tool prints `(stdin is not a terminal: space-bar stop disabled)`.

Every exit is clean: `quit` in the menu, Ctrl+C, SIGTERM and the space bar all
print a summary instead of a traceback, and the terminal settings are restored,
so the menu keeps working after a pattern was stopped:

```
--- run summary (stopped by the space bar)
frames: 18 sent, 18 got 0x80, 0 had no health byte
latency: 4.8 ms avg, 11 ms max
packet log: ledsender.log
```

The **exit code** is `0` when every frame got its `0x80`, `1` when some had no
health byte - handy for shell checks:

```bash
./tools/ledsender.py --host 192.168.0.107 --leds 10 --frames 3 chase ff0000 || echo "stripe missed frames"
```

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
That also covers the new behaviour - the sweep note, the summary and the exit
code. The **space bar needs a real terminal**, so it only works when you run
the tool in a terminal yourself, not behind `ssh host "ledsender.py ..."` or a
pipe.

## Troubleshooting

- "UDP port failed": port 1900 conflict or socket exhaustion on the ESP.
- Ack times > 50 ms: usually WLAN hiccup, not UDP.
- ESP prints `invalid UDP packet (N bytes), rejected` and `ledsender.py` reports
  `NO health byte`: the datagram is broken - wrong magic, wrong checksum or more
  triplets than `MAX_LEDS` allows (see the build-flag gotcha for `MAX_LEDS`).
  Frames longer or shorter than the physical stripe are *not* an error.
- Stripe does not change color although acks are fine: the stripe keeps its last
  valid frame across re-flashes, so re-run the boot test cycle (white/red/green/blue)
  and check that the clock wire is really on GPIO14 (D5) and the data wire on
  GPIO13 (D7).
- Stripe completely dark at the final position: run `wire_test/` (OTA-only, see the
  helper-sketch table). At this project the result was the white phase blinking and
  the swapped phase doing nothing, i.e. the SPI wiring was fine and the fault was the
  stripe's **PSU** - the 5V supply for the LEDs had to be fixed, not the two wires in
  the ESP's pads. Once the stripe works, push the production build back over the air:

  ```bash
  .venv/bin/pio run -d leuchter -e ota -t upload --upload-port 192.168.0.107
  ```
- Watching the ESP's console: `tools/serialwatch.py` (needs pyserial from the
  repo's `.venv` plus the `dialout` group):

  ```bash
  sg dialout "/home/markus/Projekte/Hanawa/.venv/bin/python -u leuchter/tools/serialwatch.py --secs 20"
  ```
