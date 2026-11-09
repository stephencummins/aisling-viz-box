// Host stand-ins for the few pico-sdk calls lcd.c makes. Every SPI word is recorded with the DC and CS pins.
#ifndef SDK_STUB_H
#define SDK_STUB_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { volatile uint32_t dr, sr, icr; } spi_hw_t;
typedef struct spi_inst spi_inst_t;
extern spi_inst_t *spi1;
extern spi_hw_t stub_spi_hw;
#define spi_get_hw(s) (&stub_spi_hw)
#define SPI_SSPSR_BSY_BITS 0x10u
#define SPI_SSPICR_RORIC_BITS 1u
enum { SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST };
enum { GPIO_IN = 0, GPIO_OUT = 1 };
enum { GPIO_FUNC_SPI = 1 };
enum { DMA_SIZE_16 = 1 };
typedef struct { int x; } dma_channel_config;
void gpio_init(unsigned p); void gpio_set_dir(unsigned p, int out); void gpio_put(unsigned p, bool v);
bool gpio_get(unsigned p); void gpio_pull_up(unsigned p); void gpio_set_function(unsigned p, int f);
void sleep_ms(uint32_t ms);
uint32_t spi_init(spi_inst_t *s, uint32_t hz); uint32_t spi_set_baudrate(spi_inst_t *s, uint32_t hz);
void spi_set_format(spi_inst_t *s, int bits, int a, int b, int c);
int spi_write16_blocking(spi_inst_t *s, const uint16_t *src, size_t n);
int spi_write_read_blocking(spi_inst_t *s, const uint8_t *src, uint8_t *dst, size_t n);
bool spi_is_readable(spi_inst_t *s); unsigned spi_get_dreq(spi_inst_t *s, bool tx);
void dma_channel_claim(unsigned ch); dma_channel_config dma_channel_get_default_config(unsigned ch);
void channel_config_set_transfer_data_size(dma_channel_config *c, int s);
void channel_config_set_dreq(dma_channel_config *c, unsigned d);
void channel_config_set_read_increment(dma_channel_config *c, bool b);
void channel_config_set_write_increment(dma_channel_config *c, bool b);
void dma_channel_configure(unsigned ch, dma_channel_config *c, volatile void *dst, const void *src, uint32_t n, bool go);
bool dma_channel_is_busy(unsigned ch); void dma_channel_wait_for_finish_blocking(unsigned ch);
static inline void tight_loop_contents(void) {}
#endif
