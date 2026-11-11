#!/usr/bin/env python3
"""pflash.h: PERFORM's 27 effects as AISLING 1.3 names, describes and colours them, read out of its ui.c.

gen_pflash.py UI_C OUT

ui.c's PKEY (name, group), PLINE (the line under the name), PG_COL (a colour a group) and pkey_col (a shade a key:
the group's colour mixed towards bainin, a step for each key before it in its group). The box's flash of a held
effect (fm1scr.c) is drawn from this table, so its colours are the FM-1's own."""
import re
import sys


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3      # gfx.c's RGB


def mix(a, b, t):                                        # ui.c's mix: t 0..16, a -> b
    if t <= 0:
        return a
    if t >= 16:
        return b
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) // 16
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) // 16
    bl = ((a & 31) * (16 - t) + (b & 31) * t) // 16
    return r << 11 | g << 5 | bl


def main():
    ui_c, out = sys.argv[1:3]
    src = open(ui_c, encoding='utf-8').read()

    def colour(name):
        m = re.search(r'#define %s RGB\((\d+), *(\d+), *(\d+)\)' % re.escape(name), src)
        if not m:
            sys.exit('gen_pflash: no colour %s in ui.c' % name)
        return rgb565(*map(int, m.groups()))

    m = re.search(r'#define PERF_N (\d+)', src)
    perf_n = int(m.group(1))
    m = re.search(r'enum \{ (PG_\w+(?:, PG_\w+)*), PG_N \};', src)
    groups = m.group(1).split(', ')
    m = re.search(r'\} PKEY\[PERF_N\] = \{(.*?)\n\};', src, re.S)
    keys = re.findall(r'\{"([^"]+)", [^,}]+, (PG_\w+)\}', m.group(1))
    m = re.search(r'PLINE\[PERF_N\] = \{(.*?)\n\};', src, re.S)
    lines = re.findall(r'"([^"]*)"', m.group(1))
    m = re.search(r'PG_COL\[PG_N\] = \{([^}]*)\}', src)
    pg_col = [colour(c.strip()) for c in m.group(1).split(',')]
    bainin = colour('K_BAININ')
    # pkey_col as this reads it: a black key a step of 1, a white key 3
    if 'return mix(PG_COL[PKEY[k].g], K_BAININ, PKEY[k].g == PG_BLACK ? n : n * 3);' not in src:
        sys.exit("gen_pflash: ui.c's pkey_col isn't the one this was written for")
    if len(keys) != perf_n or len(lines) != perf_n or len(pg_col) != len(groups):
        sys.exit('gen_pflash: %d keys, %d lines, %d colours for %d effects, %d groups'
                 % (len(keys), len(lines), len(pg_col), perf_n, len(groups)))
    names = [k[0] for k in keys]
    if len(set(names)) != perf_n:
        sys.exit('gen_pflash: two effects with one name')

    rows = []
    for k, (name, g) in enumerate(keys):
        n = sum(1 for j in range(k) if keys[j][1] == g)
        c = mix(pg_col[groups.index(g)], bainin, n if g == 'PG_BLACK' else n * 3)
        rows.append('    {"%s", "%s", 0x%04X},' % (name, lines[k], c))

    with open(out, 'w', encoding='utf-8') as f:
        f.write("// Made by hosttests/test7/gen_pflash.py from AISLING 1.3's ui.c (PKEY, PLINE, PG_COL, pkey_col): "
                "don't edit.\n")
        f.write("// PERFORM's effects in the keys' order: the name AISLING says when one is held, the line its "
                "flash shows under\n// it, and its colour (RGB565).\n")
        f.write('#ifndef VIZ_PFLASH_H_INCLUDED\n#define VIZ_PFLASH_H_INCLUDED\n#include <stdint.h>\n\n')
        f.write('#define PFLASH_N %d\n' % perf_n)
        f.write('static const struct {\n    const char *name, *line;\n    uint16_t col;\n} PFLASH[PFLASH_N] = {\n')
        f.write('\n'.join(rows) + '\n};\n\n#endif\n')


main()
