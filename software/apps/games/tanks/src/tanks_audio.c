/**
 * @file tanks_audio.c
 * @brief Generated sound effects and jingles for TANKS.
 *
 * Three mixer generators run while the game is open:
 *
 * - Ambience follows control values the game loop writes every frame: how
 *   fast the player's tank and the enemy tanks are moving, and how close
 *   rockets are. Each tread is a rhythm of track-link clacks and clinks, an
 *   engine buzz and rolling gravel, all quickening with speed.
 * - Effects play one-shot sounds from a pool of voices. The game loop posts
 *   events to a lock-free queue; the generator starts a voice for each at
 *   the next audio block, stealing the oldest voice when all are busy.
 * - Jingles sequence short brass, snare and bell phrases.
 *
 * Thumps and booms are built from pitch-dropping tones pushed through a soft
 * clipper: the harmonics carry the weight on a small speaker, and a final
 * high-pass on every generator keeps true bass out of it.
 */

#include "tanks_audio.h"

#include "mixer.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define TA_AMBIENCE_CHANNEL                 ((Mixer_ChannelTypeDef)0U)
#define TA_EFFECTS_CHANNEL                  ((Mixer_ChannelTypeDef)1U)
#define TA_JINGLE_CHANNEL                   ((Mixer_ChannelTypeDef)2U)

#define TA_SAMPLE_RATE                      ((float)MIXER_SAMPLE_RATE_HZ)
#define TA_SAMPLE_PERIOD                    (1.0f / TA_SAMPLE_RATE)
#define TA_TWO_PI                           (6.2831853f)
#define TA_FULL_SCALE                       (32767.0f)
#define TA_SILENT                           (0.0001f)

/* Overall level of every TANKS sound. */
#define TA_MASTER_LEVEL                     (0.36f)

/* Every generator is high-passed here, keeping the speaker within its excursion limit. */
#define TA_OUTPUT_HIGH_PASS_HZ              (250.0f)

/* Sounds fade as 1 / (1 + distance / TA_DISTANCE_PIXELS), but never below TA_DISTANCE_FLOOR. */
#define TA_DISTANCE_PIXELS                  (350.0f)
#define TA_DISTANCE_FLOOR                   (0.3f)

/* Game speeds that count as full tread activity. */
#define TA_PLAYER_FULL_SPEED                (132.0f)
#define TA_ENEMY_FULL_SPEED                 (76.0f)
#define TA_FULL_TURN_RATE                   (1800.0f)
#define TA_TURN_ACTIVITY                    (0.5f)

/* Treads */
#define TA_TREAD_PLAYER_LEVEL               (0.25f)
#define TA_TREAD_ENEMY_LEVEL                (0.18f)
#define TA_TREAD_SMOOTHING_SECONDS          (0.08f)
#define TA_TREAD_BODY_LEVEL                 (1.0f)
#define TA_TREAD_CLANK_LEVEL                (0.35f)
#define TA_TREAD_CLICK_LEVEL                (0.25f)
#define TA_TREAD_RATTLE_LEVEL               (0.3f)
#define TA_TREAD_ENGINE_LEVEL               (0.35f)
#define TA_TREAD_GRAVEL_LEVEL               (0.15f)
#define TA_TREAD_BODY_DECAY_SECONDS         (0.015f)
#define TA_TREAD_CLANK_DECAY_SECONDS        (0.035f)
#define TA_TREAD_CLICK_DECAY_SECONDS        (0.002f)
#define TA_TREAD_RATTLE_DECAY_SECONDS       (0.0015f)
#define TA_TREAD_RATTLE_MAXIMUM_HZ          (150.0f)
#define TA_TREAD_BODY_LOW_HZ                (350.0f)
#define TA_TREAD_BODY_HIGH_HZ               (1100.0f)
#define TA_TREAD_RATTLE_LOW_HZ              (1000.0f)
#define TA_TREAD_RATTLE_HIGH_HZ             (3000.0f)
#define TA_TREAD_CLICK_HIGH_PASS_HZ         (3000.0f)
#define TA_TREAD_ENGINE_CUTOFF_HZ           (700.0f)
#define TA_TREAD_GRAVEL_CUTOFF_HZ           (700.0f)
#define TA_TREAD_HIGH_PASS_HZ               (350.0f)

/* Rockets in flight */
#define TA_ROCKET_LEVEL                     (0.25f)
#define TA_ROCKET_MOTOR_HZ                  (330.0f)
#define TA_ROCKET_MOTOR_CUTOFF_HZ           (1500.0f)
#define TA_ROCKET_JET_HZ                    (1100.0f)
#define TA_ROCKET_JET_DAMPING               (0.3f)
#define TA_ROCKET_FLUTTER_HZ                (15.0f)

/* Effects */
#define TA_EFFECT_VOICES                    (10U)
#define TA_EVENT_QUEUE_SIZE                 (32U)
#define TA_SHOT_LEVEL                       (0.32f)
#define TA_ENEMY_SHOT_LEVEL                 (0.25f)
#define TA_ENEMY_SHOT_PITCH                 (0.88f)
#define TA_ROCKET_LAUNCH_LEVEL              (0.5f)
#define TA_RICOCHET_LEVEL                   (0.3f)
#define TA_SPENT_LEVEL                      (0.12f)
#define TA_EXPLOSION_LEVEL                  (0.4f)
#define TA_PLAYER_EXPLOSION_STRENGTH        (19U)
#define TA_MINE_DROP_LEVEL                  (0.3f)
#define TA_MINE_ARMED_LEVEL                 (0.22f)
#define TA_MINE_BEEP_LEVEL                  (0.18f)
#define TA_MINE_WARNING_LEVEL               (0.3f)
#define TA_MINE_WARNING_PITCH               (1.25f)
#define TA_MINE_FIZZLE_LEVEL                (0.25f)
#define TA_MINE_BEEP_MILLISECONDS           (1100U)
#define TA_MINE_WARNING_MILLISECONDS        (260U)
#define TA_MINE_WARNING_PIXELS              (90U)

/* Jingles */
#define TA_JINGLE_LEVEL                     (0.25f)
#define TA_JINGLE_VOICES                    (6U)
#define TA_BRASS_ATTACK_SECONDS             (0.015f)
#define TA_BRASS_RELEASE_SECONDS            (0.06f)
#define TA_BRASS_FILTER_SECONDS             (0.25f)
#define TA_BRASS_DETUNE                     (1.004f)
#define TA_BRASS_VIBRATO_HZ                 (5.5f)
#define TA_BRASS_VIBRATO_DEPTH              (0.005f)
#define TA_BRASS_VIBRATO_DELAY_SECONDS      (0.12f)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef enum
{
    TA_SOUND_SHOT,
    TA_SOUND_ROCKET_LAUNCH,
    TA_SOUND_RICOCHET,
    TA_SOUND_SPENT,
    TA_SOUND_EXPLOSION,
    TA_SOUND_MINE_DROP,
    TA_SOUND_MINE_ARMED,
    TA_SOUND_MINE_BEEP,
    TA_SOUND_MINE_FIZZLE
} TanksAudio_SoundTypeDef;

typedef enum
{
    TA_INSTRUMENT_BRASS,
    TA_INSTRUMENT_SNARE,
    TA_INSTRUMENT_BELL
} TanksAudio_InstrumentTypeDef;

/**
 * @brief A one-shot sound posted from the game loop to the effects pool.
 */
typedef struct
{
    TanksAudio_SoundTypeDef Sound;
    uint8_t Strength;
    float Level;
    float Pitch;
} TanksAudio_EventTypeDef;

/**
 * @brief A sine partial that glides towards a target pitch and decays.
 */
typedef struct
{
    float Phase;
    float Hz;
    float TargetHz;
    float Glide;
    float Amplitude;
    float Decay;
} TanksAudio_ToneTypeDef;

/**
 * @brief One effects voice. Each sound uses the shared fields its own way,
 *        as described where it starts.
 */
typedef struct
{
    TanksAudio_SoundTypeDef Sound;
    bool Active;
    uint32_t Sample;
    uint32_t LengthSamples;
    uint32_t StrikeSample;
    float Level;
    float Size;
    float Envelope[3];
    float Decay[3];
    float CutoffHz;
    float CutoffTargetHz;
    float CutoffGlide;
    float Filter[4];
    float Pop;
    TanksAudio_ToneTypeDef Tones[3];
} TanksAudio_VoiceTypeDef;

typedef struct
{
    volatile uint32_t RestartCount;
    volatile uint32_t FirstEvent;
} TanksAudio_EffectsControlTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    TanksAudio_VoiceTypeDef Voices[TA_EFFECT_VOICES];
    float HighPass[2];
} TanksAudio_EffectsStateTypeDef;

/**
 * @brief Values the game loop writes every frame for the ambience.
 */
typedef struct
{
    volatile uint32_t RestartCount;
    volatile float PlayerActivity;
    volatile float EnemyActivity;
    volatile float EnemyLevel;
    volatile float RocketLevel;
} TanksAudio_AmbienceControlTypeDef;

typedef struct
{
    float ClackMinimumHz;
    float ClackMaximumHz;
    float ClankHz;
    float EngineMinimumHz;
    float EngineMaximumHz;
    float Level;
} TanksAudio_TreadConfigTypeDef;

/**
 * @brief Filter and decay coefficients shared by every tread, set per audio block.
 */
typedef struct
{
    float Smoothing;
    float BodyDecay;
    float ClickDecay;
    float RattleDecay;
    float BodyLow;
    float BodyHigh;
    float RattleLow;
    float RattleHigh;
    float ClickHighPass;
    float Engine;
    float Gravel;
    float HighPass;
} TanksAudio_TreadCoefficientsTypeDef;

typedef struct
{
    float Activity;
    float Level;
    float ClackPhase;
    uint32_t ClackCount;
    float BodyEnvelope;
    float BodyLow[2];
    float BodyFloor;
    float ClickEnvelope;
    float ClickLow;
    float RattleEnvelope;
    float RattleLow[2];
    float RattleFloor;
    float EnginePhase;
    float EngineLow[2];
    float GravelLow[2];
    float HighPass[2];
    TanksAudio_ToneTypeDef Clank[2];
} TanksAudio_TreadTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    TanksAudio_TreadTypeDef Player;
    TanksAudio_TreadTypeDef Enemies;
    float Rocket;
    float RocketPhase;
    float RocketLow;
    float RocketBand[2];
    float Flutter;
    float HighPass[2];
} TanksAudio_AmbienceStateTypeDef;

/**
 * @brief One jingle note. Times are in seconds.
 */
typedef struct
{
    float StartSeconds;
    float Hz;
    float Seconds;
    TanksAudio_InstrumentTypeDef Instrument;
    float Level;
} TanksAudio_NoteTypeDef;

typedef struct
{
    const TanksAudio_NoteTypeDef *Notes;
    uint32_t NoteCount;
} TanksAudio_JingleDefinitionTypeDef;

typedef struct
{
    volatile uint32_t RestartCount;
    volatile uint32_t Jingle;
} TanksAudio_JingleControlTypeDef;

typedef struct
{
    bool Active;
    const TanksAudio_NoteTypeDef *Note;
    uint32_t Sample;
    uint32_t HoldSamples;
    float Amplitude;
    float FilterEnvelope;
    float Phase[2];
    float Low[2];
    float VibratoPhase;
    float ModulationIndex;
    float ToneHz;
    float ToneAmplitude;
} TanksAudio_JingleVoiceTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    const TanksAudio_JingleDefinitionTypeDef *Jingle;
    uint32_t Sample;
    uint32_t NextNote;
    TanksAudio_JingleVoiceTypeDef Voices[TA_JINGLE_VOICES];
    float HighPass[2];
} TanksAudio_JingleStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Jingles                                                                    */
/* -------------------------------------------------------------------------- */

#define TA_C5 (523.25f)
#define TA_E5 (659.26f)
#define TA_G4 (392.00f)
#define TA_G5 (783.99f)
#define TA_A5 (880.00f)
#define TA_B5 (987.77f)
#define TA_C6 (1046.50f)
#define TA_C7 (2093.00f)

/* New room: a snare roll building into a short call to arms. */
static const TanksAudio_NoteTypeDef TanksAudio_WaveIntroNotes[] = {
    { 0.00f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.35f },
    { 0.16f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.38f },
    { 0.30f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.42f },
    { 0.42f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.46f },
    { 0.52f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.5f },
    { 0.60f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.55f },
    { 0.67f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.6f },
    { 0.73f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.65f },
    { 0.78f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.7f },
    { 0.83f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.75f },
    { 0.87f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.8f },
    { 0.91f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.85f },
    { 1.05f, TA_G4, 0.1f, TA_INSTRUMENT_BRASS, 0.8f },
    { 1.20f, TA_G4, 0.1f, TA_INSTRUMENT_BRASS, 0.8f },
    { 1.35f, TA_C5, 0.55f, TA_INSTRUMENT_BRASS, 1.0f },
    { 1.35f, TA_E5, 0.55f, TA_INSTRUMENT_BRASS, 0.6f },
    { 1.35f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 1.0f },
};

/* Arena clear: a quick rising arpeggio landing on a bright chord. */
static const TanksAudio_NoteTypeDef TanksAudio_ArenaClearNotes[] = {
    { 0.00f, TA_C5, 0.09f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.10f, TA_E5, 0.09f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.20f, TA_G5, 0.09f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.30f, TA_C6, 0.55f, TA_INSTRUMENT_BRASS, 1.0f },
    { 0.30f, TA_G5, 0.55f, TA_INSTRUMENT_BRASS, 0.55f },
    { 0.30f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.8f },
    { 0.30f, TA_C7, 0.6f, TA_INSTRUMENT_BELL, 0.35f },
};

/* Tank lost: a sagging "wah, wah, wah, waah" after the explosion clears. */
static const TanksAudio_NoteTypeDef TanksAudio_TankLostNotes[] = {
    { 0.50f, 659.26f, 0.2f, TA_INSTRUMENT_BRASS, 0.7f },
    { 0.75f, 622.25f, 0.2f, TA_INSTRUMENT_BRASS, 0.7f },
    { 1.00f, 587.33f, 0.2f, TA_INSTRUMENT_BRASS, 0.7f },
    { 1.25f, 554.37f, 0.6f, TA_INSTRUMENT_BRASS, 0.75f },
};

/* Game over: a slow minor descent settling on an uneasy chord. */
static const TanksAudio_NoteTypeDef TanksAudio_GameOverNotes[] = {
    { 0.60f, 659.26f, 0.3f, TA_INSTRUMENT_BRASS, 0.8f },
    { 1.00f, 523.25f, 0.3f, TA_INSTRUMENT_BRASS, 0.8f },
    { 1.40f, 440.00f, 0.3f, TA_INSTRUMENT_BRASS, 0.8f },
    { 1.80f, 415.30f, 0.9f, TA_INSTRUMENT_BRASS, 0.85f },
    { 1.80f, 329.63f, 0.9f, TA_INSTRUMENT_BRASS, 0.45f },
    { 1.80f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.5f },
};

/* Victory: a rising call, then a lifted answer resolving on a full chord. */
static const TanksAudio_NoteTypeDef TanksAudio_VictoryNotes[] = {
    { 0.00f, TA_C5, 0.1f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.12f, TA_E5, 0.1f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.24f, TA_G5, 0.1f, TA_INSTRUMENT_BRASS, 0.8f },
    { 0.36f, TA_C6, 0.35f, TA_INSTRUMENT_BRASS, 1.0f },
    { 0.36f, TA_G5, 0.35f, TA_INSTRUMENT_BRASS, 0.5f },
    { 0.36f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 0.8f },
    { 0.80f, TA_A5, 0.12f, TA_INSTRUMENT_BRASS, 0.85f },
    { 0.95f, TA_B5, 0.12f, TA_INSTRUMENT_BRASS, 0.85f },
    { 1.10f, TA_C6, 0.9f, TA_INSTRUMENT_BRASS, 1.0f },
    { 1.10f, TA_G5, 0.9f, TA_INSTRUMENT_BRASS, 0.55f },
    { 1.10f, TA_E5, 0.9f, TA_INSTRUMENT_BRASS, 0.45f },
    { 1.10f, 0.0f, 0.1f, TA_INSTRUMENT_SNARE, 1.0f },
    { 1.10f, TA_C7, 0.8f, TA_INSTRUMENT_BELL, 0.35f },
};

#define TA_JINGLE(Notes) { (Notes), (uint32_t)(sizeof(Notes) / sizeof((Notes)[0])) }

static const TanksAudio_JingleDefinitionTypeDef TanksAudio_Jingles[] = {
    TA_JINGLE(TanksAudio_WaveIntroNotes),
    TA_JINGLE(TanksAudio_ArenaClearNotes),
    TA_JINGLE(TanksAudio_TankLostNotes),
    TA_JINGLE(TanksAudio_GameOverNotes),
    TA_JINGLE(TanksAudio_VictoryNotes),
};

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const TanksAudio_TreadConfigTypeDef TanksAudio_PlayerTread = { 5.0f, 16.0f, 640.0f, 42.0f, 70.0f, TA_TREAD_PLAYER_LEVEL };
static const TanksAudio_TreadConfigTypeDef TanksAudio_EnemyTread = { 6.0f, 18.0f, 900.0f, 60.0f, 95.0f, TA_TREAD_ENEMY_LEVEL };

static TanksAudio_AmbienceControlTypeDef TanksAudio_AmbienceControl;
static TanksAudio_EffectsControlTypeDef TanksAudio_EffectsControl;
static TanksAudio_JingleControlTypeDef TanksAudio_JingleControl;

static TanksAudio_AmbienceStateTypeDef TanksAudio_Ambience;
static TanksAudio_EffectsStateTypeDef TanksAudio_Effects;
static TanksAudio_JingleStateTypeDef TanksAudio_JinglePlayer;

/* Single-producer (game loop), single-consumer (audio) event queue. */
static TanksAudio_EventTypeDef TanksAudio_Events[TA_EVENT_QUEUE_SIZE];
static uint32_t TanksAudio_EventHead;
static uint32_t TanksAudio_EventTail;

/* Game-loop bookkeeping for mine beeps. */
static uint16_t TanksAudio_MineBeepMilliseconds[TANKS_MAX_MINES];
static bool TanksAudio_MineTracked[TANKS_MAX_MINES];

/* Each tank's last position and heading, slot 0 for the player and 1 onwards for enemies. */
static Tanks_VectorTypeDef TanksAudio_LastPosition[TANKS_MAX_ENEMIES + 1U];
static int16_t TanksAudio_LastHeading[TANKS_MAX_ENEMIES + 1U];
static bool TanksAudio_LastValid[TANKS_MAX_ENEMIES + 1U];

/* -------------------------------------------------------------------------- */
/* Synthesis helpers                                                          */
/* -------------------------------------------------------------------------- */

static float TanksAudio_Noise(uint32_t *State)
{
    uint32_t Value = *State;

    Value ^= Value << 13U;
    Value ^= Value >> 17U;
    Value ^= Value << 5U;
    *State = Value;

    return ((float)Value / 2147483648.0f) - 1.0f;
}

static float TanksAudio_RandomRange(uint32_t *State, float Minimum, float Maximum)
{
    return Minimum + ((Maximum - Minimum) * (0.5f + (0.5f * TanksAudio_Noise(State))));
}

static float TanksAudio_LowPassCoefficient(float CutoffHz)
{
    return 1.0f - expf(-TA_TWO_PI * CutoffHz * TA_SAMPLE_PERIOD);
}

/* Cheap low-pass coefficient for cutoffs that change every sample; close below a few kHz, darker above. */
static float TanksAudio_SweepCoefficient(float CutoffHz)
{
    const float Omega = TA_TWO_PI * CutoffHz * TA_SAMPLE_PERIOD;

    return Omega / (1.0f + Omega);
}

/* Per-sample multiplier that decays to 1/e in Seconds; 0 stops at once. */
static float TanksAudio_DecayCoefficient(float Seconds)
{
    return (Seconds > 0.0f) ? expf(-1.0f / (Seconds * TA_SAMPLE_RATE)) : 0.0f;
}

static uint32_t TanksAudio_Seconds(float Seconds)
{
    return (uint32_t)(Seconds * TA_SAMPLE_RATE);
}

/*
 * Fast sine of a phase in turns (1.0 = one cycle), within 0.0002 of the C
 * library sine at a fraction of the cost: fold to a quarter cycle, then a
 * 7th-order polynomial.
 */
static float TanksAudio_Sine(float Phase)
{
    float Turns = Phase - floorf(Phase) - 0.5f;
    float X;
    float Square;

    if(Turns > 0.25f)
    {
        Turns = 0.5f - Turns;
    }
    else if(Turns < -0.25f)
    {
        Turns = -0.5f - Turns;
    }

    X = TA_TWO_PI * Turns;
    Square = X * X;

    return -X * (1.0f - (Square * (0.16666667f - (Square * (0.0083333333f - (Square * 0.00019841270f))))));
}

static float TanksAudio_SoftClip(float Value)
{
    return Value / (1.0f + fabsf(Value));
}

static void TanksAudio_StartTone(TanksAudio_ToneTypeDef *Tone, float StartHz, float EndHz, float GlideSeconds, float Amplitude, float DecaySeconds)
{
    Tone->Phase = 0.0f;
    Tone->Hz = StartHz;
    Tone->TargetHz = EndHz;
    Tone->Glide = 1.0f - TanksAudio_DecayCoefficient(GlideSeconds);
    Tone->Amplitude = Amplitude;
    Tone->Decay = TanksAudio_DecayCoefficient(DecaySeconds);
}

static float TanksAudio_RunTone(TanksAudio_ToneTypeDef *Tone)
{
    float Output;

    if(Tone->Amplitude < TA_SILENT)
    {
        return 0.0f;
    }

    Output = TanksAudio_Sine(Tone->Phase) * Tone->Amplitude;
    Tone->Hz += Tone->Glide * (Tone->TargetHz - Tone->Hz);
    Tone->Phase += Tone->Hz * TA_SAMPLE_PERIOD;
    Tone->Phase -= floorf(Tone->Phase);
    Tone->Amplitude *= Tone->Decay;

    return Output;
}

/* Smooths the jump in a saw wave so high notes don't alias. */
static float TanksAudio_PolyBlep(float Phase, float Step)
{
    float Correction = 0.0f;

    if(Phase < Step)
    {
        const float T = Phase / Step;

        Correction = T + T - (T * T) - 1.0f;
    }
    else if(Phase > (1.0f - Step))
    {
        const float T = (Phase - 1.0f) / Step;

        Correction = (T * T) + T + T + 1.0f;
    }

    return Correction;
}

/*
 * Resonant band-pass (state-variable filter). Frequency is 2 * pi * f / fs;
 * keep f below about 4 kHz. Lower damping rings more narrowly.
 */
static float TanksAudio_BandPass(float *State, float Input, float Frequency, float Damping)
{
    float High;

    State[0] += Frequency * State[1];
    High = Input - State[0] - (Damping * State[1]);
    State[1] += Frequency * High;

    return State[1];
}

/* Two-pole high-pass shared by every generator's output. */
static float TanksAudio_HighPass(float *State, float Input, float Coefficient)
{
    float Output = Input;

    State[0] += Coefficient * (Output - State[0]);
    Output -= State[0];
    State[1] += Coefficient * (Output - State[1]);
    Output -= State[1];

    return Output;
}

static Audio_SampleTypeDef TanksAudio_ToSample(float Value)
{
    Value *= TA_MASTER_LEVEL;

    if(Value > 1.0f)
    {
        Value = 1.0f;
    }
    else if(Value < -1.0f)
    {
        Value = -1.0f;
    }

    return (Audio_SampleTypeDef)(Value * TA_FULL_SCALE);
}

/* -------------------------------------------------------------------------- */
/* Ambience: treads and rockets                                               */
/* -------------------------------------------------------------------------- */

/*
 * A moving tank. Each track link lands with a weighty "chuk" of mid-band
 * noise, a metal clank of two inharmonic partials, and a small click on top.
 * Between links the track chain rattles, thicker the faster it runs, over an
 * engine buzz and rolling gravel. Links alternate slightly in strength and
 * land a little irregularly, like real tracks.
 */
static float TanksAudio_RunTread(TanksAudio_TreadTypeDef *Tread, const TanksAudio_TreadConfigTypeDef *Config, uint32_t *Random,
                                 float ActivityTarget, float LevelTarget, const TanksAudio_TreadCoefficientsTypeDef *Coefficients)
{
    const float Noise = TanksAudio_Noise(Random);
    float Body;
    float Click;
    float Rattle;
    float Engine;
    float Gravel;

    Tread->Activity += Coefficients->Smoothing * (ActivityTarget - Tread->Activity);
    Tread->Level += Coefficients->Smoothing * (LevelTarget - Tread->Level);

    if((Tread->Level < TA_SILENT) && (Tread->BodyEnvelope < TA_SILENT) && (Tread->Clank[0].Amplitude < TA_SILENT))
    {
        return 0.0f;
    }

    /* Track links: the rate follows the speed. */
    Tread->ClackPhase += (Config->ClackMinimumHz + ((Config->ClackMaximumHz - Config->ClackMinimumHz) * Tread->Activity)) * TA_SAMPLE_PERIOD;

    if(Tread->ClackPhase >= 1.0f)
    {
        const float Strength = TanksAudio_RandomRange(Random, 0.6f, 1.0f) * (((Tread->ClackCount & 1U) != 0U) ? 0.75f : 1.0f);
        const float ClankHz = Config->ClankHz * TanksAudio_RandomRange(Random, 0.97f, 1.03f);

        Tread->ClackPhase -= 1.0f + TanksAudio_RandomRange(Random, 0.0f, 0.12f);
        Tread->ClackCount++;
        Tread->BodyEnvelope = Strength;
        Tread->ClickEnvelope = Strength;
        TanksAudio_StartTone(&Tread->Clank[0], ClankHz, ClankHz, 0.0f, Strength * TA_TREAD_CLANK_LEVEL, TA_TREAD_CLANK_DECAY_SECONDS);
        TanksAudio_StartTone(&Tread->Clank[1], ClankHz * 2.43f, ClankHz * 2.43f, 0.0f, Strength * TA_TREAD_CLANK_LEVEL * 0.5f,
                             TA_TREAD_CLANK_DECAY_SECONDS * 0.6f);
    }

    /* "Chuk": the link's weight, a burst of mid-band noise (two-pole, so no hiss above it). */
    Tread->BodyLow[0] += Coefficients->BodyHigh * (Noise - Tread->BodyLow[0]);
    Tread->BodyLow[1] += Coefficients->BodyHigh * (Tread->BodyLow[0] - Tread->BodyLow[1]);
    Tread->BodyFloor += Coefficients->BodyLow * (Tread->BodyLow[1] - Tread->BodyFloor);
    Body = (Tread->BodyLow[1] - Tread->BodyFloor) * Tread->BodyEnvelope * 5.0f * TA_TREAD_BODY_LEVEL;
    Tread->BodyEnvelope *= Coefficients->BodyDecay;

    /* Click: the sharp edge of the link landing. */
    Tread->ClickLow += Coefficients->ClickHighPass * (Noise - Tread->ClickLow);
    Click = (Noise - Tread->ClickLow) * Tread->ClickEnvelope * TA_TREAD_CLICK_LEVEL;
    Tread->ClickEnvelope *= Coefficients->ClickDecay;

    /* Rattle: tiny random ticks of the chain, denser with speed. */
    if((0.5f + (0.5f * TanksAudio_Noise(Random))) < (TA_TREAD_RATTLE_MAXIMUM_HZ * Tread->Activity * TA_SAMPLE_PERIOD))
    {
        Tread->RattleEnvelope = TanksAudio_RandomRange(Random, 0.3f, 1.0f);
    }

    Tread->RattleLow[0] += Coefficients->RattleHigh * ((Noise * Tread->RattleEnvelope) - Tread->RattleLow[0]);
    Tread->RattleLow[1] += Coefficients->RattleHigh * (Tread->RattleLow[0] - Tread->RattleLow[1]);
    Tread->RattleFloor += Coefficients->RattleLow * (Tread->RattleLow[1] - Tread->RattleFloor);
    Rattle = (Tread->RattleLow[1] - Tread->RattleFloor) * 1.5f * TA_TREAD_RATTLE_LEVEL;
    Tread->RattleEnvelope *= Coefficients->RattleDecay;

    /* Engine: a low saw whose upper harmonics give the buzz. */
    Tread->EnginePhase += (Config->EngineMinimumHz + ((Config->EngineMaximumHz - Config->EngineMinimumHz) * Tread->Activity)) * TA_SAMPLE_PERIOD;
    Tread->EnginePhase -= (Tread->EnginePhase >= 1.0f) ? 1.0f : 0.0f;
    Tread->EngineLow[0] += Coefficients->Engine * (((2.0f * Tread->EnginePhase) - 1.0f) - Tread->EngineLow[0]);
    Tread->EngineLow[1] += Coefficients->Engine * (Tread->EngineLow[0] - Tread->EngineLow[1]);
    Engine = Tread->EngineLow[1] * TA_TREAD_ENGINE_LEVEL * (0.5f + (0.5f * Tread->Activity));

    /* Gravel under the tracks: a soft two-pole rumble. */
    Tread->GravelLow[0] += Coefficients->Gravel * (Noise - Tread->GravelLow[0]);
    Tread->GravelLow[1] += Coefficients->Gravel * (Tread->GravelLow[0] - Tread->GravelLow[1]);
    Gravel = Tread->GravelLow[1] * 4.0f * TA_TREAD_GRAVEL_LEVEL * Tread->Activity;

    /* The engine and gravel are mostly low rumble; keep only what the speaker can carry. */
    return Tread->Level * Config->Level *
           (Body + Click + Rattle + TanksAudio_RunTone(&Tread->Clank[0]) + TanksAudio_RunTone(&Tread->Clank[1]) +
            TanksAudio_HighPass(Tread->HighPass, Engine + Gravel, Coefficients->HighPass));
}

static void TanksAudio_GenerateAmbience(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context)
{
    TanksAudio_AmbienceStateTypeDef *State = &TanksAudio_Ambience;
    const float Smoothing = 1.0f - TanksAudio_DecayCoefficient(TA_TREAD_SMOOTHING_SECONDS);
    const float RocketMotorCoefficient = TanksAudio_LowPassCoefficient(TA_ROCKET_MOTOR_CUTOFF_HZ);
    const float RocketJetFrequency = TA_TWO_PI * TA_ROCKET_JET_HZ * TA_SAMPLE_PERIOD;
    const float FlutterCoefficient = TanksAudio_LowPassCoefficient(TA_ROCKET_FLUTTER_HZ);
    const float HighPassCoefficient = TanksAudio_LowPassCoefficient(TA_OUTPUT_HIGH_PASS_HZ);
    const float PlayerActivity = TanksAudio_AmbienceControl.PlayerActivity;
    const float EnemyActivity = TanksAudio_AmbienceControl.EnemyActivity;
    const float EnemyLevel = TanksAudio_AmbienceControl.EnemyLevel;
    const float RocketLevel = TanksAudio_AmbienceControl.RocketLevel;
    const float PlayerLevel = (PlayerActivity * 3.0f > 1.0f) ? 1.0f : (PlayerActivity * 3.0f);
    const TanksAudio_TreadCoefficientsTypeDef Coefficients =
    {
        .Smoothing = Smoothing,
        .BodyDecay = TanksAudio_DecayCoefficient(TA_TREAD_BODY_DECAY_SECONDS),
        .ClickDecay = TanksAudio_DecayCoefficient(TA_TREAD_CLICK_DECAY_SECONDS),
        .RattleDecay = TanksAudio_DecayCoefficient(TA_TREAD_RATTLE_DECAY_SECONDS),
        .BodyLow = TanksAudio_LowPassCoefficient(TA_TREAD_BODY_LOW_HZ),
        .BodyHigh = TanksAudio_LowPassCoefficient(TA_TREAD_BODY_HIGH_HZ),
        .RattleLow = TanksAudio_LowPassCoefficient(TA_TREAD_RATTLE_LOW_HZ),
        .RattleHigh = TanksAudio_LowPassCoefficient(TA_TREAD_RATTLE_HIGH_HZ),
        .ClickHighPass = TanksAudio_LowPassCoefficient(TA_TREAD_CLICK_HIGH_PASS_HZ),
        .Engine = TanksAudio_LowPassCoefficient(TA_TREAD_ENGINE_CUTOFF_HZ),
        .Gravel = TanksAudio_LowPassCoefficient(TA_TREAD_GRAVEL_CUTOFF_HZ),
        .HighPass = TanksAudio_LowPassCoefficient(TA_TREAD_HIGH_PASS_HZ)
    };

    (void)Context;

    if(State->RestartCount != TanksAudio_AmbienceControl.RestartCount)
    {
        *State = (TanksAudio_AmbienceStateTypeDef){ 0 };
        State->RestartCount = TanksAudio_AmbienceControl.RestartCount;
        State->Random = 0x6D2B79F5U ^ (State->RestartCount * 0x9E3779B9U);
    }

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        float Output;

        Output = TanksAudio_RunTread(&State->Player, &TanksAudio_PlayerTread, &State->Random, PlayerActivity, PlayerLevel, &Coefficients);
        Output += TanksAudio_RunTread(&State->Enemies, &TanksAudio_EnemyTread, &State->Random, EnemyActivity, EnemyLevel, &Coefficients);

        /* Rockets: a fluttering motor drone and jet whistle, louder the closer they fly. */
        State->Rocket += Smoothing * (RocketLevel - State->Rocket);

        if(State->Rocket > TA_SILENT)
        {
            const float Noise = TanksAudio_Noise(&State->Random);
            float Jet;

            State->Flutter += FlutterCoefficient * (TanksAudio_Noise(&State->Random) - State->Flutter);
            State->RocketPhase += TA_ROCKET_MOTOR_HZ * (1.0f + (0.3f * State->Flutter)) * TA_SAMPLE_PERIOD;
            State->RocketPhase -= (State->RocketPhase >= 1.0f) ? 1.0f : 0.0f;
            State->RocketLow += RocketMotorCoefficient * (((2.0f * State->RocketPhase) - 1.0f) - State->RocketLow);
            Jet = TanksAudio_BandPass(State->RocketBand, Noise, RocketJetFrequency, TA_ROCKET_JET_DAMPING);
            Output += ((0.5f * State->RocketLow) + (0.8f * Jet)) * State->Rocket * TA_ROCKET_LEVEL * (1.0f + (3.0f * State->Flutter));
        }

        Samples[Index] = TanksAudio_ToSample(TanksAudio_HighPass(State->HighPass, Output, HighPassCoefficient));
    }
}

/* -------------------------------------------------------------------------- */
/* Effects: starting sounds                                                   */
/* -------------------------------------------------------------------------- */

/*
 * Shot: a sharp crack, a "thoomp" diving from 900 to 260 Hz, and a puff of
 * muzzle blast whose brightness closes quickly.
 * Envelope 0: crack. Envelope 1: blast. Tone 0: thoomp.
 */
static void TanksAudio_StartShot(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random, float Pitch)
{
    const float Variation = Pitch * TanksAudio_RandomRange(Random, 0.95f, 1.05f);

    Voice->LengthSamples = TanksAudio_Seconds(0.35f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.004f);
    Voice->Envelope[1] = 1.0f;
    Voice->Decay[1] = TanksAudio_DecayCoefficient(0.06f);
    Voice->CutoffHz = 5000.0f * Variation;
    Voice->CutoffTargetHz = 500.0f;
    Voice->CutoffGlide = 1.0f - TanksAudio_DecayCoefficient(0.04f);
    TanksAudio_StartTone(&Voice->Tones[0], 900.0f * Variation, 260.0f * Variation, 0.025f, 1.0f, 0.07f);
}

/*
 * Rocket launch, "FWOOMP-shhh": an ignition thump, a buzzing motor roar
 * that drops in pitch as the rocket pulls away, and a resonant jet whoosh
 * sweeping down, with a burst of crackle as it lights.
 * Envelopes 0 and 1: motor rise and fall. Tone 0: ignition thump.
 * Tone 1: motor pitch (its phase drives a saw, not a sine).
 */
static void TanksAudio_StartRocketLaunch(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random)
{
    const float Variation = TanksAudio_RandomRange(Random, 0.93f, 1.07f);

    Voice->LengthSamples = TanksAudio_Seconds(0.6f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.012f);
    Voice->Envelope[1] = 1.0f;
    Voice->Decay[1] = TanksAudio_DecayCoefficient(0.25f);
    Voice->CutoffHz = 2200.0f * Variation;
    Voice->CutoffTargetHz = 700.0f;
    Voice->CutoffGlide = 1.0f - TanksAudio_DecayCoefficient(0.15f);
    TanksAudio_StartTone(&Voice->Tones[0], 700.0f * Variation, 250.0f * Variation, 0.02f, 1.0f, 0.05f);
    TanksAudio_StartTone(&Voice->Tones[1], 520.0f * Variation, 260.0f * Variation, 0.18f, 0.0f, 0.0f);
}

/*
 * Ricochet: a tick off the wall and three bright partials that bend down as
 * the bullet sings away, "p-tiu".
 * Envelope 0: tick. Tones: the partials.
 */
static void TanksAudio_StartRicochet(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random)
{
    const float BaseHz = 2200.0f * TanksAudio_RandomRange(Random, 0.85f, 1.2f);

    Voice->LengthSamples = TanksAudio_Seconds(0.3f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.003f);
    TanksAudio_StartTone(&Voice->Tones[0], BaseHz, BaseHz * 0.8f, 0.05f, 1.0f, 0.12f);
    TanksAudio_StartTone(&Voice->Tones[1], BaseHz * 1.51f, BaseHz * 1.21f, 0.05f, 0.5f, 0.08f);
    TanksAudio_StartTone(&Voice->Tones[2], BaseHz * 2.37f, BaseHz * 1.9f, 0.05f, 0.3f, 0.05f);
}

/*
 * Spent bullet: a soft puff and a falling blip.
 * Envelope 0: puff. Tone 0: blip.
 */
static void TanksAudio_StartSpent(TanksAudio_VoiceTypeDef *Voice)
{
    Voice->LengthSamples = TanksAudio_Seconds(0.15f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.035f);
    TanksAudio_StartTone(&Voice->Tones[0], 900.0f, 450.0f, 0.02f, 0.3f, 0.03f);
}

/*
 * Explosion, scaled by strength: a crack, a roaring body whose brightness
 * closes as it rolls out, a heavy thump, crackling debris, and for bigger
 * blasts a couple of metal pieces clanking down a moment later.
 * Envelope 0: crack. Envelope 1: body. Envelope 2: body attack.
 * Tone 0: thump. Tones 1 and 2: metal debris, struck at StrikeSample.
 */
static void TanksAudio_StartExplosion(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random, uint8_t Strength)
{
    const float Size = (Strength >= TA_PLAYER_EXPLOSION_STRENGTH) ? 1.0f : ((float)Strength / (float)TA_PLAYER_EXPLOSION_STRENGTH);
    const float Variation = TanksAudio_RandomRange(Random, 0.9f, 1.1f);

    Voice->Size = (Size < 0.3f) ? 0.3f : Size;
    Voice->LengthSamples = TanksAudio_Seconds(0.7f + (1.5f * Voice->Size));
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.004f);
    Voice->Envelope[1] = 1.0f;
    Voice->Decay[1] = TanksAudio_DecayCoefficient(0.2f + (0.6f * Voice->Size));
    Voice->Envelope[2] = 1.0f;
    Voice->Decay[2] = TanksAudio_DecayCoefficient(0.004f);
    Voice->CutoffHz = (1500.0f + (2000.0f * Voice->Size)) * Variation;
    Voice->CutoffTargetHz = 300.0f + (100.0f * Voice->Size);
    Voice->CutoffGlide = 1.0f - TanksAudio_DecayCoefficient(0.08f + (0.2f * Voice->Size));
    TanksAudio_StartTone(&Voice->Tones[0], 420.0f * Variation, 220.0f * Variation, 0.05f + (0.05f * Voice->Size), 1.0f, 0.08f + (0.15f * Voice->Size));

    if(Voice->Size >= 0.5f)
    {
        Voice->StrikeSample = TanksAudio_Seconds(TanksAudio_RandomRange(Random, 0.06f, 0.21f));
        Voice->Tones[1].Hz = 700.0f * Variation;
        Voice->Tones[2].Hz = 1730.0f * Variation;
    }
}

/*
 * Mine drop: a heavy "thunk" as it hits the floor and a sharp metal click.
 * Envelope 0: click noise. Tone 0: thunk. Tones 1 and 2: metal.
 */
static void TanksAudio_StartMineDrop(TanksAudio_VoiceTypeDef *Voice)
{
    Voice->LengthSamples = TanksAudio_Seconds(0.3f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.004f);
    TanksAudio_StartTone(&Voice->Tones[0], 620.0f, 300.0f, 0.03f, 1.0f, 0.06f);
    TanksAudio_StartTone(&Voice->Tones[1], 3100.0f, 3100.0f, 0.0f, 0.25f, 0.02f);
    TanksAudio_StartTone(&Voice->Tones[2], 4700.0f, 4700.0f, 0.0f, 0.12f, 0.012f);
}

/*
 * Mine armed: a rising two-tone "bi-dip".
 * Tone 0: first bip. Tone 1: second bip, struck at StrikeSample.
 */
static void TanksAudio_StartMineArmed(TanksAudio_VoiceTypeDef *Voice)
{
    Voice->LengthSamples = TanksAudio_Seconds(0.2f);
    Voice->StrikeSample = TanksAudio_Seconds(0.06f);
    TanksAudio_StartTone(&Voice->Tones[0], 1750.0f, 1750.0f, 0.0f, 1.0f, 0.025f);
    TanksAudio_StartTone(&Voice->Tones[1], 2340.0f, 2340.0f, 0.0f, 0.0f, 0.03f);
}

/*
 * Mine beep: one short blip; a mine close to the player beeps higher.
 * Tone 0: the blip.
 */
static void TanksAudio_StartMineBeep(TanksAudio_VoiceTypeDef *Voice, float Pitch)
{
    Voice->LengthSamples = TanksAudio_Seconds(0.08f);
    TanksAudio_StartTone(&Voice->Tones[0], 2100.0f * Pitch, 2100.0f * Pitch, 0.0f, 1.0f, 0.02f);
}

/*
 * Mine fizzle: a hiss of escaping gas and a sinking whistle.
 * Envelope 0: hiss. Tone 0: whistle.
 */
static void TanksAudio_StartMineFizzle(TanksAudio_VoiceTypeDef *Voice)
{
    Voice->LengthSamples = TanksAudio_Seconds(0.4f);
    Voice->Envelope[0] = 1.0f;
    Voice->Decay[0] = TanksAudio_DecayCoefficient(0.12f);
    TanksAudio_StartTone(&Voice->Tones[0], 1200.0f, 350.0f, 0.1f, 0.25f, 0.12f);
}

static void TanksAudio_StartVoice(TanksAudio_EffectsStateTypeDef *State, const TanksAudio_EventTypeDef *Event)
{
    TanksAudio_VoiceTypeDef *Voice = &State->Voices[0];

    /* A free voice, or else the one that has played longest. */
    for(uint32_t Index = 0U; Index < TA_EFFECT_VOICES; Index++)
    {
        if(!State->Voices[Index].Active)
        {
            Voice = &State->Voices[Index];
            break;
        }

        if(State->Voices[Index].Sample > Voice->Sample)
        {
            Voice = &State->Voices[Index];
        }
    }

    *Voice = (TanksAudio_VoiceTypeDef){ 0 };
    Voice->Sound = Event->Sound;
    Voice->Level = Event->Level;
    Voice->Active = true;

    switch(Event->Sound)
    {
        case TA_SOUND_SHOT:
            TanksAudio_StartShot(Voice, &State->Random, Event->Pitch);
            break;

        case TA_SOUND_ROCKET_LAUNCH:
            TanksAudio_StartRocketLaunch(Voice, &State->Random);
            break;

        case TA_SOUND_RICOCHET:
            TanksAudio_StartRicochet(Voice, &State->Random);
            break;

        case TA_SOUND_SPENT:
            TanksAudio_StartSpent(Voice);
            break;

        case TA_SOUND_EXPLOSION:
            TanksAudio_StartExplosion(Voice, &State->Random, Event->Strength);
            break;

        case TA_SOUND_MINE_DROP:
            TanksAudio_StartMineDrop(Voice);
            break;

        case TA_SOUND_MINE_ARMED:
            TanksAudio_StartMineArmed(Voice);
            break;

        case TA_SOUND_MINE_BEEP:
            TanksAudio_StartMineBeep(Voice, Event->Pitch);
            break;

        case TA_SOUND_MINE_FIZZLE:
            TanksAudio_StartMineFizzle(Voice);
            break;

        default:
            Voice->Active = false;
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* Effects: playing sounds                                                    */
/* -------------------------------------------------------------------------- */

static float TanksAudio_RunShot(TanksAudio_VoiceTypeDef *Voice, float Noise)
{
    float Crack;
    float Coefficient;
    float Blast;

    Crack = Noise * Voice->Envelope[0];
    Voice->Envelope[0] *= Voice->Decay[0];
    Voice->Filter[0] += 0.4f * (Crack - Voice->Filter[0]);
    Crack -= Voice->Filter[0];

    Voice->CutoffHz += Voice->CutoffGlide * (Voice->CutoffTargetHz - Voice->CutoffHz);
    Coefficient = TanksAudio_SweepCoefficient(Voice->CutoffHz);
    Voice->Filter[1] += Coefficient * (Noise - Voice->Filter[1]);
    Voice->Filter[2] += Coefficient * (Voice->Filter[1] - Voice->Filter[2]);
    Blast = Voice->Filter[2] * 3.0f * Voice->Envelope[1];
    Voice->Envelope[1] *= Voice->Decay[1];

    return (0.5f * Crack) + (0.8f * TanksAudio_SoftClip(4.0f * TanksAudio_RunTone(&Voice->Tones[0]))) + (0.6f * Blast);
}

static float TanksAudio_RunRocketLaunch(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random, float Noise)
{
    const float Motor = Voice->Envelope[1] - Voice->Envelope[0];
    TanksAudio_ToneTypeDef *Pitch = &Voice->Tones[1];
    float Roar;
    float Jet;
    float Crackle;

    Voice->Envelope[0] *= Voice->Decay[0];
    Voice->Envelope[1] *= Voice->Decay[1];

    /* Motor roar: a buzzing saw, roughened by the burn, falling in pitch. */
    Pitch->Hz += Pitch->Glide * (Pitch->TargetHz - Pitch->Hz);
    Pitch->Phase += Pitch->Hz * TA_SAMPLE_PERIOD;
    Pitch->Phase -= (Pitch->Phase >= 1.0f) ? 1.0f : 0.0f;
    Voice->Filter[2] += 0.3f * (((2.0f * Pitch->Phase) - 1.0f) - Voice->Filter[2]);
    Roar = Voice->Filter[2] * Motor * (1.0f + (0.5f * Noise));

    /* Jet: a resonant band of noise sweeping down as the rocket leaves. */
    Voice->CutoffHz += Voice->CutoffGlide * (Voice->CutoffTargetHz - Voice->CutoffHz);
    Jet = TanksAudio_BandPass(Voice->Filter, Noise, TA_TWO_PI * Voice->CutoffHz * TA_SAMPLE_PERIOD, 0.25f) * Motor * 0.6f;

    /* Ignition crackle: sparse pops while it lights. */
    if((0.5f + (0.5f * TanksAudio_Noise(Random))) < ((150.0f * TA_SAMPLE_PERIOD) * Voice->Envelope[0] * 4.0f))
    {
        Voice->Pop = TanksAudio_RandomRange(Random, 0.4f, 1.0f);
    }

    Crackle = Voice->Pop * Noise;
    Voice->Pop *= 0.8f;

    return (0.8f * TanksAudio_SoftClip(4.0f * TanksAudio_RunTone(&Voice->Tones[0]))) + (0.8f * Roar) + Jet + (0.2f * Crackle);
}

static float TanksAudio_RunRicochet(TanksAudio_VoiceTypeDef *Voice, float Noise)
{
    float Tick = Noise * Voice->Envelope[0];

    Voice->Envelope[0] *= Voice->Decay[0];
    Voice->Filter[0] += 0.4f * (Tick - Voice->Filter[0]);
    Tick -= Voice->Filter[0];

    return (0.4f * Tick) + (0.6f * (TanksAudio_RunTone(&Voice->Tones[0]) + TanksAudio_RunTone(&Voice->Tones[1]) + TanksAudio_RunTone(&Voice->Tones[2])));
}

static float TanksAudio_RunSpent(TanksAudio_VoiceTypeDef *Voice, float Noise)
{
    Voice->Filter[0] += 0.38f * (Noise - Voice->Filter[0]);
    Voice->Envelope[0] *= Voice->Decay[0];

    return (Voice->Filter[0] * 2.5f * Voice->Envelope[0]) + TanksAudio_RunTone(&Voice->Tones[0]);
}

static float TanksAudio_RunExplosion(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random, float Noise)
{
    float Crack;
    float Coefficient;
    float Body;
    float Crackle;
    float Output;

    Crack = Noise * Voice->Envelope[0];
    Voice->Envelope[0] *= Voice->Decay[0];

    /* Roaring body: low-passed noise, its cutoff falling as the blast rolls out. */
    Voice->CutoffHz += Voice->CutoffGlide * (Voice->CutoffTargetHz - Voice->CutoffHz);
    Coefficient = TanksAudio_SweepCoefficient(Voice->CutoffHz);
    Voice->Filter[0] += Coefficient * (Noise - Voice->Filter[0]);
    Voice->Filter[1] += Coefficient * (Voice->Filter[0] - Voice->Filter[1]);
    Body = TanksAudio_SoftClip(Voice->Filter[1] * 9.0f) * (Voice->Envelope[1] - Voice->Envelope[2]);
    Voice->Envelope[1] *= Voice->Decay[1];
    Voice->Envelope[2] *= Voice->Decay[2];

    /* Debris crackle, thinning out as the blast fades. */
    if((0.5f + (0.5f * TanksAudio_Noise(Random))) < (((40.0f + (250.0f * Voice->Size)) * TA_SAMPLE_PERIOD) * Voice->Envelope[1]))
    {
        Voice->Pop = TanksAudio_RandomRange(Random, 0.4f, 1.0f);
    }

    Voice->Filter[2] += 0.3f * ((Voice->Pop * Noise) - Voice->Filter[2]);
    Crackle = Voice->Filter[2] * 2.0f;
    Voice->Pop *= 0.92f;

    /* Metal pieces landing. */
    if((Voice->StrikeSample > 0U) && (Voice->Sample == Voice->StrikeSample))
    {
        TanksAudio_StartTone(&Voice->Tones[1], Voice->Tones[1].Hz, Voice->Tones[1].Hz * 0.97f, 0.1f, 0.35f, 0.12f);
        TanksAudio_StartTone(&Voice->Tones[2], Voice->Tones[2].Hz, Voice->Tones[2].Hz * 0.97f, 0.1f, 0.25f, 0.08f);
    }

    Output = (0.4f * Crack) + (0.9f * Body) + (0.9f * Voice->Size * TanksAudio_SoftClip(6.0f * TanksAudio_RunTone(&Voice->Tones[0]))) + (0.6f * Crackle);
    Output += TanksAudio_RunTone(&Voice->Tones[1]) + TanksAudio_RunTone(&Voice->Tones[2]);

    return Output;
}

static float TanksAudio_RunMineDrop(TanksAudio_VoiceTypeDef *Voice, float Noise)
{
    Voice->Filter[0] += 0.3f * ((Noise * Voice->Envelope[0]) - Voice->Filter[0]);
    Voice->Envelope[0] *= Voice->Decay[0];

    return (0.7f * TanksAudio_SoftClip(4.0f * TanksAudio_RunTone(&Voice->Tones[0]))) + TanksAudio_RunTone(&Voice->Tones[1]) +
           TanksAudio_RunTone(&Voice->Tones[2]) + (0.6f * Voice->Filter[0]);
}

static float TanksAudio_RunMineArmed(TanksAudio_VoiceTypeDef *Voice)
{
    if(Voice->Sample == Voice->StrikeSample)
    {
        Voice->Tones[1].Amplitude = 1.0f;
    }

    return TanksAudio_RunTone(&Voice->Tones[0]) + TanksAudio_RunTone(&Voice->Tones[1]);
}

static float TanksAudio_RunMineFizzle(TanksAudio_VoiceTypeDef *Voice, float Noise)
{
    float Hiss;

    Voice->Filter[0] += 0.42f * (Noise - Voice->Filter[0]);
    Hiss = (Noise - Voice->Filter[0]) * Voice->Envelope[0];
    Voice->Envelope[0] *= Voice->Decay[0];

    return Hiss + TanksAudio_RunTone(&Voice->Tones[0]);
}

static float TanksAudio_RunVoice(TanksAudio_VoiceTypeDef *Voice, uint32_t *Random)
{
    const float Noise = TanksAudio_Noise(Random);
    float Output = 0.0f;

    switch(Voice->Sound)
    {
        case TA_SOUND_SHOT:
            Output = TanksAudio_RunShot(Voice, Noise);
            break;

        case TA_SOUND_ROCKET_LAUNCH:
            Output = TanksAudio_RunRocketLaunch(Voice, Random, Noise);
            break;

        case TA_SOUND_RICOCHET:
            Output = TanksAudio_RunRicochet(Voice, Noise);
            break;

        case TA_SOUND_SPENT:
            Output = TanksAudio_RunSpent(Voice, Noise);
            break;

        case TA_SOUND_EXPLOSION:
            Output = TanksAudio_RunExplosion(Voice, Random, Noise);
            break;

        case TA_SOUND_MINE_DROP:
            Output = TanksAudio_RunMineDrop(Voice, Noise);
            break;

        case TA_SOUND_MINE_ARMED:
            Output = TanksAudio_RunMineArmed(Voice);
            break;

        case TA_SOUND_MINE_BEEP:
            Output = TanksAudio_RunTone(&Voice->Tones[0]);
            break;

        case TA_SOUND_MINE_FIZZLE:
            Output = TanksAudio_RunMineFizzle(Voice, Noise);
            break;

        default:
            break;
    }

    Voice->Sample++;

    if(Voice->Sample >= Voice->LengthSamples)
    {
        Voice->Active = false;
    }

    return Output * Voice->Level;
}

static void TanksAudio_GenerateEffects(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context)
{
    TanksAudio_EffectsStateTypeDef *State = &TanksAudio_Effects;
    const float HighPassCoefficient = TanksAudio_LowPassCoefficient(TA_OUTPUT_HIGH_PASS_HZ);
    uint32_t Head = __atomic_load_n(&TanksAudio_EventHead, __ATOMIC_ACQUIRE);
    uint32_t Tail = __atomic_load_n(&TanksAudio_EventTail, __ATOMIC_RELAXED);
    bool AnyActive = false;

    (void)Context;

    /* A restart drops every sound, and any events posted before it. */
    if(State->RestartCount != TanksAudio_EffectsControl.RestartCount)
    {
        *State = (TanksAudio_EffectsStateTypeDef){ 0 };
        State->RestartCount = TanksAudio_EffectsControl.RestartCount;
        State->Random = 0x1B873593U ^ (State->RestartCount * 0x85EBCA6BU);
        Tail = TanksAudio_EffectsControl.FirstEvent;
    }

    while(Tail != Head)
    {
        TanksAudio_StartVoice(State, &TanksAudio_Events[Tail % TA_EVENT_QUEUE_SIZE]);
        Tail++;
    }

    __atomic_store_n(&TanksAudio_EventTail, Tail, __ATOMIC_RELEASE);

    for(uint32_t Voice = 0U; Voice < TA_EFFECT_VOICES; Voice++)
    {
        AnyActive = AnyActive || State->Voices[Voice].Active;
    }

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        float Output = 0.0f;

        if(AnyActive)
        {
            for(uint32_t Voice = 0U; Voice < TA_EFFECT_VOICES; Voice++)
            {
                if(State->Voices[Voice].Active)
                {
                    Output += TanksAudio_RunVoice(&State->Voices[Voice], &State->Random);
                }
            }
        }

        Samples[Index] = TanksAudio_ToSample(TanksAudio_HighPass(State->HighPass, Output, HighPassCoefficient));
    }
}

/* -------------------------------------------------------------------------- */
/* Jingles                                                                    */
/* -------------------------------------------------------------------------- */

static void TanksAudio_StartJingleNote(TanksAudio_JingleStateTypeDef *State, const TanksAudio_NoteTypeDef *Note)
{
    TanksAudio_JingleVoiceTypeDef *Voice = &State->Voices[0];

    for(uint32_t Index = 0U; Index < TA_JINGLE_VOICES; Index++)
    {
        if(!State->Voices[Index].Active)
        {
            Voice = &State->Voices[Index];
            break;
        }

        if(State->Voices[Index].Sample > Voice->Sample)
        {
            Voice = &State->Voices[Index];
        }
    }

    *Voice = (TanksAudio_JingleVoiceTypeDef){ 0 };
    Voice->Active = true;
    Voice->Note = Note;
    Voice->HoldSamples = TanksAudio_Seconds(Note->Seconds);
    Voice->FilterEnvelope = 1.0f;
    Voice->Amplitude = (Note->Instrument == TA_INSTRUMENT_BRASS) ? 0.0f : 1.0f;
    Voice->ModulationIndex = 2.0f;
    Voice->ToneHz = 420.0f;
    Voice->ToneAmplitude = (Note->Instrument == TA_INSTRUMENT_SNARE) ? 1.0f : 0.0f;
    Voice->Phase[1] = TanksAudio_RandomRange(&State->Random, 0.0f, 1.0f);
}

/* Brass: two slightly detuned saws through a filter that opens on each note. */
static float TanksAudio_RunBrass(TanksAudio_JingleVoiceTypeDef *Voice, float ReleaseDecay, float FilterDecay, float VibratoStep)
{
    const float Seconds = (float)Voice->Sample * TA_SAMPLE_PERIOD;
    float Hz = Voice->Note->Hz;
    float Step;
    float Coefficient;
    float Saw;

    if(Seconds > TA_BRASS_VIBRATO_DELAY_SECONDS)
    {
        Voice->VibratoPhase += VibratoStep;
        Voice->VibratoPhase -= (Voice->VibratoPhase >= 1.0f) ? 1.0f : 0.0f;
        Hz *= 1.0f + (TA_BRASS_VIBRATO_DEPTH * TanksAudio_Sine(Voice->VibratoPhase));
    }

    Step = Hz * TA_SAMPLE_PERIOD;
    Voice->Phase[0] += Step;
    Voice->Phase[0] -= (Voice->Phase[0] >= 1.0f) ? 1.0f : 0.0f;
    Voice->Phase[1] += Step * TA_BRASS_DETUNE;
    Voice->Phase[1] -= (Voice->Phase[1] >= 1.0f) ? 1.0f : 0.0f;
    Saw = (2.0f * Voice->Phase[0]) - 1.0f - TanksAudio_PolyBlep(Voice->Phase[0], Step);
    Saw += (2.0f * Voice->Phase[1]) - 1.0f - TanksAudio_PolyBlep(Voice->Phase[1], Step * TA_BRASS_DETUNE);

    Coefficient = TanksAudio_SweepCoefficient((Hz * 2.0f) + (3000.0f * Voice->FilterEnvelope));
    Voice->FilterEnvelope *= FilterDecay;
    Voice->Low[0] += Coefficient * ((0.5f * Saw) - Voice->Low[0]);
    Voice->Low[1] += Coefficient * (Voice->Low[0] - Voice->Low[1]);

    /* Attack, hold for the note's length, then release. */
    if(Voice->Sample < Voice->HoldSamples)
    {
        Voice->Amplitude = (Seconds < TA_BRASS_ATTACK_SECONDS) ? (Seconds / TA_BRASS_ATTACK_SECONDS) : 1.0f;
    }
    else
    {
        Voice->Amplitude *= ReleaseDecay;
    }

    return Voice->Low[1] * Voice->Amplitude;
}

/* Snare: a burst of bright noise over a short drum tone. */
static float TanksAudio_RunSnare(TanksAudio_JingleVoiceTypeDef *Voice, uint32_t *Random, float NoiseDecay, float ToneDecay)
{
    const float Noise = TanksAudio_Noise(Random);
    float Tone;

    Voice->Low[0] += 0.35f * (Noise - Voice->Low[0]);
    Voice->Amplitude *= NoiseDecay;

    Voice->ToneHz += 0.002f * (300.0f - Voice->ToneHz);
    Voice->Phase[0] += Voice->ToneHz * TA_SAMPLE_PERIOD;
    Voice->Phase[0] -= (Voice->Phase[0] >= 1.0f) ? 1.0f : 0.0f;
    Tone = TanksAudio_Sine(Voice->Phase[0]) * Voice->ToneAmplitude;
    Voice->ToneAmplitude *= ToneDecay;

    return ((Noise - Voice->Low[0]) * Voice->Amplitude) + (0.6f * Tone);
}

/* Bell: a bright FM chime. */
static float TanksAudio_RunBell(TanksAudio_JingleVoiceTypeDef *Voice, float BellDecay, float IndexDecay)
{
    const float Step = Voice->Note->Hz * TA_SAMPLE_PERIOD;
    float Output;

    Voice->Phase[0] += Step;
    Voice->Phase[0] -= (Voice->Phase[0] >= 1.0f) ? 1.0f : 0.0f;
    Voice->Phase[1] += Step * 3.5f;
    Voice->Phase[1] -= floorf(Voice->Phase[1]);

    Output = TanksAudio_Sine(Voice->Phase[0] + ((Voice->ModulationIndex / TA_TWO_PI) * TanksAudio_Sine(Voice->Phase[1]))) * Voice->Amplitude;
    Voice->Amplitude *= BellDecay;
    Voice->ModulationIndex *= IndexDecay;

    return Output;
}

static void TanksAudio_GenerateJingle(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context)
{
    TanksAudio_JingleStateTypeDef *State = &TanksAudio_JinglePlayer;
    const float HighPassCoefficient = TanksAudio_LowPassCoefficient(TA_OUTPUT_HIGH_PASS_HZ);
    const float ReleaseDecay = TanksAudio_DecayCoefficient(TA_BRASS_RELEASE_SECONDS);
    const float FilterDecay = TanksAudio_DecayCoefficient(TA_BRASS_FILTER_SECONDS);
    const float VibratoStep = TA_BRASS_VIBRATO_HZ * TA_SAMPLE_PERIOD;
    const float SnareNoiseDecay = TanksAudio_DecayCoefficient(0.07f);
    const float SnareToneDecay = TanksAudio_DecayCoefficient(0.04f);
    const float BellDecay = TanksAudio_DecayCoefficient(0.5f);
    const float BellIndexDecay = TanksAudio_DecayCoefficient(0.15f);

    (void)Context;

    if(State->RestartCount != TanksAudio_JingleControl.RestartCount)
    {
        const uint32_t Jingle = TanksAudio_JingleControl.Jingle;

        *State = (TanksAudio_JingleStateTypeDef){ 0 };
        State->RestartCount = TanksAudio_JingleControl.RestartCount;
        State->Random = 0x27D4EB2FU ^ (State->RestartCount * 0x165667B1U);
        State->Jingle = (Jingle < (uint32_t)TANKS_AUDIO_JINGLE_NONE) ? &TanksAudio_Jingles[Jingle] : NULL;
    }

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        float Output = 0.0f;

        /* Start every note that is due. */
        while((State->Jingle != NULL) && (State->NextNote < State->Jingle->NoteCount) &&
              (State->Sample >= TanksAudio_Seconds(State->Jingle->Notes[State->NextNote].StartSeconds)))
        {
            TanksAudio_StartJingleNote(State, &State->Jingle->Notes[State->NextNote]);
            State->NextNote++;
        }

        for(uint32_t Voice = 0U; Voice < TA_JINGLE_VOICES; Voice++)
        {
            TanksAudio_JingleVoiceTypeDef *Note = &State->Voices[Voice];

            if(!Note->Active)
            {
                continue;
            }

            if(Note->Note->Instrument == TA_INSTRUMENT_BRASS)
            {
                Output += TanksAudio_RunBrass(Note, ReleaseDecay, FilterDecay, VibratoStep) * Note->Note->Level;
            }
            else if(Note->Note->Instrument == TA_INSTRUMENT_SNARE)
            {
                Output += TanksAudio_RunSnare(Note, &State->Random, SnareNoiseDecay, SnareToneDecay) * Note->Note->Level;
            }
            else
            {
                Output += TanksAudio_RunBell(Note, BellDecay, BellIndexDecay) * Note->Note->Level;
            }

            Note->Sample++;

            if((Note->Sample > Note->HoldSamples) && (Note->Amplitude < TA_SILENT) && (Note->ToneAmplitude < TA_SILENT))
            {
                Note->Active = false;
            }
        }

        State->Sample++;
        Samples[Index] = TanksAudio_ToSample(TanksAudio_HighPass(State->HighPass, Output * TA_JINGLE_LEVEL, HighPassCoefficient));
    }
}

/* -------------------------------------------------------------------------- */
/* Game-loop helpers                                                          */
/* -------------------------------------------------------------------------- */

/* Quieter with distance from the player. */
static float TanksAudio_DistanceGain(Tanks_VectorTypeDef Position)
{
    const float Gain = 1.0f / (1.0f + ((float)Tanks_DistancePixels(Position, Tanks_Game.Player.Position) / TA_DISTANCE_PIXELS));

    return (Gain < TA_DISTANCE_FLOOR) ? TA_DISTANCE_FLOOR : Gain;
}

/*
 * How fast a tank's tracks are really running, 0 to 1, from how far it moved
 * and turned since the last frame. A tank pushing against a wall is silent;
 * turning on the spot still runs the tracks. A jump (a respawn) counts as
 * standing still.
 */
static float TanksAudio_TankActivity(const Tanks_TankTypeDef *Tank, uint32_t Slot, float FullSpeed, uint32_t DeltaMilliseconds)
{
    const float DeltaX = (float)(Tank->Position.X - TanksAudio_LastPosition[Slot].X) / (float)TANKS_FP_ONE;
    const float DeltaY = (float)(Tank->Position.Y - TanksAudio_LastPosition[Slot].Y) / (float)TANKS_FP_ONE;
    const float Seconds = (float)DeltaMilliseconds / 1000.0f;
    const bool Valid = TanksAudio_LastValid[Slot];
    float Speed;
    float Turn;
    float Activity;

    Turn = fabsf((float)Tanks_NormalizeAngle((int32_t)Tank->Heading - TanksAudio_LastHeading[Slot])) / Seconds;
    Speed = sqrtf((DeltaX * DeltaX) + (DeltaY * DeltaY)) / Seconds;

    TanksAudio_LastPosition[Slot] = Tank->Position;
    TanksAudio_LastHeading[Slot] = Tank->Heading;
    TanksAudio_LastValid[Slot] = true;

    if(!Valid || (Speed > (4.0f * FullSpeed)))
    {
        return 0.0f;
    }

    Activity = (Speed / FullSpeed) + (TA_TURN_ACTIVITY * Turn / TA_FULL_TURN_RATE);

    return (Activity > 1.0f) ? 1.0f : Activity;
}

static void TanksAudio_Post(TanksAudio_SoundTypeDef Sound, float Level, float Pitch, uint8_t Strength)
{
    const uint32_t Head = __atomic_load_n(&TanksAudio_EventHead, __ATOMIC_RELAXED);
    const uint32_t Tail = __atomic_load_n(&TanksAudio_EventTail, __ATOMIC_ACQUIRE);
    TanksAudio_EventTypeDef *Event;

    if((Head - Tail) >= TA_EVENT_QUEUE_SIZE)
    {
        return;
    }

    Event = &TanksAudio_Events[Head % TA_EVENT_QUEUE_SIZE];
    Event->Sound = Sound;
    Event->Level = Level;
    Event->Pitch = Pitch;
    Event->Strength = Strength;
    __atomic_store_n(&TanksAudio_EventHead, Head + 1U, __ATOMIC_RELEASE);
}

/* Armed mines beep now and then, quickly when the player is close. */
static void TanksAudio_UpdateMines(uint32_t DeltaMilliseconds, bool Playing)
{
    bool Beeped = false;

    for(uint32_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        const Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[Index];
        bool Close;
        uint16_t Interval;

        if(!Mine->Active || (Mine->ArmMilliseconds > 0U))
        {
            TanksAudio_MineTracked[Index] = false;
            continue;
        }

        Close = Tanks_Game.Player.Active && (Tanks_DistancePixels(Mine->Position, Tanks_Game.Player.Position) <= TA_MINE_WARNING_PIXELS);
        Interval = Close ? TA_MINE_WARNING_MILLISECONDS : TA_MINE_BEEP_MILLISECONDS;

        /* The arming chirp counts as the first beep. */
        if(!TanksAudio_MineTracked[Index])
        {
            TanksAudio_MineTracked[Index] = true;
            TanksAudio_MineBeepMilliseconds[Index] = Interval;
            continue;
        }

        if(TanksAudio_MineBeepMilliseconds[Index] > Interval)
        {
            TanksAudio_MineBeepMilliseconds[Index] = Interval;
        }

        if(TanksAudio_MineBeepMilliseconds[Index] > DeltaMilliseconds)
        {
            TanksAudio_MineBeepMilliseconds[Index] = (uint16_t)(TanksAudio_MineBeepMilliseconds[Index] - DeltaMilliseconds);
            continue;
        }

        TanksAudio_MineBeepMilliseconds[Index] = Interval;

        if(Playing && !Beeped)
        {
            TanksAudio_Post(TA_SOUND_MINE_BEEP, (Close ? TA_MINE_WARNING_LEVEL : TA_MINE_BEEP_LEVEL) * TanksAudio_DistanceGain(Mine->Position),
                            Close ? TA_MINE_WARNING_PITCH : 1.0f, 0U);
            Beeped = true;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void TanksAudio_Start(void)
{
    TanksAudio_AmbienceControl.PlayerActivity = 0.0f;
    TanksAudio_AmbienceControl.EnemyActivity = 0.0f;
    TanksAudio_AmbienceControl.EnemyLevel = 0.0f;
    TanksAudio_AmbienceControl.RocketLevel = 0.0f;
    TanksAudio_AmbienceControl.RestartCount++;
    TanksAudio_EffectsControl.FirstEvent = __atomic_load_n(&TanksAudio_EventHead, __ATOMIC_RELAXED);
    TanksAudio_EffectsControl.RestartCount++;
    TanksAudio_JingleControl.Jingle = (uint32_t)TANKS_AUDIO_JINGLE_NONE;
    TanksAudio_JingleControl.RestartCount++;

    (void)Mixer_PlayGenerator(TA_AMBIENCE_CHANNEL, TanksAudio_GenerateAmbience, NULL);
    (void)Mixer_PlayGenerator(TA_EFFECTS_CHANNEL, TanksAudio_GenerateEffects, NULL);
    (void)Mixer_PlayGenerator(TA_JINGLE_CHANNEL, TanksAudio_GenerateJingle, NULL);
}

void TanksAudio_Stop(void)
{
    (void)Mixer_Stop(TA_AMBIENCE_CHANNEL);
    (void)Mixer_Stop(TA_EFFECTS_CHANNEL);
    (void)Mixer_Stop(TA_JINGLE_CHANNEL);
}

void TanksAudio_Update(uint32_t DeltaMilliseconds)
{
    const bool Playing = Tanks_Game.Screen == TANKS_SCREEN_PLAYING;
    float PlayerActivity = 0.0f;
    float EnemySum = 0.0f;
    float EnemyWeight = 0.0f;
    float RocketLevel = 0.0f;

    /* Without elapsed time there is no speed to measure; keep the last values. */
    if(DeltaMilliseconds == 0U)
    {
        return;
    }

    if(Playing && Tanks_Game.Player.Active)
    {
        PlayerActivity = TanksAudio_TankActivity(&Tanks_Game.Player, 0U, TA_PLAYER_FULL_SPEED, DeltaMilliseconds);
    }
    else
    {
        TanksAudio_LastValid[0] = false;
    }

    if(Playing)
    {
        for(uint32_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
        {
            const Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
            float Gain;

            if(!Enemy->Active || !Enemy->Awake)
            {
                TanksAudio_LastValid[Index + 1U] = false;
                continue;
            }

            Gain = TanksAudio_DistanceGain(Enemy->Position);
            EnemySum += TanksAudio_TankActivity(Enemy, Index + 1U, TA_ENEMY_FULL_SPEED, DeltaMilliseconds) * Gain;
            EnemyWeight += Gain;
        }

        for(uint32_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
        {
            const Tanks_BulletTypeDef *Bullet = &Tanks_Game.Bullets[Index];

            if(Bullet->Active && (Bullet->Type == TANKS_PROJECTILE_ROCKET))
            {
                RocketLevel += 0.8f * TanksAudio_DistanceGain(Bullet->Position);
            }
        }
    }

    TanksAudio_AmbienceControl.PlayerActivity = PlayerActivity;
    TanksAudio_AmbienceControl.EnemyActivity = (EnemyWeight > 0.0f) ? (EnemySum / EnemyWeight) : 0.0f;
    TanksAudio_AmbienceControl.EnemyLevel = (EnemySum > 1.0f) ? 1.0f : EnemySum;
    TanksAudio_AmbienceControl.RocketLevel = (RocketLevel > 1.0f) ? 1.0f : RocketLevel;

    TanksAudio_UpdateMines(DeltaMilliseconds, Playing);
}

void TanksAudio_PlayFire(Tanks_VectorTypeDef Position, bool Player, bool Rocket)
{
    const float Gain = Player ? 1.0f : TanksAudio_DistanceGain(Position);

    if(Rocket)
    {
        TanksAudio_Post(TA_SOUND_ROCKET_LAUNCH, TA_ROCKET_LAUNCH_LEVEL * Gain, 1.0f, 0U);
    }
    else
    {
        TanksAudio_Post(TA_SOUND_SHOT, (Player ? TA_SHOT_LEVEL : TA_ENEMY_SHOT_LEVEL) * Gain, Player ? 1.0f : TA_ENEMY_SHOT_PITCH, 0U);
    }
}

void TanksAudio_PlayRicochet(Tanks_VectorTypeDef Position)
{
    TanksAudio_Post(TA_SOUND_RICOCHET, TA_RICOCHET_LEVEL * TanksAudio_DistanceGain(Position), 1.0f, 0U);
}

void TanksAudio_PlayBulletSpent(Tanks_VectorTypeDef Position)
{
    TanksAudio_Post(TA_SOUND_SPENT, TA_SPENT_LEVEL * TanksAudio_DistanceGain(Position), 1.0f, 0U);
}

void TanksAudio_PlayExplosion(Tanks_VectorTypeDef Position, uint8_t Strength)
{
    const float Gain = (Strength >= TA_PLAYER_EXPLOSION_STRENGTH) ? 1.0f : TanksAudio_DistanceGain(Position);

    TanksAudio_Post(TA_SOUND_EXPLOSION, TA_EXPLOSION_LEVEL * Gain, 1.0f, Strength);
}

void TanksAudio_PlayMineDropped(Tanks_VectorTypeDef Position)
{
    TanksAudio_Post(TA_SOUND_MINE_DROP, TA_MINE_DROP_LEVEL * TanksAudio_DistanceGain(Position), 1.0f, 0U);
}

void TanksAudio_PlayMineArmed(Tanks_VectorTypeDef Position)
{
    TanksAudio_Post(TA_SOUND_MINE_ARMED, TA_MINE_ARMED_LEVEL * TanksAudio_DistanceGain(Position), 1.0f, 0U);
}

void TanksAudio_PlayMineFizzle(Tanks_VectorTypeDef Position)
{
    TanksAudio_Post(TA_SOUND_MINE_FIZZLE, TA_MINE_FIZZLE_LEVEL * TanksAudio_DistanceGain(Position), 1.0f, 0U);
}

void TanksAudio_PlayJingle(TanksAudio_JingleTypeDef Jingle)
{
    TanksAudio_JingleControl.Jingle = (uint32_t)Jingle;
    TanksAudio_JingleControl.RestartCount++;
}
