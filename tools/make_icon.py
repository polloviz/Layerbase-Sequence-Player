"""Builds the Windows icons from the source artwork.

  res/app_icon.png   -> res/app.ico   (application icon)
  res/file_icon.png  -> res/file.ico  (icon for associated image files)
  res/app_icon.png   -> res/logo.png  (256 px, shown in the About dialog)

Requires Pillow (pip install pillow)."""
import io
import os
import struct

from PIL import Image

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def squared(path):
    """Crops to the visible content and centers it on a transparent square."""
    im = Image.open(path).convert("RGBA")
    bbox = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    im = im.crop(bbox)
    side = max(im.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    return canvas


def write_ico(src, out):
    entries, blobs = [], []
    for s in SIZES:
        img = src.resize((s, s), Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, "PNG", optimize=True)
        blobs.append(buf.getvalue())
        entries.append(s)
    offset = 6 + 16 * len(blobs)
    data = struct.pack("<HHH", 0, 1, len(blobs))
    for s, b in zip(entries, blobs):
        data += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(b), offset)
        offset += len(b)
    with open(out, "wb") as f:
        f.write(data + b"".join(blobs))
    print("wrote", os.path.abspath(out))


def main():
    app = squared(os.path.join(RES, "app_icon.png"))
    doc = squared(os.path.join(RES, "file_icon.png"))
    write_ico(app, os.path.join(RES, "app.ico"))
    write_ico(doc, os.path.join(RES, "file.ico"))
    app.resize((256, 256), Image.LANCZOS).save(os.path.join(RES, "logo.png"), optimize=True)
    print("wrote", os.path.abspath(os.path.join(RES, "logo.png")))


if __name__ == "__main__":
    main()
