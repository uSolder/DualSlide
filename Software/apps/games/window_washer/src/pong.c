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
#define BALL_SIZE (16U)
#define BALL_FIXED_SCALE (256)
#define BALL_START_SPEED (350 * BALL_FIXED_SCALE)
#define BALL_MAX_SPEED (760 * BALL_FIXED_SCALE)
#define WIN_SCORE (7U)
#define POWER_UP_SIZE (24U)
#define POWER_UP_DELAY_MILLISECONDS (6000U)
#define POWER_UP_LIFETIME_MILLISECONDS (14000U)
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
#define PONG_SPLASH_TITLE_X (PONG_SPLASH_X + ((int16_t)PONG_SPLASH_WIDTH / 2) - 62)
#define PONG_SPLASH_TITLE_Y (PONG_SPLASH_Y + ((int16_t)PONG_SPLASH_HEIGHT / 2) - 18)
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
    COLOUR_TRAIL_NEAR = 23U,
    COLOUR_WAVE_BLUE = 24U,
    COLOUR_WAVE_LILAC = 25U,
    COLOUR_WAVE_PEACH = 26U,
    COLOUR_WAVE_MINT = 27U,
    COLOUR_POWER = 28U,
    COLOUR_EDGE_FLASH_FIRST = 29U
};

typedef enum { PONG_SCREEN_MENU, PONG_SCREEN_SERVE, PONG_SCREEN_PLAYING, PONG_SCREEN_GAME_OVER } Pong_ScreenTypeDef;
typedef enum { PONG_POWER_UP_NONE, PONG_POWER_UP_EXPAND, PONG_POWER_UP_SHIELD, PONG_POWER_UP_SHRINK, PONG_POWER_UP_POWER, PONG_POWER_UP_INVERT } Pong_PowerUpTypeDef;
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
    int32_t PowerShotVelocityX;
    int32_t PowerShotVelocityY;
    bool PowerShotActive;
    bool PowerShotOwnerLeft;
    Pong_BallTypeDef Ball;
    Pong_PowerUpStateTypeDef PowerUp;
} Pong_GameTypeDef;

static const Display_ColourTypeDef Pong_Palette[] =
{
    0x00000000U, 0x00000000U, 0x001C2833U, 0x0058B9FFU, 0x00FF5D7AU,
    0x00FFE66DU, 0x00F7FFF7U, 0x00B7C9D9U, 0x002B4A6FU, 0x0091C8E4U,
    0x00B6F25AU, 0x007BDFF2U, 0x00F7FFF7U, 0x00D9FFF5U, 0x00263640U,
    0x00FF6B35U, 0x00C77DFFU, 0x006EE7D8U, 0x006C63FFU, 0x00FFF3B0U,
    0x00657785U, 0x00405361U, 0x002E4B3FU, 0x00496D5BU, 0x00739A7FU,
    0x00C6D9EFU, 0x00DDD0EEU, 0x00F4D0BFU, 0x00C6E1D0U, 0x00FFFF00U,
    0x00000000U, 0x00111111U, 0x00222222U, 0x00333333U, 0x00444444U, 0x00555555U, 0x00666666U, 0x00777777U,
    0x00888888U, 0x00999999U, 0x00AAAAAAU, 0x00BBBBBBU, 0x00CCCCCCU, 0x00DDDDDDU, 0x00EEEEEEU, 0x00FFFFFFU
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
    const int32_t MaximumY = (int32_t)RENDER_HEIGHT - PaddleHeight - 12;
    if(Value < 0) Value = 0;
    if(Value > 65535) Value = 65535;
    return (int16_t)(MaximumY - ((Value * (MaximumY - 12)) / 65535));
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
    Pong_Game.PowerShotActive = false;
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
    Pong_Game.LeftExpandMilliseconds = 0U;
    Pong_Game.RightExpandMilliseconds = 0U;
    Pong_Game.LeftShrinkMilliseconds = 0U;
    Pong_Game.RightShrinkMilliseconds = 0U;
    Pong_Game.LeftInvertMilliseconds = 0U;
    Pong_Game.RightInvertMilliseconds = 0U;
    Pong_Game.LeftShieldMilliseconds = 0U;
    Pong_Game.RightShieldMilliseconds = 0U;
    Pong_Game.PowerShotActive = false;
    if(*Score >= WIN_SCORE)
    {
        Pong_Game.Screen = PONG_SCREEN_GAME_OVER;
        return;
    }
    Pong_Game.PowerUp.Type = PONG_POWER_UP_NONE;
    if(!Pong_Game.TwoPlayer && !Left && (Pong_Game.BotDifficulty != PONG_BOT_IMPOSSIBLE))
    {
        const uint32_t FailureChance = Pong_Game.BotDifficulty == PONG_BOT_EASY ? 55U : (Pong_Game.BotDifficulty == PONG_BOT_MEDIUM ? 22U : 7U);
        Pong_Game.BotMistakeMilliseconds = ((Pong_Random() % 100U) < FailureChance) ? 2500U : 0U;
    }
    else Pong_Game.BotMistakeMilliseconds = 0U;
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
    const bool ActsOnOpponent = (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK) || (Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT);
    const bool Left = ActsOnOpponent ? !Pong_Game.LastHitLeft : Pong_Game.LastHitLeft;
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
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK)
    {
        if(Left) Pong_Game.LeftShrinkMilliseconds = POWER_UP_DURATION_MILLISECONDS;
        else Pong_Game.RightShrinkMilliseconds = POWER_UP_DURATION_MILLISECONDS;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_POWER)
    {
        Pong_Game.PowerShotVelocityX = Pong_Game.Ball.VelocityX;
        Pong_Game.PowerShotVelocityY = Pong_Game.Ball.VelocityY;
        Pong_Game.PowerShotOwnerLeft = Pong_Game.LastHitLeft;
        Pong_Game.PowerShotActive = true;
        Pong_Game.Ball.VelocityX = (Pong_Game.Ball.VelocityX * 3) / 2;
        Pong_Game.Ball.VelocityY = (Pong_Game.Ball.VelocityY * 3) / 2;
    }
    else if(Pong_Game.PowerUp.Type == PONG_POWER_UP_INVERT)
    {
        if(Left) Pong_Game.LeftInvertMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
        else Pong_Game.RightInvertMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS;
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
    if(Difficulty == PONG_BOT_IMPOSSIBLE)
    {
        *PaddleY = Pong_Clamp((int16_t)((Ball->Y / BALL_FIXED_SCALE) + ((int16_t)BALL_SIZE / 2) - (PaddleHeight / 2)), MinimumY, MaximumY);
        *Integral = 0;
        *PreviousError = 0;
        return;
    }
    if((Error >= -2) && (Error <= 2))
    {
        *Integral = 0;
        *PreviousError = 0;
        return;
    }
    *Integral += ((int32_t)Error * (int32_t)DeltaTimeMilliseconds) / 1000;
    if(*Integral > 220) *Integral = 220;
    if(*Integral < -220) *Integral = -220;
    Output = ((int32_t)ProportionalGain * Error) + (*Integral / 3) + ((int32_t)Derivative * 2);
    if(Output > MaximumMove) Output = MaximumMove;
    if(Output < -MaximumMove) Output = -MaximumMove;
    if((Error > 0) && (Output > Error)) Output = Error;
    if((Error < 0) && (Output < Error)) Output = Error;
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
        Pong_UpdateBotPaddle(Ball, true, &Pong_Input.LeftY, LeftHeight, 12, (int16_t)RENDER_HEIGHT - LeftHeight - 12, Pong_Game.BotDifficulty, Pong_Game.BotMistakeMilliseconds, &Pong_Game.BotIntegral, &Pong_Game.BotPreviousError, DeltaTimeMilliseconds);
    }
    Pong_Input.LeftY = Pong_Clamp(Pong_Input.LeftY, 12, (int16_t)RENDER_HEIGHT - LeftHeight - 12);
    Pong_Input.RightY = Pong_Clamp(Pong_Input.RightY, 12, (int16_t)RENDER_HEIGHT - RightHeight - 12);
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
    if((Ball->Y < 0) || (Ball->Y > ((int32_t)RENDER_HEIGHT - (int32_t)BALL_SIZE) * BALL_FIXED_SCALE))
    {
        Ball->VelocityY = -Ball->VelocityY;
        Ball->Y = Pong_Clamp((int16_t)(Ball->Y / BALL_FIXED_SCALE), 0, (int16_t)RENDER_HEIGHT - (int16_t)BALL_SIZE) * BALL_FIXED_SCALE;
    }
    Pong_RecordBallTrail();
    if((Ball->VelocityX < 0) && Pong_BallHitsRect(LeftX, Pong_Input.LeftY, PADDLE_WIDTH, (uint16_t)LeftHeight))
    {
        const int32_t HitOffset = (Ball->Y / BALL_FIXED_SCALE) + ((int32_t)BALL_SIZE / 2) - Pong_Input.LeftY - (LeftHeight / 2);
        if(Pong_Game.PowerShotActive && !Pong_Game.PowerShotOwnerLeft) { Ball->VelocityX = Pong_Game.PowerShotVelocityX; Pong_Game.PowerShotActive = false; }
        Pong_Game.LastHitLeft = true;
        Pong_Game.LeftPaddleFlashMilliseconds = PADDLE_HIT_FLASH_MILLISECONDS;
        Ball->VelocityX = -Ball->VelocityX;
        Ball->VelocityY = (HitOffset * 500 * BALL_FIXED_SCALE) / (LeftHeight / 2);
    }
    else if((Ball->VelocityX > 0) && Pong_BallHitsRect(RightX, Pong_Input.RightY, PADDLE_WIDTH, (uint16_t)RightHeight))
    {
        const int32_t HitOffset = (Ball->Y / BALL_FIXED_SCALE) + ((int32_t)BALL_SIZE / 2) - Pong_Input.RightY - (RightHeight / 2);
        if(Pong_Game.PowerShotActive && Pong_Game.PowerShotOwnerLeft) { Ball->VelocityX = Pong_Game.PowerShotVelocityX; Pong_Game.PowerShotActive = false; }
        Pong_Game.LastHitLeft = false;
        Pong_Game.RightPaddleFlashMilliseconds = PADDLE_HIT_FLASH_MILLISECONDS;
        Ball->VelocityX = -Ball->VelocityX;
        Ball->VelocityY = (HitOffset * 500 * BALL_FIXED_SCALE) / (RightHeight / 2);
    }
    if(Ball->VelocityX > (Pong_Game.PowerShotActive ? (BALL_MAX_SPEED * 3) / 2 : BALL_MAX_SPEED)) Ball->VelocityX = Pong_Game.PowerShotActive ? (BALL_MAX_SPEED * 3) / 2 : BALL_MAX_SPEED;
    if(Ball->VelocityX < -(Pong_Game.PowerShotActive ? (BALL_MAX_SPEED * 3) / 2 : BALL_MAX_SPEED)) Ball->VelocityX = -(Pong_Game.PowerShotActive ? (BALL_MAX_SPEED * 3) / 2 : BALL_MAX_SPEED);
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

static int16_t Pong_TextWidth(const Font *FontData, const char *Text)
{
    int16_t Width = 0;
    while(*Text != '\0') { const FontGlyph *Glyph = Font_GetGlyph(FontData, (uint8_t)*Text); if(Glyph != NULL) Width = (int16_t)(Width + Glyph->advance); Text++; }
    return Width;
}

static void Pong_DrawCenteredText(Render_TargetTypeDef *Target, const Font *FontData, const char *Text, int16_t CenterX, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    Render_DrawText(Target, FontData, Text, (int16_t)(CenterX - (Pong_TextWidth(FontData, Text) / 2)), Y, Colour);
}

static void Pong_DrawRightAlignedText(Render_TargetTypeDef *Target, const Font *FontData, const char *Text, int16_t RightX, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    Render_DrawText(Target, FontData, Text, (int16_t)(RightX - Pong_TextWidth(FontData, Text)), Y, Colour);
}

static int16_t Pong_TextVerticalPosition(const Font *FontData, const char *Text, int16_t BoxY, uint16_t BoxHeight)
{
    int16_t MinimumY = 127;
    int16_t MaximumY = -128;
    while(*Text != '\0') { const FontGlyph *Glyph = Font_GetGlyph(FontData, (uint8_t)*Text); if(Glyph != NULL) { if(Glyph->offsetY < MinimumY) MinimumY = Glyph->offsetY; if((int16_t)(Glyph->offsetY + Glyph->height) > MaximumY) MaximumY = (int16_t)(Glyph->offsetY + Glyph->height); } Text++; }
    return (int16_t)(BoxY + (((int16_t)BoxHeight - (MaximumY - MinimumY)) / 2) - FontData->ascent - MinimumY);
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

static void Pong_UpdateTimedEffect(uint32_t *Timer, uint32_t DeltaTimeMilliseconds)
{
    if(*Timer > DeltaTimeMilliseconds) *Timer -= DeltaTimeMilliseconds;
    else *Timer = 0U;
}

static void Pong_DrawEffectDurationBar(Render_TargetTypeDef *Target)
{
    uint32_t RemainingMilliseconds = 0U;
    uint32_t DurationMilliseconds = POWER_UP_DURATION_MILLISECONDS;
    Render_ColourIndexTypeDef Colour = COLOUR_BACKGROUND;
    if(Pong_Game.LeftExpandMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.LeftExpandMilliseconds; Colour = COLOUR_EXPAND; }
    if(Pong_Game.RightExpandMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.RightExpandMilliseconds; Colour = COLOUR_EXPAND; }
    if(Pong_Game.LeftShrinkMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.LeftShrinkMilliseconds; Colour = COLOUR_LEFT_PADDLE; }
    if(Pong_Game.RightShrinkMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.RightShrinkMilliseconds; Colour = COLOUR_LEFT_PADDLE; }
    if(Pong_Game.LeftShieldMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.LeftShieldMilliseconds; Colour = COLOUR_SHIELD; }
    if(Pong_Game.RightShieldMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.RightShieldMilliseconds; Colour = COLOUR_SHIELD; }
    if(Pong_Game.LeftInvertMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.LeftInvertMilliseconds; DurationMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS; Colour = COLOUR_INVERT; }
    if(Pong_Game.RightInvertMilliseconds > RemainingMilliseconds) { RemainingMilliseconds = Pong_Game.RightInvertMilliseconds; DurationMilliseconds = POWER_UP_LONG_DURATION_MILLISECONDS; Colour = COLOUR_INVERT; }
    if(RemainingMilliseconds > 0U) Pong_DrawRect(Target, 0, (int16_t)RENDER_HEIGHT - 4, (uint16_t)(((uint32_t)RENDER_WIDTH * RemainingMilliseconds) / DurationMilliseconds), 4U, Colour);
}

static void Pong_DrawDisc(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Diameter, Render_ColourIndexTypeDef Colour)
{
    const uint16_t CornerSize = (Diameter + 3U) / 4U;
    Pong_DrawRect(Target, X + (int16_t)CornerSize, Y, Diameter - (2U * CornerSize), CornerSize, Colour);
    Pong_DrawRect(Target, X, Y + (int16_t)CornerSize, Diameter, Diameter - (2U * CornerSize), Colour);
    Pong_DrawRect(Target, X + (int16_t)CornerSize, Y + (int16_t)Diameter - (int16_t)CornerSize, Diameter - (2U * CornerSize), CornerSize, Colour);
}

static void Pong_DrawDotMatrix(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height)
{
    int16_t DotY = (int16_t)((Y + (int16_t)(Height / 2U)) % 28);
    while(DotY < Y) DotY += 28;
    for(; DotY < (int16_t)(Y + Height); DotY += 28)
    {
        int16_t DotX = (int16_t)((X + (int16_t)(Width / 2U)) % 28);
        while(DotX < X) DotX += 28;
        for(; DotX < (int16_t)(X + Width); DotX += 28) Pong_DrawRect(Target, DotX, DotY, 2U, 2U, COLOUR_LINE);
    }
}

static void Pong_DrawCourt(Render_TargetTypeDef *Target)
{
    const int16_t LeftHeight = Pong_PaddleHeight(true);
    const int16_t RightHeight = Pong_PaddleHeight(false);
    Render_Clear(Target, COLOUR_BACKGROUND);
    Pong_DrawDotMatrix(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    if(Pong_Game.LeftShieldMilliseconds > 0U) Pong_DrawRect(Target, 28, 48, 4U, RENDER_HEIGHT - 96U, COLOUR_SHIELD_GLOW);
    if(Pong_Game.RightShieldMilliseconds > 0U) Pong_DrawRect(Target, (int16_t)RENDER_WIDTH - 32, 48, 4U, RENDER_HEIGHT - 96U, COLOUR_SHIELD_GLOW);
    Pong_DrawPaddle(Target, true, Pong_Input.LeftY, LeftHeight, COLOUR_LEFT_PADDLE, Pong_Game.LeftPaddleFlashMilliseconds > 0U);
    Pong_DrawPaddle(Target, false, Pong_Input.RightY, RightHeight, COLOUR_RIGHT_PADDLE, Pong_Game.RightPaddleFlashMilliseconds > 0U);
    Pong_DrawNumber(Target, Pong_Game.LeftScore, (int16_t)RENDER_WIDTH / 2 - 60, 34, COLOUR_LEFT_PADDLE);
    Pong_DrawNumber(Target, Pong_Game.RightScore, (int16_t)RENDER_WIDTH / 2 + 36, 34, COLOUR_RIGHT_PADDLE);
    if(Pong_Game.PowerUp.Type != PONG_POWER_UP_NONE)
    {
        Render_ColourIndexTypeDef Colour = Pong_Game.PowerUp.Type == PONG_POWER_UP_EXPAND ? COLOUR_EXPAND : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHIELD ? COLOUR_SHIELD : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK ? COLOUR_LEFT_PADDLE : (Pong_Game.PowerUp.Type == PONG_POWER_UP_POWER ? COLOUR_POWER : COLOUR_INVERT)));
        uint16_t Size = (uint16_t)((POWER_UP_SIZE * (Pong_Game.PowerUp.GrowElapsedMilliseconds < POWER_UP_GROW_MILLISECONDS ? Pong_Game.PowerUp.GrowElapsedMilliseconds : POWER_UP_GROW_MILLISECONDS)) / POWER_UP_GROW_MILLISECONDS);
        if(Size == POWER_UP_SIZE) { const uint8_t PulsePhase = (uint8_t)((Pong_Game.RallyElapsedMilliseconds / 80U) % 8U); Size = (uint16_t)(POWER_UP_SIZE - 4U + (PulsePhase <= 4U ? PulsePhase : 8U - PulsePhase)); }
        const int16_t X = (int16_t)(Pong_Game.PowerUp.X + (((int16_t)POWER_UP_SIZE - (int16_t)Size) / 2));
        const int16_t Y = (int16_t)(Pong_Game.PowerUp.Y + (((int16_t)POWER_UP_SIZE - (int16_t)Size) / 2));
        if(Size > 0U) Pong_DrawRect(Target, X, Y, Size, Size, Colour);
        if(Size >= 18U) Render_DrawText(Target, &OpenSans20, Pong_Game.PowerUp.Type == PONG_POWER_UP_EXPAND ? "+" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHIELD ? "=" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_SHRINK ? "-" : (Pong_Game.PowerUp.Type == PONG_POWER_UP_POWER ? "!" : "?"))), (int16_t)(Pong_Game.PowerUp.X + 6), (int16_t)(Pong_Game.PowerUp.Y - 1), COLOUR_BACKGROUND);
    }
    Pong_DrawDisc(Target, (int16_t)(Pong_BallTrailX[3] / BALL_FIXED_SCALE) + 4, (int16_t)(Pong_BallTrailY[3] / BALL_FIXED_SCALE) + 4, 8U, COLOUR_TRAIL_FAR);
    Pong_DrawDisc(Target, (int16_t)(Pong_BallTrailX[2] / BALL_FIXED_SCALE) + 3, (int16_t)(Pong_BallTrailY[2] / BALL_FIXED_SCALE) + 3, 10U, COLOUR_TRAIL_MID);
    Pong_DrawDisc(Target, (int16_t)(Pong_BallTrailX[1] / BALL_FIXED_SCALE) + 2, (int16_t)(Pong_BallTrailY[1] / BALL_FIXED_SCALE) + 2, 12U, COLOUR_TRAIL_NEAR);
    Pong_DrawDisc(Target, (int16_t)(Pong_Game.Ball.X / BALL_FIXED_SCALE), (int16_t)(Pong_Game.Ball.Y / BALL_FIXED_SCALE), BALL_SIZE, COLOUR_BALL);
}

static void Pong_DrawSettingsMenu(Render_TargetTypeDef *Target)
{
    const char *Difficulty = Pong_Game.BotDifficulty == PONG_BOT_EASY ? "EASY" : (Pong_Game.BotDifficulty == PONG_BOT_MEDIUM ? "MEDIUM" : (Pong_Game.BotDifficulty == PONG_BOT_HARD ? "HARD" : "IMPOSSIBLE"));
    const int16_t PlayersBoxY = 173;
    const int16_t DifficultyBoxY = 233;
    const int16_t PowerUpsBoxY = Pong_Game.TwoPlayer ? 233 : 293;
    const int16_t PlayersLabelY = Pong_TextVerticalPosition(&OpenSans20, "PLAYERS", PlayersBoxY, 42U);
    const int16_t PlayersValueY = Pong_TextVerticalPosition(&OpenSans20, Pong_Game.TwoPlayer ? "TWO" : "ONE", PlayersBoxY, 42U);
    const int16_t DifficultyLabelY = Pong_TextVerticalPosition(&OpenSans20, "BOT", DifficultyBoxY, 42U);
    const int16_t DifficultyValueY = Pong_TextVerticalPosition(&OpenSans20, Difficulty, DifficultyBoxY, 42U);
    const int16_t PowerUpsLabelY = Pong_TextVerticalPosition(&OpenSans20, "POWERUPS", PowerUpsBoxY, 42U);
    const int16_t PowerUpsValueY = Pong_TextVerticalPosition(&OpenSans20, Pong_Game.PowerUpsEnabled ? "ON" : "OFF", PowerUpsBoxY, 42U);
    const char *SecondaryLabel = "SECONDARY";
    const char *SecondaryAction = ": CHANGE";
    const char *PrimaryLabel = "PRIMARY";
    const char *PrimaryAction = ": PLAY";
    const int16_t SecondaryHintX = 202;
    const int16_t PrimaryHintX = (int16_t)(598 - Pong_TextWidth(&OpenSans20, PrimaryLabel) - Pong_TextWidth(&OpenSans20, PrimaryAction));
    Render_Clear(Target, COLOUR_BACKGROUND);
    Pong_DrawDotMatrix(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    Pong_DrawRect(Target, 144, 54, RENDER_WIDTH - 288U, 372U, COLOUR_SHADOW);
    Pong_DrawRect(Target, 146, 56, RENDER_WIDTH - 292U, 368U, COLOUR_BACKGROUND);
    Pong_DrawRect(Target, 146, 56, RENDER_WIDTH - 292U, 3U, COLOUR_PANEL_EDGE);
    Pong_DrawCenteredText(Target, &OpenSans36, "PONG", (int16_t)RENDER_WIDTH / 2, 82, COLOUR_TEXT);
    Pong_DrawCenteredText(Target, &OpenSans20, "SETUP", (int16_t)RENDER_WIDTH / 2, 126, COLOUR_MUTED_TEXT);
    if(Pong_Game.MenuItem == PONG_MENU_PLAYERS) { Pong_DrawRect(Target, 172, PlayersBoxY, 456U, 42U, COLOUR_PANEL); Pong_DrawRect(Target, 172, PlayersBoxY, 4U, 42U, COLOUR_BALL); }
    Render_DrawText(Target, &OpenSans20, "PLAYERS", 202, PlayersLabelY, Pong_Game.MenuItem == PONG_MENU_PLAYERS ? COLOUR_TEXT : COLOUR_MUTED_TEXT);
    Pong_DrawRightAlignedText(Target, &OpenSans20, Pong_Game.TwoPlayer ? "TWO" : "ONE", 598, PlayersValueY, Pong_Game.MenuItem == PONG_MENU_PLAYERS ? COLOUR_BALL : COLOUR_TEXT);
    if(!Pong_Game.TwoPlayer)
    {
        if(Pong_Game.MenuItem == PONG_MENU_DIFFICULTY) { Pong_DrawRect(Target, 172, DifficultyBoxY, 456U, 42U, COLOUR_PANEL); Pong_DrawRect(Target, 172, DifficultyBoxY, 4U, 42U, COLOUR_BALL); }
        Render_DrawText(Target, &OpenSans20, "BOT", 202, DifficultyLabelY, Pong_Game.MenuItem == PONG_MENU_DIFFICULTY ? COLOUR_TEXT : COLOUR_MUTED_TEXT);
        Pong_DrawRightAlignedText(Target, &OpenSans20, Difficulty, 598, DifficultyValueY, Pong_Game.MenuItem == PONG_MENU_DIFFICULTY ? COLOUR_BALL : COLOUR_TEXT);
    }
    if(Pong_Game.MenuItem == PONG_MENU_POWER_UPS) { Pong_DrawRect(Target, 172, PowerUpsBoxY, 456U, 42U, COLOUR_PANEL); Pong_DrawRect(Target, 172, PowerUpsBoxY, 4U, 42U, COLOUR_BALL); }
    Render_DrawText(Target, &OpenSans20, "POWERUPS", 202, PowerUpsLabelY, Pong_Game.MenuItem == PONG_MENU_POWER_UPS ? COLOUR_TEXT : COLOUR_MUTED_TEXT);
    Pong_DrawRightAlignedText(Target, &OpenSans20, Pong_Game.PowerUpsEnabled ? "ON" : "OFF", 598, PowerUpsValueY, Pong_Game.MenuItem == PONG_MENU_POWER_UPS ? COLOUR_BALL : COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, SecondaryLabel, SecondaryHintX, 356, COLOUR_LEFT_PADDLE);
    Render_DrawText(Target, &OpenSans20, SecondaryAction, (int16_t)(SecondaryHintX + Pong_TextWidth(&OpenSans20, SecondaryLabel)), 356, COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, PrimaryLabel, PrimaryHintX, 356, COLOUR_RIGHT_PADDLE);
    Render_DrawText(Target, &OpenSans20, PrimaryAction, (int16_t)(PrimaryHintX + Pong_TextWidth(&OpenSans20, PrimaryLabel)), 356, COLOUR_TEXT);
}

static void Pong_DrawGameOverMenu(Render_TargetTypeDef *Target)
{
    const char *SecondaryLabel = "SECONDARY";
    const char *SecondaryAction = ": SETTINGS";
    const char *PrimaryLabel = "PRIMARY";
    const char *PrimaryAction = ": PLAY AGAIN";
    const int16_t SecondaryX = (int16_t)(((int16_t)RENDER_WIDTH / 2) - ((Pong_TextWidth(&OpenSans20, SecondaryLabel) + Pong_TextWidth(&OpenSans20, SecondaryAction)) / 2));
    const int16_t PrimaryX = (int16_t)(((int16_t)RENDER_WIDTH / 2) - ((Pong_TextWidth(&OpenSans20, PrimaryLabel) + Pong_TextWidth(&OpenSans20, PrimaryAction)) / 2));
    Render_Clear(Target, COLOUR_BACKGROUND);
    Pong_DrawDotMatrix(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    Pong_DrawRect(Target, 144, 84, RENDER_WIDTH - 288U, 312U, COLOUR_SHADOW);
    Pong_DrawRect(Target, 146, 86, RENDER_WIDTH - 292U, 308U, COLOUR_BACKGROUND);
    Pong_DrawRect(Target, 146, 86, RENDER_WIDTH - 292U, 3U, COLOUR_PANEL_EDGE);
    Pong_DrawCenteredText(Target, &OpenSans36, Pong_Game.LastWinner == 1U ? "BLUE WINS" : "RED WINS", (int16_t)RENDER_WIDTH / 2, 112, Pong_Game.LastWinner == 1U ? COLOUR_LEFT_PADDLE : COLOUR_RIGHT_PADDLE);
    Pong_DrawCenteredText(Target, &OpenSans20, "FIRST TO 7 POINTS", (int16_t)RENDER_WIDTH / 2, 156, COLOUR_MUTED_TEXT);
    Render_DrawText(Target, &OpenSans20, PrimaryLabel, PrimaryX, 278, COLOUR_RIGHT_PADDLE);
    Render_DrawText(Target, &OpenSans20, PrimaryAction, (int16_t)(PrimaryX + Pong_TextWidth(&OpenSans20, PrimaryLabel)), 278, COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, SecondaryLabel, SecondaryX, 322, COLOUR_LEFT_PADDLE);
    Render_DrawText(Target, &OpenSans20, SecondaryAction, (int16_t)(SecondaryX + Pong_TextWidth(&OpenSans20, SecondaryLabel)), 322, COLOUR_TEXT);
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
    if(Secondary && !Pong_Input.SecondaryDown && (Pong_Game.Screen == PONG_SCREEN_GAME_OVER)) { Pong_Game.Screen = PONG_SCREEN_MENU; Pong_Input.RightSliderArmed = false; }
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
    const int16_t TopY = PONG_SPLASH_Y;
    const int16_t BottomY = PONG_SPLASH_Y + PONG_SPLASH_HEIGHT;
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
    Pong_DrawDotMatrix(Target, PONG_SPLASH_X, PONG_SPLASH_Y, PONG_SPLASH_WIDTH, PONG_SPLASH_HEIGHT);
    Pong_DrawCenteredText(Target, &OpenSans36, "PONG", PONG_SPLASH_X + ((int16_t)PONG_SPLASH_WIDTH / 2), PONG_SPLASH_TITLE_Y, COLOUR_TEXT);
    Pong_DrawRect(Target, PONG_SPLASH_X + 26, Pong_SplashLeftY, PADDLE_WIDTH, PADDLE_NORMAL_HEIGHT, COLOUR_LEFT_PADDLE);
    Pong_DrawRect(Target, PONG_SPLASH_X + PONG_SPLASH_WIDTH - 26 - PADDLE_WIDTH, Pong_SplashRightY, PADDLE_WIDTH, PADDLE_NORMAL_HEIGHT, COLOUR_RIGHT_PADDLE);
    Pong_DrawDisc(Target, (int16_t)(Pong_SplashBallX / BALL_FIXED_SCALE), (int16_t)(Pong_SplashBallY / BALL_FIXED_SCALE), BALL_SIZE, COLOUR_BALL);
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
    Pong_UpdateTimedEffect(&Pong_Game.LeftExpandMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.RightExpandMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.LeftShrinkMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.RightShrinkMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.LeftInvertMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.RightInvertMilliseconds, DeltaTimeMilliseconds);
    if(Pong_Game.LeftFreezeMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftFreezeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftFreezeMilliseconds = 0U;
    if(Pong_Game.RightFreezeMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightFreezeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightFreezeMilliseconds = 0U;
    if(Pong_Game.LeftPaddleFlashMilliseconds > DeltaTimeMilliseconds) Pong_Game.LeftPaddleFlashMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.LeftPaddleFlashMilliseconds = 0U;
    if(Pong_Game.RightPaddleFlashMilliseconds > DeltaTimeMilliseconds) Pong_Game.RightPaddleFlashMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.RightPaddleFlashMilliseconds = 0U;
    if(Pong_Game.BotMistakeMilliseconds > DeltaTimeMilliseconds) Pong_Game.BotMistakeMilliseconds -= DeltaTimeMilliseconds; else Pong_Game.BotMistakeMilliseconds = 0U;
    Pong_UpdateTimedEffect(&Pong_Game.LeftShieldMilliseconds, DeltaTimeMilliseconds);
    Pong_UpdateTimedEffect(&Pong_Game.RightShieldMilliseconds, DeltaTimeMilliseconds);
    if(Pong_Game.Screen == PONG_SCREEN_PLAYING && Pong_Game.PowerUpsEnabled)
    {
        if(Pong_Game.PowerUp.Type == PONG_POWER_UP_NONE)
        {
            if(Pong_Game.PowerUp.SpawnTimerMilliseconds > DeltaTimeMilliseconds) Pong_Game.PowerUp.SpawnTimerMilliseconds -= DeltaTimeMilliseconds;
            else { Pong_Game.PowerUp.Type = (Pong_PowerUpTypeDef)((Pong_Random() % 5U) + 1U); Pong_Game.PowerUp.X = (int16_t)RENDER_WIDTH / 2 - 12; Pong_Game.PowerUp.Y = 130 + (int16_t)(Pong_Random() % 210U); Pong_Game.PowerUp.LifetimeMilliseconds = POWER_UP_LIFETIME_MILLISECONDS; Pong_Game.PowerUp.GrowElapsedMilliseconds = 0U; }
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
    else { Pong_DrawCourt(&Target); Pong_DrawEffectDurationBar(&Target); }
    (void)Display_PresentFrame(Frame);
}

void Pong_Pause(void) { Pong_Paused = true; }
void Pong_Resume(void) { Pong_Paused = false; }
void Pong_Shutdown(void) { Pong_Initialized = false; Pong_Paused = false; Pong_PendingDeltaTimeMilliseconds = 0U; }