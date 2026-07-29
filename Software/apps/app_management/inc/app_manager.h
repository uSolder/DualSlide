/**
 * @file app_manager.h
 * @brief Application registration, lifecycle, and launcher-preview interface.
 */

#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "audio.h"
#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of applications registered with the application manager.
 */
#define NUM_APPS (2U)

/**
 * @brief Splash-screen position within the logical render target.
 */
#define APP_MANAGER_SPLASH_SCREEN_X      (60U)
#define APP_MANAGER_SPLASH_SCREEN_Y      (60U)
#define APP_MANAGER_SPLASH_SCREEN_WIDTH  (680U)
#define APP_MANAGER_SPLASH_SCREEN_HEIGHT (360U)

/**
 * @brief First palette index reserved for application splash-screen content.
 *
 * Palette indexes 0 through 127 are reserved for application splash-screen
 * content. Palette indexes 128 through 255 are reserved for the launcher and
 * shared system graphics.
 */
#define APP_MANAGER_SPLASH_PALETTE_START_INDEX (0U)

/**
 * @brief Number of palette entries reserved for application splash screens.
 */
#define APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT (128U)

/**
 * @brief Initializes the application manager.
 *
 * The application manager initially starts the launcher and later transfers
 * control to the application selected by the launcher.
 *
 * @return true if initialization completed successfully; otherwise false.
 */
bool AppManager_Init(void);

/**
 * @brief Updates the currently active runtime component.
 *
 * This updates either the launcher or the active application.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void AppManager_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Renders the currently active runtime component.
 *
 * This renders either the launcher or the active application.
 */
void AppManager_Render(void);

/**
 * @brief Fills the audio output buffer for the currently active application.
 *
 * @param Samples Destination audio sample buffer.
 * @param FrameCount Number of audio frames to generate.
 * @param Context Optional callback context supplied by the audio subsystem.
 */
void AppManager_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context);

/**
 * @brief Retrieves an application's launcher splash-screen palette.
 *
 * The application writes colours corresponding to palette indexes
 * APP_MANAGER_SPLASH_PALETTE_START_INDEX through
 * APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT - 1.
 *
 * The launcher owns the complete display palette and passes a pointer to the
 * first splash-screen palette entry.
 *
 * The supplied palette pointer is valid only for the duration of this call
 * and must not be retained by the application.
 *
 * @param ApplicationIndex Zero-based application index.
 * @param Palette Destination for APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT colours.
 *
 * @return true if the application supplied its splash-screen palette;
 *         otherwise false.
 */
bool AppManager_GetAppSplashScreenPalette(uint16_t ApplicationIndex, Display_ColourTypeDef *Palette);

/**
 * @brief Draws an application's splash screen into the launcher render target.
 *
 * The application receives the complete launcher-owned render target, including
 * its dimensions and row stride. It must draw only within the rectangle defined
 * by APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y,
 * APP_MANAGER_SPLASH_SCREEN_WIDTH, and APP_MANAGER_SPLASH_SCREEN_HEIGHT.
 *
 * Splash-screen pixels must use palette indexes 0 through
 * APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT - 1. The application must not acquire
 * or present a display frame and must not modify the display palette.
 *
 * The target pointer is valid only for the duration of this call and must not
 * be retained by the application.
 *
 * @param ApplicationIndex Zero-based application index.
 * @param Target Launcher-owned render target.
 *
 * @return true if the splash screen was drawn successfully; otherwise false.
 */
bool AppManager_DrawAppSplashScreen(uint16_t ApplicationIndex, Render_TargetTypeDef *Target);

/**
 * @brief Pauses the currently active runtime component.
 */
void AppManager_Pause(void);

/**
 * @brief Resumes the currently active runtime component.
 */
void AppManager_Resume(void);

/**
 * @brief Shuts down the application manager and active runtime component.
 */
void AppManager_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MANAGER_H */