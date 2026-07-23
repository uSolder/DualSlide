cmake_minimum_required(VERSION 3.25)

project(DualSlide LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DS_PLATFORM "windows" CACHE STRING "Build platform: windows or embedded")
set_property(CACHE DS_PLATFORM PROPERTY STRINGS windows embedded)

if(DS_PLATFORM STREQUAL "windows")
    add_subdirectory(firmware/windows)
elseif(DS_PLATFORM STREQUAL "embedded")
    add_subdirectory(firmware/embedded)
else()
    message(FATAL_ERROR
        "Invalid DS_PLATFORM='${DS_PLATFORM}'. Choose 'windows' or 'embedded'.")
endif()