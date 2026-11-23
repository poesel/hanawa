#!/usr/bin/env python3
"""sucher3 - Frame-Source: Raspberry Pi webcam (V4L2/UVC, /dev/videoN).

Primary source since the Canon A80 died. Any model that shows up in
`v4l2-compliance`/`ls /dev/video*` works; three backends are tried,
first that works wins:
  1. OpenCV  cv2.VideoCapture  (persistent, fastest, decodes BGR -> JPEG)
  2. v4l2-ctl --to-jpg         (single frame per call)
  3. ffmpeg -f v4l2            (single frame to stdout)

The old Canon A80 PTP grabber is kept below as fallback/experiment.
"""
import shutil
import subprocess
import time


class WebcamGrabber:
    """Default source: V4L2 webcam at /dev/videoN (UVC compatible models)."""

    def __init__(self, device="/dev/video0"):
        self.device = device
        self.backend = None       # "cv2" | "v4l2" | "ffmpeg", sticky on success
        self.cap = None           # persistent cv2 VideoCapture

    def _try_cv2(self):
        import cv2
        if self.cap is None:
            self.cap = cv2.VideoCapture(self.device)
            if not self.cap.isOpened():
                self.cap = None
                return None
        ret, frame = self.cap.read()   # BGR ndarray
        if not ret:
            return None
        ok, buf = cv2.imencode(".jpg", frame)
        return bytes(buf) if ok else None

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
        """Returns JPEG bytes or None. cv2 is tried first (persistent capture
        object = real webcam fps, no per-call process overhead)."""
        try:
            data = self._try_cv2()
            if data:
                self.backend = "cv2"
                return data
        except ImportError:
            pass
        except Exception:
            self.cap = None
        if self.backend in (None, "v4l2"):
            try:
                data = self.grab_v4l2()
                if data:
                    self.backend = "v4l2"
                    return data
            except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
                pass
        if self.backend in (None, "ffmpeg"):
            try:
                data = self.grab_ffmpeg()
                if data:
                    self.backend = "ffmpeg"
                    return data
            except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
                pass
        return None


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
        """Returns JPEG bytes or None."""
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
                self.last_ok = idx
                return data
        return None


def frame_from_jpeg(data):
    """Decode JPEG bytes to a Pillow Image (RGB). None on failure."""
    from PIL import Image
    import io
    try:
        im = Image.open(io.BytesIO(data)).convert("RGB")
        return im
    except Exception:
        return None


class SyntheticGrabber:
    """Test source without camera hardware: a slowly moving color scene,
    delivered as JPEG bytes like the real PTP source would be. Lets the
    whole pipeline (web UI, window config, colors, UDP sender) be tested
    before the A80 is connected."""
    def __init__(self, width=320, height=240):
        self.width = width
        self.height = height
        self.t = 0.0

    def grab(self, timeout=30):
        from PIL import Image, ImageDraw
        import io
        w, h = self.width, self.height
        self.t += 0.4
        im = Image.new("RGB", (w, h))
        dr = ImageDraw.Draw(im)
        phase = self.t
        # "TV" rectangle in the middle with hue-cycling content
        for y in range(h // 4, 3 * h // 4):
            for x in range(w // 4, 3 * w // 4, 8):
                hue = (phase + x * 0.01 + y * 0.005) % 1.0
                r = int(128 + 120 * _wave(hue))
                g = int(128 + 120 * _wave(hue + 1 / 3))
                b = int(128 + 120 * _wave(hue + 2 / 3))
                dr.rectangle([x, y, x + 8, y + 1],
                             fill=(r % 256, g % 256, b % 256))
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=85)
        return buf.getvalue()


def _wave(x):
    import math
    return math.sin(x * 2 * math.pi)
