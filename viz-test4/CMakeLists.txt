# AISLING visualiser, test 4: test 3's gamma sent at every start-up, and a warmth, with the calibration screen.
# Build: see README.md. Board: Waveshare RP2350-PiZero with the ELEGOO 3.5" SPI screen on its header.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Board")
set(PICO_PIO_USB_PATH $ENV{HOME}/.pico/Pico-PIO-USB CACHE PATH "Pico-PIO-USB")

include(pico_sdk_import.cmake)

project(viz_test4 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

if (NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "tinyusb_pico_pio_usb wasn't defined: check PICO_PIO_USB_PATH (${PICO_PIO_USB_PATH})")
endif()

add_executable(viz_test4
    main.c
    lcd.c
    cal.c
    tone.c
    land.c
    usb_descriptors.c
)

target_include_directories(viz_test4 PRIVATE ${CMAKE_CURRENT_LIST_DIR})
# The paint is single-precision floats on the M33's FPU: no errno from the maths, and optimised for speed.
target_compile_options(viz_test4 PRIVATE -Wall -Wextra -Wno-unused-parameter -O2 -fno-math-errno)

target_link_libraries(viz_test4 PRIVATE
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
pico_enable_stdio_uart(viz_test4 0)
pico_enable_stdio_usb(viz_test4 0)

pico_add_extra_outputs(viz_test4)
