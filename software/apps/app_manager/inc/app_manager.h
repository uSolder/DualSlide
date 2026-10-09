/**
 * @file app_manager.h
 * @brief Application registration, lifecycle, and launcher-preview interface.
 *
 * An app describes itself with one AppManager_AppTypeDef: its functions and
 * its colours. The app manager calls Init when the app starts, Update and
 * Render every frame, and Shutdown when it closes. It prepares the screen
 * before Render and shows it afterwards, and loads the app's palette, so an
 * app only ever draws.
 */

#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of applications registered with the application manager.
 *
 * Counted from the list in app_manager.c, so adding an app there is enough.
 */
#define NUM_APPS (AppManager_GetAppCount())

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
 * @brief Everything the app manager needs to know about an app.
 *
 * Only Init and Render are required; leave out any other function the app
 * doesn't need. Leave out the palette to use the standard one (RENDER_RED
 * and the rest of the colours in render.h).
 *
 *     const AppManager_AppTypeDef MyGame_App =
 *     {
 *         .Init = MyGame_Init,
 *         .Update = MyGame_Update,
 *         .Render = MyGame_Render,
 *         .DrawSplashScreen = MyGame_DrawSplashScreen,
 *         APP_PALETTE(MyGame_Palette)
 *     };
 */
typedef struct
{
    /** Set up a new game. Return false if it can't start. */
    bool (*Init)(void);

    /** Read the controls and move the game on by DeltaTimeMilliseconds. */
    void (*Update)(uint32_t DeltaTimeMilliseconds);

    /** Draw the whole screen into Target; it is shown when this returns. */
    void (*Render)(Render_TargetTypeDef *Target);

    /**
     * Draw the launcher's preview of the app, only within the area given by
     * APP_MANAGER_SPLASH_SCREEN_X, _Y, _WIDTH and _HEIGHT.
     */
    bool (*DrawSplashScreen)(Render_TargetTypeDef *Target);

    /** The app is put aside (for example while the device sleeps). */
    void (*Pause)(void);

    /** The app comes back after Pause. */
    void (*Resume)(void);

    /** The app closes: save anything worth keeping. */
    void (*Shutdown)(void);

    /** The app's colours, palette index 0 upwards; NULL for the standard palette. */
    const Display_ColourTypeDef *Palette;

    /** Number of colours in Palette, at most APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT. */
    uint16_t PaletteCount;
} AppManager_AppTypeDef;

/** Fills an AppManager_AppTypeDef's Palette and PaletteCount from an array of colours. */
#define APP_PALETTE(Array) .Palette = (Array), .PaletteCount = (uint16_t)(sizeof(Array) / sizeof((Array)[0]))

/**
 * @brief Return the number of registered applications (see NUM_APPS).
 */
uint16_t AppManager_GetAppCount(void);

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
 * Acquires a frame, has the launcher or the active application draw it, and
 * presents it.
 */
void AppManager_Render(void);

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
 * @brief Start an application registered with the application manager.
 *
 * The launcher is paused while the selected application is active.
 *
 * @param ApplicationIndex Zero-based index of the application to start.
 *
 * @return true if the application started successfully; otherwise false.
 */
bool AppManager_StartApplication(uint16_t ApplicationIndex);

/**
 * @brief Pauses the currently active runtime component.
 */
void AppManager_Pause(void);

/**
 * @brief Resumes the currently active runtime component.
 */
void AppManager_Resume(void);

/**
 * @brief Return to the launcher from the active application.
 *
 * The active application is shut down and the existing launcher instance is
 * resumed. This has no effect when the launcher is already active or the
 * application manager has not been initialized.
 */
void AppManager_OpenLauncher(void);

/**
 * @brief Return whether the launcher, rather than an application, is active.
 */
bool AppManager_IsLauncherActive(void);

/**
 * @brief Shuts down the application manager and active runtime component.
 */
void AppManager_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MANAGER_H */
