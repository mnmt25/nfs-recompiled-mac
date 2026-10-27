#!/usr/bin/env python3
"""Converts the largest image of a Windows .ico file to a square PNG.

    ico-to-png.py <icon.ico> <output.png> <size>

Upscaling uses nearest neighbour so the original pixel art stays sharp;
downscaling averages the covered pixels. Only the standard library is used.
"""
import struct
import sys
import zlib


def read_ico(data):
    """Returns (width, height, rows of RGBA tuples, top row first)."""
    _, kind, count = struct.unpack_from('<HHH', data, 0)
    if kind != 1 or count == 0:
        sys.exit('not an icon file')
    entries = [struct.unpack_from('<BBBBHHII', data, 6 + 16 * i) for i in range(count)]
    # 0 in the directory means 256
    width, height, _, _, _, _, size, offset = max(entries, key=lambda e: (e[0] or 256) * (e[1] or 256))
    image = data[offset:offset + size]
    if image.startswith(b'\x89PNG'):
        sys.exit('PNG-compressed icons are not supported')

    header_size, w, h2, _, bpp = struct.unpack_from('<IiiHH', image, 0)
    h = h2 // 2  # the height covers the colour bitmap and the AND mask
    colours = struct.unpack_from('<I', image, 32)[0] or (1 << bpp if bpp <= 8 else 0)
    palette_offset = header_size
    pixels_offset = palette_offset + colours * 4
    palette = [tuple(image[palette_offset + 4 * i:palette_offset + 4 * i + 3][::-1]) for i in range(colours)]

    stride = ((w * bpp + 31) // 32) * 4
    mask_offset = pixels_offset + stride * h
    mask_stride = ((w + 31) // 32) * 4

    rows = []
    for y in range(h):
        src = h - 1 - y  # bitmaps are stored bottom-up
        row_data = image[pixels_offset + src * stride:pixels_offset + (src + 1) * stride]
        mask_data = image[mask_offset + src * mask_stride:mask_offset + (src + 1) * mask_stride]
        row = []
        for x in range(w):
            transparent = (mask_data[x // 8] >> (7 - x % 8)) & 1 if mask_data else 0
            if bpp == 32:
                b, g, r, a = row_data[4 * x:4 * x + 4]
                row.append((r, g, b, a))
                continue
            if bpp == 24:
                b, g, r = row_data[3 * x:3 * x + 3]
            else:
                bit = x * bpp
                index = (row_data[bit // 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1)
                r, g, b = palette[index]
            row.append((r, g, b, 0 if transparent else 255))
        rows.append(row)
    return w, h, rows


def resize(w, h, rows, size):
    out = []
    for y in range(size):
        y0, y1 = y * h // size, max(y * h // size + 1, (y + 1) * h // size)
        row = []
        for x in range(size):
            x0, x1 = x * w // size, max(x * w // size + 1, (x + 1) * w // size)
            covered = [rows[sy][sx] for sy in range(y0, y1) for sx in range(x0, x1)]
            alpha = sum(p[3] for p in covered)
            if alpha == 0:
                row.append((0, 0, 0, 0))
                continue
            # alpha-weighted so transparent neighbours do not darken edges
            row.append(tuple(sum(p[c] * p[3] for p in covered) // alpha for c in range(3))
                       + (alpha // len(covered),))
        out.append(row)
    return out


def write_png(path, size, rows):
    raw = b''.join(b'\x00' + bytes(c for pixel in row for c in pixel) for row in rows)

    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload
                + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip())
    with open(sys.argv[1], 'rb') as f:
        w, h, rows = read_ico(f.read())
    size = int(sys.argv[3])
    write_png(sys.argv[2], size, resize(w, h, rows, size))


if __name__ == '__main__':
    main()
