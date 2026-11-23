# sucher3 — Ambilight Sucher on a Raspberry Pi

Same job as sucher2 (the ESP32-CAM sketch), but running on a
**Raspberry Pi** which gets frames from a **webcam** over **V4L2**
(`/dev/videoN`, UVC). The Canon PowerShot A80 turned out broken and was
scrapped — its PTP grabber is still in `grabber.py` as `--source ptp`
for reference. The Leuchter side is untouched.

```
webcam --V4L2/JPEG--> Pi (color sampling) --Hyperion UDP :1900--> Leuchter
                                                ^
                    browser UI on the Pi (window config, brightness, send policy)
```

## Files
| file | role |
|------|------|
| `main.py` | daemon: compute-loop + HTTP server + embedded web UI |
| `windows.py` | exact port of the sucher2 rect math + RMS color sampling |
| `sender.py` | Hyperion UDP frames (`AA 00 FF <chk>` + RGB), brightness scaling, ack stats |
| `grabber.py` | frame source: V4L2 webcam grabber (cv2/v4l2-ctl/ffmpeg), PTP + synthetic sources |
| `grabtest.py` | probe script for the webcam once it is plugged into the Pi |

## Coordinate space
Everything works in the **frame's native pixel space** (webcam: usually
640×480 or whatever `v4l2-ctl` reports). `/api/ambilight` reports it as
`"frameSize": [w,h]`, the JS UI converts canvas↔frame with it. Unlike
sucher2 there is no /2 halving (the Pi has the RAM for it).

## Running on the Pi
```bash
sudo apt install python3-opencv v4l-utils   # Pillow via python3-pillow usually built-in
python3 grabtest.py                       # once, webcam plugged in: which backend, fps, size?
python3 main.py                           # real: webcam on /dev/video0
python3 main.py --device /dev/video2      # other webcam slot
python3 main.py --source synth            # dev without webcam
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

## Webcam / V4L2 notes
- Works with any model the kernel shows as `/dev/videoN` (UVC). When you
  know the model: check `v4l2-ctl --list-devices` and its supported
  sizes with `v4l2-ctl --device=/dev/video0 --list-formats-ext`.
- Backends auto-selected, first that works wins: **OpenCV**
  (`cv2.VideoCapture`, persistent → true webcam fps, 10–30 fps), then
  `v4l2-ctl --to-jpg`, then `ffmpeg -f v4l2` (per-call processes,
  slower but need no Python OpenCV build).
- Grab rate (`--fps`, default 4) is the *camera* rate; the packet rate
  to the Leuchter is the `sendFps` slider in time mode.
- The ack trick: the Leuchter sends `0x80` back to the sender IP on
  port 1900. `sender.py` binds its socket to local port 1900, so the
  same socket sends *and* receives.
- Pi must sit on the same WLAN LAN (target `192.168.0.107:1900`).

## Status
Webcam model not given yet — the pipeline is hardware-ready and fully
tested with `--source synth`; run `grabtest.py` when the webcam is
connected to confirm device, backend and frame size.
