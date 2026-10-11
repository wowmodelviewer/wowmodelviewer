"""Make the installer's wizard images from the viewer's own splash logo and icon.

wizard-<w>x<h>.png  the Welcome and Finished pages' side panel (aspect 164:314), one per DPI size Setup
                    picks from: a midnight backdrop, the portal's blue as a glow behind the logo
small-<n>.png       the corner image on the other pages: the viewer's icon, transparent

Usage: python make_art.py [<repo root> <output folder>]   (default: this repository, this folder)
Needs Pillow. The images are committed; run this only to change them.
"""
import math
import os
import random
import sys

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO, OUT = (sys.argv[1], sys.argv[2]) if len(sys.argv) > 2 else (os.path.dirname(os.path.dirname(HERE)), HERE)
SPLASH = REPO + r"\bin_support\Splash\Splash-006-Midnight.png"
ICON = REPO + r"\bin_support\icons\WMW-Midnight-26-256x256.png"

# Colours measured from the splash logo's portal (mean of its opaque, non-white pixels by radius):
# centre (4,5,29), inner ring (20,48,143), outer ring (57,40,125).
TOP = (5, 5, 22)
BOTTOM = (13, 10, 40)
GLOW = (20, 48, 143)
RIM = (57, 40, 125)

# Inno Setup's side-panel sizes at 100/125/150/175/200/225/250% DPI (WizardImageFile help topic).
WIZARD_SIZES = [(202, 386), (269, 515), (336, 643), (403, 772), (430, 824), (498, 953), (534, 1022)]
# The corner image's sizes at the same DPI steps (WizardSmallImageFile help topic).
SMALL_SIZES = [58, 77, 97, 116, 124, 143, 159]


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def backdrop(w, h, cx, cy):
    """Vertical midnight gradient with a soft portal-blue glow centred on the logo and a purple rim below it."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    glow_r = w * 1.1
    for y in range(h):
        base = lerp(TOP, BOTTOM, y / (h - 1))
        for x in range(w):
            d = math.hypot((x - cx) / glow_r, (y - cy) / (glow_r * 1.15))
            g = max(0.0, 1.0 - d) ** 2.0 * 0.8
            r = max(0.0, 1.0 - math.hypot((x - cx) / (w * 0.9), (y - h * 0.98) / (h * 0.30))) ** 2 * 0.5
            c = lerp(base, GLOW, g)
            px[x, y] = lerp(c, RIM, r)
    return img


def stars(img, w, h, logo_box):
    """Faint fixed stars (the same on every size), kept off the logo."""
    rnd = random.Random(7)
    px = img.load()
    size = max(1, round(w / 202))
    for _ in range(90):
        fx, fy, a = rnd.random(), rnd.random(), rnd.uniform(0.15, 0.55)
        x, y = int(fx * (w - size)), int(fy * (h - size))
        l, t, r, b = logo_box
        if l - 8 <= x <= r + 8 and t - 8 <= y <= b + 8:
            continue
        for dy in range(size):
            for dx in range(size):
                c = px[x + dx, y + dy]
                px[x + dx, y + dy] = tuple(round(c[i] + (235 - c[i]) * a) for i in range(3)) + (255,)


def main():
    logo = Image.open(SPLASH).convert("RGBA")
    logo = logo.crop(logo.split()[3].getbbox())
    for w, h in WIZARD_SIZES:
        lw = round(w * 0.92)
        lh = round(logo.height * lw / logo.width)
        scaled = logo.resize((lw, lh), Image.LANCZOS)
        cx, cy = w / 2, h * 0.38
        img = backdrop(w, h, cx, cy).convert("RGBA")
        # A faint blurred copy of the logo under it lifts the emblem off the backdrop.
        halo = scaled.filter(ImageFilter.GaussianBlur(max(2, w // 40)))
        halo.putalpha(halo.split()[3].point(lambda a: a * 0.45))
        pos = (round(cx - lw / 2), round(cy - lh / 2))
        stars(img, w, h, (pos[0], pos[1], pos[0] + lw, pos[1] + lh))
        img.alpha_composite(halo, pos)
        img.alpha_composite(scaled, pos)
        img.convert("RGB").save(f"{OUT}\\wizard-{w}x{h}.png", optimize=True)
    icon = Image.open(ICON).convert("RGBA")
    for n in SMALL_SIZES:
        icon.resize((n, n), Image.LANCZOS).save(f"{OUT}\\small-{n}.png", optimize=True)


main()
