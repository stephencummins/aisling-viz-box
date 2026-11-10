// USB descriptors for test 7's own side: one CDC serial port, seen on the Mac as /dev/cu.usbmodem*.
#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"

// The pico-sdk's own IDs for a CDC serial port on an RP2350 (what pico_stdio_usb uses).
#define VIZ_VID 0x2E8A
#define VIZ_PID 0x0009

enum { ITF_CDC_COMM = 0, ITF_CDC_DATA, ITF_COUNT };
enum { STR_LANG = 0, STR_MAKER, STR_PRODUCT, STR_SERIAL, STR_CDC };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82

static const tusb_desc_device_t device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // CDC is a composite (comm + data), so use the interface association class.
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = VIZ_VID,
    .idProduct          = VIZ_PID,
    .bcdDevice          = 0x0001,
    .iManufacturer      = STR_MAKER,
    .iProduct           = STR_PRODUCT,
    .iSerialNumber      = STR_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_desc;
}

#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return config_desc;
}

static uint16_t str_buf[33];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *s;
    switch (index) {
    case STR_LANG:
        str_buf[1] = 0x0409; // English
        str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        return str_buf;
    case STR_MAKER:   s = "stephen8n"; break;
    case STR_PRODUCT: s = "AISLING viz test 7"; break;
    case STR_SERIAL:
        pico_get_unique_board_id_string(serial, sizeof serial);
        s = serial;
        break;
    case STR_CDC:     s = "AISLING viz log"; break;
    default:          return NULL;
    }
    size_t n = strlen(s);
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) str_buf[1 + i] = (uint8_t)s[i];
    str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return str_buf;
}
