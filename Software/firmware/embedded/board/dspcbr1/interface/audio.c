/**
 * @file audio.c
 * @brief Stub PCM audio-output backend.
 *
 * This implementation satisfies the platform-neutral audio contract without
 * producing audio. It records the requested configuration and reports normal
 * backend state transitions so applications can run before a real audio
 * backend is available.
 */

#include "audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private state                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    Audio_ConfigTypeDef Config;
    bool Initialized;
    bool Running;
} Audio_StateTypeDef;

static Audio_StateTypeDef Audio_State;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Audio_Init(const Audio_ConfigTypeDef *Config)
{
    if (Config == NULL)
    {
        return false;
    }

    if (Config->SampleRateHz == 0U)
    {
        return false;
    }

    if (Config->ChannelCount == 0U)
    {
        return false;
    }

    if (Config->FillCallback == NULL)
    {
        return false;
    }

    if (Audio_State.Running)
    {
        return false;
    }

    Audio_State.Config = *Config;
    Audio_State.Initialized = true;
    Audio_State.Running = false;

    return true;
}

bool Audio_Start(void)
{
    if (!Audio_State.Initialized)
    {
        return false;
    }

    Audio_State.Running = true;

    return true;
}

void Audio_Stop(void)
{
    Audio_State.Running = false;
    Audio_State.Initialized = false;
}

bool Audio_IsRunning(void)
{
    return Audio_State.Running;
}