#!/usr/bin/env python3
"""sucher3 - PTP Probe for the Canon PowerShot A80.

Run this ON THE PI once the camera is plugged in (usb-file / libgphoto2
installed). It answers the three things we need to know about the camera:
  1. is it detected at all (PTP class 07)?
  2. which capture modes does it offer (sample = fast preview JPEG)?
  3. what size/format does a grabbed frame have, and how long does one
     frame take over the slow USB 1.1 pipe?
"""
import shutil
import subprocess
import time


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
        print("  gphoto2 not installed -> sudo apt install gphoto2")
        return
    code, out, err = res
    print("  exit:", code)
    print("  stderr:", err.strip()[:600])
    if out[:2] == b"\xff\xd8":
        try:
            from PIL import Image
            import io
            im = Image.open(io.BytesIO(out))
            print("  JPEG frame:", im.size, len(out), "bytes")
        except Exception as e:
            print("  JPEG decode failed:", e)
    elif out:
        print("  stdout:", out.decode(errors="ignore")[:600])


if __name__ == "__main__":
    show("camera detection", run(["gphoto2", "--list-cameras"], timeout=15))
    show("camera found? (--auto-detect)", run(["gphoto2", "--auto-detect"],
                                              timeout=15))
    show("abilities (sample capture support?)",
         run(["gphoto2", "--abilities"], timeout=15))
    for name, args in (("samplecapture", ["gphoto2", "--samplecapture",
                                          "1", "--filename", "-"]),
                       ("capture-sample", ["gphoto2", "--capture-sample",
                                          "--filename", "-"])):
        t0 = time.time()
        res = run(args, timeout=60)
        dt = time.time() - t0
        show(f"{name} -> time {dt:.1f}s", res)
        if res and res[1][:2] == b"\xff\xd8":
            break
    print("\nIf sample capture works, use it: one JPEG per ~1-3 s over USB "
          "1.1 is plenty for 128 color windows.")
