"""Generate the small glyphs Qt stylesheets cannot draw themselves.

A styled QCheckBox::indicator renders as a bare filled square without a real
tick image, and a styled QSpinBox::up-arrow renders as an empty block: the
CSS "zero-size element with borders" triangle trick does not work in Qt, it
just paints a box. Both need actual images.

    python resources/make_check_icon.py

Writes, at 1x and @2x (Qt picks @2x automatically on high-DPI screens):
    check-white.png   tick for a checked checkbox
    spin-up.png       up chevron for spin boxes
    spin-down.png     down chevron for spin boxes
"""

from PIL import Image, ImageDraw

SS = 8  # supersample factor, for anti-aliased edges


def make(size: int, path: str) -> None:
    big = size * SS
    img = Image.new("RGBA", (big, big), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)

    # Checkmark as two strokes, inset so it never touches the border radius.
    w = max(2, int(big * 0.135))
    pts = [
        (big * 0.22, big * 0.52),
        (big * 0.42, big * 0.72),
        (big * 0.79, big * 0.28),
    ]
    d.line(pts, fill=(255, 255, 255, 255), width=w, joint="curve")
    # Round the stroke ends; PIL's line joint does not cap them.
    r = w / 2.0
    for x, y in (pts[0], pts[2]):
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, 255))

    img.resize((size, size), Image.LANCZOS).save(path)
    print("wrote", path)


def make_chevron(w: int, h: int, path: str, up: bool,
                 color=(74, 80, 96, 255)) -> None:
    """A spin-box chevron, drawn dark so it reads on the light button face."""
    bw, bh = w * SS, h * SS
    img = Image.new("RGBA", (bw, bh), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)

    stroke = max(2, int(bh * 0.30))
    pad = stroke * 0.7
    if up:
        pts = [(pad, bh - pad), (bw / 2.0, pad), (bw - pad, bh - pad)]
    else:
        pts = [(pad, pad), (bw / 2.0, bh - pad), (bw - pad, pad)]
    d.line(pts, fill=color, width=stroke, joint="curve")

    img.resize((w, h), Image.LANCZOS).save(path)
    print("wrote", path)


if __name__ == "__main__":
    import os

    here = os.path.dirname(os.path.abspath(__file__))
    make(16, os.path.join(here, "check-white.png"))
    make(32, os.path.join(here, "check-white@2x.png"))

    make_chevron(9, 6, os.path.join(here, "spin-up.png"), up=True)
    make_chevron(18, 12, os.path.join(here, "spin-up@2x.png"), up=True)
    make_chevron(9, 6, os.path.join(here, "spin-down.png"), up=False)
    make_chevron(18, 12, os.path.join(here, "spin-down@2x.png"), up=False)
