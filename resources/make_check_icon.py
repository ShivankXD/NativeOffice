"""Generate the white checkmark used by QCheckBox::indicator:checked.

Qt stylesheets cannot draw a tick, so a styled indicator needs a real image or
it renders as a bare filled square. Run this if the mark ever needs reshaping:

    python resources/make_check_icon.py

Writes check-white.png (16x16) and check-white@2x.png (32x32); Qt picks the
@2x variant automatically on high-DPI screens.
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


if __name__ == "__main__":
    import os

    here = os.path.dirname(os.path.abspath(__file__))
    make(16, os.path.join(here, "check-white.png"))
    make(32, os.path.join(here, "check-white@2x.png"))
