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
    "brightness": 100,         # percent, 0 = off
    "sendFps": 1.0,            # time-mode: how many UDP packets per second
                               # go to the Leuchter (its own constant rate,
                               # independent of the camera rate)
    "fadeSeconds": 0,          # fade duration in SECONDS (0..10): the
                               # LEUCHTER blends from its current colors to
                               # the received ones over this time (protocol
                               # tail byte in 100 ms units). 0 = instant.
                               # Works in BOTH modes.
    "mode": "event",           # "event": one packet per new camera frame
                               # "time": packets at sendFps (re-emitting the
                               # newest measurement; Leuchter does the fade)
}

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
                "fadeSeconds": cfg["fadeSeconds"],
                "mode": cfg["mode"],
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
            if v is None or not (0 <= v <= 10):
                self.send_json(400, {"error": "fade needs int 0..10 (s)"})
            else:
                with LOCK:
                    STATE["config"]["fadeSeconds"] = int(v)
                save_state()
                self.send_json(200, {"status": "ok",
                                     "fadeSeconds": int(v)})
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
# feature: every datagram carries a fade byte (units of 100 ms) and the
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

        # the fade lives in the PROTOCOL now: both modes send the fade byte
        # (seconds x 10 = units of 100 ms), the Leuchter animates the mix
        fade = int(cfg["fadeSeconds"])
        if cfg["mode"] == "event":
            SENDER.send_frame(colors, fade100ms=fade * 10)
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
                    fade = int(STATE["config"]["fadeSeconds"])
                    SENDER.send_frame(list(colors), fade100ms=fade * 10)
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
    #stream-container { position: relative; display: inline-block; }
    #overlay { position: absolute; left: 0; top: 0; pointer-events: none; }
    #coords { display: inline-block; vertical-align: top; margin-left: 20px; }
    button { margin-top: 10px; }
  </style>
</head>
<body>
  <h1>sucher3 Live View (webcam via V4L2) - v2</h1>
  <div id="camstats" style="font:12px monospace; margin-top:4px;">cam fps: – | mode: event | send: 1 fps | fade: 0 s (Leuchter)</div>
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
      <label title="0 = Stripe completely off (black frames are sent)">Brightness (%):
        <input type="range" id="bright" min="0" max="100" value="100"
               step="5" style="width:120px;">
      </label>
      <span id="brightVal">100 %</span>
      <br/>
      <label title="unchecked (Event-Mode): the Sucher sends one UDP packet to the Leuchter per new camera image. checked (Time-Mode): UDP packets are sent at a constant rate (Send-FPS); each new image only updates the color TARGET, the Leuchter blends towards it over Fade seconds.">Send-Mode
        (⬜=Event: packet per image | ✅=Time: constant rate):
        <input type="checkbox" id="modeTime">
      </label>
      <span id="modeVal">Event-Mode</span>
      <br/>
      <label title="Time-Mode only: how many UDP packets per second the Leuchter receives regardless of the camera rate">Send-FPS (Time-Mode only):
        <input type="range" id="sendfps" min="0.2" max="5" value="1"
               step="0.1" style="width:120px;">
      </label>
      <span id="sendFpsVal">1 fps</span>
      <br/>
      <label title="fade happens on the LEUCHTER (protocol byte, works in BOTH modes): the stripe interpolates from its current colors to the received ones over this many seconds, 0 = jump instantly">Fade (s, Leuchter):
        <input type="range" id="fade" min="0" max="10" value="0"
               step="1" style="width:120px;">
      </label>
      <span id="fadeVal">0 s</span>
    </div>

    <button id="reset" style="margin-top:10px;">Punkte zurückseet</button>
    <br/>
    <button id="ambilight" style="margin-top:10px;">Aktualisieren</button>
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
    const brightVal = document.getElementById('brightVal');

    let points = [];      // FRAME space
    let gridPts = [];     // FRAME space
    let ambilightData = null;
    let brightnessPct = 100;
    let frameSize = null; // [w,h]

    function scale() { return frameSize ? 640 / frameSize[0] : 1; }

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
          const f = brightnessPct / 100;   // what the Leuchter gets
          ctx.fillStyle = `rgb(${Math.round(r*f)}, ${Math.round(g*f)}, ${Math.round(b*f)})`;
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
      // canvas -> frame space
      const fx = (e.clientX - rect.left) / scale();
      const fy = (e.clientY - rect.top) / scale();
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

    // ---- cam-fps display + send policy (fps / fade / mode) --------------
    const camStats = document.getElementById('camstats');
    const brightVal = document.getElementById('brightVal');
    const sendFpsVal = document.getElementById('sendFpsVal');
    const fadeVal = document.getElementById('fadeVal');
    const modeVal = document.getElementById('modeVal');
    function fetchStats() {
      fetch('/api/stats').then(r => r.json()).then(d => {
        camStats.textContent =
          `cam fps: ${d.cameraFps ?? '-'} | mode: ${d.mode} | send: ${d.sendFps} fps | fade: ${d.fadeSeconds} s (Leuchter)`;
        // the small labels beside the controls always show what the
        // SERVER has - if you drag a slider and this number follows,
        // the value made it to the daemon
        brightVal.textContent = `${d.udp.brightness} %`;
        sendFpsVal.textContent = `${d.sendFps} fps`;
        fadeVal.textContent = `${d.fadeSeconds} s`;
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
    }, 4000);

    function pushMode(on) {
      const m = on ? 'time' : 'event';
      fetch('/api/mode', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: m }) })
        .then(r => r.json()).then(console.log).catch(console.error);
    }
    const modeChk = document.getElementById('modeTime');
    modeChk.addEventListener('click',
      () => pushMode(modeChk.checked));
  </script>
</body>
</html>
"""


def load_state():
    """Restore points/segments/brightness from state.json (if present)."""
    try:
        with open("state.json") as f:
            saved = json.load(f)
        cfg = STATE["config"]
        cfg.update({k: v for k, v in saved.items() if k in cfg})
        if cfg["points"]:
            cfg["points"] = [tuple(p) for p in cfg["points"]]
        if "brightness" in saved:
            SENDER.set_brightness(saved["brightness"])
        print("state.json loaded")
    except FileNotFoundError:
        pass


def save_state():
    with LOCK:
        cfg = {k: STATE["config"][k] for k in ("points", "hSeg", "vSeg",
                "sendFps", "fadeSeconds", "mode")}
    cfg["brightness"] = SENDER.brightness
    try:
        with open("state.json", "w") as f:
            json.dump(cfg, f)
    except OSError:
        pass


def main():
    import argparse
    ap = argparse.ArgumentParser(description="sucher3 daemon")
    ap.add_argument("--source", choices=["webcam", "ptp", "synth", "off"],
                    default="webcam",
                    help="webcam = V4L2 (/dev/videoN), ptp = Canon A80 "
                         "(broken, kept for reference)")
    ap.add_argument("--device", default="/dev/video0",
                    help="V4L2 device for the webcam")
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

    if args.set_ctrl:
        try:
            subprocess.run(["v4l2-ctl", f"--device={args.device}",
                            "-c", args.set_ctrl], capture_output=True,
                            timeout=5)
            print(f"[main] v4l2-ctl -c {args.set_ctrl} applied")
        except (FileNotFoundError, subprocess.TimeoutExpired):
            print("[main] v4l2-ctl not usable, --set-ctrl skipped")

    source = {"webcam": WebcamGrabber(args.device),
              "ptp": Gphoto2Grabber(),
              "synth": SyntheticGrabber(),
              "off": None}[args.source]
    print(f"[main] source={args.source} "
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
