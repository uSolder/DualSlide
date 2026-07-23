set(DS_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/board/dspcbr1")
set(DS_TARGET_DIR "${DS_BOARD_DIR}/target/stm32h7a3")

file(GLOB_RECURSE DS_EMBEDDED_SOURCES CONFIGURE_DEPENDS
    "${DS_BOARD_DIR}/main.c"
    "${DS_BOARD_DIR}/interface/*.c"
    "${DS_TARGET_DIR}/core/src/*.c"
    "${DS_TARGET_DIR}/drivers/*.c"
    "${DS_TARGET_DIR}/startup/*.s"
    "${CMAKE_CURRENT_LIST_DIR}/os/*.c"
)

add_executable(DualSlideFirmware
    ${DS_EMBEDDED_SOURCES}
)

set_target_properties(DualSlideFirmware PROPERTIES
    SUFFIX ".elf"
)

target_include_directories(DualSlideFirmware PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../shared"
    "${DS_BOARD_DIR}"
    "${DS_BOARD_DIR}/target/api"
    "${DS_TARGET_DIR}/core/inc"
    "${DS_TARGET_DIR}/cmsis/Include"
    "${DS_TARGET_DIR}/cmsis/Device/ST/STM32H7xx/Include"
)

target_compile_options(DualSlideFirmware PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -ffunction-sections
    -fdata-sections
)

target_link_options(DualSlideFirmware PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -nostartfiles
    --specs=nosys.specs
    -T
    "${DS_TARGET_DIR}/linker/STM32H7A3RGT6_FLASH.ld"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/DualSlideFirmware.map"
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
)

target_include_directories(DualSlideFirmware PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../shared"
    "${DS_BOARD_DIR}"
    "${DS_BOARD_DIR}/target/api"
    "${DS_TARGET_DIR}/core/inc"

    # CMSIS Core + STM32H7 device-register headers
    "${DS_TARGET_DIR}/cmsis/Include"
    "${DS_TARGET_DIR}/cmsis/Device/ST/STM32H7xx/Include"
)

target_compile_options(DualSlideFirmware PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_custom_command(TARGET DualSlideFirmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O ihex
        $<TARGET_FILE:DualSlideFirmware>
        $<TARGET_FILE_DIR:DualSlideFirmware>/DualSlideFirmware.hex

    COMMAND ${CMAKE_OBJCOPY}
        -O binary
        $<TARGET_FILE:DualSlideFirmware>
        $<TARGET_FILE_DIR:DualSlideFirmware>/DualSlideFirmware.bin

    COMMENT "Generating Intel HEX and raw binary firmware images"
)