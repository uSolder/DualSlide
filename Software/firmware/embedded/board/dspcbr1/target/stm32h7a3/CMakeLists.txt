add_library(stm32h7a3 STATIC
    target.c

    core/src/system_stm32h7xx.c
    internal/src/rcc.c
    startup/startup_stm32h7a3zitxq.s

    drivers/gpio.c
    drivers/spi.c
    drivers/audio_stream.c
)

file(GLOB_RECURSE STM32H7A3_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/core/src/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/internal/src/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/startup/*.s"
)

list(REMOVE_ITEM STM32H7A3_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/internal/src/it.c"
)

target_sources(stm32h7a3 PRIVATE
    ${STM32H7A3_SOURCES}
)

target_include_directories(stm32h7a3
    PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../api"
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/drivers"
        "${CMAKE_CURRENT_LIST_DIR}/core/inc"
        "${CMAKE_CURRENT_LIST_DIR}/cmsis/Include"
        "${CMAKE_CURRENT_LIST_DIR}/cmsis/Device/ST/STM32H7xx/Include"
        

    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/internal/inc"
)

target_compile_definitions(stm32h7a3 PUBLIC
    STM32H7A3xxQ
    HSE_VALUE=8000000UL
)

target_compile_options(stm32h7a3 PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard

    -ffunction-sections
    -fdata-sections

    -Wall
    -Wextra
    -Wpedantic
)