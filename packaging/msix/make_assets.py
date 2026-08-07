"""Generate all MSIX tile/logo assets.

WHY THIS MATTERS: an MSIX package does NOT use the .exe's embedded icon for the
Start Menu, the taskbar or the Store listing. Windows reads the PNGs below,
declared in AppxManifest. So fixing resources/nativeoffice.ico does nothing for
Store installs until this script is re-run and the package rebuilt. That is
exactly what went wrong between 1.5.1 and 1.6.0: the .ico was cropped to the
mark on 2026-08-06, these assets were left at their 2026-07-23 versions, and
Store users kept seeing the old squashed full-lockup icon on the taskbar while
zip/Inno users saw the correct one.

Two sources, deliberately:

  mark  <- resources/nativeoffice.ico (256px frame)
          Since the 2026-08-06 icon fix this frame is ALREADY just the N mark,
          so there is nothing to crop out of it. The previous version of this
          script hunted for the transparent gap between mark and wordmark,
          which only made sense while the .ico still held the whole lockup.

  full  <- resources/logo_white_bg.png
          The 1024px master, still the complete lockup (mark + wordmark +
          tagline). Used only where the wordmark is actually legible: the wide
          tile, the largest square tile, and the splash screen.

Run from anywhere:  python make_assets.py
Outputs into <this dir>/Assets/.
"""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
SRC_ICO = HERE.parent.parent / "resources" / "nativeoffice.ico"
SRC_FULL = HERE.parent.parent / "resources" / "logo_white_bg.png"
OUT = HERE / "Assets"


def trimmed(im):
    """Crop away fully transparent margins so `fill` below means what it says."""
    box = im.getchannel("A").getbbox()
    return im.crop(box) if box else im


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

    ico = Image.open(SRC_ICO)
    ico.size = (256, 256)             # select the largest frame
    mark = trimmed(ico.convert("RGBA"))
    full = trimmed(Image.open(SRC_FULL).convert("RGBA"))

    jobs = {
        # Small surfaces: the mark alone. A wordmark is unreadable here and
        # only steals room from the thing that identifies the app.
        "StoreLogo.png":          (mark, (50, 50), 0.90),
        "Square44x44Logo.png":    (mark, (44, 44), 0.90),
        "Square71x71Logo.png":    (mark, (71, 71), 0.80),
        "Square150x150Logo.png":  (mark, (150, 150), 0.62),
        # Large / wide surfaces: the full lockup reads properly at this size.
        "Square310x310Logo.png":  (full, (310, 310), 0.66),
        "Wide310x150Logo.png":    (full, (310, 150), 0.78),
        "SplashScreen.png":       (full, (620, 300), 0.72),
    }
    for name, (src, size, fill) in jobs.items():
        place(src, size, fill).save(OUT / name)
        print(f"{name}: {size[0]}x{size[1]}")

    # ── targetsize variants, plated and unplated ────────────────────────────
    # This is what actually fixes the taskbar.
    #
    # With only Square44x44Logo.png present, Windows renders the app icon on a
    # PLATE: a solid tile (the manifest's BackgroundColor, or a theme colour
    # when that is "transparent") with the logo shrunk and centred on it. That
    # is the small-icon-on-a-dark-square look Store users were getting, while
    # the zip build showed a clean edge-to-edge mark because a plain .exe icon
    # is never plated.
    #
    # Supplying Square44x44Logo.targetsize-N_altform-unplated.png tells Windows
    # to draw the icon bare. These are found by filename convention, so the
    # manifest does not change. Plated targetsize variants are emitted too, so
    # surfaces that do want a plate get a properly sized source instead of a
    # rescaled 44px one.
    #
    # fill is 1.0 here on purpose: targetsize means "the icon AT this size",
    # and any padding baked in is padding Windows cannot remove.
    for px in (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 96, 256):
        plated = place(mark, (px, px), 1.0)
        plated.save(OUT / f"Square44x44Logo.targetsize-{px}.png")
        plated.save(OUT / f"Square44x44Logo.targetsize-{px}_altform-unplated.png")
    print(f"targetsize variants: 13 sizes, plated + unplated")


if __name__ == "__main__":
    main()
