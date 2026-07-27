add_library(dspcbr1 STATIC
    board.c
)

file(GLOB_RECURSE DSPCBR1_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/interface/*.c"
)

target_sources(dspcbr1 PRIVATE
    ${DSPCBR1_SOURCES}
)

target_include_directories(dspcbr1 PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/drivers"
    "${CMAKE_CURRENT_LIST_DIR}/interface"
    "${CMAKE_CURRENT_LIST_DIR}/target/api"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/ics/inc"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/modules/inc"
)

target_compile_options(dspcbr1 PRIVATE
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

add_subdirectory(target/stm32h7a3)

target_link_libraries(dspcbr1
    PUBLIC
        stm32h7a3
        DualSlideShared
        DualSlideEmbeddedOptions
)