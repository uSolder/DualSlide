# Builds DualSlide games as .dsg game files.
#
#   dualslide_add_game(pong
#       APP         Pong_App              # the game's AppManager_AppTypeDef
#       ID          dualslide.pong        # unique and permanent
#       NAME        "Pong"
#       VERSION     1.0.0
#       AUTHOR      "Jack Herron"
#       DESCRIPTION "Classic bat and ball."
#       SOURCES     src/pong.c src/pong_audio.c
#       INCLUDES    inc)
#
# Produces <target>.dsg in the build folder. The game is compiled with the
# firmware's compiler settings and its own copy of the C and maths libraries,
# and calls the firmware through the SDK (sdk/src/dualslide_sdk.c).

set(DUALSLIDE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")
set(DUALSLIDE_SOFTWARE_DIR "${DUALSLIDE_SDK_DIR}/.." CACHE INTERNAL "")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(dualslide_add_game TARGET)
    cmake_parse_arguments(GAME "" "APP;ID;NAME;VERSION;AUTHOR;DESCRIPTION" "SOURCES;INCLUDES" ${ARGN})

    foreach(REQUIRED APP ID NAME VERSION SOURCES)
        if(NOT GAME_${REQUIRED})
            message(FATAL_ERROR "dualslide_add_game(${TARGET}) needs ${REQUIRED}")
        endif()
    endforeach()

    add_executable(${TARGET}
        ${GAME_SOURCES}
        "${DUALSLIDE_SDK_DIR}/src/dualslide_sdk.c"
    )

    set_target_properties(${TARGET} PROPERTIES
        SUFFIX ".elf"
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    # The SDK's font headers come first, in place of the firmware's.
    target_include_directories(${TARGET} PRIVATE
        ${GAME_INCLUDES}
        "${DUALSLIDE_SDK_DIR}/include"
        "${DUALSLIDE_SOFTWARE_DIR}/firmware/api"
        "${DUALSLIDE_SOFTWARE_DIR}/firmware/system/inc"
        "${DUALSLIDE_SOFTWARE_DIR}/firmware/apps/app_manager/inc"
        "${DUALSLIDE_SOFTWARE_DIR}/assets/fonts"
    )

    target_compile_definitions(${TARGET} PRIVATE DUALSLIDE_GAME_APP=${GAME_APP})

    target_compile_options(${TARGET} PRIVATE
        -O2
        -g3
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
        -ffunction-sections
        -fdata-sections
        # Absolute addresses only as whole words, so the firmware can relocate them.
        -mword-relocations
        -Wall
        -Wextra
        -Wpedantic
    )

    target_link_options(${TARGET} PRIVATE
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
        --specs=nano.specs
        -nostartfiles
        "-T${DUALSLIDE_SDK_DIR}/game.ld"
        -Wl,--emit-relocs
        -Wl,--gc-sections
        -Wl,--no-undefined
        # The game is one block of code and data by design.
        -Wl,--no-warn-rwx-segments
        "-Wl,-Map=$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.map"
    )

    target_link_libraries(${TARGET} PRIVATE m c gcc)

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${DUALSLIDE_SDK_DIR}/tools/make_dsg.py"
            --elf "$<TARGET_FILE:${TARGET}>"
            --out "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.dsg"
            --api-header "${DUALSLIDE_SOFTWARE_DIR}/firmware/system/inc/game_api.h"
            --id "${GAME_ID}"
            --name "${GAME_NAME}"
            --version "${GAME_VERSION}"
            --author "${GAME_AUTHOR}"
            --description "${GAME_DESCRIPTION}"
        COMMENT "Packaging ${GAME_NAME} as ${TARGET}.dsg"
        VERBATIM
    )
endfunction()
