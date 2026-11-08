#!/usr/bin/env python3
"""Generates the sample cursor theme data/cursors/LuminaOutline.

An original, deliberately simple outline style (white fill, dark outline,
soft shadow) used to exercise the cursor-theme pipeline end to end:
theme.json → `lumina cursors check` → CursorSet file → Window Engine →
CursorManager::SetCursorSet().

Usage: tools/make-sample-cursor-theme.py [output-dir]
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

SS = 8  # supersampling factor
BASE = 22  # nominal size at scale 1 (CursorManager uses 22 px)
SIZES = [22, 33, 44]

FILL = (250, 250, 250, 255)
LINE = (32, 34, 38, 255)
ACCENT = (52, 120, 220, 255)


def canvas(size):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def scale_points(points, size):
    f = size * SS / BASE
    return [(x * f, y * f) for x, y in points]


def finish(img, size):
    """Adds a soft shadow and downsamples."""
    alpha = img.split()[3]
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.point(lambda a: int(a * 0.35)))
    offset = int(size * SS / BASE * 1.0)
    shadow = shadow.transform(img.size, Image.AFFINE,
                              (1, 0, -offset, 0, 1, -offset))
    shadow = shadow.filter(ImageFilter.GaussianBlur(size * SS / BASE * 0.9))
    composed = Image.alpha_composite(shadow, img)
    return composed.resize((size, size), Image.LANCZOS)


def polygon(points, size, fill=FILL, outline=LINE, width=1.4):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    pts = scale_points(points, size)
    w = max(1, int(width * size * SS / BASE))
    d.polygon(pts, fill=outline)
    # inset fill: draw the polygon again shrunk toward its centroid
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    inner = []
    for x, y in pts:
        dx, dy = x - cx, y - cy
        length = math.hypot(dx, dy) or 1
        inner.append((x - dx / length * w, y - dy / length * w))
    d.polygon(inner, fill=fill)
    return img


def arrow():
    return [(2, 1), (2, 17), (6, 13.5), (9, 20), (12, 18.6), (9.2, 12.4),
            (14.5, 12.4)]


def double_arrow(angle):
    # Horizontal double arrow, rotated by angle (degrees) about the centre.
    pts = [(2, 11), (7, 6), (7, 9), (15, 9), (15, 6), (20, 11), (15, 16),
           (15, 13), (7, 13), (7, 16)]
    a = math.radians(angle)
    c, s = math.cos(a), math.sin(a)
    return [((x - 11) * c - (y - 11) * s + 11, (x - 11) * s + (y - 11) * c + 11)
            for x, y in pts]


def draw_arrow(size):
    return finish(polygon(arrow(), size), size)


def draw_double(size, angle):
    return finish(polygon(double_arrow(angle), size), size)


def draw_ibeam(size):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    f = size * SS / BASE
    for color, grow in ((LINE, 1.2), (FILL, 0)):
        g = grow * f
        d.rectangle([(10 * f - g, 3 * f - g), (12 * f + g, 19 * f + g)],
                    fill=color)
        d.rectangle([(7 * f - g, 3 * f - g), (15 * f + g, 4.6 * f + g)],
                    fill=color)
        d.rectangle([(7 * f - g, 17.4 * f - g), (15 * f + g, 19 * f + g)],
                    fill=color)
    return finish(img, size)


def draw_crosshair(size):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    f = size * SS / BASE
    for color, grow in ((LINE, 1.2), (FILL, 0)):
        g = grow * f
        d.rectangle([(10.2 * f - g, 2 * f - g), (11.8 * f + g, 20 * f + g)],
                    fill=color)
        d.rectangle([(2 * f - g, 10.2 * f - g), (20 * f + g, 11.8 * f + g)],
                    fill=color)
    return finish(img, size)


def draw_move(size):
    # Four-way arrow drawn as outline + thinner fill of the same shape.
    def shape(arm, head, tip):
        c = 11
        return [(c, c - tip), (c + head, c - tip + head), (c + arm, c - tip + head),
                (c + arm, c - arm), (c + tip - head, c - arm),
                (c + tip - head, c - head), (c + tip, c),
                (c + tip - head, c + head), (c + tip - head, c + arm),
                (c + arm, c + arm), (c + arm, c + tip - head),
                (c + head, c + tip - head), (c, c + tip),
                (c - head, c + tip - head), (c - arm, c + tip - head),
                (c - arm, c + arm), (c - tip + head, c + arm),
                (c - tip + head, c + head), (c - tip, c),
                (c - tip + head, c - head), (c - tip + head, c - arm),
                (c - arm, c - arm), (c - arm, c - tip + head),
                (c - head, c - tip + head)]
    img = canvas(size)
    d = ImageDraw.Draw(img)
    d.polygon(scale_points(shape(2.4, 4.6, 10), size), fill=LINE)
    d.polygon(scale_points(shape(1.1, 3.0, 8.2), size), fill=FILL)
    return finish(img, size)


def draw_not_allowed(size):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    f = size * SS / BASE
    d.ellipse([(3 * f, 3 * f), (19 * f, 19 * f)], fill=LINE)
    d.ellipse([(4.4 * f, 4.4 * f), (17.6 * f, 17.6 * f)], fill=FILL)
    d.ellipse([(6.2 * f, 6.2 * f), (15.8 * f, 15.8 * f)], fill=(0, 0, 0, 0))
    d.line([(6 * f, 16 * f), (16 * f, 6 * f)], fill=(200, 40, 40, 255),
           width=int(2.2 * f))
    return finish(img, size)


def draw_progress(size):
    img = polygon(arrow(), size)
    d = ImageDraw.Draw(img)
    f = size * SS / BASE
    box = [(12 * f, 12 * f), (21 * f, 21 * f)]
    d.ellipse(box, fill=LINE)
    d.pieslice(box, -90, 120, fill=ACCENT)
    d.ellipse([(14.2 * f, 14.2 * f), (18.8 * f, 18.8 * f)], fill=FILL)
    return finish(img, size)


CURSORS = {
    "default": (draw_arrow, [2, 1]),
    "progress": (draw_progress, [2, 1]),
    "text": (draw_ibeam, [11, 11]),
    "crosshair": (draw_crosshair, [11, 11]),
    "move": (draw_move, [11, 11]),
    "not-allowed": (draw_not_allowed, [11, 11]),
    "resize-ew": (lambda s: draw_double(s, 0), [11, 11]),
    "resize-ns": (lambda s: draw_double(s, 90), [11, 11]),
    "resize-nwse": (lambda s: draw_double(s, 45), [11, 11]),
    "resize-nesw": (lambda s: draw_double(s, -45), [11, 11]),
    "resize-e": (lambda s: draw_double(s, 0), [11, 11]),
    "resize-w": (lambda s: draw_double(s, 0), [11, 11]),
    "resize-n": (lambda s: draw_double(s, 90), [11, 11]),
    "resize-s": (lambda s: draw_double(s, 90), [11, 11]),
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "..", "data", "cursors", "LuminaOutline")
    manifest = {"name": "Lumina Outline", "sizes": SIZES, "cursors": {}}
    for size in SIZES:
        os.makedirs(os.path.join(out, str(size)), exist_ok=True)
        for role, (draw, hotspot) in CURSORS.items():
            draw(size).save(os.path.join(out, str(size), role + ".png"))
    for role, (_, hotspot) in CURSORS.items():
        manifest["cursors"][role] = {"file": role + ".png", "hotspot": hotspot}
    with open(os.path.join(out, "theme.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("wrote", os.path.abspath(out))


if __name__ == "__main__":
    main()
