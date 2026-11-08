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

- `LED_COUNT = n < 100` is a compile-time define (`config.h`); must match the sender.
- Magic bytes + checksum validated; invalid packets are rejected (no `0x80` sent, logged to stdout).
- Short/long packets: clamp/truncate to `LED_COUNT`.
- UDP receive buffer: `256` bytes send / `768` bytes receive (`WiFiUDP` in the ESP8261 core supports this at n < 100).
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
│   └── config.h          # WiFi, port, LED_COUNT, brightness, colors
├── tools/
│   └── ledsender.py      # terminal test program
└── test/                 # optional: raw dump of the ESP8261 health bytes
```

### ESP sketch (C++ / Arduino, ESP8261 core)

- `WiFi.h` + `WiFiUDP.h` (from the ESP8261 core, also used by the classic "ESP8261 Ambient Light" sketches).
- LEDs driven by **FastLED** (n = 60 default, max 99; `strip.begin()` / `show()`); APA102 supported by FastLED. Fallback to a hand-written RGB buffer to save RAM if FastLED proves too heavy.
- `loop()` is completely UDP-driven: `beginReceive`/`nextPacket` with ring logic, frame render, single `0x80` byte back on the same UDP connection.
- Optional brightness define `MAX_BRIGHTNESS` for all LEDs.
- Simple status web page (same `WebServer` as in `sucher/`) for debugging — optional.

### `tools/ledsender.py` (Python 3, stdlib only)

Terminal program: build Hyperion UDP frames, send them to the ESP via `socket`, wait for the `0x80` reply (and report if missing) for each frame.

- CLI: `python3 ledsender.py --host <ip> --port 1900 --leds 60 <pattern> [args]`
- Patterns: `solid <#rrggbb>`, `rainbow`, `fade <#a> <#b>`, `chase <#c>`, `blink`, `random`, `off`
- `--loop` / `--fps <n>` for continuous playback
- Reports round-trip latency from the `0x80` health byte (nice for debugging)

## Toolchain

| Piece | Installable here? |
|-------|-------------------|
| PlatformIO Core (`pio`) | ✅ `pip install platformio` — network + pip available; `sucher/` was already built with PlatformIO, so it works or is present in some environment |
| ESP8261 core for Arduino (community PlatformIO platform `esp8261`) | ✅ pulled automatically by `pio platform install esp8261` at build time |
| FastLED | ✅ pulled by `pio device install --library fastled` |
| Python 3 | ✅ already installed |

Nothing blocks as install issue — no need to ask about missing software.

## Tasks

- [x] 1. Scaffold `leuchter/` (platformio.ini, src, tools)
- [x] 2. `config.h`: WiFi + `LED_COUNT` + UDP port (default **1900**)
- [x] 3. ESP sketch: WiFi connect, `WiFiUDP` receive loop
- [x] 4. Hyperion protocol parser (magic + checksum)
- [x] 5. FastLED setup + frame render
- [x] 6. Health reply `0x80` after each frame
- [x] 7. `ledsender.py` with patterns (incl. full UDP round-trip test on PC)
- [ ] 8. Compile done ✅ (Flash 26 %, RAM 36 %); flash-test on real ESP8266 pending
- [x] 9. Write `leuchter/README.md`
- [x] 10. Fix root `README.md`: "Leuchter (ESP32)" → "Leuchter (ESP8266)"

### Implementation notes / deviations from plan

- PlatformIO platform: **`espressif8266`** (official, ESP8266 core for Arduino),
  board **`d1_mini`** – the community platform `esp8261` from the plan sketch does not
  exist in the registry; the official espressif8266 core compiles for ESP8261/8266/8285
  and is what also the ESP8266LoRAM (D1 mini) needs.
- API changes in the core: `WiFi.begin()`, `WiFi.isConnected()`, `WiFiUDP::begin(port)`
  + `parsePacket()` (not beginReceive), `remoteIP()`/`remotePort()` → health byte goes
  back to the sender. Header is `<WiFiUdp.h>` (lowercase d).
- FastLED: registry resolves to **3.10.9**; `FastLED.addLeds<APA102, D0, D1, RGB>` +
  `setBrightness()`, APA102 over SPI (data GPIO16, clock GPIO5, RAW pin order).
- Python 3.13 venv at repo root (`.venv/`, git-ignored), system pip PEP 668 locked.

## Open questions (ask before implementation)

1. UDP port? Suggest **1900** (Hyperion's default) — or the Sucher uses another?
2. The Sucher is currently not yet sending frames (see `sucher/src/main.cpp`); it must adopt the same Hyperion protocol + `0x80` answer on its side — coordinated in a follow-up task.
