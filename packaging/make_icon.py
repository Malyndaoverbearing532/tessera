#!/usr/bin/env python3
"""Generates the macOS application icon.

Kept as a script rather than a committed image so the icon is reproducible and
reviewable. Run it after changing the design; the resulting .icns is what the
build actually consumes, so numpy is not a build-time dependency.

    python3 packaging/make_icon.py

Requires numpy, plus iconutil (ships with the Command Line Tools).
"""

import os
import shutil
import struct
import subprocess
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

# Palette: the viewer's accent orange on its dark UI background.
BACKGROUND_TOP = np.array([0.17, 0.18, 0.22])
BACKGROUND_BOTTOM = np.array([0.07, 0.08, 0.10])
FACE_TOP = np.array([1.00, 0.70, 0.35])
FACE_LEFT = np.array([0.78, 0.43, 0.16])
FACE_RIGHT = np.array([0.55, 0.29, 0.11])
OUTLINE = np.array([0.05, 0.05, 0.07])


def convex_polygon_coverage(x, y, points, softness):
    """Anti-aliased fill mask for a convex polygon, in either winding order.

    Coverage is the minimum of the half-plane coverages of each edge, which is
    exact for convex shapes and needs no supersampling.
    """
    count = len(points)

    # Twice the signed area tells us the winding, so the caller does not have to
    # care which direction the points run in.
    area2 = sum(points[i][0] * points[(i + 1) % count][1] -
                points[(i + 1) % count][0] * points[i][1]
                for i in range(count))
    orientation = 1.0 if area2 >= 0.0 else -1.0

    coverage = np.ones_like(x)
    for i in range(count):
        ax, ay = points[i]
        bx, by = points[(i + 1) % count]
        edge_x, edge_y = bx - ax, by - ay
        length = np.hypot(edge_x, edge_y)
        if length < 1e-12:
            continue
        # Signed distance to the edge line, made positive on the inside.
        distance = orientation * (edge_x * (y - ay) - edge_y * (x - ax)) / length
        coverage = np.minimum(coverage, np.clip(distance / softness + 0.5, 0.0, 1.0))
    return coverage


def rounded_rect_coverage(x, y, half, radius, softness):
    """Anti-aliased mask for a rounded square centred on the origin."""
    dx = np.abs(x) - (half - radius)
    dy = np.abs(y) - (half - radius)
    outside = np.hypot(np.maximum(dx, 0.0), np.maximum(dy, 0.0))
    inside = np.minimum(np.maximum(dx, dy), 0.0)
    return np.clip((radius - (outside + inside)) / softness + 0.5, 0.0, 1.0)


def composite(base, color, mask):
    """Alpha-composites a flat colour over an RGB image."""
    return base * (1.0 - mask[..., None]) + color * mask[..., None]


def render(size):
    """Draws one icon at `size` x `size`, returning RGBA uint8."""
    # Work in a [-1, 1] square so the geometry is resolution independent.
    axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    x, y = np.meshgrid(axis, axis)
    softness = 2.0 / size  # roughly one pixel

    # macOS icons sit inside a margin rather than filling the tile.
    background = rounded_rect_coverage(x, y, half=0.90, radius=0.22, softness=softness)

    vertical = np.clip((y + 0.9) / 1.8, 0.0, 1.0)[..., None]
    image = BACKGROUND_TOP * (1.0 - vertical) + BACKGROUND_BOTTOM * vertical

    # Isometric cube: a hexagon split into three rhombi.
    r = 0.52
    dx, dy = 0.866 * r, 0.5 * r
    top = (0.0, -r)
    upper_left, upper_right = (-dx, -dy), (dx, -dy)
    lower_left, lower_right = (-dx, dy), (dx, dy)
    bottom = (0.0, r)
    centre = (0.0, 0.0)

    # A slightly larger hexagon behind the faces reads as a crisp outline.
    scale = 1.0 + 3.5 / size
    hexagon = [(px * scale, py * scale)
               for px, py in (top, upper_right, lower_right, bottom, lower_left, upper_left)]
    image = composite(image, OUTLINE, convex_polygon_coverage(x, y, hexagon, softness))

    faces = [
        ([top, upper_right, centre, upper_left], FACE_TOP),
        ([upper_left, centre, bottom, lower_left], FACE_LEFT),
        ([centre, upper_right, lower_right, bottom], FACE_RIGHT),
    ]
    for points, colour in faces:
        image = composite(image, colour, convex_polygon_coverage(x, y, points, softness))

    rgb = np.clip(image, 0.0, 1.0)
    rgba = np.concatenate([rgb, background[..., None]], axis=-1)
    return (rgba * 255.0 + 0.5).astype(np.uint8)


def write_png(path, rgba):
    height, width, _ = rgba.shape
    # Each PNG scanline is prefixed with a filter byte; 0 means "no filter".
    raw = b"".join(b"\x00" + rgba[row].tobytes() for row in range(height))

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        handle.write(chunk(b"IEND", b""))


def main():
    iconset = os.path.join(HERE, "tessera.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)

    # iconutil expects this exact naming.
    variants = [
        (16, "icon_16x16.png"), (32, "icon_16x16@2x.png"),
        (32, "icon_32x32.png"), (64, "icon_32x32@2x.png"),
        (128, "icon_128x128.png"), (256, "icon_128x128@2x.png"),
        (256, "icon_256x256.png"), (512, "icon_256x256@2x.png"),
        (512, "icon_512x512.png"), (1024, "icon_512x512@2x.png"),
    ]

    cache = {}
    for size, name in variants:
        if size not in cache:
            cache[size] = render(size)
        write_png(os.path.join(iconset, name), cache[size])
        print(f"  {name}")

    output = os.path.join(HERE, "tessera.icns")
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", output], check=True)
    shutil.rmtree(iconset, ignore_errors=True)
    print(f"wrote {output} ({os.path.getsize(output)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
