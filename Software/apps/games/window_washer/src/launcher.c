/**
 * @file launcher.c
 * @brief DualSlide startup animation and application launcher.
 *
 * Sequence:
 *  1. White uSolder brand screen inside the CRT.
 *  2. CRT channel-change shutters close over the brand, then open.
 *  3. The selected game preview remains displayed inside the CRT.
 */

#include "launcher.h"

#include "app_manager.h"
#include "display.h"
#include "input.h"
#include "open_sans.h"
#include "open_sans_bold.h"
#include "render.h"
#include "avenir_next_demi_usolder.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAUNCHER_MAX_DELTA_TIME_MS              (60U)

#define LAUNCHER_UI_PALETTE_START_INDEX         (128U)
#define LAUNCHER_UI_PALETTE_ENTRY_COUNT         (128U)

#define LAUNCHER_WHITE_END_MS                   (1000U)
/* Palette changes occur only while the closing CRT shutters cover the preview. */
#define LAUNCHER_PREVIEW_SHUTTER_TRAVEL_MS       (120U)
#define LAUNCHER_PREVIEW_SHUTTER_HOLD_MS         (50U)

#define LAUNCHER_SCREEN_OPENING_X                (60)
#define LAUNCHER_SCREEN_OPENING_Y                (60)
#define LAUNCHER_SCREEN_OPENING_WIDTH            (680U)
#define LAUNCHER_SCREEN_OPENING_HEIGHT           (360U)

/* Top-screen tab positions. */
#define LAUNCHER_SETTINGS_BUTTON_X                (240)
#define LAUNCHER_CHARGING_INDICATOR_X             (360)
#define LAUNCHER_START_BUTTON_X                   (480)
#define LAUNCHER_SCREEN_TAB_Y                     (0)
#define LAUNCHER_SETTINGS_BUTTON_WIDTH            (80)
#define LAUNCHER_CHARGING_INDICATOR_WIDTH         (80)
#define LAUNCHER_START_BUTTON_WIDTH               (80)
#define LAUNCHER_SCREEN_TAB_CAP_HEIGHT            (14U)
#define LAUNCHER_SCREEN_TAB_SIDE_ANGLE            (8)
#define LAUNCHER_SCREEN_TAB_BORDER_WIDTH          (5)
#define LAUNCHER_SCREEN_TAB_BOTTOM_BORDER_HEIGHT  (5)
#define LAUNCHER_SCREEN_TAB_LABEL_Y               (18)

#define LAUNCHER_BATTERY_DISPLAY_X                (365)
#define LAUNCHER_BATTERY_DISPLAY_Y                (21)
#define LAUNCHER_BATTERY_DISPLAY_WIDTH            (70U)
#define LAUNCHER_BATTERY_DISPLAY_HEIGHT           (24U)

/* Vertical position of the centered lower-bezel uSolder wordmark. */
#define LAUNCHER_BRAND_TEXT_Y                     (425)

/* Baseline position of the centered opening-screen uSolder wordmark. */
#define LAUNCHER_STARTUP_BRAND_TEXT_Y             (200)

#define INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)
#define INPUT_BATTERY_NUMBER          ((Input_NumberTypeDef)5U)
#define INPUT_USB_POWER_NUMBER        ((Input_NumberTypeDef)6U)

/*
 * The slider must enter a narrow hard-stop zone to change applications, then
 * return well away from that zone before another change can occur.
 */
#define LAUNCHER_SLIDER_MINIMUM                  (0)
#define LAUNCHER_SLIDER_MAXIMUM                  (65535)
#define LAUNCHER_SLIDER_TOP_TRIGGER              (1000)
#define LAUNCHER_SLIDER_BOTTOM_TRIGGER           (65535-1000)
#define LAUNCHER_SLIDER_TOP_RELEASE              (1500)
#define LAUNCHER_SLIDER_BOTTOM_RELEASE           (65535-1500)

/*
 * The effector stops against the pressure plates rather than passing through
 * them. Button-centre coordinates are separate from the travel limits so the
 * visual mechanism remains mechanically believable.
 */
#define SLIDER_TOP_BUTTON_CENTER                 (45)
#define SLIDER_BOTTOM_BUTTON_CENTER              (480-45)
#define SLIDER_TOP_LIMIT                         (60)
#define SLIDER_BOTTOM_LIMIT                      (480-60)

#define LAUNCHER_SELECTOR_SLOT_TOP_Y    (20)
#define LAUNCHER_SELECTOR_SLOT_BOTTOM_Y (480-20)

#define SLIDER_BUTTON_CAP_TRAVEL                 (8)
#define SLIDER_EFFECTOR_TOP_EXTENT               (10)
#define SLIDER_EFFECTOR_BOTTOM_EXTENT            (10)
#define SLIDER_TOP_BUTTON_CONTACT_Y              (SLIDER_TOP_BUTTON_CENTER + 10)
#define SLIDER_BOTTOM_BUTTON_CONTACT_Y           (SLIDER_BOTTOM_BUTTON_CENTER - 10)

typedef enum
{
    LAUNCHER_PHASE_WHITE = 0,
    LAUNCHER_PHASE_STARTUP_CHANNEL_CHANGE,
    LAUNCHER_PHASE_MENU
} Launcher_PhaseTypeDef;

typedef enum
{
    LAUNCHER_PREVIEW_TRANSITION_NONE = 0,
    LAUNCHER_PREVIEW_TRANSITION_CLOSE,
    LAUNCHER_PREVIEW_TRANSITION_COVERED,
    LAUNCHER_PREVIEW_TRANSITION_APPLY_PALETTE,
    LAUNCHER_PREVIEW_TRANSITION_OPEN
} Launcher_PreviewTransitionTypeDef;

typedef enum
{
    LAUNCHER_SCREEN_CONTENT_LOGO = 0,
    LAUNCHER_SCREEN_CONTENT_PREVIEW
} Launcher_ScreenContentTypeDef;

typedef struct
{
    uint32_t ElapsedMilliseconds;
    Launcher_PhaseTypeDef Phase;
    int SelectedApplication;
    int16_t SliderEndEffectorY;
    int SplashPaletteApplication;
    Launcher_PreviewTransitionTypeDef PreviewTransition;
    uint32_t PreviewTransitionElapsedMilliseconds;
    bool RightSliderArmed;
    bool PrimaryButtonPressed;
    bool PrimaryButtonLaunchArmed;
    bool SecondaryButtonPressed;
    bool USBPowerPresent;
    bool StartupChannelChangeStarted;
    bool StartupChannelChangeCompleted;
} Launcher_StateTypeDef;

enum
{
    COLOUR_BLACK = LAUNCHER_UI_PALETTE_START_INDEX,
    COLOUR_NEAR_BLACK,
    COLOUR_WHITE,
    COLOUR_PANEL,
    COLOUR_BEZEL_LIGHT,
    COLOUR_BEZEL,
    COLOUR_BEZEL_DARK,
    COLOUR_RED,
    COLOUR_GREEN,
    COLOUR_BLUE,
    COLOUR_CYAN,
    COLOUR_MAGENTA,
    COLOUR_YELLOW,
    COLOUR_LIGHT_GREY,
    COLOUR_RED_DARK,
    COLOUR_RED_LIGHT,
    COLOUR_BLUE_DARK,
    COLOUR_ORANGE_DARK,
    COLOUR_ORANGE,
    COLOUR_USOLDER_BLUE,
    COLOUR_USOLDER_NAVY
};

static Launcher_StateTypeDef Launcher_State;
static Display_ColourTypeDef Launcher_Palette[256U];
static uint32_t Launcher_PendingDeltaTimeMilliseconds;
static bool Launcher_Initialized;
static bool Launcher_Paused;

static uint32_t Launcher_ClampUnsigned(uint32_t Value, uint32_t Minimum, uint32_t Maximum);
static uint16_t Launcher_MeasureTextWidth(const Font *FontAsset, const char *Text);

static void Launcher_DrawMenuScreen(Render_TargetTypeDef *Target, Launcher_ScreenContentTypeDef Content);
static void Launcher_DrawScreenTab(Render_TargetTypeDef *Target, int16_t X, uint16_t Width, const char *Label, uint8_t CoverColour);
static void Launcher_DrawBrandName(Render_TargetTypeDef *Target);
static void Launcher_DrawStartupBrandName(Render_TargetTypeDef *Target);
static void Launcher_DrawBatteryPercentage(Render_TargetTypeDef *Target);

static void Launcher_UpdateMenuInput(void);
static void Launcher_UpdatePrimaryButton(void);
static void Launcher_UpdateSecondaryButton(void);
static void Launcher_UpdateUSBPowerStatus(void);
static void Launcher_UpdatePreviewTransition(uint32_t DeltaTimeMilliseconds);
static bool Launcher_SetSplashPalette(uint16_t ApplicationIndex);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint32_t Launcher_ClampUnsigned(uint32_t Value, uint32_t Minimum, uint32_t Maximum)
{
    if(Value < Minimum)
    {
        return Minimum;
    }

    if(Value > Maximum)
    {
        return Maximum;
    }

    return Value;
}

static uint16_t Launcher_MeasureTextWidth(const Font *FontAsset, const char *Text)
{
    uint32_t Codepoint;
    const FontGlyph *Glyph;
    uint32_t Width = 0U;

    if((FontAsset == NULL) || (FontAsset->glyphs == NULL) || (Text == NULL))
    {
        return 0U;
    }

    while(*Text != '\0')
    {
        Codepoint = (uint8_t)*Text;
        Text++;

        Glyph = Font_GetGlyph(FontAsset, Codepoint);

        if(Glyph == NULL)
        {
            continue;
        }

        Width += Glyph->advance;
    }

    return (Width > UINT16_MAX) ? UINT16_MAX : (uint16_t)Width;
}

static uint32_t Launcher_MapRange(uint32_t Value, uint32_t InputMinimum, uint32_t InputMaximum, uint32_t OutputMinimum, uint32_t OutputMaximum)
{
    if(Value <= InputMinimum)
    {
        return OutputMinimum;
    }

    if(Value >= InputMaximum)
    {
        return OutputMaximum;
    }

    return OutputMinimum + (((Value - InputMinimum) * (OutputMaximum - OutputMinimum)) / (InputMaximum - InputMinimum));
}

static uint32_t Launcher_EaseInOut(uint32_t Elapsed, uint32_t Duration, uint32_t Scale)
{
    const uint32_t T = Launcher_MapRange(Elapsed, 0U, Duration, 0U, 1024U);

    const uint32_t T2 = (T * T) / 1024U;
    const uint32_t Smooth = ((3U * T2) - ((2U * T2 * T) / 1024U));

    return (Smooth * Scale) / 1024U;
}

static void Launcher_DrawFullScreen(Render_TargetTypeDef *Target, uint8_t Colour)
{
    const Render_RectTypeDef Screen = {
        0,
        0,
        RENDER_WIDTH,
        RENDER_HEIGHT
    };

    Render_FillRect(Target, &Screen, Colour);
}

static void Launcher_DrawScreenTab(Render_TargetTypeDef *Target, int16_t X, uint16_t Width, const char *Label, uint8_t CoverColour)
{
    const uint16_t LabelWidth = Launcher_MeasureTextWidth(&OpenSansBold20, Label);
    const int16_t LabelX = (int16_t)(X + (((int16_t)Width - (int16_t)LabelWidth) / 2));
    const Render_PointTypeDef BezelPoints[] = {
        { X, LAUNCHER_SCREEN_TAB_Y },
        { (int16_t)(X + (int16_t)Width - 1), LAUNCHER_SCREEN_TAB_Y },
        { (int16_t)(X + (int16_t)Width - LAUNCHER_SCREEN_TAB_SIDE_ANGLE - 1), (int16_t)(LAUNCHER_SCREEN_TAB_Y + LAUNCHER_SCREEN_TAB_CAP_HEIGHT - 1) },
        { (int16_t)(X + LAUNCHER_SCREEN_TAB_SIDE_ANGLE), (int16_t)(LAUNCHER_SCREEN_TAB_Y + LAUNCHER_SCREEN_TAB_CAP_HEIGHT - 1) }
    };
    const Render_PointTypeDef CoverPoints[] = {
        { (int16_t)(X + LAUNCHER_SCREEN_TAB_BORDER_WIDTH), LAUNCHER_SCREEN_TAB_Y },
        { (int16_t)(X + (int16_t)Width - LAUNCHER_SCREEN_TAB_BORDER_WIDTH - 1), LAUNCHER_SCREEN_TAB_Y },
        { (int16_t)(X + (int16_t)Width - LAUNCHER_SCREEN_TAB_SIDE_ANGLE - LAUNCHER_SCREEN_TAB_BORDER_WIDTH - 1), (int16_t)(LAUNCHER_SCREEN_TAB_Y + LAUNCHER_SCREEN_TAB_CAP_HEIGHT - LAUNCHER_SCREEN_TAB_BOTTOM_BORDER_HEIGHT - 1) },
        { (int16_t)(X + LAUNCHER_SCREEN_TAB_SIDE_ANGLE + LAUNCHER_SCREEN_TAB_BORDER_WIDTH), (int16_t)(LAUNCHER_SCREEN_TAB_Y + LAUNCHER_SCREEN_TAB_CAP_HEIGHT - LAUNCHER_SCREEN_TAB_BOTTOM_BORDER_HEIGHT - 1) }
    };

    (void)Render_DrawPolygon(Target, BezelPoints, (uint8_t)(sizeof(BezelPoints) / sizeof(BezelPoints[0])), COLOUR_BEZEL_DARK);
    (void)Render_DrawPolygon(Target, CoverPoints, (uint8_t)(sizeof(CoverPoints) / sizeof(CoverPoints[0])), CoverColour);
    Render_DrawText(Target, &OpenSansBold20, Label, LabelX, LAUNCHER_SCREEN_TAB_LABEL_Y, COLOUR_BLACK);
}

static void Launcher_DrawBrandName(Render_TargetTypeDef *Target)
{
    static const char BrandNameFirstLetter[] = "u";
    static const char BrandNameRest[] = "Solder";
    const uint16_t BrandWidth = Launcher_MeasureTextWidth(&OpenSansBold28, BrandNameFirstLetter) + Launcher_MeasureTextWidth(&OpenSansBold28, BrandNameRest);
    const int16_t BrandFirstLetterX = (int16_t)(((int16_t)RENDER_WIDTH - (int16_t)BrandWidth) / 2);
    const int16_t BrandRestX = BrandFirstLetterX + Launcher_MeasureTextWidth(&OpenSansBold28, BrandNameFirstLetter);

    Render_DrawText(Target, &OpenSansBold28, BrandNameFirstLetter, BrandFirstLetterX, LAUNCHER_BRAND_TEXT_Y, COLOUR_USOLDER_BLUE);
    Render_DrawText(Target, &OpenSansBold28, BrandNameRest, BrandRestX, LAUNCHER_BRAND_TEXT_Y, COLOUR_BLACK);
}

static void Launcher_DrawStartupBrandName(Render_TargetTypeDef *Target)
{
    static const char BrandNameFirstLetter[] = "u";
    static const char BrandNameRest[] = "Solder";
    const uint16_t FirstLetterWidth = Launcher_MeasureTextWidth(&AvenirNextDemi125, BrandNameFirstLetter);
    const uint16_t BrandWidth = FirstLetterWidth + Launcher_MeasureTextWidth(&AvenirNextDemi125, BrandNameRest);
    const int16_t BrandFirstLetterX = (int16_t)(((int16_t)RENDER_WIDTH - (int16_t)BrandWidth) / 2);
    const int16_t BrandRestX = BrandFirstLetterX + (int16_t)FirstLetterWidth;

    Render_DrawText(Target, &AvenirNextDemi125, BrandNameFirstLetter, BrandFirstLetterX, LAUNCHER_STARTUP_BRAND_TEXT_Y, COLOUR_USOLDER_BLUE);
    Render_DrawText(Target, &AvenirNextDemi125, BrandNameRest, BrandRestX, LAUNCHER_STARTUP_BRAND_TEXT_Y, COLOUR_USOLDER_NAVY);
}

static void Launcher_DrawBatteryPercentage(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef BatteryBezel = {
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_X - 2),
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_Y - 2),
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_WIDTH + 4U),
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_HEIGHT + 4U)
    };
    const Render_RectTypeDef BatteryBezelTopHighlight = {
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_X - 1),
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_Y - 1),
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_WIDTH + 2U),
        1U
    };
    const Render_RectTypeDef BatteryBezelLeftHighlight = {
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_X - 1),
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_Y - 1),
        1U,
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_HEIGHT + 2U)
    };
    const Render_RectTypeDef BatteryBezelBottomShadow = {
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_X - 1),
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_Y + (int16_t)LAUNCHER_BATTERY_DISPLAY_HEIGHT),
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_WIDTH + 2U),
        1U
    };
    const Render_RectTypeDef BatteryBezelRightShadow = {
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_X + (int16_t)LAUNCHER_BATTERY_DISPLAY_WIDTH),
        (int16_t)(LAUNCHER_BATTERY_DISPLAY_Y - 1),
        1U,
        (uint16_t)(LAUNCHER_BATTERY_DISPLAY_HEIGHT + 2U)
    };
    const Render_RectTypeDef BatteryDisplay = {
        LAUNCHER_BATTERY_DISPLAY_X,
        LAUNCHER_BATTERY_DISPLAY_Y,
        LAUNCHER_BATTERY_DISPLAY_WIDTH,
        LAUNCHER_BATTERY_DISPLAY_HEIGHT
    };
    char BatteryText[5];
    int32_t BatteryPercentage;
    uint16_t TextWidth;
    int16_t TextX;
    uint8_t TextColour;

    if(!Input_Get_Value(INPUT_BATTERY_NUMBER, &BatteryPercentage))
    {
        return;
    }

    if(BatteryPercentage < 0)
    {
        BatteryPercentage = 0;
    }
    else if(BatteryPercentage > 100)
    {
        BatteryPercentage = 100;
    }

    if(BatteryPercentage >= 100)
    {
        BatteryText[0] = '1';
        BatteryText[1] = '0';
        BatteryText[2] = '0';
        BatteryText[3] = '%';
        BatteryText[4] = '\0';
    }
    else if(BatteryPercentage >= 10)
    {
        BatteryText[0] = (char)('0' + (BatteryPercentage / 10));
        BatteryText[1] = (char)('0' + (BatteryPercentage % 10));
        BatteryText[2] = '%';
        BatteryText[3] = '\0';
    }
    else
    {
        BatteryText[0] = (char)('0' + BatteryPercentage);
        BatteryText[1] = '%';
        BatteryText[2] = '\0';
    }

    if(BatteryPercentage > 30)
    {
        TextColour = COLOUR_LIGHT_GREY;
    }
    else if(BatteryPercentage > 15)
    {
        TextColour = COLOUR_ORANGE;
    }
    else
    {
        TextColour = COLOUR_RED;
    }

    TextWidth = Launcher_MeasureTextWidth(&OpenSansBold20, BatteryText);
    TextX = (int16_t)(LAUNCHER_BATTERY_DISPLAY_X + (((int16_t)LAUNCHER_BATTERY_DISPLAY_WIDTH - (int16_t)TextWidth) / 2));

    Render_FillRect(Target, &BatteryBezel, COLOUR_BEZEL_DARK);
    Render_FillRect(Target, &BatteryBezelTopHighlight, COLOUR_BEZEL_LIGHT);
    Render_FillRect(Target, &BatteryBezelLeftHighlight, COLOUR_BEZEL_LIGHT);
    Render_FillRect(Target, &BatteryBezelBottomShadow, COLOUR_NEAR_BLACK);
    Render_FillRect(Target, &BatteryBezelRightShadow, COLOUR_NEAR_BLACK);
    Render_FillRect(Target, &BatteryDisplay, COLOUR_BLACK);
    Render_DrawText(Target, &OpenSansBold20, BatteryText, TextX, LAUNCHER_SCREEN_TAB_LABEL_Y, TextColour);
}

static void Launcher_Reset(void)
{
    Launcher_State.ElapsedMilliseconds = 0U;
    Launcher_State.Phase = LAUNCHER_PHASE_WHITE;
    Launcher_State.SelectedApplication = 0;
    Launcher_State.SliderEndEffectorY = (int16_t)((SLIDER_TOP_LIMIT + SLIDER_BOTTOM_LIMIT) / 2);
    Launcher_State.SplashPaletteApplication = -1;
    Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_NONE;
    Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
    /*
     * The right slider must first leave either hard stop.  This prevents an
     * already-held slider from changing the page during startup or return.
     */
    Launcher_State.RightSliderArmed = false;
    Launcher_State.PrimaryButtonPressed = false;
    Launcher_State.PrimaryButtonLaunchArmed = false;
    Launcher_State.SecondaryButtonPressed = false;
    Launcher_State.USBPowerPresent = false;
    Launcher_State.StartupChannelChangeStarted = false;
    Launcher_State.StartupChannelChangeCompleted = false;
}

static void Launcher_UpdatePhase(void)
{
    const uint32_t Elapsed = Launcher_State.ElapsedMilliseconds;

    if(Elapsed < LAUNCHER_WHITE_END_MS)
    {
        Launcher_State.Phase = LAUNCHER_PHASE_WHITE;
    }
    else if(!Launcher_State.StartupChannelChangeStarted)
    {
        Launcher_State.StartupChannelChangeStarted = true;
        Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_CLOSE;
        Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
        Launcher_State.Phase = LAUNCHER_PHASE_STARTUP_CHANNEL_CHANGE;
    }
    else if(!Launcher_State.StartupChannelChangeCompleted)
    {
        if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_NONE)
        {
            Launcher_State.StartupChannelChangeCompleted = true;
            Launcher_State.Phase = LAUNCHER_PHASE_MENU;
        }
        else
        {
            Launcher_State.Phase = LAUNCHER_PHASE_STARTUP_CHANNEL_CHANGE;
        }
    }
    else
    {
        Launcher_State.Phase = LAUNCHER_PHASE_MENU;
    }
}

static void Launcher_SelectPreviousApplication(void)
{
    if(NUM_APPS == 0U)
    {
        Launcher_State.SelectedApplication = 0;
        return;
    }

    if(Launcher_State.SelectedApplication <= 0)
    {
        Launcher_State.SelectedApplication = (int)NUM_APPS - 1;
    }
    else
    {
        Launcher_State.SelectedApplication--;
    }
}

static void Launcher_SelectNextApplication(void)
{
    if(NUM_APPS == 0U)
    {
        Launcher_State.SelectedApplication = 0;
        return;
    }

    Launcher_State.SelectedApplication++;

    if(Launcher_State.SelectedApplication >= (int)NUM_APPS)
    {
        Launcher_State.SelectedApplication = 0;
    }
}

static bool Launcher_ProcessSliderHardStops(int32_t SliderValue, bool *Armed)
{
    if(Armed == NULL)
    {
        return false;
    }

    if(!(*Armed))
    {
        if((SliderValue >= LAUNCHER_SLIDER_TOP_RELEASE) && (SliderValue <= LAUNCHER_SLIDER_BOTTOM_RELEASE))
        {
            *Armed = true;
        }

        return false;
    }

    if(SliderValue <= LAUNCHER_SLIDER_TOP_TRIGGER)
    {
        Launcher_SelectPreviousApplication();
        *Armed = false;
        return true;
    }

    if(SliderValue >= LAUNCHER_SLIDER_BOTTOM_TRIGGER)
    {
        Launcher_SelectNextApplication();
        *Armed = false;
        return true;
    }

    return false;
}

static int16_t Launcher_MapSliderToEndEffectorY(int32_t SliderValue)
{
    if(SliderValue <= LAUNCHER_SLIDER_MINIMUM)
    {
        return SLIDER_BOTTOM_LIMIT;
    }

    if(SliderValue >= LAUNCHER_SLIDER_MAXIMUM)
    {
        return SLIDER_TOP_LIMIT;
    }

    return (int16_t)(SLIDER_BOTTOM_LIMIT - (((SliderValue - LAUNCHER_SLIDER_MINIMUM) * (SLIDER_BOTTOM_LIMIT - SLIDER_TOP_LIMIT)) / (LAUNCHER_SLIDER_MAXIMUM - LAUNCHER_SLIDER_MINIMUM)));
}

static void Launcher_UpdateMenuInput(void)
{
    int32_t LeftSliderValue;
    int32_t RightSliderValue;
    bool LeftSliderAvailable = false;
    bool RightSliderAvailable = false;
    bool SelectionChanged = false;

    if(Input_Get_Value(INPUT_LEFT_SLIDER_NUMBER, &LeftSliderValue))
    {
        LeftSliderAvailable = true;
    }

    if(Input_Get_Value(INPUT_RIGHT_SLIDER_NUMBER, &RightSliderValue))
    {
        RightSliderAvailable = true;
        Launcher_State.SliderEndEffectorY = Launcher_MapSliderToEndEffectorY(RightSliderValue);
    }
    else if(LeftSliderAvailable)
    {
        Launcher_State.SliderEndEffectorY = Launcher_MapSliderToEndEffectorY(LeftSliderValue);
    }

    if(Launcher_State.Phase != LAUNCHER_PHASE_MENU)
    {
        return;
    }

    /*
     * The selected preview is protected from repeated selection changes while
     * its shutters are moving, but the physical slider remains responsive.
     */
    if(Launcher_State.PreviewTransition != LAUNCHER_PREVIEW_TRANSITION_NONE)
    {
        return;
    }

    if(RightSliderAvailable)
    {
        SelectionChanged = Launcher_ProcessSliderHardStops(
            RightSliderValue,
            &Launcher_State.RightSliderArmed);
    }

    if(SelectionChanged)
    {
        Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_CLOSE;
        Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
    }
}

static void Launcher_UpdatePrimaryButton(void)
{
    int32_t PrimaryButtonValue;

    if(!Input_Get_Value(INPUT_PRIMARY_BUTTON_NUMBER, &PrimaryButtonValue))
    {
        Launcher_State.PrimaryButtonPressed = false;
        Launcher_State.PrimaryButtonLaunchArmed = false;
        return;
    }

    if(PrimaryButtonValue != 0)
    {
        Launcher_State.PrimaryButtonPressed = true;

        /* A press only arms launch while a stable menu preview is visible. */
        if((Launcher_State.Phase == LAUNCHER_PHASE_MENU) &&
            (Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_NONE) &&
            (NUM_APPS > 0U))
        {
            Launcher_State.PrimaryButtonLaunchArmed = true;
        }

        return;
    }

    Launcher_State.PrimaryButtonPressed = false;

    if(!Launcher_State.PrimaryButtonLaunchArmed)
    {
        return;
    }

    Launcher_State.PrimaryButtonLaunchArmed = false;

    if( (Launcher_State.Phase == LAUNCHER_PHASE_MENU) &&
        (Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_NONE) &&
        (NUM_APPS > 0U))
    {
        (void)AppManager_StartApplication((uint16_t)Launcher_State.SelectedApplication);
    }
}

static void Launcher_UpdateSecondaryButton(void)
{
    int32_t SecondaryButtonValue;

    if(!Input_Get_Value(INPUT_SECONDARY_BUTTON_NUMBER, &SecondaryButtonValue))
    {
        Launcher_State.SecondaryButtonPressed = false;
        return;
    }

    Launcher_State.SecondaryButtonPressed = SecondaryButtonValue != 0;
}

static void Launcher_UpdateUSBPowerStatus(void)
{
    int32_t USBPowerValue;

    if(!Input_Get_Value(INPUT_USB_POWER_NUMBER, &USBPowerValue))
    {
        Launcher_State.USBPowerPresent = false;
        return;
    }

    Launcher_State.USBPowerPresent = USBPowerValue != 0;
}

static void Launcher_UpdatePreviewTransition(uint32_t DeltaTimeMilliseconds)
{
    if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_CLOSE)
    {
        Launcher_State.PreviewTransitionElapsedMilliseconds += DeltaTimeMilliseconds;

        if(Launcher_State.PreviewTransitionElapsedMilliseconds >= LAUNCHER_PREVIEW_SHUTTER_TRAVEL_MS)
        {
            Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_COVERED;
            Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
        }
    }
    else if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_COVERED)
    {
        Launcher_State.PreviewTransitionElapsedMilliseconds += DeltaTimeMilliseconds;

        if(Launcher_State.PreviewTransitionElapsedMilliseconds >= LAUNCHER_PREVIEW_SHUTTER_HOLD_MS)
        {
            Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_APPLY_PALETTE;
            Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
        }
    }
    else if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_OPEN)
    {
        Launcher_State.PreviewTransitionElapsedMilliseconds += DeltaTimeMilliseconds;

        if(Launcher_State.PreviewTransitionElapsedMilliseconds >= LAUNCHER_PREVIEW_SHUTTER_TRAVEL_MS)
        {
            Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_NONE;
            Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
        }
    }
}

static void Launcher_UpdateSimulation(uint32_t DeltaTimeMilliseconds)
{
    if(Launcher_State.ElapsedMilliseconds < LAUNCHER_WHITE_END_MS)
    {
        Launcher_State.ElapsedMilliseconds += DeltaTimeMilliseconds;

        if(Launcher_State.ElapsedMilliseconds > LAUNCHER_WHITE_END_MS)
        {
            Launcher_State.ElapsedMilliseconds = LAUNCHER_WHITE_END_MS;
        }
    }

    Launcher_UpdatePhase();
    Launcher_UpdateMenuInput();
    Launcher_UpdatePreviewTransition(DeltaTimeMilliseconds);
    Launcher_UpdatePrimaryButton();
    Launcher_UpdateSecondaryButton();
    Launcher_UpdateUSBPowerStatus();
}

/* ------------------------------------------------------------------------- */
/* Opening animation                                                         */
/* ------------------------------------------------------------------------- */

static void Launcher_DrawWhiteField(Render_TargetTypeDef *Target)
{
    Launcher_DrawMenuScreen(Target, LAUNCHER_SCREEN_CONTENT_LOGO);
}

/* ------------------------------------------------------------------------- */
/* Launcher screen                                                           */
/* ------------------------------------------------------------------------- */

static bool Launcher_SetSplashPalette(uint16_t ApplicationIndex)
{
    if(NUM_APPS == 0U)
    {
        return false;
    }

    if(!AppManager_GetAppSplashScreenPalette(ApplicationIndex, &Launcher_Palette[0U]))
    {
        return false;
    }

    if(!Display_SetPalette(0U, &Launcher_Palette[0U], 128U))
    {
        return false;
    }

    Launcher_State.SplashPaletteApplication = (int)ApplicationIndex;

    return true;
}

static void Launcher_DrawSelectedApplicationSplash(Render_TargetTypeDef *Target)
{
    uint16_t ApplicationIndex;

    if((Target == NULL) || (Target->Pixels == NULL) || (NUM_APPS == 0U))
    {
        return;
    }

    /* The first preview is not replacing visible app-owned pixels. */
    if(Launcher_State.SplashPaletteApplication < 0)
    {
        if(!Launcher_SetSplashPalette((uint16_t)Launcher_State.SelectedApplication))
        {
            return;
        }
    }

    /* Keep the outgoing splash intact until the shutters fully cover it. */
    ApplicationIndex = (uint16_t)Launcher_State.SplashPaletteApplication;

    (void)AppManager_DrawAppSplashScreen(ApplicationIndex, Target);
}

static void Launcher_DrawPreviewTransition(Render_TargetTypeDef *Target)
{
    Render_RectTypeDef TopShutter = {
        LAUNCHER_SCREEN_OPENING_X,
        LAUNCHER_SCREEN_OPENING_Y,
        LAUNCHER_SCREEN_OPENING_WIDTH,
        0U
    };
    Render_RectTypeDef BottomShutter = TopShutter;
    uint16_t ShutterHeight;

    switch(Launcher_State.PreviewTransition)
    {
        case LAUNCHER_PREVIEW_TRANSITION_CLOSE:
            ShutterHeight = (uint16_t)Launcher_EaseInOut(
                Launcher_State.PreviewTransitionElapsedMilliseconds,
                LAUNCHER_PREVIEW_SHUTTER_TRAVEL_MS,
                LAUNCHER_SCREEN_OPENING_HEIGHT / 2U);
            break;

        case LAUNCHER_PREVIEW_TRANSITION_COVERED:
        case LAUNCHER_PREVIEW_TRANSITION_APPLY_PALETTE:
            ShutterHeight = LAUNCHER_SCREEN_OPENING_HEIGHT / 2U;
            break;

        case LAUNCHER_PREVIEW_TRANSITION_OPEN:
            ShutterHeight = (uint16_t)((LAUNCHER_SCREEN_OPENING_HEIGHT / 2U) - Launcher_EaseInOut(
                Launcher_State.PreviewTransitionElapsedMilliseconds,
                LAUNCHER_PREVIEW_SHUTTER_TRAVEL_MS,
                LAUNCHER_SCREEN_OPENING_HEIGHT / 2U));
            break;

        case LAUNCHER_PREVIEW_TRANSITION_NONE:
        default:
            return;
    }

    BottomShutter.Y = (int16_t)(LAUNCHER_SCREEN_OPENING_Y + LAUNCHER_SCREEN_OPENING_HEIGHT - ShutterHeight);
    TopShutter.Height = ShutterHeight;
    BottomShutter.Height = ShutterHeight;

    Render_FillRect(Target, &TopShutter, COLOUR_NEAR_BLACK);
    Render_FillRect(Target, &BottomShutter, COLOUR_NEAR_BLACK);
}

static void Launcher_DrawMenuScreen(Render_TargetTypeDef *Target, Launcher_ScreenContentTypeDef Content)
{
    const Render_RectTypeDef ScreenOpening = {
        LAUNCHER_SCREEN_OPENING_X,
        LAUNCHER_SCREEN_OPENING_Y,
        LAUNCHER_SCREEN_OPENING_WIDTH,
        LAUNCHER_SCREEN_OPENING_HEIGHT
    };

    const Render_RectTypeDef Housing = {
        0,
        0,
        RENDER_WIDTH,
        RENDER_HEIGHT
    };

    const Render_RectTypeDef OuterTopHighlight = {
        10,
        10,
        780U,
        8U
    };

    const Render_RectTypeDef OuterLeftHighlight = {
        10,
        10,
        8U,
        460U
    };

    const Render_RectTypeDef OuterBottomShadow = {
        10,
        462,
        780U,
        8U
    };

    const Render_RectTypeDef OuterRightShadow = {
        782,
        10,
        8U,
        460U
    };

    const Render_RectTypeDef RecessTop = {
        48,
        48,
        704U,
        12U
    };

    const Render_RectTypeDef RecessLeft = {
        48,
        48,
        12U,
        384U
    };

    const Render_RectTypeDef RecessBottom = {
        48,
        420,
        704U,
        12U
    };

    const Render_RectTypeDef RecessRight = {
        740,
        48,
        12U,
        384U
    };

    const Render_RectTypeDef InnerTopShadow = {
        60,
        60,
        680U,
        8U
    };

    const Render_RectTypeDef InnerLeftShadow = {
        60,
        60,
        8U,
        360U
    };

    const Render_RectTypeDef InnerBottomHighlight = {
        60,
        412,
        680U,
        8U
    };

    const Render_RectTypeDef InnerRightHighlight = {
        732,
        60,
        8U,
        360U
    };

    Render_FillRect(Target, &Housing, COLOUR_BEZEL);
    Render_FillRect(Target, &OuterTopHighlight, COLOUR_BEZEL_LIGHT);
    Render_FillRect(Target, &OuterLeftHighlight, COLOUR_BEZEL_LIGHT);
    Render_FillRect(Target, &OuterBottomShadow, COLOUR_BEZEL_DARK);
    Render_FillRect(Target, &OuterRightShadow, COLOUR_BEZEL_DARK);

    Render_FillRect(Target, &RecessTop, COLOUR_BEZEL_DARK);
    Render_FillRect(Target, &RecessLeft, COLOUR_BEZEL_DARK);
    Render_FillRect(Target, &RecessBottom, COLOUR_BEZEL_LIGHT);
    Render_FillRect(Target, &RecessRight, COLOUR_BEZEL_LIGHT);

    Render_SetClipRect(&ScreenOpening);

    if(Content == LAUNCHER_SCREEN_CONTENT_LOGO)
    {
        Render_FillRect(Target, &ScreenOpening, COLOUR_WHITE);
        Launcher_DrawStartupBrandName(Target);
    }
    else
    {
        if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_APPLY_PALETTE)
        {
            (void)Launcher_SetSplashPalette((uint16_t)Launcher_State.SelectedApplication);
        }

        /*
         * Render the selected application's splash screen only within the CRT
         * opening. Splash-screen callbacks must preserve the active clip region.
         */
        Launcher_DrawSelectedApplicationSplash(Target);
    }

    Render_ResetClipRect();

    Render_FillRect(Target, &InnerTopShadow, COLOUR_NEAR_BLACK);
    Render_FillRect(Target, &InnerLeftShadow, COLOUR_NEAR_BLACK);
    Render_FillRect(Target, &InnerBottomHighlight, COLOUR_PANEL);
    Render_FillRect(Target, &InnerRightHighlight, COLOUR_PANEL);
    
    {
        const Render_RectTypeDef TopLeftCornerA = {60, 60, 28U, 8U};
        const Render_RectTypeDef TopLeftCornerB = {60, 68, 16U, 8U};
        const Render_RectTypeDef TopLeftCornerC = {60, 76, 8U, 16U};

        const Render_RectTypeDef TopRightCornerA = {712, 60, 28U, 8U};
        const Render_RectTypeDef TopRightCornerB = {724, 68, 16U, 8U};
        const Render_RectTypeDef TopRightCornerC = {732, 76, 8U, 16U};

        const Render_RectTypeDef BottomLeftCornerA = {60, 412, 28U, 8U};
        const Render_RectTypeDef BottomLeftCornerB = {60, 404, 16U, 8U};
        const Render_RectTypeDef BottomLeftCornerC = {60, 388, 8U, 16U};

        const Render_RectTypeDef BottomRightCornerA = {712, 412, 28U, 8U};
        const Render_RectTypeDef BottomRightCornerB = {724, 404, 16U, 8U};
        const Render_RectTypeDef BottomRightCornerC = {732, 388, 8U, 16U};

        Render_FillRect(Target, &TopLeftCornerA, COLOUR_NEAR_BLACK);
        Render_FillRect(Target, &TopLeftCornerB, COLOUR_NEAR_BLACK);
        Render_FillRect(Target, &TopLeftCornerC, COLOUR_NEAR_BLACK);

        Render_FillRect(Target, &TopRightCornerA, COLOUR_NEAR_BLACK);
        Render_FillRect(Target, &TopRightCornerB, COLOUR_NEAR_BLACK);
        Render_FillRect(Target, &TopRightCornerC, COLOUR_NEAR_BLACK);

        Render_FillRect(Target, &BottomLeftCornerA, COLOUR_PANEL);
        Render_FillRect(Target, &BottomLeftCornerB, COLOUR_PANEL);
        Render_FillRect(Target, &BottomLeftCornerC, COLOUR_PANEL);

        Render_FillRect(Target, &BottomRightCornerA, COLOUR_PANEL);
        Render_FillRect(Target, &BottomRightCornerB, COLOUR_PANEL);
        Render_FillRect(Target, &BottomRightCornerC, COLOUR_PANEL);
    }

    {
        const int16_t EndEffectorY = Launcher_State.SliderEndEffectorY;

        int16_t UpperCapPress = 0;
        int16_t LowerCapPress = 0;

        /* Move a plate only after the effector's edge reaches its face. */
        if((EndEffectorY - SLIDER_EFFECTOR_TOP_EXTENT) < SLIDER_TOP_BUTTON_CONTACT_Y)
        {
            UpperCapPress = (int16_t)(SLIDER_TOP_BUTTON_CONTACT_Y - (EndEffectorY - SLIDER_EFFECTOR_TOP_EXTENT));
        }

        if((EndEffectorY + SLIDER_EFFECTOR_BOTTOM_EXTENT) > SLIDER_BOTTOM_BUTTON_CONTACT_Y)
        {
            LowerCapPress = (int16_t)((EndEffectorY + SLIDER_EFFECTOR_BOTTOM_EXTENT) - SLIDER_BOTTOM_BUTTON_CONTACT_Y);
        }

        if(UpperCapPress > SLIDER_BUTTON_CAP_TRAVEL)
        {
            UpperCapPress = SLIDER_BUTTON_CAP_TRAVEL;
        }

        if(LowerCapPress > SLIDER_BUTTON_CAP_TRAVEL)
        {
            LowerCapPress = SLIDER_BUTTON_CAP_TRAVEL;
        }

        {
            const Render_RectTypeDef ChannelTopShadow = {
                758,
                LAUNCHER_SELECTOR_SLOT_TOP_Y,
                22U,
                3U
            };

            const Render_RectTypeDef ChannelLeftShadow = {
                758,
                LAUNCHER_SELECTOR_SLOT_TOP_Y,
                3U,
                (uint16_t)(LAUNCHER_SELECTOR_SLOT_BOTTOM_Y - LAUNCHER_SELECTOR_SLOT_TOP_Y)
            };

            const Render_RectTypeDef ChannelInterior = {
                761,
                (int16_t)(LAUNCHER_SELECTOR_SLOT_TOP_Y + 3),
                16U,
                (uint16_t)(LAUNCHER_SELECTOR_SLOT_BOTTOM_Y - LAUNCHER_SELECTOR_SLOT_TOP_Y - 6)
            };

            const Render_RectTypeDef ChannelBottomHighlight = {
                758,
                (int16_t)(LAUNCHER_SELECTOR_SLOT_BOTTOM_Y - 3),
                22U,
                3U
            };

            const Render_RectTypeDef ChannelRightHighlight = {
                777,
                LAUNCHER_SELECTOR_SLOT_TOP_Y,
                3U,
                (uint16_t)(LAUNCHER_SELECTOR_SLOT_BOTTOM_Y - LAUNCHER_SELECTOR_SLOT_TOP_Y)
            };

            const Render_RectTypeDef UpperBlend = {
                761,
                (int16_t)(LAUNCHER_SELECTOR_SLOT_TOP_Y - 3),
                16U,
                3U
            };

            const Render_RectTypeDef LowerBlend = {
                761,
                LAUNCHER_SELECTOR_SLOT_BOTTOM_Y,
                16U,
                3U
            };

            Render_FillRect(Target, &ChannelInterior, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &ChannelTopShadow, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &ChannelLeftShadow, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &ChannelBottomHighlight, COLOUR_PANEL);
            Render_FillRect(Target, &ChannelRightHighlight, COLOUR_PANEL);
            Render_FillRect(Target, &UpperBlend, COLOUR_BEZEL);
            Render_FillRect(Target, &LowerBlend, COLOUR_BEZEL);
        }

        {
            const Render_RectTypeDef UpperSocket = {
                762,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 22),
                14U,
                24U
            };

            const Render_RectTypeDef UpperSocketShadow = {
                762,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 22),
                14U,
                3U
            };

            const Render_RectTypeDef LowerSocket = {
                762,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 2),
                14U,
                24U
            };

            const Render_RectTypeDef LowerSocketShadow = {
                762,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 2),
                14U,
                3U
            };

            Render_FillRect(Target, &UpperSocket, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &UpperSocketShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &LowerSocket, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &LowerSocketShadow, COLOUR_BEZEL_DARK);
        }

        {
            const Render_RectTypeDef EffectorStick = {
                779,
                (int16_t)(EndEffectorY - 3),
                21U,
                6U
            };

            const Render_RectTypeDef EffectorBallCentre = {
                760,
                (int16_t)(EndEffectorY - (SLIDER_EFFECTOR_TOP_EXTENT - 2)),
                18U,
                16U
            };

            const Render_RectTypeDef EffectorBallTop = {
                763,
                (int16_t)(EndEffectorY - SLIDER_EFFECTOR_TOP_EXTENT),
                12U,
                3U
            };

            const Render_RectTypeDef EffectorBallBottom = {
                763,
                (int16_t)(EndEffectorY + (SLIDER_EFFECTOR_BOTTOM_EXTENT - 3)),
                12U,
                3U
            };

            const Render_RectTypeDef EffectorBallUpperSide = {
                761,
                (int16_t)(EndEffectorY - (SLIDER_EFFECTOR_TOP_EXTENT - 1)),
                16U,
                2U
            };

            const Render_RectTypeDef EffectorBallLowerSide = {
                761,
                (int16_t)(EndEffectorY + (SLIDER_EFFECTOR_BOTTOM_EXTENT - 3)),
                16U,
                2U
            };

            Render_FillRect(Target, &EffectorStick, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &EffectorBallCentre, COLOUR_RED);
            Render_FillRect(Target, &EffectorBallTop, COLOUR_RED);
            Render_FillRect(Target, &EffectorBallBottom, COLOUR_RED);
            Render_FillRect(Target, &EffectorBallUpperSide, COLOUR_RED);
            Render_FillRect(Target, &EffectorBallLowerSide, COLOUR_RED);
        }

        {
            const Render_RectTypeDef UpperCapShadow = {
                762,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 10 - UpperCapPress),
                16U,
                24U
            };

            const Render_RectTypeDef UpperCapMiddle = {
                761,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 12 - UpperCapPress),
                16U,
                22U
            };

            const Render_RectTypeDef UpperCapFace = {
                763,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 9 - UpperCapPress),
                12U,
                17U
            };

            const Render_RectTypeDef UpperCapHighlight = {
                764,
                (int16_t)(SLIDER_TOP_BUTTON_CENTER - 8 - UpperCapPress),
                10U,
                2U
            };

            const Render_RectTypeDef LowerCapShadow = {
                762,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 10 + LowerCapPress),
                16U,
                24U
            };

            const Render_RectTypeDef LowerCapMiddle = {
                761,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 12 + LowerCapPress),
                16U,
                22U
            };

            const Render_RectTypeDef LowerCapFace = {
                763,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 9 + LowerCapPress),
                12U,
                17U
            };

            const Render_RectTypeDef LowerCapHighlight = {
                764,
                (int16_t)(SLIDER_BOTTOM_BUTTON_CENTER - 8 + LowerCapPress),
                10U,
                2U
            };

            Render_FillRect(Target, &UpperCapShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &UpperCapMiddle, COLOUR_RED_DARK);
            Render_FillRect(Target, &UpperCapFace, COLOUR_RED);
            Render_FillRect(Target, &UpperCapHighlight, COLOUR_RED_LIGHT);

            Render_FillRect(Target, &LowerCapShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &LowerCapMiddle, COLOUR_RED_DARK);
            Render_FillRect(Target, &LowerCapFace, COLOUR_RED);
            Render_FillRect(Target, &LowerCapHighlight, COLOUR_RED_LIGHT);
        }
    }

    Render_SetClipRect(&ScreenOpening);

        for(int16_t ScanlineY = 76; ScanlineY < 412; ScanlineY += 8)
        {
            const Render_RectTypeDef Scanline = {
                68,
                ScanlineY,
                664U,
                1U
            };

            Render_FillRect(Target, &Scanline, COLOUR_NEAR_BLACK);
        }

        Launcher_DrawPreviewTransition(Target);

    Render_ResetClipRect();

    Launcher_DrawScreenTab(
        Target,
        LAUNCHER_SETTINGS_BUTTON_X,
        LAUNCHER_SETTINGS_BUTTON_WIDTH,
        "SETTINGS",
        Launcher_State.SecondaryButtonPressed ? COLOUR_BLUE : COLOUR_BLUE_DARK);
    Launcher_DrawScreenTab(
        Target,
        LAUNCHER_CHARGING_INDICATOR_X,
        LAUNCHER_CHARGING_INDICATOR_WIDTH,
        "",
        Launcher_State.USBPowerPresent ? COLOUR_ORANGE : COLOUR_ORANGE_DARK);
    Launcher_DrawBatteryPercentage(Target);
    Launcher_DrawScreenTab(
        Target,
        LAUNCHER_START_BUTTON_X,
        LAUNCHER_START_BUTTON_WIDTH,
        "START",
        Launcher_State.PrimaryButtonPressed ? COLOUR_RED : COLOUR_RED_DARK);
    Launcher_DrawBrandName(Target);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

bool Launcher_Init(void)
{
    static const Display_ColourTypeDef BasePalette[] = {
        0x00000000U, /* Black. */
        0x0006090FU, /* Near black. */
        0x00FFFFFFU, /* White. */
        0x00101820U, /* Panel. */
        0x00E8DDBFU, /* Light beige bezel. */
        0x00CBBF9EU, /* Beige bezel. */
        0x008d916CU, /* Dark beige bezel. */
        0x00E83B3BU, /* Red. */
        0x0037C95AU, /* Green. */
        0x003D5CDEU, /* Blue. */
        0x003BD5D5U, /* Cyan. */
        0x00D44CCBU, /* Magenta. */
        0x00F0D93CU, /* Yellow. */
        0x00D8D8D8U, /* Light grey. */
        0x009E2028U, /* Dark button red. */
        0x00FF6363U, /* Button red highlight. */
        0x00253678U, /* Dark button blue. */
        0x0085521AU, /* Dark charging orange. */
        0x00FF9D27U, /* Charging orange. */
        0x001F4497U, /* uSolder blue. */
        0x00101130U  /* uSolder navy. */
    };

    for(uint16_t PaletteIndex = 0U; PaletteIndex < 256U; PaletteIndex++)
    {
        Launcher_Palette[PaletteIndex] = 0U;
    }

    for(uint16_t PaletteIndex = 0U; PaletteIndex < (uint16_t)(sizeof(BasePalette) / sizeof(BasePalette[0])); PaletteIndex++)
    {
        Launcher_Palette[LAUNCHER_UI_PALETTE_START_INDEX + PaletteIndex] = BasePalette[PaletteIndex];
    }

    _Static_assert((sizeof(BasePalette) / sizeof(BasePalette[0])) <= LAUNCHER_UI_PALETTE_ENTRY_COUNT, "Launcher UI palette exceeds the reserved upper CLUT range.");

    Launcher_PendingDeltaTimeMilliseconds = 0U;
    Launcher_Paused = false;
    Launcher_Reset();

    if(!Display_SetPalette(0U, Launcher_Palette, 256U))
    {
        Launcher_Initialized = false;
        return false;
    }

    Launcher_Initialized = true;
    return true;
}

void Launcher_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!Launcher_Initialized || Launcher_Paused)
    {
        return;
    }

    Launcher_PendingDeltaTimeMilliseconds += DeltaTimeMilliseconds;
}

void Launcher_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    uint32_t DeltaTimeMilliseconds;

    if(!Launcher_Initialized || Launcher_Paused)
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

    DeltaTimeMilliseconds = Launcher_ClampUnsigned(Launcher_PendingDeltaTimeMilliseconds, 0U, LAUNCHER_MAX_DELTA_TIME_MS);

    Launcher_PendingDeltaTimeMilliseconds = 0U;

    Launcher_UpdateSimulation(DeltaTimeMilliseconds);

    Render_ResetClipRect();

    switch(Launcher_State.Phase)
    {
        case LAUNCHER_PHASE_WHITE:
            Launcher_DrawWhiteField(&Target);
            break;

        case LAUNCHER_PHASE_STARTUP_CHANNEL_CHANGE:
            if((Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_CLOSE) ||
               (Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_COVERED))
            {
                Launcher_DrawWhiteField(&Target);
            }
            else
            {
                Launcher_DrawMenuScreen(&Target, LAUNCHER_SCREEN_CONTENT_PREVIEW);
            }
            break;

        case LAUNCHER_PHASE_MENU:
        default:
            Launcher_DrawMenuScreen(&Target, LAUNCHER_SCREEN_CONTENT_PREVIEW);
            break;
    }

    if(Display_PresentFrame(Frame))
    {
        if(Launcher_State.PreviewTransition == LAUNCHER_PREVIEW_TRANSITION_APPLY_PALETTE)
        {
            Launcher_State.PreviewTransition = LAUNCHER_PREVIEW_TRANSITION_OPEN;
            Launcher_State.PreviewTransitionElapsedMilliseconds = 0U;
        }
    }
}

void Launcher_Pause(void)
{
    Launcher_Paused = true;
}

void Launcher_Resume(void)
{
    /* Require a fresh move away from the hard stop after an application exits. */
    Launcher_State.RightSliderArmed = false;
    Launcher_Paused = false;
}

void Launcher_Shutdown(void)
{
    Launcher_Initialized = false;
    Launcher_Paused = false;
    Launcher_PendingDeltaTimeMilliseconds = 0U;
}