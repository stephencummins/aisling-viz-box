// (test 19) The FM-1's side of the end-to-end check: fm1-aisling's own update code, as it is in that repository.
// Built three times by run.sh, each a firmware of its own (ota.c keeps its state in statics and its name in a macro):
//   -DROLE=old -DFELUCCA_ID="the installed package's product"      firmware/src/ota.c: step 1, as the running firmware
//   -DROLE=new -DFELUCCA_ID="the new package's product"            the same, as the firmware installed after
//   -DROLE=loader -DROLE_LOADER                                     ota.c and firmware/loader/ldr_core.c: step 2
// The flash, the clock and the USB are e2e_check.c's (the e2e_ functions).
#include <stdint.h>
#include <string.h>
#include "e2e.h"

#ifdef ROLE_LOADER
#define FELUCCA_ID "ota-FM-1_900"   // firmware/loader/loader.c's
#endif
#define FELUCCA_OTA_DRYRUN 0

static int ota_wire_send(const uint8_t *p, uint32_t n) { return e2e_wire_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return e2e_frame_get(p, n); }
static void ota_frame_done(void) { e2e_frame_done(); }
static uint32_t ota_now_ms(void) { return e2e_now(); }
static void ota_idle(void) { e2e_idle(); }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return e2e_fread(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { e2e_show(step, code); }
#ifdef ROLE_LOADER
static int ota_erase(uint32_t off) { (void)off; return -1; }   // as tests/ldr_test.c: the loader has no use for them
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#else
static int ota_erase(uint32_t off) { return e2e_app_erase(off); }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { return e2e_app_prog(off, p, n); }
static void ota_commit(const uint8_t *parm) { e2e_commit(parm); }
#endif
#include "ota.c"

#define CAT_(a, b) a##_##b
#define CAT(a, b) CAT_(a, b)
#define NAME(x) CAT(ROLE, x)

void NAME(service)(void) { ota_service(); }
#ifdef ROLE_LOADER
static int ldr_fread(uint32_t off, void *p, uint32_t n) { return e2e_fread(off, p, n); }
static int ldr_erase(uint32_t off) { return e2e_ldr_erase(off); }
static int ldr_prog(uint32_t off, const void *p, uint32_t n) { return e2e_ldr_prog(off, p, n); }
static void ldr_record_clear(void) { e2e_record_clear(); }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "ldr_core.c"
int NAME(session)(void) { return ldr_session(); }
int NAME(cleanup)(void) { return 0; }
// Where flash.bin is in a package's logical image (tests/ldr_test.c's flash_off), and a record as that test makes one.
uint32_t e2e_flash_off(const uint8_t *lg) {
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}
int e2e_record_ok(const uint8_t *r) {
    return ota_rd16(r) == ota_crc16(r + 2, 78, 0) && ota_rd16(r + 2) == 0x5A0D && ota_rd16(r + 6) == 0x5441;
}
#else
int NAME(session)(void) { return ota_session(); }
int NAME(cleanup)(void) { return ota_boot_cleanup(); }
#endif
