/**
 * @file pong_audio.c
 * @brief Generated sound effects for Pong.
 *
 * Every sound is a short sequence of notes from the tables below, played by
 * one mixer generator per channel. Each note is a soft FM tone: a sine
 * carrier whose brightness (modulation index) fades faster than its volume,
 * so it starts with a gentle click and settles into a clean tone. A note can
 * glide in pitch and add band-limited noise.
 *
 * The game writes the sound, pitch and level for a channel, then bumps its
 * restart count; the generator picks these up at the next audio block and
 * owns all of its synthesis state.
 */

#include "pong_audio.h"

#include "mixer.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define PA_PADDLE_CHANNEL                   ((Mixer_ChannelTypeDef)0U)
#define PA_WALL_CHANNEL                     ((Mixer_ChannelTypeDef)1U)
#define PA_MISS_CHANNEL                     ((Mixer_ChannelTypeDef)2U)
#define PA_POWER_UP_CHANNEL                 ((Mixer_ChannelTypeDef)3U)
#define PA_EXPIRE_CHANNEL                   ((Mixer_ChannelTypeDef)4U)
#define PA_PLAYER_COUNT                     (5U)

#define PA_SAMPLE_RATE                      ((float)MIXER_SAMPLE_RATE_HZ)
#define PA_TWO_PI                           (6.2831853f)
#define PA_FULL_SCALE                       (32767.0f)
#define PA_MAXIMUM_NOTES                    (4U)

/* Overall level of every Pong sound. */
#define PA_MASTER_LEVEL                     (0.35f)

/* Paddle */
#define PA_PADDLE_LEVEL                     (0.5f)
#define PA_PADDLE_SLOW_PITCH                (0.85f)
#define PA_PADDLE_FAST_PITCH                (1.25f)
#define PA_PADDLE_RIGHT_PITCH               (1.122f)
#define PA_PADDLE_SLOW_LOUDNESS             (0.7f)

/* Wall */
#define PA_WALL_LEVEL                       (0.3f)
#define PA_WALL_SLOW_PITCH                  (0.9f)
#define PA_WALL_FAST_PITCH                  (1.1f)

/* Shield, miss and power-ups */
#define PA_SHIELD_LEVEL                     (0.4f)
#define PA_MISS_LEVEL                       (0.4f)
#define PA_POWER_UP_LEVEL                   (0.3f)
#define PA_EXPIRE_LEVEL                     (0.15f)

/* Noise is band-limited from here up to each note's cutoff. */
#define PA_NOISE_HIGH_PASS_HZ               (300.0f)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief One note of a sound. Times are in seconds, frequencies in hertz.
 */
typedef struct
{
    float StartSeconds;           /* When the note begins.                        */
    float StartHz;                /* Pitch at the start of the note.              */
    float EndHz;                  /* Pitch it glides towards.                     */
    float GlideSeconds;           /* Glide time constant; 0 for no glide.         */
    float AttackSeconds;          /* Fade-in time.                                */
    float DecaySeconds;           /* Volume decay time constant.                  */
    float Level;                  /* Relative volume.                             */
    float ModulatorRatio;         /* FM modulator / carrier frequency ratio.      */
    float ModulationIndex;        /* Starting brightness; 0 is a pure sine.       */
    float ModulationDecaySeconds; /* How quickly the brightness fades.            */
    float NoiseLevel;             /* Noise mixed into the note.                   */
    float NoiseCutoffHz;          /* Upper edge of that noise.                    */
} PongAudio_NoteTypeDef;

typedef struct
{
    const PongAudio_NoteTypeDef *Notes;
    uint32_t NoteCount;
    float LengthSeconds;
} PongAudio_SoundTypeDef;

/**
 * @brief Values the game passes to one channel's generator.
 */
typedef struct
{
    volatile uint32_t RestartCount;
    const PongAudio_SoundTypeDef *volatile Sound;
    volatile float Pitch;
    volatile float Level;
} PongAudio_ControlTypeDef;

typedef struct
{
    uint32_t StartSample;
    uint32_t AttackSamples;
    float FrequencyHz;
    float TargetHz;
    float GlideCoefficient;
    float DecayCoefficient;
    float ModulationDecayCoefficient;
    float NoiseCoefficient;
    float Amplitude;
    float ModulationIndex;
    float CarrierPhase;
    float ModulatorPhase;
    float NoiseLow;
    float NoiseFloor;
} PongAudio_VoiceTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    uint32_t Sample;
    uint32_t LengthSamples;
    const PongAudio_SoundTypeDef *Sound;
    float Level;
    PongAudio_VoiceTypeDef Voices[PA_MAXIMUM_NOTES];
} PongAudio_StateTypeDef;

typedef struct
{
    Mixer_ChannelTypeDef Channel;
    PongAudio_ControlTypeDef Control;
    PongAudio_StateTypeDef State;
} PongAudio_PlayerTypeDef;

/* -------------------------------------------------------------------------- */
/* Sounds                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Columns: start s, start Hz, end Hz, glide s, attack s, decay s, level,
 *          FM ratio, FM index, FM decay s, noise level, noise cutoff Hz
 */

/* Paddle: a clean "pok" with a short tock on top. */
static const PongAudio_NoteTypeDef PongAudio_PaddleNotes[] = {
    { 0.0f, 740.0f, 620.0f, 0.015f, 0.002f, 0.06f, 1.0f, 2.0f, 1.2f, 0.02f, 0.0f, 0.0f },
    { 0.0f, 2960.0f, 2960.0f, 0.0f, 0.001f, 0.008f, 0.15f, 1.0f, 0.0f, 0.01f, 0.0f, 0.0f },
};

/* Wall: a softer, shorter "tik". */
static const PongAudio_NoteTypeDef PongAudio_WallNotes[] = {
    { 0.0f, 520.0f, 470.0f, 0.01f, 0.002f, 0.035f, 1.0f, 1.0f, 0.6f, 0.015f, 0.0f, 0.0f },
};

/* Shield: the ball striking an energy field, a buzzing "thwum" that wavers, over the wall tik. */
static const PongAudio_NoteTypeDef PongAudio_ShieldNotes[] = {
    { 0.0f, 880.0f, 440.0f, 0.03f, 0.002f, 0.12f, 0.6f, 2.0f, 2.5f, 0.05f, 0.3f, 5000.0f },
    { 0.0f, 893.0f, 447.0f, 0.03f, 0.002f, 0.12f, 0.45f, 2.0f, 2.5f, 0.05f, 0.0f, 0.0f },
    { 0.0f, 520.0f, 470.0f, 0.01f, 0.002f, 0.035f, 0.5f, 1.0f, 0.6f, 0.015f, 0.0f, 0.0f },
};

/* Miss: a calm falling "da-dum" with a soft breath of air. */
static const PongAudio_NoteTypeDef PongAudio_MissNotes[] = {
    { 0.0f, 659.0f, 640.0f, 0.1f, 0.005f, 0.15f, 0.8f, 1.0f, 0.8f, 0.1f, 0.0f, 0.0f },
    { 0.14f, 494.0f, 392.0f, 0.25f, 0.01f, 0.35f, 1.0f, 1.0f, 0.8f, 0.3f, 0.25f, 1500.0f },
};

/* Expand: inflating, a swelling octave slide that fills out and lands on a bright ting. */
static const PongAudio_NoteTypeDef PongAudio_ExpandNotes[] = {
    { 0.0f, 392.0f, 784.0f, 0.1f, 0.3f, 0.3f, 0.7f, 1.0f, 1.5f, 0.3f, 0.1f, 2500.0f },
    { 0.0f, 396.0f, 792.0f, 0.1f, 0.3f, 0.3f, 0.6f, 1.0f, 1.5f, 0.3f, 0.0f, 0.0f },
    { 0.15f, 784.0f, 1568.0f, 0.08f, 0.15f, 0.25f, 0.4f, 2.0f, 1.0f, 0.2f, 0.0f, 0.0f },
    { 0.3f, 1568.0f, 1568.0f, 0.0f, 0.003f, 0.25f, 0.3f, 3.5f, 1.2f, 0.1f, 0.0f, 0.0f },
};

/* Shield: a force field powering up, a wavering hum that rises and crackles. */
static const PongAudio_NoteTypeDef PongAudio_ShieldPowerUpNotes[] = {
    { 0.0f, 330.0f, 660.0f, 0.08f, 0.12f, 0.35f, 0.7f, 2.0f, 2.0f, 0.4f, 0.15f, 4000.0f },
    { 0.0f, 336.0f, 672.0f, 0.08f, 0.12f, 0.35f, 0.6f, 2.0f, 2.0f, 0.4f, 0.0f, 0.0f },
    { 0.1f, 660.0f, 1320.0f, 0.08f, 0.1f, 0.3f, 0.35f, 2.0f, 1.5f, 0.3f, 0.0f, 0.0f },
    { 0.1f, 671.0f, 1342.0f, 0.08f, 0.1f, 0.3f, 0.3f, 2.0f, 1.5f, 0.3f, 0.0f, 0.0f },
};

/* Shrink: deflating, a zoom down with escaping air, then a tiny "pip-pip". */
static const PongAudio_NoteTypeDef PongAudio_ShrinkNotes[] = {
    { 0.0f, 1047.0f, 392.0f, 0.1f, 0.003f, 0.2f, 0.8f, 1.0f, 1.5f, 0.1f, 0.2f, 2500.0f },
    { 0.0f, 1057.0f, 396.0f, 0.1f, 0.003f, 0.2f, 0.6f, 1.0f, 1.5f, 0.1f, 0.0f, 0.0f },
    { 0.28f, 1760.0f, 1760.0f, 0.0f, 0.002f, 0.04f, 0.5f, 2.0f, 0.5f, 0.02f, 0.0f, 0.0f },
    { 0.36f, 1760.0f, 1760.0f, 0.0f, 0.002f, 0.04f, 0.35f, 2.0f, 0.5f, 0.02f, 0.0f, 0.0f },
};

/* Power: a fast rising zap, then a ringing high note. */
static const PongAudio_NoteTypeDef PongAudio_PowerNotes[] = {
    { 0.0f, 330.0f, 1320.0f, 0.05f, 0.003f, 0.2f, 1.0f, 1.0f, 2.5f, 0.08f, 0.15f, 3000.0f },
    { 0.08f, 1320.0f, 1320.0f, 0.0f, 0.003f, 0.25f, 0.6f, 2.0f, 1.0f, 0.1f, 0.0f, 0.0f },
};

/* Invert: a swoop up, then its mirror image back down; detuned pairs beat for a dizzy feel. */
static const PongAudio_NoteTypeDef PongAudio_InvertNotes[] = {
    { 0.0f, 600.0f, 1200.0f, 0.04f, 0.003f, 0.12f, 0.6f, 2.0f, 1.2f, 0.08f, 0.0f, 0.0f },
    { 0.0f, 609.0f, 1218.0f, 0.04f, 0.003f, 0.12f, 0.5f, 2.0f, 1.2f, 0.08f, 0.0f, 0.0f },
    { 0.13f, 1200.0f, 600.0f, 0.04f, 0.003f, 0.2f, 0.7f, 2.0f, 1.2f, 0.1f, 0.0f, 0.0f },
    { 0.13f, 1218.0f, 609.0f, 0.04f, 0.003f, 0.2f, 0.6f, 2.0f, 1.2f, 0.1f, 0.0f, 0.0f },
};

/* Expire: a soft two-note "bloop" falling a fourth, as a power-up wears off. */
static const PongAudio_NoteTypeDef PongAudio_ExpireNotes[] = {
    { 0.0f, 784.0f, 784.0f, 0.0f, 0.003f, 0.07f, 1.0f, 1.0f, 0.4f, 0.03f, 0.0f, 0.0f },
    { 0.07f, 587.0f, 587.0f, 0.0f, 0.003f, 0.09f, 0.8f, 1.0f, 0.3f, 0.03f, 0.0f, 0.0f },
};

#define PA_SOUND(Notes, LengthSeconds) { (Notes), (uint32_t)(sizeof(Notes) / sizeof((Notes)[0])), (LengthSeconds) }

static const PongAudio_SoundTypeDef PongAudio_PaddleSound = PA_SOUND(PongAudio_PaddleNotes, 0.3f);
static const PongAudio_SoundTypeDef PongAudio_WallSound = PA_SOUND(PongAudio_WallNotes, 0.2f);
static const PongAudio_SoundTypeDef PongAudio_ShieldSound = PA_SOUND(PongAudio_ShieldNotes, 0.5f);
static const PongAudio_SoundTypeDef PongAudio_MissSound = PA_SOUND(PongAudio_MissNotes, 1.2f);
static const PongAudio_SoundTypeDef PongAudio_ExpireSound = PA_SOUND(PongAudio_ExpireNotes, 0.5f);
static const PongAudio_SoundTypeDef PongAudio_PowerUpSounds[] = {
    PA_SOUND(PongAudio_ExpandNotes, 1.4f),
    PA_SOUND(PongAudio_ShieldPowerUpNotes, 1.3f),
    PA_SOUND(PongAudio_ShrinkNotes, 0.9f),
    PA_SOUND(PongAudio_PowerNotes, 0.8f),
    PA_SOUND(PongAudio_InvertNotes, 0.9f),
};

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static PongAudio_PlayerTypeDef PongAudio_Players[PA_PLAYER_COUNT] = {
    { .Channel = PA_PADDLE_CHANNEL },
    { .Channel = PA_WALL_CHANNEL },
    { .Channel = PA_MISS_CHANNEL },
    { .Channel = PA_POWER_UP_CHANNEL },
    { .Channel = PA_EXPIRE_CHANNEL },
};

/* -------------------------------------------------------------------------- */
/* Synthesis helpers                                                          */
/* -------------------------------------------------------------------------- */

static float PongAudio_Noise(uint32_t *State)
{
    uint32_t Value = *State;

    Value ^= Value << 13U;
    Value ^= Value >> 17U;
    Value ^= Value << 5U;
    *State = Value;

    return ((float)Value / 2147483648.0f) - 1.0f;
}

static float PongAudio_LowPassCoefficient(float CutoffHz)
{
    return 1.0f - expf(-PA_TWO_PI * CutoffHz / PA_SAMPLE_RATE);
}

/* Per-sample multiplier that decays to 1/e in TimeSeconds; 0 stops at once. */
static float PongAudio_DecayCoefficient(float TimeSeconds)
{
    return (TimeSeconds > 0.0f) ? expf(-1.0f / (TimeSeconds * PA_SAMPLE_RATE)) : 0.0f;
}

static uint32_t PongAudio_Seconds(float Seconds)
{
    return (uint32_t)(Seconds * PA_SAMPLE_RATE);
}

static Audio_SampleTypeDef PongAudio_ToSample(float Value)
{
    Value *= PA_MASTER_LEVEL;

    if(Value > 1.0f)
    {
        Value = 1.0f;
    }
    else if(Value < -1.0f)
    {
        Value = -1.0f;
    }

    return (Audio_SampleTypeDef)(Value * PA_FULL_SCALE);
}

/* -------------------------------------------------------------------------- */
/* Generator                                                                  */
/* -------------------------------------------------------------------------- */

static void PongAudio_StartSound(PongAudio_StateTypeDef *State, const PongAudio_ControlTypeDef *Control)
{
    const uint32_t RestartCount = Control->RestartCount;
    const PongAudio_SoundTypeDef *Sound = Control->Sound;
    const float Pitch = Control->Pitch;

    *State = (PongAudio_StateTypeDef){ 0 };
    State->RestartCount = RestartCount;
    State->Random = 0x2545F491U ^ (RestartCount * 0x9E3779B9U);
    State->Sound = Sound;
    State->Level = Control->Level;

    if(Sound == NULL)
    {
        return;
    }

    State->LengthSamples = PongAudio_Seconds(Sound->LengthSeconds);

    for(uint32_t Note = 0U; (Note < Sound->NoteCount) && (Note < PA_MAXIMUM_NOTES); Note++)
    {
        const PongAudio_NoteTypeDef *Definition = &Sound->Notes[Note];
        PongAudio_VoiceTypeDef *Voice = &State->Voices[Note];

        Voice->StartSample = PongAudio_Seconds(Definition->StartSeconds);
        Voice->AttackSamples = PongAudio_Seconds(Definition->AttackSeconds) + 1U;
        Voice->FrequencyHz = Definition->StartHz * Pitch;
        Voice->TargetHz = Definition->EndHz * Pitch;
        Voice->GlideCoefficient = (Definition->GlideSeconds > 0.0f) ? (1.0f - PongAudio_DecayCoefficient(Definition->GlideSeconds)) : 1.0f;
        Voice->DecayCoefficient = PongAudio_DecayCoefficient(Definition->DecaySeconds);
        Voice->ModulationDecayCoefficient = PongAudio_DecayCoefficient(Definition->ModulationDecaySeconds);
        Voice->NoiseCoefficient = PongAudio_LowPassCoefficient(Definition->NoiseCutoffHz);
        Voice->Amplitude = 1.0f;
        Voice->ModulationIndex = Definition->ModulationIndex;
    }
}

static float PongAudio_RunVoice(PongAudio_StateTypeDef *State, uint32_t Note, float Noise, float NoiseFloorCoefficient)
{
    const PongAudio_NoteTypeDef *Definition = &State->Sound->Notes[Note];
    PongAudio_VoiceTypeDef *Voice = &State->Voices[Note];
    const uint32_t Elapsed = State->Sample - Voice->StartSample;
    float Envelope;
    float Tone;

    if(State->Sample < Voice->StartSample)
    {
        return 0.0f;
    }

    /* Linear fade-in, then an exponential decay. */
    if(Elapsed < Voice->AttackSamples)
    {
        Envelope = (float)Elapsed / (float)Voice->AttackSamples;
    }
    else
    {
        Voice->Amplitude *= Voice->DecayCoefficient;
        Envelope = Voice->Amplitude;
    }

    Voice->FrequencyHz += Voice->GlideCoefficient * (Voice->TargetHz - Voice->FrequencyHz);

    Voice->CarrierPhase += Voice->FrequencyHz / PA_SAMPLE_RATE;
    Voice->CarrierPhase -= (Voice->CarrierPhase >= 1.0f) ? 1.0f : 0.0f;
    Voice->ModulatorPhase += (Voice->FrequencyHz * Definition->ModulatorRatio) / PA_SAMPLE_RATE;
    Voice->ModulatorPhase -= floorf(Voice->ModulatorPhase);

    Tone = sinf((PA_TWO_PI * Voice->CarrierPhase) + (Voice->ModulationIndex * sinf(PA_TWO_PI * Voice->ModulatorPhase)));
    Voice->ModulationIndex *= Voice->ModulationDecayCoefficient;

    if(Definition->NoiseLevel > 0.0f)
    {
        Voice->NoiseLow += Voice->NoiseCoefficient * (Noise - Voice->NoiseLow);
        Voice->NoiseFloor += NoiseFloorCoefficient * (Voice->NoiseLow - Voice->NoiseFloor);
        Tone += Definition->NoiseLevel * 2.0f * (Voice->NoiseLow - Voice->NoiseFloor);
    }

    return Tone * Envelope * Definition->Level;
}

static void PongAudio_Generate(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context)
{
    PongAudio_PlayerTypeDef *Player = (PongAudio_PlayerTypeDef *)Context;
    PongAudio_StateTypeDef *State = &Player->State;
    const float NoiseFloorCoefficient = PongAudio_LowPassCoefficient(PA_NOISE_HIGH_PASS_HZ);

    if(State->RestartCount != Player->Control.RestartCount)
    {
        PongAudio_StartSound(State, &Player->Control);
    }

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Noise = PongAudio_Noise(&State->Random);
        float Output = 0.0f;

        if((State->Sound == NULL) || (State->Sample >= State->LengthSamples))
        {
            Samples[Index] = 0;
            continue;
        }

        for(uint32_t Note = 0U; (Note < State->Sound->NoteCount) && (Note < PA_MAXIMUM_NOTES); Note++)
        {
            Output += PongAudio_RunVoice(State, Note, Noise, NoiseFloorCoefficient);
        }

        State->Sample++;
        Samples[Index] = PongAudio_ToSample(Output * State->Level);
    }
}

static void PongAudio_Play(PongAudio_PlayerTypeDef *Player, const PongAudio_SoundTypeDef *Sound, float Pitch, float Level)
{
    Player->Control.Sound = Sound;
    Player->Control.Pitch = Pitch;
    Player->Control.Level = Level;
    Player->Control.RestartCount++;

    (void)Mixer_PlayGenerator(Player->Channel, PongAudio_Generate, Player);
}

static float PongAudio_Clamp(float Value, float Minimum, float Maximum)
{
    return (Value < Minimum) ? Minimum : ((Value > Maximum) ? Maximum : Value);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void PongAudio_PlayPaddle(bool Left, float Speed)
{
    const float ClampedSpeed = PongAudio_Clamp(Speed, 0.0f, 1.5f);
    float Pitch = PA_PADDLE_SLOW_PITCH + ((PA_PADDLE_FAST_PITCH - PA_PADDLE_SLOW_PITCH) * ClampedSpeed);
    const float Loudness = PA_PADDLE_SLOW_LOUDNESS + ((1.0f - PA_PADDLE_SLOW_LOUDNESS) * PongAudio_Clamp(Speed, 0.0f, 1.0f));

    Pitch *= Left ? 1.0f : PA_PADDLE_RIGHT_PITCH;
    PongAudio_Play(&PongAudio_Players[0], &PongAudio_PaddleSound, Pitch, PA_PADDLE_LEVEL * Loudness);
}

void PongAudio_PlayWall(float Speed)
{
    const float Pitch = PA_WALL_SLOW_PITCH + ((PA_WALL_FAST_PITCH - PA_WALL_SLOW_PITCH) * PongAudio_Clamp(Speed, 0.0f, 1.0f));

    PongAudio_Play(&PongAudio_Players[1], &PongAudio_WallSound, Pitch, PA_WALL_LEVEL);
}

void PongAudio_PlayShield(void)
{
    PongAudio_Play(&PongAudio_Players[0], &PongAudio_ShieldSound, 1.0f, PA_SHIELD_LEVEL);
}

void PongAudio_PlayMiss(void)
{
    PongAudio_Play(&PongAudio_Players[2], &PongAudio_MissSound, 1.0f, PA_MISS_LEVEL);
}

void PongAudio_PlayPowerUp(PongAudio_PowerUpTypeDef PowerUp)
{
    if((uint32_t)PowerUp >= (uint32_t)(sizeof(PongAudio_PowerUpSounds) / sizeof(PongAudio_PowerUpSounds[0])))
    {
        return;
    }

    PongAudio_Play(&PongAudio_Players[3], &PongAudio_PowerUpSounds[PowerUp], 1.0f, PA_POWER_UP_LEVEL);
}

void PongAudio_PlayExpire(void)
{
    PongAudio_Play(&PongAudio_Players[4], &PongAudio_ExpireSound, 1.0f, PA_EXPIRE_LEVEL);
}

void PongAudio_Stop(void)
{
    for(uint32_t Player = 0U; Player < PA_PLAYER_COUNT; Player++)
    {
        (void)Mixer_Stop(PongAudio_Players[Player].Channel);
    }
}
