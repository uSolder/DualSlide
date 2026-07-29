file(GLOB_RECURSE DS_WINDOW_WASHER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideWindowWasher STATIC
    ${DS_WINDOW_WASHER_SOURCES}
)

target_include_directories(DualSlideWindowWasher
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideWindowWasher
    PUBLIC
        DualSlideShared
        DualSlideEmbeddedOptions
        ds_assets
        DualSlideAppManager
)