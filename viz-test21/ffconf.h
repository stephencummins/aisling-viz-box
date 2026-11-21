// (test 12) FatFs R0.15's configuration for the box's microSD card: FAT12/16/32 only, short (8.3) names, one volume,
// no clock. The settings live in fields from FatFs's own ffconf.h (the SDK's copy, lib/tinyusb/lib/fatfs/source),
// each set here for this box; see that file for what every one does.
//
// Short names only: long names need ffunicode.c (its code-page tables) and a long-name buffer, for nothing the box
// needs. A file the Mac named itself is still found by its short name (SETTINGS.TXT), and the box never deletes a
// file, so a long name the Mac gave one is never left behind on its own.
#define FFCONF_DEF 80286   // R0.15

#define FF_FS_READONLY  0
#define FF_FS_MINIMIZE  0
#define FF_USE_FIND     0
#define FF_USE_MKFS     0   // the box never formats the card: a computer does
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND   0
#define FF_USE_CHMOD    0
#define FF_USE_LABEL    0
#define FF_USE_FORWARD  0
#define FF_USE_STRFUNC  0
#define FF_PRINT_LLI    0
#define FF_PRINT_FLOAT  0
#define FF_STRF_ENCODE  0

#define FF_CODE_PAGE    437
#define FF_USE_LFN      1   // (test 20) the rows show the names as written; one working buffer, one core
#define FF_MAX_LFN      255
#define FF_LFN_UNICODE  0
#define FF_LFN_BUF      255
#define FF_SFN_BUF      12
#define FF_FS_RPATH     0

#define FF_VOLUMES         1
#define FF_STR_VOLUME_ID   0
#define FF_VOLUME_STRS     "SD"
#define FF_MULTI_PARTITION 0   // the first FAT partition the MBR lists, or a card with no partition table
#define FF_MIN_SS          512
#define FF_MAX_SS          512
#define FF_LBA64           0
#define FF_MIN_GPT         0x10000000
#define FF_USE_TRIM        0

#define FF_FS_TINY      1   // the files share the volume's one sector buffer: 512 bytes of RAM, not 512 a file
#define FF_FS_EXFAT     0   // a card of 64 GB or more comes as exFAT: format it FAT32 on a computer
#define FF_FS_NORTC     1   // no clock: every file is dated test 12's day
#define FF_NORTC_MON    11
#define FF_NORTC_MDAY   14
#define FF_NORTC_YEAR   2026
#define FF_FS_NOFSINFO  0
#define FF_FS_LOCK      0
#define FF_FS_REENTRANT 0   // core 0 alone uses the card
#define FF_FS_TIMEOUT   1000
