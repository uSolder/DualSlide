/**
 * @file game_loader.h
 * @brief Loads a game file from the game disk into game memory and starts it.
 *
 * One game is loaded at a time (game_memory.h). Loading another replaces it,
 * so the caller first stops everything the loaded game started: its sounds,
 * music and mixer channels.
 */

#ifndef GAME_LOADER_H
#define GAME_LOADER_H

#include "app_manager.h"
#include "fat_reader.h"
#include "game_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a game file's summary and decide whether it can run here.
 *
 * Reads only the start of the file.
 *
 * @param File    The game file.
 * @param Summary Receives the summary; see GameFile_ReadSummary().
 *
 * @return GAME_FILE_OK, or why the game cannot run.
 */
GameFile_ResultTypeDef GameLoader_ReadSummary(const FAT_FileTypeDef *File, GameFile_SummaryTypeDef *Summary);

/**
 * @brief Load a game file into game memory and call its entry point.
 *
 * Checks the whole file's CRC, copies the code and data, clears the zeroed
 * memory, applies the relocations and makes the code runnable.
 *
 * @param File        The game file.
 * @param Application Receives the game's application on success.
 *
 * @return GAME_FILE_OK, or why the game could not be loaded. Game memory
 *         holds no runnable game after a failure.
 */
GameFile_ResultTypeDef GameLoader_Load(const FAT_FileTypeDef *File, const AppManager_AppTypeDef **Application);

/**
 * @brief Return the bytes of memory available to a loaded game.
 */
uint32_t GameLoader_GetAvailableMemory(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_LOADER_H */
