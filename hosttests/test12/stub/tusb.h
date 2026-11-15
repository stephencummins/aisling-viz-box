// What msc.c takes from TinyUSB 0.18: the prototypes are msc_device.h's and usbd.h's, word for word, so a callback
// in msc.c with other arguments doesn't compile; the sense keys are msc.h's.
#ifndef TUSB_STUB12_H
#define TUSB_STUB12_H
#include <stdbool.h>
#include <stdint.h>
enum { SCSI_SENSE_NONE = 0x00, SCSI_SENSE_NOT_READY = 0x02, SCSI_SENSE_ILLEGAL_REQUEST = 0x05, SCSI_SENSE_UNIT_ATTENTION = 0x06 };
bool tud_mounted(void);
bool tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code, uint8_t add_sense_qualifier);
int32_t tud_msc_read10_cb (uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
int32_t tud_msc_write10_cb (uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]);
bool tud_msc_test_unit_ready_cb(uint8_t lun);
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size);
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize);
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject);
bool tud_msc_is_writable_cb(uint8_t lun);
void tud_umount_cb(void);
#endif
