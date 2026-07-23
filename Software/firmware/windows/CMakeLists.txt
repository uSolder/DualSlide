file(GLOB_RECURSE DS_WINDOWS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(DualSlideSim
    ${DS_WINDOWS_SOURCES}
)

target_include_directories(DualSlideSim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../shared"
)