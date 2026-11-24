"""sucher3 - Fenster-Rect + Color-Sampling
Python-Port des sucher2 sketch (sucher2/sucher/src/windows.cpp):
  - calculateAmbilightWindows (line 180 ff.)
  - calculateMeanRGB (line 44 ff., RMS / quadraticscher Mittelwert, step=2)

Differences to the C++ sketch:
  - there is NO 2x-coordinate-halving: the Pi has enough RAM to work in the
    frame's native pixel space, so config points, rects and sampling all
    live in the SAME coordinate space (the frame size reported in
    /api/ambilight "frameSize").
  - pixels come from the grabbers as RGB numpy arrays, not RGB565.
"""
import math
import numpy as np


def constrain(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def calculate_ambilight_windows(top_left, top_right, bot_left, bot_right,
                                xwindows, ywindows, depth=2):
    """Returns (topRects, bottomRects, leftRects, rightRects).

    Each rect is a dict {"x1","y1","x2","y2"}. Base arithmetic is the C++
    version (round() quirks kept, corner-skip on the vertical sides: i
    runs from 1 .. ywindows-2), with one deliberate change:

    `depth` = how far the sampling boxes reach INTO the screen, in units
    of one window segment. depth=1 was the sucher2 geometry; the real
    halo color of an edge lives deeper than one segment, so we default to
    2 (boxes reach twice as far in). The boxes overlap more at the four
    corners then - handled here: the side rects adjacent to the top/bottom
    corner bands are one segment SHALLOWER, so the deep top/bottom bands
    own the corner area and no window's sample region is contained in its
    neighbour's.
    """
    topRects, bottomRects, leftRects, rightRects = [], [], [], []

    xtopwinwidth = (top_right[0] - top_left[0]) / xwindows
    xbotwinwidth = (bot_right[0] - bot_left[0]) / xwindows
    yleftwinheight = (bot_left[1] - top_left[1]) / ywindows
    yrightwinheight = (bot_right[1] - top_right[1]) / ywindows

    topslope = (top_right[1] - top_left[1]) / xwindows
    bottomslope = (bot_right[1] - bot_left[1]) / xwindows
    leftslope = (bot_left[0] - top_left[0]) / ywindows
    rightslope = (bot_right[0] - top_right[0]) / ywindows

    # C++ round() = round-half-away-from-zero; Python round() is
    # half-to-even, so use this helper for all int conversions:
    rnd = lambda v: math.floor(v + 0.5) if v >= 0 else -math.floor(-v + 0.5)

    def side_depth(i):
        if i < depth or i > ywindows - 1 - depth:
            return max(1, depth - 1)
        return depth

    for i in range(xwindows):
        tx1 = top_left[0] + rnd(i * xtopwinwidth)
        ty1 = top_left[1] + rnd(i * topslope)
        tx2 = tx1 + rnd(xtopwinwidth)
        ty2 = ty1 + depth * ((1.0 - (i / (xwindows - 1))) * yleftwinheight +
              (i / (xwindows - 1)) * yrightwinheight)
        topRects.append({"x1": tx1, "y1": ty1, "x2": tx2, "y2": int(ty2)})

        bx1 = bot_left[0] + rnd(i * xbotwinwidth)
        by1 = bot_left[1] + rnd(i * bottomslope) - depth * ((1.0 - (i / (xwindows - 1))) * yleftwinheight +
              (i / (xwindows - 1)) * yrightwinheight)
        bx2 = bx1 + rnd(xbotwinwidth)
        by2 = bot_left[1] + rnd(i * bottomslope)
        bottomRects.append({"x1": bx1, "y1": int(by1), "x2": bx2, "y2": by2})

    for i in range(1, ywindows - 1):
        sd = side_depth(i)
        lx1 = top_left[0] + rnd(i * leftslope)
        ly1 = top_left[1] + rnd(i * yleftwinheight)
        lx2 = lx1 + sd * ((1.0 - (i / (ywindows - 1))) * xtopwinwidth +
              (i / (ywindows - 1)) * xbotwinwidth)
        ly2 = ly1 + rnd(yleftwinheight)
        leftRects.append({"x1": lx1, "y1": ly1, "x2": int(lx2), "y2": ly2})

        rx1 = top_right[0] + rnd(i * rightslope) - sd * ((1.0 - (i / (ywindows - 1))) * xtopwinwidth +
                  (i / (ywindows - 1)) * xbotwinwidth)
        ry1 = top_right[1] + rnd(i * yrightwinheight)
        rx2 = top_right[0] + rnd(i * rightslope)
        ry2 = ry1 + rnd(yrightwinheight)
        rightRects.append({"x1": int(rx1), "y1": ry1, "x2": rx2, "y2": ry2})

    return topRects, bottomRects, leftRects, rightRects


def calculate_mean_rgb(px, width, height, x1, y1, x2, y2, step=2):
    """RMS-mean over the rect, sampling every `step`-th pixel - identical
    math to calculateMeanRGB in windows.cpp, vectorized with numpy.
    `px` is an RGB ndarray (H,W,3)."""
    x1 = constrain(int(x1), 0, width - 1)
    x2 = constrain(int(x2), 0, width - 1)
    y1 = constrain(int(y1), 0, height - 1)
    y2 = constrain(int(y2), 0, height - 1)
    if x1 >= x2 or y1 >= y2:
        return (0, 0, 0)

    sub = px[y1:y2:step, x1:x2:step]
    n = sub.shape[0] * sub.shape[1]
    if n == 0:
        return (128, 128, 128)
    ssq = (sub.astype(np.int64) ** 2).sum(axis=(0, 1))
    # "step-Korrektur" exactly like the sketch: sqrt(step^2 * sum / n)
    k = step * step
    return tuple(min(255, int(math.sqrt(k * int(s) / n) + 0.5))
                 for s in ssq)


def sample_colors(px, width, height, rects):
    return [calculate_mean_rgb(px, width, height, r["x1"], r["y1"],
                               r["x2"], r["y2"]) for r in rects]


if __name__ == "__main__":
    # selftest: hSeg=40, vSeg=26 -> 40+40 top/bottom, 24+24 sides = 128
    t, b, l, r = calculate_ambilight_windows(
        (25, 25), (295, 25), (25, 215), (295, 215), 40, 26)
    assert (len(t), len(b), len(l), len(r)) == (40, 40, 24, 24)
    # default depth=2: middle top/bottom boxes reach 2 window rows into
    # the screen (TV is 190 px tall / 26 rows = 7.3 px per row)...
    assert t[20]["y2"] - t[20]["y1"] > 12, "top boxes too shallow"
    # ...while the side boxes ADJACENT to the corners stay 1 segment deep
    # so they don't reach into the deep corner bands (overlap handling):
    assert l[0]["x2"] - l[0]["x1"] < 10, "corner side box reaches too deep"
    assert l[12]["x2"] - l[12]["x1"] > 12, "middle side box too shallow"
    print("windows selftest OK:", len(t) + len(b) + len(l) + len(r), "rects")
