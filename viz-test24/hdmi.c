// (test 24) The landscape out of the board's mini-HDMI socket. Test 23 sent a still test picture this way.
//
// The RP2350-PiZero's socket is wired to eight plain pins through resistors (its schematic: GPIO32/33 lane 2,
// 34/35 lane 1, 36/37 lane 0, 38/39 the clock; the + wire on the even pin). There is no video chip: the bits
// have to be made here. So, for 640 x 480 at 60 Hz:
//   - the system clock is 252 MHz, and a PIO state machine for each lane puts out one bit a tick: 252 Mbit/s,
//     ten bits to a pixel, 25.2 million pixels a second. A fourth state machine makes the pixel clock.
//   - the DMA feeds the state machines a line at a time from RAM. A second DMA channel per lane hands the first
//     its next line the moment one ends, so the stream never waits on a processor.
//   - once a line, an interrupt on core 0 says which line comes after the next one. It has a whole line (32
//     microseconds) to do it in.
//   - (test 24) the picture is 120 rows of four lines. Two rows' worth of lines are kept: while one is being sent
//     (four times), the same interrupt works the next row out of the landscape's paint cells into the other
//     (hdmipic.c), a part of it at each of three lines so that no one turn is long. If the work is ever late the
//     screen shows a row twice; the stream itself never depends on it.
//
// PIO 2 and DMA channels 4 to 9 (PIO-USB has PIO 0 and channel 0 and its own core, the screen channel 11).
#include "hdmi.h"
#include "tmds.h"
#include "hdmipic.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/timer.h"

#define HDMI_PIO pio2
#define PIN_D2 32
#define PIN_D1 34
#define PIN_D0 36
#define PIN_CK 38
#define SM_CK 3
#define CH_DATA 4   // 4, 5, 6: lanes 0, 1, 2
#define CH_NEXT 7   // 7, 8, 9: the channels that hand them their next line
#define HDMI_IRQN 2 // DMA_IRQ_2: nothing else here uses the DMA's interrupts

static const uint8_t LANE_PIN[3] = {PIN_D0, PIN_D1, PIN_D2};

// ---------------------------------------------------------------- the lines
// Nine lines' worth, 9000 bytes. The blanking: lane 0 (blue, and the syncs) outside and inside the vertical sync, and
// lanes 1 and 2 (green, red: the same for both). And two rows of the picture, a line for each lane: a row goes
// out of one while the next is worked out in the other.
static uint32_t l0_blank[TMDS_LINE_WORDS], l0_vsync[TMDS_LINE_WORDS], l12_blank[TMDS_LINE_WORDS];
static uint32_t pic[2][3][TMDS_LINE_WORDS];

static const uint8_t *volatile cells_r, *volatile cells_g, *volatile cells_b;
static volatile int shape;

static void build_lines(void) {
    static const uint8_t black[TMDS_H_ACTIVE];
    tmds_line(l0_blank, NULL, true, false);
    tmds_line(l0_vsync, NULL, true, true);
    tmds_line(l12_blank, NULL, false, false);
    // The picture's lines: their blanking, which stays, and black until a row is worked out.
    for (int set = 0; set < 2; set++)
        for (int lane = 0; lane < 3; lane++) tmds_line(pic[set][lane], black, lane == 0, false);
    pic_table();
}

static inline const uint32_t *__not_in_flash_func(line_for)(unsigned lane, unsigned line) {
    if (line < TMDS_V_ACTIVE) return pic[(line / PIC_ROW_LINES) & 1][lane];
    if (lane) return l12_blank;
    unsigned v = line - (TMDS_V_ACTIVE + TMDS_V_FRONT);
    return v < TMDS_V_SYNC ? l0_vsync : l0_blank;
}

void hdmi_picture(const uint8_t *r, const uint8_t *g, const uint8_t *b) {
    cells_r = r; cells_g = g; cells_b = b;
}

static const char *const SHAPES[PIC_SHAPES] = {"the whole 640 x 480", "a 16:9 band, 640 x 360"};
const char *hdmi_shape(void) { return SHAPES[shape]; }
const char *hdmi_next_shape(void) {
    shape = (shape + 1) % PIC_SHAPES;
    return hdmi_shape();
}

// ---------------------------------------------------------------- the stream
// Each lane's hand-over channel reads its two slots in turn (its read address wraps after 8 bytes). The interrupt
// at the end of a line fills the slot that line came from, with the line after the next: the hand-over channel is
// reading the other slot at that moment, so the two never meet.
static uint32_t slots[3][2] __attribute__((aligned(8)));
static volatile uint32_t v_line;     // the line going out now
static volatile uint32_t v_flip;     // the slot it came from
static volatile uint32_t n_frames, n_late;
static uint32_t last_us;
static volatile uint32_t work_longest, work_frame;   // the picture's work, microseconds: one turn, a frame
static uint32_t work_sum;
static uint32_t n_stalls;
static uint32_t flash_was, flash_now;

static void __not_in_flash_func(hdmi_irq)(void) {
    dma_hw->irq_ctrl[HDMI_IRQN].ints = 1u << CH_DATA;
    uint32_t now = timer_hw->timerawl;
    if (now - last_us > 48) n_late++;   // a line is 31.7 microseconds: more than one went by
    last_us = now;
    uint32_t line = v_line, flip = v_flip;
    uint32_t after = line + 2;
    if (after >= TMDS_V_TOTAL) after -= TMDS_V_TOTAL;
    for (unsigned lane = 0; lane < 3; lane++) slots[lane][flip] = (uint32_t)line_for(lane, after);
    if (++line == TMDS_V_TOTAL) {
        line = 0;
        n_frames++;
        work_frame = work_sum;
        work_sum = 0;
    }
    v_line = line;
    v_flip = flip ^ 1;

    // (test 24) The picture. `after` has just been handed over; the line going out now is the one before it. A row
    // is worked out while the three lines before its first are handed over: by then the row that last used its
    // buffer has gone (its last line ended as the first of those turns began), and the buffer isn't asked for until
    // the turn after the third.
    unsigned k;
    if (after + 3 >= TMDS_V_TOTAL) k = after + 3 - TMDS_V_TOTAL;              // the three lines before line 0
    else if (after + 3 < TMDS_V_ACTIVE) k = after + 3;
    else return;
    unsigned row = k / PIC_ROW_LINES, turn = k % PIC_ROW_LINES;
    if (turn == 3) return;
    uint32_t (*out)[TMDS_LINE_WORDS] = pic[row & 1];
    if (turn == 0) {
        pic_prep((int)row, shape, cells_r, cells_g, cells_b);
        pic_groups(0, 10, out[0], out[1], out[2]);
    } else if (turn == 1) pic_groups(10, 25, out[0], out[1], out[2]);
    else pic_groups(25, PIC_GROUPS, out[0], out[1], out[2]);
    uint32_t took = timer_hw->timerawl - now;
    if (took > work_longest) work_longest = took;
    work_sum += took;
}

// The serialiser, at the start of PIO 2's memory, because the bit sent is the address jumped to:
//   0: out pc, 1  side 0b10      a 0: + low, - high
//   1: out pc, 1  side 0b01      a 1: + high, - low
// and the clock, five ticks each way:
//   2: nop side 0b01 [4]
//   3: nop side 0b10 [4]
typedef struct { const char *name; enum gpio_drive_strength ma; enum gpio_slew_rate slew; } drive_t;
static const drive_t DRIVES[] = {
    {"2 mA, slow edges", GPIO_DRIVE_STRENGTH_2MA, GPIO_SLEW_RATE_SLOW},
    {"2 mA, fast edges", GPIO_DRIVE_STRENGTH_2MA, GPIO_SLEW_RATE_FAST},
    {"4 mA, slow edges", GPIO_DRIVE_STRENGTH_4MA, GPIO_SLEW_RATE_SLOW},
    {"4 mA, fast edges", GPIO_DRIVE_STRENGTH_4MA, GPIO_SLEW_RATE_FAST},
    {"8 mA, fast edges", GPIO_DRIVE_STRENGTH_8MA, GPIO_SLEW_RATE_FAST},
    {"12 mA, fast edges", GPIO_DRIVE_STRENGTH_12MA, GPIO_SLEW_RATE_FAST},
};
#define N_DRIVES (int)(sizeof DRIVES / sizeof DRIVES[0])
static int drive_i;

static void set_drive(void) {
    for (uint pin = PIN_D2; pin < PIN_CK + 2; pin++) {
        gpio_set_drive_strength(pin, DRIVES[drive_i].ma);
        gpio_set_slew_rate(pin, DRIVES[drive_i].slew);
    }
}

const char *hdmi_drive(void) { return DRIVES[drive_i].name; }

const char *hdmi_next_drive(void) {
    drive_i = (drive_i + 1) % N_DRIVES;
    set_drive();
    return hdmi_drive();
}

// ---------------------------------------------------------------- the clock, the voltage and the flash
// The flash's clock is the system clock divided; the board starts at 150 MHz with a divider the flash is known to
// work at. Before going to 252, divide further by the same ratio (rounded up), and move the sampling point the same
// way, so the flash sees a clock no faster than it had. In RAM with interrupts off: nothing may read the flash while
// its timing changes.
static void __no_inline_not_in_flash_func(flash_timing)(uint32_t t) {
    uint32_t s = save_and_disable_interrupts();
    qmi_hw->m[0].timing = t;
    restore_interrupts(s);
}

void hdmi_before_clock(void) {
    uint32_t t = qmi_hw->m[0].timing;
    flash_was = t;
    uint32_t now_mhz = clock_get_hz(clk_sys) / 1000000;
    if (now_mhz && now_mhz < HDMI_SYS_MHZ) {
        uint32_t div = t & QMI_M0_TIMING_CLKDIV_BITS;
        if (!div) div = 256;
        div = (div * HDMI_SYS_MHZ + now_mhz - 1) / now_mhz;
        if (div > 255) div = 255;
        uint32_t rx = (t & QMI_M0_TIMING_RXDELAY_BITS) >> QMI_M0_TIMING_RXDELAY_LSB;
        rx = (rx * HDMI_SYS_MHZ + now_mhz - 1) / now_mhz;
        if (rx > 7) rx = 7;
        t = (t & ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS)) | div | (rx << QMI_M0_TIMING_RXDELAY_LSB);
        flash_timing(t);
    }
    flash_now = t;
    // 252 MHz is past the RP2350's rated 150: it needs more than the 1.10 V the core starts with.
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    busy_wait_ms(2);
}

// ---------------------------------------------------------------- start
void hdmi_start(void) {
    build_lines();

    static uint16_t prog[4];
    prog[0] = (uint16_t)(pio_encode_out(pio_pc, 1) | pio_encode_sideset(2, 2));
    prog[1] = (uint16_t)(pio_encode_out(pio_pc, 1) | pio_encode_sideset(2, 1));
    prog[2] = (uint16_t)(pio_encode_nop() | pio_encode_sideset(2, 1) | pio_encode_delay(4));
    prog[3] = (uint16_t)(pio_encode_nop() | pio_encode_sideset(2, 2) | pio_encode_delay(4));
    static pio_program_t program = {.instructions = prog, .length = 4, .origin = 0};

    PIO pio = HDMI_PIO;
    pio_set_gpio_base(pio, 16);   // pins 32 to 39 are past a PIO's first 32
    pio_add_program_at_offset(pio, &program, 0);

    for (uint pin = PIN_D2; pin < PIN_CK + 2; pin++) pio_gpio_init(pio, pin);
    set_drive();

    for (uint sm = 0; sm < 4; sm++) {
        uint pin = sm == SM_CK ? PIN_CK : LANE_PIN[sm];
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_sideset(&c, 2, false, false);
        sm_config_set_sideset_pins(&c, pin);
        sm_config_set_out_shift(&c, true, true, 32);   // the first bit sent is bit 0; a new word every 32
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        sm_config_set_clkdiv_int_frac(&c, 1, 0);
        if (sm == SM_CK) sm_config_set_wrap(&c, 2, 3);
        else sm_config_set_wrap(&c, 0, 1);
        pio_sm_init(pio, sm, sm == SM_CK ? 2 : 0, &c);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 2, true);
    }

    // The DMA before the processors on the bus, so a lane's next word is never kept waiting.
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    v_line = 0;
    v_flip = 0;
    for (uint lane = 0; lane < 3; lane++) {
        uint data = CH_DATA + lane, next = CH_NEXT + lane;
        dma_channel_claim(data);
        dma_channel_claim(next);
        slots[lane][0] = (uint32_t)line_for(lane, 0);
        slots[lane][1] = (uint32_t)line_for(lane, 1);

        dma_channel_config d = dma_channel_get_default_config(data);
        channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
        channel_config_set_read_increment(&d, true);
        channel_config_set_write_increment(&d, false);
        channel_config_set_dreq(&d, pio_get_dreq(pio, lane, true));
        channel_config_set_chain_to(&d, next);
        channel_config_set_high_priority(&d, true);
        dma_channel_configure(data, &d, &pio->txf[lane], slots[lane] /* set by the hand-over */, TMDS_LINE_WORDS,
                              false);

        dma_channel_config n = dma_channel_get_default_config(next);
        channel_config_set_transfer_data_size(&n, DMA_SIZE_32);
        channel_config_set_read_increment(&n, true);
        channel_config_set_write_increment(&n, false);
        channel_config_set_ring(&n, false, 3);   // the read address wraps every 8 bytes: slot 0, slot 1, slot 0 ...
        channel_config_set_dreq(&n, DREQ_FORCE);
        channel_config_set_high_priority(&n, true);
        // (chain_to stays itself: no chain. Writing the data channel's read address through this register starts it.)
        dma_channel_configure(next, &n, &dma_hw->ch[data].al3_read_addr_trig, slots[lane], 1, false);
    }

    irq_set_exclusive_handler(DMA_IRQ_NUM(HDMI_IRQN), hdmi_irq);
    irq_set_priority(DMA_IRQ_NUM(HDMI_IRQN), 0);   // before everything else on this core
    dma_irqn_set_channel_enabled(HDMI_IRQN, CH_DATA, true);
    last_us = timer_hw->timerawl;
    irq_set_enabled(DMA_IRQ_NUM(HDMI_IRQN), true);

    // Line 0 into the state machines' queues, then all four started on the same tick.
    dma_start_channel_mask(7u << CH_NEXT);
    busy_wait_us(50);
    pio->fdebug = 0xFu << PIO_FDEBUG_TXSTALL_LSB;
    pio_enable_sm_mask_in_sync(pio, 0xF);
}

void hdmi_state(hdmi_state_t *s) {
    uint32_t f = HDMI_PIO->fdebug & (0x7u << PIO_FDEBUG_TXSTALL_LSB);
    if (f) {
        HDMI_PIO->fdebug = f;
        n_stalls++;
    }
    s->frames = n_frames;
    s->late = n_late;
    s->stalls = n_stalls;
    s->flash_was = flash_was;
    s->flash_now = flash_now;
    s->work_longest = work_longest;
    s->work_frame = work_frame;
    work_longest = 0;
}
