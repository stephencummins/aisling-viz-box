// The FatFs calls settings.c makes, for the checks that only read and write its text: there is no card here.
#include "ff.h"
FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) { (void)fp; (void)path; (void)mode; return FR_NOT_READY; }
FRESULT f_close(FIL *fp) { (void)fp; return FR_NOT_READY; }
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) { (void)fp; (void)buff; (void)btr; *br = 0; return FR_NOT_READY; }
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) { (void)fp; (void)buff; (void)btw; *bw = 0; return FR_NOT_READY; }
FRESULT f_truncate(FIL *fp) { (void)fp; return FR_NOT_READY; }
