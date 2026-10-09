/**
 * @file save.h
 * @brief Keep a game's data (high scores, unlocks, records) when the power goes off.
 *
 * QUICK START
 *
 *   Put everything worth keeping in one structure, give it a short name of up
 *   to four letters that no other game uses, and load it when the game starts:
 *
 *       typedef struct
 *       {
 *           uint32_t BestScore;
 *           char Initials[4];
 *       } MyGame_SaveTypeDef;
 *
 *       static MyGame_SaveTypeDef MyGame_Save = { 0, "---" };   // used until a save exists
 *
 *       Save_Load("MYGM", &MyGame_Save, sizeof(MyGame_Save));   // in Init
 *       Save_Store("MYGM", &MyGame_Save, sizeof(MyGame_Save));  // after a new best score
 *
 *   Save_Load() leaves your structure untouched when there is nothing saved,
 *   so the starting values you gave it stay. If you later add or remove
 *   fields, the structure's size changes and old saves are ignored rather
 *   than read wrongly.
 *
 *   Save_Store() only writes when the data has changed, so calling it often
 *   doesn't wear out the memory, but saving at moments that matter (a new
 *   high score, the game closing) is best.
 */

#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load saved data into Data.
 *
 * @param Name Up to four letters naming the save, such as "PONG".
 * @param Data The structure to fill.
 * @param Size sizeof the structure.
 *
 * @return true if a save of exactly this size was found and loaded;
 *         otherwise false, with Data left as it was.
 */
bool Save_Load(const char *Name, void *Data, uint32_t Size);

/**
 * @brief Save Data, if it differs from what is already saved.
 *
 * @return true if the data is saved.
 */
bool Save_Store(const char *Name, const void *Data, uint32_t Size);

/**
 * @brief Delete a save.
 *
 * @return true if the save is gone (or never existed).
 */
bool Save_Erase(const char *Name);

#ifdef __cplusplus
}
#endif

#endif /* SAVE_H */
