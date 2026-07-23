"""Generate all MSIX tile/logo assets from resources/nativeoffice.ico.

The .ico's 256x256 frame is the full transparent logo: the "N" mark on top,
the wordmark + tagline below, separated by a fully-transparent row gap. Small
tiles (44/71/150, store logo) use the mark alone -- the wordmark is illegible
at those sizes -- while wide/large tiles and the splash use the full logo.

Run from anywhere:  python make_assets.py
Outputs into <this dir>/Assets/.
"""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent / "resources" / "nativeoffice.ico"
OUT = HERE / "Assets"


def alpha_bbox(im):
    return im.getchannel("A").getbbox()


def find_mark(im):
    """Crop the icon mark: everything above the widest fully-transparent row
    gap in the middle half of the image (the gap between mark and wordmark)."""
    a = im.getchannel("A")
    w, h = im.size
    # "Empty" tolerates faint drop-shadow alpha; a hard is-None bbox test finds
    # no gap at all because the shadow bridges the mark and the wordmark.
    rows = list(a.getdata())
    empty = [max(rows[y * w:(y + 1) * w]) < 24 for y in range(h)]
    # Crop above the FIRST gap of >=3 empty rows past 25% height. The logo has
    # two gaps (mark|wordmark and wordmark|tagline); "longest gap" picked the
    # second and kept the wordmark in the 44px tiles.
    best_start, run_start = None, None
    for y in range(h // 4, int(h * 0.85)):
        if empty[y]:
            if run_start is None:
                run_start = y
            if y - run_start + 1 >= 3:
                best_start = run_start
                break
        else:
            run_start = None
    if best_start is None:            # no gap found -- fall back to full logo
        return im.crop(alpha_bbox(im))
    mark = im.crop((0, 0, w, best_start))
    return mark.crop(alpha_bbox(mark))


def place(source, size, fill=0.78):
    """Center `source` on a transparent size[0]xsize[1] canvas, scaled to
    occupy `fill` of the limiting dimension."""
    tw, th = size
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    sw, sh = source.size
    scale = min(tw * fill / sw, th * fill / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    scaled = source.resize((nw, nh), Image.LANCZOS)
    canvas.paste(scaled, ((tw - nw) // 2, (th - nh) // 2), scaled)
    return canvas


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ico = Image.open(SRC)
    ico.size = (256, 256)             # select the largest frame
    full = ico.convert("RGBA")
    full = full.crop(alpha_bbox(full))
    mark = find_mark(ico.convert("RGBA"))

    jobs = {
        "StoreLogo.png":          (mark, (50, 50), 0.90),
        "Square44x44Logo.png":    (mark, (44, 44), 0.90),
        "Square71x71Logo.png":    (mark, (71, 71), 0.80),
        "Square150x150Logo.png":  (mark, (150, 150), 0.62),
        "Square310x310Logo.png":  (full, (310, 310), 0.66),
        "Wide310x150Logo.png":    (full, (310, 150), 0.78),
        "SplashScreen.png":       (full, (620, 300), 0.72),
    }
    for name, (src, size, fill) in jobs.items():
        place(src, size, fill).save(OUT / name)
        print(f"{name}: {size[0]}x{size[1]}")


if __name__ == "__main__":
    main()
