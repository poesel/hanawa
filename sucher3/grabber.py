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
import re
import shutil
import subprocess
import time

import numpy as np


class WebcamGrabber:
    """Default source: V4L2 webcam at /dev/videoN (UVC compatible models)."""

    def __init__(self, device="/dev/video0", size=None):
        self.device = device
        # "WxH" or None = whatever the cam negotiates. Cams that expose
        # RAW formats (gspca ov534 / PlayStation Eye: YUYV only, no MJPEG)
        # move 4x the bytes at 640x480 vs 320x240, so the small mode is
        # the way to get frames on a slow Pi. Colors are window-sampled
        # from frame space, so a smaller frame costs nothing there.
        self.size = size
        self.wh = None
        if size:
            w, h = size.lower().split("x")
            self.wh = (int(w), int(h))
        self.backend = None       # "bayer" | "cv2" | "ffstream" | "stream" | "v4l2" | "ffmpeg"
        self.cap = None           # persistent cv2 VideoCapture
        self.proc = None          # persistent subprocess stream
        self.buf = b""
        self.cv2_off = False      # set once cv2 proved dead on this device
        self.cv2_fail = 0
        self.ffwh = None          # ffmpeg raw stream frame size
        self.ffn = None           # bytes per raw frame
        self.bayer_fmt = None     # probed (ok, [sizes]) of native Bayer stream
        self.bayer_dead = False   # cam is not Bayer -> fall through chain
        self.bayer_probe_at = 0.0 # last probe time (retry max every 5 s)
        self.bayer_wh = None      # native (w,h) of the Bayer stream
        self.bayer_out = None     # (w,h) of the RGB frame we hand out

    def _try_cv2(self):
        """RGB ndarray off a persistent capture (BGR -> RGB view)."""
        import cv2
        if self.cap is None:
            self.cap = cv2.VideoCapture(self.device)
            if not self.cap.isOpened():
                self.cap = None
                return None
            if self.wh:
                self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.wh[0])
                self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.wh[1])
        ret, frame = self.cap.read()   # BGR ndarray
        if not ret or frame is None:
            return None
        return frame[:, :, ::-1]       # cheap RGB view

    # ---- persistent raw-video stream backend (ffmpeg) --------------------
    # For cams that expose only RAW V4L2 formats and no MJPEG — gspca
    # ov534 / Sony PlayStation Eye is the case in point (YUYV + Bayer).
    # ffmpeg negotiates the format/size with the driver itself, and
    # swscale converts YUYV -> rgb8 in C, so Python just reads w*h*3
    # bytes per frame (an RGB (H,W,3) ndarray). stderr goes to a file:
    # ffmpeg's own progress output would otherwise fill a pipe and
    # stall the process after tens of minutes.
    def _ff_spawn(self):
        args = ["ffmpeg", "-y", "-nostats", "-loglevel", "error",
                "-f", "v4l2"]
        if self.wh:
            args += ["-s", f"{self.wh[0]}x{self.wh[1]}"]
        args += ["-i", self.device, "-f", "rawvideo", "-pix_fmt", "rgb8", "-"]
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                                     stderr=open("/tmp/sucher3_ffmpeg.log", "wb"))
        self.buf = b""
        self.ffwh = self.wh or self._ff_negotiate()
        self.ffn = self.ffwh[0] * self.ffwh[1] * 3 if self.ffwh else None

    def _ff_negotiate(self):
        """Ask ffmpeg what size the cam picks when we don't force one."""
        out = subprocess.run(["ffmpeg", "-y", "-f", "v4l2",
                              "-i", self.device, "-t", "0.1",
                              "-f", "null", "-"],
                             capture_output=True, timeout=15)
        line = out.stderr.decode(errors="ignore")
        m = re.search(r"Video: rawvideo[^,]*, [a-z0-9]+, (\d+)x(\d+)", line)
        return (int(m.group(1)), int(m.group(2))) if m else None

    def grab_ffstream(self, timeout=30):
        """One RGB ndarray off the persistent ffmpeg stream, or None."""
        if self.proc is None or self.proc.poll() is not None:
            try:
                self._ff_spawn()
            except FileNotFoundError:
                return None
        if not self.ffn:
            return None
        deadline = time.time() + timeout
        while len(self.buf) < self.ffn:
            if time.time() > deadline:
                return None
            try:
                chunk = self.proc.stdout.read(self.ffn - len(self.buf))
            except Exception:
                return None
            if not chunk:                    # ffmpeg died -> respawn next call
                return None
            self.buf += chunk
        w, h = self.ffwh
        frame = np.frombuffer(self.buf[:self.ffn], np.uint8)
        self.buf = self.buf[self.ffn:]
        return frame.reshape(h, w, 3).copy()  # rgb8 = already RGB

    # ---- persistent raw V4L2 stream in the cam's NATIVE format -----------
    # The ffmpeg build on the Pi has no v4l2 INPUT demuxer at all ("Cannot
    # find format 'v4l2'"), and the Sony PlayStation Eye (PS3 cam, USB
    # 1415:2000, driver gspca_ov534) offers no MJPEG either - its format is
    # plain Bayer GRBG sensor bytes. v4l2-ctl --stream-mmap keeps that
    # stream flowing; we keep only the LATEST frame per poll (the cam does
    # ~50 fps, the poll much less, so the preview never lags). Sampling
    # needs no true demosaic: the channel means of a rect live right on the
    # Bayer planes (GRBG: G at p00/p11, R at p01, B at p10), and the cheap
    # 2x block upsample below is exact enough for preview + colors.
    def _bayer_probe(self):
        """(ok, [(w,h), ...]) of the cam's FIRST format (the stream uses
        alt 0), or None if the probe itself could not run (device busy/
        initializing -> retried next poll). A WRONG cam format on the other
        hand is decided once and for all."""
        try:
            out = subprocess.run(["v4l2-ctl", f"--device={self.device}",
                                  "--list-formats-ext"],
                                 capture_output=True, timeout=15)
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
            return None
        if out.returncode != 0:
            return None
        txt = out.stdout.decode(errors="ignore")
        m = re.search(r"\[0\]:\s*'([^']*)'", txt)
        if not m:
            return None
        ok = any(b in m.group(1) for b in ("GRBG", "GBRG", "BGGR", "BAYER"))
        # only the sizes of format [0] count -> cut off at the next format
        block = txt.split("[1]:")[0]
        sizes = [(int(a), int(b)) for a, b in
                 re.findall(r"Size:\s+Discrete\s+(\d+)x(\d+)", block)]
        return ok, sizes

    def _bayer_measure(self):
        """Bytes of ONE frame as the stream really delivers it. The format
        list's first size entry is NOT what gspca streams (the PS Eye lists
        320x240 but streams its native 640x480 = 307200 Bayer bytes), and
        a wrong frame size silently drifts the window by a few hundred
        bytes per grab -> the preview looks like a jumping enlarged crop.
        So the size is MEASURED, once, with a single-frame capture."""
        try:
            out = subprocess.run(["v4l2-ctl", f"--device={self.device}",
                                  "--stream-mmap", "--stream-count=1",
                                  "--stream-to=-"],
                                 capture_output=True, timeout=20)
        except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
            return 0
        return len(out.stdout)

    def _bayer_pick(self, sizes):
        """(w,h) the stream uses: the measured frame matched against the
        advertised sizes (largest wins if the measure came back odd)."""
        n = self._bayer_measure()
        for (w, h) in sorted(sizes or [], key=lambda s: s[0] * s[1],
                             reverse=True):
            if w * h == n:
                return (w, h)
        if n >= 4096:                       # unlisted but plausible
            w = int((n * 4 / 3) ** 0.5) & ~1
            h = (n // w) if w else 0
            if w and h and w * h == n:
                return (w, h)
        return max(sizes, key=lambda s: s[0] * s[1]) if sizes else None

    def _bayer_spawn(self):
        self.proc = subprocess.Popen(
            ["v4l2-ctl", f"--device={self.device}", "--stream-mmap",
             "--stream-to=-", "--stream-count=0"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.buf = b""

    def _bayer_rgb(self, raw):
        """GRBG Bayer bytes -> RGB (H,W,3) ndarray. The Bayer 2x2 blocks
        (G R / G B) collapse to ONE pixel each: that decimation is exact
        and costs a quarter of the bytes - no demosaic needed for window
        sampling, and the PS Eye's 640x480 stream lands right on the
        requested 320x240."""
        w, h = self.bayer_wh
        f = np.frombuffer(raw, np.uint8).reshape(h, w)
        g1, g2 = f[0::2, 0::2], f[1::2, 1::2]
        r, b = f[0::2, 1::2], f[1::2, 0::2]
        g = ((g1.astype(np.uint16) + g2.astype(np.uint16)) // 2).astype(np.uint8)
        return self._fit_size(np.dstack((r, g, b)))

    def _fit_size(self, rgb):
        """Resize the decimated frame to the requested --size with cheap
        integer block ops (the common cases are exact: a 640x480 stream
        decimates to exactly 320x240, or a small stream gets upscaled)."""
        ih, iw = rgb.shape[:2]
        want = self.wh
        if not want or (iw, ih) == want:
            self.bayer_out = (iw, ih)
            return rgb
        ow, oh = want
        if ow <= iw and ih <= ih and iw % ow == 0 and ih % oh == 0:
            fy, fx = ih // oh, iw // ow
            rgb = rgb.reshape(oh, fy, ow, fx, 3).mean(axis=(1, 3))
            rgb = rgb.astype(np.uint8)
        elif ow >= iw and oh >= ih and ow % iw == 0 and oh % ih == 0:
            rgb = np.repeat(np.repeat(rgb, oh // ih, 0), ow // iw, 1)
        else:
            rgb = rgb[::max(1, ih // oh), ::max(1, iw // ow)]
        self.bayer_out = rgb.shape[1::-1]
        return rgb

    def grab_bayer(self, timeout=5.0):
        """Latest frame off the native-format stream, as RGB ndarray."""
        if self.proc is None or self.proc.poll() is not None:
            if self.bayer_fmt is None:
                if time.time() - self.bayer_probe_at < 5.0:
                    return None      # probe retry scheduled, device busy
                self.bayer_probe_at = time.time()
                self.bayer_fmt = self._bayer_probe()
            if self.bayer_fmt is None:
                return None          # probe failed -> retry next polls
            ok, sizes = self.bayer_fmt
            if not ok or not sizes:
                self.bayer_dead = True
                return None
            wh = self._bayer_pick(sizes)
            if wh is None:
                self.bayer_dead = True
                return None
            self.bayer_wh = wh       # the stream's native size wins
            try:
                self._bayer_spawn()
            except FileNotFoundError:
                return None
        w, h = self.bayer_wh
        ffn = w * h                   # Bayer = ONE byte per pixel
        deadline = time.time() + timeout
        while len(self.buf) < ffn:
            if time.time() > deadline or self.proc.poll() is not None:
                return None
            try:
                self.buf += self.proc.stdout.read(ffn)
            except Exception:
                return None
        # only the LATEST frame matters
        frames = len(self.buf) // ffn
        raw = self.buf[(frames - 1) * ffn: frames * ffn]
        self.buf = self.buf[frames * ffn:]
        return self._bayer_rgb(raw)

    # ---- persistent MJPEG stream backend (v4l2-ctl) --------------------
    def _stream_spawn(self):
        args = ["v4l2-ctl", f"--device={self.device}"]
        if self.wh:
            args += ["-s", f"{self.wh[0]}x{self.wh[1]}"]
        self.proc = subprocess.Popen(
            args + ["--capture-mjpeg", "--capture-to", "-",
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
        args = ["v4l2-ctl", f"--device={self.device}"]
        if self.wh:
            args += ["-s", f"{self.wh[0]}x{self.wh[1]}"]
        out = subprocess.run(args + ["--to-jpg",
                                     "/tmp/sucher3_frame.jpg"],
                             capture_output=True, timeout=10)
        if out.returncode != 0:
            return None
        with open("/tmp/sucher3_frame.jpg", "rb") as f:
            return f.read()

    def grab_ffmpeg(self):
        args = ["ffmpeg", "-y", "-f", "v4l2"]
        if self.wh:
            args += ["-s", f"{self.wh[0]}x{self.wh[1]}"]
        out = subprocess.run(args + ["-i", self.device,
                                     "-frames:v", "1", "-f", "jpg", "-"],
                             capture_output=True, timeout=15)
        return out.stdout if out.stdout[:2] == b"\xff\xd8" else None

    def grab(self, timeout=30):
        """Returns RGB ndarray or None. Native-format Bayer stream first
        (works on the gspca cams like the PlayStation Eye that expose no
        MJPEG at all), then cv2 (persistent object = real webcam fps for
        cams OpenCV understands), then the ffmpeg raw stream, then MJPEG
        stream, then per-call grabs (those decode their JPEG once).
        A backend that proves itself dead (cv2 stuck behind gspca) gets
        turned OFF — reopening the capture every frame resets the device
        and starves every other process using it."""
        if not self.bayer_dead:
            arr = self.grab_bayer(timeout=timeout)
            if arr is not None:
                self.backend = "bayer"
                return arr
            if self.bayer_fmt is None:
                # cam still initializing -> nobody else may open the
                # device now (a second opener behind a gspca stream
                # resets it); the probe gets retried on the next poll
                return None
        try:
            if not self.cv2_off:
                arr = self._try_cv2()
                if arr is not None:
                    self.backend = "cv2"
                    self.cv2_fail = 0
                    return arr
                self.cv2_fail += 1
                if self.cv2_fail >= 3:
                    self.cv2_off = True
                    if self.cap is not None:
                        try:
                            self.cap.release()
                        except Exception:
                            pass
                        self.cap = None
        except ImportError:
            self.cv2_off = True
        except Exception:
            self.cap = None

        if self.backend in (None, "ffstream"):
            arr = self.grab_ffstream(timeout=timeout)
            if arr is not None:
                self.backend = "ffstream"
                return arr
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
