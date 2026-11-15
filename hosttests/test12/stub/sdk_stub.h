// Test 12's SDK stand-ins: test 4's (what lcd.c calls), and what sd.c calls besides. model.c has the functions.
#ifndef SDK_STUB12_H
#define SDK_STUB12_H
#include "../../test4/stub/sdk_stub.h"
enum { GPIO_FUNC_SIO = 5, GPIO_FUNC_NULL = 0x1f };
int spi_write_blocking(spi_inst_t *s, const uint8_t *src, size_t n);
int spi_read_blocking(spi_inst_t *s, uint8_t tx, uint8_t *dst, size_t n);
uint32_t time_us_32(void);
#endif
