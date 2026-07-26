include(FetchContent)

file(GLOB_RECURSE DS_WINDOWS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(DualSlideSim
    ${DS_WINDOWS_SOURCES}
)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.16
)

FetchContent_MakeAvailable(SDL3)

target_link_libraries(DualSlideSim
    PRIVATE
        DualSlideShared
        DualSlideSystem
        DualSlideAppManager
        SDL3::SDL3
)

target_include_directories(DualSlideSim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../shared"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/api"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/system/inc"
)

