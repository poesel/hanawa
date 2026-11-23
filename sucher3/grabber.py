#!/usr/bin/env python3
"""sucher3 - Frame-Source: Raspberry Pi webcam (V4L2/UVC, /dev/videoN).

Primary source since the Canon A80 died. The Logitech QuickCam
(046d:08ae, gspca_zc3xx) hands out COMPRESSED JPEG frames natively;
grab() contract for ALL grabbers here:

    grab() -> RGB numpy array (H, W, 3) or None

No JPEG re-encode per frame: on a Pi-1-class machine imencode costs
~1.5 s/frame while the camera itself does ~18 fps, so the array path
is mandatory. The browser snapshot is JPEG-encoded lazily in main.py
only when /api/snapshot is requested.

Backends tried in order, first that works wins (sticky):
  1. OpenCV cv2.VideoCapture  (works with gspca JPEG cams; persistent)
  2. v4l2-ctl --capture-mjpeg stream   (needs v4l2-ctl capture support)
  3. per-call v4l2-ctl --to-jpg
  4. per-call ffmpeg -f v4l2
The old Canon A80 PTP grabber is kept as --source ptp for reference.
"""
import io
import shutil
import subprocess
import time

import numpy as np


class WebcamGrabber:
    """Default source: V4L2 webcam at /dev/videoN (UVC compatible models)."""

    def __init__(self, device="/dev/video0"):
        self.device = device
        self.backend = None       # "cv2" | "stream" | "v4l2" | "ffmpeg"
        self.cap = None           # persistent cv2 VideoCapture
        self.proc = None          # persistent v4l2-ctl MJPEG stream
        self.buf = b""

    def _try_cv2(self):
        """RGB ndarray off a persistent capture (BGR -> RGB view)."""
        import cv2
        if self.cap is None:
            self.cap = cv2.VideoCapture(self.device)
            if not self.cap.isOpened():
                self.cap = None
                return None
        ret, frame = self.cap.read()   # BGR ndarray
        if not ret or frame is None:
            return None
        return frame[:, :, ::-1]       # cheap RGB view

    # ---- persistent MJPEG stream backend (v4l2-ctl) --------------------
    def _stream_spawn(self):
        self.proc = subprocess.Popen(
            ["v4l2-ctl", f"--device={self.device}",
             "--capture-mjpeg", "--capture-to", "-",
             "--capture-count", "0"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.buf = b""

    def _stream_next(self, timeout=5.0):
        """One JPEG frame off the persistent stream (SOI..EOI split)."""
        deadline = time.time() + timeout
        while True:
            soi = self.buf.find(b"\xff\xd8")
            if soi >= 0:
                eoi = self.buf.find(b"\xff\xd9", soi)
                if eoi >= 0:
                    frame = self.buf[soi:eoi + 2]
                    self.buf = self.buf[eoi + 2:]
                    return frame
            if time.time() > deadline or self.proc.poll() is not None:
                return None
            try:
                self.buf += self.proc.stdout.read(65536)
            except Exception:
                return None
            self.buf = self.buf[-8 * 1024 * 1024:]  # cap buffer

    def grab_stream(self, timeout=10):
        if self.proc is None or self.proc.poll() is not None:
            try:
                self._stream_spawn()
            except FileNotFoundError:
                return None
        return self._stream_next(timeout=timeout)

    def grab_v4l2(self):
        out = subprocess.run(["v4l2-ctl", f"--device={self.device}",
                              "--to-jpg", "/tmp/sucher3_frame.jpg"],
                             capture_output=True, timeout=10)
        if out.returncode != 0:
            return None
        with open("/tmp/sucher3_frame.jpg", "rb") as f:
            return f.read()

    def grab_ffmpeg(self):
        out = subprocess.run(
            ["ffmpeg", "-y", "-f", "v4l2", "-i", self.device,
             "-frames:v", "1", "-f", "jpg", "-"],
            capture_output=True, timeout=15)
        return out.stdout if out.stdout[:2] == b"\xff\xd8" else None

    def grab(self, timeout=30):
        """Returns RGB ndarray or None. cv2 first (persistent object = real
        webcam fps, no per-call overhead), then MJPEG stream, then per-call
        grabs (those decode their JPEG into an array once)."""
        try:
            arr = self._try_cv2()
            if arr is not None:
                self.backend = "cv2"
                return arr
        except ImportError:
            pass
        except Exception:
            self.cap = None
        if self.backend in (None, "stream"):
            data = self.grab_stream(timeout=timeout)
            if data:
                self.backend = "stream"
                return frame_from_jpeg(data)
        if self.backend in (None, "v4l2"):
            try:
                data = self.grab_v4l2()
                if data:
                    self.backend = "v4l2"
                    return frame_from_jpeg(data)
            except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
                pass
        if self.backend in (None, "ffmpeg"):
            try:
                data = self.grab_ffmpeg()
                if data:
                    self.backend = "ffmpeg"
                    return frame_from_jpeg(data)
            except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
                pass
        return None


def frame_from_jpeg(data):
    """Decode JPEG bytes to an RGB numpy array (H,W,3). None on failure."""
    from PIL import Image
    try:
        return np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))
    except Exception:
        return None


class SyntheticGrabber:
    """Test source without camera hardware: a slowly moving color scene,
    delivered as an RGB ndarray like the real sources."""
    def __init__(self, width=320, height=240):
        self.width = width
        self.height = height
        self.t = 0.0

    def grab(self, timeout=30):
        from PIL import Image, ImageDraw
        w, h = self.width, self.height
        self.t += 0.4
        im = Image.new("RGB", (w, h))
        dr = ImageDraw.Draw(im)
        phase = self.t
        for y in range(h // 4, 3 * h // 4):
            for x in range(w // 4, 3 * w // 4, 8):
                hue = (phase + x * 0.01 + y * 0.005) % 1.0
                r = int(128 + 120 * _wave(hue))
                g = int(128 + 120 * _wave(hue + 1 / 3))
                b = int(128 + 120 * _wave(hue + 2 / 3))
                dr.rectangle([x, y, x + 8, y + 1],
                             fill=(r % 256, g % 256, b % 256))
        return np.asarray(im)


def _wave(x):
    import math
    return math.sin(x * 2 * math.pi)


class Gphoto2Grabber:
    # kept from the dead A80 experiment - selectable via --source ptp
    def __init__(self):
        self.tools = [
            ["gphoto2", "--samplecapture", "1", "--filename", "-"],
            ["gphoto2", "--capture-sample", "--filename", "-"],
            ["gphoto2", "--capture-image-and-download", "--filename", "-"],
        ]
        self.last_ok = None  # index of the command variant that worked

    def grab(self, timeout=30):
        """Returns RGB ndarray or None."""
        if shutil.which("gphoto2") is None:
            return None
        start = (self.last_ok if self.last_ok is not None else 0)
        for i in range(len(self.tools)):
            idx = (start + i) % len(self.tools)
            try:
                out = subprocess.run(self.tools[idx], capture_output=True,
                                     timeout=timeout)
            except subprocess.TimeoutExpired:
                continue
            data = out.stdout
            if data[:2] == b"\xff\xd8":  # JPEG SOI
                arr = frame_from_jpeg(data)
                if arr is not None:
                    self.last_ok = idx
                    return arr
        return None
