cmake_minimum_required(VERSION 3.25)

project(DualSlide LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DS_PLATFORM "windows" CACHE STRING "Build platform: windows or embedded")
set_property(CACHE DS_PLATFORM PROPERTY STRINGS windows embedded)

add_library(ds_assets STATIC
    assets/fonts/open_sans.c
    assets/fonts/open_sans_bold.c
)

target_include_directories(ds_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
)

if(DS_PLATFORM STREQUAL "embedded")
    target_compile_options(ds_assets PRIVATE
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
        -ffunction-sections
        -fdata-sections
    )
endif()

add_subdirectory(firmware/shared)
add_subdirectory(apps)

if(DS_PLATFORM STREQUAL "windows")
    add_subdirectory(firmware/windows)
elseif(DS_PLATFORM STREQUAL "embedded")
    add_subdirectory(firmware/embedded)
else()
    message(FATAL_ERROR
        "Invalid DS_PLATFORM='${DS_PLATFORM}'. Choose 'windows' or 'embedded'.")
endif()