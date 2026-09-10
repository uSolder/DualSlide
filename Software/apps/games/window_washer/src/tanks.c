/**
 * @file tanks.c
 * @brief DualSlide lifecycle, input sampling and screen-state controller.
 */

#include "tanks_internal.h"

#include "app_manager.h"
#include "display.h"
#include "input.h"

#include <stddef.h>

#define TRACK_DEAD_ZONE              (2200)
#define TRACK_FILTER_MS              (85)
#define WAVE_INTRO_DURATION_MS       (2400U)

Tanks_GameTypeDef Tanks_Game;

const Display_ColourTypeDef Tanks_Palette[TANKS_COLOUR_COUNT] =
{
    0x0060472EU, /* outside */
    0x00826745U, /* outside light */
    0x003A291BU, /* outside dark */
    0x00D7B875U, /* floor */
    0x00E8CF98U, /* floor light */
    0x00B98E52U, /* floor dark */
    0x00C69A5CU, /* grout */
    0x00B98542U, /* wall */
    0x00E7C878U, /* wall light */
    0x006A421FU, /* wall shadow */
    0x0005070CU, /* pit */
    0x00212836U, /* pit edge */
    0x00D83D3DU, /* player */
    0x00FF7A70U, /* player light */
    0x00891F25U, /* player dark */
    0x006B4AA5U, /* enemy */
    0x00A989D4U, /* enemy light */
    0x003B255FU, /* enemy dark */
    0x00161A21U, /* track */
    0x00424A57U, /* track light */
    0x001E68DDU, /* HQ */
    0x0068A7FFU, /* HQ light */
    0x000F3C8FU, /* HQ dark */
    0x00FFF2A8U, /* bullet */
    0x00FF9A8FU, /* player laser */
    0x00FF776FU, /* enemy laser */
    0x00F06832U, /* fire */
    0x00FFD166U, /* fire light */
    0x00686D76U, /* smoke */
    0x0031353DU, /* smoke dark */
    0x00E2B84FU, /* mine */
    0x00F8FAFFU, /* text */
    0x00B8C3D8U, /* muted */
    0x00101A2DU, /* panel */
    0x003C5D91U, /* panel edge */
    0x00FF4D5AU, /* danger */
    0x00FFBD4AU, /* warning */
    0x0057E389U, /* success */
    0x00101620U, /* shadow */
    0x00FFFFFFU, /* white */
    0x00000000U, /* black */
    0x00191B20U, /* wreck */
    0x003B2B2CU  /* scorch */
};

static uint32_t Tanks_SplashElapsedMilliseconds;

static int16_t Tanks_SliderToTrack(int32_t Value)
{
    int32_t Offset = Tanks_Clamp32(Value, 0, 65535) - 32768;
    if((Offset >= -TRACK_DEAD_ZONE) && (Offset <= TRACK_DEAD_ZONE)) return 0;
    if(Offset > 0) return (int16_t)(((Offset - TRACK_DEAD_ZONE) * 1000) / (32767 - TRACK_DEAD_ZONE));
    return (int16_t)(((Offset + TRACK_DEAD_ZONE) * 1000) / (32768 - TRACK_DEAD_ZONE));
}

static int16_t Tanks_FilterTrack(int16_t Current, int16_t Target, uint32_t DeltaMilliseconds)
{
    const int32_t Error = (int32_t)Target - Current;
    int32_t Step = (Error * (int32_t)DeltaMilliseconds) / TRACK_FILTER_MS;
    if((Step == 0) && (Error != 0)) Step = Error > 0 ? 1 : -1;
    if((Error > 0) && (Step > Error)) Step = Error;
    if((Error < 0) && (Step < Error)) Step = Error;
    return (int16_t)Tanks_Clamp32((int32_t)Current + Step, -1000, 1000);
}

static void Tanks_UpdateButton(Tanks_ButtonTypeDef *Button, bool Down)
{
    Button->PreviousDown = Button->Down;
    Button->Down = Down;
    Button->Pressed = Button->Down && !Button->PreviousDown;
    Button->Released = !Button->Down && Button->PreviousDown;
}

static void Tanks_ReadInput(uint32_t DeltaMilliseconds)
{
    int32_t Value;
    bool Primary = false;
    bool Secondary = false;
    if(Input_Get_Value(TANKS_INPUT_LEFT_TRACK, &Value)) Tanks_Game.Input.LeftTarget = Tanks_SliderToTrack(Value);
    if(Input_Get_Value(TANKS_INPUT_RIGHT_TRACK, &Value)) Tanks_Game.Input.RightTarget = Tanks_SliderToTrack(Value);
    if(Input_Get_Value(TANKS_INPUT_PRIMARY, &Value)) Primary = Value != 0;
    if(Input_Get_Value(TANKS_INPUT_SECONDARY, &Value)) Secondary = Value != 0;
    Tanks_Game.Input.LeftTrack = Tanks_FilterTrack(Tanks_Game.Input.LeftTrack, Tanks_Game.Input.LeftTarget, DeltaMilliseconds);
    Tanks_Game.Input.RightTrack = Tanks_FilterTrack(Tanks_Game.Input.RightTrack, Tanks_Game.Input.RightTarget, DeltaMilliseconds);
    Tanks_UpdateButton(&Tanks_Game.Input.Primary, Primary);
    Tanks_UpdateButton(&Tanks_Game.Input.Secondary, Secondary);
}

static void Tanks_HandleScreenInput(void)
{
    if(Tanks_Game.Screen == TANKS_SCREEN_TITLE)
    {
        if(Tanks_Game.Input.Primary.Released) Tanks_StartNewGame();
        return;
    }
    if((Tanks_Game.Screen == TANKS_SCREEN_GAME_OVER) || (Tanks_Game.Screen == TANKS_SCREEN_VICTORY))
    {
        if(Tanks_Game.Input.Primary.Released) Tanks_StartNewGame();
        else if(Tanks_Game.Input.Secondary.Released)
        {
            Tanks_Game.Screen = TANKS_SCREEN_TITLE;
            Tanks_Game.ScreenMilliseconds = 0U;
        }
    }
}

bool Tanks_Init(void)
{
    Tanks_Game.RandomState = 0x7A6B5C4DU;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
    Tanks_Game.RunMilliseconds = 0U;
    Tanks_Game.ScreenMilliseconds = 0U;
    Tanks_Game.Score = 0U;
    Tanks_Game.HighScore = 0U;
    Tanks_Game.Wave = 1U;
    Tanks_Game.Screen = TANKS_SCREEN_TITLE;
    Tanks_Game.Input.LeftTarget = 0;
    Tanks_Game.Input.RightTarget = 0;
    Tanks_Game.Input.LeftTrack = 0;
    Tanks_Game.Input.RightTrack = 0;
    Tanks_Game.Input.Primary.Down = false;
    Tanks_Game.Input.Primary.PreviousDown = false;
    Tanks_Game.Input.Primary.Pressed = false;
    Tanks_Game.Input.Primary.Released = false;
    Tanks_Game.Input.Secondary = Tanks_Game.Input.Primary;
    Tanks_Game.Message[0] = '\0';
    Tanks_Game.Paused = false;
    Tanks_Game.Initialized = true;
    Tanks_SplashElapsedMilliseconds = 0U;
    Tanks_ResetBattlefield();
    return true;
}

void Tanks_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!Tanks_Game.Initialized || Tanks_Game.Paused) return;
    if(DeltaTimeMilliseconds > TANKS_MAXIMUM_DELTA_MS) DeltaTimeMilliseconds = TANKS_MAXIMUM_DELTA_MS;
    Tanks_ReadInput(DeltaTimeMilliseconds);
    Tanks_HandleScreenInput();
    Tanks_Game.PendingDeltaMilliseconds += DeltaTimeMilliseconds;
    if(Tanks_Game.PendingDeltaMilliseconds > TANKS_MAXIMUM_DELTA_MS) Tanks_Game.PendingDeltaMilliseconds = TANKS_MAXIMUM_DELTA_MS;
}

bool Tanks_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL) return false;
    for(uint16_t Index = 0U; Index < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; Index++) Palette[Index] = 0U;
    for(uint16_t Index = 0U; Index < TANKS_COLOUR_COUNT; Index++) Palette[Index] = Tanks_Palette[Index];
    return true;
}

bool Tanks_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    if((Target == NULL) || (Target->Pixels == NULL)) return false;
    Tanks_SplashElapsedMilliseconds += 33U;
    Tanks_DrawSplashArtwork(Target, Tanks_SplashElapsedMilliseconds);
    return true;
}

void Tanks_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    uint32_t DeltaMilliseconds;
    if(!Tanks_Game.Initialized || Tanks_Game.Paused) return;
    Frame = Display_AcquireFrame();
    if(Frame == NULL) return;
    Target.Pixels = Frame->Pixels;
    Target.Width = Frame->Width;
    Target.Height = Frame->Height;
    Target.StridePixels = Frame->StridePixels;
    DeltaMilliseconds = Tanks_Game.PendingDeltaMilliseconds;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
    Tanks_Game.ScreenMilliseconds += DeltaMilliseconds;

    if(Tanks_Game.Screen == TANKS_SCREEN_WAVE_INTRO)
    {
        if(Tanks_Game.ScreenMilliseconds >= WAVE_INTRO_DURATION_MS)
        {
            Tanks_Game.Screen = TANKS_SCREEN_PLAYING;
            Tanks_Game.ScreenMilliseconds = 0U;
            Tanks_ShowMessage("CLEAR THE ARENA", 900U);
        }
    }
    else if(Tanks_Game.Screen == TANKS_SCREEN_PLAYING)
    {
        uint32_t Remaining = DeltaMilliseconds;
        while(Remaining > 0U)
        {
            const uint32_t Step = Remaining > 16U ? 16U : Remaining;
            Tanks_Simulate(Step);
            Remaining -= Step;
        }
    }

    if(Tanks_Game.Screen == TANKS_SCREEN_TITLE) Tanks_DrawTitle(&Target);
    else if(Tanks_Game.Screen == TANKS_SCREEN_GAME_OVER) Tanks_DrawEndScreen(&Target, false);
    else if(Tanks_Game.Screen == TANKS_SCREEN_VICTORY) Tanks_DrawEndScreen(&Target, true);
    else Tanks_DrawGame(&Target);
    (void)Display_PresentFrame(Frame);
}

void Tanks_Pause(void)
{
    Tanks_Game.Paused = true;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
}

void Tanks_Resume(void)
{
    Tanks_Game.Paused = false;
    Tanks_Game.Input.Primary.Down = false;
    Tanks_Game.Input.Secondary.Down = false;
}

void Tanks_Shutdown(void)
{
    Tanks_Game.Initialized = false;
    Tanks_Game.Paused = false;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
}
