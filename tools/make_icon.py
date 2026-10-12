"""Draws the app icon: a green wireframe pyramid on black, after the original
arcade Battlezone's vector pyramids. Writes app.png (128 px, the window icon)
and app.ico (16-256 px, the .exe icon) to the current directory; icons/app.res
is compiled from app.ico with `llvm-rc -fo app.res app.rc` (app.rc:
`1 ICON "app.ico"`). Usage: cd icons && python3 ../tools/make_icon.py"""
import math
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 1024
GREEN = (80, 255, 90)

def project(p, yaw, pitch, dist, scale, cx, cy):
    x, y, z = p
    cy_, sy_ = math.cos(yaw), math.sin(yaw)
    x, z = x * cy_ - z * sy_, x * sy_ + z * cy_
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    z += dist
    return (cx + scale * x / z, cy - scale * y / z)

def render(size_px):
    # Square base on the ground, apex above; a slight elevation and turn so
    # all four sides and the hidden back edges read as a wireframe.
    base = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
    apex = (0, 1.45, 0)
    yaw, pitch, dist, scale = math.radians(45), math.radians(20), 5.0, 1180
    pts = [project(p, yaw, pitch, dist, scale, S * 0.5, S * 0.61) for p in base]
    top = project(apex, yaw, pitch, dist, scale, S * 0.5, S * 0.61)
    edges = [(pts[i], pts[(i + 1) % 4]) for i in range(4)] + [(p, top) for p in pts]

    lines = Image.new("RGB", (S, S), (0, 0, 0))
    d = ImageDraw.Draw(lines)
    width = int(S * 0.02)
    for a, b in edges:
        d.line([a, b], fill=GREEN, width=width, joint="curve")
    for p in pts + [top]:
        r = width / 2
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=GREEN)

    glow = lines.filter(ImageFilter.GaussianBlur(S * 0.018))
    glow = Image.eval(glow, lambda v: min(255, int(v * 0.9)))
    img = ImageChops.add(glow, lines)

    # Black rounded square.
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.16), fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    return out.resize((size_px, size_px), Image.LANCZOS)

big = render(1024)
big.resize((128, 128), Image.LANCZOS).save("app.png")
big.save("app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
