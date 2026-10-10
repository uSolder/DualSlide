/**
 * @file canal_tug.h
 * @brief Public interface for Canal Tug.
 *
 * A game exports a single AppManager_AppTypeDef describing its functions and
 * colours; everything else stays private to the game's source files.
 */

#ifndef CANAL_TUG_H
#define CANAL_TUG_H

#include "app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The app, as registered with the application manager. */
extern const AppManager_AppTypeDef CanalTug_App;

#ifdef __cplusplus
}
#endif

#endif /* CANAL_TUG_H */
