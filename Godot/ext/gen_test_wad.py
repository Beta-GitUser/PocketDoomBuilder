#!/usr/bin/env python3
"""Generate a minimal PWAD with one Doom map (MAP01) for the GDExtension spike test.

Mirrors makeDoomWad() in Android/tests/pdb_tests.cpp so the C++ core
(readWad, listMaps, loadMap) can parse it.
"""
import struct
import sys
import os


def put_name(buf, name):
    name_bytes = name.encode("ascii")[:8]
    buf.extend(name_bytes.ljust(8, b"\x00"))


def put16(buf, val):
    buf.extend(struct.pack("<H", val))


def make_doom_wad():
    lumps = []  # (name, data)

    # MAP01 header (empty)
    lumps.append(("MAP01", b""))

    # THINGS: 2 things, 10 bytes each
    t = bytearray()
    for thing in [
        (64, 128, 90, 1, 7, 256, 192, 180, 2, 0),
        (64, 128, 90, 1, 7, 256, 192, 180, 2, 0),
    ]:
        for v in thing:
            put16(t, v)
    lumps.append(("THINGS", bytes(t)))

    # VERTEXES: 4 vertices
    v = bytearray()
    for (x, y) in [(0, 0), (256, 0), (256, 256), (0, 256)]:
        put16(v, x)
        put16(v, y)
    lumps.append(("VERTEXES", bytes(v)))

    # LINEDEFS: 4 linedefs forming a square
    l = bytearray()
    for i in range(4):
        put16(l, i)
        put16(l, (i + 1) % 4)
        put16(l, 1)       # flags: blocking
        put16(l, 0)       # action
        put16(l, 0)       # tag
        put16(l, i)       # front sidedef
        put16(l, 0xFFFF)  # back sidedef
    lumps.append(("LINEDEFS", bytes(l)))

    # SIDEDEFS: 4 sidedefs -> sector 0
    s = bytearray()
    for _ in range(4):
        put16(s, 0)  # x offset
        put16(s, 0)  # y offset
        put_name(s, "-")
        put_name(s, "-")
        put_name(s, "STARTAN3")
        put16(s, 0)  # sector
    lumps.append(("SIDEDEFS", bytes(s)))

    # SECTORS: 1 sector
    sec = bytearray()
    put16(sec, 0)       # floor height
    put16(sec, 128)     # ceiling height
    put_name(sec, "FLOOR4_8")
    put_name(sec, "CEIL3_5")
    put16(sec, 160)     # brightness
    put16(sec, 0)       # special
    put16(sec, 1)       # tag
    lumps.append(("SECTORS", bytes(sec)))

    # Build the WAD image.
    data_offset = 12  # header is 12 bytes
    directory = bytearray()
    file_data = bytearray()

    for name, lump_data in lumps:
        lump_offset = data_offset + len(file_data)
        # The C++ core's writeWad() uses a non-standard directory layout:
        # [offset:4][size:4][name:8] (standard WAD is [name:8][offset:4][size:4]).
        directory.extend(struct.pack("<I", lump_offset))       # offset
        directory.extend(struct.pack("<I", len(lump_data)))    # size
        put_name(directory, name)
        file_data.extend(lump_data)

    header = b"PWAD"
    header += struct.pack("<I", len(lumps))   # lump count
    header += struct.pack("<I", 12 + len(file_data))  # directory offset

    return header + file_data + directory


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "test.wad"
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    wad_bytes = make_doom_wad()
    with open(out_path, "wb") as f:
        f.write(wad_bytes)
    print("Wrote %s (%d bytes)" % (out_path, len(wad_bytes)))


if __name__ == "__main__":
    main()
