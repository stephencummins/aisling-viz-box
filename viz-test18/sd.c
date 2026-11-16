// (test 12) The microSD card in SPI mode. See sd.h.
//
// From the SD Association's Physical Layer Simplified Specification (version 9), chapter 7, SPI mode: CMD0 with the
// card selected puts it in SPI mode; CMD8 says the host's voltage (a version 1 card doesn't know it); ACMD41 starts
// the card, saying the host takes block addressing (HCS), until it answers "not idle"; CMD58 reads its OCR, whose
// CCS bit says it is addressed in blocks (SDHC/SDXC) rather than bytes; CMD9 its CSD, for its size. Reads are CMD17
// (one block) and CMD18 (several, ended by CMD12); writes CMD24 and CMD25, ended by a stop token. A data block is a
// start token, 512 bytes and a CRC the card doesn't check in SPI mode. Commands carry their CRC7 anyway.
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "lcd.h"
#include "sd.h"

#define SD_SPI spi1
#define LCD_PIN_SCK  10   // lcd.c's PIN_SCK, PIN_MOSI and PIN_MISO
#define LCD_PIN_MOSI 11
#define LCD_PIN_MISO 12

#define ACMD(n) (0x80 | (n))
#define TOKEN_START  0xFE   // a block of data: read, or one block written
#define TOKEN_MULTI  0xFC   // each block of a CMD25
#define TOKEN_STOP   0xFD   // the end of a CMD25

static bool ok, block_addr;
static uint32_t sectors;
static char name[6];
static const char *err = "not looked for yet";

// ---------------------------------------------------------------- the bus

static void bus_take(uint32_t hz) {
    lcd_push_wait();   // the screen's last push out, the SPI idle and the screen deselected
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_NULL);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_NULL);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_NULL);
    gpio_set_function(SD_PIN_CLK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_CMD, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_D0, GPIO_FUNC_SPI);
    spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_set_baudrate(SD_SPI, hz);
}

static uint8_t xchg(uint8_t b) {
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &b, &r, 1);
    return r;
}

static void bus_give(void) {
    gpio_put(SD_PIN_CS, 1);
    xchg(0xFF);   // a byte with the card deselected: some cards only let go of D0 on a clock
    gpio_set_function(SD_PIN_CLK, GPIO_FUNC_NULL);
    gpio_set_function(SD_PIN_CMD, GPIO_FUNC_NULL);
    gpio_set_function(SD_PIN_D0, GPIO_FUNC_NULL);
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SPI);
    spi_set_baudrate(SD_SPI, lcd_hz());
    spi_set_format(SD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

// ---------------------------------------------------------------- commands

// CRC7 (x^7 + x^3 + 1) of a command's first five bytes, as its sixth: the CRC in the top seven bits, then the end bit.
uint8_t sd_crc7(const uint8_t *d, int n) {
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        uint8_t b = d[i];
        for (int k = 0; k < 8; k++) {
            crc = (uint8_t)(crc << 1);
            if ((b ^ crc) & 0x80) crc ^= 0x09;
            b = (uint8_t)(b << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1);
}

static bool wait_ready(uint32_t ms) {
    uint32_t t0 = time_us_32();
    do {
        if (xchg(0xFF) == 0xFF) return true;   // a busy card holds D0 low
    } while (time_us_32() - t0 < ms * 1000u);
    return false;
}

static void deselect(void) {
    gpio_put(SD_PIN_CS, 1);
    xchg(0xFF);
}

static bool select_card(void) {
    gpio_put(SD_PIN_CS, 0);
    xchg(0xFF);
    if (wait_ready(500)) return true;
    deselect();
    return false;
}

// Sends a command and returns its first answer byte, R1 (0xFF: no answer). The card stays selected, for whatever
// follows: the rest of the answer, or data.
static uint8_t cmd(uint8_t c, uint32_t arg) {
    if (c & 0x80) {   // an application command: CMD55 first
        c &= 0x7F;
        uint8_t r = cmd(55, 0);
        if (r > 1) return r;
    }
    if (c != 12) {   // CMD12 goes in the middle of a read, while the card is sending
        deselect();
        if (!select_card()) return 0xFF;
    }
    uint8_t p[6] = {(uint8_t)(0x40 | c), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8),
                    (uint8_t)arg, 0};
    p[5] = sd_crc7(p, 5);
    spi_write_blocking(SD_SPI, p, 6);
    if (c == 12) xchg(0xFF);   // the byte of the block the card was sending when CMD12 stopped it
    uint8_t r;
    int n = 10;
    do r = xchg(0xFF);
    while ((r & 0x80) && --n);
    return r;
}

// A block from the card: its start token (within 200 ms), n bytes, and the CRC, unchecked.
static bool data_in(uint8_t *buf, size_t n) {
    uint32_t t0 = time_us_32();
    uint8_t t;
    do t = xchg(0xFF);
    while (t == 0xFF && time_us_32() - t0 < 200000u);
    if (t != TOKEN_START) return false;
    spi_read_blocking(SD_SPI, 0xFF, buf, n);
    uint8_t crc[2];
    spi_read_blocking(SD_SPI, 0xFF, crc, 2);
    return true;
}

// A block to the card (or, with TOKEN_STOP, the end of a CMD25). True if the card took it.
static bool data_out(const uint8_t *buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    xchg(token);
    if (token == TOKEN_STOP) return true;
    spi_write_blocking(SD_SPI, buf, 512);
    xchg(0xFF);   // the CRC, which SPI mode doesn't check
    xchg(0xFF);
    return (xchg(0xFF) & 0x1F) == 0x05;   // data response: accepted
}

// ---------------------------------------------------------------- starting the card

// Its size in sectors from its CSD (version 1: standard capacity; 2: SDHC/SDXC). 0 if it isn't one of those.
static uint32_t csd_sectors(const uint8_t *c) {
    if (c[0] >> 6 == 1) {
        uint64_t size = ((uint64_t)(c[7] & 0x3F) << 16) | ((uint32_t)c[8] << 8) | c[9];
        size = (size + 1) << 10;   // C_SIZE + 1 units of 512 KB
        return size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)size;
    }
    if (c[0] >> 6 == 0) {
        unsigned bl = c[5] & 0x0F;   // READ_BL_LEN: 9, 10 or 11
        uint32_t cs = ((uint32_t)(c[6] & 0x03) << 10) | ((uint32_t)c[7] << 2) | (c[8] >> 6);
        unsigned mult = (unsigned)((c[9] & 0x03) << 1) | (c[10] >> 7);
        if (bl < 9 || bl > 11) return 0;
        return (cs + 1) << (mult + 2 + bl - 9);
    }
    return 0;
}

static bool start(void) {
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && r != 0x01; i++) r = cmd(0, 0);
    if (r != 0x01) {
        err = r == 0xFF ? "no card" : "no answer to CMD0";
        return false;
    }
    uint8_t b[16];
    bool v2 = false;
    r = cmd(8, 0x1AA);   // 2.7-3.6 V, check pattern AA
    if (r == 0x01) {
        spi_read_blocking(SD_SPI, 0xFF, b, 4);
        if ((b[2] & 0x0F) != 0x01 || b[3] != 0xAA) {
            err = "the card can't take 3.3 V";
            return false;
        }
        v2 = true;
    } else if (!(r & 0x04)) {   // not "illegal command", which is what a version 1 card says
        err = "an odd answer to CMD8";
        return false;
    }
    uint32_t t0 = time_us_32();
    do r = cmd(ACMD(41), v2 ? 1u << 30 : 0);
    while (r == 0x01 && time_us_32() - t0 < 1000000u);
    if (r != 0x00) {
        err = r == 0x01 ? "the card didn't finish starting" : "not an SD card";
        return false;
    }
    block_addr = false;
    if (v2) {
        if (cmd(58, 0) != 0x00) {
            err = "no answer to CMD58";
            return false;
        }
        spi_read_blocking(SD_SPI, 0xFF, b, 4);
        block_addr = b[0] & 0x40;   // CCS
    }
    if (!block_addr && cmd(16, 512) != 0x00) {
        err = "no answer to CMD16";
        return false;
    }
    if (cmd(9, 0) != 0x00 || !data_in(b, 16)) {
        err = "couldn't read the card's size (CSD)";
        return false;
    }
    sectors = csd_sectors(b);
    if (!sectors) {
        err = "a card of a size this doesn't know (CSD)";
        return false;
    }
    if (cmd(10, 0) == 0x00 && data_in(b, 16)) {   // CID: bytes 3 to 7 are its product name
        for (int i = 0; i < 5; i++) name[i] = b[3 + i] >= 0x20 && b[3 + i] < 0x7F ? (char)b[3 + i] : '?';
        name[5] = 0;
    }
    return true;
}

bool sd_init(void) {
    ok = false;
    sectors = 0;
    name[0] = 0;
    gpio_init(SD_PIN_CS);
    gpio_put(SD_PIN_CS, 1);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    // With the slot empty D0 floats, and a float read low looks like a busy card: half a second a command. Pulled
    // up, an empty slot answers 0xFF at once. (The SD spec wants CMD and D0 pulled up anyway: these are 50 kohm.)
    gpio_pull_up(SD_PIN_D0);
    gpio_pull_up(SD_PIN_CMD);
    bus_take(SD_SLOW_HZ);
    for (int i = 0; i < 10; i++) xchg(0xFF);   // 80 clocks deselected: the card's power-up needs 74
    ok = start();
    if (ok) err = "none";
    else sectors = 0;
    bus_give();
    return ok;
}

bool sd_ok(void) { return ok; }
uint32_t sd_sectors(void) { return ok ? sectors : 0; }
const char *sd_name(void) { return name; }
const char *sd_error(void) { return err; }

// ---------------------------------------------------------------- reading and writing

static bool in_range(uint32_t lba, uint32_t n) { return ok && n > 0 && lba < sectors && n <= sectors - lba; }

bool sd_read(uint32_t lba, uint8_t *buf, uint32_t n) {
    if (!in_range(lba, n)) return false;
    bus_take(SD_FAST_HZ);
    uint32_t a = block_addr ? lba : lba * 512u;
    bool good;
    if (n == 1) {
        good = cmd(17, a) == 0x00 && data_in(buf, 512);
    } else {
        good = cmd(18, a) == 0x00;
        if (good) {
            for (; good && n; n--, buf += 512) good = data_in(buf, 512);
            if (cmd(12, 0) != 0x00) good = false;
        }
    }
    if (!good) {
        ok = false;
        err = "a read failed";
    }
    bus_give();
    return good;
}

bool sd_write(uint32_t lba, const uint8_t *buf, uint32_t n) {
    if (!in_range(lba, n)) return false;
    bus_take(SD_FAST_HZ);
    uint32_t a = block_addr ? lba : lba * 512u;
    bool good;
    if (n == 1) {
        good = cmd(24, a) == 0x00 && data_out(buf, TOKEN_START);
    } else {
        good = cmd(25, a) == 0x00;
        if (good) {
            for (; good && n; n--, buf += 512) good = data_out(buf, TOKEN_MULTI);
            if (!data_out(NULL, TOKEN_STOP)) good = false;
        }
    }
    if (!good) {
        ok = false;
        err = "a write failed";
    }
    bus_give();
    return good;
}

bool sd_sync(void) {
    if (!ok) return false;
    bus_take(SD_FAST_HZ);
    bool good = select_card();   // waits while the card is busy writing
    bus_give();
    if (!good) {
        ok = false;
        err = "the card stayed busy";
    }
    return good;
}
