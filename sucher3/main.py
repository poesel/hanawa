#!/usr/bin/env python3
"""sucher3 - Ambilight Sucher for Raspberry Pi + webcam (V4L2)

Does the same as sucher2 (the ESP32-CAM sketch): samples colors along a
trapezoid window on the TV picture and streams Hyperion UDP frames to the
Leuchter at 192.168.0.107:1900 (protocol unchanged:
`AA 00 FF <chk>` + 3 Bytes per LED, clockwise, LED 0 at the stripe's top
right). Plus the same browser web UI (snapshot polling, 4 click-corners,
hSeg/vSeg, brightness slider 0-100 % / 0 = off, /api/stats).

Coordinate space: everything (points, rects, sampling) works in the
FRAME's native pixel space - reported in /api/ambilight as "frameSize".
The JS UI converts canvas <-> frame space with that.

Usage:
  python3 main.py                           # real: webcam on /dev/video0
  python3 main.py --device /dev/video2      # other webcam slot
  python3 main.py --source synth            # no webcam needed (dev/test)
  python3 main.py --leuchter 192.168.0.40   # test sender against udpcatch
"""
import json
import os
import socket
import subprocess
import threading
import time
from datetime import datetime

from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from io import BytesIO

from windows import (calculate_ambilight_windows, sample_colors)
from sender import HyperionSender
from grabber import WebcamGrabber, Gphoto2Grabber, SyntheticGrabber

DEFAULTS = {
    "leuchter_ip": "192.168.0.107",
    "udp_port": 1900,
    "max_leds": 150,
    "points": None,            # set via UI/config; 4 pts in FRAME space
    "hSeg": 40,                # 128-LED stripe: 40 top + 40 bottom
    "vSeg": 26,                # 24 + 24 on the sides
    "brightness": 10,          # percent, 0 = off. This is the honest-color
                               # equivalent of sucher2's 5 %: that sketch
                               # doubled every RMS channel (its step
                               # correction squared the mean), so its 5 %
                               # sent what 10 % of the true values sends
                               # here. Below ~8 % the triplets fall to 4..12
                               # of 255 and the stripe reads as off
    "sendFps": 1.0,            # time-mode: how many UDP packets per second
                               # go to the Leuchter (its own constant rate,
                               # independent of the camera rate)
    "fadeMs": 0,               # fade duration in ms (0..1000, UI steps of
                               # 50 ms): the LEUCHTER blends from its current
                               # colors to the received ones over this time
                               # (protocol tail byte in 50 ms units, so max
                               # 255 = 12.75 s in the protocol; the UI caps
                               # the slider at 1 s). 0 = instant.
                               # Works in BOTH modes.
    "mode": "event",           # "event": one packet per new camera frame
                               # "time": packets at sendFps (re-emitting the
                               # newest measurement; Leuchter does the fade)
}

# TV-off blackout: while the screen is OFF the Pi sends black frames so the
# whole stripe goes completely off (instead of a glow from the room reflected
# off the dark glass).  Two signatures of "off", held for BLACK_HOLD seconds
# on the plain RMS colors as they get sent (no step inflation any more):
# (a) every window at most BLACK_LEVEL bright, or
# (b) FLAT WARM WASH: whole stripe varies by <= BLACK_SPREAD, red moderate
# (mean >= 60 - the PS Eye amplifies the dark room into ~R127), green a
# third to three quarters of red (its IR flood), blue crushed (<= a quarter
# of red). Saturated content fails one of these (pure red: no green; pure
# green: no red; white page: full blue) -> only the room wash matches.
BLACK_LEVEL = 30
BLACK_SPREAD = 15
BLACK_HOLD = 1.0

STATE = {
    "config": dict(DEFAULTS),
    "result": None,            # dict of the last computed ambilight JSON
    "jpeg": None,              # lazily encoded snapshot for /api/snapshot
    "frame": None,             # last frame as RGB ndarray (H,W,3)
    "frame_size": None,        # (w, h) of the last frame
    "started": time.time(),
    "cam_fps": None,           # exp. moving average of the camera grab rate
    "cam_last": None,          # time of the previous successfully grabbed frame
    "last_colors": None,       # newest measured colors (time-mode re-emits)
    "blackSince": None,        # start time of the current all-dark streak
    "blackout": False,         # TV-off detected -> black frames get sent
}
LOCK = threading.Lock()


class AmbilightStateHandler(SimpleHTTPRequestHandler):
    """HTTP routes mirror the sucher2 sketch (see doc/BENUTZERHANDBUCH.md)."""

    def do_GET(self):
        route = self.uri_path()
        if route == "/":
            self.send_bytes(200, "text/html; charset=utf-8",
                            INDEX_HTML.encode())
        elif route == "/api/snapshot":
            jpg = ensure_snapshot_jpeg()
            if jpg:
                self.send_bytes(200, "image/jpeg", jpg)
            else:
                self.send_bytes(404, "text/plain", b"no frame yet")
        elif route == "/api/ambilight":
            with LOCK:
                res = STATE["result"]
            self.send_json(200, res if res else {"error": "no config"})
        elif route == "/api/points":
            # the 4 clicked corners as the SERVER has them (state.json), so a
            # page reload restores the window instead of asking for it again
            with LOCK:
                pts = STATE["config"]["points"]
                cfg = STATE["config"]
                hs, vs = cfg["hSeg"], cfg["vSeg"]
            self.send_json(200, {"points": [{"x": p[0], "y": p[1]} for p in pts]
                                 if pts else [],
                                 "hSeg": hs, "vSeg": vs})
        elif route == "/api/stats":
            with LOCK:
                sender = SENDER
                cfg = STATE["config"]
                cam = STATE["cam_fps"]
            self.send_json(200, {
                "uptimeMs": int((time.time() - STATE["started"]) * 1000),
                "udp": dict(sender.stats() if sender else {}),
                "cameraFps": round(cam, 2) if cam else None,
                "sendFps": cfg["sendFps"],
                "fadeMs": cfg["fadeMs"],
                "mode": cfg["mode"],
                "blackout": STATE["blackout"],
            })
        else:
            self.send_error(404)

    def do_POST(self):
        route = self.uri_path()
        body = self._read_body()
        if route == "/api/config":
            ok, err = apply_config(body)
            self.send_json(200 if ok else 400,
                           {"status": "ok"} if ok else {"error": err})
        elif route == "/api/grid":
            self.send_json(200, grid_response(body))
        elif route == "/api/brightness":
            value = parse_value(body)
            if value is None:
                self.send_json(400, {"error": "no numeric value"})
                return
            SENDER.set_brightness(value)
            save_state()
            self.send_json(200, {"status": "ok",
                                 "brightness": SENDER.brightness})
        elif route == "/api/sendfps":
            v = parse_number(body)
            if v is None or not (0.05 <= v <= 30.0):
                self.send_json(400, {"error": "sendFps needs 0.05..30"})
            else:
                with LOCK:
                    STATE["config"]["sendFps"] = v
                save_state()
                self.send_json(200, {"status": "ok", "sendFps": v})
        elif route == "/api/fade":
            v = parse_value(body)
            if v is None or not (0 <= v <= 1000):
                self.send_json(400, {"error": "fade needs ms 0..1000 (steps of 50)"})
            else:
                with LOCK:
                    STATE["config"]["fadeMs"] = int(v)
                save_state()
                self.send_json(200, {"status": "ok",
                                     "fadeMs": int(v)})
        elif route == "/api/mode":
            m = (body.get("mode") or body.get("value")
                 if isinstance(body, dict) else body)
            if m not in ("event", "time"):
                self.send_json(400, {"error": 'mode: "event" or "time"'})
            else:
                with LOCK:
                    STATE["config"]["mode"] = m
                save_state()
                self.send_json(200, {"status": "ok", "mode": m})
        else:
            self.send_error(404)

    # ---- helpers ---------------------------------------------------------
    def uri_path(self):
        return self.path.split("?")[0]

    def send_json(self, code, obj):
        self.send_bytes(code, "application/json",
                        json.dumps(obj).encode())

    def send_bytes(self, code, ctype, data):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _read_body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rinput = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw)
        except Exception:
            return raw.decode(errors="ignore")


def ensure_snapshot_jpeg():
    """Encode the cached RGB ndarray as JPEG *only on request*: imencode
    costs seconds per frame on Pi-1-class hardware, so it must never run
    in the grab loop. Result is cached until the next frame replaces it."""
    with LOCK:
        jpg = STATE["jpeg"]
        frame = STATE["frame"]
    if jpg:
        return jpg
    if frame is None:
        return None
    try:
        import cv2
        small = (frame if frame.shape[1] == 320 else
                 cv2.resize(frame, (320, 240), interpolation=cv2.INTER_AREA))
        ok, buf = cv2.imencode(".jpg", small[:, :, ::-1],   # RGB -> BGR
                               [int(cv2.IMWRITE_JPEG_QUALITY), 70])
        if not ok:
            return None
        data = buf.tobytes()
    except (ImportError, AttributeError, TypeError):
        from PIL import Image
        img = Image.fromarray(frame)
        if img.size != (320, 240):
            img = img.resize((320, 240))
        out = BytesIO()
        img.save(out, "JPEG", quality=70)
        data = out.getvalue()
    with LOCK:
        STATE["jpeg"] = data
    return data


def parse_number(body):
    """float from {"value":X} or a bare number string (for sendFps)."""
    v = body.get("value") if isinstance(body, dict) else body
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def parse_value(body):
    if isinstance(body, dict):
        v = body.get("value")
        try:
            return int(v)
        except (TypeError, ValueError):
            return None
    if isinstance(body, str):
        try:
            return int(body)
        except ValueError:
            return None
    return None


def apply_config(body):
    """Update points/hSeg/vSeg from a JSON body (as in sucher2's
    /api/config; points are in FRAME space here)."""
    try:
        data = json.loads(body) if isinstance(body, str) else body
        pts = data["points"]
        if len(pts) != 4:
            return False, "need 4 points"
        with LOCK:
            STATE["config"]["points"] = [(float(p["x"]), float(p["y"]))
                                         for p in pts]
            STATE["config"]["hSeg"] = int(data.get("hSeg")
                                          or STATE["config"]["hSeg"])
            STATE["config"]["vSeg"] = int(data.get("vSeg")
                                          or STATE["config"]["vSeg"])
        save_state()
        return True, None
    except Exception as e:
        return False, str(e)


def grid_response(body):
    """Echo the intermediate points of the trapezoid - same formulas as
    sucher2's handle_grid (hSeg-1 pts between p0-p1 and p2-p3, vSeg-1
    between p1-p2 and p3-p0), order: top, bottom(rev), right, left(rev)."""
    try:
        data = json.loads(body) if isinstance(body, str) else body
        pts = [(float(p["x"]), float(p["y"])) for p in data["points"]]
        h = int(data.get("hSeg", 4))
        v = int(data.get("vSeg", 4))
    except Exception:
        return {"error": "bad body"}
    if len(pts) != 4:
        return {"error": "need 4 points"}

    def interp(a, b, n):
        out = []
        for t in range(1, n):
            f = t / n
            out.append({"x": a[0] + (b[0] - a[0]) * f,
                        "y": a[1] + (b[1] - a[1]) * f})
        return out

    p = pts
    return {"points": interp(p[0], p[1], h) + interp(p[2], p[3], h)
            + interp(p[1], p[2], v) + interp(p[3], p[0], v)}


SENDER = HyperionSender()  # (re)created in main() with the CLI args


# NOTE: no client-side fader anymore since the fade became a PROTOCOL
# feature: every datagram carries a fade byte (units of 50 ms) and the
# Leuchter interpolates cur -> new on its own 40 ms timer. The Pi only
# keeps the latest measured colors so time-mode can re-emit them.

class ComputeLoop:
    """Grabs frames, computes colors, sends UDP frames - like the sketch's
    loop() with calculateAmbilightContinuous() every 100 ms."""

    def __init__(self, source):
        self.source = source

    def run(self, fps):
        while True:
            t0 = time.time()
            self.poll_once()
            dt = time.time() - t0
            time.sleep(max(0.0, 1.0 / fps - dt))

    def poll_once(self):
        cfg = STATE["config"]
        frame = self.source.grab()   # RGB ndarray (H,W,3) or None
        if frame is None:
            return
        h, w = frame.shape[:2]
        # camera fps: exp. moving average over successful grabs
        now = time.time()
        with LOCK:
            if STATE["cam_last"]:
                dt = now - STATE["cam_last"]
                if dt > 0:
                    inst = 1.0 / dt
                    STATE["cam_fps"] = (0.7 * STATE["cam_fps"] + 0.3 * inst
                                        if STATE["cam_fps"] else inst)
            STATE["cam_last"] = now
        # frames are kept for the browser preview even without a window
        # configured (so the UI can show the picture while you click the
        # 4 corners); only the color computation/packets need the points
        if not cfg["points"]:
            with LOCK:
                STATE["jpeg"] = None     # snapshot re-encoded on demand
                STATE["frame"] = frame
                STATE["frame_size"] = (w, h)
            return
        # sucher2 point order: [topLeft, topRight, botRight, botLeft]
        tl, tr, br, bl = cfg["points"]
        top, bottom, left, right = calculate_ambilight_windows(
            tl, tr, bl, br, cfg["hSeg"], cfg["vSeg"])
        topc = sample_colors(frame, w, h, top)
        bottomc = sample_colors(frame, w, h, bottom)
        leftc = sample_colors(frame, w, h, left)
        rightc = sample_colors(frame, w, h, right)

        # packing for the stripe: LED 0 at top right, clockwise
        colors = (list(rightc) + list(reversed(bottomc))
                  + list(reversed(leftc)) + list(topc))

        # TV-off blackout (see BLACK_LEVEL/BLACK_SPREAD): while the screen is
        # dark, the SENT colors are forced black. 'result' keeps the real
        # measurement so the UI overlay still shows the truth.
        # Values here are RMS-means already DOUBLED by the sketch's step
        # correction, so "off" is recognised as: (a) truly dark everywhere,
        # or (b) the FLAT WARM WASH of the PlayStation Eye pointing at a
        # dark TV: red saturated (>=150 after doubling), green ~half of it
        # (IR!), blue crushed (<= a quarter of red) and the whole stripe
        # barely varying. Saturated content (red scene: no green; green
        # scene: no red; white page: lots of blue) fails one of these.
        now = time.time()
        vals = [max(c) for c in colors]
        sr = sum(c[0] for c in colors)
        sg = sum(c[1] for c in colors)
        sb = sum(c[2] for c in colors)
        dark = max(vals) <= BLACK_LEVEL
        wash = ((max(vals) - min(vals)) <= BLACK_SPREAD
                and sr >= 60 * len(colors)
                and 0.3 * sr <= sg <= 0.75 * sr
                and sb <= 0.25 * sr)
        if dark or wash:
            if STATE["blackSince"] is None:
                STATE["blackSince"] = now
            if now - STATE["blackSince"] >= BLACK_HOLD:
                colors = [(0, 0, 0)] * len(colors)
                STATE["blackout"] = True
        else:
            STATE["blackSince"] = None
            STATE["blackout"] = False

        # the fade lives in the PROTOCOL now: both modes send the fade tail
        # byte (50 ms units); the Leuchter animates cur -> new itself
        fade = int(cfg["fadeMs"])
        if cfg["mode"] == "event":
            SENDER.send_frame(colors, fadeMs=fade)
            SENDER.poll_ack()
        else:
            # time mode: only remember the newest measurement, the sender
            # thread re-emits it at sendFps
            with LOCK:
                STATE["last_colors"] = colors

        with LOCK:
            STATE["jpeg"] = None    # snapshot re-encoded on demand
            STATE["frame"] = frame
            STATE["frame_size"] = (w, h)
            STATE["result"] = {
                "timestamp": int(time.time() * 1000),
                "frameSize": [w, h],
                "top": [list(c) for c in topc],
                "bottom": [list(c) for c in bottomc],
                "left": [list(c) for c in leftc],
                "right": [list(c) for c in rightc],
                "topRects": top, "bottomRects": bottom,
                "leftRects": left, "rightRects": right,
            }


def sender_loop():
    """time-mode: re-emit the latest measured colors at sendFps so the
    Leuchter gets fresh targets at a constant rate; the actual blending
    happens on the Leuchter (fade byte). In event mode this does nothing."""
    while True:
        t0 = time.time()
        try:
            if STATE["config"]["mode"] == "time":
                with LOCK:
                    colors = STATE["last_colors"]
                if colors:
                    fade = int(STATE["config"]["fadeMs"])
                    SENDER.send_frame(list(colors), fadeMs=fade)
                    SENDER.poll_ack()
        except Exception as e:  # never kill the loop
            print(f"[sender] {e}")
        dt = time.time() - t0
        rate = STATE["config"]["sendFps"] or 1.0
        time.sleep(max(0.0, 1.0 / rate - dt))


INDEX_HTML = r"""<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>sucher3 - Raspberry Pi + webcam</title>
  <style>
    /* the picture and the controls sit side by side: the image column
       shrinks, the control column never does */
    #layout { display: flex; flex-wrap: nowrap; align-items: flex-start; }
    #stream-container { position: relative; flex: 1 1 640px; min-width: 240px; }
    #stream { display: block; width: 100%; height: auto; }
    #overlay { position: absolute; left: 0; top: 0; width: 100%; height: 100%;
               pointer-events: none; }
    #coords { flex: 0 0 auto; margin-left: 16px; }
    button { margin-top: 10px; }
  </style>
</head>
<body>
  <h1>sucher3 Live View (webcam via V4L2) - v2</h1>
  <div id="camstats" style="font:12px monospace; margin-top:4px;">cam fps: – | mode: event | send: 1 fps | fade: 0 ms (Leuchter)</div>
  <div id="layout">
  <div id="stream-container">
    <img id="stream" width="640" height="480"/>
    <canvas id="overlay" width="640" height="480"></canvas>
  </div>
  <div id="coords">
    <h3>Klick-Koordinaten (image space):</h3>
    <ol id="coordList"></ol>

    <div style="margin-top:10px;">
      <label>Horizontale Segmente:
        <input type="number" id="hSeg" value="40" min="1" style="width:60px;">
      </label>
      <br/>
      <label>Vertikale Segmente:
        <input type="number" id="vSeg" value="26" min="1" style="width:60px;">
      </label>
      <br/>
      <label title="0 = Stripe completely off (black frames are sent). 1..100 = camera color scaled by this %, wide slider for 1% aiming">Brightness (%):
        <input type="range" id="bright" min="0" max="100" value="10"
               step="1" style="width:260px;">
      </label>
      <span id="brightVal">10 %</span>
      <br/>
      <span title="Event: one UDP packet per new image | Time: constant Send-FPS rate">Send-Mode:</span>
      <label for="modeEvent"><input type="radio" name="mode" id="modeEvent" checked> Event</label>
      <label for="modeTime"><input type="radio" name="mode" id="modeTime"> Time</label>
      <span id="modeVal">Event-Mode</span>
      <br/>
      <label title="Time-Mode only: how many UDP packets per second the Leuchter receives regardless of the camera rate">Send-FPS (Time-Mode only):
        <input type="range" id="sendfps" min="0.2" max="5" value="1"
               step="0.1" style="width:120px;">
      </label>
      <span id="sendFpsVal">1 fps</span>
      <br/>
      <label title="fade happens on the LEUCHTER (protocol byte, works in BOTH modes): the stripe interpolates from its current colors to the received ones over this many milliseconds (steps of 50 ms), 0 = jump instantly">Fade (ms, Leuchter):
        <input type="range" id="fade" min="0" max="1000" value="0"
               step="50" style="width:260px;">
      </label>
      <span id="fadeVal">0 ms</span>
    </div>

    <button id="reset" style="margin-top:10px;">Punkte zurückset</button>
    <br/>
    <button id="ambilight" style="margin-top:10px;">Aktualisieren</button>
  </div>
  </div>

  <script>
    // Canvas is 640x480; the frame comes at the camera's own size
    // ("frameSize" from /api/ambilight). scale maps frame -> canvas.
    const overlay = document.getElementById('overlay');
    const ctx = overlay.getContext('2d');
    const streamImg = document.getElementById('stream');
    const coordList = document.getElementById('coordList');
    const resetBtn = document.getElementById('reset');
    const ambilightBtn = document.getElementById('ambilight');
    const brightInput = document.getElementById('bright');

    let points = [];      // FRAME space
    let gridPts = [];     // FRAME space
    let ambilightData = null;
    let brightnessPct = 100;
    let frameSize = null; // [w,h]

    function scale() {
      // frame -> canvas. Prefer the server's frameSize; before the first
      // /api/ambilight answer fall back to the picture's own size, so the
      // click corners and the boxes are already in the right place then
      const f = frameSize ? frameSize[0]
              : (streamImg.naturalWidth || 640);
      return 640 / f;
    }

    function updateSnapshot() {
      fetch('/api/snapshot?t=' + Date.now())
        .then(r => { if (!r.ok) throw new Error('snapshot ' + r.status);
                     return r.blob(); })
        .then(blob => { const url = URL.createObjectURL(blob);
                        streamImg.onload = () => URL.revokeObjectURL(url);
                        streamImg.src = url; })
        .catch(console.error);
    }
    setInterval(updateSnapshot, 1000);
    updateSnapshot();

    function drawPoints() {
      ctx.clearRect(0, 0, overlay.width, overlay.height);
      if (ambilightData) drawAmbilightRects(ambilightData);
      const s = scale();
      ctx.fillStyle = 'red';
      points.forEach(p => { ctx.beginPath();
        ctx.arc(p.x * s, p.y * s, 5, 0, 2 * Math.PI); ctx.fill(); });
      if (points.length === 4) {
        ctx.strokeStyle = 'lime'; ctx.lineWidth = 2;
        ctx.beginPath();
        points.forEach((p, i) => i ? ctx.lineTo(p.x*s, p.y*s)
                                   : ctx.moveTo(p.x*s, p.y*s));
        ctx.closePath(); ctx.stroke();
        ctx.fillStyle = 'yellow';
        gridPts.forEach(g => { ctx.beginPath();
          ctx.arc(g.x * s, g.y * s, 3, 0, 2 * Math.PI); ctx.fill(); });
      }
    }

    function drawAmbilightRects(data) {
      const s = scale();
      [['top'],['bottom'],['left'],['right']].forEach(([side]) => {
        const colors = data[side] || [], rects = data[side+'Rects'] || [];
        for (let i = 0; i < colors.length && i < rects.length; i++) {
          const [r, g, b] = colors[i], q = rects[i];
          const x1 = q.x1*s, y1 = q.y1*s, x2 = q.x2*s, y2 = q.y2*s;
          // the TRUE measured color: the boxes are a diagnostic of what the
          // windows see, dimming them by the LED brightness made them
          // unreadable at the 5 % that this stripe actually uses
          ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
          ctx.fillRect(x1, y1, x2 - x1, y2 - y1);
          ctx.strokeStyle = 'rgba(0,255,0,0.5)'; ctx.lineWidth = 1;
          ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);
        }
      });
    }

    function fetchAmbilightData() {
      fetch('/api/ambilight')
        .then(r => r.text()).then(t => { const d = JSON.parse(t);
          if (!d.error) {
            ambilightData = d;
            if (d.frameSize) frameSize = d.frameSize;
            drawPoints();
          }})
        .catch(console.error);
    }
    setInterval(fetchAmbilightData, 2000);
    fetchAmbilightData();

    function pushBrightness(value) {
      brightnessPct = Math.max(0, Math.min(100, Math.round(value)));
      brightVal.textContent = brightnessPct + ' %';
      fetch('/api/brightness', { method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ value: brightnessPct }) })
        .then(r => r.json()).then(console.log).catch(console.error);
      drawPoints();
    }
    brightInput.addEventListener('input',
      e => pushBrightness(brightInput.value));

    document.getElementById('stream-container').addEventListener('click', e => {
      if (points.length >= 4) return;
      const rect = overlay.getBoundingClientRect();
      // the canvas may be CSS-scaled (the image column shrinks to make room
      // for the controls), so map CSS px -> canvas px -> frame space
      const fx = (e.clientX - rect.left) * (overlay.width / rect.width) / scale();
      const fy = (e.clientY - rect.top) * (overlay.height / rect.height) / scale();
      points.push({ x: fx, y: fy });
      const li = document.createElement('li');
      li.textContent = `(${Math.round(fx)}, ${Math.round(fy)}) frame` ;
      coordList.appendChild(li);
      drawPoints();
      if (points.length === 4) {
        const payload = { points,
          hSeg: parseInt(document.getElementById('hSeg').value) || 40,
          vSeg: parseInt(document.getElementById('vSeg').value) || 26 };
        fetch('/api/grid', { method:'POST',
          headers:{'Content-Type':'application/json'},
          body: JSON.stringify(payload) })
          .then(r=>r.json()).then(d => { gridPts = d.points; drawPoints(); })
          .catch(console.error);
        fetch('/api/config', { method:'POST',
          headers:{'Content-Type':'application/json'},
          body: JSON.stringify(payload) })
          .then(r=>r.json()).then(console.log).catch(console.error);
      }
    });

    resetBtn.addEventListener('click', () => {
      points = []; gridPts = []; ambilightData = null;
      coordList.innerHTML = ''; drawPoints();
    });
    ambilightBtn.addEventListener('click', fetchAmbilightData);

    // ---- the clicked corners survive a page reload -----------------------
    // the daemon keeps them in state.json, so GET /api/points hands them
    // back and the window (plus its grid) is redrawn without clicking again
    function restorePoints() {
      fetch('/api/points').then(r => r.json()).then(d => {
        const pts = d.points || [];
        if (pts.length !== 4) return;
        points = pts.map(p => ({ x: p.x, y: p.y }));
        const h = document.getElementById('hSeg');
        const v = document.getElementById('vSeg');
        if (d.hSeg) h.value = d.hSeg;
        if (d.vSeg) v.value = d.vSeg;
        coordList.innerHTML = '';
        points.forEach(p => {
          const li = document.createElement('li');
          li.textContent = `(${Math.round(p.x)}, ${Math.round(p.y)}) frame`;
          coordList.appendChild(li);
        });
        fetch('/api/grid', { method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ points,
            hSeg: parseInt(h.value) || 40,
            vSeg: parseInt(v.value) || 26 }) })
          .then(r => r.json()).then(g => { gridPts = g.points; drawPoints(); })
          .catch(console.error);
        drawPoints();
      }).catch(console.error);
    }
    restorePoints();

    // ---- cam-fps display + send policy (fps / fade / mode) --------------
    const camStats = document.getElementById('camstats');
    const brightVal = document.getElementById('brightVal');
    const sendFpsVal = document.getElementById('sendFpsVal');
    const fadeVal = document.getElementById('fadeVal');
    const modeVal = document.getElementById('modeVal');
    function fetchStats() {
      fetch('/api/stats').then(r => r.json()).then(d => {
        camStats.textContent =
          `cam fps: ${d.cameraFps ?? '-'} | mode: ${d.mode} | send: ${d.sendFps} fps | fade: ${d.fadeMs} ms (Leuchter)` +
          (d.blackout ? ' | TV off: LEDs out' : '');
        // the small labels beside the controls always show what the
        // SERVER has - if you drag a slider and this number follows,
        // the value made it to the daemon
        brightVal.textContent = `${d.udp.brightness} %`;
        sendFpsVal.textContent = `${d.sendFps} fps`;
        fadeVal.textContent = `${d.fadeMs} ms`;
        modeVal.textContent = d.mode === 'time'
          ? 'Time-Mode (constant rate)' : 'Event-Mode (per image)';
      }).catch(console.error);
    }
    setInterval(fetchStats, 2000);
    fetchStats();

    function pushSendFps(value) {
      fetch('/api/sendfps', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ value: parseFloat(value) }) })
        .then(r => r.json()).then(console.log).catch(console.error);
    }
    const sendfpsInput = document.getElementById('sendfps');
    sendfpsInput.addEventListener('input',
      e => pushSendFps(sendfpsInput.value));

    function pushFade(value) {
      fetch('/api/fade', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ value: parseInt(value) }) })
        .then(r => r.json()).then(console.log).catch(console.error);
    }
    const fadeInput = document.getElementById('fade');
    fadeInput.addEventListener('input',
      e => pushFade(fadeInput.value));

    // belt and braces: browsers may skip the 'input' event when a slider
    // is dragged fast or the page reloads - re-push the DOM values, the
    // server set is idempotent
    setInterval(() => {
      pushSendFps(sendfpsInput.value);
      pushFade(fadeInput.value);
      pushMode();
    }, 4000);

    function pushMode() {
      // the radio group decides: whichever of the two is checked
      const m = document.getElementById('modeTime').checked
                ? 'time' : 'event';
      fetch('/api/mode', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: m }) })
        .then(r => r.json()).then(console.log).catch(console.error);
    }
    const modeTime = document.getElementById('modeTime');
    const modeEvent = document.getElementById('modeEvent');
    // checking one radio unchecks the other, so 'change' fires on BOTH
    // inputs per click -> push once, the second event is a no-op
    let lastMode = null;
    function pushModeOnce() {
      const m = modeTime.checked ? 'time' : 'event';
      if (m === lastMode) return;
      lastMode = m;
      pushMode();
    }
    [modeTime, modeEvent].forEach(el =>
      el.addEventListener('change', pushModeOnce));
  </script>
</body>
</html>
"""


def load_state():
    """Restore points/segments/brightness from state.json (if present)."""
    try:
        with open("state.json") as f:
            saved = json.load(f)
    except (ValueError, UnicodeError):
        # truncated/empty file (an interrupted write) -> defaults, but the
        # daemon must still start
        print("state.json unreadable, using defaults")
        return
    except FileNotFoundError:
        return
    cfg = STATE["config"]
    cfg.update({k: v for k, v in saved.items() if k in cfg})
    if cfg["points"]:
        cfg["points"] = [tuple(p) for p in cfg["points"]]
    if "brightness" in saved:
        SENDER.set_brightness(saved["brightness"])
    print("state.json loaded")


def save_state():
    with LOCK:
        cfg = {k: STATE["config"][k] for k in ("points", "hSeg", "vSeg",
                "sendFps", "fadeMs", "mode")}
    cfg["brightness"] = SENDER.brightness
    # atomic: a half-written state.json (killed process, crash mid-write)
    # used to brick every later start, load_state then read an empty file
    tmp = "state.json.tmp"
    try:
        with open(tmp, "w") as f:
            json.dump(cfg, f)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, "state.json")
    except OSError:
        pass


def resolve_device(wanted):
    """The /dev/videoN slot of a cam changes when the Pi reboots or another
    cam is plugged (the PS Eye was video1 next to the QuickCam, video0 alone)
    - so a missing slot gets resolved via v4l2-ctl --list-devices: the first
    node of the first REAL USB camera block (platform nodes like bcm2835-*
    are not cameras at all)."""
    if os.path.exists(wanted):
        return wanted
    try:
        out = subprocess.run(["v4l2-ctl", "--list-devices"],
                             capture_output=True, timeout=10)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return wanted
    header = None
    for line in out.stdout.decode(errors="ignore").splitlines():
        if line.strip() and not line.startswith(("\t", " ")):
            header = line.strip()
            if any(t in header.lower() for t in ("bcm2835", "vchiq", "platform",
                                                 "mmal", "codec", "isp")):
                header = None      # not a camera -> skip its nodes
            continue
        if header and "/dev/video" in line:
            found = line.strip()
            print(f"[main] {wanted} is gone -> using {found} "
                  f"(camera '{header}')")
            return found
    print(f"[main] {wanted} is gone and no USB camera found, keeping it")
    return wanted


def main():
    import argparse
    ap = argparse.ArgumentParser(description="sucher3 daemon")
    ap.add_argument("--source", choices=["webcam", "ptp", "synth", "off"],
                    default="webcam",
                    help="webcam = V4L2 (/dev/videoN), ptp = Canon A80 "
                         "(broken, kept for reference)")
    ap.add_argument("--device", default="/dev/video0",
                    help="V4L2 device for the webcam")
    ap.add_argument("--size", default=None,
                    help='force capture size "WxH" (e.g. 320x240). Raw '
                         "cams (gspca ov534 / PlayStation Eye: YUYV, no "
                         "MJPEG) move 4x the bytes at 640x480, so the "
                         "small mode is how frames stay fast on a slow Pi")
    ap.add_argument("--set-ctrl", default="",
                    help="one-time v4l2-ctl -c <ctrls> at startup, e.g. "
                         "\"gain_automatic=0,brightness=200\" to fight dark "
                         "scenes; default: untouched")
    ap.add_argument("--leuchter", default=None,
                    help="target IP (default 192.168.0.107)")
    ap.add_argument("--udp-port", type=int, default=None)
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--fps", type=float, default=4.0,
                    help="camera grab rate (webcam: 2-10 fine; the send "
                         "rate to the Leuchter is sendFps, not this)")
    args = ap.parse_args()

    global SENDER  # the HTTP handlers reference the single sender
    SENDER = HyperionSender(leuchter_ip=args.leuchter
                            or DEFAULTS["leuchter_ip"],
                            port=args.udp_port or DEFAULTS["udp_port"],
                            max_leds=DEFAULTS["max_leds"])

    load_state()

    # the /dev/videoN slot may have moved (Pi reboot / other cam) -> follow
    # the real USB camera instead of dying silently
    args.device = resolve_device(args.device)

    if args.set_ctrl:
        try:
            subprocess.run(["v4l2-ctl", f"--device={args.device}",
                            "-c", args.set_ctrl], capture_output=True,
                            timeout=5)
            print(f"[main] v4l2-ctl -c {args.set_ctrl} applied")
        except (FileNotFoundError, subprocess.TimeoutExpired):
            print("[main] v4l2-ctl not usable, --set-ctrl skipped")

    source = {"webcam": WebcamGrabber(args.device, args.size),
              "ptp": Gphoto2Grabber(),
              "synth": SyntheticGrabber(),
              "off": None}[args.source]
    print(f"[main] source={args.source} device={args.device} size={args.size or 'cam default'} "
          f"leuchter={SENDER.leuchter_ip}:{SENDER.port} "
          f"http=:{args.http_port}")

    if source is not None:
        threading.Thread(target=lambda: ComputeLoop(source).run(args.fps),
                         name="compute").start()
    threading.Thread(target=sender_loop, name="sender").start()

    srv = ThreadingHTTPServer(("0.0.0.0", args.http_port),
                              AmbilightStateHandler)
    print(f"[main] open http://<pi-ip>:{args.http_port}/ in the browser")
    srv.serve_forever()


if __name__ == "__main__":
    main()
