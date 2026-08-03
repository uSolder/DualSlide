/**
 * @file pong.c
 * @brief Calm, modern Pong for one or two DualSlide players.
 *
 * In solo mode the left blue paddle is bot-controlled and the right red
 * paddle is player-controlled. In two-player mode each slider controls its
 * matching side.
 * The menu uses right-slider hard stops to select a setting, secondary to
 * change that setting, and primary to start or restart a match.
 */

#include "pong.h"

#include "app_manager.h"
#include "display.h"
#include "input.h"
#include "open_sans.h"

#include <stddef.h>

#define INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)
#define PADDLE_WIDTH (8U)
#define PADDLE_NORMAL_HEIGHT (84U)
#define PADDLE_EXPANDED_HEIGHT (124U)
#define PADDLE_SHRUNK_HEIGHT (44U)
#define PADDLE_MARGIN (46)
#define BALL_SIZE (12U)
#define BALL_FIXED_SCALE (256)
#define BALL_START_SPEED (350 * BALL_FIXED_SCALE)
#define BALL_MAX_SPEED (760 * BALL_FIXED_SCALE)
#define WIN_SCORE (7U)
#define POWER_UP_SIZE (24U)
#define POWER_UP_DELAY_MILLISECONDS (6000U)
#define POWER_UP_LIFETIME_MILLISECONDS (7000U)
#define POWER_UP_DURATION_MILLISECONDS (6000U)
#define POWER_UP_LONG_DURATION_MILLISECONDS (4500U)
#define BALL_SPEED_UP_INTERVAL_MILLISECONDS (5000U)
#define PADDLE_HIT_FLASH_MILLISECONDS (100U)
#define POWER_UP_GROW_MILLISECONDS (280U)
#define BALL_TRAIL_COUNT (4U)
#define PONG_SPLASH_X (60)
#define PONG_SPLASH_Y (60)
#define PONG_SPLASH_WIDTH (680U)
#define PONG_SPLASH_HEIGHT (360U)
#define PONG_SLIDER_TOP_TRIGGER (1000)
#define PONG_SLIDER_BOTTOM_TRIGGER (65535 - 1000)
#define PONG_SLIDER_TOP_RELEASE (1500)
#define PONG_SLIDER_BOTTOM_RELEASE (65535 - 1500)

enum
{
    COLOUR_BACKGROUND = 0U,
    COLOUR_COURT = 1U,
    COLOUR_LINE = 2U,
    COLOUR_LEFT_PADDLE = 3U,
    COLOUR_RIGHT_PADDLE = 4U,
    COLOUR_BALL = 5U,
    COLOUR_TEXT = 6U,
    COLOUR_MUTED_TEXT = 7U,
    COLOUR_PANEL = 8U,
    COLOUR_PANEL_EDGE = 9U,
    COLOUR_EXPAND = 10U,
    COLOUR_SLOW = 11U,
    COLOUR_SHIELD = 12U,
    COLOUR_SHIELD_GLOW = 13U,
    COLOUR_SHADOW = 14U,
    COLOUR_HASTE = 15U,
    COLOUR_INVERT = 16U,
    COLOUR_FREEZE = 17U,
    COLOUR_COURT_ACCENT = 18U,
    COLOUR_PADDLE_FLASH = 19U,
    COLOUR_COURT_INSET = 20U,
    COLOUR_TRAIL_FAR = 21U,
    COLOUR_TRAIL_MID = 22U,
    COLOUR_TRAIL_NEAR = 23U
};

typedef enum { PONG_SCREEN_MENU, PONG_SCREEN_SERVE, PONG_SCREEN_PLAYING, PONG_SCREEN_GAME_OVER } Pong_ScreenTypeDef;
typedef enum { PONG_POWER_UP_NONE, PONG_POWER_UP_EXPAND, PONG_POWER_UP_SLOW, PONG_POWER_UP_SHIELD, PONG_POWER_UP_SHRINK, PONG_POWER_UP_HASTE, PONG_POWER_UP_INVERT, PONG_POWER_UP_FREEZE } Pong_PowerUpTypeDef;
typedef enum { PONG_BOT_EASY, PONG_BOT_MEDIUM, PONG_BOT_HARD, PONG_BOT_IMPOSSIBLE } Pong_BotDifficultyTypeDef;
typedef enum { PONG_MENU_PLAYERS, PONG_MENU_DIFFICULTY, PONG_MENU_POWER_UPS, PONG_MENU_ITEM_COUNT } Pong_MenuItemTypeDef;
typedef struct { int16_t LeftY; int16_t RightY; bool PrimaryDown; bool SecondaryDown; bool RightSliderArmed; } Pong_InputTypeDef;
typedef struct { int32_t X; int32_t Y; int32_t VelocityX; int32_t VelocityY; } Pong_BallTypeDef;
typedef struct { Pong_PowerUpTypeDef Type; int16_t X; int16_t Y; uint32_t LifetimeMilliseconds; uint32_t SpawnTimerMilliseconds; uint32_t GrowElapsedMilliseconds; } Pong_PowerUpStateTypeDef;
typedef struct
{
    Pong_ScreenTypeDef Screen;
    bool TwoPlayer;
    uint8_t LeftScore;
    uint8_t RightScore;
    uint8_t LastWinner;
    bool LastHitLeft;
    bool PowerUpsEnabled;
    Pong_BotDifficultyTypeDef BotDifficulty;
    Pong_MenuItemTypeDef MenuItem;
    uint32_t BotMistakeMilliseconds;
    int32_t BotIntegral;
    int16_t BotPreviousError;
    uint32_t RandomState;
    uint32_t RallyElapsedMilliseconds;
    uint32_t BallSpeedUpElapsedMilliseconds;
    uint32_t ServeDelayMilliseconds;
    uint32_t LeftExpandMilliseconds;
    uint32_t RightExpandMilliseconds;
    uint32_t LeftShrinkMilliseconds;
    uint32_t RightShrinkMilliseconds;
    uint32_t LeftInvertMilliseconds;
    uint32_t RightInvertMilliseconds;
    uint32_t LeftFreezeMilliseconds;
    uint32_t RightFreezeMilliseconds;
    uint32_t LeftPaddleFlashMilliseconds;
    uint32_t RightPaddleFlashMilliseconds;
    uint32_t LeftShieldMilliseconds;
    uint32_t RightShieldMilliseconds;
    Pong_BallTypeDef Ball;
    Pong_PowerUpStateTypeDef PowerUp;
} Pong_GameTypeDef;

static const Display_ColourTypeDef Pong_Palette[] =
{
    0x00000000U, 0x00000000U, 0x001C2833U, 0x0058B9FFU, 0x00FF5D7AU,
    0x00FFE66DU, 0x00F7FFF7U, 0x00B7C9D9U, 0x002B4A6FU, 0x0091C8E4U,
    0x00FF9F1CU, 0x007BDFF2U, 0x00A8E6CFU, 0x00D9FFF5U, 0x000B132BU,
    0x00FF6B35U, 0x00C77DFFU, 0x006EE7D8U, 0x006C63FFU, 0x00FFF3B0U,
    0x001B3150U, 0x00373216U, 0x006D5C2AU, 0x00B59A48U
};

static Pong_InputTypeDef Pong_Input;
static Pong_GameTypeDef Pong_Game;
static uint32_t Pong_PendingDeltaTimeMilliseconds;
static uint32_t Pong_SplashElapsedMilliseconds;
static bool Pong_Initialized;
static bool Pong_Paused;
static int32_t Pong_BallTrailX[BALL_TRAIL_COUNT];
static int32_t Pong_BallTrailY[BALL_TRAIL_COUNT];
static int32_t Pong_SplashBallX;
static int32_t Pong_SplashBallY;
static int32_t Pong_SplashBallVelocityX;
static int32_t Pong_SplashBallVelocityY;
static int16_t Pong_SplashLeftY;
static int16_t Pong_SplashRightY;
static int32_t Pong_SplashLeftBotIntegral;
static int32_t Pong_SplashRightBotIntegral;
static int16_t Pong_SplashLeftBotPreviousError;
static int16_t Pong_SplashRightBotPreviousError;
static bool Pong_SplashInitialized;

static int16_t Pong_Clamp(int16_t Value, int16_t Minimum, int16_t Maximum)
{
    if(Value < Minimum) return Minimum;
    if(Value > Maximum) return Maximum;
    return Value;
}

static int16_t Pong_SliderToY(int32_t Value, int16_t PaddleHeight)
{
    const int32_t MaximumY = (int32_t)RENDER_HEIGHT - PaddleHeight - 42;
    if(Value < 0) Value = 0;
    if(Value > 65535) Value = 65535;
    return (int16_t)(MaximumY - ((Value * (MaximumY - 42)) / 65535));
}

static int16_t Pong_PaddleHeight(bool Left)
{
    if((Left ? Pong_Game.LeftShrinkMilliseconds : Pong_Game.RightShrinkMilliseconds) > 0U) return PADDLE_SHRUNK_HEIGHT;
    return ((Left ? Pong_Game.LeftExpandMilliseconds : Pong_Game.RightExpandMilliseconds) > 0U) ? PADDLE_EXPANDED_HEIGHT : PADDLE_NORMAL_HEIGHT;
}

static void Pong_ResetBall(bool ServeRight)
{
    Pong_Game.Ball.X = ((int32_t)RENDER_WIDTH / 2) * BALL_FIXED_SCALE;
    Pong_Game.Ball.Y = ((int32_t)RENDER_HEIGHT / 2) * BALL_FIXED_SCALE;
    Pong_Game.Ball.VelocityX = ServeRight ? BALL_START_SPEED : -BALL_START_SPEED;
    Pong_Game.Ball.VelocityY = ServeRight ? (110 * BALL_FIXED_SCALE) : (-110 * BALL_FIXED_SCALE);
    for(uint8_t TrailIndex = 0U; TrailIndex < BALL_TRAIL_COUNT; TrailIndex++)
    {
        Pong_BallTrailX[TrailIndex] = Pong_Game.Ball.X;
        Pong_BallTrailY[TrailIndex] = Pong_Game.Ball.Y;
    }
}

static void Pong_RecordBallTrail(void)
{
    for(uint8_t TrailIndex = BALL_TRAIL_COUNT - 1U; TrailIndex > 0U; TrailIndex--)
    {
        Pong_BallTrailX[TrailIndex] = Pong_BallTrailX[TrailIndex - 1U];
        Pong_BallTrailY[TrailIndex] = Pong_BallTrailY[TrailIndex - 1U];
    }
    Pong_BallTrailX[0] = Pong_Game.Ball.X;
    Pong_BallTrailY[0] = Pong_Game.Ball.Y;
}

static void Pong_ResetMatch(void)
{
    Pong_Game.LeftScore = 0U;
    Pong_Game.RightScore = 0U;
    Pong_Game.LastWinner = 0U;
    Pong_Game.LastHitLeft = true;
    Pong_Game.RallyElapsedMilliseconds = 0U;
    Pong_Game.BallSpeedUpElapsedMilliseconds = 0U;
    Pong_Game.BotIntegral = 0;
    Pong_Game.BotPreviousError = 0;
    Pong_Game.LeftExpandMilliseconds = 0U;
    Pong_Game.RightExpandMilliseconds = 0U;
    Pong_Game.LeftShrinkMilliseconds = 0U;
    Pong_Game.RightShrinkMilliseconds = 0U;
    Pong_Game.LeftInvertMilliseconds = 0U;
    Pong_Game.RightInvertMilliseconds = 0U;
    Pong_Game.LeftFreezeMilliseconds = 0U;
    Pong_Game.RightFreezeMilliseconds = 0U;
    Pong_Game.LeftPaddleFlashMilliseconds = 0U;
    Pong_Game.RightPaddleFlashMilliseconds = 0U;
    Pong_Game.LeftShieldMilliseconds = 0U;
    Pong_Game.RightShieldMilliseconds = 0U;
    Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE;
    Pong_Game.PowerUp.SpawnTimerMilliseconds = POWER_UP_DELAY_MILLISECONDS;
    Pong_Game.ServeDelayMilliseconds = 900U;
    Pong_Game.Screen = PONG_SCREEN_SERVE;
    Pong_ResetBall(true);
}

static uint32_t Pong_Random(void)
{
    Pong_Game.RandomState = (Pong_Game.RandomState * 1664525U) + 1013904223U;
    return Pong_Game.RandomState;
}

static void Pong_AwardPoint(bool Left)
{
    uint8_t *Score = Left ? &Pong_Game.LeftScore : &Pong_Game.RightScore;
    (*Score)++;
    Pong_Game.LastWinner = Left ? 1U : 2U;
    if(*Score >= WIN_SCORE)
    {
        Pong_Game.Screen = PONG_SCREEN_GAME_OVER;
        return;
    }
    Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE;
    if(!Pong_Game.TwoPlayer && (Pong_Game.BotDifficulty != PONG_BOT_IMPOSSIBLE))
    {
        const uint32_t FailureChance = Pong_Game.BotDifficulty == PONG_BOT_EASY ? 55U : (Pong_Game.BotDifficulty == PONG_BOT_MEDIUM ? 22U : 7U);
        Pong_Game.BotMistakeMilliseconds = ((Pong_Random() % 100U) < FailureChance) ? 2500U : 0U;
    }
    Pong_Game.ServeDelayMilliseconds = 850U;
    Pong_Game.Screen = PONG_SCREEN_SERVE;
    Pong_ResetBall(!Left);
}

static bool Pong_BallHitsRect(int16_t X, int16_t Y, uint16_t Width, uint16_t Height)
{
    const int16_t BallX = (int16_t)(Pong_Game.Ball.X / BALL_FIXED_SCALE);
    const int16_t BallY = (int16_t)(Pong_Game.Ball.Y / BALL_FIXED_SCALE);
    return (BallX + (int16_t)BALL_SIZE > X) && (BallX < X + (int16_t)Width) && (BallY + (int16_t)BALL_SIZE > Y) && (BallY < Y + (int16_t)Height);
}

static void Pong_ApplyPowerUp(void)
{
    const bool IsBad = (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK) || (Pong_Game.PowerUp.Type == PONG_POWER_UP_HASTE) || (Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT) || (Pong_Game.PowerUp.Type == PONG_POWER_UP_FREEZE);
    const bool Left = IsBad ? !Pong_Game.LastHitLeft : Pong_Game.LastHitLeft;
    if(Pong_Game.PowerUp.Type == PONG_POWER_UP_EXPAND)
    {
        if(Left) Pong_Game.LeftExpandMilliseconds = POWER_UP_DURATION_MILLISECONDS;
        else Pong_Game.RightExpandMilliseconds = POWER_UP_DURATION_MILLISECONDS;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_SHIELD)
    {
        if(Left) Pong_Game.LeftShieldMilliseconds = POWER_UP_DURATION_MILLISECONDS;
        else Pong_Game.RightShieldMilliseconds = POWER_UP_DURATION_MILLISECONDS;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_SLOW)
    {
        Pong_Game.Ball.VelocityX = (Pong_Game.Ball.VelocityX * 3) / 4;
        Pong_Game.Ball.VelocityY = (Pong_Game.Ball.VelocityY * 3) / 4;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK)
    {
        if(Left) Pong_Game.LeftShrinkMilliseconds = POWER_UP_DURATION_MILLISECONDS;
        else Pong_Game.RightShrinkMilliseconds = POWER_UP_DURATION_MILLISECONDS;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_HASTE)
    {
        Pong_Game.Ball.VelocityX = (Pong_Game.Ball.VelocityX * 5) / 4;
        Pong_Game.Ball.VelocityY = (Pong_Game.Ball.VelocityY * 5) / 4;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT)
    {
        if(Left) Pong_Game.LeftInvertMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
        else Pong_Game.RightInvertMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_FREEZE)
    {
        if(Left) Pong_Game.LeftFreezeMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
        else Pong_Game.RightFreezeMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
    }
    Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE;
    Pong_Game.PowerUp.SpawnTimerMilliseconds = POWER_UP_DELAY_MILLISECONDS;
}

static void Pong_UpdateBotPaddle(const Pong_BallTypeDef *Ball, bool Left, int16_t *PaddleY, int16_t PaddleHeight, int16_t MinimumY, int16_t MaximumY, Pong_BotDifficultyTypeDef Difficulty, uint32_t MistakeMilliseconds, int32_t *Integral, int16_t *PreviousError, uint32_t DeltaTimeMilliseconds)
{
    const int16_t ProportionalGain = Difficulty == PONG_BOT_EASY ? 1 : (Difficulty == PONG_BOT_MEDIUM ? 2 : (Difficulty == PONG_BOT_HARD ? 3 : 5));
    const int16_t MaximumMove = Difficulty == PONG_BOT_EASY ? 3 : (Difficulty == PONG_BOT_MEDIUM ? 6 : (Difficulty == PONG_BOT_HARD ? 9 : 18));
    const int16_t MistakeOffset = MistakeMilliseconds > 0U ? 150 : 0;
    const bool BallApproachesPaddle = Left ? (Ball->VelocityX < 0) : (Ball->VelocityX > 0);
    const int16_t DesiredY = BallApproachesPaddle ? Pong_Clamp((int16_t)((Ball->Y / BALL_FIXED_SCALE) - (PaddleHeight / 2) + MistakeOffset), MinimumY, MaximumY) : (MinimumY + MaximumY) / 2;
    const int16_t Error = (int16_t)(DesiredY - *PaddleY);
    const int16_t Derivative = (int16_t)(Error - *PreviousError);
    int32_t Output;
    *Integral += ((int32_t)Error * (int32_t)DeltaTimeMilliseconds) / 1000;
    if(*Integral > 220) *Integral = 220;
    if(*Integral < -220) *Integral = -220;
    Output = ((int32_t)ProportionalGain * Error) + (*Integral / 3) + ((int32_t)Derivative * 2);
    if(Output > MaximumMove) Output = MaximumMove;
    if(Output < -MaximumMove) Output = -MaximumMove;
    *PaddleY = Pong_Clamp((int16_t)(*PaddleY + Output), MinimumY, MaximumY);
    *PreviousError = Error;
}

static void Pong_UpdatePlaying(uint32_t DeltaTimeMilliseconds)
{
    Pong_BallTypeDef *Ball = &Pong_Game.Ball;
    const int16_t LeftHeight = Pong_PaddleHeight(true);
    const int16_t RightHeight = Pong_PaddleHeight(false);
    const int16_t LeftX = PADDLE_MARGIN;
    const int16_t RightX = (int16_t)RENDER_WIDTH - PADDLE_MARGIN - (int16_t)PADDLE_WIDTH;
    if(!Pong_Game.TwoPlayer)
    {
        Pong_UpdateBotPaddle(Ball, true, &Pong_Input.LeftY, LeftHeight, 42, (int16_t)RENDER_HEIGHT - LeftHeight - 42, Pong_Game.BotDifficulty, Pong_Game.BotMistakeMilliseconds, &Pong_Game.BotIntegral, &Pong_Game.BotPreviousError, DeltaTimeMilliseconds);
    }
    Pong_Input.LeftY = Pong_Clamp(Pong_Input.LeftY, 42, (int16_t)RENDER_HEIGHT - LeftHeight - 42);
    Pong_Input.RightY = Pong_Clamp(Pong_Input.RightY, 42, (int16_t)RENDER_HEIGHT - RightHeight - 42);
    Pong_Game.RallyElapsedMilliseconds += DeltaTimeMilliseconds;
    Pong_Game.BallSpeedUpElapsedMilliseconds += DeltaTimeMilliseconds;
    if(Pong_Game.BallSpeedUpElapsedMilliseconds >= BALL_SPEED_UP_INTERVAL_MILLISECONDS)
    {
        Pong_Game.BallSpeedUpElapsedMilliseconds -= BALL_SPEED_UP_INTERVAL_MILLISECONDS;
        Ball->VelocityX = (Ball->VelocityX * 110) / 100;
        Ball->VelocityY = (Ball->VelocityY * 110) / 100;
    }
    Ball->X += (int32_t)(((int64_t)Ball->VelocityX * DeltaTimeMilliseconds) / 1000);
    Ball->Y += (int32_t)(((int64_t)Ball->VelocityY * DeltaTimeMilliseconds) / 1000);
    if((Ball->Y < (42 * BALL_FIXED_SCALE)) || (Ball->Y > ((int32_t)RENDER_HEIGHT - 42 - (int32_t)BALL_SIZE) * BALL_FIXED_SCALE))
    {
        Ball->VelocityY = -Ball->VelocityY;
        Ball->Y = Pong_Clamp((int16_t)(Ball->Y / BALL_FIXED_SCALE), 42, (int16_t)RENDER_HEIGHT - 42 - (int16_t)BALL_SIZE) * BALL_FIXED_SCALE;
    }
    Pong_RecordBallTrail();
    if((Ball->VelocityX < 0) && Pong_BallHitsRect(LeftX, Pong_Input.LeftY, PADDLE_WIDTH, (uint16_t)LeftHeight))
    {
        const int32_t HitOffset = (Ball->Y / BALL_FIXED_SCALE) + ((int32_t)BALL_SIZE / 2) - Pong_Input.LeftY - (LeftHeight / 2);
        Pong_Game.LastHitLeft = true;
        Pong_Game.LeftPaddleFlashMilliseconds = PADDLE_HIT_FLASH_MILLISECONDS;
        Ball->VelocityX = -Ball->VelocityX;
        Ball->VelocityY = (HitOffset * 500 * BALL_FIXED_SCALE) / (LeftHeight / 2);
    }
    else if((Ball->VelocityX > 0) && Pong_BallHitsRect(RightX, Pong_Input.RightY, PADDLE_WIDTH, (uint16_t)RightHeight))
    {
        const int32_t HitOffset = (Ball->Y / BALL_FIXED_SCALE) + ((int32_t)BALL_SIZE / 2) - Pong_Input.RightY - (RightHeight / 2);
        Pong_Game.LastHitLeft = false;
        Pong_Game.RightPaddleFlashMilliseconds = PADDLE_HIT_FLASH_MILLISECONDS;
        Ball->VelocityX = -Ball->VelocityX;
        Ball->VelocityY = (HitOffset * 500 * BALL_FIXED_SCALE) / (RightHeight / 2);
    }
    if(Ball->VelocityX > BALL_MAX_SPEED) Ball->VelocityX = BALL_MAX_SPEED;
    if(Ball->VelocityX < -BALL_MAX_SPEED) Ball->VelocityX = -BALL_MAX_SPEED;
    if((Pong_Game.PowerUp.Type != PONG_POWER_UP_NONE) && (Pong_Game.PowerUp.GrowElapsedMilliseconds >= POWER_UP_GROW_MILLISECONDS) && Pong_BallHitsRect(Pong_Game.PowerUp.X, Pong_Game.PowerUp.Y, POWER_UP_SIZE, POWER_UP_SIZE)) Pong_ApplyPowerUp();
    if(Ball->X < (12 * BALL_FIXED_SCALE))
    {
        if(Pong_Game.LeftShieldMilliseconds > 0U) { Ball->VelocityX = -Ball->VelocityX; Ball->X = 12 * BALL_FIXED_SCALE; }
        else Pong_AwardPoint(false);
    }
    else if(Ball->X > (((int32_t)RENDER_WIDTH - 12 - (int32_t)BALL_SIZE) * BALL_FIXED_SCALE))
    {
        if(Pong_Game.RightShieldMilliseconds > 0U) { Ball->VelocityX = -Ball->VelocityX; Ball->X = ((int32_t)RENDER_WIDTH - 12 - (int32_t)BALL_SIZE) * BALL_FIXED_SCALE; }
        else Pong_AwardPoint(true);
    }
}

static void Pong_DrawRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    const Render_RectTypeDef Rect = { X, Y, Width, Height };
    Render_FillRect(Target, &Rect, Colour);
}

static void Pong_DrawNumber(Render_TargetTypeDef *Target, uint8_t Number, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    char Text[2] = { (char)('0' + Number), '\0' };
    Render_DrawText(Target, &OpenSans36, Text, X, Y, Colour);
}

static void Pong_DrawPaddle(Render_TargetTypeDef *Target, bool Left, int16_t Y, int16_t Height, Render_ColourIndexTypeDef BaseColour, bool Flashing)
{
    const int16_t BaseX = Left ? PADDLE_MARGIN : ((int16_t)RENDER_WIDTH - PADDLE_MARGIN - (int16_t)PADDLE_WIDTH);
    const Render_ColourIndexTypeDef Colour = Flashing ? COLOUR_PADDLE_FLASH : BaseColour;
    Pong_DrawRect(Target, BaseX, Y, PADDLE_WIDTH, (uint16_t)Height, Colour);
}

static void Pong_DrawCourt(Render_TargetTypeDef *Target)
{
    const int16_t LeftHeight = Pong_PaddleHeight(true);
    const int16_t RightHeight = Pong_PaddleHeight(false);
    const int16_t DotStartX = ((int16_t)RENDER_WIDTH / 2) % 28;
    const int16_t DotStartY = ((int16_t)RENDER_HEIGHT / 2) % 28;
    Render_Clear(Target, COLOUR_BACKGROUND);
    for(int16_t Y = DotStartY; Y < (int16_t)RENDER_HEIGHT; Y += 28)
        for(int16_t X = DotStartX; X < (int16_t)RENDER_WIDTH; X += 28) Pong_DrawRect(Target, X, Y, 2U, 2U, COLOUR_LINE);
    if(Pong_Game.LeftShieldMilliseconds > 0U) Pong_DrawRect(Target, 28, 48, 4U, RENDER_HEIGHT - 96U, COLOUR_SHIELD_GLOW);
    if(Pong_Game.RightShieldMilliseconds > 0U) Pong_DrawRect(Target, (int16_t)RENDER_WIDTH - 32, 48, 4U, RENDER_HEIGHT - 96U, COLOUR_SHIELD_GLOW);
    Pong_DrawPaddle(Target, true, Pong_Input.LeftY, LeftHeight, COLOUR_LEFT_PADDLE, Pong_Game.LeftPaddleFlashMilliseconds > 0U);
    Pong_DrawPaddle(Target, false, Pong_Input.RightY, RightHeight, COLOUR_RIGHT_PADDLE, Pong_Game.RightPaddleFlashMilliseconds > 0U);
    Pong_DrawNumber(Target, Pong_Game.LeftScore, (int16_t)RENDER_WIDTH / 2 - 60, 34, COLOUR_LEFT_PADDLE);
    Pong_DrawNumber(Target, Pong_Game.RightScore, (int16_t)RENDER_WIDTH / 2 + 36, 34, COLOUR_RIGHT_PADDLE);
    if(Pong_Game.PowerUp.Type != PONG_POWER_UP_NONE)
    {
        Render_ColourIndexTypeDef Colour = Pong_Game.PowerUp.Type == PONG_POWER_UP_EXPAND ? COLOUR_EXPAND : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SLOW ? COLOUR_SLOW : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHIELD ? COLOUR_SHIELD : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK ? COLOUR_RIGHT_PADDLE : (Pong_Game.PowerUp.Type == PONG_POWER_UP_HASTE ? COLOUR_HASTE : (Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT ? COLOUR_INVERT : COLOUR_FREEZE)))));
        const uint16_t Size = (uint16_t)((POWER_UP_SIZE * (Pong_Game.PowerUp.GrowElapsedMilliseconds < POWER_UP_GROW_MILLISECONDS ? Pong_Game.PowerUp.GrowElapsedMilliseconds : POWER_UP_GROW_MILLISECONDS)) / POWER_UP_GROW_MILLISECONDS);
        const int16_t X = (int16_t)(Pong_Game.PowerUp.X + (((int16_t)POWER_UP_SIZE - (int16_t)Size) / 2));
        const int16_t Y = (int16_t)(Pong_Game.PowerUp.Y + (((int16_t)POWER_UP_SIZE - (int16_t)Size) / 2));
        if(Size > 0U) Pong_DrawRect(Target, X, Y, Size, Size, Colour);
        if(Size >= 18U) Render_DrawText(Target, &OpenSans20, Pong_Game.PowerUp.Type == PONG_POWER_UP_EXPAND ? "+" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SLOW ? "~" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHIELD ? "=" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK ? "-" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_HASTE ? "!" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT ? "?" : "X"))))), (int16_t)(Pong_Game.PowerUp.X + 6), (int16_t)(Pong_Game.PowerUp.Y - 1), COLOUR_BACKGROUND);
    }
    Pong_DrawRect(Target, (int16_t)(Pong_BallTrailX[3] / BALL_FIXED_SCALE) + 4, (int16_t)(Pong_BallTrailY[3] / BALL_FIXED_SCALE) + 4, 4U, 4U, COLOUR_TRAIL_FAR);
    Pong_DrawRect(Target, (int16_t)(Pong_BallTrailX[2] / BALL_FIXED_SCALE) + 3, (int16_t)(Pong_BallTrailY[2] / BALL_FIXED_SCALE) + 3, 6U, 6U, COLOUR_TRAIL_MID);
    Pong_DrawRect(Target, (int16_t)(Pong_BallTrailX[1] / BALL_FIXED_SCALE) + 2, (int16_t)(Pong_BallTrailY[1] / BALL_FIXED_SCALE) + 2, 8U, 8U, COLOUR_TRAIL_NEAR);
    Pong_DrawRect(Target, (int16_t)(Pong_Game.Ball.X / BALL_FIXED_SCALE), (int16_t)(Pong_Game.Ball.Y / BALL_FIXED_SCALE), BALL_SIZE, BALL_SIZE, COLOUR_BALL);
}

static void Pong_DrawSettingsMenu(Render_TargetTypeDef *Target)
{
    const char *Difficulty = Pong_Game.BotDifficulty == PONG_BOT_EASY ? "EASY" : (Pong_Game.BotDifficulty == PONG_BOT_MEDIUM ? "MEDIUM" : (Pong_Game.BotDifficulty == PONG_BOT_HARD ? "HARD" : "IMPOSSIBLE"));
    const int16_t PlayersY = 202;
    const int16_t DifficultyY = 246;
    const int16_t PowerUpsY = Pong_Game.TwoPlayer ? 246 : 290;
    Render_Clear(Target, COLOUR_BACKGROUND);
    Pong_DrawRect(Target, 116, 84, RENDER_WIDTH - 232U, 320U, COLOUR_PANEL);
    Pong_DrawRect(Target, 116, 104, RENDER_WIDTH - 232U, 4U, COLOUR_PANEL_EDGE);
    Render_DrawText(Target, &OpenSans36, "PONG", 338, 120, COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, "PLAYERS", 206, PlayersY, Pong_Game.MenuItem == PONG_MENU_PLAYERS ? COLOUR_BALL : COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, Pong_Game.TwoPlayer ? "2" : "1", 530, PlayersY, Pong_Game.MenuItem == PONG_MENU_PLAYERS ? COLOUR_BALL : COLOUR_TEXT);
    if(!Pong_Game.TwoPlayer)
    {
        Render_DrawText(Target, &OpenSans20, "BOT DIFFICULTY", 206, DifficultyY, Pong_Game.MenuItem == PONG_MENU_DIFFICULTY ? COLOUR_BALL : COLOUR_TEXT);
        Render_DrawText(Target, &OpenSans20, Difficulty, 530, DifficultyY, Pong_Game.MenuItem == PONG_MENU_DIFFICULTY ? COLOUR_BALL : COLOUR_TEXT);
    }
    Render_DrawText(Target, &OpenSans20, "POWERUPS", 206, PowerUpsY, Pong_Game.MenuItem == PONG_MENU_POWER_UPS ? COLOUR_BALL : COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, Pong_Game.PowerUpsEnabled ? "ON" : "OFF", 530, PowerUpsY, Pong_Game.MenuItem == PONG_MENU_POWER_UPS ? COLOUR_BALL : COLOUR_TEXT);
}

static void Pong_DrawGameOverMenu(Render_TargetTypeDef *Target)
{
    Render_Clear(Target, COLOUR_BACKGROUND);
    Pong_DrawRect(Target, 116, 104, RENDER_WIDTH - 232U, 278U, COLOUR_PANEL);
    Pong_DrawRect(Target, 116, 104, RENDER_WIDTH - 232U, 4U, COLOUR_PANEL_EDGE);
    Render_DrawText(Target, &OpenSans36, Pong_Game.LastWinner == 1U ? "BLUE WINS" : "RED WINS", 285, 148, Pong_Game.LastWinner == 1U ? COLOUR_LEFT_PADDLE : COLOUR_RIGHT_PADDLE);
    Render_DrawText(Target, &OpenSans20, "FIRST TO 7 POINTS", 310, 286, COLOUR_MUTED_TEXT);
}

static bool Pong_ProcessMenuHardStops(int32_t SliderValue)
{
    const Pong_MenuItemTypeDef LastItem = Pong_Game.TwoPlayer ? PONG_MENU_POWER_UPS : PONG_MENU_POWER_UPS;
    if(!Pong_Input.RightSliderArmed)
    {
        if((SliderValue >= PONG_SLIDER_TOP_RELEASE) && (SliderValue <= PONG_SLIDER_BOTTOM_RELEASE)) Pong_Input.RightSliderArmed = true;
        return false;
    }
    if(SliderValue <= PONG_SLIDER_TOP_TRIGGER)
    {
        if(Pong_Game.MenuItem == LastItem) Pong_Game.MenuItem = PONG_MENU_PLAYERS;
        else if(Pong_Game.TwoPlayer && (Pong_Game.MenuItem == PONG_MENU_PLAYERS)) Pong_Game.MenuItem = PONG_MENU_POWER_UPS;
        else Pong_Game.MenuItem = (Pong_MenuItemTypeDef)(Pong_Game.MenuItem + 1U);
        Pong_Input.RightSliderArmed = false;
        return true;
    }
    if(SliderValue >= PONG_SLIDER_BOTTOM_TRIGGER)
    {
        if(Pong_Game.MenuItem == PONG_MENU_PLAYERS) Pong_Game.MenuItem = LastItem;
        else if(Pong_Game.TwoPlayer && (Pong_Game.MenuItem == PONG_MENU_POWER_UPS)) Pong_Game.MenuItem = PONG_MENU_PLAYERS;
        else Pong_Game.MenuItem = (Pong_MenuItemTypeDef)(Pong_Game.MenuItem - 1U);
        Pong_Input.RightSliderArmed = false;
        return true;
    }
    return false;
}

static void Pong_ChangeSelectedMenuValue(void)
{
    if(Pong_Game.MenuItem == PONG_MENU_PLAYERS)
    {
        Pong_Game.TwoPlayer = !Pong_Game.TwoPlayer;
        if(Pong_Game.TwoPlayer && (Pong_Game.MenuItem == PONG_MENU_DIFFICULTY)) Pong_Game.MenuItem = PONG_MENU_PLAYERS;
    }
    else if(Pong_Game.MenuItem == PONG_MENU_DIFFICULTY) Pong_Game.BotDifficulty = (Pong_BotDifficultyTypeDef)((Pong_Game.BotDifficulty + 1U) % 4U);
    else Pong_Game.PowerUpsEnabled = !Pong_Game.PowerUpsEnabled;
}

bool Pong_Init(void)
{
    Pong_Input.LeftY = ((int16_t)RENDER_HEIGHT - PADDLE_NORMAL_HEIGHT) / 2;
    Pong_Input.RightY = Pong_Input.LeftY;
    Pong_Input.PrimaryDown = false;
    Pong_Input.SecondaryDown = false;
    Pong_Input.RightSliderArmed = false;
    Pong_Game.Screen = PONG_SCREEN_MENU;
    Pong_Game.TwoPlayer = false;
    Pong_Game.PowerUpsEnabled = true;
    Pong_Game.BotDifficulty = PONG_BOT_MEDIUM;
    Pong_Game.MenuItem = PONG_MENU_PLAYERS;
    Pong_Game.RandomState = 0x50A6C31DU;
    Pong_SplashInitialized = false;
    Pong_SplashLeftBotIntegral = 0;
    Pong_SplashRightBotIntegral = 0;
    Pong_SplashLeftBotPreviousError = 0;
    Pong_SplashRightBotPreviousError = 0;
    Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE;
    Pong_PendingDeltaTimeMilliseconds = 0U;
    Pong_Paused = false;
    Pong_Initialized = true;
    return true;
}

void Pong_Update(uint32_t DeltaTimeMilliseconds)
{
    int32_t Value;
    bool Primary = false;
    bool Secondary = false;
    if(!Pong_Initialized || Pong_Paused) return;
    if(Pong_Game.TwoPlayer && Input_Get_Value(INPUT_LEFT_SLIDER_NUMBER, &Value) && (Pong_Game.LeftFreezeMilliseconds == 0U)) Pong_Input.LeftY = Pong_SliderToY(Pong_Game.LeftInvertMilliseconds > 0U ? (65535 - Value) : Value, Pong_PaddleHeight(true));
    if(Input_Get_Value(INPUT_RIGHT_SLIDER_NUMBER, &Value))
    {
        if((Pong_Game.Screen != PONG_SCREEN_MENU) && (Pong_Game.RightFreezeMilliseconds == 0U)) Pong_Input.RightY = Pong_SliderToY(Pong_Game.RightInvertMilliseconds > 0U ? (65535 - Value) : Value, Pong_PaddleHeight(false));
        else if(Pong_Game.Screen == PONG_SCREEN_MENU) (void)Pong_ProcessMenuHardStops(Value);
    }
    if(Input_Get_Value(INPUT_PRIMARY_BUTTON_NUMBER, &Value)) Primary = Value != 0;
    if(Input_Get_Value(INPUT_SECONDARY_BUTTON_NUMBER, &Value)) Secondary = Value != 0;
    if(Secondary && !Pong_Input.SecondaryDown && (Pong_Game.Screen == PONG_SCREEN_MENU)) Pong_ChangeSelectedMenuValue();
    if(Primary && !Pong_Input.PrimaryDown && ((Pong_Game.Screen == PONG_SCREEN_MENU) || (Pong_Game.Screen == PONG_SCREEN_GAME_OVER))) Pong_ResetMatch();
    Pong_Input.PrimaryDown = Primary;
    Pong_Input.SecondaryDown = Secondary;
    Pong_PendingDeltaTimeMilliseconds += DeltaTimeMilliseconds;
}

bool Pong_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL) return false;
    for(uint16_t Index = 0U; Index < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; Index++) Palette[Index] = 0U;
    for(uint16_t Index = 0U; Index < (uint16_t)(sizeof(Pong_Palette) / sizeof(Pong_Palette[0])); Index++) Palette[Index] = Pong_Palette[Index];
    return true;
}

static void Pong_UpdateSplash(void)
{
    const int16_t PaddleHeight = PADDLE_NORMAL_HEIGHT;
    const int16_t TopY = PONG_SPLASH_Y + 12;
    const int16_t BottomY = PONG_SPLASH_Y + PONG_SPLASH_HEIGHT - 12;
    const int16_t LeftPaddleX = PONG_SPLASH_X + 26;
    const int16_t RightPaddleX = PONG_SPLASH_X + PONG_SPLASH_WIDTH - 26 - PADDLE_WIDTH;
    Pong_BallTypeDef SplashBall;
    if(!Pong_SplashInitialized)
    {
        Pong_SplashBallX = ((LeftPaddleX + RightPaddleX) / 2) * BALL_FIXED_SCALE;
        Pong_SplashBallY = ((TopY + BottomY) / 2) * BALL_FIXED_SCALE;
        Pong_SplashBallVelocityX = 200 * BALL_FIXED_SCALE;
        Pong_SplashBallVelocityY = 75 * BALL_FIXED_SCALE;
        Pong_SplashLeftY = (TopY + BottomY - PaddleHeight) / 2;
        Pong_SplashRightY = Pong_SplashLeftY;
        Pong_SplashInitialized = true;
        return;
    }
    SplashBall.X = Pong_SplashBallX;
    SplashBall.Y = Pong_SplashBallY;
    SplashBall.VelocityX = Pong_SplashBallVelocityX;
    SplashBall.VelocityY = Pong_SplashBallVelocityY;
    Pong_UpdateBotPaddle(&SplashBall, true, &Pong_SplashLeftY, PaddleHeight, TopY, BottomY - PaddleHeight, PONG_BOT_MEDIUM, 0U, &Pong_SplashLeftBotIntegral, &Pong_SplashLeftBotPreviousError, 33U);
    Pong_UpdateBotPaddle(&SplashBall, false, &Pong_SplashRightY, PaddleHeight, TopY, BottomY - PaddleHeight, PONG_BOT_MEDIUM, 0U, &Pong_SplashRightBotIntegral, &Pong_SplashRightBotPreviousError, 33U);
    Pong_SplashBallX += (Pong_SplashBallVelocityX * 33) / 1000;
    Pong_SplashBallY += (Pong_SplashBallVelocityY * 33) / 1000;
    if((Pong_SplashBallY < (TopY * BALL_FIXED_SCALE)) || (Pong_SplashBallY > ((BottomY - (int16_t)BALL_SIZE) * BALL_FIXED_SCALE))) Pong_SplashBallVelocityY = -Pong_SplashBallVelocityY;
    if((Pong_SplashBallVelocityX < 0) && ((Pong_SplashBallX / BALL_FIXED_SCALE) <= (LeftPaddleX + (int16_t)PADDLE_WIDTH)) && ((Pong_SplashBallY / BALL_FIXED_SCALE) + (int16_t)BALL_SIZE > Pong_SplashLeftY) && ((Pong_SplashBallY / BALL_FIXED_SCALE) < (Pong_SplashLeftY + PaddleHeight))) Pong_SplashBallVelocityX = -Pong_SplashBallVelocityX;
    if((Pong_SplashBallVelocityX > 0) && (((Pong_SplashBallX / BALL_FIXED_SCALE) + (int16_t)BALL_SIZE) >= RightPaddleX) && ((Pong_SplashBallY / BALL_FIXED_SCALE) + (int16_t)BALL_SIZE > Pong_SplashRightY) && ((Pong_SplashBallY / BALL_FIXED_SCALE) < (Pong_SplashRightY + PaddleHeight))) Pong_SplashBallVelocityX = -Pong_SplashBallVelocityX;
    if((Pong_SplashBallX < ((LeftPaddleX - 24) * BALL_FIXED_SCALE)) || (Pong_SplashBallX > ((RightPaddleX + 24) * BALL_FIXED_SCALE)))
    {
        Pong_SplashBallX = ((LeftPaddleX + RightPaddleX) / 2) * BALL_FIXED_SCALE;
        Pong_SplashBallY = ((TopY + BottomY) / 2) * BALL_FIXED_SCALE;
        Pong_SplashBallVelocityX = Pong_SplashBallVelocityX < 0 ? 200 * BALL_FIXED_SCALE : -200 * BALL_FIXED_SCALE;
    }
}

bool Pong_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef Bounds = { APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y, APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT };
    if((Target == NULL) || (Target->Pixels == NULL)) return false;
    Pong_SplashElapsedMilliseconds += 33U;
    Pong_UpdateSplash();
    Render_FillRect(Target, &Bounds, COLOUR_BACKGROUND);
    Pong_DrawRect(Target, PONG_SPLASH_X + 26, Pong_SplashLeftY, PADDLE_WIDTH, PADDLE_NORMAL_HEIGHT, COLOUR_LEFT_PADDLE);
    Pong_DrawRect(Target, PONG_SPLASH_X + PONG_SPLASH_WIDTH - 26 - PADDLE_WIDTH, Pong_SplashRightY, PADDLE_WIDTH, PADDLE_NORMAL_HEIGHT, COLOUR_RIGHT_PADDLE);
    Pong_DrawRect(Target, (int16_t)(Pong_SplashBallX / BALL_FIXED_SCALE), (int16_t)(Pong_SplashBallY / BALL_FIXED_SCALE), BALL_SIZE, BALL_SIZE, COLOUR_BALL);
    return true;
}

void Pong_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    uint32_t DeltaTimeMilliseconds;
    if(!Pong_Initialized || Pong_Paused) return;
    Frame = Display_AcquireFrame();
    if(Frame == NULL) return;
    Target.Pixels = Frame->Pixels; Target.Width = Frame->Width; Target.Height = Frame->Height; Target.StridePixels = Frame->StridePixels;
    DeltaTimeMilliseconds = Pong_PendingDeltaTimeMilliseconds;
    if(DeltaTimeMilliseconds > 50U) DeltaTimeMilliseconds = 50U;
    Pong_PendingDeltaTimeMilliseconds = 0U;
    if(Pong_Game.Screen == PONG_SCREEN_PLAYING) Pong_UpdatePlaying(DeltaTimeMilliseconds);
    else if(Pong_Game.Screen == PONG_SCREEN_SERVE)
    {
        if(Pong_Game.ServeDelayMilliseconds > DeltaTimeMilliseconds) Pong_Game.ServeDelayMilliseconds -= DeltaTimeMilliseconds;
        else { Pong_Game.ServeDelayMilliseconds = 0U; Pong_Game.Screen = PONG_SCREEN_PLAYING; }
    }
    if(Pong_Game.LeftExpandMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftExpandMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftExpandMilliseconds = 0U;
    if(Pong_Game.RightExpandMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightExpandMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightExpandMilliseconds = 0U;
    if(Pong_Game.LeftShrinkMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftShrinkMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftShrinkMilliseconds = 0U;
    if(Pong_Game.RightShrinkMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightShrinkMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightShrinkMilliseconds = 0U;
    if(Pong_Game.LeftInvertMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftInvertMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftInvertMilliseconds = 0U;
    if(Pong_Game.RightInvertMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightInvertMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightInvertMilliseconds = 0U;
    if(Pong_Game.LeftFreezeMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftFreezeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftFreezeMilliseconds = 0U;
    if(Pong_Game.RightFreezeMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightFreezeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightFreezeMilliseconds = 0U;
    if(Pong_Game.LeftPaddleFlashMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftPaddleFlashMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftPaddleFlashMilliseconds = 0U;
    if(Pong_Game.RightPaddleFlashMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightPaddleFlashMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightPaddleFlashMilliseconds = 0U;
    if(Pong_Game.BotMistakeMilliseconds > DeltaTimeMilliseconds) Pong_Game.BotMistakeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.BotMistakeMilliseconds = 0U;
    if(Pong_Game.LeftShieldMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftShieldMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftShieldMilliseconds = 0U;
    if(Pong_Game.RightShieldMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightShieldMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightShieldMilliseconds = 0U;
    if(Pong_Game.Screen == PONG_SCREEN_PLAYING && Pong_Game.PowerUpsEnabled)
    {
        if(Pong_Game.PowerUp.Type == PONG_POWER_UP_NONE)
        {
            if(Pong_Game.PowerUp.SpawnTimerMilliseconds > DeltaTimeMilliseconds) Pong_Game.PowerUp.SpawnTimerMilliseconds -= DeltaTimeMilliseconds;
            else { Pong_Game.PowerUp.Type = (Pong_PowerUpTypeDef)((Pong_Random() % 7U) + 1U); Pong_Game.PowerUp.X = (int16_t)RENDER_WIDTH / 2 - 12; Pong_Game.PowerUp.Y = 130 + (int16_t)(Pong_Random() % 210U); Pong_Game.PowerUp.LifetimeMilliseconds = POWER_UP_LIFETIME_MILLISECONDS; Pong_Game.PowerUp.GrowElapsedMilliseconds = 0U; }
        }
        else if(Pong_Game.PowerUp.GrowElapsedMilliseconds < POWER_UP_GROW_MILLISECONDS)
        {
            Pong_Game.PowerUp.GrowElapsedMilliseconds += DeltaTimeMilliseconds;
            if(Pong_Game.PowerUp.GrowElapsedMilliseconds > POWER_UP_GROW_MILLISECONDS) Pong_Game.PowerUp.GrowElapsedMilliseconds = POWER_UP_GROW_MILLISECONDS;
        }
        else if(Pong_Game.PowerUp.LifetimeMilliseconds > DeltaTimeMilliseconds) Pong_Game.PowerUp.LifetimeMilliseconds -= DeltaTimeMilliseconds;
        else { Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE; Pong_Game.PowerUp.SpawnTimerMilliseconds = POWER_UP_DELAY_MILLISECONDS; }
    }
    if(Pong_Game.Screen == PONG_SCREEN_MENU) Pong_DrawSettingsMenu(&Target);
    else if(Pong_Game.Screen == PONG_SCREEN_GAME_OVER) Pong_DrawGameOverMenu(&Target);
    else Pong_DrawCourt(&Target);
    (void)Display_PresentFrame(Frame);
}

void Pong_Pause(void) { Pong_Paused = true; }
void Pong_Resume(void) { Pong_Paused = false; }
void Pong_Shutdown(void) { Pong_Initialized = false; Pong_Paused = false; Pong_PendingDeltaTimeMilliseconds = 0U; }