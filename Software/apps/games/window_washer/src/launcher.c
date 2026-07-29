/**
 * @file launcher.c
 * @brief DualSlide startup animation and application launcher.
 *
 * Sequence:
 *  1. Blank white screen.
 *  2. Six colour bars rise from the bottom.
 *  3. The bars descend and directly reveal the launcher.
 *  4. The launcher remains displayed.
 */

#include "launcher.h"

#include "app_manager.h"
#include "display.h"
#include "input.h"
#include "render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAUNCHER_MAX_DELTA_TIME_MS              (60U)

#define LAUNCHER_UI_PALETTE_START_INDEX         (128U)
#define LAUNCHER_UI_PALETTE_ENTRY_COUNT         (128U)

#define LAUNCHER_WHITE_END_MS                   (700U)
#define LAUNCHER_BARS_RISE_END_MS               (2300U)
#define LAUNCHER_BARS_HOLD_END_MS               (2550U)
#define LAUNCHER_BARS_FALL_END_MS               (4050U)

#define LAUNCHER_BAR_COUNT                      (6U)
#define LAUNCHER_BAR_STAGGER_MS                 (140U)
#define LAUNCHER_BAR_TRAVEL_MS                  (700U)

#define INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)

/*
 * The slider must enter a narrow hard-stop zone to change applications, then
 * return well away from that zone before another change can occur.
 */
#define LAUNCHER_SLIDER_MINIMUM                  (0)
#define LAUNCHER_SLIDER_MAXIMUM                  (65535)
#define LAUNCHER_SLIDER_TOP_TRIGGER              (2500)
#define LAUNCHER_SLIDER_BOTTOM_TRIGGER           (63035)
#define LAUNCHER_SLIDER_TOP_RELEASE              (9000)
#define LAUNCHER_SLIDER_BOTTOM_RELEASE           (56535)

/*
 * Vertical pixel centres of the two molded TV buttons. The slider end
 * effector travels between these points.
 */
#define SLIDER_TOP_LIMIT                         (112)
#define SLIDER_BOTTOM_LIMIT                      (368)

#define SLIDER_BUTTON_CAP_TRAVEL                 (8)
#define SLIDER_BUTTON_CLICK_DISTANCE             (18)

typedef enum
{
    LAUNCHER_PHASE_WHITE = 0,
    LAUNCHER_PHASE_BARS_RISE,
    LAUNCHER_PHASE_BARS_HOLD,
    LAUNCHER_PHASE_BARS_FALL,
    LAUNCHER_PHASE_MENU
} Launcher_PhaseTypeDef;

typedef struct
{
    uint32_t ElapsedMilliseconds;
    Launcher_PhaseTypeDef Phase;
    int SelectedApplication;
    int16_t SliderEndEffectorY;
    int SplashPaletteApplication;
    bool LeftSliderArmed;
    bool RightSliderArmed;
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
    COLOUR_YELLOW
};

static Launcher_StateTypeDef Launcher_State;
static Display_ColourTypeDef Launcher_Palette[256U];
static uint32_t Launcher_PendingDeltaTimeMilliseconds;
static bool Launcher_Initialized;
static bool Launcher_Paused;

static uint32_t Launcher_ClampUnsigned(uint32_t Value, uint32_t Minimum, uint32_t Maximum);

static void Launcher_DrawMenuScreen(Render_TargetTypeDef *Target);

static void Launcher_UpdateMenuInput(void);

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

static void Launcher_Reset(void)
{
    Launcher_State.ElapsedMilliseconds = 0U;
    Launcher_State.Phase = LAUNCHER_PHASE_WHITE;
    Launcher_State.SelectedApplication = 0;
    Launcher_State.SliderEndEffectorY = (int16_t)((SLIDER_TOP_LIMIT + SLIDER_BOTTOM_LIMIT) / 2);
    Launcher_State.SplashPaletteApplication = -1;
    Launcher_State.LeftSliderArmed = true;
    Launcher_State.RightSliderArmed = true;
}

static void Launcher_UpdatePhase(void)
{
    const uint32_t Elapsed = Launcher_State.ElapsedMilliseconds;

    if(Elapsed < LAUNCHER_WHITE_END_MS)
    {
        Launcher_State.Phase = LAUNCHER_PHASE_WHITE;
    }
    else if(Elapsed < LAUNCHER_BARS_RISE_END_MS)
    {
        Launcher_State.Phase = LAUNCHER_PHASE_BARS_RISE;
    }
    else if(Elapsed < LAUNCHER_BARS_HOLD_END_MS)
    {
        Launcher_State.Phase = LAUNCHER_PHASE_BARS_HOLD;
    }
    else if(Elapsed < LAUNCHER_BARS_FALL_END_MS)
    {
        Launcher_State.Phase = LAUNCHER_PHASE_BARS_FALL;
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
    bool SelectionChanged = false;

    if(Launcher_State.Phase != LAUNCHER_PHASE_MENU)
    {
        return;
    }

    if(Input_Get_Value(INPUT_LEFT_SLIDER_NUMBER, &LeftSliderValue))
    {
        LeftSliderAvailable = true;

        SelectionChanged = Launcher_ProcessSliderHardStops(LeftSliderValue, &Launcher_State.LeftSliderArmed);
    }

    if(Input_Get_Value(INPUT_RIGHT_SLIDER_NUMBER, &RightSliderValue))
    {
        Launcher_State.SliderEndEffectorY = Launcher_MapSliderToEndEffectorY(RightSliderValue);

        if(SelectionChanged)
        {
            if(!Launcher_State.RightSliderArmed && (RightSliderValue >= LAUNCHER_SLIDER_TOP_RELEASE) && (RightSliderValue <= LAUNCHER_SLIDER_BOTTOM_RELEASE))
            {
                Launcher_State.RightSliderArmed = true;
            }
        }
        else
        {
            (void)Launcher_ProcessSliderHardStops(RightSliderValue, &Launcher_State.RightSliderArmed);
        }
    }
    else if(LeftSliderAvailable)
    {
        Launcher_State.SliderEndEffectorY = Launcher_MapSliderToEndEffectorY(LeftSliderValue);
    }
}

static void Launcher_UpdateSimulation(uint32_t DeltaTimeMilliseconds)
{
    if(Launcher_State.ElapsedMilliseconds < LAUNCHER_BARS_FALL_END_MS)
    {
        Launcher_State.ElapsedMilliseconds += DeltaTimeMilliseconds;

        if(Launcher_State.ElapsedMilliseconds > LAUNCHER_BARS_FALL_END_MS)
        {
            Launcher_State.ElapsedMilliseconds = LAUNCHER_BARS_FALL_END_MS;
        }
    }

    Launcher_UpdatePhase();
    Launcher_UpdateMenuInput();
}

/* ------------------------------------------------------------------------- */
/* Opening animation                                                         */
/* ------------------------------------------------------------------------- */

static void Launcher_DrawWhiteField(Render_TargetTypeDef *Target)
{
    Launcher_DrawFullScreen(Target, COLOUR_WHITE);
}

static uint16_t Launcher_GetBarVisibleHeight(uint8_t BarIndex, bool Rising)
{
    uint32_t LocalElapsed;

    if(Rising)
    {
        const uint32_t PhaseElapsed = Launcher_State.ElapsedMilliseconds -
            LAUNCHER_WHITE_END_MS;

        const uint32_t Start = (uint32_t)BarIndex *
            LAUNCHER_BAR_STAGGER_MS;

        if(PhaseElapsed <= Start)
        {
            return 0U;
        }

        LocalElapsed = PhaseElapsed - Start;

        return (uint16_t)Launcher_EaseInOut(LocalElapsed, LAUNCHER_BAR_TRAVEL_MS, RENDER_HEIGHT);
    }

    {
        const uint32_t PhaseElapsed = Launcher_State.ElapsedMilliseconds -
            LAUNCHER_BARS_HOLD_END_MS;

        const uint32_t Start = (uint32_t)BarIndex *
            LAUNCHER_BAR_STAGGER_MS;

        if(PhaseElapsed <= Start)
        {
            return RENDER_HEIGHT;
        }

        LocalElapsed = PhaseElapsed - Start;

        return (uint16_t)(RENDER_HEIGHT - Launcher_EaseInOut(LocalElapsed, LAUNCHER_BAR_TRAVEL_MS, RENDER_HEIGHT));
    }
}

static void Launcher_DrawColourBars(Render_TargetTypeDef *Target, bool Rising, bool FullyVisible)
{
    static const uint8_t BarColours[LAUNCHER_BAR_COUNT] = {
        COLOUR_RED,
        COLOUR_GREEN,
        COLOUR_BLUE,
        COLOUR_CYAN,
        COLOUR_MAGENTA,
        COLOUR_YELLOW
    };

    if(Rising || FullyVisible)
    {
        Launcher_DrawFullScreen(Target, COLOUR_WHITE);
    }
    else
    {
        Launcher_DrawMenuScreen(Target);
    }

    for(uint8_t Index = 0U; Index < LAUNCHER_BAR_COUNT; Index++)
    {
        const uint16_t Height = FullyVisible
                ? RENDER_HEIGHT
                : Launcher_GetBarVisibleHeight(Index, Rising);

        const uint16_t BarWidth = (uint16_t)(RENDER_WIDTH / LAUNCHER_BAR_COUNT);

        const int16_t X = (int16_t)(Index * BarWidth);

        const uint16_t Width = Index == (LAUNCHER_BAR_COUNT - 1U)
                ? (uint16_t)(RENDER_WIDTH - X)
                : BarWidth;

        if(Height > 0U)
        {
            const Render_RectTypeDef Bar = {
                X,
                (int16_t)(RENDER_HEIGHT - Height),
                Width,
                Height
            };

            Render_FillRect(Target, &Bar, BarColours[Index]);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Launcher screen                                                           */
/* ------------------------------------------------------------------------- */

static void Launcher_DrawSelectedApplicationSplash(Render_TargetTypeDef *Target)
{
    const uint16_t ApplicationIndex = (uint16_t)Launcher_State.SelectedApplication;

    if((Target == NULL) || (Target->Pixels == NULL) || (NUM_APPS == 0U))
    {
        return;
    }

    if(Launcher_State.SplashPaletteApplication != Launcher_State.SelectedApplication)
    {
        if(!AppManager_GetAppSplashScreenPalette(ApplicationIndex, &Launcher_Palette[0U]))
        {
            return;
        }

        if(!Display_SetPalette(0U, &Launcher_Palette[0U], 128U))
        {
            return;
        }

        Launcher_State.SplashPaletteApplication = Launcher_State.SelectedApplication;
    }

    (void)AppManager_DrawAppSplashScreen(ApplicationIndex, Target);
}

static void Launcher_DrawMenuScreen(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef ScreenOpening = {
        60,
        60,
        680U,
        360U
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

    /*
     * Render the selected application's splash screen only within the CRT
     * opening. Splash-screen callbacks must preserve the active clip region.
     */
    Render_SetClipRect(&ScreenOpening);
    Launcher_DrawSelectedApplicationSplash(Target);
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

        int16_t UpperDistance = (int16_t)(EndEffectorY - SLIDER_TOP_LIMIT);
        int16_t LowerDistance = (int16_t)(SLIDER_BOTTOM_LIMIT - EndEffectorY);

        int16_t UpperCapPress = 0;
        int16_t LowerCapPress = 0;

        if(UpperDistance < 0)
        {
            UpperDistance = 0;
        }

        if(LowerDistance < 0)
        {
            LowerDistance = 0;
        }

        if(UpperDistance < SLIDER_BUTTON_CLICK_DISTANCE)
        {
            UpperCapPress = (int16_t)(((SLIDER_BUTTON_CLICK_DISTANCE - UpperDistance) * SLIDER_BUTTON_CAP_TRAVEL) / SLIDER_BUTTON_CLICK_DISTANCE);
        }

        if(LowerDistance < SLIDER_BUTTON_CLICK_DISTANCE)
        {
            LowerCapPress = (int16_t)(((SLIDER_BUTTON_CLICK_DISTANCE - LowerDistance) * SLIDER_BUTTON_CAP_TRAVEL) / SLIDER_BUTTON_CLICK_DISTANCE);
        }

        {
            const Render_RectTypeDef ChannelTopShadow = {
                744,
                76,
                44U,
                5U
            };

            const Render_RectTypeDef ChannelLeftShadow = {
                744,
                76,
                5U,
                328U
            };

            const Render_RectTypeDef ChannelInterior = {
                749,
                81,
                34U,
                318U
            };

            const Render_RectTypeDef ChannelBottomHighlight = {
                744,
                399,
                44U,
                5U
            };

            const Render_RectTypeDef ChannelRightHighlight = {
                783,
                76,
                5U,
                328U
            };

            const Render_RectTypeDef UpperBlend = {
                749,
                72,
                34U,
                4U
            };

            const Render_RectTypeDef LowerBlend = {
                749,
                404,
                34U,
                4U
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
                751,
                (int16_t)(SLIDER_TOP_LIMIT - 24),
                26U,
                28U
            };

            const Render_RectTypeDef UpperSocketShadow = {
                751,
                (int16_t)(SLIDER_TOP_LIMIT - 24),
                26U,
                5U
            };

            const Render_RectTypeDef LowerSocket = {
                751,
                (int16_t)(SLIDER_BOTTOM_LIMIT - 4),
                26U,
                28U
            };

            const Render_RectTypeDef LowerSocketShadow = {
                751,
                (int16_t)(SLIDER_BOTTOM_LIMIT - 4),
                26U,
                5U
            };

            Render_FillRect(Target, &UpperSocket, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &UpperSocketShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &LowerSocket, COLOUR_NEAR_BLACK);
            Render_FillRect(Target, &LowerSocketShadow, COLOUR_BEZEL_DARK);
        }

        {
            const Render_RectTypeDef EffectorStick = {
                772,
                (int16_t)(EndEffectorY - 3),
                28U,
                6U
            };

            const Render_RectTypeDef EffectorBallCentre = {
                754,
                (int16_t)(EndEffectorY - 7),
                20U,
                14U
            };

            const Render_RectTypeDef EffectorBallTop = {
                758,
                (int16_t)(EndEffectorY - 10),
                12U,
                3U
            };

            const Render_RectTypeDef EffectorBallBottom = {
                758,
                (int16_t)(EndEffectorY + 7),
                12U,
                3U
            };

            const Render_RectTypeDef EffectorBallUpperSide = {
                756,
                (int16_t)(EndEffectorY - 9),
                16U,
                2U
            };

            const Render_RectTypeDef EffectorBallLowerSide = {
                756,
                (int16_t)(EndEffectorY + 7),
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
                752,
                (int16_t)(SLIDER_TOP_LIMIT - 9 - UpperCapPress),
                24U,
                18U
            };

            const Render_RectTypeDef UpperCap = {
                750,
                (int16_t)(SLIDER_TOP_LIMIT - 11 - UpperCapPress),
                24U,
                18U
            };

            const Render_RectTypeDef UpperCapHighlight = {
                754,
                (int16_t)(SLIDER_TOP_LIMIT - 7 - UpperCapPress),
                16U,
                3U
            };

            const Render_RectTypeDef LowerCapShadow = {
                752,
                (int16_t)(SLIDER_BOTTOM_LIMIT - 9 + LowerCapPress),
                24U,
                18U
            };

            const Render_RectTypeDef LowerCap = {
                750,
                (int16_t)(SLIDER_BOTTOM_LIMIT - 11 + LowerCapPress),
                24U,
                18U
            };

            const Render_RectTypeDef LowerCapHighlight = {
                754,
                (int16_t)(SLIDER_BOTTOM_LIMIT - 7 + LowerCapPress),
                16U,
                3U
            };

            Render_FillRect(Target, &UpperCapShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &UpperCap, COLOUR_RED);
            Render_FillRect(Target, &UpperCapHighlight, COLOUR_WHITE);

            Render_FillRect(Target, &LowerCapShadow, COLOUR_BEZEL_DARK);
            Render_FillRect(Target, &LowerCap, COLOUR_RED);
            Render_FillRect(Target, &LowerCapHighlight, COLOUR_WHITE);
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

    Render_ResetClipRect();
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
        0x006C624FU, /* Dark beige bezel. */
        0x00E83B3BU, /* Red. */
        0x0037C95AU, /* Green. */
        0x003D5CDEU, /* Blue. */
        0x003BD5D5U, /* Cyan. */
        0x00D44CCBU, /* Magenta. */
        0x00F0D93CU  /* Yellow. */
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

        case LAUNCHER_PHASE_BARS_RISE:
            Launcher_DrawColourBars(&Target, true, false);
            break;

        case LAUNCHER_PHASE_BARS_HOLD:
            Launcher_DrawColourBars(&Target, true, true);
            break;

        case LAUNCHER_PHASE_BARS_FALL:
            Launcher_DrawColourBars(&Target, false, false);
            break;

        case LAUNCHER_PHASE_MENU:
        default:
            Launcher_DrawMenuScreen(&Target);
            break;
    }

    (void)Display_PresentFrame(Frame);
}

void Launcher_Pause(void)
{
    Launcher_Paused = true;
}

void Launcher_Resume(void)
{
    Launcher_Paused = false;
}

void Launcher_Shutdown(void)
{
    Launcher_Initialized = false;
    Launcher_Paused = false;
    Launcher_PendingDeltaTimeMilliseconds = 0U;
}