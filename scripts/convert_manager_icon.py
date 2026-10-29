#!/usr/bin/env python3
"""Rewrite the Manager launcher icon with an LVGL v8 image header.

manager_icon.bin carries an LVGL v9 header (magic 0x19, colour format, w, h,
stride) in front of ARGB8888 pixels. The v8 firmware generation (3.1.32,
3.1.190) reads `lv_img_header_t`: cf in bits 0-4, width in bits 10-20, height in
bits 21-31, and its GPU decoder rejects anything but a true-colour format
(`lv_gpu_decoder_info: invalid file ... cf: 25`). The format becomes
LV_IMG_CF_TRUE_COLOR_ALPHA. 3.1.190 uses 32-bit colour, so the pixels stay as
they are; 3.1.32 uses RGB565, where a pixel is the little-endian RGB565 value
followed by one alpha byte.

usage: convert_manager_icon.py [--rgb565] SOURCE.bin OUT.bin
"""
import struct
import sys

V9_MAGIC = 0x19
V9_ARGB8888 = 0x10
V8_TRUE_COLOR_ALPHA = 5


def main(source, out, rgb565):
    data = open(source, "rb").read()
    magic, color_format, _flags, width, height, stride, _reserved = struct.unpack_from("<BBHHHHH", data)
    if magic != V9_MAGIC or color_format != V9_ARGB8888 or stride != width * 4:
        sys.exit("source is not a v9 ARGB8888 icon")
    pixels = data[12:]
    if len(pixels) != stride * height:
        sys.exit("pixel data does not match the header")
    if rgb565:
        converted = bytearray()
        for i in range(0, len(pixels), 4):
            blue, green, red, alpha = pixels[i:i + 4]
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            converted += struct.pack("<HB", value, alpha)
        pixels = bytes(converted)
    header = V8_TRUE_COLOR_ALPHA | (width << 10) | (height << 21)
    open(out, "wb").write(struct.pack("<I", header) + pixels)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--rgb565"]
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], "--rgb565" in sys.argv)
