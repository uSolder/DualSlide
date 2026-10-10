file(GLOB_RECURSE DS_CANAL_TUG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideCanalTug STATIC
    ${DS_CANAL_TUG_SOURCES}
)

target_include_directories(DualSlideCanalTug
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideCanalTug
    PUBLIC
        DualSlideShared
        DualSlideAppManager
        DualSlideAssets
        $<TARGET_NAME_IF_EXISTS:DualSlideEmbeddedOptions>
)
