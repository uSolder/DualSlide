/**
 * @file tanks.c
 * @brief DualSlide lifecycle, input sampling and screen-state controller.
 */

#include "tanks_internal.h"
#include "tanks_audio.h"

#include "app_manager.h"
#include "display.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define TRACK_DEAD_ZONE        (2200)
#define TRACK_FILTER_MS        (85)
#define WAVE_INTRO_DURATION_MS (2400U)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

Tanks_GameTypeDef Tanks_Game;

/* A proving ground at night: dark concrete, steel barriers, amber hazard markings. */
const Display_ColourTypeDef Tanks_Palette[TANKS_COLOUR_COUNT] = {
    0x000E141CU, /* outside */
    0x0018222EU, /* outside light */
    0x00080C12U, /* outside dark */
    0x001B2633U, /* floor */
    0x00233243U, /* floor light */
    0x00131B25U, /* floor dark */
    0x00253548U, /* grout: the tactical grid */
    0x00414D5EU, /* wall: steel */
    0x00657488U, /* wall light */
    0x00262E39U, /* wall shadow */
    0x0005070CU, /* pit */
    0x005A4212U, /* pit edge: dim amber */
    0x0026C9BFU, /* player: teal */
    0x0094F6EEU, /* player light */
    0x000D5F5BU, /* player dark */
    0x00D07A2CU, /* enemy: orange */
    0x00F4B46CU, /* enemy light */
    0x00603211U, /* enemy dark */
    0x000A0E13U, /* track */
    0x00394352U, /* track light */
    0x003A7BEAU, /* HQ */
    0x0079A8FFU, /* HQ light */
    0x00163E8FU, /* HQ dark */
    0x00FFF2A8U, /* bullet */
    0x00A46BFFU, /* player laser: violet */
    0x00FF776FU, /* enemy laser */
    0x00F06832U, /* fire */
    0x00FFD166U, /* fire light */
    0x00686D76U, /* smoke */
    0x0031353DU, /* smoke dark */
    0x00E2B84FU, /* mine */
    0x00F8FAFFU, /* text */
    0x00A9B6CBU, /* muted */
    0x000C121BU, /* panel */
    0x00314A6EU, /* panel edge */
    0x00FF4D5AU, /* danger */
    0x00FFBD4AU, /* warning */
    0x0057E389U, /* success */
    0x0006090EU, /* shadow */
    0x00FFFFFFU, /* white */
    0x00000000U, /* black */
    0x00191B20U, /* wreck */
    0x002E2423U, /* scorch */
    0x00B08A2AU, /* hazard: barrier markings */
    0x003E5A6EU, /* trail: fresh tread marks */
    0x00263646U  /* trail old: fading tread marks */
};

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static int16_t Tanks_SliderToTrack(int32_t Value)
{
    int32_t Offset = Tanks_Clamp32(Value, 0, CONTROLS_SLIDER_RAW_MAXIMUM) - 32768;
    if((Offset >= -TRACK_DEAD_ZONE) && (Offset <= TRACK_DEAD_ZONE))
    {
        return 0;
    }
    if(Offset > 0)
    {
        return (int16_t)(((Offset - TRACK_DEAD_ZONE) * 1000) / (32767 - TRACK_DEAD_ZONE));
    }
    return (int16_t)(((Offset + TRACK_DEAD_ZONE) * 1000) / (32768 - TRACK_DEAD_ZONE));
}

static int16_t Tanks_FilterTrack(int16_t Current, int16_t Target, uint32_t DeltaMilliseconds)
{
    const int32_t Error = (int32_t)Target - Current;
    int32_t Step = (Error * (int32_t)DeltaMilliseconds) / TRACK_FILTER_MS;
    if((Step == 0) && (Error != 0))
    {
        Step = Error > 0 ? 1 : -1;
    }
    if((Error > 0) && (Step > Error))
    {
        Step = Error;
    }
    if((Error < 0) && (Step < Error))
    {
        Step = Error;
    }
    return (int16_t)Tanks_Clamp32((int32_t)Current + Step, -1000, 1000);
}

static void Tanks_ReadButton(Tanks_ButtonTypeDef *Button, Controls_ButtonTypeDef Control)
{
    Button->Down = Controls_IsDown(Control);
    Button->Pressed = Controls_WasPressed(Control);
    Button->Released = Controls_WasReleased(Control);
}

static void Tanks_ReadInput(uint32_t DeltaMilliseconds)
{
    Tanks_Game.Input.LeftTarget = Tanks_SliderToTrack(Controls_SliderRaw(CONTROLS_LEFT_SLIDER));
    Tanks_Game.Input.RightTarget = Tanks_SliderToTrack(Controls_SliderRaw(CONTROLS_RIGHT_SLIDER));
    Tanks_Game.Input.LeftTrack = Tanks_FilterTrack(Tanks_Game.Input.LeftTrack, Tanks_Game.Input.LeftTarget, DeltaMilliseconds);
    Tanks_Game.Input.RightTrack = Tanks_FilterTrack(Tanks_Game.Input.RightTrack, Tanks_Game.Input.RightTarget, DeltaMilliseconds);
    Tanks_ReadButton(&Tanks_Game.Input.Primary, CONTROLS_PRIMARY);
    Tanks_ReadButton(&Tanks_Game.Input.Secondary, CONTROLS_SECONDARY);
}

static void Tanks_HandleScreenInput(void)
{
    if(Tanks_Game.Screen == TANKS_SCREEN_TITLE)
    {
        if(Tanks_Game.Input.Primary.Released)
        {
            Tanks_StartNewGame();
        }
        return;
    }
    if(Tanks_Game.Screen == TANKS_SCREEN_GAME_OVER)
    {
        if(Tanks_Game.Record.Entering)
        {
            Tanks_UpdateRecordEntry();
            return;
        }
        if(Tanks_Game.Input.Primary.Released)
        {
            Tanks_StartNewGame();
        }
        else if(Tanks_Game.Input.Secondary.Released)
        {
            Tanks_Game.Screen = TANKS_SCREEN_TITLE;
            Tanks_Game.ScreenMilliseconds = 0U;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Application functions                                                      */
/* -------------------------------------------------------------------------- */

static bool Tanks_Init(void)
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
    Tanks_Game.Input.Primary = (Tanks_ButtonTypeDef){ 0 };
    Tanks_Game.Input.Secondary = Tanks_Game.Input.Primary;
    Tanks_Game.Message[0] = '\0';
    Tanks_Game.Paused = false;
    Tanks_Game.Initialized = true;
    Tanks_Game.Demo = false;
    Tanks_LoadRecord();
    Tanks_ResetBattlefield();
    TanksAudio_Start();
    return true;
}

static void Tanks_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!Tanks_Game.Initialized || Tanks_Game.Paused)
    {
        return;
    }
    if(DeltaTimeMilliseconds > TANKS_MAXIMUM_DELTA_MS)
    {
        DeltaTimeMilliseconds = TANKS_MAXIMUM_DELTA_MS;
    }
    Tanks_ReadInput(DeltaTimeMilliseconds);
    Tanks_HandleScreenInput();
    Tanks_Game.PendingDeltaMilliseconds += DeltaTimeMilliseconds;
    if(Tanks_Game.PendingDeltaMilliseconds > TANKS_MAXIMUM_DELTA_MS)
    {
        Tanks_Game.PendingDeltaMilliseconds = TANKS_MAXIMUM_DELTA_MS;
    }
}

static bool Tanks_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }
    /*
     * A live battle between two tanks, run a frame at a time while the preview
     * shows. It shares the game's state, so it never runs while the game does.
     */
    if(!Tanks_Game.Initialized)
    {
        if(!Tanks_Game.Demo)
        {
            Tanks_StartDemo();
        }
        Tanks_UpdateDemo(16U);
        Tanks_UpdateDemo(17U);
    }
    Tanks_DrawSplashArtwork(Target);
    return true;
}

static void Tanks_Render(Render_TargetTypeDef *Target)
{
    uint32_t DeltaMilliseconds;
    if(!Tanks_Game.Initialized || Tanks_Game.Paused)
    {
        return;
    }
    DeltaMilliseconds = Tanks_Game.PendingDeltaMilliseconds;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
    Tanks_Game.ScreenMilliseconds += DeltaMilliseconds;

    if(Tanks_Game.Screen == TANKS_SCREEN_WAVE_INTRO)
    {
        if(Tanks_Game.ScreenMilliseconds >= WAVE_INTRO_DURATION_MS)
        {
            Tanks_Game.Screen = TANKS_SCREEN_PLAYING;
            Tanks_Game.ScreenMilliseconds = 0U;
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
    TanksAudio_Update(DeltaMilliseconds);

    if(Tanks_Game.Screen == TANKS_SCREEN_TITLE)
    {
        Tanks_DrawTitle(Target);
    }
    else if(Tanks_Game.Screen == TANKS_SCREEN_GAME_OVER)
    {
        Tanks_DrawEndScreen(Target);
    }
    else
    {
        Tanks_DrawGame(Target);
    }
}

static void Tanks_Pause(void)
{
    TanksAudio_Stop();
    Tanks_Game.Paused = true;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
}

static void Tanks_Resume(void)
{
    TanksAudio_Start();
    Tanks_Game.Paused = false;
}

static void Tanks_Shutdown(void)
{
    TanksAudio_Stop();
    Tanks_SaveUnclaimedRecord();
    Tanks_Game.Initialized = false;
    Tanks_Game.Paused = false;
    Tanks_Game.PendingDeltaMilliseconds = 0U;
}

/* -------------------------------------------------------------------------- */
/* Application                                                                */
/* -------------------------------------------------------------------------- */

const AppManager_AppTypeDef Tanks_App =
{
    .Init = Tanks_Init,
    .Update = Tanks_Update,
    .Render = Tanks_Render,
    .DrawSplashScreen = Tanks_DrawSplashScreen,
    .Pause = Tanks_Pause,
    .Resume = Tanks_Resume,
    .Shutdown = Tanks_Shutdown,
    APP_PALETTE(Tanks_Palette)
};
