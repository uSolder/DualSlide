# DualSlide game SDK

Builds DualSlide games as `.dsg` game files, which players copy onto the
DUALSLIDE drive. Each game lives in its own repository beside this one.

## Writing a game

Start from the `DualSlide-GameTemplate` repository. A game uses the ordinary
DualSlide headers (`render.h`, `controls.h`, `sound.h`, `mixer.h`, `music.h`,
`save.h`, `random.h`, `open_sans.h` and so on) and defines an
`AppManager_AppTypeDef` with its `Init`, `Update`, `Render` and other
callbacks.

Its `CMakeLists.txt` fetches this repository at a release tag, so every game
builds against a known SDK, then names the game:

```cmake
set(DUALSLIDE_VERSION v1.0.0)

include(FetchContent)
FetchContent_Declare(DualSlide
    GIT_REPOSITORY https://github.com/uSolder/DualSlide.git
    GIT_TAG        ${DUALSLIDE_VERSION}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  software/sdk)
FetchContent_MakeAvailable(DualSlide)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${dualslide_SOURCE_DIR}/software/sdk/cmake/toolchain.cmake")
endif()

project(Pong LANGUAGES C)

include("${dualslide_SOURCE_DIR}/software/sdk/cmake/DualSlideGame.cmake")

dualslide_add_game(pong
    APP         Pong_App
    ID          dualslide.pong
    NAME        "Pong"
    VERSION     1.0.0
    AUTHOR      "Jack Herron"
    DESCRIPTION "Calm, modern Pong for one or two players."
    SOURCES     src/pong.c src/pong_audio.c
    INCLUDES    inc)
```

- `ID` identifies the game for good: keep it when releasing new versions.
- `VERSION` is the game's own version, shown to players. Raise it with every
  release.
- `DUALSLIDE_VERSION` is the DualSlide release the game is built for. Each
  game repository also has a `local` preset that sets
  `FETCHCONTENT_SOURCE_DIR_DUALSLIDE` to a checkout beside it, for changing
  the SDK and a game together.

## What games can include

Only the game-facing firmware headers, which the SDK copies into the game's
build folder (`DUALSLIDE_SDK_HEADERS` in `cmake/DualSlideGame.cmake`):
`render.h`, `display.h`, `audio.h`, `font.h`, `system_time.h`, `controls.h`,
`sound.h`, `mixer.h`, `music.h`, `save.h`, `random.h`, `system.h`,
`app_manager.h`, and the SDK's `open_sans.h` and `open_sans_bold.h`. The rest
of the firmware is out of reach.

## How it works

| Part | Job |
| --- | --- |
| `src/dualslide_sdk.c` | The firmware functions a game calls, forwarding to the firmware through the game API (`firmware/system/inc/game_api.h`), plus the entry point |
| `include/` | Font headers that point at the firmware's fonts |
| `game.ld` | Links the game at address zero as one block, keeping its relocations |
| `tools/make_dsg.py` | Packs the linked game into a `.dsg` (`firmware/system/inc/game_file.h`) |
| `cmake/` | `dualslide_add_game()` and the toolchain |

Games are compiled with `-mword-relocations`, so every absolute address is a
whole word the firmware can relocate; `make_dsg.py` refuses anything else.
Each game carries its own copy of the C and maths libraries.

## Limits

- Fonts are reached through the API, so `&OpenSans20` and friends cannot
  appear in static initialisers.
- No `malloc`, `printf` or file access: use `Render_FormatText()` for text.
