# Leuchter – Plan

Der **Leuchter** ist die zweite Hälfte des Hanawa Ambient Lighting System (see root README.md): It receives per-LED colors from the Sucher (ESP32CAM) via UDP, drives the LED stripe and sends a health/sync signal back to the sender.

## Hardware

- **ESP8266 Development Board** – undocumented ESP8266 microcontroller (ESP8266LoRAM), as used in the Wemos D1 mini. ESP8261-compatible: programmed with the Arduino ESP8261 core, no ESP32 toolchain.
- **APA102 LED-stripe** (n < 100 LEDs, 3× RGB bytes per LED), 5V PSU for the LEDs, separate mains power for the board.
- WiFi (same LAN as the Sucher).

## UDP protocol (Hyperion-compatible)

Binary protocol, identical to the Hyperion "UDP LED" protocol, so the Sucher can act as a drop-in Hyperion sender.

### Receiver packet (Sucher → Leuchter), 3 + 3·n bytes

| Byte | Meaning |
|------|---------|
| 0 | `0xAA` (magic) |
| 1 | `0x00` (no command) / `0xAB` (command follows) |
| 2 | `0xFF` (magic) |
| 3 | checksum: low byte of `sum(bytes[0..2] + all color bytes)` |
| 4… | `R G B` triplets, LED 0 … LED n-1 |

### Answer packet (Leuchter → Sucher), 1 byte

| Byte | Meaning |
|------|---------|
| 0 | `0x80` = "OK, frame accepted, LEDs updated" (health/sync signal) |

Rules:

- The number of LEDs addressed is the number of triplets in the received datagram
  (Hyperion's `ledCount`, our sender's `--leds`) — **not** a compile-time constant.
  The only limit is `MAX_LEDS = 150` (receive buffer + driver capacity). The spec
  asked for a compile-time `LED_COUNT < 100`; that constant is gone, see notes.
- Magic bytes + checksum validated; invalid packets are rejected (no `0x80` sent, logged to stdout).
- Short packets (fewer triplets than the stripe has) leave the remaining LEDs black;
  long packets (more than the stripe has) are accepted, the extra blocks are dropped by
  the stripe's connector. No truncation in the parser, the checksum covers the whole payload.
- UDP receive buffer: `454 + 6` bytes receive (derived from `MAX_LEDS`), 8 bytes send
  (`WiFiUDP` in the ESP8266 core).
- `0x80` is only sent **after** the frame has been applied to the stripe → the Sucher can wait for it as a frame-sync signal.

## Software

### Project structure (PlatformIO, mirrors `sucher/`)

```
leuchter/
├── platformio.ini        # platform = esp8261 (community platform, ESP8261 core for Arduino)
├── PLAN.md               # this file
├── README.md             # usage instructions (written during implementation)
├── src/
│   ├── main.cpp          # setup()/loop(): WiFi + UDP receive + LED render + health reply
│   └── config.h          # WiFi, port, MAX_LEDS (capacity), brightness, colors
├── tools/
│   └── ledsender.py      # terminal test program
└── test/                 # optional: raw dump of the ESP8261 health bytes
```

### ESP sketch (C++ / Arduino, ESP8261 core)

- `WiFi.h` + `WiFiUDP.h` (from the ESP8261 core, also used by the classic "ESP8261 Ambient Light" sketches).
- LEDs driven by a **hand-written APA102 driver** (`src/apa102.h/.cpp`, a port of the
  proven driver in `sucher2/leuchter`: core `SPI.begin()` on the ESP8266 HSPI pads
  GPIO14=SCK / GPIO13=MOSI, native APA102 frames, brightness 0..31) — FastLED was
  dropped because its ESP8266-HSPI path never reached the stripe (see notes below).
  `APA102::begin(capacity)` only allocates the pixel buffer (`MAX_LEDS` = 150 → 450 B);
  `APA102::setNumLeds(n)` is set per frame to the datagram's triplet count, so `show()`
  sends exactly n LED blocks. The binary therefore works with any stripe up to 150 LEDs.
  The stripe's two wires
  are connected so that the clock wire sits on GPIO14 (D5) and the data wire on
  GPIO13 (D7); the HSPI unit is fixed to those pads and cannot be re-targeted.
- `loop()` is completely UDP-driven: `beginReceive`/`nextPacket` with ring logic, frame render, single `0x80` byte back on the same UDP connection.
- Optional brightness define `MAX_BRIGHTNESS` for all LEDs.
- Simple status web page (same `WebServer` as in `sucher/`) for debugging — optional.

### `tools/ledsender.py` (Python 3, stdlib only)

Terminal program: build Hyperion UDP frames, send them to the ESP via `socket`, wait for the `0x80` reply (and report if missing) for each frame.

- CLI: `python3 ledsender.py --host <ip> --port 1900 --leds 42 <pattern> [args]`
- Patterns: `solid <#rrggbb>`, `rainbow`, `fade <#a> <#b>`, `chase <#c>`, `blink`, `random`, `off`
- `--loop` / `--fps <n>` for continuous playback
- Reports round-trip latency from the `0x80` health byte (nice for debugging)

## Toolchain

| Piece | Installable here? |
|-------|-------------------|
| PlatformIO Core (`pio`) | ✅ `pip install platformio` — network + pip available; `sucher/` was already built with PlatformIO, so it works or is present in some environment |
| ESP8261 core for Arduino (community PlatformIO platform `esp8261`) | ✅ pulled automatically by `pio platform install esp8261` at build time |
| FastLED | ⚠️ installed but **not used** — dropped from `platformio.ini` (`lib_deps` empty) after it failed to reach the stripe |
| Python 3 | ✅ already installed |

Nothing blocks as install issue — no need to ask about missing software.

## Tasks

- [x] 1. Scaffold `leuchter/` (platformio.ini, src, tools)
- [x] 2. `config.h`: WiFi + LED count + UDP port (default **1900**)
- [x] 3. ESP sketch: WiFi connect, `WiFiUDP` receive loop
- [x] 4. Hyperion protocol parser (magic + checksum)
- [x] 5. LED driver: FastLED setup → replaced by ported APA102 class + frame render
- [x] 6. Health reply `0x80` after each frame
- [x] 7. `ledsender.py` with patterns (incl. full UDP round-trip test on PC)
- [x] 8. Compile + flash done ✅ (Flash 26.2 %, RAM 35.5 %); verified on the real
      ESP8266: `ledsender.py --leds 42` gets `0x80` acks in ~5 ms and the stripe
      visibly follows red → green → blue → off
- [x] 9. Write `leuchter/README.md`
- [x] 10. Fix root `README.md`: "Leuchter (ESP32)" → "Leuchter (ESP8266)"
- [x] 11. OTA: ArduinoOTA wired into the sketch + `env:ota` (`upload_protocol = espota`),
      upload plumbing verified; the one-time serial flash that installs the first
      OTA-capable build is still pending (no `/dev/ttyUSB0` connected)
- [x] 12. OTA round trip done: bootstrap serial flash (`env:d1mini -t upload`) → wireless
      upload (`env:ota -t upload --upload-port 192.168.0.107`) printed
      `OTA start: sketch` / `Result: OK` / `OTA finished, rebooting into new sketch`,
      UDP acks still fine afterwards
- [x] 13. Frame length decides the number of addressed LEDs (no `LED_COUNT` any more):
      `MAX_LEDS 150` capacity, `UDP_BUF_RX = 4 + 3*MAX_LEDS + 6`, `setNumLeds(colors)`
      before `show()` — flashed over OTA and verified with `--leds 42/150/10/1`, all
      frames ack `0x80`

### Implementation notes / deviations from plan

- PlatformIO platform: **`espressif8266`** (official, ESP8266 core for Arduino),
  board **`d1_mini`** – the community platform `esp8261` from the plan sketch does not
  exist in the registry; the official espressif8266 core compiles for ESP8261/8266/8285
  and is what also the ESP8266LoRAM (D1 mini) needs.
- API changes in the core: `WiFi.begin()`, `WiFi.isConnected()`, `WiFiUDP::begin(port)`
  + `parsePacket()` (not beginReceive), `remoteIP()`/`remotePort()` → health byte goes
  back to the sender. Header is `<WiFiUdp.h>` (lowercase d).
- FastLED was tried first and **abandoned**: registry resolves to **3.10.9**;
  `FastLED.addLeds<APA102, …, RGB>` with `FASTLED_ESP8266_RAW_PIN_ORDER`
  (data GPIO16 / clock GPIO5 and also the HSPI pads) compiled but never reached
  the stripe. `lib_deps` is now empty and FastLED is not linked in at all.
- LED driver: port of the proven `APA102` class from `sucher2/leuchter`, which
  uses the ESP8266's built-in **HSPI unit** (fixed pads, `SPI.begin()` =
  SCK=GPIO14/D5, MOSI=GPIO13/D7; the core `SPIClass` refuses any other pin
  combination).
- Board is a **D1 mini v2.1.0** (ESP8266). The v2 pads are labelled only `D0…D8`
  + `LED`; no authoritative GPIO map for that generation was found online, so
  the physical position of the stripe was established empirically with
  `probe_pins/` (bit-bangs a native APA102 frame to all 110 ordered GPIO pairs,
  each pair encodes a unique colour/lit-LED count). It measured the stripe at
  clock=GPIO13 / data=GPIO14, i.e. **swapped** against HSPI. The stripe's two
  wires have since been **swapped** so that clock sits on GPIO14 and data on
  GPIO13, which lets the hardware HSPI (and the old driver) be used again.
  The bit-bang variant that made the swapped wiring work is kept in git history
  (`src/apa102.cpp` history) and documented in `probe_pins/`.
- Gotcha: `MAX_LEDS` is passed as a build flag in `platformio.ini` (`-DMAX_LEDS=150`);
  that define beats the `#ifndef MAX_LEDS` fallback in `src/config.h`, so a stale
  command-line flag silently changes the receive/stripe capacity. (The old failure mode
  of a stale `-DLED_COUNT=40` rejecting 42-LED frames cannot happen any more — there
  is no `LED_COUNT`.)
- Stripe length is not a firmware setting at all: `MAX_LEDS 150` is only the capacity
  (`UDP_BUF_RX = 4 + 3*150 + 6 = 460`, driver pixel buffer 450 B via `malloc`). Each
  frame states its own length, `parseHyperion` checksums the **whole** payload and
  refuses frames with more than `MAX_LEDS` triplets, `renderFrame` sets exactly those
  pixels and calls `setNumLeds(colors)`, so `show()` emits exactly n APA102 LED blocks
  plus an end frame sized after n. An APA102 stripe is a one-way pass-through and
  cannot report its own length, which is why nothing needs to know it.
- `ledsender.py`'s own limit (`MAX_LEDS = 150`) matches, so `--leds` never exceeds
  what the ESP accepts.
- OTA: the sketch runs `ArduinoOTA` (version 1.0, bundled in the ESP8266 core, no
  extra `lib_deps`), hostname `hanawa-leuchter`, port 8266, **no password**.
  `platformio.ini` has a second environment `env:ota` with
  `upload_protocol = espota`, so `pio run -d leuchter -e ota -t upload
  --upload-port <ESP-IP>` pushes the built `.bin` over WLAN (espota.py
  announces itself on a random local port and invites the ESP). Verified plumbing;
  the ESP's current firmware has no OTA listener, so the first OTA-capable build
  still must go over serial (`AVAILABLE: espota, esptool` for this board). Done:
  that bootstrap serial flash happened, and the following wireless upload
  succeeded end to end (`Result: OK`, ESP reboots into the new sketch).
  Costs measured on this project: +46 KB flash / +630 B RAM, build 320,388 B
  (30.7 % of flash) - ArduinoOTA writes the new sketch into the free flash behind
  the running one, so `old + new` has to stay below ~1 MB.
- Python 3.13 venv at repo root (`.venv/`, git-ignored), system pip PEP 668 locked.

## Open questions (ask before implementation)

1. UDP port? Suggest **1900** (Hyperion's default) — or the Sucher uses another?
2. The Sucher is currently not yet sending frames (see `sucher/src/main.cpp`); it must adopt the same Hyperion protocol + `0x80` answer on its side — coordinated in a follow-up task.
