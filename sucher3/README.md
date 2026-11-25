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

## systemd service (boot + journalctl)
The unit lives in this folder as `sucher3.service` (runs as `markus`,
`WorkingDirectory=/home/markus/sucher3` so `state.json` sits next to the
code):

```bash
sudo install -m644 -o root -g root sucher3.service /etc/systemd/system/
sudo ln -sf /etc/systemd/system/sucher3.service \
     /etc/systemd/system/multi-user.target.wants/   # = enable at boot
sudo systemctl start sucher3.service
systemctl status sucher3.service                    # journalctl -u sucher3 -f for logs
```

Verified: `systemctl is-enabled` reports `enabled`, HTTP 200 from the LAN
after start.

## Coordinate space
Everything works in the **frame's native pixel space** (webcam: usually
640×480 or whatever `v4l2-ctl` reports). `/api/ambilight` reports it as
`"frameSize": [w,h]`, the JS UI converts canvas↔frame with it. Unlike
sucher2 there is no /2 halving (the Pi has the RAM for it).

## Color math
`calculate_mean_rgb` keeps the sucher2 RMS (`sqrt(sum(c*c)/n)` over every
`step`-th pixel) but **drops the sketch's `step*step` factor inside the
sqrt**: that multiplied by `step` while `pixelCount` only counted the
sampled pixels, so every channel came in doubled. On the TV that pushed
anything mid-bright to 255 (a green pitch at ~150/200/150 arrived as
white) and the stripe followed nothing. The plain RMS is the honest
mean-square value and keeps the colors distinguishable.

That also re-calibrates the brightness slider: sucher2 looked fine at 5 %
because those were 5 % of the **doubled** values, i.e. 10 % of the honest
ones. Measured live on the Pi: at 5 % the sent triplets were only 4..12 of
255 and the wall ring right around the TV stayed as dark as with the stripe
off, at 100 % the same ring lit up — so the default is 10 % now, and the
sender rounds (`(v * pct + 50) // 100`) instead of truncating so the low
channels keep their hue.

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
stripe), brightness slider 0–100 % in 1 % steps (default 10 % — that is the
honest-color equivalent of sucher2's 5 %, see the color-math note below;
below ~8 % the triplets fall to single digits of 255 and the stripe reads as
off), wide slider for aiming
(0 = off) → `POST /api/brightness`,
plus `GET /api/ambilight`, `/api/stats` (uptime + UDP sent/acked/failed +
brightness). Points, segments and brightness persist to `state.json`,
written atomically (tmp + `os.replace`) and re-read tolerantly — a
truncated file used to throw in `load_state` and make every systemd
start exit 1, which looked like a dead daemon with the port still open.
`GET /api/points` hands the saved corners (plus `hSeg`/`vSeg`) back to the
page, so reloading the UI restores the window — the green quad, the grid
dots and the sampling boxes — instead of asking for 4 new clicks. The
controls sit to the right of the picture: `#layout` is a flex row whose
image column shrinks (`#stream`/`#overlay` follow it at 100 %), so the
click handler converts CSS px → canvas px → frame space.
The preview boxes are filled with the **true measured color** (not dimmed
by the LED brightness) so they stay readable at low brightness.
The sampling boxes reach 2 window segments into the screen (`depth` in
`windows.py`, was 1 in sucher2 — the true halo color of an edge lives
deeper than one band); side boxes adjacent to a corner keep depth 1 so
the deep top/bottom bands own the corner and no box sits inside another.

### Send policy (new in sucher3)
- **Mode switch** (UI radio group, two buttons, default `Event` /
  `POST /api/mode {"mode":"event"|"time"}`):
  - `event` (default): one UDP packet per new camera picture.
  - `time`: packets are emitted at **Send-FPS** independent of the camera
    (UI slider 0.2–5 → `POST /api/sendfps {"value":F}`); each new camera
    measurement only updates the color *target*, the sender re-emits it.
- **Fade slider 0–1000 ms in steps of 50 ms** (`POST /api/fade {"value":MS}`)
  — works in **both** modes: the fade is a PROTOCOL feature, every datagram
  carries a tail byte (duration in **50 ms** units, protocol max 255 =
  12.75 s, the UI slider caps at 1 s) and the LEUCHTER interpolates from its
  current colors to the received ones on its own **2 ms** timer — that is the
  fastest the diodes can display (an APA102 only resets its frame lock after
  2 ms), so the fade is animated at the hardware maximum. The Pi sends no
  intermediate frames. 0 = instant.
- **Neighbour smoothing is on the Leuchter, not on the Pi**: the Pi keeps
  sending the plain RMS colors, the sketch blends every incoming frame with
  its neighbours (`(prev + 2*own + next) / 4`, ring) before it fades to it.
  See `leuchter/src/main.cpp`, `LED_SMOOTH` in `config.h`.
- **TV-off blackout**: while the screen is off the Pi sends black frames, so
  the whole stripe goes completely off (instead of a glow from the room
  reflected in the dark glass). "Off" is held for 1 s (`BLACK_HOLD`) and
  recognised in two ways: (a) every sampled window ≤ 30 bright (`BLACK_LEVEL`)
  — fine for cameras without IR flood; or (b) the **flat warm wash** the
  PlayStation Eye produces when the TV is off: the IR sensitivity amplifies
  the dark room into an almost uniform ~R127/G64/B2 over the *whole* frame.
  The rule (`BLACK_SPREAD`): stripe varies ≤ 15, mean red ≥ 60, green 0.3…0.75×
  red, blue ≤ 0.25× red. Saturated content fails one of these (pure red: no
  green; green: no red; white: full blue) — so it does not mistaken real
  scenes for the wash. The UI status line shows "TV off: LEDs out"
  (`/api/stats` `blackout`), `/api/ambilight` still shows the real measurement.
- **Camera fps** is measured (exp. moving average over successful grabs)
  and shown in the UI status line + reported by `/api/stats` as
  `cameraFps` (plus `sendFps`, `fadeMs`, `mode`).

## Webcam / V4L2 notes
- Works with any model the kernel shows as `/dev/videoN` (UVC). When you
  know the model: check `v4l2-ctl --list-devices` and its supported
  sizes with `v4l2-ctl --device=/dev/video0 --list-formats-ext`.
- Backends auto-selected, first that works wins: **raw Bayer stream**
  (`v4l2-ctl --stream-mmap` + numpy, the PS Eye's case: gspca streams its
  native 640x480 even when the format list offers 320x240 first, so the
  frame size is MEASURED once and the Bayer 2x2 blocks decimated, and the
  format is PINNED with `--set-fmt-video=…pixelformat=GRBG` in both the
  measure and the stream — the device keeps whatever format the last
  program that touched it negotiated, so a `cv2` probe leaves it in YUYV
  (2 bytes/px = 614400 at 640x480) and an unpinned stream then drifts by
  half a frame per grab: jumping, wrongly tinted preview frames. A cheap
  `v4l2-ctl -V` re-check every 5 min re-pins after such a visitor), then
  OpenCV (`cv2.VideoCapture`), ffmpeg raw stream, v4l2-ctl streams,
  per-call `v4l2-ctl --to-jpg`. The Pi's `v4l2-ctl` has no capture
  options and `ffmpeg -f v4l2` fails on this cam.
- The `/dev/videoN` slot is not stable across boots (a Pi reboot moved
  the PS Eye from video1 to video0), so `main.py` resolves the wanted
  device against `v4l2-ctl --list-devices` and follows the real USB cam.
- Grab rate (`--fps`, default 4) is the *camera* rate; the packet rate
  to the Leuchter is the `sendFps` slider in time mode.
- The ack trick: the Leuchter sends `0x80` back to the sender IP on
  port 1900. `sender.py` binds its socket to local port 1900, so the
  same socket sends *and* receives.
- Pi must sit on the same WLAN LAN (target `192.168.0.107:1900`).

## Verified hardware (2026-11, Pi @192.168.0.101 + Logitech QuickCam)
- Pi: Raspbian 13 (trixie), armv6l Pi-1-class, 427 MB RAM. `python3-opencv`
  + `python3-pil` + ffmpeg installed; user must be in the `video` group
  (`sudo usermod -aG video <user>`, log in after).
- Webcam: `046d:08ae` QuickCam for Notebooks, driver `gspca_zc3xx`,
  `/dev/video0`, capture is **compressed JPEG only** at 320×240 /
  640×480. OpenCV decodes it and grabs 640×480 natively.
- Frames flow as **RGB numpy arrays** end to end — never re-encode JPEG
  per frame on this machine (`imencode` costs seconds; the camera does
  ~20 fps). `/api/snapshot` JPEG-encodes lazily on request only.
- Startup takes ~5 s (numpy/cv2 import on Pi1). **Cold-start first
  frame can take ~45 s**: the cam has no exposure control and its
  auto-exposure settles slowly in a dark room; afterwards grabs run at
  17–20 fps (the UI/`--fps` throttles to 4). With the TV on the scene
  is lit and the first frame is quick. HTTP GETs may take seconds while
  the sampling thread saturates the Pi1 CPU — that is normal.
- Live run: `--source webcam`, config 40×26 → `sent` climbs 1:1 per
  grabbed frame (event mode), `failed 0`, `acked 0` while the Leuchter
  is off. `cameraFps: 4.0` in `/api/stats` once a frame landed.

## Second camera: Sony PlayStation Eye (PS3, `1415:2000`)
Plugged into the same Pi → `/dev/video1`, driver **`gspca_ov534`** (mainline
kernel since 2.6.29, OV534 chip + OV7721 sensor). The unit now runs
`--device /dev/video1`, so the Eye is the active source.

- Formats: **`GRBG` (8-bit Bayer) and `YUYV` only — no MJPEG**, so the
  `--capture-mjpeg` stream backend cannot be used here; OpenCV grabs it and
  the kernel demosaics (frames arrive as `(480, 640, 3)` RGB).
- Sizes 320×240 and 640×480; the advertised rates go up to **187 fps**
  (320×240) / 60 fps (640×480) — far above what the Pi-1 CPU can process, so
  the rate is CPU-bound, not camera-bound.
- Exposure/colour **controls exist** (unlike the QuickCam):
  `brightness`, `contrast`, `saturation`, `hue`, `sharpness`,
  `white_balance_automatic`, `auto_exposure` (0=Auto/1=Manual),
  `exposure` (0..255), `gain_automatic`, `gain` (0..63),
  `horizontal_flip`/`vertical_flip`. Set them once from the shell
  (`v4l2-ctl -d /dev/video1 --set-ctrl auto_exposure=1 --set-ctrl exposure=200`)
  — pinning exposure kills the auto-exposure lag a webcam suffers in a
  TV-lit room.
- Fixed focus, two positions chosen by rotating the lens barrel: **56°
  (red dot, close) / 75° (blue dot, long shot)**. Use 56° for a TV at ~3 m.
  No autofocus, so no focus oscillation to fight.
- The 4-capsule mic array also shows up in ALSA (`hw:0,0`); harmless.

## Status
128-LED windows, UDP `sent` climbing, UI served. Fade/event modes and
brightness against the Leuchter still pending once it is powered again.
