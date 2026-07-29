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
 * 1. Add persistent game state near the module-level variables.
 * 2. Read controls and update gameplay in TemplateGame_Update().
 * 3. Add drawing helpers above TemplateGame_Render().
 * 4. Replace the two-colour palette as artwork is introduced.
 * 5. Keep splash-screen rendering inside the launcher-provided bounds.
 */

#include "template_game.h"

#include "app_manager.h"
#include "display.h"
#include "open_sans.h"
#include "render.h"

#include <limits.h>
#include <stddef.h>

/*
 * Application palette indices.
 *
 * Games may use indices 0 through 127. Indices 128 through 255 are reserved for
 * launcher UI colours and must not be used by application artwork.
 */
enum
{
    COLOUR_BACKGROUND = 0U,
    COLOUR_TEXT = 1U
};

/*
 * The application manager copies this palette into the application-owned
 * section of the display palette before drawing the splash screen or launching
 * the game.
 *
 * Display colours use 0x00RRGGBB format.
 */
static const Display_ColourTypeDef TemplateGame_Palette[] =
{
    0x00FFFFFFU, /* White background. */
    0x00000000U  /* Black text. */
};

_Static_assert((sizeof(TemplateGame_Palette) / sizeof(TemplateGame_Palette[0])) <= APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT, "Template Game palette exceeds the reserved application palette range.");

static bool TemplateGame_Initialized;
static bool TemplateGame_Paused;

/**
 * @brief Measure the horizontal advance of a single-line string.
 *
 * Render_DrawText() accepts a starting position but does not center text
 * automatically. This helper totals the advance value of every glyph so the
 * caller can calculate a centered X coordinate.
 *
 * Unsupported characters are replaced with '?' when that glyph exists.
 *
 * @param FontAsset Font used to measure the text.
 * @param Text Null-terminated text string.
 *
 * @return Text width in pixels, clamped to UINT16_MAX.
 */
static uint16_t TemplateGame_MeasureTextWidth(const Font *FontAsset, const char *Text)
{
    uint32_t Codepoint;
    uint32_t GlyphIndex;
    uint32_t Width = 0U;

    if((FontAsset == NULL) || (FontAsset->glyphs == NULL) || (Text == NULL))
    {
        return 0U;
    }

    while(*Text != '\0')
    {
        Codepoint = (uint8_t)*Text;
        Text++;

        if((Codepoint == (uint32_t)'\n') || (Codepoint == (uint32_t)'\r'))
        {
            continue;
        }

        if((Codepoint < FontAsset->firstCodepoint) || ((Codepoint - FontAsset->firstCodepoint) >= (uint32_t)FontAsset->glyphCount))
        {
            Codepoint = (uint32_t)'?';

            if((Codepoint < FontAsset->firstCodepoint) || ((Codepoint - FontAsset->firstCodepoint) >= (uint32_t)FontAsset->glyphCount))
            {
                continue;
            }
        }

        GlyphIndex = Codepoint - FontAsset->firstCodepoint;
        Width += FontAsset->glyphs[GlyphIndex].advance;
    }

    return (Width > UINT16_MAX) ? UINT16_MAX : (uint16_t)Width;
}

/**
 * @brief Draw the shared template scene into the specified bounds.
 *
 * Keeping splash and game artwork in one helper prevents the two versions from
 * drifting apart while the template is still minimal.
 *
 * @param Target Destination render target.
 * @param Bounds Rectangle to fill and use for text centering.
 */
static void TemplateGame_DrawScene(Render_TargetTypeDef *Target, const Render_RectTypeDef *Bounds)
{
    static const char Title[] = "Template";
    const uint16_t TextWidth = TemplateGame_MeasureTextWidth(&OpenSans36, Title);
    const int16_t TextX = (int16_t)(Bounds->X + (((int16_t)Bounds->Width - (int16_t)TextWidth) / 2));
    const int16_t TextY = (int16_t)(Bounds->Y + (((int16_t)Bounds->Height - 36) / 2));

    Render_FillRect(Target, Bounds, COLOUR_BACKGROUND);
    Render_DrawText(Target, &OpenSans36, Title, TextX, TextY, COLOUR_TEXT);
}

bool TemplateGame_Init(void)
{
    /*
     * Initialize all game-owned state here. Keep hardware and launcher state
     * outside the game unless the application contract explicitly assigns it.
     */
    TemplateGame_Paused = false;
    TemplateGame_Initialized = true;

    return true;
}

void TemplateGame_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!TemplateGame_Initialized || TemplateGame_Paused)
    {
        return;
    }

    /*
     * Add input handling, timers, physics, scoring, and other game logic here.
     * DeltaTimeMilliseconds is intentionally unused by the blank template.
     */
    (void)DeltaTimeMilliseconds;
}

bool TemplateGame_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL)
    {
        return false;
    }

    /*
     * Clear the entire application-owned palette range first. This gives unused
     * entries a deterministic value and avoids carrying colours from another
     * application.
     */
    for(uint16_t PaletteIndex = 0U; PaletteIndex < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; PaletteIndex++)
    {
        Palette[PaletteIndex] = 0U;
    }

    for(uint16_t PaletteIndex = 0U; PaletteIndex < (uint16_t)(sizeof(TemplateGame_Palette) / sizeof(TemplateGame_Palette[0])); PaletteIndex++)
    {
        Palette[PaletteIndex] = TemplateGame_Palette[PaletteIndex];
    }

    return true;
}

bool TemplateGame_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef SplashBounds =
    {
        APP_MANAGER_SPLASH_SCREEN_X,
        APP_MANAGER_SPLASH_SCREEN_Y,
        APP_MANAGER_SPLASH_SCREEN_WIDTH,
        APP_MANAGER_SPLASH_SCREEN_HEIGHT
    };

    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    /*
     * The launcher owns this target and presents it after all launcher UI and
     * splash artwork are complete. Do not call Display_AcquireFrame() or
     * Display_PresentFrame() from a splash-screen function.
     */
    TemplateGame_DrawScene(Target, &SplashBounds);

    return true;
}

void TemplateGame_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    Render_RectTypeDef ScreenBounds;

    if(!TemplateGame_Initialized || TemplateGame_Paused)
    {
        return;
    }

    Frame = Display_AcquireFrame();

    if(Frame == NULL)
    {
        return;
    }

    Target.Pixels = Frame->Pixels;
    Target.Width = Frame->Width;
    Target.Height = Frame->Height;
    Target.StridePixels = Frame->StridePixels;

    ScreenBounds.X = 0;
    ScreenBounds.Y = 0;
    ScreenBounds.Width = Target.Width;
    ScreenBounds.Height = Target.Height;

    /*
     * Reset clipping before drawing a complete frame. A previous renderer may
     * have left a smaller clip rectangle active.
     */
    Render_ResetClipRect();
    TemplateGame_DrawScene(&Target, &ScreenBounds);

    (void)Display_PresentFrame(Frame);
}

void TemplateGame_Pause(void)
{
    TemplateGame_Paused = true;
}

void TemplateGame_Resume(void)
{
    if(TemplateGame_Initialized)
    {
        TemplateGame_Paused = false;
    }
}

void TemplateGame_Shutdown(void)
{
    /*
     * Release dynamically allocated or externally owned resources here when a
     * future game introduces them.
     */
    TemplateGame_Initialized = false;
    TemplateGame_Paused = false;
}