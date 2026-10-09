file(GLOB_RECURSE DS_SETTINGS_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideSettingsApp STATIC
    ${DS_SETTINGS_APP_SOURCES}
)

target_include_directories(DualSlideSettingsApp
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideSettingsApp
    PUBLIC
        DualSlideShared
        DualSlideAppManager
        DualSlideAssets
        $<TARGET_NAME_IF_EXISTS:DualSlideEmbeddedOptions>
)
