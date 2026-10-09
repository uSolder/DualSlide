/**
 * @file settings_app.h
 * @brief Settings application: screen brightness, volume, and erasing scores.
 *
 * Registered with the application manager like a game, so it appears as a
 * channel in the launcher and has both sliders and both buttons to itself.
 */

#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the settings page.
 *
 * @return true when initialization succeeds; otherwise false.
 */
bool SettingsApp_Init(void);

/**
 * @brief Read the sliders and buttons and apply any changes.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void SettingsApp_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Copy the settings palette into the application palette range.
 *
 * @param Palette Destination for APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT colours.
 *
 * @return true when the palette is copied successfully; otherwise false.
 */
bool SettingsApp_GetSplashScreenPalette(Display_ColourTypeDef *Palette);

/**
 * @brief Draw the settings card shown in the launcher.
 *
 * @param Target Launcher-owned render target.
 *
 * @return true when the splash screen is drawn successfully; otherwise false.
 */
bool SettingsApp_DrawSplashScreen(Render_TargetTypeDef *Target);

/**
 * @brief Render the settings page.
 */
void SettingsApp_Render(void);

/**
 * @brief Pause the settings page, saving any changes.
 */
void SettingsApp_Pause(void);

/**
 * @brief Resume the settings page.
 */
void SettingsApp_Resume(void);

/**
 * @brief Close the settings page, saving any changes.
 */
void SettingsApp_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_APP_H */
