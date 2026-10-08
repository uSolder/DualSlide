add_library(DualSlideEmbeddedOptions INTERFACE)

target_compile_options(DualSlideEmbeddedOptions INTERFACE
    -O2
    -g3

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

target_link_options(DualSlideEmbeddedOptions INTERFACE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_executable(DualSlideFirmware
    main.c
    board/dspcbr1/target/stm32h7a3/internal/src/it.c
)

set_target_properties(DualSlideFirmware PROPERTIES
    SUFFIX ".elf"
)

option(DS_USB_AUDIO_TEST "Also enumerate as a USB speaker whose PCM replaces application audio" OFF)

if(DS_USB_AUDIO_TEST)
    target_compile_definitions(DualSlideSystem PRIVATE DS_USB_AUDIO_TEST)
endif()

add_subdirectory(board/dspcbr1)

target_link_libraries(DualSlideFirmware
    PRIVATE
        "-Wl,--start-group"

        DualSlideSystem
        DualSlideAppManager
        DualSlideLauncher
        DualSlideWindowWasher
        DualSlidePong
        DualSlideTanks
        DualSlideTemplateGame
        dspcbr1

        "-Wl,--end-group"

        DualSlideEmbeddedOptions

        m
        c
        nosys
)

set(DS_LINKER_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/board/dspcbr1/target/stm32h7a3/linker/STM32H7A3RGT6_FLASH.ld"
)

target_link_options(DualSlideFirmware PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard

    "-T${DS_LINKER_SCRIPT}"

    -Wl,-Map=${CMAKE_BINARY_DIR}/DualSlide.map
    -Wl,--print-memory-usage

    -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/DualSlideFirmware.map"
)
