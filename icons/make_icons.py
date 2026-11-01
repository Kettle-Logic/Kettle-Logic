#!/usr/bin/env python3
"""Kettle Logic app icon generator.

Draws the mug mark (amber body, dark lid) on a light rounded plate and writes:
  macOS/AppIcon.appiconset   drop into Xcode's Assets.xcassets
  Windows/Assets             WinUI 3 packaged-app logo assets (PNG)
  Windows/KettleLogic.ico    multi-size icon for window / unpackaged use

Usage: python3 make_icons.py OUTPUT_DIR
"""
import json
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

STOUT = (27, 20, 16)
AMBER_TOP = (214, 169, 77)
AMBER_BOT = (180, 133, 44)
PLATE_TOP = (248, 250, 250)
PLATE_BOT = (221, 229, 229)
STEEL = (185, 196, 196)


def superellipse(cx, cy, half, n=5.0, steps=720):
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        pts.append((cx + half * math.copysign(abs(c) ** (2 / n), c),
                    cy + half * math.copysign(abs(s) ** (2 / n), s)))
    return pts


def vgrad(size, top, bot):
    w, h = size
    g = Image.linear_gradient("L").resize((w, h))
    return Image.composite(Image.new("RGB", (w, h), bot), Image.new("RGB", (w, h), top), g)


def solid(color, mask):
    im = Image.new("RGBA", mask.size, color + (255,))
    im.putalpha(mask)
    return im


def render(S, mode):
    """mode 'mac': inset plate with soft shadow (Apple's 824/1024 grid).
       mode 'win': plate fills the canvas."""
    ss = max(2, min(8, 2048 // S))
    W = S * ss
    cx = cy = W / 2
    canvas = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    if mode == "mac":
        P = round(W * 824 / 1024)
    else:
        inset = round(W * 0.04) if S >= 48 else 0
        P = W - 2 * inset
    off = (W - P) // 2
    half = P / 2

    if mode == "mac":
        sm = Image.new("L", (W, W), 0)
        ImageDraw.Draw(sm).polygon(superellipse(cx, cy + W * 10 / 1024, half), fill=255)
        sm = sm.filter(ImageFilter.GaussianBlur(W * 14 / 1024)).point(lambda v: int(v * 0.30))
        canvas = Image.alpha_composite(canvas, solid((0, 0, 0), sm))

    # plate: thin steel edge, light vertical gradient inside
    outer = Image.new("L", (W, W), 0)
    ImageDraw.Draw(outer).polygon(superellipse(cx, cy, half), fill=255)
    bw = max(ss, P * 0.012)
    inner = Image.new("L", (W, W), 0)
    ImageDraw.Draw(inner).polygon(superellipse(cx, cy, half - bw), fill=255)
    canvas = Image.alpha_composite(canvas, solid(STEEL, outer))
    fill = Image.new("RGB", (W, W), PLATE_TOP)
    fill.paste(vgrad((P, P), PLATE_TOP, PLATE_BOT), (off, off))
    fill = fill.convert("RGBA")
    fill.putalpha(inner)
    canvas = Image.alpha_composite(canvas, fill)

    # mug (coordinates are the 64x64 favicon geometry)
    k = 0.60 if S > 32 else 0.68
    u = P * k / 51.0

    def T(x, y):
        return (cx + (x - 32) * u, cy + (y - 32.5) * u)

    body = [(16, 14), (48, 14), (44, 54)]
    for i in range(0, 19):
        t = math.radians(i * 5)
        body.append((40 + 4 * math.cos(t), 54 + 4 * math.sin(t)))
    for i in range(18, 37):
        t = math.radians(i * 5)
        body.append((24 + 4 * math.cos(t), 54 + 4 * math.sin(t)))
    body_px = [T(x, y) for x, y in body]

    bmask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(bmask).polygon(body_px, fill=255)
    gy0, gy1 = int(T(0, 14)[1]), int(T(0, 58)[1]) + 1
    bfill = Image.new("RGB", (W, W), AMBER_TOP)
    bfill.paste(vgrad((W, gy1 - gy0), AMBER_TOP, AMBER_BOT), (0, gy0))
    bfill = bfill.convert("RGBA")
    bfill.putalpha(bmask)
    canvas = Image.alpha_composite(canvas, bfill)

    if S >= 48:
        hl = Image.new("RGBA", (W, W), (0, 0, 0, 0))
        ImageDraw.Draw(hl).polygon(
            [T(21, 22), T(24.5, 22), T(27, 48), T(23.4, 48)], fill=(255, 255, 255, 72))
        canvas = Image.alpha_composite(canvas, hl)

    ImageDraw.Draw(canvas).rounded_rectangle(
        [T(12, 7), T(52, 18)], radius=5.5 * u, fill=STOUT + (255,))

    return canvas.resize((S, S), Image.LANCZOS)


def main(out):
    cache = {}

    def get(px, mode):
        if (px, mode) not in cache:
            cache[(px, mode)] = render(px, mode)
        return cache[(px, mode)]

    # ---------- macOS ----------
    mac_dir = os.path.join(out, "macOS", "AppIcon.appiconset")
    os.makedirs(mac_dir, exist_ok=True)
    images = []
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
            get(size * scale, "mac").save(os.path.join(mac_dir, name))
            images.append({"filename": name, "idiom": "mac",
                           "scale": f"{scale}x", "size": f"{size}x{size}"})
    with open(os.path.join(mac_dir, "Contents.json"), "w") as f:
        json.dump({"images": images, "info": {"author": "xcode", "version": 1}}, f, indent=2)
        f.write("\n")
    get(1024, "mac").save(os.path.join(out, "macOS", "KettleLogic-1024.png"))

    # ---------- Windows ----------
    assets = os.path.join(out, "Windows", "Assets")
    os.makedirs(assets, exist_ok=True)
    for sc, px in {100: 44, 125: 55, 150: 66, 200: 88, 400: 176}.items():
        get(px, "win").save(os.path.join(assets, f"Square44x44Logo.scale-{sc}.png"))
    for t in (16, 24, 32, 48, 256):
        get(t, "win").save(os.path.join(assets, f"Square44x44Logo.targetsize-{t}.png"))
        get(t, "win").save(os.path.join(assets, f"Square44x44Logo.targetsize-{t}_altform-unplated.png"))
    for sc, px in {100: 150, 125: 188, 150: 225, 200: 300, 400: 600}.items():
        get(px, "win").save(os.path.join(assets, f"Square150x150Logo.scale-{sc}.png"))
    get(50, "win").save(os.path.join(assets, "StoreLogo.png"))

    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for s in (16, 24, 32, 48, 64, 128, 256):
            p = os.path.join(tmp, f"{s}.png")
            get(s, "win").save(p)
            files.append(p)
        subprocess.run(["convert", *files, os.path.join(out, "Windows", "KettleLogic.ico")], check=True)

    print("done:", out)


if __name__ == "__main__":
    main(sys.argv[1])
