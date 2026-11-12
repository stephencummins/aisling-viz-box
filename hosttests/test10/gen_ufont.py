#!/usr/bin/env python3
"""ufont.h for the box: Uncial Antiqua (SIL OFL 1.1, the face AISLING 1.3 names its effects in), capitals, figures
and signs (32..95), at the sizes the box's playing pages use. Each glyph is its advance wide and the font's line
high, rows top to bottom, 2 pixels a byte (high nibble first), alpha 0..15. Our own script: fm1-aisling's
gen_font.py isn't run. Usage: gen_ufont.py UncialAntiqua-Regular.ttf > ufont.h"""
import sys
from PIL import Image, ImageDraw, ImageFont

SIZES = [("U15", 15), ("U24", 24)]
FIRST, LAST = 32, 95
out = ["// ufont.h: Uncial Antiqua (SIL OFL 1.1; UncialAntiqua-OFL.txt beside this file) as alpha bitmaps, made by",
       "// hosttests/test8/gen_ufont.py from fm1-aisling's assets/fonts/UncialAntiqua-Regular.ttf: don't edit, run it again.",
       "#ifndef VIZ_UFONT_H_INCLUDED", "#define VIZ_UFONT_H_INCLUDED", "#include <stdint.h>", "",
       "#define UFONT_FIRST %d" % FIRST, "#define UFONT_LAST %d" % LAST,
       "typedef struct {", "    uint8_t h;                 // the line's height",
       "    uint8_t w[%d];             // each glyph's width (its advance)" % (LAST - FIRST + 1),
       "    uint16_t at[%d];           // where its rows start in px" % (LAST - FIRST + 1),
       "    const uint8_t *px;         // rows of (w + 1) / 2 bytes", "} ufont_t;", ""]
for name, size in SIZES:
    f = ImageFont.truetype(sys.argv[1], size)
    asc, desc = f.getmetrics()
    h = asc + desc
    ws, ats, data = [], [], []
    for c in range(FIRST, LAST + 1):
        w = max(1, round(f.getlength(chr(c))))
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((0, asc), chr(c), font=f, fill=255, anchor="ls")
        ws.append(w)
        ats.append(len(data))
        p = im.load()
        for y in range(h):
            for x in range(0, w, 2):
                a = (p[x, y] * 15 + 127) // 255
                b = (p[x + 1, y] * 15 + 127) // 255 if x + 1 < w else 0
                data.append(a << 4 | b)
    assert h < 256 and max(ws) < 256 and len(data) < 65536
    out.append("static const uint8_t UFONT_%s_PX[%d] = {" % (name, len(data)))
    for i in range(0, len(data), 24):
        out.append("    " + ",".join("%d" % b for b in data[i:i + 24]) + ",")
    out.append("};")
    out.append("static const ufont_t UFONT_%s = {%d, {%s},\n    {%s}, UFONT_%s_PX};" % (
        name, h, ",".join(map(str, ws)), ",".join(map(str, ats)), name))
    out.append("")
out.append("#endif")
print("\n".join(out))
