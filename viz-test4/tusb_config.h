// TinyUSB settings for test 4 (as test 3).
// Root port 0 is the RP2350's own USB (the "USB" socket): a CDC serial port to the Mac for the log.
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

// ---- host (PIO-USB, to the FM-1)
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1
#define CFG_TUH_DEVICE_MAX        1
#define CFG_TUH_HUB               0
#define CFG_TUH_ENUMERATION_BUFSIZE 1024 // an audio + MIDI composite can pass 256 bytes; TinyUSB gives up silently if it does not fit
// TinyUSB 0.18 has no MIDI host class, so the MIDI IN endpoint is opened and read by hand.
#define CFG_TUH_API_EDPT_XFER     1

#endif
