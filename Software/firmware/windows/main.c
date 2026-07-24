/**
 * @file main.c
 * @brief Window-washer platform game demonstration for the Windows simulator.
 */

#include "display.h"
#include "render.h"

#include <SDL3/SDL.h>

#include <stddef.h>

#define BUILDING_LEFT_X (70)
#define BUILDING_RIGHT_X ((int16_t)RENDER_WIDTH - 70)
#define WINDOW_COLUMN_COUNT (7U)
#define WINDOW_WIDTH (70U)
#define WINDOW_HEIGHT (48U)
#define WINDOW_STEP_X (88U)
#define WINDOW_STEP_Y (68U)
#define PLATFORM_LEFT_X (170)
#define PLATFORM_RIGHT_X ((int16_t)RENDER_WIDTH - 170)
#define PLATFORM_BASE_Y (320)
#define PLATFORM_THICKNESS (14U)
#define FIGURE_WIDTH (32U)
#define FIGURE_HEIGHT (64U)
#define FIGURE_FIXED_SCALE (256)
#define FIGURE_SLIDE_GRAVITY (128)
#define WASHER_IMAGE_SIZE (64U)
#define BALCONY_STEP_Y (270U)
#define BALCONY_THICKNESS (16U)
#define BALCONY_CLEAR_START (2)
#define BUILDING_SPEED_SCALE (256U)
#define BUILDING_START_SPEED (384U)
#define BUILDING_FULL_SPEED (768U)
#define BUILDING_RAMP_MILLISECONDS (60000ULL)
#define CLEAN_WINDOW_MAX_COUNT (8U)
#define CLEAN_WINDOW_SPARKLE_FRAMES (45U)
#define SLIDER_TRAVEL (150)
#define MOUSE_SLIDER_PER_PIXEL (1)
#define CRASH_FRAME_COUNT (55U)

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
    COLOUR_SPARKLE = 19U
};

typedef struct
{
    SDL_MouseID LeftMouse;
    SDL_MouseID RightMouse;
    int16_t LeftSlider;
    int16_t RightSlider;
} Windows_MainInputTypeDef;

typedef struct
{
    int16_t LeftY;
    int16_t RightY;
} Windows_MainPlatformTypeDef;

typedef struct
{
    int32_t PositionX;
    int32_t VelocityX;
} Windows_MainFigureTypeDef;

typedef struct
{
    bool Active;
    int32_t WorldRow;
    uint8_t Column;
    uint8_t SparkleFrameCount;
} Windows_MainCleanWindowTypeDef;

typedef struct
{
    int32_t BuildingScroll;
    uint16_t BuildingSubpixel;
    uint32_t LayoutSeed;
    uint64_t StartedAtMilliseconds;
    bool Crashed;
    uint16_t CrashFrameCount;
    Windows_MainCleanWindowTypeDef CleanWindows[CLEAN_WINDOW_MAX_COUNT];
} Windows_MainGameTypeDef;

static Render_ColourIndexTypeDef Windows_MainWasherPixels[WASHER_IMAGE_SIZE * WASHER_IMAGE_SIZE];

static const Render_ImageTypeDef Windows_MainWasherImage =
{
    .Pixels = Windows_MainWasherPixels,
    .Width = WASHER_IMAGE_SIZE,
    .Height = WASHER_IMAGE_SIZE,
    .StridePixels = WASHER_IMAGE_SIZE,
    .HasTransparentColour = true,
    .TransparentColour = COLOUR_SKY
};

static int16_t Windows_MainClamp(int16_t Value, int16_t Minimum, int16_t Maximum)
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

static uint32_t Windows_MainHash(uint32_t Value)
{
    Value ^= Value >> 16U;
    Value *= 0x7FEB352DU;
    Value ^= Value >> 15U;
    Value *= 0x846CA68BU;
    Value ^= Value >> 16U;

    return Value;
}

static uint32_t Windows_MainBalconyPattern(const Windows_MainGameTypeDef *Game, int32_t WorldRow)
{
    return Windows_MainHash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0x9E3779B9U));
}

static bool Windows_MainBalconyFromLeft(uint32_t Pattern)
{
    return (Pattern & 1U) == 0U;
}

static int16_t Windows_MainBalconyEndX(uint32_t Pattern, bool FromLeft)
{
    const int16_t BalconyLength = (int16_t)(220U + (Pattern % 151U));

    return FromLeft ? (int16_t)(BUILDING_LEFT_X + BalconyLength) : (int16_t)(BUILDING_RIGHT_X - BalconyLength);
}

static bool Windows_MainBalconyHasCentreGap(uint32_t Pattern)
{
    return ((Pattern >> 4U) & 0x03U) == 0U;
}

static int16_t Windows_MainBalconyGapLeftX(uint32_t Pattern)
{
    const int16_t GapWidth = (int16_t)(160U + ((Pattern >> 8U) % 41U));
    const int16_t GapCentreX = (int16_t)(360U + ((Pattern >> 16U) % 81U));

    return (int16_t)(GapCentreX - (GapWidth / 2));
}

static bool Windows_MainWindowIsReachable(const Windows_MainGameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    const int16_t WindowX = (int16_t)(BUILDING_LEFT_X + 9 + ((int16_t)Column * WINDOW_STEP_X));
    const int32_t WindowWorldY = (WorldRow * (int32_t)WINDOW_STEP_Y) + 8;
    const int32_t NearestBalconyRow = WindowWorldY / BALCONY_STEP_Y;

    if((Column == 0U) || (Column >= (WINDOW_COLUMN_COUNT - 1U)))
    {
        return false;
    }

    for(int32_t BalconyRow = NearestBalconyRow - 1; BalconyRow <= (NearestBalconyRow + 1); BalconyRow++)
    {
        const int32_t BalconyRelativeY = WindowWorldY - (BalconyRow * (int32_t)BALCONY_STEP_Y);
        const uint32_t Pattern = Windows_MainBalconyPattern(Game, BalconyRow);
        const bool FromLeft = Windows_MainBalconyFromLeft(Pattern);
        const bool HasCentreGap = Windows_MainBalconyHasCentreGap(Pattern);
        const int16_t SingleEndX = Windows_MainBalconyEndX(Pattern, FromLeft);
        const int16_t SingleX = FromLeft ? BUILDING_LEFT_X : SingleEndX;
        const uint16_t SingleWidth = (uint16_t)(FromLeft ? (SingleEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - SingleEndX));
        const int16_t GapLeftX = Windows_MainBalconyGapLeftX(Pattern);
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

static bool Windows_MainWindowIsDirty(const Windows_MainGameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    const uint32_t Pattern = Windows_MainHash(Game->LayoutSeed ^ ((uint32_t)WorldRow * 0x85EBCA6BU) ^ ((uint32_t)Column * 0xC2B2AE35U));

    return (Pattern % 100U) == 0U && Windows_MainWindowIsReachable(Game, WorldRow, Column);
}

static const Windows_MainCleanWindowTypeDef *Windows_MainFindCleanWindow(const Windows_MainGameTypeDef *Game, int32_t WorldRow, uint8_t Column)
{
    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_MAX_COUNT; WindowIndex++)
    {
        const Windows_MainCleanWindowTypeDef *Window = &Game->CleanWindows[WindowIndex];

        if(Window->Active && (Window->WorldRow == WorldRow) && (Window->Column == Column))
        {
            return Window;
        }
    }

    return NULL;
}

static void Windows_MainPaintWasherRectangle(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    for(uint16_t Row = 0U; Row < Height; Row++)
    {
        for(uint16_t Column = 0U; Column < Width; Column++)
        {
            Windows_MainWasherPixels[((Y + Row) * WASHER_IMAGE_SIZE) + X + Column] = Colour;
        }
    }
}

static void Windows_MainBuildWasherImage(void)
{
    Windows_MainPaintWasherRectangle(17U, 3U, 30U, 5U, COLOUR_WASHER_HELMET);
    Windows_MainPaintWasherRectangle(13U, 8U, 38U, 7U, COLOUR_WASHER_HELMET);
    Windows_MainPaintWasherRectangle(19U, 15U, 26U, 4U, COLOUR_WASHER_HELMET);
    Windows_MainPaintWasherRectangle(22U, 19U, 20U, 13U, COLOUR_WASHER_FACE);
    Windows_MainPaintWasherRectangle(22U, 23U, 4U, 3U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(38U, 23U, 4U, 3U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(29U, 29U, 6U, 2U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(18U, 32U, 28U, 19U, COLOUR_WASHER_UNIFORM_LIGHT);
    Windows_MainPaintWasherRectangle(14U, 34U, 6U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(44U, 34U, 6U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(22U, 34U, 5U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(37U, 34U, 5U, 17U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(20U, 47U, 24U, 4U, COLOUR_WASHER_HELMET);
    Windows_MainPaintWasherRectangle(19U, 51U, 10U, 9U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(35U, 51U, 10U, 9U, COLOUR_WASHER_UNIFORM_DARK);
    Windows_MainPaintWasherRectangle(16U, 59U, 15U, 5U, COLOUR_PLATFORM_EDGE);
    Windows_MainPaintWasherRectangle(33U, 59U, 15U, 5U, COLOUR_PLATFORM_EDGE);
}

static void Windows_MainProcessMouseMotion(Windows_MainInputTypeDef *Input, const SDL_MouseMotionEvent *Motion)
{
    const int16_t SliderDelta = (int16_t)(-Motion->yrel * MOUSE_SLIDER_PER_PIXEL);

    if(Motion->which == 0U)
    {
        return;
    }

    if((Input->LeftMouse == 0U) || (Input->LeftMouse == Motion->which))
    {
        Input->LeftMouse = Motion->which;
        Input->LeftSlider = Windows_MainClamp((int16_t)(Input->LeftSlider + SliderDelta), -SLIDER_TRAVEL, SLIDER_TRAVEL);
    }
    else if((Input->RightMouse == 0U) || (Input->RightMouse == Motion->which))
    {
        Input->RightMouse = Motion->which;
        Input->RightSlider = Windows_MainClamp((int16_t)(Input->RightSlider + SliderDelta), -SLIDER_TRAVEL, SLIDER_TRAVEL);
    }
}

static void Windows_MainProcessMouseRemoval(Windows_MainInputTypeDef *Input, SDL_MouseID Mouse)
{
    if(Mouse == Input->LeftMouse)
    {
        Input->LeftMouse = 0U;
        Input->LeftSlider = 0;
    }

    if(Mouse == Input->RightMouse)
    {
        Input->RightMouse = 0U;
        Input->RightSlider = 0;
    }
}

static Windows_MainPlatformTypeDef Windows_MainMakePlatform(const Windows_MainInputTypeDef *Input)
{
    Windows_MainPlatformTypeDef Platform;

    Platform.LeftY = (int16_t)(PLATFORM_BASE_Y - (Input->LeftSlider / 2));
    Platform.RightY = (int16_t)(PLATFORM_BASE_Y - (Input->RightSlider / 2));

    return Platform;
}

static int16_t Windows_MainPlatformYAtX(const Windows_MainPlatformTypeDef *Platform, int16_t X)
{
    const int32_t RelativeX = Windows_MainClamp(X, PLATFORM_LEFT_X, PLATFORM_RIGHT_X) - PLATFORM_LEFT_X;
    const int32_t HeightDifference = Platform->RightY - Platform->LeftY;

    return (int16_t)(Platform->LeftY + ((RelativeX * HeightDifference) / (PLATFORM_RIGHT_X - PLATFORM_LEFT_X)));
}

static int16_t Windows_MainFigureY(const Windows_MainPlatformTypeDef *Platform, const Windows_MainFigureTypeDef *Figure)
{
    return (int16_t)(Windows_MainPlatformYAtX(Platform, (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE)) - FIGURE_HEIGHT);
}

static void Windows_MainResetGame(Windows_MainGameTypeDef *Game, Windows_MainFigureTypeDef *Figure)
{
    static uint32_t ResetNonce = 0U;
    const uint64_t Now = SDL_GetTicks();

    Game->BuildingScroll = 0;
    Game->BuildingSubpixel = 0U;
    Game->LayoutSeed = Windows_MainHash((uint32_t)Now ^ (uint32_t)(Now >> 32U) ^ (++ResetNonce * 0xA511E9B3U));
    Game->StartedAtMilliseconds = Now;
    Game->Crashed = false;
    Game->CrashFrameCount = 0U;

    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_MAX_COUNT; WindowIndex++)
    {
        Game->CleanWindows[WindowIndex].Active = false;
    }

    Figure->PositionX = ((int32_t)RENDER_WIDTH / 2) * FIGURE_FIXED_SCALE;
    Figure->VelocityX = 0;
}

static bool Windows_MainFigureHitsBalcony(const Windows_MainGameTypeDef *Game, const Windows_MainPlatformTypeDef *Platform, const Windows_MainFigureTypeDef *Figure)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / BALCONY_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % BALCONY_STEP_Y);
    const int16_t FigureX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t FigureY = Windows_MainFigureY(Platform, Figure);

    for(int8_t ScreenRow = -1; ScreenRow < 4; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const uint32_t Pattern = Windows_MainBalconyPattern(Game, WorldRow);
        const bool FromLeft = Windows_MainBalconyFromLeft(Pattern);
        const bool HasCentreGap = Windows_MainBalconyHasCentreGap(Pattern);
        const int16_t BalconyY = (int16_t)((ScreenRow * (int16_t)BALCONY_STEP_Y) + ScrollOffset);
        const int16_t BalconyEndX = Windows_MainBalconyEndX(Pattern, FromLeft);
        const int16_t BalconyX = FromLeft ? BUILDING_LEFT_X : BalconyEndX;
        const uint16_t BalconyWidth = (uint16_t)(FromLeft ? (BalconyEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - BalconyEndX));
        const int16_t GapLeftX = Windows_MainBalconyGapLeftX(Pattern);
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

static void Windows_MainUpdateWindowCleaning(Windows_MainGameTypeDef *Game, const Windows_MainPlatformTypeDef *Platform, const Windows_MainFigureTypeDef *Figure)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / WINDOW_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % WINDOW_STEP_Y);
    const int16_t FigureX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t FigureY = Windows_MainFigureY(Platform, Figure);

    for(uint8_t WindowIndex = 0U; WindowIndex < CLEAN_WINDOW_MAX_COUNT; WindowIndex++)
    {
        if(Game->CleanWindows[WindowIndex].Active && (Game->CleanWindows[WindowIndex].SparkleFrameCount > 0U))
        {
            Game->CleanWindows[WindowIndex].SparkleFrameCount--;
        }
    }

    for(int8_t ScreenRow = -1; ScreenRow < 9; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t WindowY = (int16_t)((ScreenRow * (int16_t)WINDOW_STEP_Y) + ScrollOffset + 8);

        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(BUILDING_LEFT_X + 9 + ((int16_t)Column * WINDOW_STEP_X));

            if(Windows_MainWindowIsDirty(Game, WorldRow, Column) && (Windows_MainFindCleanWindow(Game, WorldRow, Column) == NULL) && ((FigureX + ((int16_t)FIGURE_WIDTH / 2)) > WindowX) && ((FigureX - ((int16_t)FIGURE_WIDTH / 2)) < (WindowX + (int16_t)WINDOW_WIDTH)) && ((FigureY + (int16_t)FIGURE_HEIGHT) > WindowY) && (FigureY < (WindowY + (int16_t)WINDOW_HEIGHT)))
            {
                for(uint8_t CleanIndex = 0U; CleanIndex < CLEAN_WINDOW_MAX_COUNT; CleanIndex++)
                {
                    Windows_MainCleanWindowTypeDef *CleanWindow = &Game->CleanWindows[CleanIndex];

                    if(!CleanWindow->Active)
                    {
                        CleanWindow->Active = true;
                        CleanWindow->WorldRow = WorldRow;
                        CleanWindow->Column = Column;
                        CleanWindow->SparkleFrameCount = CLEAN_WINDOW_SPARKLE_FRAMES;
                        break;
                    }
                }
            }
        }
    }
}

static void Windows_MainUpdateGame(Windows_MainGameTypeDef *Game, Windows_MainFigureTypeDef *Figure, const Windows_MainPlatformTypeDef *Platform)
{
    const uint64_t Now = SDL_GetTicks();
    const uint64_t ElapsedMilliseconds = (Now - Game->StartedAtMilliseconds) < BUILDING_RAMP_MILLISECONDS ? (Now - Game->StartedAtMilliseconds) : BUILDING_RAMP_MILLISECONDS;
    const uint16_t BuildingSpeed = (uint16_t)(BUILDING_START_SPEED + ((ElapsedMilliseconds * (BUILDING_FULL_SPEED - BUILDING_START_SPEED)) / BUILDING_RAMP_MILLISECONDS));

    if(Game->Crashed)
    {
        Game->CrashFrameCount++;

        if(Game->CrashFrameCount >= CRASH_FRAME_COUNT)
        {
            Windows_MainResetGame(Game, Figure);
        }

        return;
    }

    Figure->VelocityX += ((int32_t)(Platform->RightY - Platform->LeftY) * FIGURE_SLIDE_GRAVITY) / (PLATFORM_RIGHT_X - PLATFORM_LEFT_X);
    Figure->VelocityX = (Figure->VelocityX * 250) / 256;
    Figure->PositionX += Figure->VelocityX;
    Game->BuildingSubpixel = (uint16_t)(Game->BuildingSubpixel + BuildingSpeed);
    Game->BuildingScroll += (int32_t)(Game->BuildingSubpixel / BUILDING_SPEED_SCALE);
    Game->BuildingSubpixel = (uint16_t)(Game->BuildingSubpixel % BUILDING_SPEED_SCALE);
    Windows_MainUpdateWindowCleaning(Game, Platform, Figure);

    if(((Figure->PositionX / FIGURE_FIXED_SCALE) < (PLATFORM_LEFT_X + ((int16_t)FIGURE_WIDTH / 2))) || ((Figure->PositionX / FIGURE_FIXED_SCALE) > (PLATFORM_RIGHT_X - ((int16_t)FIGURE_WIDTH / 2))) || Windows_MainFigureHitsBalcony(Game, Platform, Figure))
    {
        Game->Crashed = true;
        Game->CrashFrameCount = 0U;
    }
}

static void Windows_MainDrawBuilding(Render_TargetTypeDef *Target, const Windows_MainGameTypeDef *Game)
{
    const Render_RectTypeDef Building = { BUILDING_LEFT_X, 0, (uint16_t)(BUILDING_RIGHT_X - BUILDING_LEFT_X), RENDER_HEIGHT };
    const int32_t FirstWorldRow = Game->BuildingScroll / WINDOW_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % WINDOW_STEP_Y);

    Render_FillRect(Target, &Building, COLOUR_BUILDING);

    for(int8_t ScreenRow = -1; ScreenRow < 9; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const int16_t WindowY = (int16_t)((ScreenRow * (int16_t)WINDOW_STEP_Y) + ScrollOffset + 8);

        for(uint8_t Column = 0U; Column < WINDOW_COLUMN_COUNT; Column++)
        {
            const int16_t WindowX = (int16_t)(BUILDING_LEFT_X + 9 + ((int16_t)Column * WINDOW_STEP_X));
            const Render_RectTypeDef Frame = { WindowX, WindowY, WINDOW_WIDTH, WINDOW_HEIGHT };
            const Render_RectTypeDef Glass = { (int16_t)(WindowX + 4), (int16_t)(WindowY + 4), WINDOW_WIDTH - 8U, WINDOW_HEIGHT - 8U };
            const Windows_MainCleanWindowTypeDef *CleanWindow = Windows_MainFindCleanWindow(Game, WorldRow, Column);

            Render_FillRect(Target, &Frame, COLOUR_WINDOW_FRAME);
            Render_FillRect(Target, &Glass, COLOUR_WINDOW);

            if((CleanWindow == NULL) && Windows_MainWindowIsDirty(Game, WorldRow, Column))
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

static void Windows_MainDrawBalconySection(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, int16_t InnerEndX)
{
    const Render_RectTypeDef Deck = { X, Y, Width, BALCONY_THICKNESS };
    const Render_RectTypeDef Rail = { X, (int16_t)(Y - 19), Width, 4U };
    const Render_RectTypeDef InnerPost = { (int16_t)(InnerEndX - 2), (int16_t)(Y - 20), 4U, 36U };

    Render_FillRect(Target, &Deck, COLOUR_BALCONY);
    Render_FillRect(Target, &Rail, COLOUR_BALCONY_HIGHLIGHT);
    Render_FillRect(Target, &InnerPost, COLOUR_BALCONY);

    for(int16_t PostX = (int16_t)(X + 18); PostX < (int16_t)(X + Width - 8U); PostX = (int16_t)(PostX + 28))
    {
        const Render_RectTypeDef Post = { PostX, (int16_t)(Y - 18), 3U, 18U };

        Render_FillRect(Target, &Post, COLOUR_BALCONY);
    }
}

static void Windows_MainDrawBalconies(Render_TargetTypeDef *Target, const Windows_MainGameTypeDef *Game)
{
    const int32_t FirstWorldRow = Game->BuildingScroll / BALCONY_STEP_Y;
    const int16_t ScrollOffset = (int16_t)(Game->BuildingScroll % BALCONY_STEP_Y);

    for(int8_t ScreenRow = -1; ScreenRow < 4; ScreenRow++)
    {
        const int32_t WorldRow = FirstWorldRow - ScreenRow;
        const uint32_t Pattern = Windows_MainBalconyPattern(Game, WorldRow);
        const bool FromLeft = Windows_MainBalconyFromLeft(Pattern);
        const bool HasCentreGap = Windows_MainBalconyHasCentreGap(Pattern);
        const int16_t BalconyY = (int16_t)((ScreenRow * (int16_t)BALCONY_STEP_Y) + ScrollOffset);
        const int16_t BalconyEndX = Windows_MainBalconyEndX(Pattern, FromLeft);
        const int16_t BalconyX = FromLeft ? BUILDING_LEFT_X : BalconyEndX;
        const uint16_t BalconyWidth = (uint16_t)(FromLeft ? (BalconyEndX - BUILDING_LEFT_X) : (BUILDING_RIGHT_X - BalconyEndX));
        const int16_t GapLeftX = Windows_MainBalconyGapLeftX(Pattern);
        const int16_t GapRightX = (int16_t)(GapLeftX + 160U + ((Pattern >> 8U) % 41U));

        if(WorldRow < BALCONY_CLEAR_START)
        {
            continue;
        }

        if(HasCentreGap)
        {
            Windows_MainDrawBalconySection(Target, BUILDING_LEFT_X, BalconyY, (uint16_t)(GapLeftX - BUILDING_LEFT_X), GapLeftX);
            Windows_MainDrawBalconySection(Target, GapRightX, BalconyY, (uint16_t)(BUILDING_RIGHT_X - GapRightX), GapRightX);
        }
        else
        {
            Windows_MainDrawBalconySection(Target, BalconyX, BalconyY, BalconyWidth, BalconyEndX);
        }
    }
}

static void Windows_MainDrawPlatform(Render_TargetTypeDef *Target, const Windows_MainPlatformTypeDef *Platform)
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

    Render_FillRect(Target, &LeftCable, COLOUR_CABLE);
    Render_FillRect(Target, &RightCable, COLOUR_CABLE);
    Render_DrawPolygon(Target, Edge, 4U, COLOUR_PLATFORM_EDGE);
    Render_DrawPolygon(Target, Surface, 4U, COLOUR_PLATFORM);
}

static void Windows_MainDrawWasher(Render_TargetTypeDef *Target, const Windows_MainPlatformTypeDef *Platform, const Windows_MainFigureTypeDef *Figure, bool Crashed)
{
    const int16_t CentreX = (int16_t)(Figure->PositionX / FIGURE_FIXED_SCALE);
    const int16_t FeetY = Windows_MainPlatformYAtX(Platform, CentreX);

    Render_DrawImage(Target, &Windows_MainWasherImage, (int16_t)(CentreX - (WASHER_IMAGE_SIZE / 2U)), (int16_t)(FeetY - WASHER_IMAGE_SIZE));

    if(Crashed)
    {
        const Render_RectTypeDef CrashMarker = { (int16_t)(CentreX - 8), (int16_t)(FeetY - 36), 16U, 16U };

        Render_FillRect(Target, &CrashMarker, COLOUR_CRASH);
    }
}

static void Windows_MainDrawSlider(Render_TargetTypeDef *Target, int16_t X, int16_t Slider)
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

int main(void)
{
    const Display_ColourTypeDef Palette[] =
    {
        0x0077C9F2U,
        0x00D8D1C3U,
        0x0068767CU,
        0x009FCBE0U,
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
        0x00FFF4A3U
    };
    Windows_MainInputTypeDef Input = { 0U, 0U, 0, 0 };
    Windows_MainGameTypeDef Game;
    Windows_MainFigureTypeDef Figure;
    bool Running = true;
    bool RelativeMouseModeEnabled = false;

    Windows_MainResetGame(&Game, &Figure);

    if(!Display_Init() || !Display_SetPalette(0U, Palette, (uint16_t)(sizeof(Palette) / sizeof(Palette[0]))))
    {
        return 1;
    }

    Windows_MainBuildWasherImage();

    while(Running)
    {
        SDL_Event Event;

        if(!RelativeMouseModeEnabled)
        {
            SDL_Window *InputWindow = SDL_GetMouseFocus();

            if(InputWindow != NULL)
            {
                RelativeMouseModeEnabled = SDL_SetWindowRelativeMouseMode(InputWindow, true);
            }
        }

        while(SDL_PollEvent(&Event))
        {
            if(Event.type == SDL_EVENT_QUIT)
            {
                Running = false;
            }
            else if(Event.type == SDL_EVENT_MOUSE_MOTION)
            {
                Windows_MainProcessMouseMotion(&Input, &Event.motion);
            }
            else if(Event.type == SDL_EVENT_MOUSE_REMOVED)
            {
                Windows_MainProcessMouseRemoval(&Input, Event.mdevice.which);
            }
        }

        if(SDL_GetKeyboardState(NULL)[SDL_SCANCODE_R])
        {
            Input.LeftSlider = 0;
            Input.RightSlider = 0;
            Windows_MainResetGame(&Game, &Figure);
        }

        Display_FrameTypeDef *Frame = Display_AcquireFrame();

        if(Frame != NULL)
        {
            Render_TargetTypeDef Target;
            const Windows_MainPlatformTypeDef Platform = Windows_MainMakePlatform(&Input);

            Target.Pixels = Frame->Pixels;
            Target.Width = Frame->Width;
            Target.Height = Frame->Height;
            Target.StridePixels = Frame->StridePixels;

            Windows_MainUpdateGame(&Game, &Figure, &Platform);
            Render_ResetClipRect();
            Render_Clear(&Target, COLOUR_SKY);
            Windows_MainDrawBuilding(&Target, &Game);
            Windows_MainDrawBalconies(&Target, &Game);
            Windows_MainDrawPlatform(&Target, &Platform);
            Windows_MainDrawWasher(&Target, &Platform, &Figure, Game.Crashed);
            Windows_MainDrawSlider(&Target, 24, Input.LeftSlider);
            Windows_MainDrawSlider(&Target, (int16_t)(RENDER_WIDTH - 36U), Input.RightSlider);
            Display_PresentFrame(Frame);
        }

        SDL_Delay(16U);
    }

    return 0;
}