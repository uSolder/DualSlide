# DualSlide firmware for the STM32H7A3 on DualSlide board revision 1.
#
#   chip/     the STM32H7A3: start-up, clocks, peripherals, CMSIS
#   board/    this board: pins, power, the display panel
#   system/   services: display, drawing, sound, controls, saves, the main loop
#   usb/      the USB device and the DUALSLIDE drive
#   games/    the game disk, game files and the API games call
#   apps/     the launcher and the app manager

set(DS_ASSETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../assets")

file(GLOB DS_FIRMWARE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/chip/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/chip/*.s"
    "${CMAKE_CURRENT_LIST_DIR}/board/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/system/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/usb/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/games/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/apps/*.c"
)

# Fonts, and music instruments made by tools/music_converter/instrument_converter.py.
file(GLOB DS_ASSET_SOURCES CONFIGURE_DEPENDS
    "${DS_ASSETS_DIR}/fonts/*.c"
    "${DS_ASSETS_DIR}/instruments/*.c"
)

add_executable(DualSlideFirmware
    main.c
    ${DS_FIRMWARE_SOURCES}
    ${DS_ASSET_SOURCES}
)

set_target_properties(DualSlideFirmware PROPERTIES
    SUFFIX ".elf"
)

target_include_directories(DualSlideFirmware PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/chip"
    "${CMAKE_CURRENT_LIST_DIR}/chip/cmsis"
    "${CMAKE_CURRENT_LIST_DIR}/board"
    "${CMAKE_CURRENT_LIST_DIR}/system"
    "${CMAKE_CURRENT_LIST_DIR}/usb"
    "${CMAKE_CURRENT_LIST_DIR}/games"
    "${CMAKE_CURRENT_LIST_DIR}/apps"
    "${DS_ASSETS_DIR}/fonts"
    "${DS_ASSETS_DIR}/instruments"
)

# The exact part, for the CMSIS device header, and the board's 8 MHz crystal.
target_compile_definitions(DualSlideFirmware PRIVATE
    STM32H7A3xxQ
    HSE_VALUE=8000000UL
)

set(DS_CPU_OPTIONS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

target_compile_options(DualSlideFirmware PRIVATE
    -O2
    -g3
    ${DS_CPU_OPTIONS}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Wpedantic
)

set(DS_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/chip/STM32H7A3RGT6_FLASH.ld")

target_link_options(DualSlideFirmware PRIVATE
    ${DS_CPU_OPTIONS}
    "-T${DS_LINKER_SCRIPT}"
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/DualSlideFirmware.map"
)

set_target_properties(DualSlideFirmware PROPERTIES LINK_DEPENDS "${DS_LINKER_SCRIPT}")

target_link_libraries(DualSlideFirmware PRIVATE m c nosys)
