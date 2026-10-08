file(GLOB_RECURSE DS_PONG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlidePong STATIC
    ${DS_PONG_SOURCES}
)

target_include_directories(DualSlidePong
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlidePong
    PUBLIC
        DualSlideShared
        DualSlideAppManager
        DualSlideAssets
        $<TARGET_NAME_IF_EXISTS:DualSlideEmbeddedOptions>
)
