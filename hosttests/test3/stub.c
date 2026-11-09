// The SDK stand-ins: records what goes to the screen.
#include <stdio.h>
#include <string.h>
#include "sdk_stub.h"
spi_hw_t stub_spi_hw;
static struct spi_inst { int x; } spi1_inst;
spi_inst_t *spi1 = &spi1_inst;
enum { PIN_DC = 24, PIN_RST = 25, PIN_CS = 8, PIN_IRQ = 17 };
static bool pin[48];
// The record: each SPI word with DC, or a reset marker (word 0xFFFF0000).
#define MAXREC 4000000
uint32_t rec[MAXREC]; int nrec; int bad_cs; uint32_t hz_now = 20000000; int fmt_bits = 16;
void gpio_init(unsigned p) { (void)p; }
void gpio_set_dir(unsigned p, int o) { (void)p; (void)o; }
void gpio_put(unsigned p, bool v) {
    if (p == PIN_RST && pin[p] && !v && nrec < MAXREC) rec[nrec++] = 0xFFFF0000u; // a reset pulse starts
    pin[p] = v;
}
bool gpio_get(unsigned p) { return p == PIN_IRQ ? true : pin[p]; }
void gpio_pull_up(unsigned p) { (void)p; }
void gpio_set_function(unsigned p, int f) { (void)p; (void)f; }
void sleep_ms(uint32_t ms) { (void)ms; }
uint32_t spi_init(spi_inst_t *s, uint32_t hz) { (void)s; hz_now = hz; return hz; }
uint32_t spi_set_baudrate(spi_inst_t *s, uint32_t hz) { (void)s; hz_now = hz; return hz; }
void spi_set_format(spi_inst_t *s, int bits, int a, int b, int c) { (void)s; (void)a; (void)b; (void)c; fmt_bits = bits; }
int spi_write16_blocking(spi_inst_t *s, const uint16_t *src, size_t n) {
    (void)s;
    if (pin[PIN_CS] || fmt_bits != 16) bad_cs++; // the screen must be selected, at 16 bits
    for (size_t i = 0; i < n && nrec < MAXREC; i++) rec[nrec++] = ((uint32_t)pin[PIN_DC] << 16) | src[i];
    return (int)n;
}
int spi_write_read_blocking(spi_inst_t *s, const uint8_t *a, uint8_t *b, size_t n) { (void)s; (void)a; memset(b, 0, n); return (int)n; }
bool spi_is_readable(spi_inst_t *s) { (void)s; return false; }
unsigned spi_get_dreq(spi_inst_t *s, bool tx) { (void)s; (void)tx; return 0; }
void dma_channel_claim(unsigned ch) { (void)ch; }
dma_channel_config dma_channel_get_default_config(unsigned ch) { (void)ch; dma_channel_config c = {0}; return c; }
void channel_config_set_transfer_data_size(dma_channel_config *c, int s) { (void)c; (void)s; }
void channel_config_set_dreq(dma_channel_config *c, unsigned d) { (void)c; (void)d; }
void channel_config_set_read_increment(dma_channel_config *c, bool b) { (void)c; (void)b; }
void channel_config_set_write_increment(dma_channel_config *c, bool b) { (void)c; (void)b; }
void dma_channel_configure(unsigned ch, dma_channel_config *c, volatile void *d, const void *s, uint32_t n, bool go) {
    (void)ch; (void)c; (void)d; (void)s; (void)n; (void)go;
}
bool dma_channel_is_busy(unsigned ch) { (void)ch; return false; }
void dma_channel_wait_for_finish_blocking(unsigned ch) { (void)ch; }
