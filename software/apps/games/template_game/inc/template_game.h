/**
 * @file template_game.h
 * @brief Public interface for the DualSlide template game.
 *
 * A game exports a single AppManager_AppTypeDef describing its functions and
 * colours; everything else stays private to the game's source files.
 */

#ifndef TEMPLATE_GAME_H
#define TEMPLATE_GAME_H

#include "app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The app, as registered with the application manager. */
extern const AppManager_AppTypeDef TemplateGame_App;

#ifdef __cplusplus
}
#endif

#endif /* TEMPLATE_GAME_H */
