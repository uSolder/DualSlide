/**
 * @file window_washer.c
 * @brief Window-washer platform game demonstration for the DualSlide application system.
 */

#include "window_washer.h"

#include "audio.h"
#include "display.h"
#include "input.h"
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
#define PLATFORM_THICKNESS (8U)
#define FIGURE_WIDTH (48U)
#define FIGURE_HEIGHT (82U)
#define BUCKET_HANG_OFFSET (25)
#define BUCKET_WIDTH (56U)
#define BUCKET_HEIGHT (34U)
#define FIGURE_FIXED_SCALE (256)
#define FIGURE_SLIDE_GRAVITY (128)
#define WASHER_IMAGE_SIZE (64U)
#define BALCONY_STEP_Y (276U)
#define BALCONY_VERTICAL_OFFSET (84)
#define BALCONY_THICKNESS (16U)
#define BALCONY_BUILDING_WRAP (14U)
#define BALCONY_CLEAR_START (2)
#define BUILDING_SPEED_SCALE (256U)
#define BUILDING_START_SPEED (384U)
#define BUILDING_FULL_SPEED (1075U)
#define BUILDING_RAMP_MILLISECONDS (240000ULL)
#define CLEAN_WINDOW_TRACKED_COUNT (64U)
#define CLEAN_WINDOW_SPARKLE_FRAMES (45U)
#define SLIDER_TRAVEL (150)
#define MOUSE_SLIDER_PER_PIXEL (1)
#define CRASH_FRAME_COUNT (55U)

#define INPUT_SLIDER_MINIMUM (0)
#define INPUT_SLIDER_MAXIMUM (65535)
#define INPUT_SLIDER_CENTRE  ((INPUT_SLIDER_MAXIMUM + 1) / 2)

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
    COLOUR_BEAM_ORANGE_HIGHLIGHT = 48U
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
    uint16_t BuildingSubpixel;
    uint32_t LayoutSeed;
    uint64_t ElapsedMilliseconds;
    bool Crashed;
    uint16_t CrashFrameCount;
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

static WindowWasher_InputTypeDef WindowWasher_Input;
static WindowWasher_GameTypeDef WindowWasher_Game;
static WindowWasher_FigureTypeDef WindowWasher_Figure;
static uint32_t WindowWasher_PendingDeltaTimeMilliseconds;
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
        for(uint16_t Column = 0U; Column < Width; Column++)
        {
            WindowWasher_WasherPixels[((Y + Row) * WASHER_IMAGE_SIZE) + X + Column] = Colour;
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

static WindowWasher_PlatformTypeDef WindowWasher_MakePlatform(const WindowWasher_InputTypeDef *Input)
{
    WindowWasher_PlatformTypeDef Platform;

    Platform.LeftY = (int16_t)(PLATFORM_BASE_Y - (Input->LeftSlider / 2));
    Platform.RightY = (int16_t)(PLATFORM_BASE_Y - (Input->RightSlider / 2));

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

static void WindowWasher_ResetGame(WindowWasher_GameTypeDef *Game, WindowWasher_FigureTypeDef *Figure)
{
    static uint32_t ResetNonce = 0U;

    Game->BuildingScroll = 0;
    Game->BuildingSubpixel = 0U;
    Game->LayoutSeed = WindowWasher_Hash((uint32_t)Game->ElapsedMilliseconds ^ (uint32_t)(Game->ElapsedMilliseconds >> 32U) ^ (++ResetNonce * 0xA511E9B3U));
    Game->ElapsedMilliseconds = 0U;
    Game->Crashed = false;
    Game->CrashFrameCount = 0U;

    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_TRACKED_COUNT; WindowIndex++)
    {
        Game->CleanWindows[WindowIndex].Active = false;
    }

    Figure->PositionX = ((int32_t)RENDER_WIDTH / 2) * FIGURE_FIXED_SCALE;
    Figure->VelocityX = 0;
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

    for(int8_t ScreenRow = -1; ScreenRow < 9; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t WindowY = (int16_t)((ScreenRow * (int16_t)WINDOW_STEP_Y) + ScrollOffset + 8);

        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(WINDOW_GRID_LEFT_X + ((int16_t)Column * WINDOW_STEP_X));

            if(WindowWasher_WindowIsDirty(Game, WorldRow, Column) && (WindowWasher_FindCleanWindow(Game, WorldRow, Column) == NULL) && ((FigureX + ((int16_t)FIGURE_WIDTH / 2)) > WindowX) && ((FigureX - ((int16_t)FIGURE_WIDTH / 2)) < (WindowX + (int16_t)WINDOW_WIDTH)) && ((FigureY + (int16_t)FIGURE_HEIGHT) > WindowY) && (FigureY < (WindowY + (int16_t)WINDOW_HEIGHT)))
            {
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
            }
        }
    }
}

static void WindowWasher_UpdateGame(WindowWasher_GameTypeDef *Game, WindowWasher_FigureTypeDef *Figure, const WindowWasher_PlatformTypeDef *Platform, uint32_t DeltaTimeMilliseconds)
{
    const uint64_t ElapsedMilliseconds = Game->ElapsedMilliseconds < BUILDING_RAMP_MILLISECONDS ? Game->ElapsedMilliseconds : BUILDING_RAMP_MILLISECONDS;
    const uint16_t BuildingSpeed = (uint16_t)(BUILDING_START_SPEED + ((ElapsedMilliseconds * (BUILDING_FULL_SPEED - BUILDING_START_SPEED)) / BUILDING_RAMP_MILLISECONDS));

    Game->ElapsedMilliseconds += DeltaTimeMilliseconds;

    if(Game->Crashed)
    {
        Game->CrashFrameCount++;

        if(Game->CrashFrameCount >= CRASH_FRAME_COUNT)
        {
            WindowWasher_ResetGame(Game, Figure);
        }

        return;
    }

    Figure->VelocityX += ((int32_t)(Platform->RightY - Platform->LeftY) * FIGURE_SLIDE_GRAVITY) / (PLATFORM_RIGHT_X - PLATFORM_LEFT_X);
    Figure->VelocityX = (Figure->VelocityX * 250) / 256;
    Figure->PositionX += Figure->VelocityX;
    Game->BuildingSubpixel = (uint16_t)(Game->BuildingSubpixel + BuildingSpeed);
    Game->BuildingScroll += (int32_t)(Game->BuildingSubpixel / BUILDING_SPEED_SCALE);
    Game->BuildingSubpixel = (uint16_t)(Game->BuildingSubpixel % BUILDING_SPEED_SCALE);
    WindowWasher_UpdateWindowCleaning(Game, Platform, Figure);

    if(((Figure->PositionX / FIGURE_FIXED_SCALE) < (PLATFORM_LEFT_X + ((int16_t)FIGURE_WIDTH / 2))) || ((Figure->PositionX / FIGURE_FIXED_SCALE) > (PLATFORM_RIGHT_X - ((int16_t)FIGURE_WIDTH / 2))) || WindowWasher_FigureHitsBalcony(Game, Platform, Figure))
    {
        Game->Crashed = true;
        Game->CrashFrameCount = 0U;
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
        case 0U: /* Blinds */
            for(uint8_t Blind = 0U; Blind < 4U; Blind++)
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

        default: /* Office party silhouettes */
            for(uint8_t Guest = 0U; Guest < 4U; Guest++)
            {
                const Render_RectTypeDef Head = { (int16_t)(WindowX + 13 + (Guest * 12U)), (int16_t)(WindowY + 17 + ((Guest & 1U) * 3U)), 6U, 6U };
                const Render_RectTypeDef Body = { (int16_t)(WindowX + 11 + (Guest * 12U)), (int16_t)(WindowY + 23 + ((Guest & 1U) * 3U)), 10U, 13U };

                Render_FillRect(Target, &Head, COLOUR_WINDOW_SILHOUETTE);
                Render_FillRect(Target, &Body, COLOUR_WINDOW_SILHOUETTE);
            }
            break;
    }
}

static void WindowWasher_DrawBackground(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
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
        Render_FillRect(Target, &BandedCloudOne, COLOUR_BACKGROUND_CLOUD);
        Render_FillRect(Target, &BandedCloudTwo, COLOUR_BACKGROUND_CLOUD);
    }

    if(MountainBaseY < ((int16_t)RENDER_HEIGHT + 180))
    {
        Render_DrawPolygon(Target, Mountains, (uint8_t)(sizeof(Mountains) / sizeof(Mountains[0])), COLOUR_BACKGROUND_MOUNTAIN);
    }

    for(uint8_t BlockIndex = 0U; BlockIndex < (uint8_t)(sizeof(CityBlocks) / sizeof(CityBlocks[0])); BlockIndex++)
    {
        const Render_RectTypeDef Light = { (int16_t)(CityBlocks[BlockIndex].X + 7), (int16_t)(CityBlocks[BlockIndex].Y + 13), 4U, 8U };

        Render_FillRect(Target, &CityBlocks[BlockIndex], COLOUR_BACKGROUND_CITY);
        Render_FillRect(Target, &Light, COLOUR_BACKGROUND_CITY_LIGHT);
    }

    Render_FillRect(Target, &AntennaBase, COLOUR_BACKGROUND_CITY);
    Render_FillRect(Target, &AntennaLowerMast, COLOUR_BACKGROUND_CITY);
    Render_FillRect(Target, &AntennaUpperMast, COLOUR_BACKGROUND_CITY);

    if(AntennaLightOn)
    {
        Render_FillRect(Target, &AntennaLight, COLOUR_ANTENNA_LIGHT);
    }
}

static void WindowWasher_DrawBuilding(Render_TargetTypeDef *Target, const WindowWasher_GameTypeDef *Game)
{
    const Render_RectTypeDef Building = { BUILDING_LEFT_X, 0, (uint16_t)(BUILDING_RIGHT_X - BUILDING_LEFT_X), RENDER_HEIGHT };
    const Render_RectTypeDef LeftCorner = { BUILDING_LEFT_X, 0, 26U, RENDER_HEIGHT };
    const Render_RectTypeDef RightCorner = { (int16_t)(BUILDING_RIGHT_X - 26), 0, 26U, RENDER_HEIGHT };
    const Render_RectTypeDef LeftCornerHighlight = { (int16_t)(BUILDING_LEFT_X + 21), 0, 3U, RENDER_HEIGHT };
    const Render_RectTypeDef RightCornerHighlight = { (int16_t)(BUILDING_RIGHT_X - 24), 0, 3U, RENDER_HEIGHT };
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
        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(WINDOW_GRID_LEFT_X + ((int16_t)Column * WINDOW_STEP_X));
            const Render_RectTypeDef Recess = { (int16_t)(WindowX - 3), (int16_t)(WindowY - 3), WINDOW_WIDTH + 6U, WINDOW_HEIGHT + 9U };
            const Render_RectTypeDef Frame = { WindowX, WindowY, WINDOW_WIDTH, WINDOW_HEIGHT };
            const Render_RectTypeDef Glass = { (int16_t)(WindowX + 5), (int16_t)(WindowY + 5), WINDOW_WIDTH - 10U, WINDOW_HEIGHT - 10U };
            const Render_RectTypeDef SillShadow = { (int16_t)(WindowX - 3), (int16_t)(WindowY + WINDOW_HEIGHT + 3U), WINDOW_WIDTH + 6U, 4U };
            const Render_RectTypeDef Sill = { (int16_t)(WindowX - 3), (int16_t)(WindowY + WINDOW_HEIGHT), WINDOW_WIDTH + 6U, 3U };
            const uint32_t ScenePattern = WindowWasher_Hash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0xD1B54A35U) ^ ((uint32_t)Column * 0x94D049BBU));
            const WindowWasher_CleanWindowTypeDef *CleanWindow = WindowWasher_FindCleanWindow(Game, WorldRow, Column);

            Render_FillRect(Target, &Recess, COLOUR_WINDOW_RECESS);
            Render_FillRect(Target, &Frame, COLOUR_WINDOW_FRAME);
            Render_FillRect(Target, &Glass, COLOUR_WINDOW);

            if((ScenePattern % 20U) == 0U)
            {
                WindowWasher_DrawWindowScene(Target, WindowX, WindowY, ScenePattern);
            }

            Render_FillRect(Target, &SillShadow, COLOUR_BUILDING_SHADOW);
            Render_FillRect(Target, &Sill, COLOUR_FACADE_TRIM);

            if((CleanWindow == NULL) && WindowWasher_WindowIsDirty(Game, WorldRow, Column))
            {
                const Render_RectTypeDef SpotOne = { (int16_t)(WindowX + 13), (int16_t)(WindowY + 11), 12U, 8U };
                const Render_RectTypeDef SpotTwo = { (int16_t)(WindowX + 38), (int16_t)(WindowY + 25), 15U, 10U };
                const Render_RectTypeDef SpotThree = { (int16_t)(WindowX + 27), (int16_t)(WindowY + 34), 8U, 5U };

                Render_FillRect(Target, &SpotOne, COLOUR_DIRT);
                Render_FillRect(Target, &SpotTwo, COLOUR_DIRT);
                Render_FillRect(Target, &SpotThree, COLOUR_DIRT);
            }
            else if((CleanWindow != NULL) && (CleanWindow->SparkleFrameCount > 0U))
            {
                const Render_RectTypeDef HorizontalSparkle = { (int16_t)(WindowX + 25), (int16_t)(WindowY + 22), 20U, 3U };
                const Render_RectTypeDef VerticalSparkle = { (int16_t)(WindowX + 33), (int16_t)(WindowY + 14), 4U, 19U };

                Render_FillRect(Target, &HorizontalSparkle, COLOUR_SPARKLE);
                Render_FillRect(Target, &VerticalSparkle, COLOUR_SPARKLE);
            }
        }
    }
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
        const uint32_t Pattern = WindowWasher_BalconyPattern(Game, WorldRow);
        const bool FromLeft = WindowWasher_BalconyFromLeft(Pattern);
        const bool HasCentreGap = WindowWasher_BalconyHasCentreGap(Pattern);
        const int16_t BalconyY = (int16_t)((ScreenRow * (int16_t)BALCONY_STEP_Y) + ScrollOffset + BALCONY_VERTICAL_OFFSET);
        const int16_t BalconyEndX = WindowWasher_BalconyEndX(Pattern, FromLeft);
        const int16_t BalconyX = FromLeft ? BUILDING_LEFT_X : BalconyEndX;
        const uint16_t BalconyWidth = (uint16_t)(FromLeft ? (BalconyEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - BalconyEndX));
        const int16_t GapLeftX = WindowWasher_BalconyGapLeftX(Pattern);
        const int16_t GapRightX = (int16_t)(GapLeftX + 160U + ((Pattern >> 8U) % 41U));

        if(WorldRow < BALCONY_CLEAR_START)
        {
            continue;
        }

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
    Render_DrawImage(Target, &WindowWasher_WasherImage, (int16_t)(CentreX - (WASHER_IMAGE_SIZE / 2U)), (int16_t)(BucketY - 43));
    Render_FillRect(Target, &BucketFront, COLOUR_BUCKET_METAL);
    Render_FillRect(Target, &BucketRim, COLOUR_BUCKET_HIGHLIGHT);
    Render_FillRect(Target, &SqueegeeHandle, COLOUR_TOOL_HANDLE);
    Render_FillRect(Target, &SqueegeeHead, COLOUR_TOOL_RUBBER);
    Render_FillRect(Target, &BrushHandle, COLOUR_TOOL_HANDLE);
    Render_FillRect(Target, &BrushHead, COLOUR_TOOL_BRISTLES);
    Render_FillRect(Target, &BrushBristlesOne, COLOUR_TOOL_BRISTLES);
    Render_FillRect(Target, &BrushBristlesTwo, COLOUR_TOOL_BRISTLES);

    if(Crashed)
    {
        const Render_RectTypeDef CrashMarker = { (int16_t)(CentreX - 8), (int16_t)(BucketY - 22), 16U, 16U };

        Render_FillRect(Target, &CrashMarker, COLOUR_CRASH);
    }
}

static void WindowWasher_DrawSlider(Render_TargetTypeDef *Target, int16_t X, int16_t Slider)
{
    const int16_t Top = 116;
    const uint16_t Height = 248U;
    const int16_t CentreY = (int16_t)(Top + (Height / 2U));
    const uint16_t FillHeight = (uint16_t)(((uint32_t)(Slider < 0 ? -Slider : Slider) * ((Height / 2U) - 3U)) / SLIDER_TRAVEL);
    const Render_RectTypeDef Frame = { X, Top, 12U, Height };
    const Render_RectTypeDef Interior = { (int16_t)(X + 2), (int16_t)(Top + 2), 8U, Height - 4U };
    const Render_RectTypeDef Centre = { (int16_t)(X + 2), CentreY, 8U, 1U };
    const Render_RectTypeDef Fill = { (int16_t)(X + 2), Slider >= 0 ? (int16_t)(CentreY - FillHeight) : CentreY, 8U, FillHeight };

    Render_FillRect(Target, &Frame, COLOUR_PLATFORM_EDGE);
    Render_FillRect(Target, &Interior, COLOUR_SKY);
    Render_FillRect(Target, &Centre, COLOUR_PLATFORM_EDGE);

    if(FillHeight > 0U)
    {
        Render_FillRect(Target, &Fill, Slider >= 0 ? COLOUR_SLIDER_UP : COLOUR_SLIDER_DOWN);
    }
}


bool WindowWasher_Init(void)
{
    static const Display_ColourTypeDef Palette[] =
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
        0x00F3A34AU
    };

    WindowWasher_Input.LeftSlider = 0;
    WindowWasher_Input.RightSlider = 0;
    WindowWasher_Game.ElapsedMilliseconds = 0U;
    WindowWasher_PendingDeltaTimeMilliseconds = 0U;
    WindowWasher_Paused = false;

    WindowWasher_ResetGame(&WindowWasher_Game, &WindowWasher_Figure);

    if(!Display_SetPalette(0U, Palette, (uint16_t)(sizeof(Palette) / sizeof(Palette[0]))))
    {
        WindowWasher_Initialized = false;
        return false;
    }

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

    Platform = WindowWasher_MakePlatform(&WindowWasher_Input);

    Target.Pixels = Frame->Pixels;
    Target.Width = Frame->Width;
    Target.Height = Frame->Height;
    Target.StridePixels = Frame->StridePixels;

    WindowWasher_UpdateGame(&WindowWasher_Game, &WindowWasher_Figure, &Platform, WindowWasher_PendingDeltaTimeMilliseconds);
    WindowWasher_PendingDeltaTimeMilliseconds = 0U;

    Render_ResetClipRect();
    Render_Clear(&Target, COLOUR_SKY);
    WindowWasher_DrawBackground(&Target, &WindowWasher_Game);
    WindowWasher_DrawBuilding(&Target, &WindowWasher_Game);
    WindowWasher_DrawBalconies(&Target, &WindowWasher_Game);
    WindowWasher_DrawPlatform(&Target, &Platform);
    WindowWasher_DrawWasher(&Target, &Platform, &WindowWasher_Figure, WindowWasher_Game.Crashed);
    WindowWasher_DrawSlider(&Target, 24, WindowWasher_Input.LeftSlider);
    WindowWasher_DrawSlider(&Target, (int16_t)(RENDER_WIDTH - 36U), WindowWasher_Input.RightSlider);

    Display_PresentFrame(Frame);
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