/**
 * @file audio.c
 * @brief SDL3 implementation of the platform-neutral audio contract.
 */

#include "audio.h"

#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define WINDOWS_AUDIO_CALLBACK_BUFFER_FRAME_COUNT    (1024U)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static SDL_AudioStream *Windows_AudioStream = NULL;
static Audio_ConfigTypeDef Windows_AudioConfig;
static Audio_SampleTypeDef *Windows_AudioCallbackBuffer = NULL;
static bool Windows_AudioInitialized = false;
static bool Windows_AudioRunning = false;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Supply additional PCM data requested by SDL.
 */
static void SDLCALL Windows_AudioStreamCallback(void *UserData, SDL_AudioStream *Stream, int AdditionalAmount, int TotalAmount)
{
    uint32_t RemainingFrameCount;
    const uint32_t BytesPerFrame = (uint32_t)sizeof(Audio_SampleTypeDef) * Windows_AudioConfig.ChannelCount;

    (void)UserData;
    (void)TotalAmount;

    if((Stream == NULL) || (AdditionalAmount <= 0) || (BytesPerFrame == 0U))
    {
        return;
    }

    RemainingFrameCount = ((uint32_t)AdditionalAmount + BytesPerFrame - 1U) / BytesPerFrame;

    while(RemainingFrameCount > 0U)
    {
        const uint32_t FrameCount = (RemainingFrameCount > WINDOWS_AUDIO_CALLBACK_BUFFER_FRAME_COUNT) ? WINDOWS_AUDIO_CALLBACK_BUFFER_FRAME_COUNT : RemainingFrameCount;
        const uint32_t SampleCount = FrameCount * Windows_AudioConfig.ChannelCount;
        const int ByteCount = (int)(SampleCount * sizeof(Audio_SampleTypeDef));

        Windows_AudioConfig.FillCallback(Windows_AudioCallbackBuffer, FrameCount, Windows_AudioConfig.CallbackContext);

        if(!SDL_PutAudioStreamData(Stream, Windows_AudioCallbackBuffer, ByteCount))
        {
            return;
        }

        RemainingFrameCount -= FrameCount;
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Audio_Init(const Audio_ConfigTypeDef *Config)
{
    SDL_AudioSpec AudioSpec;
    size_t CallbackBufferSampleCount;

    if(Windows_AudioInitialized)
    {
        return false;
    }

    if((Config == NULL) || (Config->SampleRateHz == 0U) || (Config->ChannelCount == 0U) || (Config->FillCallback == NULL))
    {
        return false;
    }

    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        return false;
    }

    CallbackBufferSampleCount = WINDOWS_AUDIO_CALLBACK_BUFFER_FRAME_COUNT * Config->ChannelCount;
    Windows_AudioCallbackBuffer = SDL_malloc(CallbackBufferSampleCount * sizeof(Audio_SampleTypeDef));

    if(Windows_AudioCallbackBuffer == NULL)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    Windows_AudioConfig = *Config;

    SDL_zero(AudioSpec);

    AudioSpec.freq = (int)Config->SampleRateHz;
    AudioSpec.format = SDL_AUDIO_S16;
    AudioSpec.channels = Config->ChannelCount;

    Windows_AudioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &AudioSpec,
        Windows_AudioStreamCallback,
        NULL
    );

    if(Windows_AudioStream == NULL)
    {
        SDL_free(Windows_AudioCallbackBuffer);
        Windows_AudioCallbackBuffer = NULL;

        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    Windows_AudioRunning = false;
    Windows_AudioInitialized = true;

    return true;
}

bool Audio_Start(void)
{
    if(!Windows_AudioInitialized || (Windows_AudioStream == NULL))
    {
        return false;
    }

    if(Windows_AudioRunning)
    {
        return true;
    }

    if(!SDL_ResumeAudioStreamDevice(Windows_AudioStream))
    {
        return false;
    }

    Windows_AudioRunning = true;

    return true;
}

void Audio_Stop(void)
{
    if(Windows_AudioStream != NULL)
    {
        SDL_DestroyAudioStream(Windows_AudioStream);
        Windows_AudioStream = NULL;
    }

    if(Windows_AudioCallbackBuffer != NULL)
    {
        SDL_free(Windows_AudioCallbackBuffer);
        Windows_AudioCallbackBuffer = NULL;
    }

    Windows_AudioConfig.SampleRateHz = 0U;
    Windows_AudioConfig.ChannelCount = 0U;
    Windows_AudioConfig.FillCallback = NULL;
    Windows_AudioConfig.CallbackContext = NULL;

    Windows_AudioRunning = false;

    if(Windows_AudioInitialized)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        Windows_AudioInitialized = false;
    }
}

bool Audio_IsRunning(void)
{
    return Windows_AudioRunning;
}
