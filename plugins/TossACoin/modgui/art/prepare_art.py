#!/usr/bin/env python3
"""
Rebuild the pedal artwork in ../img/ and the knob film strip ../knobs/coin.png
from their public-domain / CC0 sources (see ../img/CREDITS.md).

    python3 prepare_art.py          # needs network, numpy, scipy, Pillow

Steps: download the sources, cut the coins out of their photo backgrounds
(gold by colour saturation, silver by surface texture — shadows and studio
backdrops are smooth and unsaturated), recolour two 17th-century silk damasks
into the pedal's navy-teal and magenta, key the quill off its white backdrop,
and rotate the notched gold coin into MOD's 65-frame knob strip.
"""
import io
import json
import os
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps
from scipy import ndimage

HERE  = os.path.dirname(os.path.abspath(__file__))
IMG   = os.path.join(HERE, "..", "img")
KNOBS = os.path.join(HERE, "..", "knobs")
UA    = {"User-Agent": "TossACoin-art/1.0 (github.com/stefdoerr/tossacoin)"}

MET = "https://collectionapi.metmuseum.org/public/collection/v1/objects/{}"
QUILL = "https://upload.wikimedia.org/wikipedia/commons/8/87/Quill_pen.PNG"


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return r.read()


def met_image(object_id, field="primaryImageSmall"):
    meta = json.loads(fetch(MET.format(object_id)))
    assert meta["isPublicDomain"], object_id
    return Image.open(io.BytesIO(fetch(meta[field]))).convert("RGB")


def circle_cutout(im, mask, size, shrink):
    """Fit a circle to the mask's edge (least squares), crop it, alpha-mask it."""
    m = ndimage.binary_closing(mask, iterations=5)
    m = ndimage.binary_opening(ndimage.binary_fill_holes(m), iterations=3)
    lab, n = ndimage.label(m)
    big = lab == (np.argmax(ndimage.sum(m, lab, range(1, n + 1))) + 1)
    ys, xs = np.nonzero(big & ~ndimage.binary_erosion(big))
    x, y = xs.astype(float), ys.astype(float)
    a, b, c = np.linalg.lstsq(np.c_[2 * x, 2 * y, np.ones_like(x)], x * x + y * y, rcond=None)[0]
    r = np.sqrt(c + a * a + b * b) * shrink
    big4 = size * 4
    out = im.crop((round(a - r), round(b - r), round(a + r), round(b + r))).resize((big4, big4), Image.LANCZOS)
    alpha = Image.new("L", (big4, big4), 0)
    ImageDraw.Draw(alpha).ellipse((2, 2, big4 - 3, big4 - 3), fill=255)
    out.putalpha(alpha)
    return out.resize((size, size), Image.LANCZOS)


def gold_coin(im, size):
    a = np.asarray(im).astype(float)
    sat = (a.max(axis=2) - a.min(axis=2)) / np.maximum(a.max(axis=2), 1)
    return circle_cutout(im, sat > 0.22, size, 0.975)


def silver_coin(im, size):
    L = np.asarray(im).astype(float).mean(axis=2)
    mu = ndimage.uniform_filter(L, 9)
    sd = np.sqrt(np.maximum(ndimage.uniform_filter(L * L, 9) - mu * mu, 0))
    return circle_cutout(im, sd > 4.0, size, 0.97)


def add_notch(coin):
    """Engraved pointer groove at 12 o'clock, so the knob shows where it points."""
    d = ImageDraw.Draw(coin)
    s = coin.size[0]
    cx, w, top, bot = s / 2, s * 0.035, s * 0.035, s * 0.20
    d.polygon([(cx - w, top), (cx + w, top), (cx + w * 0.45, bot), (cx - w * 0.45, bot)], fill=(70, 40, 8, 235))
    d.line([(cx + w * 0.9, top + 2), (cx + w * 0.35, bot - 1)], fill=(255, 236, 160, 200), width=max(1, int(s * 0.008)))
    return coin


def recolour(im, crop, stops, size):
    g = im.convert("L")
    w, h = g.size
    g = g.crop((int(crop[0] * w), int(crop[1] * h), int(crop[2] * w), int(crop[3] * h)))
    L = np.asarray(ImageOps.autocontrast(g, cutoff=2).resize(size, Image.LANCZOS)).astype(float) / 255
    xs = np.array([s[0] for s in stops])
    cols = np.array([s[1] for s in stops], float)
    rgb = np.stack([np.interp(L, xs, cols[:, i]) for i in range(3)], axis=2)
    return Image.fromarray(rgb.astype(np.uint8))


def quill():
    q = Image.open(io.BytesIO(fetch(QUILL))).convert("RGB")
    w, h = q.size
    mark = q.copy()
    for p in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        ImageDraw.floodfill(mark, p, (255, 0, 255), thresh=6)
    bg = np.all(np.asarray(mark) == [255, 0, 255], axis=2)
    q.putalpha(Image.fromarray(((~bg) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)))
    return q.crop(q.getbbox())


def knob_strip(coin, frames=65, px=128):
    """MOD film strip: frame 0 = minimum (-135 deg) ... last = maximum (+135 deg)."""
    coin = coin.resize((px, px), Image.LANCZOS)
    strip = Image.new("RGBA", (frames * px, px), (0, 0, 0, 0))
    for i in range(frames):
        angle = -135.0 + 270.0 * i / (frames - 1)
        strip.alpha_composite(coin.rotate(-angle, resample=Image.BICUBIC), (i * px, 0))
    return strip.quantize(colors=256, method=Image.FASTOCTREE)   # ~4x smaller, gold survives fine


def main():
    os.makedirs(IMG, exist_ok=True)
    os.makedirs(KNOBS, exist_ok=True)

    gold = add_notch(gold_coin(met_image(247144), 256))          # gold stater, Philip II
    gold.save(os.path.join(IMG, "coin-gold.png"))
    knob_strip(gold).save(os.path.join(KNOBS, "coin.png"), optimize=True)

    silver_coin(met_image(326156), 144).save(os.path.join(IMG, "coin-silver.png"))   # Seleucus I

    navy = [(0, (10, 18, 40)), (0.4, (24, 44, 86)), (0.7, (36, 92, 118)), (1, (92, 170, 176))]
    magenta = [(0, (58, 6, 40)), (0.35, (118, 22, 82)), (0.65, (166, 52, 128)), (1, (222, 160, 214))]
    recolour(met_image(216471, "primaryImage"), (0.08, 0.12, 0.92, 0.52), navy, (1280, 600)) \
        .save(os.path.join(IMG, "damask-navy.jpg"), quality=82)
    recolour(met_image(227205, "primaryImage"), (0.08, 0.10, 0.92, 0.48), magenta, (1280, 600)) \
        .save(os.path.join(IMG, "damask-magenta.jpg"), quality=82)

    quill().save(os.path.join(IMG, "quill.png"), optimize=True)
    print("art written to", os.path.normpath(IMG), "and", os.path.normpath(KNOBS))


if __name__ == "__main__":
    main()
