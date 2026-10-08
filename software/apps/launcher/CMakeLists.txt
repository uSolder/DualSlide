file(GLOB_RECURSE DS_LAUNCHER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideLauncher STATIC
    ${DS_LAUNCHER_SOURCES}
)

target_include_directories(DualSlideLauncher
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideLauncher
    PUBLIC
        DualSlideShared
        DualSlideAppManager
        DualSlideAssets
        $<TARGET_NAME_IF_EXISTS:DualSlideEmbeddedOptions>
)
