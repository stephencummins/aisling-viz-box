// (test 12) FatFs's disk layer (diskio.h) over the microSD card (sd.h): one drive, 512-byte sectors.
#include "ff.h"
#include "diskio.h"
#include "sd.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return sd_ok() ? 0 : STA_NOINIT;
}

// f_mount's first look, or after the card failed: started again if it isn't going.
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    if (!sd_ok()) sd_init();
    return sd_ok() ? 0 : STA_NOINIT | STA_NODISK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (!sd_ok()) return RES_NOTRDY;
    return sd_read(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (!sd_ok()) return RES_NOTRDY;
    return sd_write(sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv) return RES_PARERR;
    if (!sd_ok()) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: return sd_sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sd_sectors(); return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;   // erase block size not known
    default: return RES_PARERR;
    }
}
