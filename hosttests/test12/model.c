// See model.h. The SDK calls lcd.c and sd.c make, a clock that moves as bytes are clocked, and the card.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdk_stub.h"
#include "lcd.h"
#include "model.h"

spi_hw_t stub_spi_hw;
static struct spi_inst { int x; } spi1_inst;
spi_inst_t *spi1 = &spi1_inst;

enum { P_LCD_SCK = 10, P_LCD_MOSI = 11, P_LCD_MISO = 12, P_IRQ = 17, P_CLK = 30, P_CMD = 31, P_D0 = 40, P_CS = 43 };
static int func[48];
static bool dir_out[48], val[48], pulled[48];
static bool func_init;
static int fmt_bits = 16;
static uint32_t hz = 20000000;
static uint64_t ns = 1000000000ull;

int m_errors;
char m_err[200];
uint64_t m_bytes;
uint32_t m_blocks_written, m_blocks_read, m_lcd_words;
bool m_mounted;

static void bad(const char *what) {
    if (!m_errors++) snprintf(m_err, sizeof m_err, "%s", what);
}

// ---------------------------------------------------------------- the store

#define CHUNK 2048u   // sectors
static uint8_t **chunk;
static const uint32_t N_CHUNKS = 0x100000000ull / CHUNK;

uint8_t *m_sector(uint32_t lba) {
    if (!chunk) chunk = calloc(N_CHUNKS, sizeof *chunk);
    uint8_t **c = &chunk[lba / CHUNK];
    if (!*c) *c = calloc(CHUNK, 512);
    return *c + (size_t)(lba % CHUNK) * 512;
}

void m_wipe(void) {
    if (!chunk) return;
    for (uint32_t i = 0; i < N_CHUNKS; i++) free(chunk[i]), chunk[i] = NULL;
}

uint32_t m_load(const char *path) {
    m_wipe();
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "can't open %s\n", path);
        exit(2);
    }
    uint32_t n = 0;
    uint8_t b[512];
    size_t k;
    while ((k = fread(b, 1, 512, f)) > 0) {
        memset(b + k, 0, 512 - k);
        memcpy(m_sector(n++), b, 512);
    }
    fclose(f);
    return (n + 1023) / 1024 * 1024;
}

bool m_save(const char *path, uint32_t sectors) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    for (uint32_t i = 0; i < sectors; i++) fwrite(m_sector(i), 1, 512, f);
    return fclose(f) == 0;
}

// ---------------------------------------------------------------- the card

static card_cfg_t cfg;
static struct {
    unsigned pre_clocks;
    bool seen_cmd, dead, spi_mode, inited, app, got8, blocklen, stream, busy_after;
    uint8_t cmd[6];
    int ncmd;
    uint8_t out[600];
    int o_head, o_len;
    uint32_t stream_lba, wlba;
    int wphase, wn;   // 0 none, 1 waiting for a token, 2 data, 3 its CRC
    bool wmulti;
    uint8_t wbuf[512];
    uint64_t busy_until, init_at, seen;
} c;
static bool pullup_matters = true;

void m_no_pullup_is_error(bool on) { pullup_matters = on; }

void m_insert(card_cfg_t k) {
    cfg = k;
    memset(&c, 0, sizeof c);
}

// The specification's CRC7, a bit at a time (not sd.c's way of doing it).
static uint8_t crc7(const uint8_t *d, int n) {
    uint8_t r = 0;
    for (int i = 0; i < n; i++)
        for (int bit = 7; bit >= 0; bit--) {
            int in = ((d[i] >> bit) & 1) ^ ((r >> 6) & 1);
            r = (uint8_t)((r << 1) & 0x7F);
            if (in) r ^= 0x09;
        }
    return (uint8_t)((r << 1) | 1);
}

static void q(uint8_t b) {
    if (c.o_len < (int)sizeof c.out) c.out[c.o_len++] = b;
}

static void resp(uint8_t r1) {
    c.o_head = c.o_len = 0;
    q(0xFF);   // Ncr: a byte before the answer
    q(r1);
}

static void q_data(const uint8_t *d, int n) {
    q(0xFF), q(0xFF), q(0xFE);
    for (int i = 0; i < n; i++) q(d[i]);
    q(0x12), q(0x34);   // a CRC16 nobody checks
}

static void csd_bytes(uint8_t *b) {
    memset(b, 0, 16);
    if (cfg.csd == 2) {
        b[0] = 0x80;
    } else if (cfg.csd == 1) {
        uint32_t cs = cfg.sectors / 1024 - 1;
        if (cfg.sectors == 0xFFFFFFFFu) cs = 0x3FFFFF;   // the 2 TB card: 2^32 sectors
        b[0] = 0x40;
        b[7] = (cs >> 16) & 0x3F, b[8] = (uint8_t)(cs >> 8), b[9] = (uint8_t)cs;
    } else {
        int bl = cfg.bl ? cfg.bl : 9, mult = 7;
        uint32_t cs = (cfg.sectors >> (mult + 2 + bl - 9)) - 1;
        b[5] = (uint8_t)bl;
        b[6] = (cs >> 10) & 3, b[7] = (uint8_t)(cs >> 2), b[8] = (uint8_t)((cs & 3) << 6);
        b[9] = (uint8_t)(mult >> 1), b[10] = (uint8_t)((mult & 1) << 7);
    }
}

static bool lba_of(uint32_t arg, uint32_t *lba) {
    if (cfg.hc) {
        *lba = arg;
    } else {
        if (arg % 512) return false;
        if (!c.blocklen) bad("a byte-addressed card read or written before CMD16 set 512-byte blocks");
        *lba = arg / 512;
    }
    return *lba < cfg.sectors;
}

static void do_cmd(void) {
    uint8_t n = c.cmd[0] & 0x3F, idle = c.inited ? 0 : 1;
    uint32_t arg = ((uint32_t)c.cmd[1] << 24) | ((uint32_t)c.cmd[2] << 16) | ((uint32_t)c.cmd[3] << 8) | c.cmd[4];
    if (!c.seen_cmd) {
        if (c.pre_clocks < 74) {   // not listening yet: the command is lost, and the card still waits for its clocks
            bad("a command before the card had its 74 clocks");
            return;
        }
        c.seen_cmd = true;
    }
    if (c.cmd[5] != crc7(c.cmd, 5)) {
        bad("a command with a wrong CRC7");
        resp(0x08 | idle);
        return;
    }
    if (!c.spi_mode && n != 0) return;   // still in SD mode: it doesn't answer on D0
    bool app = c.app;
    c.app = false;
    uint32_t lba;
    uint8_t b[16];
    switch (n) {
    case 0:
        c.spi_mode = true, c.inited = false, c.init_at = 0, c.stream = false, c.wphase = 0;
        resp(0x01);
        break;
    case 8:
        if (cfg.ver < 2) {
            resp(0x05);
        } else {
            c.got8 = true;
            resp(0x01);
            q(0), q(0), q(cfg.bad_voltage ? 0 : (arg >> 8) & 0x0F), q((uint8_t)arg);
        }
        break;
    case 55:
        c.app = true;
        resp(idle);
        break;
    case 41:
        if (!app || cfg.mmc) {
            resp(0x04 | idle);
        } else if ((cfg.ver == 2 && !c.got8) || (cfg.hc && !(arg & (1u << 30))) || cfg.init_ms == UINT32_MAX) {
            resp(0x01);   // a high-capacity card waits for a host that says it can address one
        } else {
            if (!c.init_at) c.init_at = ns + (uint64_t)cfg.init_ms * 1000000u;
            if (ns >= c.init_at) c.inited = true;
            resp(c.inited ? 0x00 : 0x01);
        }
        break;
    case 58:
        resp(idle);
        q((uint8_t)((c.inited ? 0x80 : 0) | (c.inited && cfg.hc ? 0x40 : 0))), q(0xFF), q(0x80), q(0);
        break;
    case 16:
        if (arg == 512) c.blocklen = true;
        resp(idle);
        break;
    case 9:
    case 10:
        if (!c.inited) {
            resp(0x05);
            break;
        }
        if (n == 9) {
            csd_bytes(b);
        } else {
            memset(b, 0, sizeof b);
            b[0] = 0x03, b[1] = 'S', b[2] = 'D';
            memcpy(b + 3, cfg.name, 5);
        }
        resp(0x00);
        q_data(b, 16);
        break;
    case 17:
    case 18:
        if (!c.inited) {
            resp(0x05);
        } else if (!lba_of(arg, &lba)) {
            resp(0x40);
        } else {
            resp(0x00);
            if (n == 17) q_data(m_sector(lba), 512), m_blocks_read++;
            else c.stream = true, c.stream_lba = lba;
        }
        break;
    case 12:
        c.o_head = c.o_len = 0;
        q(0x5A);   // the stuff byte: part of the block it was sending, and it can look like an answer
        q(0xFF);
        q(c.stream ? 0x00 : 0x04);
        c.stream = false;
        c.busy_after = true;
        break;
    case 24:
    case 25:
        if (!c.inited) {
            resp(0x05);
        } else if (!lba_of(arg, &lba)) {
            resp(0x40);
        } else {
            resp(0x00);
            c.wphase = 1, c.wmulti = n == 25, c.wlba = lba, c.wn = 0;
        }
        break;
    default:
        resp(0x04 | idle);
    }
}

static void set_busy(void) {
    c.busy_until = cfg.busy_us == UINT32_MAX ? UINT64_MAX : ns + (uint64_t)cfg.busy_us * 1000u;
}

static uint8_t card_out(void) {
    if (c.o_head == c.o_len && c.stream) {   // the next block of a CMD18
        c.o_head = c.o_len = 0;
        if (c.stream_lba < cfg.sectors) q_data(m_sector(c.stream_lba++), 512), m_blocks_read++;
        else q(0xFF);
    }
    if (c.o_head < c.o_len) {
        uint8_t b = c.out[c.o_head++];
        if (c.o_head == c.o_len && c.busy_after) {
            c.busy_after = false;
            set_busy();
        }
        return b;
    }
    return ns < c.busy_until ? 0x00 : 0xFF;
}

static void card_in(uint8_t in) {
    if (c.wphase == 2) {
        c.wbuf[c.wn++] = in;
        if (c.wn == 512) c.wphase = 3, c.wn = 0;
    } else if (c.wphase == 3) {
        if (++c.wn < 2) return;
        if (c.wlba < cfg.sectors) memcpy(m_sector(c.wlba), c.wbuf, 512);
        c.wlba++, m_blocks_written++;
        c.o_head = c.o_len = 0;
        q(0xE5);   // data accepted
        c.busy_after = true;
        c.wphase = c.wmulti ? 1 : 0;
        c.wn = 0;
    } else if (c.ncmd) {
        c.cmd[c.ncmd++] = in;
        if (c.ncmd == 6) c.ncmd = 0, do_cmd();
    } else if (c.wphase == 1 && in != 0xFF && (in & 0xC0) != 0x40) {
        if (ns < c.busy_until || c.o_head < c.o_len) bad("a data token sent while the card was busy");
        if (in == (c.wmulti ? 0xFC : 0xFE)) c.wphase = 2, c.wn = 0;
        else if (in == 0xFD && c.wmulti) c.wphase = 0, set_busy();
        else bad("a wrong data token");
    } else if ((in & 0xC0) == 0x40) {
        c.wphase = 0;
        c.ncmd = 1, c.cmd[0] = in;
        if (!c.stream) c.o_head = c.o_len = 0;
    } else if (in != 0xFF && !c.stream) {
        bad("a stray byte to the card");
    }
}

static bool sd_pins(void) { return func[P_CLK] == GPIO_FUNC_SPI && func[P_CMD] == GPIO_FUNC_SPI && func[P_D0] == GPIO_FUNC_SPI; }
static bool sd_any(void) { return func[P_CLK] == GPIO_FUNC_SPI || func[P_CMD] == GPIO_FUNC_SPI || func[P_D0] == GPIO_FUNC_SPI; }
static bool lcd_pins(void) { return func[P_LCD_SCK] == GPIO_FUNC_SPI && func[P_LCD_MOSI] == GPIO_FUNC_SPI && func[P_LCD_MISO] == GPIO_FUNC_SPI; }
static bool lcd_any(void) { return func[P_LCD_SCK] == GPIO_FUNC_SPI || func[P_LCD_MOSI] == GPIO_FUNC_SPI || func[P_LCD_MISO] == GPIO_FUNC_SPI; }
static bool cs_high(void) { return !dir_out[P_CS] || val[P_CS]; }

// One byte on spi1, wherever its pins are.
static uint8_t byte(uint8_t in) {
    ns += 8000000000ull / (hz ? hz : 1);
    if (!sd_any()) return 0;   // the screen's side (its touch chip): nothing these checks read
    if (!sd_pins()) bad("only some of the card's pins on the SPI");
    if (lcd_any()) bad("the screen's pins on the SPI while the card's are");
    if (fmt_bits != 8) bad("the card clocked at 16 bits a word");
    m_bytes++;
    uint8_t flt = pulled[P_D0] ? 0xFF : 0x00;   // D0 with nothing driving it
    if (!cfg.present) return flt;
    if (cfg.pull_at && ++c.seen > cfg.pull_at) {
        cfg.present = false;
        return flt;
    }
    if (!c.inited && hz > 400000) bad("more than 400 kHz before the card had started");
    if (cs_high()) {
        if (!c.seen_cmd) c.pre_clocks += 8;
        if (c.wphase >= 2) bad("the card deselected in the middle of a block written");
        if (c.stream) bad("the card deselected in a read of several blocks (no CMD12: it would go on sending)");
        c.o_head = c.o_len = 0, c.stream = false, c.ncmd = 0, c.wphase = 0, c.busy_after = false;
        return flt;
    }
    if (c.dead) return flt;
    uint8_t out = card_out();
    card_in(in);
    return out;
}

// ---------------------------------------------------------------- the SDK

static void funcs(void) {
    if (func_init) return;
    func_init = true;
    for (int i = 0; i < 48; i++) func[i] = GPIO_FUNC_NULL;
}

void gpio_init(unsigned p) { funcs(); func[p] = GPIO_FUNC_SIO, dir_out[p] = false, val[p] = false; }
void gpio_set_dir(unsigned p, int o) { dir_out[p] = o; }
void gpio_put(unsigned p, bool v) { val[p] = v; }
bool gpio_get(unsigned p) { return p == P_IRQ ? true : val[p]; }
void gpio_pull_up(unsigned p) { pulled[p] = true; }
void gpio_set_function(unsigned p, int f) { funcs(); func[p] = f; }
void sleep_ms(uint32_t ms) { ns += (uint64_t)ms * 1000000u; }
uint32_t time_us_32(void) { return (uint32_t)(ns / 1000); }
uint32_t spi_init(spi_inst_t *s, uint32_t h) { (void)s; hz = h; return h; }
uint32_t spi_set_baudrate(spi_inst_t *s, uint32_t h) { (void)s; hz = h; return h; }
void spi_set_format(spi_inst_t *s, int bits, int a, int b, int d) { (void)s, (void)a, (void)b, (void)d; fmt_bits = bits; }
int spi_write16_blocking(spi_inst_t *s, const uint16_t *src, size_t n) {
    (void)s, (void)src;
    if (sd_any()) bad("the screen written to while the card's pins had the SPI");
    m_lcd_words += (uint32_t)n;
    return (int)n;
}
int spi_write_read_blocking(spi_inst_t *s, const uint8_t *a, uint8_t *b, size_t n) {
    (void)s;
    for (size_t i = 0; i < n; i++) b[i] = byte(a[i]);
    return (int)n;
}
int spi_write_blocking(spi_inst_t *s, const uint8_t *a, size_t n) {
    (void)s;
    for (size_t i = 0; i < n; i++) byte(a[i]);
    return (int)n;
}
int spi_read_blocking(spi_inst_t *s, uint8_t tx, uint8_t *b, size_t n) {
    (void)s;
    for (size_t i = 0; i < n; i++) b[i] = byte(tx);
    return (int)n;
}
bool spi_is_readable(spi_inst_t *s) { (void)s; return false; }
unsigned spi_get_dreq(spi_inst_t *s, bool tx) { (void)s, (void)tx; return 0; }
void dma_channel_claim(unsigned ch) { (void)ch; }
dma_channel_config dma_channel_get_default_config(unsigned ch) { (void)ch; dma_channel_config k = {0}; return k; }
void channel_config_set_transfer_data_size(dma_channel_config *k, int s) { (void)k, (void)s; }
void channel_config_set_dreq(dma_channel_config *k, unsigned d) { (void)k, (void)d; }
void channel_config_set_read_increment(dma_channel_config *k, bool b) { (void)k, (void)b; }
void channel_config_set_write_increment(dma_channel_config *k, bool b) { (void)k, (void)b; }
void dma_channel_configure(unsigned ch, dma_channel_config *k, volatile void *d, const void *s, uint32_t n, bool go) {
    (void)ch, (void)k, (void)d, (void)s, (void)n, (void)go;
}
bool dma_channel_is_busy(unsigned ch) { (void)ch; return false; }
void dma_channel_wait_for_finish_blocking(unsigned ch) { (void)ch; }

bool tud_mounted(void) { return m_mounted; }

void m_advance_ms(uint32_t ms) { ns += (uint64_t)ms * 1000000u; }
uint32_t m_now_ms(void) { return (uint32_t)(ns / 1000000u); }

bool m_idle(void) {
    return lcd_pins() && !sd_any() && fmt_bits == 16 && hz == lcd_hz() && cs_high() && dir_out[P_CS] &&
           (!pullup_matters || (pulled[P_D0] && pulled[P_CMD]));
}
