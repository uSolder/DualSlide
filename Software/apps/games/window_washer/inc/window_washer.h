/**
 * @file window_washer.h
 * @brief Window Washer game application interface.
 */

#ifndef WINDOW_WASHER_H
#define WINDOW_WASHER_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Window Washer game.
 *
 * @return true if initialization succeeded; otherwise false.
 */
bool WindowWasher_Init(void);

/**
 * @brief Updates game input and elapsed-time state.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void WindowWasher_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Updates game simulation and renders the next frame.
 */
void WindowWasher_Render(void);

/**
 * @brief Generates the launcher splash-screen palette.
 *
 * The supplied palette corresponds to palette indexes
 * APP_MANAGER_SPLASH_PALETTE_START_INDEX through
 * APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT - 1. Palette element zero becomes
 * colour index APP_MANAGER_SPLASH_PALETTE_START_INDEX.
 *
 * The application must write no more than
 * APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT entries. The supplied pointer is not
 * retained after this function returns.
 *
 * @param Palette Destination palette buffer.
 *
 * @return true if the palette was generated successfully; otherwise false.
 */
bool WindowWasher_GetSplashScreenPalette(Display_ColourTypeDef *Palette);

/**
 * @brief Draws the launcher splash screen.
 *
 * The launcher owns the render target and installs the palette before this
 * function is called. The application shall draw only within the splash-screen
 * rectangle defined by the application manager.
 *
 * All splash-screen pixels must use palette indexes beginning at
 * APP_MANAGER_SPLASH_PALETTE_START_INDEX.
 *
 * The supplied render-target pointer is valid only for the duration of this
 * call and must not be retained by the application.
 *
 * @param Target Launcher-owned render target.
 *
 * @return true if the splash screen was drawn successfully; otherwise false.
 */
bool WindowWasher_DrawSplashScreen(Render_TargetTypeDef *Target);

/**
 * @brief Pauses the game.
 */
void WindowWasher_Pause(void);

/**
 * @brief Resumes the game.
 */
void WindowWasher_Resume(void);

/**
 * @brief Shuts down the game.
 */
void WindowWasher_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_WASHER_H */