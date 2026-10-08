file(GLOB_RECURSE DS_TANKS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideTanks STATIC
    ${DS_TANKS_SOURCES}
)

target_include_directories(DualSlideTanks
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideTanks
    PUBLIC
        DualSlideShared
        DualSlideAppManager
        DualSlideAssets
        $<TARGET_NAME_IF_EXISTS:DualSlideEmbeddedOptions>
)
