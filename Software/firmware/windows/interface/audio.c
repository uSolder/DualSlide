/**
 * @file audio.c
 * @brief SDL3 implementation of the platform-neutral audio contract.
 */

#include "audio.h"

#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdint.h>

#define AUDIO_CALLBACK_BUFFER_FRAME_COUNT    (1024U)

static SDL_AudioStream *Audio_Stream = NULL;
static Audio_ConfigTypeDef Audio_Config;
static Audio_SampleTypeDef *Audio_CallbackBuffer = NULL;
static bool Audio_Initialized = false;
static bool Audio_Running = false;

/**
 * @brief Supply additional PCM data requested by SDL.
 */
static void SDLCALL Audio_StreamCallback(void *UserData, SDL_AudioStream *Stream, int AdditionalAmount, int TotalAmount)
{
    uint32_t RemainingFrameCount;
    const uint32_t BytesPerFrame = (uint32_t)sizeof(Audio_SampleTypeDef) * Audio_Config.ChannelCount;

    (void)UserData;
    (void)TotalAmount;

    if((Stream == NULL) || (AdditionalAmount <= 0) || (BytesPerFrame == 0U))
    {
        return;
    }

    RemainingFrameCount = ((uint32_t)AdditionalAmount + BytesPerFrame - 1U) / BytesPerFrame;

    while(RemainingFrameCount > 0U)
    {
        const uint32_t FrameCount = (RemainingFrameCount > AUDIO_CALLBACK_BUFFER_FRAME_COUNT) ? AUDIO_CALLBACK_BUFFER_FRAME_COUNT : RemainingFrameCount;
        const uint32_t SampleCount = FrameCount * Audio_Config.ChannelCount;
        const int ByteCount = (int)(SampleCount * sizeof(Audio_SampleTypeDef));

        Audio_Config.FillCallback(Audio_CallbackBuffer, FrameCount, Audio_Config.CallbackContext);

        if(!SDL_PutAudioStreamData(Stream, Audio_CallbackBuffer, ByteCount))
        {
            return;
        }

        RemainingFrameCount -= FrameCount;
    }
}

bool Audio_Init(const Audio_ConfigTypeDef *Config)
{
    SDL_AudioSpec AudioSpec;
    const size_t CallbackBufferSampleCount = AUDIO_CALLBACK_BUFFER_FRAME_COUNT * Config->ChannelCount;

    if(Audio_Initialized)
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

    Audio_CallbackBuffer = SDL_malloc(CallbackBufferSampleCount * sizeof(Audio_SampleTypeDef));

    if(Audio_CallbackBuffer == NULL)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    Audio_Config = *Config;

    SDL_zero(AudioSpec);

    AudioSpec.freq = (int)Config->SampleRateHz;
    AudioSpec.format = SDL_AUDIO_S16;
    AudioSpec.channels = Config->ChannelCount;

    Audio_Stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &AudioSpec,
        Audio_StreamCallback,
        NULL
    );

    if(Audio_Stream == NULL)
    {
        SDL_free(Audio_CallbackBuffer);
        Audio_CallbackBuffer = NULL;

        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    Audio_Running = false;
    Audio_Initialized = true;

    return true;
}

bool Audio_Start(void)
{
    if(!Audio_Initialized || (Audio_Stream == NULL))
    {
        return false;
    }

    if(Audio_Running)
    {
        return true;
    }

    if(!SDL_ResumeAudioStreamDevice(Audio_Stream))
    {
        return false;
    }

    Audio_Running = true;

    return true;
}

void Audio_Stop(void)
{
    if(Audio_Stream != NULL)
    {
        SDL_DestroyAudioStream(Audio_Stream);
        Audio_Stream = NULL;
    }

    if(Audio_CallbackBuffer != NULL)
    {
        SDL_free(Audio_CallbackBuffer);
        Audio_CallbackBuffer = NULL;
    }

    Audio_Config.SampleRateHz = 0U;
    Audio_Config.ChannelCount = 0U;
    Audio_Config.FillCallback = NULL;
    Audio_Config.CallbackContext = NULL;

    Audio_Running = false;

    if(Audio_Initialized)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        Audio_Initialized = false;
    }
}

bool Audio_IsRunning(void)
{
    return Audio_Running;
}