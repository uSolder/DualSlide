/**
 * @file template_game.c
 * @brief Minimal starting point for a DualSlide game.
 *
 * This example intentionally contains very little game logic. Both the splash
 * screen and the running game draw a white background with the word "Template"
 * in the 36-point Open Sans font.
 *
 * Developers can use this file as a safe starting point for a new game:
 *
 * 1. Add game state near the module-level variables, and reset it in
 *    TemplateGame_Init().
 * 2. Read controls and update gameplay in TemplateGame_Update().
 * 3. Draw the whole screen in TemplateGame_Render() with the render.h
 *    functions: Render_Clear(), Render_Box(), Render_FillCircle(),
 *    Render_DrawLine(), Render_DrawTextAligned(), Render_DrawTextf() and so
 *    on. The screen is shown when Render returns.
 * 4. Colours are the standard palette's RENDER_WHITE, RENDER_RED and the
 *    rest. For colours of your own, make an array of up to 128 and add
 *    APP_PALETTE(YourPalette) to the app below.
 * 5. Keep splash-screen drawing inside the launcher-provided bounds.
 * 6. Make sound effects with sound.h: describe each sound as a small table
 *    of layers and play it with Sound_Play(). pong_audio.c is a complete
 *    example. A sound the engine can't make can use a patch (see sound.h),
 *    or a synth of its own on mixer channels 0 to
 *    MIXER_APPLICATION_CHANNEL_COUNT - 1 (mixer.h). Music plays
 *    through music.h; songs and instruments are generated with the scripts
 *    in tools/music_converter. Everything stops automatically when the game
 *    exits.
 * 7. Register the game: add &TemplateGame_App to the list in app_manager.c
 *    and raise NUM_APPS in app_manager.h.
 */

#include "template_game.h"

#include "app_manager.h"
#include "open_sans.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Draw the shared template scene into the specified bounds.
 *
 * Keeping splash and game artwork in one helper prevents the two versions from
 * drifting apart while the template is still minimal.
 */
static void TemplateGame_DrawScene(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height)
{
    Render_Box(Target, X, Y, Width, Height, RENDER_WHITE);
    Render_DrawTextAligned(Target, &OpenSans36, "Template", (int16_t)(X + (int16_t)(Width / 2U)), (int16_t)(Y + (((int16_t)Height - 36) / 2)),
                           RENDER_ALIGN_CENTRE, RENDER_BLACK);
}

/* -------------------------------------------------------------------------- */
/* Application functions                                                      */
/* -------------------------------------------------------------------------- */

/* The game starts: reset all game state here. */
static bool TemplateGame_Init(void)
{
    return true;
}

/* Called every frame: read controls, then move the game on by DeltaTimeMilliseconds. */
static void TemplateGame_Update(uint32_t DeltaTimeMilliseconds)
{
    (void)DeltaTimeMilliseconds;
}

/* Called every frame after Update: draw the whole screen. */
static void TemplateGame_Render(Render_TargetTypeDef *Target)
{
    TemplateGame_DrawScene(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT);
}

/* The launcher's preview of the game, drawn only within the splash area. */
static bool TemplateGame_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    TemplateGame_DrawScene(Target, APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y, APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT);

    return true;
}

/* -------------------------------------------------------------------------- */
/* Application                                                                */
/* -------------------------------------------------------------------------- */

/* Only Init and Render are required; Pause, Resume and Shutdown can be added the same way. */
const AppManager_AppTypeDef TemplateGame_App =
{
    .Init = TemplateGame_Init,
    .Update = TemplateGame_Update,
    .Render = TemplateGame_Render,
    .DrawSplashScreen = TemplateGame_DrawSplashScreen
};
