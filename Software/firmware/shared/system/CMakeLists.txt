file(GLOB_RECURSE SYSTEM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

add_library(DualSlideSystem STATIC
    ${SYSTEM_SOURCES}
)

target_include_directories(DualSlideSystem
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/inc"

)

target_link_libraries(DualSlideSystem
    PUBLIC
        DualSlideShared
        DualSlideAppManager
)