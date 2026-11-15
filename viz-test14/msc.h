// (test 12) The card as a USB drive on the computer (USB mass storage, TinyUSB's MSC class), lent and taken back.
//
// The drive is always there, as an empty card reader is: until the box lends the card, the computer is told there
// is no card in it. Lent, the computer reads and writes the card's sectors as it likes, and the box keeps off it
// (FatFs unmounted first, by card.c). Ejected on the computer, the card comes back to the box by itself.
#ifndef VIZ_MSC_H_INCLUDED
#define VIZ_MSC_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>

void msc_lend(void);         // card.c has unmounted FatFs and the card is going
void msc_take_back(void);    // the computer is told the card has gone
bool msc_lent(void);

// Since the card was lent: the computer has written to it, and hasn't ejected it since. Taking it back then can
// lose what the computer still means to write.
bool msc_written(void);
uint32_t msc_blocks_read(void);
uint32_t msc_blocks_written(void);

enum { MSC_BACK_NONE, MSC_BACK_EJECTED, MSC_BACK_UNPLUGGED };
// Why the card came back on the computer's side (ejected, or the USB cable gone), once; MSC_BACK_NONE otherwise.
int msc_came_back(void);
// Called often (main loop, core 0), with the time. The board has no VBUS sense, so TinyUSB never says the cable
// went: a lent card whose computer has stopped configuring the box (tud_mounted() false) for a second comes back.
// (A computer resetting the port configures it again within that.)
void msc_tick(uint32_t now_ms);

#endif
