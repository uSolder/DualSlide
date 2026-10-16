/**
 * @file game_library.h
 * @brief The games installed on the game disk, for the app manager.
 */

#ifndef GAME_LIBRARY_H
#define GAME_LIBRARY_H

#include "fat_reader.h"
#include "game_file.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most games listed at once. */
#define GAME_LIBRARY_MAX_GAMES              (32U)

/**
 * @brief One installed game.
 */
typedef struct
{
    FAT_FileTypeDef File;
    GameFile_SummaryTypeDef Summary;
    GameFile_ResultTypeDef Result;          /* GAME_FILE_OK, or why it cannot run */
} GameLibrary_GameTypeDef;

/**
 * @brief List the games on the disk afresh.
 *
 * Game files are the root directory's .DSG files that are not hidden.
 * Files that are not games are left out; games that cannot run are listed
 * with the reason. They are sorted by name, and where several copies share
 * an identifier, only the newest that can run is kept.
 *
 * @return Number of games listed.
 */
uint16_t GameLibrary_Scan(void);

/** @brief Return the number of games listed. */
uint16_t GameLibrary_GetCount(void);

/** @brief Return a listed game, or NULL for an index past the end. */
GameLibrary_GameTypeDef *GameLibrary_GetGame(uint16_t Index);

/**
 * @brief Draw a card explaining why a game cannot run, in the splash area.
 *
 * Uses the standard palette's colours.
 */
void GameLibrary_DrawProblem(const GameLibrary_GameTypeDef *Game, Render_TargetTypeDef *Target);

#ifdef __cplusplus
}
#endif

#endif /* GAME_LIBRARY_H */
