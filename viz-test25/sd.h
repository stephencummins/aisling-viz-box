// (test 12) The board's microSD slot (J1), in SPI mode, on the screen's own SPI (spi1).
//
// The slot's pins are spi1's too (GP30 clock, GP31 command, GP40 data out of the card) but not the screen's (GP10,
// 11, 12), so each card call takes the bus: it waits for the screen's last push, moves spi1 onto the card's pins at
// 8 bits a word, does its work, and gives the bus back to the screen's pins at 16 bits and the screen's speed. The
// screen's pins carry nothing while the card has the bus, and the card's nothing while the screen has it. Core 0
// only: the screen and the card are both core 0's.
#ifndef VIZ_SD_H_INCLUDED
#define VIZ_SD_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>

#define SD_PIN_CLK  30   // SPI1 SCK
#define SD_PIN_CMD  31   // SPI1 TX: the card's CMD, its data in
#define SD_PIN_D0   40   // SPI1 RX: the card's D0, its data out
#define SD_PIN_CS   43   // the card's D3, a plain output
#define SD_SLOW_HZ  400000u      // while the card starts: SD's limit until it has
#define SD_FAST_HZ  12000000u    // after: 12 MHz is about 1.4 MB/s, more than USB full speed carries

// Looks for a card and starts it. True if one answered and is ready (sd_sectors() says how big).
bool sd_init(void);
bool sd_ok(void);                // a card started, and nothing has failed since
uint32_t sd_sectors(void);       // its size in 512-byte sectors (0: no card)
const char *sd_name(void);       // its own name for itself, five letters (CID), or ""
const char *sd_error(void);      // what went wrong last, for the log

// n sectors from lba. A failure (a card pulled out, say) marks the card gone until sd_init finds it again.
bool sd_read(uint32_t lba, uint8_t *buf, uint32_t n);
bool sd_write(uint32_t lba, const uint8_t *buf, uint32_t n);
bool sd_sync(void);              // true once the card has finished its last write

uint8_t sd_crc7(const uint8_t *d, int n);   // a command's CRC byte (for the host checks)

#endif
