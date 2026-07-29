/**
 * @file template_game.h
 * @brief Public interface for the DualSlide template game.
 *
 * This file defines the functions used by the application manager to control
 * the game. New games should keep this interface intact unless the application
 * manager contract is intentionally changed.
 */

#ifndef TEMPLATE_GAME_H
#define TEMPLATE_GAME_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the game.
 *
 * Use this function to reset game state, prepare assets, and initialize any
 * resources owned by the game.
 *
 * @return true when initialization succeeds; otherwise false.
 */
bool TemplateGame_Init(void);

/**
 * @brief Advance the game state.
 *
 * This function should process input and update game logic. Rendering should
 * remain in TemplateGame_Render().
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void TemplateGame_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Copy the application's palette into the launcher-reserved range.
 *
 * Applications may use palette indices 0 through
 * APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT - 1. The launcher owns the remaining
 * palette entries.
 *
 * @param Palette Destination palette supplied by the application manager.
 *
 * @return true when the palette is copied successfully; otherwise false.
 */
bool TemplateGame_GetSplashScreenPalette(Display_ColourTypeDef *Palette);

/**
 * @brief Draw the game's splash-screen artwork.
 *
 * The application manager supplies the render target and clips drawing to the
 * splash-screen area. The game must not acquire or present a display frame from
 * this function.
 *
 * @param Target Launcher-owned render target.
 *
 * @return true when the splash screen is drawn successfully; otherwise false.
 */
bool TemplateGame_DrawSplashScreen(Render_TargetTypeDef *Target);

/**
 * @brief Render one complete game frame.
 *
 * This function acquires the display framebuffer, draws the game, and presents
 * the completed frame.
 */
void TemplateGame_Render(void);

/**
 * @brief Pause the game.
 *
 * Paused games should stop updating and rendering until resumed.
 */
void TemplateGame_Pause(void);

/**
 * @brief Resume a previously paused game.
 */
void TemplateGame_Resume(void);

/**
 * @brief Release the game and return it to an uninitialized state.
 */
void TemplateGame_Shutdown(void);

#endif /* TEMPLATE_GAME_H */