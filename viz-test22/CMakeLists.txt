# AISLING visualiser, test 15: test 14 with the picture turned half a turn (VIZ_FLIP). Test 14's line:
# AISLING visualiser, test 14: tests 12 (the microSD card) and 13 (any firmware on the FM-1) in one build.
# Build: see README.md. Board: Waveshare RP2350-PiZero with the ELEGOO 3.5" SPI screen on its header.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Board")
set(PICO_PIO_USB_PATH $ENV{HOME}/.pico/Pico-PIO-USB CACHE PATH "Pico-PIO-USB")

include(pico_sdk_import.cmake)

project(viz_test22 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

if (NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "tinyusb_pico_pio_usb wasn't defined: check PICO_PIO_USB_PATH (${PICO_PIO_USB_PATH})")
endif()

add_executable(viz_test22
    main.c
    lcd.c
    cal.c
    tone.c
    land.c
    reader.c
    fm1scr.c
    knobctl.c
    perf.c
    usb_descriptors.c
    sd.c
    diskio.c
    ff.c
    ffunicode.c
    settings.c
    msc.c
    card.c
    guest.c
    swap.c
    fwpage.c
)

target_include_directories(viz_test22 PRIVATE ${CMAKE_CURRENT_LIST_DIR})
# The paint is single-precision floats on the M33's FPU: no errno from the maths, and optimised for speed.
target_compile_options(viz_test22 PRIVATE -Wall -Wextra -Wno-unused-parameter -O2 -fno-math-errno)
# hot.h: the functions run every frame go in RAM on the board (the host checks build the same files without this).
target_compile_definitions(viz_test22 PRIVATE VIZ_BOARD=1)
# The system clock in MHz: 120 (the default, as tests 1-4) or 144 (cmake ... -DVIZ_SYS_MHZ=144 -B build144).
set(VIZ_SYS_MHZ 120 CACHE STRING "system clock, MHz: a multiple of 12, at most 150")
target_compile_definitions(viz_test22 PRIVATE VIZ_SYS_MHZ=${VIZ_SYS_MHZ})
# The picture half a turn round, for a box that stands with its cables behind it (cmake ... -DVIZ_FLIP=OFF: as test 14).
option(VIZ_FLIP "the picture and the touch turned half a turn" ON)
if(VIZ_FLIP)
    target_compile_definitions(viz_test22 PRIVATE VIZ_FLIP=1)
endif()
# A diagnostic build (cmake ... -DVIZ_USB_DIAG=ON -B build144-usbdiag): the status row shows the Mac's side of USB.
option(VIZ_USB_DIAG "status row shows the USB device side's counters" OFF)
if(VIZ_USB_DIAG)
    target_compile_definitions(viz_test22 PRIVATE VIZ_USB_DIAG=1)
endif()

target_link_libraries(viz_test22 PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_util
    hardware_spi
    hardware_dma
    hardware_clocks
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
    m
)

# Our own CDC port does the talking; no stdio on the UART pins or on USB.
pico_enable_stdio_uart(viz_test22 0)
pico_enable_stdio_usb(viz_test22 0)

pico_add_extra_outputs(viz_test22)
