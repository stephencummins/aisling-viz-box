// (test 19) Between e2e_check.c and the three builds of e2e_dev.c.
#ifndef E2E_H
#define E2E_H
#include <stdint.h>
int e2e_wire_send(const uint8_t *p, uint32_t n);
int e2e_frame_get(const uint8_t **p, uint32_t *n);
void e2e_frame_done(void);
uint32_t e2e_now(void);
void e2e_idle(void);
int e2e_fread(uint32_t off, void *p, uint32_t n);
void e2e_show(uint32_t step, int32_t code);
int e2e_app_erase(uint32_t off);
int e2e_app_prog(uint32_t off, const void *p, uint32_t n);
void e2e_commit(const uint8_t *parm);
int e2e_ldr_erase(uint32_t off);
int e2e_ldr_prog(uint32_t off, const void *p, uint32_t n);
void e2e_record_clear(void);
uint32_t e2e_flash_off(const uint8_t *logical);
int e2e_record_ok(const uint8_t *record);

void old_service(void); int old_session(void); int old_cleanup(void);
void new_service(void); int new_session(void); int new_cleanup(void);
void loader_service(void); int loader_session(void); int loader_cleanup(void);
#endif
