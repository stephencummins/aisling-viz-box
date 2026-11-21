// (test 12) The card as a USB drive. See msc.h.
//
// TinyUSB 0.18's MSC device (class/msc/msc_device.c) answers INQUIRY, MODE SENSE, REQUEST SENSE and the rest
// itself and asks these callbacks the rest. Its ways of saying "no card": TEST UNIT READY false (NOT READY, medium
// not present, unless a sense is set here first), a capacity of 0 blocks (the same), a read or write returning -1
// (the same, and the transfer stalled). All of them run inside tud_task(), on core 0, as everything that touches
// the card does.
#include <string.h>
#include "tusb.h"
#include "sd.h"
#include "msc.h"

static bool lent, written, attention;
static int came_back;
static uint32_t n_read, n_written;
static bool gone;            // while lent: no computer configuring the box, since gone_ms
static uint32_t gone_ms;

#define GONE_MS 1000u

void msc_lend(void) {
    lent = true;
    written = false;
    gone = false;
    attention = true;   // a card has "just gone in": the computer looks at it afresh
    n_read = n_written = 0;
}

void msc_take_back(void) {
    lent = false;
    written = false;
}

bool msc_lent(void) { return lent; }
bool msc_written(void) { return lent && written; }
uint32_t msc_blocks_read(void) { return n_read; }
uint32_t msc_blocks_written(void) { return n_written; }

int msc_came_back(void) {
    int w = came_back;
    came_back = MSC_BACK_NONE;
    return w;
}

static void back(int why) {
    if (!lent) return;
    lent = false;
    written = false;
    came_back = why;
}

void msc_tick(uint32_t now_ms) {
    if (!lent || tud_mounted()) {
        gone = false;
        return;
    }
    if (!gone) {
        gone = true;
        gone_ms = now_ms;
    } else if (now_ms - gone_ms >= GONE_MS) {
        back(MSC_BACK_UNPLUGGED);
    }
}

// ---------------------------------------------------------------- TinyUSB's callbacks

static void pad_copy(uint8_t *to, const char *s, size_t n) {
    size_t k = strlen(s);
    memcpy(to, s, k < n ? k : n);   // TinyUSB has filled the field with spaces
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    pad_copy(vendor_id, "stephen8", 8);
    pad_copy(product_id, "AISLING card", 16);
    pad_copy(product_rev, "12", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (!lent || !sd_ok()) return false;   // NOT READY, medium not present
    if (attention) {   // once: UNIT ATTENTION, "not ready to ready change, medium may have changed"
        attention = false;
        tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = lent ? sd_sectors() : 0;
    *block_size = 512;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

// LoEj with Start clear is the computer ejecting the card: it has written all it means to, and lets it go.
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun, (void)power_condition;
    if (load_eject && !start) back(MSC_BACK_EJECTED);
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    // CFG_TUD_MSC_EP_BUFSIZE is a whole number of sectors, so a piece always starts and ends on one
    if (!lent || offset || !bufsize || bufsize % 512) return -1;
    if (!sd_read(lba, buffer, bufsize / 512)) return -1;
    n_read += bufsize / 512;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (!lent || offset || !bufsize || bufsize % 512) return -1;
    written = true;   // even if it fails: the card may have part of it
    if (!sd_write(lba, buffer, bufsize / 512)) return -1;
    n_written += bufsize / 512;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer, (void)bufsize;
    if (scsi_cmd[0] == 0x35) {   // SYNCHRONIZE CACHE (10): every write here has reached the card already
        if (lent && sd_sync()) return 0;
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return -1;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);   // invalid command operation code
    return -1;
}

// The computer unconfigured the box (SET_CONFIGURATION 0): the card is the box's again. (A cable pulled out is
// msc_tick's: with no VBUS sense, TinyUSB sees only a bus reset, which doesn't call this.)
void tud_umount_cb(void) { back(MSC_BACK_UNPLUGGED); }
