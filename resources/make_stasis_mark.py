#!/usr/bin/env python3
"""Build the Stasis brand mark used by the AI sidebar.

The source art (E:/Downloads/stasis_logo.png) is a white glyph sitting on a
large black field. Drawn as-is it would paint a black square onto whatever is
behind it, so this strips the field and keeps only the glyph:

  * alpha comes from luminance, so the black background becomes fully
    transparent while the white strokes stay opaque and their antialiased
    edges keep their softness (a hard threshold would leave jagged edges);
  * colour is forced to pure white, letting the widget tint it by drawing
    with a composition mode or an opacity, the same trick BrandBar uses;
  * the result is cropped to the glyph's bounding box, so layout code can
    size it by its real extents instead of guessing at the padding.

Regenerate with:  python resources/make_stasis_mark.py
"""

import os
import sys

from PIL import Image

SRC = r"E:\Downloads\stasis_logo.png"
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# Anything at or below this luminance is treated as background. The art is a
# near-pure black field, so this only has to clear compression noise.
FLOOR = 18


def build():
    if not os.path.exists(SRC):
        sys.exit("source art not found: " + SRC)

    im = Image.open(SRC).convert("RGB")
    lum = im.convert("L")

    # Rescale luminance so FLOOR maps to 0 and 255 stays 255. Without this the
    # background keeps a faint alpha and shows as a grey box on light chrome.
    alpha = lum.point(lambda v: 0 if v <= FLOOR else int((v - FLOOR) * 255 / (255 - FLOOR)))

    white = Image.new("RGB", im.size, (255, 255, 255))
    out = white.convert("RGBA")
    out.putalpha(alpha)

    box = alpha.getbbox()
    if box is None:
        sys.exit("source art is entirely background")
    out = out.crop(box)

    # A little breathing room so round caps are not clipped when scaled down.
    pad = max(2, int(0.02 * max(out.size)))
    padded = Image.new("RGBA", (out.width + pad * 2, out.height + pad * 2), (255, 255, 255, 0))
    padded.paste(out, (pad, pad))

    full = os.path.join(OUT_DIR, "stasis-mark.png")
    padded.save(full)
    print("wrote", full, padded.size)

    # Pre-scaled copies for the button and the sidebar hero. Qt scales these
    # smoothly, but a source this large costs memory and softens small sizes.
    for name, height in (("stasis-mark-32.png", 32), ("stasis-mark-64.png", 64),
                         ("stasis-mark-256.png", 256)):
        w = max(1, round(padded.width * height / padded.height))
        small = padded.resize((w, height), Image.LANCZOS)
        p = os.path.join(OUT_DIR, name)
        small.save(p)
        print("wrote", p, small.size)


if __name__ == "__main__":
    build()
