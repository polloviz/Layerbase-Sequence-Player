"""Writes a small uncompressed half-float EXR test sequence (scene-linear, with HDR values).
Usage: python make_test_sequence.py <out_dir> [frames] [width] [height]"""
import math
import os
import struct
import sys


def attr(name, typ, data):
    return name.encode() + b"\0" + typ.encode() + b"\0" + struct.pack("<i", len(data)) + data


def write_exr(path, w, h, pixel):
    chans = ["A", "B", "G", "R"]  # must be sorted
    chlist = b"".join(c.encode() + b"\0" + struct.pack("<iB3xii", 1, 0, 1, 1) for c in chans) + b"\0"
    header = b"\x76\x2f\x31\x01" + struct.pack("<i", 2)
    header += attr("channels", "chlist", chlist)
    header += attr("compression", "compression", b"\0")
    header += attr("dataWindow", "box2i", struct.pack("<iiii", 0, 0, w - 1, h - 1))
    header += attr("displayWindow", "box2i", struct.pack("<iiii", 0, 0, w - 1, h - 1))
    header += attr("lineOrder", "lineOrder", b"\0")
    header += attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    header += attr("screenWindowCenter", "v2f", struct.pack("<ff", 0, 0))
    header += attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    header += b"\0"

    line_bytes = w * 2 * len(chans)
    block = 8 + line_bytes
    table_start = len(header)
    first = table_start + 8 * h
    table = b"".join(struct.pack("<Q", first + y * block) for y in range(h))
    blocks = []
    fmt = "<%de" % w
    for y in range(h):
        rows = {c: [] for c in chans}
        for x in range(w):
            r, g, b, a = pixel(x, y)
            rows["R"].append(r); rows["G"].append(g); rows["B"].append(b); rows["A"].append(a)
        data = b"".join(struct.pack(fmt, *rows[c]) for c in chans)
        blocks.append(struct.pack("<ii", y, line_bytes) + data)
    with open(path, "wb") as f:
        f.write(header + table + b"".join(blocks))


def main():
    out = sys.argv[1]
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 24
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 640
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 360
    os.makedirs(out, exist_ok=True)
    for f in range(frames):
        t = f / max(1, frames - 1)
        cx, cy = w * (0.15 + 0.7 * t), h * (0.5 + 0.25 * math.sin(t * math.tau))

        def pixel(x, y):
            u, v = x / (w - 1), y / (h - 1)
            # exposure ramp: 2^(-6..+4) across x, hue across y
            lum = 2 ** (-6 + 10 * u)
            r = lum * (0.5 + 0.5 * math.cos(v * math.tau))
            g = lum * (0.5 + 0.5 * math.cos(v * math.tau - 2.094))
            b = lum * (0.5 + 0.5 * math.cos(v * math.tau + 2.094))
            if (x - cx) ** 2 + (y - cy) ** 2 < (h * 0.12) ** 2:
                r, g, b = 0.18, 0.18, 0.18  # mid grey ball
            return r, g, b, 1.0
        write_exr(os.path.join(out, "test_shot.%04d.exr" % (1001 + f)), w, h, pixel)
        print("frame", 1001 + f)


if __name__ == "__main__":
    main()
