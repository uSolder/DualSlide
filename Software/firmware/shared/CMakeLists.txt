# Shared contracts are header-only at present. Keeping them in an INTERFACE
# target lets both platform builds consume the same public API without adding
# an empty archive to the link step.
add_library(DualSlideShared INTERFACE)

target_include_directories(DualSlideShared
    INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/api"
        "${CMAKE_CURRENT_LIST_DIR}/os"
)