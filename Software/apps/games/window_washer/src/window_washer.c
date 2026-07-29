/**
 * @file window_washer.c
 * @brief Window-washer platform game demonstration for the DualSlide application system.
 */

#include "window_washer.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "open_sans.h"
#include "render.h"

#include <stddef.h>


#define INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)

#define BUILDING_LEFT_X (70)
#define BUILDING_RIGHT_X ((int16_t)RENDER_WIDTH - 70)
#define WINDOW_COLUMN_COUNT (6U)
#define WINDOW_WIDTH (70U)
#define WINDOW_HEIGHT (48U)
#define WINDOW_STEP_X (104U)
#define WINDOW_STEP_Y (92U)
#define WINDOW_GRID_WIDTH ((WINDOW_COLUMN_COUNT * WINDOW_WIDTH) + ((WINDOW_COLUMN_COUNT - 1U) * (WINDOW_STEP_X - WINDOW_WIDTH)))
#define WINDOW_GRID_LEFT_X (BUILDING_LEFT_X + (((BUILDING_RIGHT_X - BUILDING_LEFT_X) - WINDOW_GRID_WIDTH) / 2U))
#define PLATFORM_LEFT_X (170)
#define PLATFORM_RIGHT_X ((int16_t)RENDER_WIDTH - 170)
#define PLATFORM_BASE_Y (320)
#define PLATFORM_START_OFFSET_Y (200)
#define PLATFORM_THICKNESS (8U)
#define FIGURE_WIDTH (48U)
#define FIGURE_HEIGHT (82U)
#define BUCKET_HANG_OFFSET (25)
#define BUCKET_WIDTH (56U)
#define BUCKET_HEIGHT (34U)
#define FIGURE_FIXED_SCALE (256)
#define FIGURE_SLIDE_GRAVITY (240)
#define WASHER_IMAGE_SIZE (64U)
#define BALCONY_STEP_Y (276U)
#define BALCONY_VERTICAL_OFFSET (84)
#define BALCONY_THICKNESS (16U)
#define BALCONY_BUILDING_WRAP (14U)
#define BALCONY_CLEAR_START (2)
/*
 * Building scroll speed is restricted to whole pixels per rendered frame.
 * This keeps every presented frame moving by a consistent integer amount and
 * avoids the visible cadence caused by fractional pixel accumulation.
 *
 * Start times must be listed in ascending order.
 */
#define SPEED_1_PIXELS_PER_FRAME         (1U)
#define SPEED_1_START_TIME_MS            (1000ULL)

#define SPEED_2_PIXELS_PER_FRAME         (2U)
#define SPEED_2_START_TIME_MS            (30000ULL)

#define SPEED_3_PIXELS_PER_FRAME         (3U)
#define SPEED_3_START_TIME_MS            (60000ULL)

#define SPEED_4_PIXELS_PER_FRAME         (4U)
#define SPEED_4_START_TIME_MS            (120000ULL)

#define SPEED_5_PIXELS_PER_FRAME         (5U)
#define SPEED_5_START_TIME_MS            (240000ULL)

#define HOTEL_GROUND_Y         ((int16_t)RENDER_HEIGHT - 34)
#define HOTEL_ENTRANCE_TOP_Y   ((int16_t)RENDER_HEIGHT - 218)
#define HOTEL_ENTRANCE_WIDTH   (276U)
#define HOTEL_ENTRANCE_HEIGHT  (184U)
#define HOTEL_CANOPY_WIDTH     (340U)
#define HOTEL_CANOPY_HEIGHT    (18U)
#define CLEAN_WINDOW_TRACKED_COUNT (64U)
#define CLEAN_WINDOW_SPARKLE_FRAMES (45U)
#define SLIDER_TRAVEL (150)
#define MOUSE_SLIDER_PER_PIXEL (1)
#define FALL_GRAVITY_FIXED_PER_SECOND_SQUARED (1800 * FIGURE_FIXED_SCALE)
#define FALL_INITIAL_VELOCITY_FIXED_PER_SECOND  (120 * FIGURE_FIXED_SCALE)
#define FALL_RESTART_DELAY_MILLISECONDS         (500U)

#define INPUT_SLIDER_MINIMUM (0)
#define INPUT_SLIDER_MAXIMUM (65535)
#define INPUT_SLIDER_CENTRE  ((INPUT_SLIDER_MAXIMUM + 1) / 2)

#define SCORE_PANEL_WIDTH              (174U)
#define SCORE_PANEL_HEIGHT             (36U)
#define SCORE_PANEL_X                  ((int16_t)((int16_t)RENDER_WIDTH - (int16_t)SCORE_PANEL_WIDTH - 10))
#define SCORE_PANEL_Y                  ((int16_t)((int16_t)RENDER_HEIGHT - (int16_t)SCORE_PANEL_HEIGHT - 10))
#define SCORE_CURRENT_AREA_WIDTH       (78U)
#define SCORE_MINIMUM_DIGITS           (4U)
#define SCORE_MAXIMUM                  (9999U)
#define SCORE_PULSE_FRAME_COUNT        (10U)

#define SCORE_POINTS_PER_FLOOR         (1U)
#define SCORE_POINTS_PER_DIRT          (10U)

enum
{
    COLOUR_SKY = 0U,
    COLOUR_BUILDING = 1U,
    COLOUR_WINDOW_FRAME = 2U,
    COLOUR_WINDOW = 3U,
    COLOUR_PLATFORM_EDGE = 4U,
    COLOUR_PLATFORM = 5U,
    COLOUR_CABLE = 6U,
    COLOUR_BURGLAR = 7U,
    COLOUR_WASHER = 8U,
    COLOUR_SLIDER_UP = 9U,
    COLOUR_SLIDER_DOWN = 10U,
    COLOUR_CRASH = 11U,
    COLOUR_WASHER_HELMET = 12U,
    COLOUR_WASHER_FACE = 13U,
    COLOUR_WASHER_UNIFORM_DARK = 14U,
    COLOUR_WASHER_UNIFORM_LIGHT = 15U,
    COLOUR_BALCONY = 16U,
    COLOUR_BALCONY_HIGHLIGHT = 17U,
    COLOUR_DIRT = 18U,
    COLOUR_SPARKLE = 19U,
    COLOUR_BUILDING_SHADOW = 20U,
    COLOUR_BUILDING_HIGHLIGHT = 21U,
    COLOUR_FACADE_TRIM = 22U,
    COLOUR_WINDOW_RECESS = 23U,
    COLOUR_EMPIRE_CORNER = 24U,
    COLOUR_EMPIRE_CORNER_HIGHLIGHT = 25U,
    COLOUR_WINDOW_WARM_LIGHT = 26U,
    COLOUR_WINDOW_SILHOUETTE = 27U,
    COLOUR_WINDOW_INTERIOR_ACCENT = 28U,
    COLOUR_WINDOW_NEON = 29U,
    COLOUR_BACKGROUND_CLOUD = 30U,
    COLOUR_BACKGROUND_MOUNTAIN = 31U,
    COLOUR_BACKGROUND_CITY = 32U,
    COLOUR_BACKGROUND_CITY_LIGHT = 33U,
    COLOUR_ANTENNA_LIGHT = 34U,
    COLOUR_TRACK_STEEL = 35U,
    COLOUR_TRACK_HIGHLIGHT = 36U,
    COLOUR_BUCKET_METAL = 37U,
    COLOUR_BUCKET_HIGHLIGHT = 38U,
    COLOUR_WASHER_HARNESS = 39U,
    COLOUR_WASHER_GOGGLES = 40U,
    COLOUR_WASHER_GLOVE = 41U,
    COLOUR_WASHER_FACE_SHADOW = 42U,
    COLOUR_WASHER_EYES = 43U,
    COLOUR_TOOL_HANDLE = 44U,
    COLOUR_TOOL_BRISTLES = 45U,
    COLOUR_TOOL_RUBBER = 46U,
    COLOUR_BEAM_ORANGE = 47U,
    COLOUR_BEAM_ORANGE_HIGHLIGHT = 48U,
    COLOUR_SHRUB_DARK = 49U,
    COLOUR_SHRUB_GREEN = 50U,
    COLOUR_SHRUB_HIGHLIGHT = 51U,
    COLOUR_SCORE_TEXT = 52U,
    COLOUR_SCORE_SHADOW = 53U
};

typedef struct
{
    int16_t LeftSlider;
    int16_t RightSlider;
} WindowWasher_InputTypeDef;

typedef struct
{
    int16_t LeftY;
    int16_t RightY;
} WindowWasher_PlatformTypeDef;

typedef struct
{
    int32_t PositionX;
    int32_t VelocityX;
    int32_t PositionY;
    int32_t VelocityY;
    uint32_t OffscreenMilliseconds;
} WindowWasher_FigureTypeDef;

typedef struct
{
    bool Active;
    int32_t WorldRow;
    uint8_t Column;
    uint8_t SparkleFrameCount;
} WindowWasher_CleanWindowTypeDef;

typedef struct
{
    int32_t BuildingScroll;
    uint32_t LayoutSeed;
    uint64_t ElapsedMilliseconds;
    uint32_t Score;
    bool Crashed;
    WindowWasher_CleanWindowTypeDef CleanWindows[CLEAN_WINDOW_TRACKED_COUNT];
} WindowWasher_GameTypeDef;

static Render_ColourIndexTypeDef WindowWasher_WasherPixels[WASHER_IMAGE_SIZE * WASHER_IMAGE_SIZE];

static const Render_ImageTypeDef WindowWasher_WasherImage =
{
    .Pixels = WindowWasher_WasherPixels,
    .Width = WASHER_IMAGE_SIZE,
    .Height = WASHER_IMAGE_SIZE,
    .StridePixels = WASHER_IMAGE_SIZE,
    .HasTransparentColour = true,
    .TransparentColour = COLOUR_SKY
};

static const Display_ColourTypeDef WindowWasher_Palette[] =
{
    0x00A9DDF5U,
    0x00CBD1D0U,
    0x0068767CU,
    0x00AEDBE9U,
    0x003D4C54U,
    0x00F4CF6AU,
    0x005F6F7AU,
    0x00732F3EU,
    0x002E5263U,
    0x0049B8B2U,
    0x002D76B3U,
    0x00E34747U,
    0x00F2B441U,
    0x00F0B486U,
    0x001B3F59U,
    0x004C91BEU,
    0x00C94343U,
    0x00F37B70U,
    0x008A5A34U,
    0x00FFF4A3U,
    0x00A69B8BU,
    0x00EEE6D6U,
    0x00B78A64U,
    0x0055646AU,
    0x00273743U,
    0x004B6475U,
    0x00F6CB71U,
    0x002C4250U,
    0x006F99A8U,
    0x00EF78A9U,
    0x00DDF1FAU,
    0x0088B8CBU,
    0x006E98ACU,
    0x00BCD9E5U,
    0x00F14A48U,
    0x002E3C45U,
    0x006F838DU,
    0x00AAB4B8U,
    0x00D4DEE0U,
    0x00F28C36U,
    0x00C4E9EDU,
    0x00F2E8D5U,
    0x00C77D60U,
    0x0026313AU,
    0x00D9E1E3U,
    0x005BB6BCU,
    0x0029343BU,
    0x00C9652CU,
    0x00F3A34AU,
    0x002F5D38U,
    0x004F8A4CU,
    0x0079B866U,
    0x00FFFFFFU,
    0x00182128U
};

_Static_assert((sizeof(WindowWasher_Palette) / sizeof(WindowWasher_Palette[0])) <= APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT, "Window Washer splash palette exceeds the reserved application palette range.");

static WindowWasher_InputTypeDef WindowWasher_Input;
static WindowWasher_GameTypeDef WindowWasher_Game;
static WindowWasher_FigureTypeDef WindowWasher_Figure;
static uint32_t WindowWasher_PendingDeltaTimeMilliseconds;
static uint64_t WindowWasher_SplashElapsedMilliseconds;
static uint32_t WindowWasher_HighScore;
static uint8_t WindowWasher_ScorePulseFrames;
static bool WindowWasher_Initialized;
static bool WindowWasher_Paused;

static int16_t WindowWasher_Clamp(int16_t Value, int16_t Minimum, int16_t Maximum)
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

static uint32_t WindowWasher_Hash(uint32_t Value)
{
    Value ^= Value >> 16U;
    Value *= 0x7FEB352DU;
    Value ^= Value >> 15U;
    Value *= 0x846CA68BU;
    Value ^= Value >> 16U;

    return Value;
}

static bool WindowWasher_RectIsVisible(const Render_RectTypeDef *Rect)
{
    const int32_t Right = (int32_t)Rect->X + (int32_t)Rect->Width;
    const int32_t Bottom = (int32_t)Rect->Y + (int32_t)Rect->Height;

    return (Rect->Width > 0U) && (Rect->Height > 0U) && (Right > 0) && (Bottom > 0) && ((int32_t)Rect->X < (int32_t)RENDER_WIDTH) && ((int32_t)Rect->Y < (int32_t)RENDER_HEIGHT);
}

/**
 * @brief Draw the window recess, frame, and glass without painting over pixels
 *        that will immediately be replaced by a later layer.
 */
static void WindowWasher_DrawWindowBase(Render_TargetTypeDef *Target, int16_t WindowX, int16_t WindowY)
{
    const Render_RectTypeDef RecessTop =
    {
        (int16_t)(WindowX - 3),
        (int16_t)(WindowY - 3),
        WINDOW_WIDTH + 6U,
        3U
    };

    const Render_RectTypeDef RecessLeft =
    {
        (int16_t)(WindowX - 3),
        WindowY,
        3U,
        WINDOW_HEIGHT
    };

    const Render_RectTypeDef RecessRight =
    {
        (int16_t)(WindowX + (int16_t)WINDOW_WIDTH),
        WindowY,
        3U,
        WINDOW_HEIGHT
    };

    const Render_RectTypeDef RecessBottom =
    {
        (int16_t)(WindowX - 3),
        (int16_t)(WindowY + (int16_t)WINDOW_HEIGHT),
        WINDOW_WIDTH + 6U,
        9U
    };

    const Render_RectTypeDef FrameTop =
    {
        WindowX,
        WindowY,
        WINDOW_WIDTH,
        5U
    };

    const Render_RectTypeDef FrameBottom =
    {
        WindowX,
        (int16_t)(WindowY + (int16_t)WINDOW_HEIGHT - 5),
        WINDOW_WIDTH,
        5U
    };

    const Render_RectTypeDef FrameLeft =
    {
        WindowX,
        (int16_t)(WindowY + 5),
        5U,
        WINDOW_HEIGHT - 10U
    };

    const Render_RectTypeDef FrameRight =
    {
        (int16_t)(WindowX + (int16_t)WINDOW_WIDTH - 5),
        (int16_t)(WindowY + 5),
        5U,
        WINDOW_HEIGHT - 10U
    };

    const Render_RectTypeDef Glass =
    {
        (int16_t)(WindowX + 5),
        (int16_t)(WindowY + 5),
        WINDOW_WIDTH - 10U,
        WINDOW_HEIGHT - 10U
    };

    Render_FillRect(Target, &RecessTop, COLOUR_WINDOW_RECESS);
    Render_FillRect(Target, &RecessLeft, COLOUR_WINDOW_RECESS);
    Render_FillRect(Target, &RecessRight, COLOUR_WINDOW_RECESS);
    Render_FillRect(Target, &RecessBottom, COLOUR_WINDOW_RECESS);

    Render_FillRect(Target, &FrameTop, COLOUR_WINDOW_FRAME);
    Render_FillRect(Target, &FrameBottom, COLOUR_WINDOW_FRAME);
    Render_FillRect(Target, &FrameLeft, COLOUR_WINDOW_FRAME);
    Render_FillRect(Target, &FrameRight, COLOUR_WINDOW_FRAME);

    Render_FillRect(Target, &Glass, COLOUR_WINDOW);
}


static int16_t WindowWasher_ConvertSliderValue(int32_t Value)
{
    int32_t CentredValue;

    if(Value < INPUT_SLIDER_MINIMUM)
    {
        Value = INPUT_SLIDER_MINIMUM;
    }
    else if(Value > INPUT_SLIDER_MAXIMUM)
    {
        Value = INPUT_SLIDER_MAXIMUM;
    }

    CentredValue = Value - INPUT_SLIDER_CENTRE;

    if(CentredValue >= 0)
    {
        return (int16_t)((CentredValue * SLIDER_TRAVEL) / (INPUT_SLIDER_MAXIMUM - INPUT_SLIDER_CENTRE));
    }

    return (int16_t)((CentredValue * SLIDER_TRAVEL) / (INPUT_SLIDER_CENTRE - INPUT_SLIDER_MINIMUM));
}

static uint32_t WindowWasher_BalconyPattern(const WindowWasher_GameTypeDef *Game, int32_t WorldRow)
{
    return WindowWasher_Hash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0x9E3779B9U));
}

static bool WindowWasher_BalconyFromLeft(uint32_t Pattern)
{
    return (Pattern & 1U) == 0U;
}

static int16_t WindowWasher_BalconyEndX(uint32_t Pattern, bool FromLeft)
{
    const int16_t BalconyLength = (int16_t)(220U + (Pattern % 151U));

    return FromLeft ? (int16_t)(BUILDING_LEFT_X + BalconyLength) : (int16_t)(BUILDING_RIGHT_X - BalconyLength);
}

static bool WindowWasher_BalconyHasCentreGap(uint32_t Pattern)
{
    return ((Pattern >> 4U) & 0x03U) == 0U;
}

static int16_t WindowWasher_BalconyGapLeftX(uint32_t Pattern)
{
    const int16_t GapWidth = (int16_t)(160U + ((Pattern >> 8U) % 41U));
    const int16_t GapCentreX = (int16_t)(360U + ((Pattern >> 16U) % 81U));

    return (int16_t)(GapCentreX - (GapWidth / 2));
}

static bool WindowWasher_WindowIsReachable(const WindowWasher_GameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    const int16_t WindowX = (int16_t)(WINDOW_GRID_LEFT_X + ((int16_t)Column * WINDOW_STEP_X));
    const int32_t WindowWorldY = (WorldRow * (int32_t)WINDOW_STEP_Y) + 8;
    const int32_t NearestBalconyRow = (WindowWorldY - BALCONY_VERTICAL_OFFSET) / BALCONY_STEP_Y;

    if((Column == 0U) || (Column >= (WINDOW_COLUMN_COUNT - 1U)))
    {
        return false;
    }

    for(int32_t BalconyRow = NearestBalconyRow - 1; BalconyRow <= (NearestBalconyRow + 1); BalconyRow++)
    {
        const int32_t BalconyRelativeY = WindowWorldY - ((BalconyRow * (int32_t)BALCONY_STEP_Y) + BALCONY_VERTICAL_OFFSET);
        const uint32_t Pattern = WindowWasher_BalconyPattern(Game, BalconyRow);
        const bool FromLeft = WindowWasher_BalconyFromLeft(Pattern);
        const bool HasCentreGap = WindowWasher_BalconyHasCentreGap(Pattern);
        const int16_t SingleEndX = WindowWasher_BalconyEndX(Pattern, FromLeft);
        const int16_t SingleX = FromLeft ? BUILDING_LEFT_X : SingleEndX;
        const uint16_t SingleWidth = (uint16_t)(FromLeft ? (SingleEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - SingleEndX));
        const int16_t GapLeftX = WindowWasher_BalconyGapLeftX(Pattern);
        const int16_t GapRightX = (int16_t)(GapLeftX + 160U + ((Pattern >> 8U) % 41U));
        const bool WindowMeetsBalconyHeight = (BalconyRelativeY > -(int32_t)FIGURE_HEIGHT) && (BalconyRelativeY < (int32_t)WINDOW_HEIGHT);
        const bool WindowHitsLeftSection = ((WindowX + (int16_t)WINDOW_WIDTH) > BUILDING_LEFT_X) && (WindowX < GapLeftX);
        const bool WindowHitsRightSection = ((WindowX + (int16_t)WINDOW_WIDTH) > GapRightX) && (WindowX < BUILDING_RIGHT_X);
        const bool WindowHitsSingleSection = ((WindowX + (int16_t)WINDOW_WIDTH) > SingleX) && (WindowX < (SingleX + (int16_t)SingleWidth));

        if((BalconyRow >= BALCONY_CLEAR_START) && WindowMeetsBalconyHeight && (HasCentreGap ? (WindowHitsLeftSection || WindowHitsRightSection) : WindowHitsSingleSection))
        {
            return false;
        }
    }

    return true;
}

static bool WindowWasher_WindowIsDirty(const WindowWasher_GameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    const uint32_t Pattern = WindowWasher_Hash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0x85EBCA6BU) ^ ((uint32_t)Column * 0xC2B2AE35U));

    return (Pattern % 50U) == 0U && WindowWasher_WindowIsReachable(Game, WorldRow, Column);
}

static const WindowWasher_CleanWindowTypeDef *WindowWasher_FindCleanWindow(const WindowWasher_GameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_TRACKED_COUNT; WindowIndex++)
    {
        const WindowWasher_CleanWindowTypeDef *Window = &Game->CleanWindows[WindowIndex];

        if(Window->Active && (Window->WorldRow == WorldRow) && (Window->Column == Column))
        {
            return Window;
        }
    }

    return NULL;
}

static void WindowWasher_PaintWasherRectangle(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    for(uint16_t Row = 0U; Row < Height; Row++)
    {
        Render_ColourIndexTypeDef *Destination = &WindowWasher_WasherPixels[((Y + Row) * WASHER_IMAGE_SIZE) + X];

        for(uint16_t Column = 0U; Column < Width; Column++)
        {
            Destination[Column] = Colour;
        }
    }
}

static void WindowWasher_BuildWasherImage(void)
{
    WindowWasher_PaintWasherRectangle(20U, 3U, 24U, 5U, COLOUR_WASHER_HELMET);
    WindowWasher_PaintWasherRectangle(16U, 8U, 32U, 8U, COLOUR_WASHER_HELMET);
    WindowWasher_PaintWasherRectangle(12U, 15U, 40U, 5U, COLOUR_WASHER_HELMET);
    WindowWasher_PaintWasherRectangle(20U, 20U, 24U, 15U, COLOUR_WASHER_FACE);
    WindowWasher_PaintWasherRectangle(17U, 24U, 3U, 6U, COLOUR_WASHER_FACE_SHADOW);
    WindowWasher_PaintWasherRectangle(44U, 24U, 3U, 6U, COLOUR_WASHER_FACE_SHADOW);
    WindowWasher_PaintWasherRectangle(23U, 23U, 8U, 5U, COLOUR_WASHER_GOGGLES);
    WindowWasher_PaintWasherRectangle(34U, 23U, 8U, 5U, COLOUR_WASHER_GOGGLES);
    WindowWasher_PaintWasherRectangle(31U, 24U, 3U, 2U, COLOUR_WASHER_GOGGLES);
    WindowWasher_PaintWasherRectangle(26U, 24U, 2U, 2U, COLOUR_WASHER_EYES);
    WindowWasher_PaintWasherRectangle(37U, 24U, 2U, 2U, COLOUR_WASHER_EYES);
    WindowWasher_PaintWasherRectangle(31U, 28U, 3U, 4U, COLOUR_WASHER_FACE_SHADOW);
    WindowWasher_PaintWasherRectangle(27U, 32U, 10U, 2U, COLOUR_WASHER_FACE_SHADOW);
    WindowWasher_PaintWasherRectangle(26U, 35U, 12U, 4U, COLOUR_WASHER_FACE);
    WindowWasher_PaintWasherRectangle(19U, 37U, 26U, 18U, COLOUR_WASHER_UNIFORM_DARK);
    WindowWasher_PaintWasherRectangle(23U, 38U, 18U, 17U, COLOUR_WASHER_UNIFORM_LIGHT);
    WindowWasher_PaintWasherRectangle(13U, 39U, 7U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    WindowWasher_PaintWasherRectangle(44U, 39U, 7U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    WindowWasher_PaintWasherRectangle(11U, 53U, 9U, 5U, COLOUR_WASHER_GLOVE);
    WindowWasher_PaintWasherRectangle(44U, 53U, 9U, 5U, COLOUR_WASHER_GLOVE);
    WindowWasher_PaintWasherRectangle(25U, 38U, 3U, 18U, COLOUR_WASHER_HARNESS);
    WindowWasher_PaintWasherRectangle(36U, 38U, 3U, 18U, COLOUR_WASHER_HARNESS);
    WindowWasher_PaintWasherRectangle(23U, 48U, 19U, 4U, COLOUR_WASHER_HARNESS);
    WindowWasher_PaintWasherRectangle(19U, 56U, 11U, 6U, COLOUR_WASHER_UNIFORM_DARK);
    WindowWasher_PaintWasherRectangle(34U, 56U, 11U, 6U, COLOUR_WASHER_UNIFORM_DARK);
    WindowWasher_PaintWasherRectangle(17U, 61U, 15U, 3U, COLOUR_TRACK_STEEL);
    WindowWasher_PaintWasherRectangle(33U, 61U, 15U, 3U, COLOUR_TRACK_STEEL);
}

static WindowWasher_PlatformTypeDef WindowWasher_MakePlatform(const WindowWasher_GameTypeDef *Game, const WindowWasher_InputTypeDef *Input)
{
    WindowWasher_PlatformTypeDef Platform;
    const int32_t CameraTravelY = Game->BuildingScroll < PLATFORM_START_OFFSET_Y ? Game->BuildingScroll : PLATFORM_START_OFFSET_Y;
    const int16_t StartOffsetY = (int16_t)(PLATFORM_START_OFFSET_Y - CameraTravelY);

    Platform.LeftY = (int16_t)(PLATFORM_BASE_Y - (Input->LeftSlider / 2) - StartOffsetY);
    Platform.RightY = (int16_t)(PLATFORM_BASE_Y - (Input->RightSlider / 2) - StartOffsetY);

    return Platform;
}

static int16_t WindowWasher_PlatformYAtX(const WindowWasher_PlatformTypeDef *Platform, int16_t X)
{
    const int32_t RelativeX = WindowWasher_Clamp(X, PLATFORM_LEFT_X, PLATFORM_RIGHT_X) - PLATFORM_LEFT_X;
    const int32_t HeightDifference = Platform->RightY - Platform->LeftY;

    return (int16_t)(Platform->LeftY + ((RelativeX * HeightDifference) / (PLATFORM_RIGHT_X - PLATFORM_LEFT_X)));
}

static int16_t WindowWasher_FigureY(const WindowWasher_PlatformTypeDef *Platform, const WindowWasher_FigureTypeDef *Figure)
{
    return (int16_t)(WindowWasher_PlatformYAtX(Platform, (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE)) - 22);
}

/**
 * @brief Add points to the current score and update the session high score.
 *
 * @param Game Game state to update.
 * @param Points Number of points to add.
 * @param FlashScore True when the score display should briefly flash.
 */
static void WindowWasher_AddScore(WindowWasher_GameTypeDef *Game, uint32_t Points, bool FlashScore)
{
    if((Game == NULL) || (Points == 0U))
    {
        return;
    }

    if(Points >= (SCORE_MAXIMUM - Game->Score))
    {
        Game->Score = SCORE_MAXIMUM;
    }
    else
    {
        Game->Score += Points;
    }

    if(Game->Score > WindowWasher_HighScore)
    {
        WindowWasher_HighScore = Game->Score;
    }

    if(FlashScore)
    {
        WindowWasher_ScorePulseFrames = SCORE_PULSE_FRAME_COUNT;
    }
}

static void WindowWasher_ResetGame(WindowWasher_GameTypeDef *Game, WindowWasher_FigureTypeDef *Figure)
{
    static uint32_t ResetNonce = 0U;

    Game->BuildingScroll = 0;
    Game->LayoutSeed = WindowWasher_Hash((uint32_t)Game->ElapsedMilliseconds ^ (uint32_t)(Game->ElapsedMilliseconds >> 32U) ^ (++ResetNonce * 0xA511E9B3U));
    Game->ElapsedMilliseconds = 0U;
    Game->Score = 0U;
    Game->Crashed = false;

    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_TRACKED_COUNT; WindowIndex++)
    {
        Game->CleanWindows[WindowIndex].Active = false;
    }

    Figure->PositionX = ((int32_t)RENDER_WIDTH / 2) * FIGURE_FIXED_SCALE;
    Figure->VelocityX = 0;
    Figure->PositionY = 0;
    Figure->VelocityY = 0;
    Figure->OffscreenMilliseconds = 0U;
}

static bool WindowWasher_FigureHitsBalcony(const WindowWasher_GameTypeDef *Game, const WindowWasher_PlatformTypeDef *Platform, const WindowWasher_FigureTypeDef *Figure)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / BALCONY_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % BALCONY_STEP_Y);
    const int16_t FigureX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t FigureY = WindowWasher_FigureY(Platform, Figure);

    for(int8_t ScreenRow = -1; ScreenRow < 4; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const uint32_t Pattern = WindowWasher_BalconyPattern(Game, WorldRow);
        const bool FromLeft = WindowWasher_BalconyFromLeft(Pattern);
        const bool HasCentreGap = WindowWasher_BalconyHasCentreGap(Pattern);
        const int16_t BalconyY = (int16_t)((ScreenRow * (int16_t)BALCONY_STEP_Y) + ScrollOffset + BALCONY_VERTICAL_OFFSET);
        const int16_t BalconyEndX = WindowWasher_BalconyEndX(Pattern, FromLeft);
        const int16_t BalconyX = FromLeft ? BUILDING_LEFT_X : BalconyEndX;
        const uint16_t BalconyWidth = (uint16_t)(FromLeft ? (BalconyEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - BalconyEndX));
        const int16_t GapLeftX = WindowWasher_BalconyGapLeftX(Pattern);
        const int16_t GapRightX = (int16_t)(GapLeftX + 160U + ((Pattern >> 8U) % 41U));
        const bool FigureHitsHeight = ((FigureY + (int16_t)FIGURE_HEIGHT) > BalconyY) && (FigureY < (BalconyY + (int16_t)BALCONY_THICKNESS));
        const bool FigureHitsLeftSection = ((FigureX + ((int16_t)FIGURE_WIDTH / 2)) > BUILDING_LEFT_X) && ((FigureX - ((int16_t)FIGURE_WIDTH / 2)) < GapLeftX);
        const bool FigureHitsRightSection = ((FigureX + ((int16_t)FIGURE_WIDTH / 2)) > GapRightX) && ((FigureX - ((int16_t)FIGURE_WIDTH / 2)) < BUILDING_RIGHT_X);
        const bool FigureHitsSingleSection = ((FigureX + ((int16_t)FIGURE_WIDTH / 2)) > BalconyX) && ((FigureX - ((int16_t)FIGURE_WIDTH / 2)) < (BalconyX + (int16_t)BalconyWidth));

        if((WorldRow >= BALCONY_CLEAR_START) && FigureHitsHeight && (HasCentreGap ? (FigureHitsLeftSection || FigureHitsRightSection) : FigureHitsSingleSection))
        {
            return true;
        }
    }

    return false;
}

static void WindowWasher_UpdateWindowCleaning(WindowWasher_GameTypeDef *Game, const WindowWasher_PlatformTypeDef *Platform, const WindowWasher_FigureTypeDef *Figure)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / WINDOW_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % WINDOW_STEP_Y);
    const int16_t FigureX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t FigureY = WindowWasher_FigureY(Platform, Figure);
    const int16_t FigureLeft = (int16_t)(FigureX - ((int16_t)FIGURE_WIDTH / 2));
    const int16_t FigureRight = (int16_t)(FigureX + ((int16_t)FIGURE_WIDTH / 2));
    const int16_t FigureBottom = (int16_t)(FigureY + (int16_t)FIGURE_HEIGHT);

    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_TRACKED_COUNT; WindowIndex++)
    {
        WindowWasher_CleanWindowTypeDef *CleanWindow = &Game->CleanWindows[WindowIndex];

        if(CleanWindow->Active && (CleanWindow->WorldRow < (FirstWorldRow - 10)))
        {
            CleanWindow->Active = false;
        }
        else if(CleanWindow->Active && (CleanWindow->SparkleFrameCount > 0U))
        {
            CleanWindow->SparkleFrameCount--;
        }
    }

    /*
     * Reject rows and columns against the washer bounds before performing the
     * hash, reachability test, or 64-entry clean-window search.
     */
    for(int8_t ScreenRow = -1; ScreenRow < 9; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t WindowY = (int16_t)((ScreenRow * (int16_t)WINDOW_STEP_Y) + ScrollOffset + 8);

        if((FigureBottom <= WindowY) || (FigureY >= (int16_t)(WindowY + (int16_t)WINDOW_HEIGHT)))
        {
            continue;
        }

        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(WINDOW_GRID_LEFT_X + ((int16_t)Column * WINDOW_STEP_X));

            if((FigureRight <= WindowX) || (FigureLeft >= (int16_t)(WindowX + (int16_t)WINDOW_WIDTH)))
            {
                continue;
            }

            if(!WindowWasher_WindowIsDirty(Game, WorldRow, Column) || (WindowWasher_FindCleanWindow(Game, WorldRow, Column) != NULL))
            {
                continue;
            }

            WindowWasher_CleanWindowTypeDef *CleanWindow = NULL;

            for(uint8_t CleanIndex = 0U; CleanIndex < CLEAN_WINDOW_TRACKED_COUNT; CleanIndex++)
            {
                if(!Game->CleanWindows[CleanIndex].Active)
                {
                    CleanWindow = &Game->CleanWindows[CleanIndex];
                    break;
                }
            }

            if(CleanWindow == NULL)
            {
                CleanWindow = &Game->CleanWindows[0];

                for(uint8_t CleanIndex = 1U; CleanIndex < CLEAN_WINDOW_TRACKED_COUNT; CleanIndex++)
                {
                    if(Game->CleanWindows[CleanIndex].WorldRow < CleanWindow->WorldRow)
                    {
                        CleanWindow = &Game->CleanWindows[CleanIndex];
                    }
                }
            }

            CleanWindow->Active = true;
            CleanWindow->WorldRow = WorldRow;
            CleanWindow->Column = Column;
            CleanWindow->SparkleFrameCount = CLEAN_WINDOW_SPARKLE_FRAMES;

            WindowWasher_AddScore(Game, SCORE_POINTS_PER_DIRT, true);
        }
    }
}

static uint8_t WindowWasher_GetBuildingSpeed(uint64_t ElapsedMilliseconds)
{
    if(ElapsedMilliseconds >= SPEED_5_START_TIME_MS)
    {
        return SPEED_5_PIXELS_PER_FRAME;
    }

    if(ElapsedMilliseconds >= SPEED_4_START_TIME_MS)
    {
        return SPEED_4_PIXELS_PER_FRAME;
    }

    if(ElapsedMilliseconds >= SPEED_3_START_TIME_MS)
    {
        return SPEED_3_PIXELS_PER_FRAME;
    }

    if(ElapsedMilliseconds >= SPEED_2_START_TIME_MS)
    {
        return SPEED_2_PIXELS_PER_FRAME;
    }

    if(ElapsedMilliseconds >= SPEED_1_START_TIME_MS)
    {
        return SPEED_1_PIXELS_PER_FRAME;
    }

    return 0U;
}

static void WindowWasher_UpdateGame(WindowWasher_GameTypeDef *Game, WindowWasher_FigureTypeDef *Figure, const WindowWasher_PlatformTypeDef *Platform, uint32_t DeltaTimeMilliseconds)
{
    uint8_t BuildingSpeedPixelsPerFrame;
    uint32_t PreviousFloor;
    uint32_t CurrentFloor;

    Game->ElapsedMilliseconds += DeltaTimeMilliseconds;
    BuildingSpeedPixelsPerFrame = WindowWasher_GetBuildingSpeed(Game->ElapsedMilliseconds);

    if(Game->Crashed)
    {
        Figure->VelocityY += (int32_t)(((int64_t)FALL_GRAVITY_FIXED_PER_SECOND_SQUARED * DeltaTimeMilliseconds) / 1000);
        Figure->PositionY += (int32_t)(((int64_t)Figure->VelocityY * DeltaTimeMilliseconds) / 1000);

        if((Figure->PositionY / FIGURE_FIXED_SCALE) > (int32_t)RENDER_HEIGHT)
        {
            Figure->OffscreenMilliseconds += DeltaTimeMilliseconds;

            if(Figure->OffscreenMilliseconds >= FALL_RESTART_DELAY_MILLISECONDS)
            {
                WindowWasher_ResetGame(Game, Figure);
            }
        }

        return;
    }

    Figure->VelocityX += ((int32_t)(Platform->RightY - Platform->LeftY) * FIGURE_SLIDE_GRAVITY) / (PLATFORM_RIGHT_X - PLATFORM_LEFT_X);
    Figure->VelocityX = (Figure->VelocityX * 250) / 256;
    Figure->PositionX += Figure->VelocityX;

    PreviousFloor = (uint32_t)Game->BuildingScroll / WINDOW_STEP_Y;

    Game->BuildingScroll += (int32_t)BuildingSpeedPixelsPerFrame;

    CurrentFloor = (uint32_t)Game->BuildingScroll / WINDOW_STEP_Y;

    if(CurrentFloor > PreviousFloor)
    {
        WindowWasher_AddScore(Game, (CurrentFloor - PreviousFloor) * SCORE_POINTS_PER_FLOOR, false);
    }

    WindowWasher_UpdateWindowCleaning(Game, Platform, Figure);

    if(((Figure->PositionX / FIGURE_FIXED_SCALE) < (PLATFORM_LEFT_X + ((int16_t)FIGURE_WIDTH / 2))) || ((Figure->PositionX / FIGURE_FIXED_SCALE) > (PLATFORM_RIGHT_X - ((int16_t)FIGURE_WIDTH / 2))) || WindowWasher_FigureHitsBalcony(Game, Platform, Figure))
    {
        Game->Crashed = true;
        Figure->PositionY = (int32_t)WindowWasher_FigureY(Platform, Figure) * FIGURE_FIXED_SCALE;
        Figure->VelocityY = FALL_INITIAL_VELOCITY_FIXED_PER_SECOND;
        Figure->OffscreenMilliseconds = 0U;
    }
}

static void WindowWasher_DrawWindowScene(Render_TargetTypeDef *Target, int16_t WindowX, int16_t WindowY, uint32_t Pattern)
{
    const uint8_t Scene = (uint8_t)((Pattern >> 8U) % 6U);
    const Render_RectTypeDef Interior = { (int16_t)(WindowX + 7), (int16_t)(WindowY + 7), WINDOW_WIDTH - 14U, WINDOW_HEIGHT - 14U };
    const Render_RectTypeDef Floor = { (int16_t)(WindowX + 7), (int16_t)(WindowY + WINDOW_HEIGHT - 11U), WINDOW_WIDTH - 14U, 4U };

    if((Pattern % 3U) == 0U)
    {
        Render_FillRect(Target, &Interior, COLOUR_WINDOW_WARM_LIGHT);
    }

    Render_FillRect(Target, &Floor, COLOUR_WINDOW_SILHOUETTE);

    switch(Scene)
    {
    case 0U: /* Blinds */ for(uint8_t Blind = 0U; Blind < 4U; Blind++)
        {
            const Render_RectTypeDef Slat = { (int16_t)(WindowX + 9), (int16_t)(WindowY + 10 + (Blind * 7U)), WINDOW_WIDTH - 18U, 2U };

            Render_FillRect(Target, &Slat, COLOUR_WINDOW_SILHOUETTE);
        }
        break;

    case 1U: /* Desk, chair, lamp, and small plant */
        {
            const Render_RectTypeDef Desk = { (int16_t)(WindowX + 13), (int16_t)(WindowY + 30), 28U, 4U };
            const Render_RectTypeDef LampStand = { (int16_t)(WindowX + 20), (int16_t)(WindowY + 17), 2U, 13U };
            const Render_RectTypeDef LampShade = { (int16_t)(WindowX + 17), (int16_t)(WindowY + 15), 8U, 4U };

            Render_FillRect(Target, &Desk, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &LampStand, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &LampShade, COLOUR_WINDOW_WARM_LIGHT);
            break;
        }

    case 2U: /* Employee silhouette */
        {
            const Render_RectTypeDef Head = { (int16_t)(WindowX + 30), (int16_t)(WindowY + 13), 9U, 9U };
            const Render_RectTypeDef Body = { (int16_t)(WindowX + 27), (int16_t)(WindowY + 22), 15U, 17U };

            Render_FillRect(Target, &Head, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Body, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 3U: /* Tall potted plant */
        {
            const Render_RectTypeDef Pot = { (int16_t)(WindowX + 29), (int16_t)(WindowY + 30), 12U, 8U };
            const Render_RectTypeDef Stem = { (int16_t)(WindowX + 34), (int16_t)(WindowY + 13), 3U, 18U };
            const Render_RectTypeDef Leaves = { (int16_t)(WindowX + 25), (int16_t)(WindowY + 16), 21U, 12U };

            Render_FillRect(Target, &Pot, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &Stem, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Leaves, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 4U: /* Floor lamp */
        {
            const Render_RectTypeDef Shade = { (int16_t)(WindowX + 28), (int16_t)(WindowY + 12), 14U, 7U };
            const Render_RectTypeDef Stand = { (int16_t)(WindowX + 34), (int16_t)(WindowY + 19), 3U, 18U };
            const Render_RectTypeDef Base = { (int16_t)(WindowX + 29), (int16_t)(WindowY + 36), 13U, 3U };

            Render_FillRect(Target, &Shade, COLOUR_WINDOW_WARM_LIGHT);
            Render_FillRect(Target, &Stand, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Base, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 5U: /* Office clock */
        {
            const Render_RectTypeDef Clock = { (int16_t)(WindowX + 28), (int16_t)(WindowY + 12), 15U, 15U };
            const Render_RectTypeDef HandOne = { (int16_t)(WindowX + 35), (int16_t)(WindowY + 14), 2U, 7U };
            const Render_RectTypeDef HandTwo = { (int16_t)(WindowX + 35), (int16_t)(WindowY + 20), 5U, 2U };

            Render_FillRect(Target, &Clock, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &HandOne, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &HandTwo, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 6U: /* Filing cabinet */
        {
            const Render_RectTypeDef Cabinet = { (int16_t)(WindowX + 24), (int16_t)(WindowY + 16), 20U, 22U };
            const Render_RectTypeDef DrawerOne = { (int16_t)(WindowX + 26), (int16_t)(WindowY + 22), 16U, 2U };
            const Render_RectTypeDef DrawerTwo = { (int16_t)(WindowX + 26), (int16_t)(WindowY + 30), 16U, 2U };

            Render_FillRect(Target, &Cabinet, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &DrawerOne, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &DrawerTwo, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 7U: /* Framed picture */
        {
            const Render_RectTypeDef Frame = { (int16_t)(WindowX + 23), (int16_t)(WindowY + 12), 24U, 17U };
            const Render_RectTypeDef Picture = { (int16_t)(WindowX + 26), (int16_t)(WindowY + 15), 18U, 11U };

            Render_FillRect(Target, &Frame, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Picture, COLOUR_WINDOW_INTERIOR_ACCENT);
            break;
        }

    case 8U: /* Hotel-suite couch and table lamp */
        {
            const Render_RectTypeDef Couch = { (int16_t)(WindowX + 13), (int16_t)(WindowY + 27), 30U, 11U };
            const Render_RectTypeDef LampStand = { (int16_t)(WindowX + 49), (int16_t)(WindowY + 19), 2U, 18U };
            const Render_RectTypeDef LampShade = { (int16_t)(WindowX + 46), (int16_t)(WindowY + 17), 8U, 4U };

            Render_FillRect(Target, &Couch, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &LampStand, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &LampShade, COLOUR_WINDOW_WARM_LIGHT);
            break;
        }

    case 9U: /* Maintenance closet */
        {
            const Render_RectTypeDef Cart = { (int16_t)(WindowX + 16), (int16_t)(WindowY + 28), 28U, 7U };
            const Render_RectTypeDef Handle = { (int16_t)(WindowX + 42), (int16_t)(WindowY + 19), 3U, 16U };
            const Render_RectTypeDef Boxes = { (int16_t)(WindowX + 32), (int16_t)(WindowY + 15), 14U, 12U };

            Render_FillRect(Target, &Cart, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &Handle, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Boxes, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 10U: /* Open window with curtains */
        {
            const Render_RectTypeDef LeftCurtain = { (int16_t)(WindowX + 8), (int16_t)(WindowY + 9), 13U, 28U };
            const Render_RectTypeDef RightCurtain = { (int16_t)(WindowX + 49), (int16_t)(WindowY + 9), 13U, 28U };
            const Render_RectTypeDef OpenGlass = { (int16_t)(WindowX + 30), (int16_t)(WindowY + 11), 12U, 22U };

            Render_FillRect(Target, &LeftCurtain, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &RightCurtain, COLOUR_WINDOW_INTERIOR_ACCENT);
            Render_FillRect(Target, &OpenGlass, COLOUR_SKY);
            break;
        }

    case 11U: /* Neon sign */
        {
            const Render_RectTypeDef Sign = { (int16_t)(WindowX + 15), (int16_t)(WindowY + 18), 40U, 12U };
            const Render_RectTypeDef LetterGapOne = { (int16_t)(WindowX + 27), (int16_t)(WindowY + 20), 3U, 8U };
            const Render_RectTypeDef LetterGapTwo = { (int16_t)(WindowX + 41), (int16_t)(WindowY + 20), 3U, 8U };

            Render_FillRect(Target, &Sign, COLOUR_WINDOW_NEON);
            Render_FillRect(Target, &LetterGapOne, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &LetterGapTwo, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    case 12U: /* Cat on a sill */
        {
            const Render_RectTypeDef Body = { (int16_t)(WindowX + 27), (int16_t)(WindowY + 27), 16U, 9U };
            const Render_RectTypeDef Head = { (int16_t)(WindowX + 39), (int16_t)(WindowY + 22), 8U, 8U };
            const Render_RectTypeDef Tail = { (int16_t)(WindowX + 22), (int16_t)(WindowY + 23), 4U, 12U };

            Render_FillRect(Target, &Body, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Head, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Tail, COLOUR_WINDOW_SILHOUETTE);
            break;
        }

    default: /* Office party silhouettes */ for(uint8_t Guest = 0U; Guest < 4U; Guest++)
        {
            const Render_RectTypeDef Head = { (int16_t)(WindowX + 13 + (Guest * 12U)), (int16_t)(WindowY + 17 + ((Guest & 1U) * 3U)), 6U, 6U };
            const Render_RectTypeDef Body = { (int16_t)(WindowX + 11 + (Guest * 12U)), (int16_t)(WindowY + 23 + ((Guest & 1U) * 3U)), 10U, 13U };

            Render_FillRect(Target, &Head, COLOUR_WINDOW_SILHOUETTE);
            Render_FillRect(Target, &Body, COLOUR_WINDOW_SILHOUETTE);
        }
        break;
    }
}

static void WindowWasher_DrawBackgroundLayer(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    const int16_t CloudY = (int16_t)(145 + ((Game->BuildingScroll / 70) % 360));
    const int16_t MountainBaseY = (int16_t)((int16_t)RENDER_HEIGHT + 30 + (Game->BuildingScroll / 80));
    const int16_t CityBaseY = (int16_t)((int16_t)RENDER_HEIGHT + 10 + (Game->BuildingScroll / 70));
    const Render_RectTypeDef CloudOne = { 16, CloudY, 126U, 14U };
    const Render_RectTypeDef CloudTwo = { 595, (int16_t)(CloudY + 78), 166U, 16U };
    const Render_PointTypeDef Mountains[] =
    {
        { 0, MountainBaseY },
        { 0, (int16_t)(MountainBaseY - 72) },
        { 148, (int16_t)(MountainBaseY - 148) },
        { 322, (int16_t)(MountainBaseY - 75) },
        { 494, (int16_t)(MountainBaseY - 174) },
        { 652, (int16_t)(MountainBaseY - 88) },
        { (int16_t)RENDER_WIDTH, (int16_t)(MountainBaseY - 132) },
        { (int16_t)RENDER_WIDTH, MountainBaseY }
    };
    const Render_RectTypeDef CityBlocks[] =
    {
        { 0, (int16_t)(CityBaseY - 125), 31U, 125U },
        { 34, (int16_t)(CityBaseY - 185), 38U, 185U },
        { 74, (int16_t)(CityBaseY - 104), 50U, 104U },
        { 628, (int16_t)(CityBaseY - 98), 49U, 98U },
        { 680, (int16_t)(CityBaseY - 178), 42U, 178U },
        { 725, (int16_t)(CityBaseY - 128), 75U, 128U }
    };
    const Render_RectTypeDef AntennaBase = { 753, (int16_t)(CityBlocks[5].Y - 10), 18U, 10U };
    const Render_RectTypeDef AntennaLowerMast = { 758, (int16_t)(CityBlocks[5].Y - 36), 8U, 26U };
    const Render_RectTypeDef AntennaUpperMast = { 760, (int16_t)(CityBlocks[5].Y - 60), 4U, 24U };
    const Render_RectTypeDef AntennaLight = { 757, (int16_t)(CityBlocks[5].Y - 67), 10U, 7U };
    const bool AntennaLightOn = ((Game->ElapsedMilliseconds / 500ULL) & 1ULL) == 0ULL;

    for(int8_t CloudBand = -1; CloudBand <= 1; CloudBand++)
    {
        Render_RectTypeDef BandedCloudOne = CloudOne;
        Render_RectTypeDef BandedCloudTwo = CloudTwo;

        BandedCloudOne.Y = (int16_t)(BandedCloudOne.Y + (CloudBand * 360));
        BandedCloudTwo.Y = (int16_t)(BandedCloudTwo.Y + (CloudBand * 360));

        if(WindowWasher_RectIsVisible(&BandedCloudOne))
        {
            Render_FillRect(Target, &BandedCloudOne, COLOUR_BACKGROUND_CLOUD);
        }

        if(WindowWasher_RectIsVisible(&BandedCloudTwo))
        {
            Render_FillRect(Target, &BandedCloudTwo, COLOUR_BACKGROUND_CLOUD);
        }
    }

    if(MountainBaseY < ((int16_t)RENDER_HEIGHT + 180))
    {
        Render_DrawPolygon(Target, Mountains, (uint8_t)(sizeof(Mountains) / sizeof(Mountains[0])), COLOUR_BACKGROUND_MOUNTAIN);
    }

    for(uint8_t BlockIndex = 0U; BlockIndex < (uint8_t)(sizeof(CityBlocks) / sizeof(CityBlocks[0])); BlockIndex++)
    {
        if(!WindowWasher_RectIsVisible(&CityBlocks[BlockIndex]))
        {
            continue;
        }

        const Render_RectTypeDef Light =
        {
            (int16_t)(CityBlocks[BlockIndex].X + 7),
            (int16_t)(CityBlocks[BlockIndex].Y + 13),
            4U,
            8U
        };

        Render_FillRect(Target, &CityBlocks[BlockIndex], COLOUR_BACKGROUND_CITY);
        Render_FillRect(Target, &Light, COLOUR_BACKGROUND_CITY_LIGHT);
    }

    if(WindowWasher_RectIsVisible(&AntennaBase))
    {
        Render_FillRect(Target, &AntennaBase, COLOUR_BACKGROUND_CITY);
        Render_FillRect(Target, &AntennaLowerMast, COLOUR_BACKGROUND_CITY);
        Render_FillRect(Target, &AntennaUpperMast, COLOUR_BACKGROUND_CITY);

        if(AntennaLightOn)
        {
            Render_FillRect(Target, &AntennaLight, COLOUR_ANTENNA_LIGHT);
        }
    }
}

static void WindowWasher_DrawBackground(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    const Render_RectTypeDef LeftVisibleStrip =
    {
        0,
        0,
        BUILDING_LEFT_X,
        RENDER_HEIGHT
    };

    const Render_RectTypeDef RightVisibleStrip =
    {
        BUILDING_RIGHT_X,
        0,
        (uint16_t)((int16_t)RENDER_WIDTH - BUILDING_RIGHT_X),
        RENDER_HEIGHT
    };

    /*
     * The building covers the centre 660 pixels of the background. Draw the
     * scenery only into the two side strips instead of painting it and then
     * immediately replacing it with the facade.
     */
    Render_SetClipRect(&LeftVisibleStrip);
    WindowWasher_DrawBackgroundLayer(Target, Game);

    Render_SetClipRect(&RightVisibleStrip);
    WindowWasher_DrawBackgroundLayer(Target, Game);

    Render_ResetClipRect();
}

static void WindowWasher_DrawBuilding(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    const Render_RectTypeDef Building =
    {
        BUILDING_LEFT_X,
        0,
        (uint16_t)(BUILDING_RIGHT_X - BUILDING_LEFT_X),
        RENDER_HEIGHT
    };
    const Render_RectTypeDef LeftCorner =
    {
        BUILDING_LEFT_X,
        0,
        26U,
        RENDER_HEIGHT
    };
    const Render_RectTypeDef RightCorner =
    {
        (int16_t)(BUILDING_RIGHT_X - 26),
        0,
        26U,
        RENDER_HEIGHT
    };
    const Render_RectTypeDef LeftCornerHighlight =
    {
        (int16_t)(BUILDING_LEFT_X + 21),
        0,
        3U,
        RENDER_HEIGHT
    };
    const Render_RectTypeDef RightCornerHighlight =
    {
        (int16_t)(BUILDING_RIGHT_X - 24),
        0,
        3U,
        RENDER_HEIGHT
    };
    const int32_t FirstWorldRow = Game->BuildingScroll / WINDOW_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % WINDOW_STEP_Y);
    Render_FillRect(Target, &Building, COLOUR_BUILDING);
    Render_FillRect(Target, &LeftCorner, COLOUR_EMPIRE_CORNER);
    Render_FillRect(Target, &RightCorner, COLOUR_EMPIRE_CORNER);
    Render_FillRect(Target, &LeftCornerHighlight, COLOUR_EMPIRE_CORNER_HIGHLIGHT);
    Render_FillRect(Target, &RightCornerHighlight, COLOUR_EMPIRE_CORNER_HIGHLIGHT);

    for(int8_t ScreenRow = -1; ScreenRow < 9; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t WindowY = (int16_t)((ScreenRow * (int16_t)WINDOW_STEP_Y) + ScrollOffset + 8);
        const Render_RectTypeDef RowBounds =
        {
            (int16_t)(WINDOW_GRID_LEFT_X - 3),
            (int16_t)(WindowY - 3),
            WINDOW_GRID_WIDTH + 6U,
            WINDOW_HEIGHT + 9U
        };

        if(!WindowWasher_RectIsVisible(&RowBounds))
        {
            continue;
        }

        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(WINDOW_GRID_LEFT_X + ((int16_t)Column * WINDOW_STEP_X));
            const uint32_t ScenePattern = WindowWasher_Hash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0xD1B54A35U) ^ ((uint32_t)Column * 0x94D049BBU));
            const bool Dirty = WindowWasher_WindowIsDirty(Game, WorldRow, Column);
            const WindowWasher_CleanWindowTypeDef *CleanWindow = Dirty ? WindowWasher_FindCleanWindow(Game, WorldRow, Column) : NULL;
            const Render_RectTypeDef SillShadow =
            {
                (int16_t)(WindowX - 3),
                (int16_t)(WindowY + WINDOW_HEIGHT + 3U),
                WINDOW_WIDTH + 6U,
                4U
            };
            const Render_RectTypeDef Sill =
            {
                (int16_t)(WindowX - 3),
                (int16_t)(WindowY + WINDOW_HEIGHT),
                WINDOW_WIDTH + 6U,
                3U
            };

            WindowWasher_DrawWindowBase(Target, WindowX, WindowY);

            if((ScenePattern % 20U) == 0U)
            {
                WindowWasher_DrawWindowScene(Target, WindowX, WindowY, ScenePattern);
            }

            /*
             * These sit on top of the recess bottom by design. They are narrow
             * decorative strips, so retaining the tiny overlap is cheaper than
             * fragmenting the base into additional rectangles.
             */
            Render_FillRect(Target, &SillShadow, COLOUR_BUILDING_SHADOW);
            Render_FillRect(Target, &Sill, COLOUR_FACADE_TRIM);

            if(Dirty && (CleanWindow == NULL))
            {
                const Render_RectTypeDef SpotOne =
                {
                    (int16_t)(WindowX + 13),
                    (int16_t)(WindowY + 11),
                    12U,
                    8U
                };
                const Render_RectTypeDef SpotTwo =
                {
                    (int16_t)(WindowX + 38),
                    (int16_t)(WindowY + 25),
                    15U,
                    10U
                };
                const Render_RectTypeDef SpotThree =
                {
                    (int16_t)(WindowX + 27),
                    (int16_t)(WindowY + 34),
                    8U,
                    5U
                };

                Render_FillRect(Target, &SpotOne, COLOUR_DIRT);
                Render_FillRect(Target, &SpotTwo, COLOUR_DIRT);
                Render_FillRect(Target, &SpotThree, COLOUR_DIRT);
            }
            else if((CleanWindow != NULL) && (CleanWindow->SparkleFrameCount > 0U))
            {
                const Render_RectTypeDef HorizontalSparkle =
                {
                    (int16_t)(WindowX + 25),
                    (int16_t)(WindowY + 22),
                    20U,
                    3U
                };
                const Render_RectTypeDef VerticalSparkle =
                {
                    (int16_t)(WindowX + 33),
                    (int16_t)(WindowY + 14),
                    4U,
                    19U
                };

                Render_FillRect(Target, &HorizontalSparkle, COLOUR_SPARKLE);
                Render_FillRect(Target, &VerticalSparkle, COLOUR_SPARKLE);
            }
        }
    }
}

static void WindowWasher_DrawHotelEntrance(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    /*
     * Stop drawing the ground-floor facade once it has moved completely below
     * the display. This check must happen before narrowing BuildingScroll to
     * int16_t, otherwise a long play session can wrap the coordinates and make
     * the hotel reappear with invalid rectangle dimensions.
     */
    if(Game->BuildingScroll >= ((int32_t)RENDER_HEIGHT - HOTEL_ENTRANCE_TOP_Y))
    {
        return;
    }

    const int16_t GroundY = (int16_t)((int32_t)HOTEL_GROUND_Y + Game->BuildingScroll);
    const int16_t EntranceTopY = (int16_t)((int32_t)HOTEL_ENTRANCE_TOP_Y + Game->BuildingScroll);
    const int16_t EntranceLeftX = (int16_t)(((int16_t)RENDER_WIDTH - (int16_t)HOTEL_ENTRANCE_WIDTH) / 2);
    const int16_t EntranceRightX = (int16_t)(EntranceLeftX + (int16_t)HOTEL_ENTRANCE_WIDTH);
    const int16_t CanopyLeftX = (int16_t)(((int16_t)RENDER_WIDTH - (int16_t)HOTEL_CANOPY_WIDTH) / 2);
    const Render_RectTypeDef Pavement = { 0, GroundY, RENDER_WIDTH, (uint16_t)((int16_t)RENDER_HEIGHT - GroundY) };
    const Render_RectTypeDef EntranceRecess =
    {
        (int16_t)(EntranceLeftX - 18),
        EntranceTopY,
        HOTEL_ENTRANCE_WIDTH + 36U,
        HOTEL_ENTRANCE_HEIGHT
    };
    const Render_RectTypeDef EntranceFrame =
    {
        EntranceLeftX,
        (int16_t)(EntranceTopY + 20),
        HOTEL_ENTRANCE_WIDTH,
        HOTEL_ENTRANCE_HEIGHT - 20U
    };
    const Render_RectTypeDef LeftDoor =
    {
        (int16_t)(EntranceLeftX + 35),
        (int16_t)(EntranceTopY + 54),
        91U,
        HOTEL_ENTRANCE_HEIGHT - 54U
    };
    const Render_RectTypeDef RightDoor =
    {
        (int16_t)(EntranceRightX - 126),
        (int16_t)(EntranceTopY + 54),
        91U,
        HOTEL_ENTRANCE_HEIGHT - 54U
    };
    const Render_RectTypeDef DoorDivider =
    {
        (int16_t)(((int16_t)RENDER_WIDTH / 2) - 4),
        (int16_t)(EntranceTopY + 54),
        8U,
        HOTEL_ENTRANCE_HEIGHT - 54U
    };
    const Render_RectTypeDef LeftHandle =
    {
        (int16_t)(((int16_t)RENDER_WIDTH / 2) - 25),
        (int16_t)(EntranceTopY + 112),
        5U,
        25U
    };
    const Render_RectTypeDef RightHandle =
    {
        (int16_t)(((int16_t)RENDER_WIDTH / 2) + 20),
        (int16_t)(EntranceTopY + 112),
        5U,
        25U
    };
    const Render_RectTypeDef CanopyShadow =
    {
        CanopyLeftX,
        (int16_t)(EntranceTopY + 26),
        HOTEL_CANOPY_WIDTH,
        HOTEL_CANOPY_HEIGHT + 8U
    };
    const Render_RectTypeDef Canopy =
    {
        CanopyLeftX,
        (int16_t)(EntranceTopY + 20),
        HOTEL_CANOPY_WIDTH,
        HOTEL_CANOPY_HEIGHT
    };
    const Render_RectTypeDef CanopyHighlight =
    {
        (int16_t)(CanopyLeftX + 5),
        (int16_t)(EntranceTopY + 22),
        HOTEL_CANOPY_WIDTH - 10U,
        4U
    };
    const Render_RectTypeDef LeftColumn =
    {
        (int16_t)(EntranceLeftX - 31),
        (int16_t)(EntranceTopY + 36),
        22U,
        HOTEL_ENTRANCE_HEIGHT - 36U
    };
    const Render_RectTypeDef RightColumn =
    {
        (int16_t)(EntranceRightX + 9),
        (int16_t)(EntranceTopY + 36),
        22U,
        HOTEL_ENTRANCE_HEIGHT - 36U
    };
    const Render_RectTypeDef LeftColumnBase =
    {
        (int16_t)(EntranceLeftX - 37),
        (int16_t)(GroundY - 17),
        34U,
        17U
    };
    const Render_RectTypeDef RightColumnBase =
    {
        (int16_t)(EntranceRightX + 3),
        (int16_t)(GroundY - 17),
        34U,
        17U
    };
    const Render_RectTypeDef Sign =
    {
        (int16_t)(((int16_t)RENDER_WIDTH / 2) - 82),
        (int16_t)(EntranceTopY - 12),
        164U,
        30U
    };
    const Render_RectTypeDef SignInset =
    {
        (int16_t)(((int16_t)RENDER_WIDTH / 2) - 72),
        (int16_t)(EntranceTopY - 6),
        144U,
        18U
    };
    const Render_RectTypeDef LeftLamp =
    {
        (int16_t)(EntranceLeftX - 58),
        (int16_t)(EntranceTopY + 68),
        16U,
        24U
    };
    const Render_RectTypeDef RightLamp =
    {
        (int16_t)(EntranceRightX + 42),
        (int16_t)(EntranceTopY + 68),
        16U,
        24U
    };

    const Render_RectTypeDef LeftPlanter =
    {
        8,
        (int16_t)(GroundY - 28),
        (uint16_t)(BUILDING_LEFT_X - 16),
        28U
    };
    const Render_RectTypeDef RightPlanter =
    {
        (int16_t)(BUILDING_RIGHT_X + 8),
        (int16_t)(GroundY - 28),
        (uint16_t)((int16_t)RENDER_WIDTH - BUILDING_RIGHT_X - 16),
        28U
    };
    const Render_RectTypeDef LeftShrubBase =
    {
        5,
        (int16_t)(GroundY - 63),
        (uint16_t)(BUILDING_LEFT_X - 10),
        38U
    };
    const Render_RectTypeDef RightShrubBase =
    {
        (int16_t)(BUILDING_RIGHT_X + 5),
        (int16_t)(GroundY - 63),
        (uint16_t)((int16_t)RENDER_WIDTH - BUILDING_RIGHT_X - 10),
        38U
    };
    const Render_RectTypeDef LeftShrubTop =
    {
        15,
        (int16_t)(GroundY - 88),
        (uint16_t)(BUILDING_LEFT_X - 30),
        31U
    };
    const Render_RectTypeDef RightShrubTop =
    {
        (int16_t)(BUILDING_RIGHT_X + 15),
        (int16_t)(GroundY - 88),
        (uint16_t)((int16_t)RENDER_WIDTH - BUILDING_RIGHT_X - 30),
        31U
    };
    const Render_RectTypeDef LeftShrubHighlight =
    {
        22,
        (int16_t)(GroundY - 78),
        22U,
        8U
    };
    const Render_RectTypeDef RightShrubHighlight =
    {
        (int16_t)(BUILDING_RIGHT_X + 22),
        (int16_t)(GroundY - 78),
        22U,
        8U
    };

    Render_FillRect(Target, &EntranceRecess, COLOUR_WINDOW_RECESS);
    Render_FillRect(Target, &EntranceFrame, COLOUR_FACADE_TRIM);
    Render_FillRect(Target, &LeftDoor, COLOUR_WINDOW);
    Render_FillRect(Target, &RightDoor, COLOUR_WINDOW);
    Render_FillRect(Target, &DoorDivider, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &LeftHandle, COLOUR_BUCKET_HIGHLIGHT);
    Render_FillRect(Target, &RightHandle, COLOUR_BUCKET_HIGHLIGHT);

    Render_FillRect(Target, &CanopyShadow, COLOUR_BUILDING_SHADOW);
    Render_FillRect(Target, &Canopy, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &CanopyHighlight, COLOUR_TRACK_HIGHLIGHT);

    Render_FillRect(Target, &LeftColumn, COLOUR_FACADE_TRIM);
    Render_FillRect(Target, &RightColumn, COLOUR_FACADE_TRIM);
    Render_FillRect(Target, &LeftColumnBase, COLOUR_WINDOW_RECESS);
    Render_FillRect(Target, &RightColumnBase, COLOUR_WINDOW_RECESS);

    Render_FillRect(Target, &Sign, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &SignInset, COLOUR_WINDOW_WARM_LIGHT);
    Render_FillRect(Target, &LeftLamp, COLOUR_WINDOW_WARM_LIGHT);
    Render_FillRect(Target, &RightLamp, COLOUR_WINDOW_WARM_LIGHT);

    Render_FillRect(Target, &LeftPlanter, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &RightPlanter, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &LeftShrubBase, COLOUR_SHRUB_DARK);
    Render_FillRect(Target, &RightShrubBase, COLOUR_SHRUB_DARK);
    Render_FillRect(Target, &LeftShrubTop, COLOUR_SHRUB_GREEN);
    Render_FillRect(Target, &RightShrubTop, COLOUR_SHRUB_GREEN);
    Render_FillRect(Target, &LeftShrubHighlight, COLOUR_SHRUB_HIGHLIGHT);
    Render_FillRect(Target, &RightShrubHighlight, COLOUR_SHRUB_HIGHLIGHT);

    Render_FillRect(Target, &Pavement, COLOUR_BUILDING_SHADOW);
}

static void WindowWasher_DrawBalconySection(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, int16_t InnerEndX)
{
    const Render_RectTypeDef Deck = { X, Y, Width, BALCONY_THICKNESS };
    const Render_RectTypeDef Rail = { X, (int16_t)(Y - 19), Width, 4U };
    const Render_RectTypeDef InnerPost = { (int16_t)(InnerEndX - 2), (int16_t)(Y - 20), 4U, 36U };
    const int16_t OuterPostX = (X < InnerEndX) ? (int16_t)(X + 2) : (int16_t)(X + Width - 5U);
    const Render_RectTypeDef OuterPost = { OuterPostX, (int16_t)(Y - 20), 4U, 36U };

    Render_FillRect(Target, &Deck, COLOUR_BALCONY);
    Render_FillRect(Target, &Rail, COLOUR_BALCONY_HIGHLIGHT);
    Render_FillRect(Target, &InnerPost, COLOUR_BALCONY);
    Render_FillRect(Target, &OuterPost, COLOUR_BALCONY);

    for(int16_t PostX = (int16_t)(X + 18); PostX < (int16_t)(X + Width - 8U); PostX = (int16_t)(PostX + 28))
    {
        const Render_RectTypeDef Post = { PostX, (int16_t)(Y - 18), 3U, 18U };

        Render_FillRect(Target, &Post, COLOUR_BALCONY);
    }
}

static void WindowWasher_DrawBalconies(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / BALCONY_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % BALCONY_STEP_Y);

    for(int8_t ScreenRow = -1; ScreenRow < 4; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t BalconyY = (int16_t)((ScreenRow * (int16_t)BALCONY_STEP_Y) + ScrollOffset + BALCONY_VERTICAL_OFFSET);

        if((WorldRow < BALCONY_CLEAR_START) || ((int32_t)BalconyY + (int32_t)BALCONY_THICKNESS <= 0) || ((int32_t)BalconyY - 20 >= (int32_t)RENDER_HEIGHT))
        {
            continue;
        }

        const uint32_t Pattern = WindowWasher_BalconyPattern(Game, WorldRow);
        const bool FromLeft = WindowWasher_BalconyFromLeft(Pattern);
        const bool HasCentreGap = WindowWasher_BalconyHasCentreGap(Pattern);
        const int16_t BalconyEndX = WindowWasher_BalconyEndX(Pattern, FromLeft);
        const int16_t BalconyX = FromLeft ? BUILDING_LEFT_X : BalconyEndX;
        const uint16_t BalconyWidth = (uint16_t)(FromLeft ? (BalconyEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - BalconyEndX));
        const int16_t GapLeftX = WindowWasher_BalconyGapLeftX(Pattern);
        const int16_t GapRightX = (int16_t)(GapLeftX + 160U + ((Pattern >> 8U) % 41U));

        if(HasCentreGap)
        {
            WindowWasher_DrawBalconySection(Target, (int16_t)(BUILDING_LEFT_X - BALCONY_BUILDING_WRAP), BalconyY, (uint16_t)(GapLeftX - BUILDING_LEFT_X + BALCONY_BUILDING_WRAP), GapLeftX);

            WindowWasher_DrawBalconySection(Target, GapRightX, BalconyY, (uint16_t)(BUILDING_RIGHT_X - GapRightX + BALCONY_BUILDING_WRAP), GapRightX);
        }
        else
        {
            const int16_t WrappedX = FromLeft ? (int16_t)(BalconyX - BALCONY_BUILDING_WRAP) : BalconyX;
            const uint16_t WrappedWidth = (uint16_t)(BalconyWidth + BALCONY_BUILDING_WRAP);

            WindowWasher_DrawBalconySection(Target, WrappedX, BalconyY, WrappedWidth, BalconyEndX);
        }
    }
}

static void WindowWasher_DrawPlatform(Render_TargetTypeDef *Target, const WindowWasher_PlatformTypeDef *Platform)
{
    const Render_RectTypeDef LeftCable = { PLATFORM_LEFT_X - 2, 0, 4U, (uint16_t)Platform->LeftY };
    const Render_RectTypeDef RightCable = { PLATFORM_RIGHT_X - 2, 0, 4U, (uint16_t)Platform->RightY };
    const Render_PointTypeDef Edge[] =
    {
        { PLATFORM_LEFT_X - 4, Platform->LeftY - 4 },
        { PLATFORM_RIGHT_X + 4, Platform->RightY - 4 },
        { PLATFORM_RIGHT_X + 4, (int16_t)(Platform->RightY + PLATFORM_THICKNESS + 4U) },
        { PLATFORM_LEFT_X - 4, (int16_t)(Platform->LeftY + PLATFORM_THICKNESS + 4U) }
    };
    const Render_PointTypeDef Surface[] =
    {
        { PLATFORM_LEFT_X, Platform->LeftY },
        { PLATFORM_RIGHT_X, Platform->RightY },
        { PLATFORM_RIGHT_X, (int16_t)(Platform->RightY + PLATFORM_THICKNESS) },
        { PLATFORM_LEFT_X, (int16_t)(Platform->LeftY + PLATFORM_THICKNESS) }
    };
    const Render_RectTypeDef LeftConnector = { PLATFORM_LEFT_X - 6, (int16_t)(Platform->LeftY - 9), 14U, 19U };
    const Render_RectTypeDef RightConnector = { PLATFORM_RIGHT_X - 8, (int16_t)(Platform->RightY - 9), 14U, 19U };

    Render_FillRect(Target, &LeftCable, COLOUR_CABLE);
    Render_FillRect(Target, &RightCable, COLOUR_CABLE);
    Render_DrawPolygon(Target, Edge, 4U, COLOUR_TRACK_STEEL);
    Render_DrawPolygon(Target, Surface, 4U, COLOUR_TRACK_HIGHLIGHT);
    Render_FillRect(Target, &LeftConnector, COLOUR_BEAM_ORANGE);
    Render_FillRect(Target, &RightConnector, COLOUR_BEAM_ORANGE);
}

static void WindowWasher_DrawWasher(Render_TargetTypeDef *Target, const WindowWasher_PlatformTypeDef *Platform, const WindowWasher_FigureTypeDef *Figure, bool Crashed)
{
    const int16_t CentreX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t TrackY = WindowWasher_PlatformYAtX(Platform, CentreX);
    const int16_t BucketY = (int16_t)(TrackY + BUCKET_HANG_OFFSET);
    const Render_RectTypeDef Trolley = { (int16_t)(CentreX - 23), (int16_t)(TrackY - 7), 46U, 12U };
    const Render_RectTypeDef LeftWheel = { (int16_t)(CentreX - 17), (int16_t)(TrackY - 3), 8U, 7U };
    const Render_RectTypeDef RightWheel = { (int16_t)(CentreX + 9), (int16_t)(TrackY - 3), 8U, 7U };
    const Render_RectTypeDef LeftHanger = { (int16_t)(CentreX - 17), (int16_t)(TrackY + 5), 3U, (uint16_t)(BUCKET_HANG_OFFSET - 2) };
    const Render_RectTypeDef RightHanger = { (int16_t)(CentreX + 14), (int16_t)(TrackY + 5), 3U, (uint16_t)(BUCKET_HANG_OFFSET - 2) };
    const Render_RectTypeDef BucketEdge = { (int16_t)(CentreX - (BUCKET_WIDTH / 2U)), BucketY, BUCKET_WIDTH, BUCKET_HEIGHT };
    const Render_RectTypeDef BucketInterior = { (int16_t)(CentreX - (BUCKET_WIDTH / 2U) + 4U), (int16_t)(BucketY + 4), BUCKET_WIDTH - 8U, BUCKET_HEIGHT - 8U };
    const Render_RectTypeDef BucketFront = { (int16_t)(CentreX - (BUCKET_WIDTH / 2U) + 4U), (int16_t)(BucketY + 13), BUCKET_WIDTH - 8U, BUCKET_HEIGHT - 17U };
    const Render_RectTypeDef BucketRim = { (int16_t)(CentreX - (BUCKET_WIDTH / 2U) - 2U), (int16_t)(BucketY + 8), BUCKET_WIDTH + 4U, 5U };
    const Render_RectTypeDef SqueegeeHandle = { (int16_t)(CentreX + 11), (int16_t)(BucketY + 12), 3U, 25U };
    const Render_RectTypeDef SqueegeeHead = { (int16_t)(CentreX + 5), (int16_t)(BucketY + 34), 15U, 4U };
    const Render_RectTypeDef BrushHandle = { (int16_t)(CentreX - 15), (int16_t)(BucketY + 12), 3U, 19U };
    const Render_RectTypeDef BrushHead = { (int16_t)(CentreX - 20), (int16_t)(BucketY + 28), 13U, 7U };
    const Render_RectTypeDef BrushBristlesOne = { (int16_t)(CentreX - 18), (int16_t)(BucketY + 34), 2U, 4U };
    const Render_RectTypeDef BrushBristlesTwo = { (int16_t)(CentreX - 13), (int16_t)(BucketY + 34), 2U, 4U };

    Render_FillRect(Target, &Trolley, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &LeftWheel, COLOUR_TRACK_HIGHLIGHT);
    Render_FillRect(Target, &RightWheel, COLOUR_TRACK_HIGHLIGHT);
    Render_FillRect(Target, &LeftHanger, COLOUR_CABLE);
    Render_FillRect(Target, &RightHanger, COLOUR_CABLE);
    Render_FillRect(Target, &BucketEdge, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &BucketInterior, COLOUR_BUCKET_METAL);
    Render_DrawImage(Target, &WindowWasher_WasherImage, (int16_t)(CentreX - (WASHER_IMAGE_SIZE / 2U)), Crashed ? (int16_t)(Figure->PositionY / FIGURE_FIXED_SCALE) : (int16_t)(BucketY - 43));
    Render_FillRect(Target, &BucketFront, COLOUR_BUCKET_METAL);
    Render_FillRect(Target, &BucketRim, COLOUR_BUCKET_HIGHLIGHT);
    Render_FillRect(Target, &SqueegeeHandle, COLOUR_TOOL_HANDLE);
    Render_FillRect(Target, &SqueegeeHead, COLOUR_TOOL_RUBBER);
    Render_FillRect(Target, &BrushHandle, COLOUR_TOOL_HANDLE);
    Render_FillRect(Target, &BrushHead, COLOUR_TOOL_BRISTLES);
    Render_FillRect(Target, &BrushBristlesOne, COLOUR_TOOL_BRISTLES);
    Render_FillRect(Target, &BrushBristlesTwo, COLOUR_TOOL_BRISTLES);
}


static void WindowWasher_FormatScore(uint32_t Score, char *Buffer, size_t BufferSize)
{
    char Digits[10];
    size_t DigitCount = 0U;
    size_t OutputIndex = 0U;

    if((Buffer == NULL) || (BufferSize == 0U))
    {
        return;
    }

    do
    {
        Digits[DigitCount++] = (char)('0' + (Score % 10U));
        Score /= 10U;
    }
    while((Score > 0U) && (DigitCount < sizeof(Digits)));

    while((DigitCount < SCORE_MINIMUM_DIGITS) && (DigitCount < sizeof(Digits)))
    {
        Digits[DigitCount++] = '0';
    }

    while((DigitCount > 0U) && ((OutputIndex + 1U) < BufferSize))
    {
        Buffer[OutputIndex++] = Digits[--DigitCount];
    }

    Buffer[OutputIndex] = '\0';
}

/**
 * @brief Measure the horizontal advance of a single-line string.
 */
static uint16_t WindowWasher_MeasureTextWidth(const Font *FontAsset, const char *Text)
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
 * @brief Draw the Window Washer title on a hanging scoreboard-style plaque.
 */
static void WindowWasher_DrawSplashPlaque(Render_TargetTypeDef *Target, const Render_RectTypeDef *Bounds, uint64_t ElapsedMilliseconds)
{
    static const char Title[] = "WINDOW WASHER";
    const uint16_t TextWidth = WindowWasher_MeasureTextWidth(&OpenSans36, Title);
    const uint16_t PlaqueWidth = (uint16_t)(TextWidth + 62U);
    const uint16_t PlaqueHeight = 64U;
    const int16_t PlaqueX = (int16_t)(Bounds->X + (((int16_t)Bounds->Width - (int16_t)PlaqueWidth) / 2));
    const int16_t PlaqueY = (int16_t)(Bounds->Y + 62);
    const int16_t TextX = (int16_t)(PlaqueX + (((int16_t)PlaqueWidth - (int16_t)TextWidth) / 2));
    const int16_t TextY = (int16_t)(PlaqueY + 10);
    const int16_t LeftHangerX = (int16_t)(PlaqueX + 35);
    const int16_t RightHangerX = (int16_t)(PlaqueX + (int16_t)PlaqueWidth - 41);
    const int16_t HangerTopY = (int16_t)(Bounds->Y + 5);
    const uint16_t HangerHeight = (uint16_t)(PlaqueY - HangerTopY);

    const Render_RectTypeDef LeftHangerShadow =
    {
        (int16_t)(LeftHangerX + 2),
        HangerTopY,
        5U,
        HangerHeight
    };

    const Render_RectTypeDef RightHangerShadow =
    {
        (int16_t)(RightHangerX + 2),
        HangerTopY,
        5U,
        HangerHeight
    };

    const Render_RectTypeDef LeftHanger =
    {
        LeftHangerX,
        HangerTopY,
        4U,
        HangerHeight
    };

    const Render_RectTypeDef RightHanger =
    {
        RightHangerX,
        HangerTopY,
        4U,
        HangerHeight
    };

    const Render_RectTypeDef LeftMount =
    {
        (int16_t)(LeftHangerX - 5),
        (int16_t)(PlaqueY - 5),
        14U,
        11U
    };

    const Render_RectTypeDef RightMount =
    {
        (int16_t)(RightHangerX - 5),
        (int16_t)(PlaqueY - 5),
        14U,
        11U
    };

    const Render_RectTypeDef Shadow =
    {
        (int16_t)(PlaqueX + 4),
        (int16_t)(PlaqueY + 5),
        PlaqueWidth,
        PlaqueHeight
    };

    const Render_RectTypeDef OuterFrame =
    {
        PlaqueX,
        PlaqueY,
        PlaqueWidth,
        PlaqueHeight
    };

    const Render_RectTypeDef InnerPanel =
    {
        (int16_t)(PlaqueX + 4),
        (int16_t)(PlaqueY + 4),
        PlaqueWidth - 8U,
        PlaqueHeight - 8U
    };

    const Render_RectTypeDef TopHighlight =
    {
        (int16_t)(PlaqueX + 8),
        (int16_t)(PlaqueY + 7),
        PlaqueWidth - 16U,
        3U
    };

    const Render_RectTypeDef BottomAccent =
    {
        (int16_t)(PlaqueX + 12),
        (int16_t)(PlaqueY + (int16_t)PlaqueHeight - 8),
        PlaqueWidth - 24U,
        2U
    };

    Render_FillRect(Target, &LeftHangerShadow, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &RightHangerShadow, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &LeftHanger, COLOUR_PLATFORM_EDGE);
    Render_FillRect(Target, &RightHanger, COLOUR_PLATFORM_EDGE);

    Render_FillRect(Target, &Shadow, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &OuterFrame, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &InnerPanel, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &TopHighlight, COLOUR_BUCKET_HIGHLIGHT);
    Render_FillRect(Target, &BottomAccent, COLOUR_PLATFORM_EDGE);

    Render_FillRect(Target, &LeftMount, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &RightMount, COLOUR_SCORE_SHADOW);

    {
        const bool OddLightsOn = ((ElapsedMilliseconds / 1000ULL) & 1ULL) != 0ULL;

        for(uint16_t LightIndex = 0U; LightIndex < 7U; LightIndex++)
        {
            const bool LightOn = ((LightIndex & 1U) != 0U) == OddLightsOn;
            const int16_t LightX = (int16_t)(PlaqueX + 16 + ((int16_t)LightIndex * ((int16_t)(PlaqueWidth - 40U) / 6)));
            const Render_RectTypeDef LightShadow =
            {
                (int16_t)(LightX + 2),
                (int16_t)(PlaqueY + 3),
                9U,
                9U
            };
            const Render_RectTypeDef LightHousing =
            {
                LightX,
                (int16_t)(PlaqueY + 1),
                9U,
                9U
            };
            const Render_RectTypeDef LightCore =
            {
                (int16_t)(LightX + 2),
                (int16_t)(PlaqueY + 3),
                5U,
                5U
            };

            Render_FillRect(Target, &LightShadow, COLOUR_SCORE_SHADOW);
            Render_FillRect(Target, &LightHousing, COLOUR_PLATFORM_EDGE);
            Render_FillRect(Target, &LightCore, LightOn ? COLOUR_SPARKLE : COLOUR_TRACK_STEEL);
        }
    }

    Render_DrawText(Target, &OpenSans36, Title, (int16_t)(TextX + 2), (int16_t)(TextY + 2), COLOUR_SCORE_SHADOW);
    Render_DrawText(Target, &OpenSans36, Title, TextX, TextY, COLOUR_SCORE_TEXT);
}

static void WindowWasher_DrawScore(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    static const char HighScorePrefix[] = "HI ";

    char ScoreText[11];
    char HighScoreText[14];
    uint16_t ScoreWidth;
    uint16_t HighScoreWidth;
    int16_t ScoreX;
    int16_t HighScoreX;
    size_t HighScoreIndex;
    Render_ColourIndexTypeDef ScoreColour;

    const Render_RectTypeDef Shadow =
    {
        (int16_t)(SCORE_PANEL_X + 3),
        (int16_t)(SCORE_PANEL_Y + 4),
        SCORE_PANEL_WIDTH,
        SCORE_PANEL_HEIGHT
    };

    const Render_RectTypeDef OuterFrame =
    {
        SCORE_PANEL_X,
        SCORE_PANEL_Y,
        SCORE_PANEL_WIDTH,
        SCORE_PANEL_HEIGHT
    };

    const Render_RectTypeDef InnerPanel =
    {
        (int16_t)(SCORE_PANEL_X + 3),
        (int16_t)(SCORE_PANEL_Y + 3),
        SCORE_PANEL_WIDTH - 6U,
        SCORE_PANEL_HEIGHT - 6U
    };

    const Render_RectTypeDef TopHighlight =
    {
        (int16_t)(SCORE_PANEL_X + 6),
        (int16_t)(SCORE_PANEL_Y + 6),
        SCORE_PANEL_WIDTH - 12U,
        2U
    };

    const Render_RectTypeDef Divider =
    {
        (int16_t)(SCORE_PANEL_X + (int16_t)SCORE_CURRENT_AREA_WIDTH),
        (int16_t)(SCORE_PANEL_Y + 7),
        2U,
        SCORE_PANEL_HEIGHT - 14U
    };

    const Render_RectTypeDef SparkleVertical =
    {
        (int16_t)(SCORE_PANEL_X + 12),
        (int16_t)(SCORE_PANEL_Y + 10),
        3U,
        15U
    };

    const Render_RectTypeDef SparkleHorizontal =
    {
        (int16_t)(SCORE_PANEL_X + 6),
        (int16_t)(SCORE_PANEL_Y + 16),
        15U,
        3U
    };

    ScoreColour = (WindowWasher_ScorePulseFrames > 0U) ? COLOUR_SPARKLE : COLOUR_SCORE_TEXT;

    if(WindowWasher_ScorePulseFrames > 0U)
    {
        WindowWasher_ScorePulseFrames--;
    }

    WindowWasher_FormatScore(Game->Score, ScoreText, sizeof(ScoreText));

    HighScoreIndex = 0U;

    while((HighScorePrefix[HighScoreIndex] != '\0') && ((HighScoreIndex + 1U) < sizeof(HighScoreText)))
    {
        HighScoreText[HighScoreIndex] = HighScorePrefix[HighScoreIndex];
        HighScoreIndex++;
    }

    WindowWasher_FormatScore(WindowWasher_HighScore, &HighScoreText[HighScoreIndex], sizeof(HighScoreText) - HighScoreIndex);

    ScoreWidth = WindowWasher_MeasureTextWidth(&OpenSans20, ScoreText);
    HighScoreWidth = WindowWasher_MeasureTextWidth(&OpenSans20, HighScoreText);

    ScoreX = (int16_t)(SCORE_PANEL_X + 25 + (((int16_t)SCORE_CURRENT_AREA_WIDTH - 25 - (int16_t)ScoreWidth) / 2));

    HighScoreX = (int16_t)(SCORE_PANEL_X + (int16_t)SCORE_CURRENT_AREA_WIDTH + (((int16_t)SCORE_PANEL_WIDTH - (int16_t)SCORE_CURRENT_AREA_WIDTH - (int16_t)HighScoreWidth) / 2));

    Render_FillRect(Target, &Shadow, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &OuterFrame, COLOUR_SCORE_SHADOW);
    Render_FillRect(Target, &InnerPanel, COLOUR_TRACK_STEEL);
    Render_FillRect(Target, &TopHighlight, COLOUR_BUCKET_HIGHLIGHT);
    Render_FillRect(Target, &Divider, COLOUR_PLATFORM_EDGE);

    Render_FillRect(Target, &SparkleVertical, COLOUR_SPARKLE);
    Render_FillRect(Target, &SparkleHorizontal, COLOUR_SPARKLE);

    Render_DrawText(Target, &OpenSans20, ScoreText, (int16_t)(ScoreX + 1), (int16_t)(SCORE_PANEL_Y + 8), COLOUR_SCORE_SHADOW);

    Render_DrawText(Target, &OpenSans20, ScoreText, ScoreX, (int16_t)(SCORE_PANEL_Y + 7), ScoreColour);

    Render_DrawText(Target, &OpenSans20, HighScoreText, (int16_t)(HighScoreX + 1), (int16_t)(SCORE_PANEL_Y + 8), COLOUR_SCORE_SHADOW);

    Render_DrawText(Target, &OpenSans20, HighScoreText, HighScoreX, (int16_t)(SCORE_PANEL_Y + 7), COLOUR_BUCKET_HIGHLIGHT);
}


bool WindowWasher_Init(void)
{
    WindowWasher_Input.LeftSlider = 0;
    WindowWasher_Input.RightSlider = 0;
    WindowWasher_Game.ElapsedMilliseconds = 0U;
    WindowWasher_PendingDeltaTimeMilliseconds = 0U;
    WindowWasher_ScorePulseFrames = 0U;
    WindowWasher_Paused = false;

    WindowWasher_ResetGame(&WindowWasher_Game, &WindowWasher_Figure);


    WindowWasher_BuildWasherImage();
    WindowWasher_Initialized = true;

    return true;
}

void WindowWasher_Update(uint32_t DeltaTimeMilliseconds)
{
    int32_t LeftSliderValue;
    int32_t RightSliderValue;

    if(!WindowWasher_Initialized || WindowWasher_Paused)
    {
        return;
    }

    if(Input_Get_Value(INPUT_LEFT_SLIDER_NUMBER, &LeftSliderValue))
    {
        WindowWasher_Input.LeftSlider = WindowWasher_ConvertSliderValue(LeftSliderValue);
    }

    if(Input_Get_Value(INPUT_RIGHT_SLIDER_NUMBER, &RightSliderValue))
    {
        WindowWasher_Input.RightSlider = WindowWasher_ConvertSliderValue(RightSliderValue);
    }

    WindowWasher_PendingDeltaTimeMilliseconds += DeltaTimeMilliseconds;
}

bool WindowWasher_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL)
    {
        return false;
    }

    for(uint16_t PaletteIndex = 0U; PaletteIndex < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; PaletteIndex++)
    {
        Palette[PaletteIndex] = 0U;
    }

    for(uint16_t PaletteIndex = 0U; PaletteIndex < (uint16_t)(sizeof(WindowWasher_Palette) / sizeof(WindowWasher_Palette[0])); PaletteIndex++)
    {
        Palette[PaletteIndex] = WindowWasher_Palette[PaletteIndex];
    }

    return true;
}

bool WindowWasher_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    WindowWasher_GameTypeDef SplashGame = { 0 };
    WindowWasher_PlatformTypeDef SplashPlatform;
    WindowWasher_FigureTypeDef SplashFigure = { 0 };
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

    WindowWasher_SplashElapsedMilliseconds += 33ULL;

    SplashGame.BuildingScroll = 184;
    SplashGame.LayoutSeed = 0x57A5C3E1U;
    SplashGame.ElapsedMilliseconds = WindowWasher_SplashElapsedMilliseconds;
    SplashGame.Score = 0U;
    SplashGame.Crashed = false;

    SplashPlatform.LeftY = 318;
    SplashPlatform.RightY = 292;

    SplashFigure.PositionX = ((int32_t)RENDER_WIDTH / 2) * FIGURE_FIXED_SCALE;
    SplashFigure.VelocityX = 0;
    SplashFigure.PositionY = 0;
    SplashFigure.VelocityY = 0;
    SplashFigure.OffscreenMilliseconds = 0U;

    WindowWasher_BuildWasherImage();

    Render_FillRect(Target, &SplashBounds, COLOUR_SKY);
    WindowWasher_DrawBackgroundLayer(Target, &SplashGame);
    WindowWasher_DrawBuilding(Target, &SplashGame);
    WindowWasher_DrawBalconies(Target, &SplashGame);
    WindowWasher_DrawPlatform(Target, &SplashPlatform);
    WindowWasher_DrawWasher(Target, &SplashPlatform, &SplashFigure, false);
    WindowWasher_DrawSplashPlaque(Target, &SplashBounds, WindowWasher_SplashElapsedMilliseconds);

    return true;
}

void WindowWasher_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    WindowWasher_PlatformTypeDef Platform;

    if(!WindowWasher_Initialized || WindowWasher_Paused)
    {
        return;
    }

    Frame = Display_AcquireFrame();

    if(Frame == NULL)
    {
        return;
    }

    Platform = WindowWasher_MakePlatform(&WindowWasher_Game, &WindowWasher_Input);

    Target.Pixels = Frame->Pixels;
    Target.Width = Frame->Width;
    Target.Height = Frame->Height;
    Target.StridePixels = Frame->StridePixels;

    {
        uint32_t DeltaTimeMilliseconds = WindowWasher_PendingDeltaTimeMilliseconds;

        /* Prevent a long pause from causing an excessive physics step. */
        if(DeltaTimeMilliseconds > 100U)
        {
            DeltaTimeMilliseconds = 100U;
        }

        WindowWasher_PendingDeltaTimeMilliseconds = 0U;

        WindowWasher_UpdateGame(&WindowWasher_Game, &WindowWasher_Figure, &Platform, DeltaTimeMilliseconds);
    }

    Render_ResetClipRect();
    Render_Clear(&Target, COLOUR_SKY);

    WindowWasher_DrawBackground(&Target, &WindowWasher_Game);
    WindowWasher_DrawBuilding(&Target, &WindowWasher_Game);
    WindowWasher_DrawHotelEntrance(&Target, &WindowWasher_Game);
    WindowWasher_DrawBalconies(&Target, &WindowWasher_Game);
    WindowWasher_DrawPlatform(&Target, &Platform);
    WindowWasher_DrawWasher(&Target, &Platform, &WindowWasher_Figure, WindowWasher_Game.Crashed);
    WindowWasher_DrawScore(&Target, &WindowWasher_Game);

    (void)Display_PresentFrame(Frame);
}

void WindowWasher_Pause(void)
{
    WindowWasher_Paused = true;
}

void WindowWasher_Resume(void)
{
    WindowWasher_Paused = false;
}

void WindowWasher_Shutdown(void)
{
    WindowWasher_Initialized = false;
    WindowWasher_Paused = false;
    WindowWasher_PendingDeltaTimeMilliseconds = 0U;
}