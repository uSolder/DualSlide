/**
 * @file mixer.c
 * @brief Eight-channel audio mixer for applications and the system UI.
 *
 * Control functions run in the main loop and the mix runs in the audio output
 * context (an interrupt on embedded targets, an audio thread on Windows). The
 * two never share channel state: the main loop posts requests through a
 * single-producer, single-consumer queue, and the audio context applies them
 * before mixing each buffer.
 */

#include "mixer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Request queue capacity; must be a power of two. */
#define MIXER_QUEUE_SIZE                    (32U)
#define MIXER_QUEUE_MASK                    (MIXER_QUEUE_SIZE - 1U)

/* Samples mixed per pass; longer audio buffers are mixed in several passes. */
#define MIXER_BLOCK_SAMPLES                 (256U)

#define MIXER_SAMPLE_MINIMUM                (-32768)
#define MIXER_SAMPLE_MAXIMUM                (32767)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef enum
{
    MIXER_REQUEST_PLAY_SOUND = 0,
    MIXER_REQUEST_PLAY_GENERATOR,
    MIXER_REQUEST_STOP,
    MIXER_REQUEST_STOP_APPLICATION_CHANNELS,
    MIXER_REQUEST_SET_VOLUME
} Mixer_RequestKindTypeDef;

typedef struct
{
    Mixer_RequestKindTypeDef Kind;
    Mixer_ChannelTypeDef Channel;
    Mixer_SoundTypeDef Sound;
    bool Loop;
    Mixer_GeneratorTypeDef Generator;
    void *Context;
    uint16_t Volume;
} Mixer_RequestTypeDef;

/**
 * @brief Playback state of one mixer channel, owned by the audio context.
 */
typedef struct
{
    Mixer_SoundTypeDef Sound;
    uint32_t Position;
    bool Loop;
    Mixer_GeneratorTypeDef Generator;
    void *Context;
    uint16_t Volume;
    bool Active;
} Mixer_ChannelStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Mixer_RequestTypeDef Mixer_Requests[MIXER_QUEUE_SIZE];
static uint32_t Mixer_RequestWriteIndex;
static uint32_t Mixer_RequestReadIndex;

static Mixer_ChannelStateTypeDef Mixer_Channels[MIXER_CHANNEL_COUNT];

/* Master volume, written by the main loop and read once per audio block. */
static uint32_t Mixer_MasterVolume = MIXER_VOLUME_MAX;
static bool Mixer_ChannelPlaying[MIXER_CHANNEL_COUNT];

static int32_t Mixer_MixBuffer[MIXER_BLOCK_SAMPLES];
static Audio_SampleTypeDef Mixer_GeneratorBuffer[MIXER_BLOCK_SAMPLES];

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool Mixer_IsChannelValid(Mixer_ChannelTypeDef Channel)
{
    return Channel < MIXER_CHANNEL_COUNT;
}

/* Main loop: append a request for the audio context. */
static bool Mixer_PostRequest(const Mixer_RequestTypeDef *Request)
{
    uint32_t WriteIndex = Mixer_RequestWriteIndex;
    uint32_t ReadIndex = __atomic_load_n(&Mixer_RequestReadIndex, __ATOMIC_ACQUIRE);

    if((WriteIndex - ReadIndex) >= MIXER_QUEUE_SIZE)
    {
        return false;
    }

    Mixer_Requests[WriteIndex & MIXER_QUEUE_MASK] = *Request;
    __atomic_store_n(&Mixer_RequestWriteIndex, WriteIndex + 1U, __ATOMIC_RELEASE);

    return true;
}

static void Mixer_StopChannel(Mixer_ChannelStateTypeDef *State)
{
    State->Active = false;
    State->Generator = NULL;
    State->Sound.Samples = NULL;
    State->Sound.SampleCount = 0U;
    State->Position = 0U;
}

/* Audio context: apply every request posted since the previous buffer. */
static void Mixer_ApplyRequests(void)
{
    uint32_t ReadIndex = Mixer_RequestReadIndex;
    uint32_t WriteIndex = __atomic_load_n(&Mixer_RequestWriteIndex, __ATOMIC_ACQUIRE);

    while(ReadIndex != WriteIndex)
    {
        const Mixer_RequestTypeDef *Request = &Mixer_Requests[ReadIndex & MIXER_QUEUE_MASK];
        Mixer_ChannelStateTypeDef *State = &Mixer_Channels[Request->Channel];

        switch(Request->Kind)
        {
            case MIXER_REQUEST_PLAY_SOUND:
                Mixer_StopChannel(State);
                State->Sound = Request->Sound;
                State->Loop = Request->Loop;
                State->Active = true;
                break;

            case MIXER_REQUEST_PLAY_GENERATOR:
                Mixer_StopChannel(State);
                State->Generator = Request->Generator;
                State->Context = Request->Context;
                State->Active = true;
                break;

            case MIXER_REQUEST_STOP:
                Mixer_StopChannel(State);
                break;

            case MIXER_REQUEST_STOP_APPLICATION_CHANNELS:
                for(uint32_t Channel = 0U; Channel < MIXER_APPLICATION_CHANNEL_COUNT; Channel++)
                {
                    Mixer_StopChannel(&Mixer_Channels[Channel]);
                }
                break;

            case MIXER_REQUEST_SET_VOLUME:
                State->Volume = Request->Volume;
                break;

            default:
                break;
        }

        ReadIndex++;
    }

    __atomic_store_n(&Mixer_RequestReadIndex, ReadIndex, __ATOMIC_RELEASE);
}

/* Add up to SampleCount samples of a sound clip to the mix. */
static void Mixer_MixSound(Mixer_ChannelStateTypeDef *State, uint32_t SampleCount)
{
    uint32_t Index = 0U;

    while(Index < SampleCount)
    {
        if(State->Position >= State->Sound.SampleCount)
        {
            if(!State->Loop || (State->Sound.SampleCount == 0U))
            {
                Mixer_StopChannel(State);
                return;
            }

            State->Position = 0U;
        }

        Mixer_MixBuffer[Index] += ((int32_t)State->Sound.Samples[State->Position] * (int32_t)State->Volume) / (int32_t)MIXER_VOLUME_MAX;
        State->Position++;
        Index++;
    }
}

/* Add SampleCount generated samples to the mix. */
static void Mixer_MixGenerator(Mixer_ChannelStateTypeDef *State, uint32_t SampleCount)
{
    State->Generator(Mixer_GeneratorBuffer, SampleCount, State->Context);

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        Mixer_MixBuffer[Index] += ((int32_t)Mixer_GeneratorBuffer[Index] * (int32_t)State->Volume) / (int32_t)MIXER_VOLUME_MAX;
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Mixer_Init(void)
{
    for(uint32_t Channel = 0U; Channel < MIXER_CHANNEL_COUNT; Channel++)
    {
        Mixer_StopChannel(&Mixer_Channels[Channel]);
        Mixer_Channels[Channel].Volume = MIXER_VOLUME_MAX;
        Mixer_ChannelPlaying[Channel] = false;
    }

    Mixer_RequestWriteIndex = 0U;
    Mixer_RequestReadIndex = 0U;
}

bool Mixer_SetMasterVolume(uint16_t Volume)
{
    if(Volume > MIXER_VOLUME_MAX)
    {
        return false;
    }

    __atomic_store_n(&Mixer_MasterVolume, (uint32_t)Volume, __ATOMIC_RELAXED);

    return true;
}

bool Mixer_PlaySound(Mixer_ChannelTypeDef Channel, const Mixer_SoundTypeDef *Sound, bool Loop)
{
    Mixer_RequestTypeDef Request = { 0 };

    if(!Mixer_IsChannelValid(Channel) || (Sound == NULL) || (Sound->Samples == NULL) || (Sound->SampleCount == 0U))
    {
        return false;
    }

    Request.Kind = MIXER_REQUEST_PLAY_SOUND;
    Request.Channel = Channel;
    Request.Sound = *Sound;
    Request.Loop = Loop;

    return Mixer_PostRequest(&Request);
}

bool Mixer_PlayGenerator(Mixer_ChannelTypeDef Channel, Mixer_GeneratorTypeDef Generator, void *Context)
{
    Mixer_RequestTypeDef Request = { 0 };

    if(!Mixer_IsChannelValid(Channel) || (Generator == NULL))
    {
        return false;
    }

    Request.Kind = MIXER_REQUEST_PLAY_GENERATOR;
    Request.Channel = Channel;
    Request.Generator = Generator;
    Request.Context = Context;

    return Mixer_PostRequest(&Request);
}

bool Mixer_Stop(Mixer_ChannelTypeDef Channel)
{
    Mixer_RequestTypeDef Request = { 0 };

    if(!Mixer_IsChannelValid(Channel))
    {
        return false;
    }

    Request.Kind = MIXER_REQUEST_STOP;
    Request.Channel = Channel;

    return Mixer_PostRequest(&Request);
}

bool Mixer_StopApplicationChannels(void)
{
    Mixer_RequestTypeDef Request = { 0 };

    Request.Kind = MIXER_REQUEST_STOP_APPLICATION_CHANNELS;

    return Mixer_PostRequest(&Request);
}

bool Mixer_SetVolume(Mixer_ChannelTypeDef Channel, uint16_t Volume)
{
    Mixer_RequestTypeDef Request = { 0 };

    if(!Mixer_IsChannelValid(Channel))
    {
        return false;
    }

    Request.Kind = MIXER_REQUEST_SET_VOLUME;
    Request.Channel = Channel;
    Request.Volume = (Volume > MIXER_VOLUME_MAX) ? (uint16_t)MIXER_VOLUME_MAX : Volume;

    return Mixer_PostRequest(&Request);
}

bool Mixer_IsPlaying(Mixer_ChannelTypeDef Channel)
{
    if(!Mixer_IsChannelValid(Channel))
    {
        return false;
    }

    return __atomic_load_n(&Mixer_ChannelPlaying[Channel], __ATOMIC_RELAXED);
}

void Mixer_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context)
{
    uint32_t Offset = 0U;

    (void)Context;

    if(Samples == NULL)
    {
        return;
    }

    const uint32_t MasterVolume = __atomic_load_n(&Mixer_MasterVolume, __ATOMIC_RELAXED);

    Mixer_ApplyRequests();

    while(Offset < FrameCount)
    {
        uint32_t BlockSamples = FrameCount - Offset;

        if(BlockSamples > MIXER_BLOCK_SAMPLES)
        {
            BlockSamples = MIXER_BLOCK_SAMPLES;
        }

        for(uint32_t Index = 0U; Index < BlockSamples; Index++)
        {
            Mixer_MixBuffer[Index] = 0;
        }

        for(uint32_t Channel = 0U; Channel < MIXER_CHANNEL_COUNT; Channel++)
        {
            Mixer_ChannelStateTypeDef *State = &Mixer_Channels[Channel];

            if(!State->Active)
            {
                continue;
            }

            if(State->Generator != NULL)
            {
                Mixer_MixGenerator(State, BlockSamples);
            }
            else
            {
                Mixer_MixSound(State, BlockSamples);
            }
        }

        /* Apply the master volume, then saturate so overlapping loud sources clip instead of wrapping. */
        for(uint32_t Index = 0U; Index < BlockSamples; Index++)
        {
            int32_t Sample = (int32_t)(((int64_t)Mixer_MixBuffer[Index] * (int64_t)MasterVolume) / (int64_t)MIXER_VOLUME_MAX);

            if(Sample > MIXER_SAMPLE_MAXIMUM)
            {
                Sample = MIXER_SAMPLE_MAXIMUM;
            }
            else if(Sample < MIXER_SAMPLE_MINIMUM)
            {
                Sample = MIXER_SAMPLE_MINIMUM;
            }

            Samples[Offset + Index] = (Audio_SampleTypeDef)Sample;
        }

        Offset += BlockSamples;
    }

    for(uint32_t Channel = 0U; Channel < MIXER_CHANNEL_COUNT; Channel++)
    {
        __atomic_store_n(&Mixer_ChannelPlaying[Channel], Mixer_Channels[Channel].Active, __ATOMIC_RELAXED);
    }
}
