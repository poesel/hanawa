#!/usr/bin/env python3
"""sucher3 - Probe for the frame source.

Default: webcam (V4L2). Run ON THE PI once the webcam is plugged in.
Answers:  is there a /dev/videoN?  which backend grabs?  how long does
one frame take and what is its size?

  python3 grabtest.py                 # webcam at /dev/video0
  python3 grabtest.py --device /dev/video2
  python3 grabtest.py --size 320x240        # force capture size
  python3 grabtest.py --ptp           # old Canon A80 probe (dead cam)
"""
import glob
import shutil
import subprocess
import time

from grabber import WebcamGrabber, frame_from_jpeg


def run(args, timeout=30):
    if shutil.which(args[0]) is None:
        return None
    try:
        p = subprocess.run(args, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr.decode(errors="ignore")
    except subprocess.TimeoutExpired:
        return None


def show(title, res):
    print("=" * 60)
    print(title)
    if res is None:
        print("  tool not installed")
        return
    code, out, err = res
    print("  exit:", code)
    if err.strip():
        print("  stderr:", err.strip()[:600])
    if out:
        print("  stdout:", out.decode(errors="ignore")[:600])


def report_frame(what, data):
    if data is None:
        return False
    if hasattr(data, "shape"):
        print(f"  {what}: ndarray {data.shape} HxWxC")
        return True
    if not data:
        return False
    im = frame_from_jpeg(data)
    if im is None:
        return False
    print(f"  {what}: {len(data)} bytes, size {(im.shape[1], im.shape[0])}")
    return True


if __name__ == "__main__":
    import sys
    args = sys.argv[1:]
    device = "/dev/video0"
    size = None
    if "--device" in args:
        device = args[args.index("--device") + 1]
    if "--size" in args:
        size = args[args.index("--size") + 1]

    if "--ptp" in args:
        show("camera detection", run(["gphoto2", "--auto-detect"], 15))
        show("abilities", run(["gphoto2", "--abilities"], 15))
        for name, cmd in (("samplecapture",
                           ["gphoto2", "--samplecapture", "1",
                            "--filename", "-"]),
                          ("capture-sample",
                           ["gphoto2", "--capture-sample",
                            "--filename", "-"])):
            t0 = time.time()
            res = run(cmd, 60)
            dt = time.time() - t0
            show(f"{name} -> time {dt:.1f}s", res)
            if res and report_frame(name, res[1]):
                break
    else:
        print("V4L2 devices:", glob.glob("/dev/video*"))
        show("v4l2 tools?", run(["v4l2-ctl", "--list-devices"], 15))
        show("formats (alt 0 first)", run(["v4l2-ctl", f"--device={device}",
                                          "--list-formats-ext"], 15))
        # backend 0: native raw V4L2 stream (Bayer GRBG cams, PS Eye style)
        g = WebcamGrabber(device, size)
        t0 = time.time()
        ok = 0
        first = None
        for i in range(10):
            d = g.grab_bayer(timeout=8)
            if d is not None:
                ok += 1
                first = first if first is not None else d
        dt = time.time() - t0
        print(f"  v4l2-ctl bayer raw stream: {ok}/10 frames in {dt:.1f}s "
              f"-> {ok / dt:.1f} fps")
        report_frame("bayer frame", first)
        if ok:
            # bayer works -> that is what the daemon will use; probing the
            # other backends now would fight over the device with the
            # stream still running
            sys.exit(0)
        # backend 1: OpenCV persistent capture, measure 10 frames
        try:
            import cv2  # noqa
            have_cv2 = True
        except ImportError:
            have_cv2 = False
        if have_cv2:
            g = WebcamGrabber(device, size)
            t0 = time.time()
            ok = 0
            first = None
            for i in range(10):
                d = g.grab()          # cv2 backend preferred
                if d is not None:
                    ok += 1
                    first = first if first is not None else d
            dt = time.time() - t0
            print(f"  cv2: {ok}/10 frames in {dt:.1f}s "
                  f"-> {ok / dt:.1f} fps (backend {'cv2' if ok else '-'})")
            report_frame("first frame", first)
            if ok:
                sys.exit(0)
        else:
            print("  no OpenCV -> try: sudo apt install python3-opencv")
        # backends with timing: persistent stream, then per-call grabs
        g = WebcamGrabber(device, size)
        t0 = time.time()
        ok = 0
        first = None
        for i in range(10):
            d = g.grab_stream(timeout=5)
            if d is not None:
                ok += 1
                first = first if first is not None else d
        dt = time.time() - t0
        print(f"  v4l2-ctl mjpeg stream: {ok}/10 frames in {dt:.1f}s "
              f"-> {ok / dt:.1f} fps")
        report_frame("stream frame", first)
        for name in ("v4l2", "ffmpeg"):
            try:
                t0 = time.time()
                d = (g.grab_v4l2() if name == "v4l2" else g.grab_ffmpeg())
                dt = time.time() - t0
                print(f"  {name}: {'OK' if d else 'FAIL'} "
                      f"({dt:.2f}s/frame)")
                report_frame(name, d)
            except Exception as e:
                print(f"  {name}: FAIL ({type(e).__name__})")
        print("\nmain.py auto-uses the first backend that works.")
