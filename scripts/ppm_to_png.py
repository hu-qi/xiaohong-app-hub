#!/usr/bin/env python3
"""Convert the exact 240x240 RGB P6 LVGL framebuffer preview to PNG with stdlib."""
import struct
import sys
import zlib

def chunk(name, data):
    return (struct.pack(">I", len(data)) + name + data +
            struct.pack(">I", zlib.crc32(name + data) & 0xffffffff))

def convert(input_path, output_path):
    with open(input_path, "rb") as f:
        assert f.readline() == b"P6\n"
        assert f.readline() == b"240 240\n"
        assert f.readline() == b"255\n"
        pixels = f.read()
    assert len(pixels) == 240 * 240 * 3
    scanlines = b"".join(b"\x00" + pixels[y*240*3:(y+1)*240*3]
                         for y in range(240))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", 240,240,8,2,0,0,0))
    png += chunk(b"IDAT", zlib.compress(scanlines,9))
    png += chunk(b"IEND", b"")
    with open(output_path, "wb") as f: f.write(png)

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
