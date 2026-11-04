"""Contact sheet of BLP textures or PNG shots (Tools/WebClient/photo.js), for picking textures and comparing views.

    python Tools/MpqTool/blp_sheet.py out.png [--size 128] [--cols 8] a.blp b.png ...

Draws each texture at --size pixels (its nearest mipmap, tiled) and each PNG shrunk to --size wide in a grid, numbered from 0 in the order given; the numbers
and file names are printed too. Extract the BLPs first: build/MpqTool/Release/MpqTool.exe extract <dir> <file> <mpqs>.
"""
import struct
import sys
import zlib

# 3x5 digits for the numbers on the sheet
DIGITS = ["111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
          "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111"]


def rgb565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def dxt_colors(block, offset, four):
    c0, c1 = struct.unpack_from("<HH", block, offset)
    a, b = rgb565(c0), rgb565(c1)
    if four or c0 > c1:
        return [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
    return [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0)]


def decode(data):
    """The BLP2's mipmaps as (width, height, rgb bytes) from largest to smallest."""
    if data[:4] != b"BLP2":
        raise ValueError("not a BLP2")
    compression, alpha_depth, alpha_type, _ = data[8:12]
    width, height = struct.unpack_from("<II", data, 12)
    offsets = struct.unpack_from("<16I", data, 20)
    sizes = struct.unpack_from("<16I", data, 84)
    palette = struct.unpack_from("<256I", data, 148)
    mips = []
    for level in range(16):
        w, h = max(1, width >> level), max(1, height >> level)
        if not sizes[level] or w < 4 or h < 4:
            break
        mip = data[offsets[level]:offsets[level] + sizes[level]]
        rgb = bytearray(w * h * 3)
        if compression == 2:  # DXT1, DXT3 (alpha type 1) or DXT5 (7): only the colour part is drawn
            step = 8 if alpha_type == 0 and alpha_depth <= 1 else 16
            colour = step - 8
            for by in range(h // 4):
                for bx in range(w // 4):
                    o = (by * (w // 4) + bx) * step
                    palette4 = dxt_colors(mip, o + colour, step == 16)
                    bits = struct.unpack_from("<I", mip, o + colour + 4)[0]
                    for p in range(16):
                        r, g, b = palette4[(bits >> (2 * p)) & 3]
                        i = ((by * 4 + p // 4) * w + bx * 4 + p % 4) * 3
                        rgb[i:i + 3] = bytes((r, g, b))
        elif compression == 1:  # palette indices
            for p in range(w * h):
                c = palette[mip[p]]
                rgb[p * 3:p * 3 + 3] = bytes(((c >> 16) & 255, (c >> 8) & 255, c & 255))
        else:  # 3: BGRA
            for p in range(w * h):
                rgb[p * 3:p * 3 + 3] = bytes((mip[p * 4 + 2], mip[p * 4 + 1], mip[p * 4]))
        mips.append((w, h, bytes(rgb)))
    return mips


def read_png(data):
    """An 8-bit RGB or RGBA PNG (the photo tool's shots) as [(width, height, rgb bytes)]."""
    width, height, depth, kind = struct.unpack_from(">IIBB", data, 16)
    if depth != 8 or kind not in (2, 6):
        raise ValueError("only 8-bit RGB(A) PNGs")
    i, idat = 8, b""
    while i < len(data):
        size, tag = struct.unpack_from(">I4s", data, i)
        if tag == b"IDAT":
            idat += data[i + 8:i + 8 + size]
        i += 12 + size
    raw, bpp = zlib.decompress(idat), 3 if kind == 2 else 4
    stride = width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line) if bpp == 3 else bytes(v for k, v in enumerate(line) if k % 4 != 3))
        prev = line
    return [(width, height, b"".join(rows))]


def shrink(image, size):
    """Nearest-neighbour shrink so the longer side is size."""
    w, h, rgb = image
    scale = max(w, h) / size
    nw, nh = max(1, int(w / scale)), max(1, int(h / scale))
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        for x in range(nw):
            i = (int(y * scale) * w + int(x * scale)) * 3
            out[(y * nw + x) * 3:(y * nw + x) * 3 + 3] = rgb[i:i + 3]
    return nw, nh, bytes(out)


def png(width, height, rgb):
    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    chunk = lambda kind, body: struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main(args):
    size, cols = 128, 8
    out = args.pop(0)
    while args and args[0].startswith("--"):
        flag, value = args.pop(0), int(args.pop(0))
        size, cols = (value, cols) if flag == "--size" else (size, value)
    images = []
    for n, path in enumerate(args):
        print(n, path)
        try:
            data = open(path, "rb").read()
            if path.lower().endswith(".png"):  # shots: shrunk to fit, not tiled
                images.append((shrink(read_png(data)[0], size), False))
            else:
                mips = decode(data)
                images.append((next((m for m in mips if m[0] <= size), mips[-1]), True))
        except (OSError, ValueError, struct.error, zlib.error) as error:
            print("  skipped:", error)
            images.append(((1, 1, b"\0\0\0"), False))
    cols = min(cols, len(images))
    rows = (len(images) + cols - 1) // cols
    gap = 4
    cell = max(size if tiled else h for (w, h, rgb), tiled in images)
    width, height = cols * (size + gap), rows * (cell + gap)
    sheet = bytearray(width * height * 3)
    for n, ((w, h, rgb), tiled) in enumerate(images):
        x0, y0 = (n % cols) * (size + gap), (n // cols) * (cell + gap)
        for y in range(size if tiled else h):  # textures tile when smaller than the cell
            for x in range(size if tiled else w):
                i = ((y % h) * w + x % w) * 3
                j = ((y0 + y) * width + x0 + x) * 3
                sheet[j:j + 3] = rgb[i:i + 3]
        for k, digit in enumerate(str(n)):  # the number, white on black, scaled 2x
            for p, bit in enumerate(DIGITS[int(digit)]):
                for dy in range(2):
                    for dx in range(2):
                        x, y = x0 + 2 + k * 8 + (p % 3) * 2 + dx, y0 + 2 + (p // 3) * 2 + dy
                        sheet[(y * width + x) * 3:(y * width + x) * 3 + 3] = b"\xff\xff\xff" if bit == "1" else b"\0\0\0"
    open(out, "wb").write(png(width, height, bytes(sheet)))


if __name__ == "__main__":
    main(sys.argv[1:])
