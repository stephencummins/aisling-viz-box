// Built against TEST 16's settings.c and settings.h (which know no look; with -DWITH_LOOK, against test 17's):
//   old16 write        the SETTINGS.TXT test 16 writes, to stdout
//   old16 read FILE    what test 16 makes of a file (test 17's): what it got, and what it couldn't use
#include <stdio.h>
#include <string.h>
#include "settings.h"
static char text[SETTINGS_MAX + 1];
int main(int argc, char **argv) {
    settings_t s;
    memset(&s, 0, sizeof s);
    if (argc >= 2 && !strcmp(argv[1], "write")) {
        s.warmth = 2, s.spi = 1, s.palette = 1, s.ground = 2, s.pad[0] = 0, s.pad[1] = 1;
#ifdef WITH_LOOK   // built against test 17's instead: the same settings, and the light look
        s.look = LOOK_LIGHT;
#endif
        size_t n = settings_format(&s, "FALLING", text, sizeof text);
        fwrite(text, 1, n, stdout);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "read")) {
        FILE *f = fopen(argv[2], "rb");
        if (!f) return 2;
        size_t n = fread(text, 1, SETTINGS_MAX, f);
        fclose(f);
        settings_report_t r;
        settings_parse(text, n, &s, &r);
        printf("test 16 reads it: got=%x bad=%d unknown=%d first=%d [%s] warmth=%d spi=%d palette=%d ground=%d pad=%d,%d\n",
               r.got, r.bad, r.unknown, r.first_line, r.first, s.warmth, s.spi, s.palette, s.ground, s.pad[0], s.pad[1]);
        return 0;
    }
    return 2;
}
