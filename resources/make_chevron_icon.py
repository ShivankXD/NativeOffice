"""Generates the light chevron used by combo boxes on the dark tool pages.

Qt cannot draw a triangle from CSS borders (it paints an empty block), and the
existing spin-up/spin-down artwork is dark: correct on the light editor chrome,
invisible on the dark Home tools. This writes a pale chevron at 1x and 2x.

    python resources/make_chevron_icon.py
"""
from PIL import Image, ImageDraw

COLOR = (174, 182, 198, 255)   # Home::kTextBody-ish, reads on the dark panels


def draw(path, w, h, scale):
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = 1 * scale
    d.line(
        [(pad, h * scale * 0.32), (w * scale / 2, h * scale - pad),
         (w * scale - pad, h * scale * 0.32)],
        fill=COLOR, width=max(1, int(1.6 * scale)), joint="curve")
    img.save(path)
    print(path, img.size)


draw("chevron-down-light.png", 10, 7, 1)
draw("chevron-down-light@2x.png", 10, 7, 2)
