# sucher3 — Ambilight Sucher on a Raspberry Pi

Same job as sucher2 (the ESP32-CAM sketch), but running on a
**Raspberry Pi** which gets frames from a **Canon PowerShot A80** over
USB (**PTP**, class 07 camera, USB 1.1 pipe). The Leuchter side is
untouched.

```
Canon A80 --PTP/JPEG--> Pi (color sampling) --Hyperion UDP :1900--> Leuchter
                                                    ^
                        browser UI on the Pi (window config, brightness)
```

## Files
| file | role |
|------|------|
| `main.py` | daemon: compute-loop + HTTP server + embedded web UI |
| `windows.py` | exact port of the sucher2 rect math + RMS color sampling |
| `sender.py` | Hyperion UDP frames (`AA 00 FF <chk>` + RGB), brightness scaling, ack stats |
| `grabber.py` | frame source: gphoto2 PTP grabber + synthetic test source |
| `grabtest.py` | probe script for the A80 once it is plugged into the Pi |

## Coordinate space
Everything works in the **frame's native pixel space** (the A80 sample
capture is ~160×120 / preview bigger). `/api/ambilight` reports it as
`"frameSize": [w,h]`, the JS UI converts canvas↔frame with it. Unlike
sucher2 there is no /2 halving (the Pi has the RAM for it).

## Running on the Pi
```bash
sudo apt install gphoto2 python3-pillow   # or pip install Pillow
python3 grabtest.py                       # once, camera plugged in: what works?
python3 main.py                           # real: source=ptp
python3 main.py --source synth            # dev without camera
python3 main.py --leuchter 192.168.0.40   # sender test against a catcher
```
Open `http://<pi-ip>:8080/` — same UI as sucher2: snapshot polling, click
4 corners (TL, TR, BR, BL), `hSeg`/`vSeg` (default 40/26 = 128-LED
stripe), brightness slider 0–100 % (0 = off) → `POST /api/brightness`,
plus `GET /api/ambilight`, `/api/stats` (uptime + UDP sent/acked/failed +
brightness). Points, segments and brightness persist to `state.json`.

### Send policy (new in sucher3)
- **Mode switch** (UI checkbox / `POST /api/mode {"mode":"event"|"time"}`):
  - `event` (default): one UDP packet per new camera picture. No fps, no
    fade — the packet rate equals the camera rate.
  - `time`: packets are emitted at **Send-FPS** independent of the camera
    (UI slider 0.2–5 → `POST /api/sendfps {"value":F}`); each new camera
    measurement only updates the color *target*.
- **Fade slider 0–10** (`POST /api/fade {"value":N}`): in time mode the
  transition from the last sent colors to a new measurement is spread
  linearly over N packets (0 = jump immediately with next packet). A new
  measurement during a fade restarts the fade from the current
  interpolated color.
- **Camera fps** is measured (exp. moving average over successful grabs)
  and shown in the UI status line + reported by `/api/stats` as
  `cameraFps` (plus `sendFps`, `fadeFrames`, `mode`).

## A80 / PTP notes
- The A80 has **no video stream**: each live frame is one PTP transfer.
  `--samplecapture` (fast, low-res JPEG) is the intended source; the
  grabber falls back to `--capture-sample`, then full capture.
- USB 1.1 is slow: expect ~0.3–2 fps, not 10. One JPEG per second is
  plenty for the stripe (the Leuchter just holds the last frame).
- The ack trick: the Leuchter sends `0x80` back to the sender IP on
  port 1900. `sender.py` binds its socket to local port 1900, so the
  same socket sends *and* receives — no raw-socket hacks needed.
- Pi must sit on the same WLAN LAN (target `192.168.0.107:1900`).
- USB permission: add `SUBCENTS`/udev rule or just run with the stock
  `usb-file` udev support of gphoto2 (class 07 PTP cameras work without
  special rules on recent Raspberry Pi OS).

## Status
Hardware not connected yet — the full pipeline can be run with
`--source synth` and verified against a UDP catcher before the camera
arrives.
