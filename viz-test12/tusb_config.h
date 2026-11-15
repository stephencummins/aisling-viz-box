// TinyUSB settings for test 12 (test 7's, and the card as a drive).
// Root port 0 is the RP2350's own USB (the "USB" socket): a CDC serial port to the Mac for the log, and (test 12) a
// USB mass-storage drive, the microSD card when the box lends it (msc.c).
// Root port 1 is PIO-USB (the "PIO-USB" socket): the host the FM-1 plugs into.
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_OS               OPT_OS_PICO

// ---- device (native USB, to the Mac)
#define CFG_TUD_ENABLED           1
#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    64
#define CFG_TUD_CDC_TX_BUFSIZE    2048
#define CFG_TUD_CDC_EP_BUFSIZE    64
#define CFG_TUD_MSC               1
// The most of a read or write handed to msc.c at once: eight sectors, one CMD18 or CMD25. A whole number of
// sectors, so a piece never starts part-way into one (msc.c relies on it).
#define CFG_TUD_MSC_EP_BUFSIZE    4096

// ---- host (PIO-USB, to the FM-1)
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1
#define CFG_TUH_DEVICE_MAX        1
#define CFG_TUH_HUB               0
#define CFG_TUH_ENUMERATION_BUFSIZE 1024 // an audio + MIDI composite can pass 256 bytes; TinyUSB gives up silently if it does not fit
// TinyUSB 0.18 has no MIDI host class, so the MIDI IN endpoint is opened and read by hand.
#define CFG_TUH_API_EDPT_XFER     1

#endif
