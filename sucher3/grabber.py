#!/usr/bin/env python3
"""sucher3 - Frame-Source: Canon PowerShot A80 via USB (PTP, class 07).

The A80 is a libgphoto2-supported classic (driver canon/utvdc "canusum"-
line). It has no MJPEG stream: every live frame is ONE PTP transfer over
the slow USB 1.1 pipe. Fastest preview of this camera: the camera's
"sample capture" (small JPEG, ~160x120), which is exactly what we need
for color sampling.

Grab strategy (in order, first that works wins):
  1. gphoto2 --samplecapture 1   (fast low-res JPEG, no shutter release)
  2. gphoto2 --capture-sample
  3. gphoto2 --capture-image-and-download (full res, slow - last resort)

Run `sucher3/grabtest.py` on the Pi to see what the camera really
supports once it is connected (prints size/format of one frame).
"""
import shutil
import subprocess
import time


class Gphoto2Grabber:
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
