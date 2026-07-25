file(GLOB_RECURSE DS_APP_MANAGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideAppManager STATIC
    ${DS_APP_MANAGER_SOURCES}
)

target_include_directories(DualSlideAppManager
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(DualSlideAppManager
    PUBLIC
        DualSlideShared
        DualSlideWindowWasher
)