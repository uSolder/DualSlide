/**
 * @file music.c
 * @brief Sample-based music player.
 *
 * The player runs as a mixer generator in the audio output context and owns
 * all playback state there. Music_Play() only publishes which song to start:
 * each request carries a sequence number, and the generator restarts the
 * song whenever it sees a sequence number it has not played yet.
 *
 * Each voice plays an instrument recording at a pitch-dependent step, using
 * 16.16 fixed-point positions and linear interpolation between samples.
 */

#include "music.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Play requests in flight; older slots are reused round-robin. */
#define MUSIC_REQUEST_COUNT                 (4U)

#define MUSIC_FRACTION_BITS                 (16U)
#define MUSIC_FRACTION_MASK                 ((1UL << MUSIC_FRACTION_BITS) - 1UL)
#define MUSIC_GAIN_BITS                     (15U)

/* Velocity 127 maps to half scale, leaving headroom for chords. */
#define MUSIC_MAXIMUM_VELOCITY              (127U)
#define MUSIC_VELOCITY_GAIN                 (129)

/* Highest octave shift above an instrument's root note. */
#define MUSIC_MAXIMUM_OCTAVE_SHIFT          (8)

#define MUSIC_SAMPLES_PER_MILLISECOND       (MIXER_SAMPLE_RATE_HZ / 1000U)
#define MUSIC_SAMPLE_MINIMUM                (-32768)
#define MUSIC_SAMPLE_MAXIMUM                (32767)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    const Music_SongTypeDef *Song;
    uint32_t Sequence;
} Music_RequestTypeDef;

/**
 * @brief One sounding note.
 */
typedef struct
{
    const Music_InstrumentTypeDef *Instrument;
    uint32_t Position;
    uint32_t Fraction;
    uint32_t Step;
    int32_t Gain;
    uint32_t HoldSamples;
    uint32_t ReleaseSamples;
    uint32_t ReleaseRemaining;
    uint32_t Age;
    bool Releasing;
    bool Active;
} Music_VoiceTypeDef;

/**
 * @brief Playback state, owned by the audio output context.
 */
typedef struct
{
    const Music_SongTypeDef *Song;
    uint32_t Sequence;
    uint64_t SamplesPerTickQ16;
    uint64_t LengthSamples;
    uint64_t Position;
    uint64_t NextNoteSample;
    uint32_t NextNote;
    uint32_t NextAge;
    bool Started;
    bool Playing;
    Music_VoiceTypeDef Voices[MUSIC_VOICE_COUNT];
} Music_PlayerTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Playback step for 0 to 11 semitones above the root, in 16.16 fixed point. */
static const uint32_t Music_SemitoneSteps[12] =
{
    65536U, 69433U, 73562U, 77936U, 82570U, 87480U,
    92682U, 98193U, 104032U, 110218U, 116772U, 123715U
};

/* Main loop state. */
static Music_RequestTypeDef Music_Requests[MUSIC_REQUEST_COUNT];
static uint32_t Music_NextRequest;
static uint32_t Music_NextSequence = 1U;
static Mixer_ChannelTypeDef Music_Channel;
static bool Music_ChannelAssigned;

/* Audio context state. */
static Music_PlayerTypeDef Music_Player;

/* Written by both contexts with atomic stores. */
static bool Music_Playing;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint64_t Music_TicksToSamples(uint32_t Ticks)
{
    return ((uint64_t)Ticks * Music_Player.SamplesPerTickQ16) >> MUSIC_FRACTION_BITS;
}

static void Music_ScheduleNextNote(void)
{
    const Music_SongTypeDef *Song = Music_Player.Song;

    Music_Player.NextNoteSample = (Music_Player.NextNote < Song->NoteCount) ?
                                  Music_TicksToSamples(Song->Notes[Music_Player.NextNote].StartTick) :
                                  UINT64_MAX;
}

static void Music_RestartSong(void)
{
    Music_Player.Position = 0U;
    Music_Player.NextNote = 0U;
    Music_ScheduleNextNote();
}

static void Music_StartSong(const Music_SongTypeDef *Song)
{
    for(uint32_t Index = 0U; Index < MUSIC_VOICE_COUNT; Index++)
    {
        Music_Player.Voices[Index].Active = false;
    }

    Music_Player.Song = Song;
    Music_Player.SamplesPerTickQ16 = (((uint64_t)MIXER_SAMPLE_RATE_HZ * Song->MicrosecondsPerQuarterNote) << MUSIC_FRACTION_BITS) /
                                     (1000000ULL * Song->TicksPerQuarterNote);
    Music_Player.LengthSamples = Music_TicksToSamples(Song->LengthTicks);
    Music_Player.NextAge = 0U;
    Music_Player.Playing = true;
    Music_RestartSong();
}

/* A free voice, else the quietest releasing voice, else the oldest voice. */
static Music_VoiceTypeDef *Music_AllocateVoice(void)
{
    Music_VoiceTypeDef *Releasing = NULL;
    Music_VoiceTypeDef *Oldest = &Music_Player.Voices[0];

    for(uint32_t Index = 0U; Index < MUSIC_VOICE_COUNT; Index++)
    {
        Music_VoiceTypeDef *Voice = &Music_Player.Voices[Index];

        if(!Voice->Active)
        {
            return Voice;
        }

        if(Voice->Releasing && ((Releasing == NULL) || (Voice->ReleaseRemaining < Releasing->ReleaseRemaining)))
        {
            Releasing = Voice;
        }

        if((Music_Player.NextAge - Voice->Age) > (Music_Player.NextAge - Oldest->Age))
        {
            Oldest = Voice;
        }
    }

    return (Releasing != NULL) ? Releasing : Oldest;
}

/* Playback step that shifts an instrument from its root note to Note. */
static uint32_t Music_GetStep(uint8_t RootNote, uint8_t Note)
{
    int32_t Semitones = (int32_t)Note - (int32_t)RootNote;
    int32_t Octaves = ((Semitones + 120) / 12) - 10;
    uint32_t Step = Music_SemitoneSteps[(Semitones + 120) % 12];

    if(Octaves > MUSIC_MAXIMUM_OCTAVE_SHIFT)
    {
        Octaves = MUSIC_MAXIMUM_OCTAVE_SHIFT;
    }

    return (Octaves >= 0) ? (Step << Octaves) : (Step >> -Octaves);
}

static void Music_StartNote(const Music_NoteTypeDef *Note)
{
    const Music_SongTypeDef *Song = Music_Player.Song;
    const Music_InstrumentTypeDef *Instrument;
    Music_VoiceTypeDef *Voice;
    uint64_t HoldSamples;
    uint8_t Velocity;

    if(Note->Instrument >= Song->InstrumentCount)
    {
        return;
    }

    Instrument = Song->Instruments[Note->Instrument];

    if((Instrument == NULL) || (Instrument->Samples == NULL) || (Instrument->SampleCount == 0U))
    {
        return;
    }

    HoldSamples = Music_TicksToSamples(Note->DurationTicks);
    Velocity = (Note->Velocity > MUSIC_MAXIMUM_VELOCITY) ? (uint8_t)MUSIC_MAXIMUM_VELOCITY : Note->Velocity;

    Voice = Music_AllocateVoice();
    Voice->Instrument = Instrument;
    Voice->Position = 0U;
    Voice->Fraction = 0U;
    Voice->Step = Music_GetStep(Instrument->RootNote, Note->Note);
    Voice->Gain = (int32_t)Velocity * MUSIC_VELOCITY_GAIN;
    Voice->HoldSamples = (HoldSamples == 0U) ? 1U : ((HoldSamples > UINT32_MAX) ? UINT32_MAX : (uint32_t)HoldSamples);
    Voice->ReleaseSamples = (uint32_t)Instrument->ReleaseMilliseconds * MUSIC_SAMPLES_PER_MILLISECOND;
    Voice->ReleaseSamples = (Voice->ReleaseSamples == 0U) ? 1U : Voice->ReleaseSamples;
    Voice->ReleaseRemaining = Voice->ReleaseSamples;
    Voice->Age = Music_Player.NextAge++;
    Voice->Releasing = false;
    Voice->Active = true;
}

/* Produce the next sample of one voice and advance it. */
static int32_t Music_RenderVoice(Music_VoiceTypeDef *Voice)
{
    const Music_InstrumentTypeDef *Instrument = Voice->Instrument;
    bool Looping = (Instrument->LoopEnd > Instrument->LoopStart) && (Instrument->LoopEnd <= Instrument->SampleCount);
    uint32_t NextPosition = Voice->Position + 1U;
    int32_t Current = Instrument->Samples[Voice->Position];
    int32_t Next;
    int32_t Value;
    int32_t Gain = Voice->Gain;

    if(Looping && (NextPosition >= Instrument->LoopEnd))
    {
        NextPosition = Instrument->LoopStart;
    }

    Next = (NextPosition < Instrument->SampleCount) ? Instrument->Samples[NextPosition] : Current;
    Value = Current + (((Next - Current) * (int32_t)(Voice->Fraction >> 1U)) >> (MUSIC_FRACTION_BITS - 1U));

    if(Voice->Releasing)
    {
        Gain = (int32_t)(((uint64_t)(uint32_t)Gain * Voice->ReleaseRemaining) / Voice->ReleaseSamples);
    }

    /* Advance through the recording. */
    Voice->Fraction += Voice->Step;
    Voice->Position += Voice->Fraction >> MUSIC_FRACTION_BITS;
    Voice->Fraction &= MUSIC_FRACTION_MASK;

    if(Looping)
    {
        while(Voice->Position >= Instrument->LoopEnd)
        {
            Voice->Position -= Instrument->LoopEnd - Instrument->LoopStart;
        }
    }
    else if(Voice->Position >= Instrument->SampleCount)
    {
        Voice->Active = false;
    }

    /* Hold for the note's duration, then fade out over the release time. */
    if(Voice->Releasing)
    {
        if(Voice->ReleaseRemaining <= 1U)
        {
            Voice->Active = false;
        }
        else
        {
            Voice->ReleaseRemaining--;
        }
    }
    else if(Voice->HoldSamples > 1U)
    {
        Voice->HoldSamples--;
    }
    else
    {
        Voice->Releasing = true;
    }

    return (Value * Gain) >> MUSIC_GAIN_BITS;
}

/* Mixer generator: render the song into Samples. */
static void Music_Generate(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context)
{
    const Music_RequestTypeDef *Request = (const Music_RequestTypeDef *)Context;
    bool Sounding = false;

    if(!Music_Player.Started || (Request->Sequence != Music_Player.Sequence))
    {
        Music_Player.Sequence = Request->Sequence;
        Music_Player.Started = true;
        Music_StartSong(Request->Song);
    }

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const Music_SongTypeDef *Song = Music_Player.Song;
        int32_t Sum = 0;

        if(Music_Player.Playing)
        {
            while(Music_Player.NextNoteSample <= Music_Player.Position)
            {
                Music_StartNote(&Song->Notes[Music_Player.NextNote]);
                Music_Player.NextNote++;
                Music_ScheduleNextNote();
            }
        }

        for(uint32_t Voice = 0U; Voice < MUSIC_VOICE_COUNT; Voice++)
        {
            if(Music_Player.Voices[Voice].Active)
            {
                Sum += Music_RenderVoice(&Music_Player.Voices[Voice]);
            }
        }

        if(Sum > MUSIC_SAMPLE_MAXIMUM)
        {
            Sum = MUSIC_SAMPLE_MAXIMUM;
        }
        else if(Sum < MUSIC_SAMPLE_MINIMUM)
        {
            Sum = MUSIC_SAMPLE_MINIMUM;
        }

        Samples[Index] = (Audio_SampleTypeDef)Sum;

        if(Music_Player.Playing)
        {
            Music_Player.Position++;

            if(Music_Player.Position >= Music_Player.LengthSamples)
            {
                if(Song->Loop && (Music_Player.LengthSamples > 0U))
                {
                    Music_RestartSong();
                }
                else if(Music_Player.NextNote >= Song->NoteCount)
                {
                    Music_Player.Playing = false;
                }
            }
        }
    }

    for(uint32_t Voice = 0U; Voice < MUSIC_VOICE_COUNT; Voice++)
    {
        Sounding = Sounding || Music_Player.Voices[Voice].Active;
    }

    __atomic_store_n(&Music_Playing, Music_Player.Playing || Sounding, __ATOMIC_RELAXED);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Music_Play(Mixer_ChannelTypeDef Channel, const Music_SongTypeDef *Song)
{
    Music_RequestTypeDef *Request;

    if((Channel >= MIXER_CHANNEL_COUNT) ||
       (Song == NULL) ||
       (Song->Instruments == NULL) ||
       ((Song->Notes == NULL) && (Song->NoteCount != 0U)) ||
       (Song->TicksPerQuarterNote == 0U) ||
       (Song->MicrosecondsPerQuarterNote == 0U))
    {
        return false;
    }

    Request = &Music_Requests[Music_NextRequest];
    Music_NextRequest = (Music_NextRequest + 1U) % MUSIC_REQUEST_COUNT;
    Request->Song = Song;
    Request->Sequence = Music_NextSequence++;

    if(Music_ChannelAssigned && (Music_Channel != Channel))
    {
        (void)Mixer_Stop(Music_Channel);
    }

    if(!Mixer_PlayGenerator(Channel, Music_Generate, Request))
    {
        return false;
    }

    Music_Channel = Channel;
    Music_ChannelAssigned = true;
    __atomic_store_n(&Music_Playing, true, __ATOMIC_RELAXED);

    return true;
}

void Music_Stop(void)
{
    if(Music_ChannelAssigned)
    {
        (void)Mixer_Stop(Music_Channel);
        Music_ChannelAssigned = false;
    }

    __atomic_store_n(&Music_Playing, false, __ATOMIC_RELAXED);
}

bool Music_IsPlaying(void)
{
    return __atomic_load_n(&Music_Playing, __ATOMIC_RELAXED);
}
