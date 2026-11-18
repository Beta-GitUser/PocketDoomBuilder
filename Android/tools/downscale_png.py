#!/usr/bin/env python3
"""Downscale an RGBA PNG by an integer factor, filtering-free box average.

Used for Android/tools/preview.png: the headless renderer has to draw at the
real 1350x933 UI size (that is what makes the layout metrics correct), but the
committed screenshot should stay small, so it is halved on the way in.

Usage: python3 Android/tools/downscale_png.py in.png out.png [factor]
"""

import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s is not a PNG" % path)
    pos = 8
    idat = bytearray()
    width = height = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color != 6 or interlace != 0:
                raise SystemExit("only 8-bit RGBA, non-interlaced PNGs are supported")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(bytes(idat))

    # Undo the per-scanline filters.
    stride = width * 4
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ftype = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if ftype == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def write_png(path, width, height, rgba):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) < 3:
        print("usage: downscale_png.py in.png out.png [factor]", file=sys.stderr)
        return 2
    src, dst = argv[1], argv[2]
    factor = int(argv[3]) if len(argv) > 3 else 2
    if factor < 1:
        print("factor must be >= 1", file=sys.stderr)
        return 2

    width, height, rgba = read_png(src)
    if factor == 1:
        write_png(dst, width, height, rgba)
        print("%s (%dx%d, factor 1)" % (dst, width, height))
        return 0

    out_w, out_h = width // factor, height // factor
    out = bytearray(out_w * out_h * 4)
    area = factor * factor
    for oy in range(out_h):
        for ox in range(out_w):
            r = g = b = a = 0
            for dy in range(factor):
                base = ((oy * factor + dy) * width + ox * factor) * 4
                for dx in range(factor):
                    i = base + dx * 4
                    r += rgba[i]
                    g += rgba[i + 1]
                    b += rgba[i + 2]
                    a += rgba[i + 3]
            o = (oy * out_w + ox) * 4
            out[o] = r // area
            out[o + 1] = g // area
            out[o + 2] = b // area
            out[o + 3] = a // area
    write_png(dst, out_w, out_h, out)
    print("%s (%dx%d, factor %d)" % (dst, out_w, out_h, factor))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
