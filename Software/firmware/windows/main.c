/**
 * @file main.c
 * @brief Windows simulator entry point.
 */

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_SAMPLE_RATE_HZ    (48000U)
#define AUDIO_CHANNEL_COUNT     (1U)
#define FRAME_INTERVAL_MS       (16U)

int main(void)
{
    const Audio_ConfigTypeDef AudioConfig =
    {
        .SampleRateHz = AUDIO_SAMPLE_RATE_HZ,
        .ChannelCount = AUDIO_CHANNEL_COUNT,
        .FillCallback = AppManager_FillAudioBuffer,
        .CallbackContext = NULL
    };

    uint64_t PreviousFrameTimeMilliseconds;
    bool Running = true;

    if(!Display_Init())
    {
        return 1;
    }

    if(!Input_Init())
    {
        return 1;
    }

    if(!AppManager_Init())
    {
        return 1;
    }

    if(!Audio_Init(&AudioConfig))
    {
        AppManager_Shutdown();
        return 1;
    }

    if(!Audio_Start())
    {
        Audio_Stop();
        AppManager_Shutdown();
        return 1;
    }

    PreviousFrameTimeMilliseconds = SDL_GetTicks();

    while(Running)
    {
        SDL_Event Event;
        uint64_t FrameStartTimeMilliseconds;
        uint64_t FrameElapsedMilliseconds;
        uint32_t DeltaTimeMilliseconds;

        FrameStartTimeMilliseconds = SDL_GetTicks();
        DeltaTimeMilliseconds = (uint32_t)(FrameStartTimeMilliseconds - PreviousFrameTimeMilliseconds);
        PreviousFrameTimeMilliseconds = FrameStartTimeMilliseconds;

        SDL_PumpEvents();

        if(SDL_HasEvent(SDL_EVENT_QUIT))
        {
            Running = false;
        }

        if(!Running)
        {
            break;
        }

        AppManager_Update(DeltaTimeMilliseconds);
        AppManager_Render();

        FrameElapsedMilliseconds = SDL_GetTicks() - FrameStartTimeMilliseconds;

        if(FrameElapsedMilliseconds < FRAME_INTERVAL_MS)
        {
            SDL_Delay((uint32_t)(FRAME_INTERVAL_MS - FrameElapsedMilliseconds));
        }
    }

    Audio_Stop();
    AppManager_Shutdown();

    return 0;
}