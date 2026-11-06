#!/usr/bin/env python3
"""Convert a fixed-cell BDF font into a compact ASCII C bitmap table."""

import sys


FONT_WIDTH = 6
FONT_HEIGHT = 12


def parse_font(path):
    glyphs = {}
    font_box = None
    glyph = None
    reading_bitmap = False

    with open(path, encoding="ascii") as source:
        for raw_line in source:
            line = raw_line.strip()
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "FONTBOUNDINGBOX":
                font_box = tuple(map(int, fields[1:]))
            elif fields[0] == "STARTCHAR":
                glyph = {"encoding": None, "bbx": None, "bitmap": []}
                reading_bitmap = False
            elif glyph is not None and fields[0] == "ENCODING":
                glyph["encoding"] = int(fields[1])
            elif glyph is not None and fields[0] == "BBX":
                glyph["bbx"] = tuple(map(int, fields[1:]))
            elif glyph is not None and fields[0] == "BITMAP":
                reading_bitmap = True
            elif glyph is not None and fields[0] == "ENDCHAR":
                encoding = glyph["encoding"]
                if encoding is not None and 0 <= encoding < 128:
                    glyphs[encoding] = glyph
                glyph = None
                reading_bitmap = False
            elif glyph is not None and reading_bitmap:
                glyph["bitmap"].append(line)

    if font_box is None or font_box[:2] != (FONT_WIDTH, FONT_HEIGHT):
        raise ValueError("font must have a 6x12 FONTBOUNDINGBOX")
    if ord("?") not in glyphs:
        raise ValueError("font has no ASCII question-mark glyph")
    return glyphs


def rasterize(glyph):
    if glyph["bbx"] is None:
        raise ValueError("glyph is missing BBX metrics")

    width, height, x_offset, y_offset = glyph["bbx"]
    bitmap = glyph["bitmap"]
    if len(bitmap) != height:
        raise ValueError("glyph bitmap row count does not match BBX height")

    rows = [0] * FONT_HEIGHT
    top = FONT_HEIGHT - (y_offset + height)
    for row_index, encoded_row in enumerate(bitmap):
        bits = int(encoded_row, 16)
        bit_count = len(encoded_row) * 4
        for column in range(width):
            x = x_offset + column
            y = top + row_index
            if (bits & (1 << (bit_count - 1 - column)) and
                    0 <= x < FONT_WIDTH and 0 <= y < FONT_HEIGHT):
                rows[y] |= 1 << (FONT_WIDTH - 1 - x)
    return rows


def emit(path, glyphs):
    fallback = rasterize(glyphs[ord("?")])
    rows_by_code = [fallback[:] for _ in range(128)]
    for code, glyph in glyphs.items():
        rows_by_code[code] = rasterize(glyph)

    with open(path, "w", encoding="ascii") as output:
        output.write('#include "stdint.h"\n\n')
        output.write("const uint8_t curie_font[128][12] = {\n")
        for code, rows in enumerate(rows_by_code):
            output.write("    [%d] = {" % code)
            output.write(", ".join("0x%02X" % row for row in rows))
            output.write("},\n")
        output.write("};\n")


def main():
    if len(sys.argv) != 3:
        print("usage: bdf_to_c.py FONT.bdf OUTPUT.c", file=sys.stderr)
        return 2
    try:
        emit(sys.argv[2], parse_font(sys.argv[1]))
    except (OSError, UnicodeError, ValueError) as error:
        print("font conversion failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
