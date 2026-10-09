/**
 * @file settings_app.c
 * @brief Settings application: screen brightness, volume, and erasing scores.
 *
 * A menu of three rows:
 *  - Volume and brightness: while selected, the right slider sets them.
 *  - Erase saved scores: while selected, holding primary for five seconds
 *    erases them, with a bar filling as it goes.
 * Tap primary for the next row and secondary for the previous one. Both held
 * together belong to the system (back to the menu), so then nothing here acts.
 *
 * The right slider does nothing until it moves a little after a row is
 * selected, so a setting never jumps to wherever the slider was left.
 */

#include "settings_app.h"

#include "app_manager.h"
#include "controls.h"
#include "display.h"
#include "open_sans.h"
#include "open_sans_bold.h"
#include "render.h"
#include "sound.h"
#include "system.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Menu rows. */
#define SETTINGS_APP_ROW_X                 (100)
#define SETTINGS_APP_ROW_TOP_Y             (92)
#define SETTINGS_APP_ROW_WIDTH             (600U)
#define SETTINGS_APP_ROW_HEIGHT            (84U)
#define SETTINGS_APP_ROW_PITCH             (100)
#define SETTINGS_APP_ROW_PADDING           (24)
#define SETTINGS_APP_ROW_BORDER            (3)
#define SETTINGS_APP_ROW_BAR_Y             (58)
#define SETTINGS_APP_ROW_BAR_HEIGHT        (12U)

/* Erasing takes a long hold of primary; a shorter press is a tap. */
#define SETTINGS_APP_ERASE_HOLD_MS         (5000U)
#define SETTINGS_APP_TAP_MS                (400U)

/* How long the result of an erase stays on screen. */
#define SETTINGS_APP_MESSAGE_MS            (2000U)


/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

enum
{
    COLOUR_BACKGROUND = 0U,
    COLOUR_PANEL,
    COLOUR_PANEL_SELECTED,
    COLOUR_TRACK,
    COLOUR_WHITE,
    COLOUR_GREY,
    COLOUR_GREEN,
    COLOUR_YELLOW,
    COLOUR_CYAN,
    COLOUR_RED,
    COLOUR_RED_LIGHT,
    COLOUR_BLACK
};

/**
 * @brief A slider's take-over state: it controls nothing until moved.
 */
typedef enum
{
    SETTINGS_APP_ROW_VOLUME = 0,
    SETTINGS_APP_ROW_BRIGHTNESS,
    SETTINGS_APP_ROW_ERASE,
    SETTINGS_APP_ROW_COUNT
} SettingsApp_RowTypeDef;

typedef struct
{
    SettingsApp_RowTypeDef Row;
    bool EraseDone;
    bool EraseSucceeded;
    uint32_t MessageMilliseconds;
    uint8_t VolumeBlipStep;
} SettingsApp_StateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const Display_ColourTypeDef SettingsApp_Palette[] =
{
    0x00101820U, /* Background. */
    0x001C2834U, /* Panel. */
    0x00283A4CU, /* Selected panel. */
    0x00303C48U, /* Bar track. */
    0x00FFFFFFU, /* White. */
    0x00A0A8B0U, /* Grey. */
    0x0040E070U, /* Green. */
    0x00FFD040U, /* Yellow. */
    0x0040D0F0U, /* Cyan. */
    0x00E03030U, /* Red. */
    0x00FF7070U, /* Light red. */
    0x00000000U  /* Black. */
};

_Static_assert((sizeof(SettingsApp_Palette) / sizeof(SettingsApp_Palette[0])) <= APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT, "Settings palette exceeds the reserved application palette range.");

static SettingsApp_StateTypeDef SettingsApp_State;
static bool SettingsApp_Initialized;
static bool SettingsApp_Paused;

/* Volume preview: a short beep, so the volume can be heard while it is set. */
static const Sound_LayerTypeDef SettingsApp_BlipLayers[] = {
    { .Hz = 880.0f, .Decay = 0.08f, .Volume = 0.3f },
};
static const Sound_TypeDef SettingsApp_Blip = { SOUND_LAYERS(SettingsApp_BlipLayers), .Single = true };

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Sliders                                                                   */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Input                                                                     */
/* ------------------------------------------------------------------------- */

/* The right slider sets the selected row's value, once it has been moved. */
static void SettingsApp_UpdateSlider(void)
{
    uint8_t Percent;

    if((SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE) || !Controls_SliderMoved(CONTROLS_RIGHT_SLIDER))
    {
        return;
    }

    if(SettingsApp_State.Row == SETTINGS_APP_ROW_BRIGHTNESS)
    {
        System_SetBrightness((uint8_t)Controls_SliderBetween(CONTROLS_RIGHT_SLIDER, SYSTEM_BRIGHTNESS_MINIMUM_PERCENT, SYSTEM_PERCENT_MAXIMUM));
        return;
    }

    Percent = (uint8_t)Controls_SliderBetween(CONTROLS_RIGHT_SLIDER, 0, SYSTEM_PERCENT_MAXIMUM);

    if(Percent != System_GetVolume())
    {
        System_SetVolume(Percent);

        /* A blip each time the volume crosses a tenth of its range. */
        if((Percent / 10U) != SettingsApp_State.VolumeBlipStep)
        {
            SettingsApp_State.VolumeBlipStep = (uint8_t)(Percent / 10U);
            (void)Sound_Play(&SettingsApp_Blip);
        }
    }
}

/* A new row leaves its setting alone until the slider is moved. */
static void SettingsApp_SelectRow(int Direction)
{
    SettingsApp_State.Row = (SettingsApp_RowTypeDef)(((int)SettingsApp_State.Row + (int)SETTINGS_APP_ROW_COUNT + Direction) % (int)SETTINGS_APP_ROW_COUNT);
    Controls_ResetSliderMoved(CONTROLS_RIGHT_SLIDER);
}

/* True while primary is erasing: held on the erase row, and not yet used up by an erase. */
static bool SettingsApp_Erasing(void)
{
    return (SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE) && Controls_IsDown(CONTROLS_PRIMARY) && !SettingsApp_State.EraseDone;
}

/* Holding both buttons is the system gesture: controls.h stops reporting them, which also cancels an erase. */
static void SettingsApp_UpdateButtons(void)
{
    if(Controls_WasPressed(CONTROLS_PRIMARY))
    {
        SettingsApp_State.EraseDone = false;
    }

    if(SettingsApp_Erasing() && (Controls_HeldMilliseconds(CONTROLS_PRIMARY) >= SETTINGS_APP_ERASE_HOLD_MS))
    {
        SettingsApp_State.EraseDone = true;
        SettingsApp_State.EraseSucceeded = System_EraseSavedData();
        SettingsApp_State.MessageMilliseconds = SETTINGS_APP_MESSAGE_MS;
        return;
    }

    /* On the erase row, letting go of a hold cancels it rather than moving on. */
    if(Controls_WasReleased(CONTROLS_PRIMARY) && !SettingsApp_State.EraseDone &&
       ((SettingsApp_State.Row != SETTINGS_APP_ROW_ERASE) || (Controls_HeldMilliseconds(CONTROLS_PRIMARY) < SETTINGS_APP_TAP_MS)))
    {
        SettingsApp_SelectRow(1);
    }

    if(Controls_WasReleased(CONTROLS_SECONDARY))
    {
        SettingsApp_SelectRow(-1);
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------- */

/* A horizontal bar along the bottom of a row, filled to Permille. */
static void SettingsApp_DrawRowBar(Render_TargetTypeDef *Target, int16_t RowY, uint32_t Permille, uint8_t Colour)
{
    const uint16_t Width = (uint16_t)(SETTINGS_APP_ROW_WIDTH - (2U * SETTINGS_APP_ROW_PADDING));
    const Render_RectTypeDef Track = { (int16_t)(SETTINGS_APP_ROW_X + SETTINGS_APP_ROW_PADDING), (int16_t)(RowY + SETTINGS_APP_ROW_BAR_Y), Width, SETTINGS_APP_ROW_BAR_HEIGHT };
    const Render_RectTypeDef Fill = { Track.X, Track.Y, (uint16_t)(((uint32_t)Width * ((Permille > 1000U) ? 1000U : Permille)) / 1000U), SETTINGS_APP_ROW_BAR_HEIGHT };

    Render_FillRect(Target, &Track, COLOUR_TRACK);

    if(Fill.Width > 0U)
    {
        Render_FillRect(Target, &Fill, Colour);
    }
}

/* A row's panel, outlined when selected, and its label; returns the row's top edge. */
static int16_t SettingsApp_DrawRowPanel(Render_TargetTypeDef *Target, SettingsApp_RowTypeDef Row, const char *Label, uint8_t LabelColour)
{
    const int16_t RowY = (int16_t)(SETTINGS_APP_ROW_TOP_Y + ((int16_t)Row * SETTINGS_APP_ROW_PITCH));
    const bool Selected = (SettingsApp_State.Row == Row);
    const Render_RectTypeDef Border = { (int16_t)(SETTINGS_APP_ROW_X - SETTINGS_APP_ROW_BORDER), (int16_t)(RowY - SETTINGS_APP_ROW_BORDER),
                                        (uint16_t)(SETTINGS_APP_ROW_WIDTH + (2U * SETTINGS_APP_ROW_BORDER)), (uint16_t)(SETTINGS_APP_ROW_HEIGHT + (2U * SETTINGS_APP_ROW_BORDER)) };
    const Render_RectTypeDef Panel = { SETTINGS_APP_ROW_X, RowY, SETTINGS_APP_ROW_WIDTH, SETTINGS_APP_ROW_HEIGHT };

    if(Selected)
    {
        Render_FillRect(Target, &Border, COLOUR_WHITE);
    }

    Render_FillRect(Target, &Panel, Selected ? COLOUR_PANEL_SELECTED : COLOUR_PANEL);
    Render_DrawText(Target, &OpenSansBold28, Label, (int16_t)(SETTINGS_APP_ROW_X + SETTINGS_APP_ROW_PADDING), (int16_t)(RowY + 12), Selected ? LabelColour : COLOUR_GREY);

    return RowY;
}

/* Text right-aligned against a row's padding. */
static void SettingsApp_DrawRowValue(Render_TargetTypeDef *Target, int16_t RowY, const Font *FontAsset, const char *Text, int16_t TextY, uint8_t Colour)
{
    const int16_t RightX = (int16_t)(SETTINGS_APP_ROW_X + (int16_t)SETTINGS_APP_ROW_WIDTH - SETTINGS_APP_ROW_PADDING);

    Render_DrawText(Target, FontAsset, Text, (int16_t)(RightX - (int16_t)Render_TextWidth(FontAsset, Text)), (int16_t)(RowY + TextY), Colour);
}

/* The bar spans the setting's range: empty at Minimum, full at 100%. */
static void SettingsApp_DrawLevelRow(Render_TargetTypeDef *Target, SettingsApp_RowTypeDef Row, const char *Label, uint8_t Percent, uint8_t Minimum, uint8_t Colour)
{
    const int16_t RowY = SettingsApp_DrawRowPanel(Target, Row, Label, COLOUR_WHITE);
    const uint8_t ValueColour = (SettingsApp_State.Row == Row) ? Colour : COLOUR_GREY;
    char Text[5];

    (void)Render_FormatText(Text, sizeof(Text), "%u%%", (unsigned int)Percent);
    SettingsApp_DrawRowValue(Target, RowY, &OpenSansBold28, Text, 12, ValueColour);
    SettingsApp_DrawRowBar(Target, RowY, ((uint32_t)(Percent - Minimum) * 1000U) / (SYSTEM_PERCENT_MAXIMUM - Minimum), ValueColour);
}

static void SettingsApp_DrawEraseRow(Render_TargetTypeDef *Target)
{
    const int16_t RowY = SettingsApp_DrawRowPanel(Target, SETTINGS_APP_ROW_ERASE, "ERASE SAVED SCORES", COLOUR_RED_LIGHT);
    uint32_t Progress = 0U;

    if(SettingsApp_State.MessageMilliseconds > 0U)
    {
        SettingsApp_DrawRowValue(Target, RowY, &OpenSansBold20, SettingsApp_State.EraseSucceeded ? "ERASED" : "FAILED", 18,
                                 SettingsApp_State.EraseSucceeded ? COLOUR_GREEN : COLOUR_RED);
    }

    if(SettingsApp_Erasing())
    {
        Progress = (Controls_HeldMilliseconds(CONTROLS_PRIMARY) * 1000U) / SETTINGS_APP_ERASE_HOLD_MS;
    }

    SettingsApp_DrawRowBar(Target, RowY, Progress, COLOUR_RED);
}

static void SettingsApp_DrawScene(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef Screen = { 0, 0, RENDER_WIDTH, RENDER_HEIGHT };
    const int16_t CentreX = (int16_t)(RENDER_WIDTH / 2U);
    const char *Hint = "RIGHT SLIDER: ADJUST";

    Render_FillRect(Target, &Screen, COLOUR_BACKGROUND);
    Render_DrawTextAligned(Target, &OpenSansBold36, "SETTINGS", CentreX, 24, RENDER_ALIGN_CENTRE, COLOUR_WHITE);

    SettingsApp_DrawLevelRow(Target, SETTINGS_APP_ROW_VOLUME, "VOLUME", System_GetVolume(), 0U, COLOUR_CYAN);
    SettingsApp_DrawLevelRow(Target, SETTINGS_APP_ROW_BRIGHTNESS, "BRIGHTNESS", System_GetBrightness(), SYSTEM_BRIGHTNESS_MINIMUM_PERCENT, COLOUR_YELLOW);
    SettingsApp_DrawEraseRow(Target);

    if(SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE)
    {
        Hint = SettingsApp_Erasing() ? "KEEP HOLDING TO ERASE" : "HOLD PRIMARY FOR 5 SECONDS TO ERASE";
    }

    Render_DrawTextAligned(Target, &OpenSansBold20, Hint, CentreX, 396, RENDER_ALIGN_CENTRE, COLOUR_WHITE);
    Render_DrawTextAligned(Target, &OpenSans16, "PRIMARY: NEXT     SECONDARY: BACK     HOLD BOTH: MENU", CentreX, 436, RENDER_ALIGN_CENTRE, COLOUR_GREY);
}

/* The launcher card: a gear above the title. */
static void SettingsApp_DrawSplashScene(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef Bounds = { APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y, APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT };
    const int16_t CentreX = (int16_t)(APP_MANAGER_SPLASH_SCREEN_X + (APP_MANAGER_SPLASH_SCREEN_WIDTH / 2U));
    const int16_t GearY = (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 140);
    Render_PointTypeDef Points[24];

    Render_FillRect(Target, &Bounds, COLOUR_BACKGROUND);

    /* Teeth: eight squares around the rim. */
    for(uint8_t Tooth = 0U; Tooth < 8U; Tooth++)
    {
        const float Angle = 0.78539816f * (float)Tooth;
        const float AlongX = cosf(Angle);
        const float AlongY = sinf(Angle);

        Points[0] = (Render_PointTypeDef){ (int16_t)(CentreX + (50.0f * AlongX) - (14.0f * AlongY)), (int16_t)(GearY + (50.0f * AlongY) + (14.0f * AlongX)) };
        Points[1] = (Render_PointTypeDef){ (int16_t)(CentreX + (82.0f * AlongX) - (12.0f * AlongY)), (int16_t)(GearY + (82.0f * AlongY) + (12.0f * AlongX)) };
        Points[2] = (Render_PointTypeDef){ (int16_t)(CentreX + (82.0f * AlongX) + (12.0f * AlongY)), (int16_t)(GearY + (82.0f * AlongY) - (12.0f * AlongX)) };
        Points[3] = (Render_PointTypeDef){ (int16_t)(CentreX + (50.0f * AlongX) + (14.0f * AlongY)), (int16_t)(GearY + (50.0f * AlongY) - (14.0f * AlongX)) };
        (void)Render_DrawPolygon(Target, Points, 4U, COLOUR_GREY);
    }

    /* The body, then the hole through it. */
    for(uint8_t Ring = 0U; Ring < 2U; Ring++)
    {
        const float Radius = (Ring == 0U) ? 64.0f : 26.0f;

        for(uint8_t Index = 0U; Index < 24U; Index++)
        {
            const float Angle = 0.26179939f * (float)Index;

            Points[Index] = (Render_PointTypeDef){ (int16_t)(CentreX + (Radius * cosf(Angle))), (int16_t)(GearY + (Radius * sinf(Angle))) };
        }

        (void)Render_DrawPolygon(Target, Points, 24U, (Ring == 0U) ? COLOUR_GREY : COLOUR_BACKGROUND);
    }

    Render_DrawTextAligned(Target, &OpenSansBold36, "SETTINGS", CentreX, (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 250), RENDER_ALIGN_CENTRE, COLOUR_WHITE);
}

/* -------------------------------------------------------------------------- */
/* Application functions                                                      */
/* -------------------------------------------------------------------------- */

static bool SettingsApp_Init(void)
{
    SettingsApp_State = (SettingsApp_StateTypeDef){ 0 };
    SettingsApp_State.VolumeBlipStep = (uint8_t)(System_GetVolume() / 10U);

    SettingsApp_Paused = false;
    SettingsApp_Initialized = true;

    return true;
}

static void SettingsApp_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!SettingsApp_Initialized || SettingsApp_Paused)
    {
        return;
    }

    SettingsApp_State.MessageMilliseconds = (SettingsApp_State.MessageMilliseconds > DeltaTimeMilliseconds) ? (SettingsApp_State.MessageMilliseconds - DeltaTimeMilliseconds) : 0U;

    SettingsApp_UpdateButtons();
    SettingsApp_UpdateSlider();
}

static bool SettingsApp_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    SettingsApp_DrawSplashScene(Target);

    return true;
}

static void SettingsApp_Render(Render_TargetTypeDef *Target)
{
    if(!SettingsApp_Initialized || SettingsApp_Paused)
    {
        return;
    }

    SettingsApp_DrawScene(Target);
}

static void SettingsApp_Pause(void)
{
    (void)System_SaveSettings();
    SettingsApp_Paused = true;
}

static void SettingsApp_Resume(void)
{
    if(SettingsApp_Initialized)
    {
        SettingsApp_Paused = false;
    }
}

static void SettingsApp_Shutdown(void)
{
    (void)System_SaveSettings();
    SettingsApp_Initialized = false;
    SettingsApp_Paused = false;
}

/* -------------------------------------------------------------------------- */
/* Application                                                                */
/* -------------------------------------------------------------------------- */

const AppManager_AppTypeDef SettingsApp_App =
{
    .Init = SettingsApp_Init,
    .Update = SettingsApp_Update,
    .Render = SettingsApp_Render,
    .DrawSplashScreen = SettingsApp_DrawSplashScreen,
    .Pause = SettingsApp_Pause,
    .Resume = SettingsApp_Resume,
    .Shutdown = SettingsApp_Shutdown,
    APP_PALETTE(SettingsApp_Palette)
};
