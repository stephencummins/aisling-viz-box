// The board as test 12's checks see it: the pins, spi1, the clock, and a microSD card in the slot that speaks SPI
// mode byte by byte (SD Physical Layer Simplified Specification, chapter 7).
#ifndef MODEL12_H
#define MODEL12_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;
    int ver;                  // 1: doesn't know CMD8. 2: does
    bool hc;                  // addressed in blocks (SDHC/SDXC); otherwise in bytes
    bool mmc;                 // not an SD card: ACMD41 is an illegal command
    bool bad_voltage;         // CMD8 answered with no voltage accepted
    int csd;                  // the CSD it gives: 0 version 1, 1 version 2, 2 version 3 (SDUC)
    int bl;                   // a version 1 CSD's READ_BL_LEN (9, 10 or 11)
    uint32_t sectors;
    uint32_t init_ms;         // how long ACMD41 says "still starting" (UINT32_MAX: for ever)
    uint32_t busy_us;         // how long it's busy after a block written (UINT32_MAX: for ever)
    uint64_t pull_at;         // pulled out of the slot after this many bytes (0: stays in)
    char name[6];
} card_cfg_t;

void m_insert(card_cfg_t c);            // a card (or none) in the slot, just powered; what's stored on it stays
void m_wipe(void);                      // the store: all zeros
uint32_t m_load(const char *path);      // the store from a file; returns its sectors, rounded up to 1024
bool m_save(const char *path, uint32_t sectors);
uint8_t *m_sector(uint32_t lba);        // the store's sector, to read or change behind the card's back
void m_advance_ms(uint32_t ms);
uint32_t m_now_ms(void);
bool m_idle(void);                      // the screen has the bus as lcd.c left it, and the card is deselected
void m_no_pullup_is_error(bool on);

extern int m_errors;                    // things the code did that a real card or bus wouldn't stand
extern char m_err[200];                 // the first of them
extern uint64_t m_bytes;                // bytes clocked on the card's pins
extern uint32_t m_blocks_written, m_blocks_read, m_lcd_words;
extern bool m_mounted;                  // what tud_mounted() says

#endif
