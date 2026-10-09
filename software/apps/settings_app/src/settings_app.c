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
#include "display.h"
#include "input.h"
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

#define INPUT_RIGHT_SLIDER_NUMBER          ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER        ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER      ((Input_NumberTypeDef)4U)

#define SETTINGS_APP_SLIDER_MINIMUM        (0)
#define SETTINGS_APP_SLIDER_MAXIMUM        (65535)

/* How far a slider must move before it takes control. */
#define SETTINGS_APP_SLIDER_TAKEOVER       (2000)

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
    bool AnchorValid;
    bool TakenOver;
    int32_t Anchor;
} SettingsApp_SliderTypeDef;

/**
 * @brief One button. A press already held when the page opened, or one that
 *        became part of the two-button gesture, is claimed and does nothing.
 */
typedef struct
{
    bool Down;
    bool Claimed;
    uint32_t HeldMilliseconds;
} SettingsApp_ButtonTypeDef;

typedef struct
{
    SettingsApp_RowTypeDef Row;
    SettingsApp_SliderTypeDef Slider;
    SettingsApp_ButtonTypeDef Primary;
    SettingsApp_ButtonTypeDef Secondary;
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

static uint16_t SettingsApp_MeasureTextWidth(const Font *FontAsset, const char *Text)
{
    uint32_t Codepoint;
    uint32_t Width = 0U;

    if((FontAsset == NULL) || (FontAsset->glyphs == NULL) || (Text == NULL))
    {
        return 0U;
    }

    while(*Text != '\0')
    {
        Codepoint = (uint8_t)*Text;
        Text++;

        if((Codepoint < FontAsset->firstCodepoint) || ((Codepoint - FontAsset->firstCodepoint) >= (uint32_t)FontAsset->glyphCount))
        {
            continue;
        }

        Width += FontAsset->glyphs[Codepoint - FontAsset->firstCodepoint].advance;
    }

    return (Width > UINT16_MAX) ? UINT16_MAX : (uint16_t)Width;
}

static void SettingsApp_DrawCentredText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t CentreX, int16_t Y, uint8_t Colour)
{
    Render_DrawText(Target, FontAsset, Text, (int16_t)(CentreX - ((int16_t)SettingsApp_MeasureTextWidth(FontAsset, Text) / 2)), Y, Colour);
}

static void SettingsApp_FormatPercent(uint8_t Percent, char *Buffer)
{
    uint8_t Length = 0U;

    if(Percent >= 100U)
    {
        Buffer[Length++] = (char)('0' + (Percent / 100U));
    }

    if(Percent >= 10U)
    {
        Buffer[Length++] = (char)('0' + ((Percent / 10U) % 10U));
    }

    Buffer[Length++] = (char)('0' + (Percent % 10U));
    Buffer[Length++] = '%';
    Buffer[Length] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Sliders                                                                   */
/* ------------------------------------------------------------------------- */

static void SettingsApp_ResetSlider(void)
{
    SettingsApp_State.Slider = (SettingsApp_SliderTypeDef){ 0 };
}

/* Returns true once the slider has moved far enough from where it was to take control. */
static bool SettingsApp_SliderTakenOver(SettingsApp_SliderTypeDef *Slider, int32_t Value)
{
    if(Slider->TakenOver)
    {
        return true;
    }

    if(!Slider->AnchorValid)
    {
        Slider->Anchor = Value;
        Slider->AnchorValid = true;
        return false;
    }

    if(((Value - Slider->Anchor) >= SETTINGS_APP_SLIDER_TAKEOVER) || ((Slider->Anchor - Value) >= SETTINGS_APP_SLIDER_TAKEOVER))
    {
        Slider->TakenOver = true;
    }

    return Slider->TakenOver;
}

/* Full slider travel onto a percentage range, low at the bottom of the travel. */
static uint8_t SettingsApp_SliderToPercent(int32_t Value, uint32_t Minimum, uint32_t Maximum)
{
    if(Value < SETTINGS_APP_SLIDER_MINIMUM)
    {
        Value = SETTINGS_APP_SLIDER_MINIMUM;
    }
    else if(Value > SETTINGS_APP_SLIDER_MAXIMUM)
    {
        Value = SETTINGS_APP_SLIDER_MAXIMUM;
    }

    return (uint8_t)(Minimum + ((((uint32_t)Value * (Maximum - Minimum)) + (SETTINGS_APP_SLIDER_MAXIMUM / 2U)) / SETTINGS_APP_SLIDER_MAXIMUM));
}

/* ------------------------------------------------------------------------- */
/* Input                                                                     */
/* ------------------------------------------------------------------------- */

/* The right slider sets the selected row's value. */
static void SettingsApp_UpdateSlider(void)
{
    int32_t Value;
    uint8_t Percent;

    if((SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE) ||
       !Input_GetValue(INPUT_RIGHT_SLIDER_NUMBER, &Value) ||
       !SettingsApp_SliderTakenOver(&SettingsApp_State.Slider, Value))
    {
        return;
    }

    if(SettingsApp_State.Row == SETTINGS_APP_ROW_BRIGHTNESS)
    {
        System_SetBrightness(SettingsApp_SliderToPercent(Value, SYSTEM_BRIGHTNESS_MINIMUM_PERCENT, SYSTEM_PERCENT_MAXIMUM));
        return;
    }

    Percent = SettingsApp_SliderToPercent(Value, 0U, SYSTEM_PERCENT_MAXIMUM);

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

/* Returns true when an unclaimed press of the button has just been released. */
static bool SettingsApp_UpdateButton(SettingsApp_ButtonTypeDef *Button, Input_NumberTypeDef Input, uint32_t DeltaTimeMilliseconds)
{
    int32_t Value;
    const bool Down = Input_GetValue(Input, &Value) && (Value != 0);
    const bool Released = !Down && Button->Down && !Button->Claimed;

    if(Down && !Button->Down)
    {
        Button->Claimed = false;
        Button->HeldMilliseconds = 0U;
    }
    else if(Down && (Button->HeldMilliseconds < UINT32_MAX - DeltaTimeMilliseconds))
    {
        Button->HeldMilliseconds += DeltaTimeMilliseconds;
    }

    Button->Down = Down;

    return Released;
}

static void SettingsApp_ClaimButtons(void)
{
    SettingsApp_State.Primary = (SettingsApp_ButtonTypeDef){ .Down = true, .Claimed = true };
    SettingsApp_State.Secondary = (SettingsApp_ButtonTypeDef){ .Down = true, .Claimed = true };
}

static void SettingsApp_SelectRow(int Direction)
{
    SettingsApp_State.Row = (SettingsApp_RowTypeDef)(((int)SettingsApp_State.Row + (int)SETTINGS_APP_ROW_COUNT + Direction) % (int)SETTINGS_APP_ROW_COUNT);
    SettingsApp_ResetSlider();
}

/* True while primary is erasing: held, unclaimed, on the erase row. */
static bool SettingsApp_Erasing(void)
{
    return (SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE) && SettingsApp_State.Primary.Down && !SettingsApp_State.Primary.Claimed;
}

static void SettingsApp_UpdateButtons(uint32_t DeltaTimeMilliseconds)
{
    const bool PrimaryReleased = SettingsApp_UpdateButton(&SettingsApp_State.Primary, INPUT_PRIMARY_BUTTON_NUMBER, DeltaTimeMilliseconds);
    const bool SecondaryReleased = SettingsApp_UpdateButton(&SettingsApp_State.Secondary, INPUT_SECONDARY_BUTTON_NUMBER, DeltaTimeMilliseconds);

    /* Both together belong to the system gesture, and cancel an erase. */
    if(SettingsApp_State.Primary.Down && SettingsApp_State.Secondary.Down)
    {
        SettingsApp_State.Primary.Claimed = true;
        SettingsApp_State.Secondary.Claimed = true;
        return;
    }

    if(SettingsApp_Erasing() && (SettingsApp_State.Primary.HeldMilliseconds >= SETTINGS_APP_ERASE_HOLD_MS))
    {
        SettingsApp_State.Primary.Claimed = true;
        SettingsApp_State.EraseSucceeded = System_EraseSavedData();
        SettingsApp_State.MessageMilliseconds = SETTINGS_APP_MESSAGE_MS;
        return;
    }

    /* On the erase row, letting go of a hold cancels it rather than moving on. */
    if(PrimaryReleased && ((SettingsApp_State.Row != SETTINGS_APP_ROW_ERASE) || (SettingsApp_State.Primary.HeldMilliseconds < SETTINGS_APP_TAP_MS)))
    {
        SettingsApp_SelectRow(1);
    }

    if(SecondaryReleased)
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

    Render_DrawText(Target, FontAsset, Text, (int16_t)(RightX - (int16_t)SettingsApp_MeasureTextWidth(FontAsset, Text)), (int16_t)(RowY + TextY), Colour);
}

/* The bar spans the setting's range: empty at Minimum, full at 100%. */
static void SettingsApp_DrawLevelRow(Render_TargetTypeDef *Target, SettingsApp_RowTypeDef Row, const char *Label, uint8_t Percent, uint8_t Minimum, uint8_t Colour)
{
    const int16_t RowY = SettingsApp_DrawRowPanel(Target, Row, Label, COLOUR_WHITE);
    const uint8_t ValueColour = (SettingsApp_State.Row == Row) ? Colour : COLOUR_GREY;
    char Text[5];

    SettingsApp_FormatPercent(Percent, Text);
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
        Progress = (SettingsApp_State.Primary.HeldMilliseconds * 1000U) / SETTINGS_APP_ERASE_HOLD_MS;
    }

    SettingsApp_DrawRowBar(Target, RowY, Progress, COLOUR_RED);
}

static void SettingsApp_DrawScene(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef Screen = { 0, 0, RENDER_WIDTH, RENDER_HEIGHT };
    const int16_t CentreX = (int16_t)(RENDER_WIDTH / 2U);
    const char *Hint = "RIGHT SLIDER: ADJUST";

    Render_FillRect(Target, &Screen, COLOUR_BACKGROUND);
    SettingsApp_DrawCentredText(Target, &OpenSansBold36, "SETTINGS", CentreX, 24, COLOUR_WHITE);

    SettingsApp_DrawLevelRow(Target, SETTINGS_APP_ROW_VOLUME, "VOLUME", System_GetVolume(), 0U, COLOUR_CYAN);
    SettingsApp_DrawLevelRow(Target, SETTINGS_APP_ROW_BRIGHTNESS, "BRIGHTNESS", System_GetBrightness(), SYSTEM_BRIGHTNESS_MINIMUM_PERCENT, COLOUR_YELLOW);
    SettingsApp_DrawEraseRow(Target);

    if(SettingsApp_State.Row == SETTINGS_APP_ROW_ERASE)
    {
        Hint = SettingsApp_Erasing() ? "KEEP HOLDING TO ERASE" : "HOLD PRIMARY FOR 5 SECONDS TO ERASE";
    }

    SettingsApp_DrawCentredText(Target, &OpenSansBold20, Hint, CentreX, 396, COLOUR_WHITE);
    SettingsApp_DrawCentredText(Target, &OpenSans16, "PRIMARY: NEXT     SECONDARY: BACK     HOLD BOTH: MENU", CentreX, 436, COLOUR_GREY);
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

    SettingsApp_DrawCentredText(Target, &OpenSansBold36, "SETTINGS", CentreX, (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 250), COLOUR_WHITE);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool SettingsApp_Init(void)
{
    Display_ColourTypeDef Palette[APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT];

    if(!SettingsApp_GetSplashScreenPalette(Palette) || !Display_SetPalette(0U, Palette, APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT))
    {
        return false;
    }

    SettingsApp_State = (SettingsApp_StateTypeDef){ 0 };
    SettingsApp_State.VolumeBlipStep = (uint8_t)(System_GetVolume() / 10U);

    /* The hold that started this page is still down; letting go must do nothing. */
    SettingsApp_ClaimButtons();
    SettingsApp_ResetSlider();

    SettingsApp_Paused = false;
    SettingsApp_Initialized = true;

    return true;
}

void SettingsApp_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!SettingsApp_Initialized || SettingsApp_Paused)
    {
        return;
    }

    SettingsApp_State.MessageMilliseconds = (SettingsApp_State.MessageMilliseconds > DeltaTimeMilliseconds) ? (SettingsApp_State.MessageMilliseconds - DeltaTimeMilliseconds) : 0U;

    SettingsApp_UpdateButtons(DeltaTimeMilliseconds);
    SettingsApp_UpdateSlider();
}

bool SettingsApp_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL)
    {
        return false;
    }

    for(uint16_t Index = 0U; Index < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; Index++)
    {
        Palette[Index] = 0U;
    }

    for(uint16_t Index = 0U; Index < (uint16_t)(sizeof(SettingsApp_Palette) / sizeof(SettingsApp_Palette[0])); Index++)
    {
        Palette[Index] = SettingsApp_Palette[Index];
    }

    return true;
}

bool SettingsApp_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    SettingsApp_DrawSplashScene(Target);

    return true;
}

void SettingsApp_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;

    if(!SettingsApp_Initialized || SettingsApp_Paused)
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

    Render_ResetClipRect();
    SettingsApp_DrawScene(&Target);

    (void)Display_PresentFrame(Frame);
}

void SettingsApp_Pause(void)
{
    (void)System_SaveSettings();
    SettingsApp_Paused = true;
}

void SettingsApp_Resume(void)
{
    if(SettingsApp_Initialized)
    {
        /* After a pause, the slider takes control again only once moved. */
        SettingsApp_ResetSlider();
        SettingsApp_ClaimButtons();
        SettingsApp_Paused = false;
    }
}

void SettingsApp_Shutdown(void)
{
    (void)System_SaveSettings();
    SettingsApp_Initialized = false;
    SettingsApp_Paused = false;
}
