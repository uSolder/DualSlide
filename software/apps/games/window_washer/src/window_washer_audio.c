/**
 * @file window_washer_audio.c
 * @brief Generated sound effects for the Window Washer game.
 *
 * Each sound is a mixer synth running in the audio output context, and owns
 * all of its state. Playing a synth restarts it. The cart and wind follow
 * target values (cart speed, wind level) the game sends as messages, and
 * smooth them so changes never step audibly; the crash is sent its strength.
 *
 * Building blocks:
 * - Noise from a xorshift generator, shaped by one-pole low-pass filters and
 *   a state-variable band-pass filter.
 * - Damped resonators (two-pole filters struck by an impulse) for the
 *   bicycle bells.
 * - Square-wave pairs for car horns.
 */

#include "window_washer_audio.h"

#include "mixer.h"
#include "sound.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define WWA_CART_CHANNEL                    ((Mixer_ChannelTypeDef)0U)
#define WWA_WIND_CHANNEL                    ((Mixer_ChannelTypeDef)1U)
#define WWA_CITY_CHANNEL                    ((Mixer_ChannelTypeDef)2U)
#define WWA_SQUEEGEE_CHANNEL                ((Mixer_ChannelTypeDef)3U)
#define WWA_CRASH_CHANNEL                   ((Mixer_ChannelTypeDef)4U)
#define WWA_YELL_CHANNEL                    ((Mixer_ChannelTypeDef)5U)


/* Overall loudness of every Window Washer sound. */
#define WWA_MASTER_LEVEL                    (0.6f)

/* Cart */
#define WWA_CART_LEVEL                      (0.25f)
#define WWA_CART_SMOOTHING_SECONDS          (0.06f)
#define WWA_CART_RUMBLE_MINIMUM_HZ          (40.0f)
#define WWA_CART_RUMBLE_MAXIMUM_HZ          (100.0f)
#define WWA_CART_HUM_MINIMUM_HZ             (60.0f)
#define WWA_CART_HUM_MAXIMUM_HZ             (100.0f)
#define WWA_CART_ROTATION_MINIMUM_HZ        (0.0f)
#define WWA_CART_ROTATION_MAXIMUM_HZ        (30.0f)
#define WWA_CART_PITCH_WOBBLE               (0.04f)
#define WWA_CART_GRIND_HARMONICS            (4.0f)
#define WWA_CART_GRIND_LEVEL                (0.4f)
#define WWA_CART_RUMBLE_LEVEL               (0.6f)
#define WWA_CART_THUD_DECAY_SECONDS         (0.04f)
#define WWA_CART_THUD_CUTOFF_HZ             (200.0f)
#define WWA_CART_THUD_LEVEL                 (6.0f)
#define WWA_CART_SILENT_SPEED               (0.02f)

/* Wind */
#define WWA_WIND_MINIMUM_LEVEL              (0.00f)
#define WWA_WIND_MAXIMUM_LEVEL              (0.15f)
#define WWA_WIND_RAMP_MILLISECONDS          (60000U)
#define WWA_WIND_SMOOTHING_SECONDS          (0.5f)
#define WWA_WIND_BODY_CUTOFF_HZ             (350.0f)
#define WWA_WIND_WHISTLE_MINIMUM_HZ         (450.0f)
#define WWA_WIND_WHISTLE_MAXIMUM_HZ         (950.0f)
#define WWA_WIND_WHISTLE_DAMPING            (0.35f)
#define WWA_WIND_GUST_UPDATE_SAMPLES        (64U)

/* City */
#define WWA_CITY_LEVEL                      (0.15f)
#define WWA_CITY_HOLD_MILLISECONDS          (5000U)
#define WWA_CITY_FADE_MILLISECONDS          (30000U)
#define WWA_CITY_STOP_MILLISECONDS          (WWA_CITY_HOLD_MILLISECONDS + WWA_CITY_FADE_MILLISECONDS + 100U)
#define WWA_CITY_HONK_COUNT                 (5U)
#define WWA_CITY_MAXIMUM_BELLS              (2U)
#define WWA_CITY_EVENT_LATEST_SECONDS       (30.0f)
#define WWA_CITY_EVENT_MINIMUM_GAP_SECONDS  (10.0f)
#define WWA_CITY_TRAFFIC_CUTOFF_HZ          (140.0f)
#define WWA_CITY_HORN_TONE_CUTOFF_HZ        (1800.0f)
#define WWA_CITY_BELL_PARTIALS              (3U)
#define WWA_CITY_BELL_DECAY_SECONDS         (0.7f)
#define WWA_CITY_RING_GAP_SECONDS           (0.6f)
#define WWA_CITY_NOISE_LEVEL                (0.05f)
#define WWA_CITY_NOISE_HIGH_PASS_HZ         (250.0f)
#define WWA_CITY_NOISE_CUTOFF_HZ            (1800.0f)
#define WWA_CITY_NOISE_SWELL_MINIMUM_SECONDS (2.5f)
#define WWA_CITY_NOISE_SWELL_MAXIMUM_SECONDS (5.0f)
#define WWA_CITY_NOISE_SWELL_DEPTH          (0.35f)

/* Squeegee: overall */
#define WWA_SQUEEGEE_LEVEL                  (0.2f)
#define WWA_SQUEEGEE_STROKES                (2U)
#define WWA_SQUEEGEE_STROKE_SECONDS         (0.18f)
#define WWA_SQUEEGEE_STROKE_VARIATION       (0.25f)
#define WWA_SQUEEGEE_GAP_SECONDS            (0.04f)
#define WWA_SQUEEGEE_ATTACK_SECONDS         (0.015f)
#define WWA_SQUEEGEE_RELEASE_SECONDS        (0.03f)
#define WWA_SQUEEGEE_SPEED_LOUDNESS         (0.5f)

/* Squeegee: squeal, the tone of the blade sliding at speed */
#define WWA_SQUEEGEE_LOW_HZ                 (700.0f)
#define WWA_SQUEEGEE_HIGH_HZ                (1400.0f)
#define WWA_SQUEEGEE_STROKE_PITCH_STEP      (1.15f)
#define WWA_SQUEEGEE_PITCH_VARIATION        (0.1f)
#define WWA_SQUEEGEE_JITTER                 (0.02f)
#define WWA_SQUEEGEE_ROUGHNESS              (0.3f)
#define WWA_SQUEEGEE_STICK_FRACTION         (0.85f)
#define WWA_SQUEEGEE_TONE_CUTOFF_HZ         (4000.0f)
#define WWA_SQUEEGEE_LOCK_SPEED             (0.35f)
#define WWA_SQUEEGEE_LOCK_SOFTNESS          (0.15f)

/* Squeegee: grit, the blade catching while it moves slowly */
#define WWA_SQUEEGEE_GRIT_RATE_HZ           (250.0f)
#define WWA_SQUEEGEE_GRIT_LEVEL             (0.6f)

/* Squeegee: glass ring and wet swish */
#define WWA_SQUEEGEE_RESONANCE_HZ           (2400.0f)
#define WWA_SQUEEGEE_RESONANCE_DAMPING      (0.15f)
#define WWA_SQUEEGEE_RESONANCE_LEVEL        (0.5f)
#define WWA_SQUEEGEE_SWISH_HZ               (2500.0f)
#define WWA_SQUEEGEE_SWISH_DAMPING          (0.8f)
#define WWA_SQUEEGEE_SWISH_LEVEL            (0.1f)

/* Crash */
#define WWA_CRASH_LEVEL                     (0.35f)
#define WWA_CRASH_MINIMUM_STRENGTH          (0.0f)
#define WWA_CRASH_SECONDS                   (0.6f)
#define WWA_CRASH_BODY_START_HZ             (95.0f)
#define WWA_CRASH_BODY_END_HZ               (48.0f)
#define WWA_CRASH_BODY_DECAY_SECONDS        (0.15f)
#define WWA_CRASH_PITCH_DROP_SECONDS        (0.08f)
#define WWA_CRASH_IMPACT_DECAY_SECONDS      (0.025f)

/* Yell */
#define WWA_YELL_LEVEL                      (0.1f)
#define WWA_YELL_SECONDS                    (1.2f)
#define WWA_YELL_ATTACK_SECONDS             (0.03f)
#define WWA_YELL_START_HZ                   (380.0f)
#define WWA_YELL_PEAK_HZ                    (500.0f)
#define WWA_YELL_END_HZ                     (260.0f)
#define WWA_YELL_RISE_SECONDS               (0.12f)
#define WWA_YELL_VOWEL_OPEN_SECONDS         (0.08f)
#define WWA_YELL_VOWEL_CLOSE_SECONDS        (0.5f)
#define WWA_YELL_VIBRATO_HZ                 (6.0f)
#define WWA_YELL_VIBRATO_DEPTH              (0.015f)
#define WWA_YELL_JITTER                     (0.012f)
#define WWA_YELL_SHIMMER                    (0.1f)
#define WWA_YELL_STRAIN_SECONDS             (0.6f)
#define WWA_YELL_ROUGHNESS                  (0.3f)
#define WWA_YELL_BREATH                     (0.15f)
#define WWA_YELL_OPEN_QUOTIENT              (0.5f)
#define WWA_YELL_CLOSING_FRACTION           (0.2f)
#define WWA_YELL_BRIGHTNESS                 (0.6f)
#define WWA_YELL_HIGH_PASS_HZ               (350.0f)
#define WWA_YELL_FORMANTS                   (4U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Message from the game to the cart or wind synth.
 */
typedef struct
{
    bool Restart;
    float Target;
} WindowWasherAudio_ControlMessageTypeDef;

/**
 * @brief Two-pole resonator: a decaying sine when struck.
 */
typedef struct
{
    float Coefficient1;
    float Coefficient2;
    float Output1;
    float Output2;
    float StrikeGain;
} WindowWasherAudio_ResonatorTypeDef;

typedef struct
{
    uint32_t Random;
    float Target;
    float Speed;
    float Rumble1;
    float Rumble2;
    float RotationPhase;
    float GrindPhase;
    float Grind1;
    float Grind2;
    float ThudEnvelope;
    float Thud1;
    float Thud2;
} WindowWasherAudio_CartStateTypeDef;

typedef struct
{
    uint32_t Random;
    float Target;
    float Level;
    float Body1;
    float Body2;
    float WhistleLow;
    float WhistleBand;
    float WhistleFrequency;
    float Gust;
    float GustSeconds;
    uint32_t GustCountdown;
} WindowWasherAudio_WindStateTypeDef;

typedef struct
{
    uint32_t SamplesRemaining;
    uint32_t SamplesTotal;
    uint32_t SecondHonkStart;
    uint32_t SecondHonkEnd;
    float Phase1;
    float Phase2;
    float Step1;
    float Step2;
    float Level;
    float ToneLow;
} WindowWasherAudio_HornTypeDef;

typedef struct
{
    WindowWasherAudio_ResonatorTypeDef Partials[WWA_CITY_BELL_PARTIALS];
    uint32_t SecondStrikeCountdown;
    float SecondStrikeLevel;
} WindowWasherAudio_BellTypeDef;

typedef struct
{
    uint32_t Sample;
    bool Bell;
} WindowWasherAudio_CityEventTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    uint32_t Sample;
    WindowWasherAudio_CityEventTypeDef Events[WWA_CITY_HONK_COUNT + WWA_CITY_MAXIMUM_BELLS];
    uint32_t EventCount;
    uint32_t NextEvent;
    float Traffic1;
    float Traffic2;
    WindowWasherAudio_HornTypeDef Horn;
    WindowWasherAudio_BellTypeDef Bell;
    float NoiseHighPass1;
    float NoiseHighPass2;
    float NoiseLow1;
    float NoiseLow2;
    float Swell;
    float SwellTarget;
    uint32_t SwellCountdown;
} WindowWasherAudio_CityStateTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    uint32_t Stroke;
    uint32_t StrokeSample;
    uint32_t StrokeSamples;
    uint32_t GapSamples;
    float Pitch;
    float Phase;
    float Jitter;
    float CycleLevel;
    float Tone;
    Sound_BandPassStateTypeDef Resonance;
    Sound_BandPassStateTypeDef Swish;
} WindowWasherAudio_SqueegeeStateTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    uint32_t Sample;
    float Strength;
    float BodyHz;
    float BodyPhase;
    float BodyEnvelope;
    float ImpactEnvelope;
    float ImpactCoefficient;
    float Impact;
    WindowWasherAudio_ResonatorTypeDef Clank[2];
} WindowWasherAudio_CrashStateTypeDef;

/**
 * @brief Two-pole formant resonator with unity gain at DC.
 */
typedef struct
{
    float A;
    float B;
    float C;
    float Y1;
    float Y2;
} WindowWasherAudio_FormantTypeDef;

typedef struct
{
    uint32_t RestartCount;
    uint32_t Random;
    uint32_t Sample;
    float Pitch;
    float Phase;
    float VibratoPhase;
    float Jitter;
    float CycleLevel;
    bool OddCycle;
    float PreviousFlow;
    float PreviousSource;
    WindowWasherAudio_FormantTypeDef Formants[WWA_YELL_FORMANTS];
    float HighPass1;
    float HighPass2;
    float Distance;
} WindowWasherAudio_YellStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Bicycle bell: inharmonic partials relative to the strike pitch, and their levels. */
static const float WindowWasherAudio_BellPartialRatios[WWA_CITY_BELL_PARTIALS] = { 1.0f, 2.42f, 3.87f };
static const float WindowWasherAudio_BellPartialLevels[WWA_CITY_BELL_PARTIALS] = { 1.0f, 0.5f, 0.3f };

/* Main loop only. */
static bool WindowWasherAudio_CityPlaying;

/* Audio context only. */
static WindowWasherAudio_CartStateTypeDef WindowWasherAudio_Cart;
static WindowWasherAudio_WindStateTypeDef WindowWasherAudio_Wind;
static WindowWasherAudio_CityStateTypeDef WindowWasherAudio_City;
static WindowWasherAudio_SqueegeeStateTypeDef WindowWasherAudio_Squeegee;
static WindowWasherAudio_CrashStateTypeDef WindowWasherAudio_Crash;
static WindowWasherAudio_YellStateTypeDef WindowWasherAudio_Yell;

/* -------------------------------------------------------------------------- */
/* Synthesis helpers                                                          */
/* -------------------------------------------------------------------------- */

static void WindowWasherAudio_TuneResonator(WindowWasherAudio_ResonatorTypeDef *Resonator, float FrequencyHz, float DecaySeconds)
{
    const float Radius = Sound_DecayCoefficient(DecaySeconds);
    const float Angle = SOUND_TWO_PI * FrequencyHz / SOUND_SAMPLE_RATE;

    Resonator->Coefficient1 = 2.0f * Radius * cosf(Angle);
    Resonator->Coefficient2 = Radius * Radius;
    Resonator->StrikeGain = sinf(Angle);
}

/* Excite a resonator so its ring peaks at about Level. */
static void WindowWasherAudio_StrikeResonator(WindowWasherAudio_ResonatorTypeDef *Resonator, float Level)
{
    Resonator->Output1 += Level * Resonator->StrikeGain;
}

static float WindowWasherAudio_RunResonator(WindowWasherAudio_ResonatorTypeDef *Resonator)
{
    const float Output = (Resonator->Coefficient1 * Resonator->Output1) - (Resonator->Coefficient2 * Resonator->Output2);

    Resonator->Output2 = Resonator->Output1;
    Resonator->Output1 = Output;

    return Output;
}

/* Formant resonators are chained in series, which keeps the vowel's natural balance. */
static void WindowWasherAudio_TuneFormant(WindowWasherAudio_FormantTypeDef *Formant, float FrequencyHz, float BandwidthHz)
{
    const float Radius = expf(-0.5f * SOUND_TWO_PI * BandwidthHz / SOUND_SAMPLE_RATE);

    Formant->B = 2.0f * Radius * cosf(SOUND_TWO_PI * FrequencyHz / SOUND_SAMPLE_RATE);
    Formant->C = -(Radius * Radius);
    Formant->A = 1.0f - Formant->B - Formant->C;
}

static float WindowWasherAudio_RunFormant(WindowWasherAudio_FormantTypeDef *Formant, float Input)
{
    const float Output = (Formant->A * Input) + (Formant->B * Formant->Y1) + (Formant->C * Formant->Y2);

    Formant->Y2 = Formant->Y1;
    Formant->Y1 = Output;

    return Output;
}

/* -------------------------------------------------------------------------- */
/* Cart                                                                       */
/* -------------------------------------------------------------------------- */

static void WindowWasherAudio_RenderCart(float *Samples, uint32_t SampleCount, void *Context)
{
    WindowWasherAudio_CartStateTypeDef *State = &WindowWasherAudio_Cart;
    const float Smoothing = 1.0f / (WWA_CART_SMOOTHING_SECONDS * SOUND_SAMPLE_RATE);
    const float ThudCoefficient = Sound_LowPassCoefficient(WWA_CART_THUD_CUTOFF_HZ);
    const float ThudDecay = Sound_DecayCoefficient(WWA_CART_THUD_DECAY_SECONDS);
    const float TargetSpeed = State->Target;
    float RumbleCoefficient;
    float GrindCoefficient;

    (void)Context;

    /* A faster cart rumbles deeper and fuller: open the filters with speed. */
    RumbleCoefficient = Sound_LowPassCoefficient(WWA_CART_RUMBLE_MINIMUM_HZ + ((WWA_CART_RUMBLE_MAXIMUM_HZ - WWA_CART_RUMBLE_MINIMUM_HZ) * State->Speed));
    GrindCoefficient = Sound_LowPassCoefficient(WWA_CART_GRIND_HARMONICS * (WWA_CART_HUM_MINIMUM_HZ + ((WWA_CART_HUM_MAXIMUM_HZ - WWA_CART_HUM_MINIMUM_HZ) * State->Speed)));

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Noise = Sound_Noise(&State->Random);
        float RotationHz;
        float Wheel;
        float Rotary;
        float GrindHz;
        float Output;

        State->Speed += (TargetSpeed - State->Speed) * Smoothing;

        /* Wheel rotation; each turn is one soft thud from the rail. */
        RotationHz = WWA_CART_ROTATION_MINIMUM_HZ + ((WWA_CART_ROTATION_MAXIMUM_HZ - WWA_CART_ROTATION_MINIMUM_HZ) * State->Speed);
        State->RotationPhase += RotationHz / SOUND_SAMPLE_RATE;

        if(State->RotationPhase >= 1.0f)
        {
            State->RotationPhase -= 1.0f;
            State->ThudEnvelope = (State->Speed > WWA_CART_SILENT_SPEED) ? 1.0f : 0.0f;
        }

        /* Rotary "wub": the sound swells once per wheel turn. */
        Wheel = 1.0f - (4.0f * fabsf(State->RotationPhase - 0.5f));
        Rotary = 0.5f + (0.5f * Wheel);
        Rotary = 0.35f + (0.65f * Rotary * Rotary);

        /* Metallic grind: a rounded sawtooth whose pitch wavers with each turn. */
        GrindHz = (WWA_CART_HUM_MINIMUM_HZ + ((WWA_CART_HUM_MAXIMUM_HZ - WWA_CART_HUM_MINIMUM_HZ) * State->Speed)) * (1.0f + (WWA_CART_PITCH_WOBBLE * Wheel));
        State->GrindPhase += GrindHz / SOUND_SAMPLE_RATE;
        State->GrindPhase -= (State->GrindPhase >= 1.0f) ? 1.0f : 0.0f;
        State->Grind1 += GrindCoefficient * (((2.0f * State->GrindPhase) - 1.0f) - State->Grind1);
        State->Grind2 += GrindCoefficient * (State->Grind1 - State->Grind2);

        /* Track rumble: twice low-passed noise. */
        State->Rumble1 += RumbleCoefficient * (Noise - State->Rumble1);
        State->Rumble2 += RumbleCoefficient * (State->Rumble1 - State->Rumble2);

        /* Rail thud: a short bass burst. */
        State->ThudEnvelope *= ThudDecay;
        State->Thud1 += ThudCoefficient * ((Noise * State->ThudEnvelope) - State->Thud1);
        State->Thud2 += ThudCoefficient * (State->Thud1 - State->Thud2);

        Output = Rotary * ((State->Grind2 * WWA_CART_GRIND_LEVEL) + (State->Rumble2 * WWA_CART_RUMBLE_LEVEL));
        Output += State->Thud2 * WWA_CART_THUD_LEVEL;

        Samples[Index] = (Output * State->Speed * WWA_CART_LEVEL) * WWA_MASTER_LEVEL;
    }
}

static void WindowWasherAudio_ReceiveCart(const void *Message, uint32_t Size, void *Context)
{
    const WindowWasherAudio_ControlMessageTypeDef *Control = (const WindowWasherAudio_ControlMessageTypeDef *)Message;
    WindowWasherAudio_CartStateTypeDef *State = &WindowWasherAudio_Cart;

    (void)Context;

    if(Size != sizeof(*Control))
    {
        return;
    }

    if(Control->Restart)
    {
        *State = (WindowWasherAudio_CartStateTypeDef){ 0 };
        State->Random = 0x2F6B1C45U;
    }

    State->Target = Control->Target;
}

static const Mixer_SynthTypeDef WindowWasherAudio_CartSynth =
{
    .Start = NULL,
    .Receive = WindowWasherAudio_ReceiveCart,
    .Render = WindowWasherAudio_RenderCart
};

/* -------------------------------------------------------------------------- */
/* Wind                                                                       */
/* -------------------------------------------------------------------------- */

static void WindowWasherAudio_RenderWind(float *Samples, uint32_t SampleCount, void *Context)
{
    WindowWasherAudio_WindStateTypeDef *State = &WindowWasherAudio_Wind;
    const float Smoothing = 1.0f / (WWA_WIND_SMOOTHING_SECONDS * SOUND_SAMPLE_RATE);
    const float BodyCoefficient = Sound_LowPassCoefficient(WWA_WIND_BODY_CUTOFF_HZ);
    const float TargetLevel = State->Target;

    (void)Context;

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Noise = Sound_Noise(&State->Random);
        float Coefficient;
        float High;

        /* Gusts: two slow, unrelated waves recomputed every few samples. */
        if(State->GustCountdown == 0U)
        {
            State->GustCountdown = WWA_WIND_GUST_UPDATE_SAMPLES;
            State->GustSeconds += (float)WWA_WIND_GUST_UPDATE_SAMPLES / SOUND_SAMPLE_RATE;
            State->Gust = 0.6f + (0.25f * sinf(SOUND_TWO_PI * 0.13f * State->GustSeconds)) +
                          (0.15f * sinf((SOUND_TWO_PI * 0.37f * State->GustSeconds) + 1.3f));
            State->WhistleFrequency = WWA_WIND_WHISTLE_MINIMUM_HZ +
                                      ((WWA_WIND_WHISTLE_MAXIMUM_HZ - WWA_WIND_WHISTLE_MINIMUM_HZ) * (State->Gust - 0.2f));
        }

        State->GustCountdown--;
        State->Level += (TargetLevel - State->Level) * Smoothing;

        /* Rushing body: twice low-passed noise. */
        State->Body1 += BodyCoefficient * (Noise - State->Body1);
        State->Body2 += BodyCoefficient * (State->Body1 - State->Body2);

        /* Whistle: noise through a band-pass that follows the gusts. */
        Coefficient = SOUND_TWO_PI * State->WhistleFrequency / SOUND_SAMPLE_RATE;
        State->WhistleLow += Coefficient * State->WhistleBand;
        High = Noise - State->WhistleLow - (WWA_WIND_WHISTLE_DAMPING * State->WhistleBand);
        State->WhistleBand += Coefficient * High;

        Samples[Index] = (((State->Body2 * 4.0f) + (State->WhistleBand * 0.35f)) * State->Gust * State->Level) * WWA_MASTER_LEVEL;
    }
}

static void WindowWasherAudio_ReceiveWind(const void *Message, uint32_t Size, void *Context)
{
    const WindowWasherAudio_ControlMessageTypeDef *Control = (const WindowWasherAudio_ControlMessageTypeDef *)Message;
    WindowWasherAudio_WindStateTypeDef *State = &WindowWasherAudio_Wind;

    (void)Context;

    if(Size != sizeof(*Control))
    {
        return;
    }

    /* A restart starts at the target level rather than fading up to it. */
    if(Control->Restart)
    {
        *State = (WindowWasherAudio_WindStateTypeDef){ 0 };
        State->Random = 0x6C8E9CF5U;
        State->Level = Control->Target;
    }

    State->Target = Control->Target;
}

static const Mixer_SynthTypeDef WindowWasherAudio_WindSynth =
{
    .Start = NULL,
    .Receive = WindowWasherAudio_ReceiveWind,
    .Render = WindowWasherAudio_RenderWind
};

/* -------------------------------------------------------------------------- */
/* City                                                                       */
/* -------------------------------------------------------------------------- */

static void WindowWasherAudio_StartHorn(WindowWasherAudio_CityStateTypeDef *State)
{
    WindowWasherAudio_HornTypeDef *Horn = &State->Horn;
    const float PitchHz = Sound_RandomRange(&State->Random, 300.0f, 420.0f);
    const bool DoubleHonk = Sound_Noise(&State->Random) > 0.2f;

    /* Two tones a major third apart, like most car horns. */
    Horn->Step1 = PitchHz / SOUND_SAMPLE_RATE;
    Horn->Step2 = (PitchHz * 1.26f) / SOUND_SAMPLE_RATE;
    Horn->Level = Sound_RandomRange(&State->Random, 0.12f, 0.3f);

    if(DoubleHonk)
    {
        Horn->SecondHonkStart = Sound_Seconds(0.14f);
        Horn->SecondHonkEnd = Sound_Seconds(0.26f);
        Horn->SamplesTotal = Sound_Seconds(0.45f);
    }
    else
    {
        Horn->SecondHonkStart = 0U;
        Horn->SecondHonkEnd = 0U;
        Horn->SamplesTotal = Sound_Seconds(Sound_RandomRange(&State->Random, 0.2f, 0.5f));
    }

    Horn->SamplesRemaining = Horn->SamplesTotal;
}

static float WindowWasherAudio_RunHorn(WindowWasherAudio_HornTypeDef *Horn, float ToneCoefficient)
{
    const uint32_t Elapsed = Horn->SamplesTotal - Horn->SamplesRemaining;
    const uint32_t RampSamples = Sound_Seconds(0.012f);
    uint32_t FromEdge;
    float Tone;

    if(Horn->SamplesRemaining == 0U)
    {
        return 0.0f;
    }

    Horn->SamplesRemaining--;

    /* Distance to the nearest start or stop of sound, for short fades. */
    FromEdge = (Elapsed < Horn->SamplesRemaining) ? Elapsed : Horn->SamplesRemaining;

    if(Horn->SecondHonkEnd != 0U)
    {
        if((Elapsed >= Horn->SecondHonkStart) && (Elapsed < Horn->SecondHonkEnd))
        {
            FromEdge = 0U;
        }
        else if(Elapsed < Horn->SecondHonkStart)
        {
            FromEdge = (FromEdge < (Horn->SecondHonkStart - Elapsed)) ? FromEdge : (Horn->SecondHonkStart - Elapsed);
        }
        else
        {
            FromEdge = (FromEdge < (Elapsed - Horn->SecondHonkEnd)) ? FromEdge : (Elapsed - Horn->SecondHonkEnd);
        }
    }

    Horn->Phase1 += Horn->Step1;
    Horn->Phase1 -= (Horn->Phase1 >= 1.0f) ? 1.0f : 0.0f;
    Horn->Phase2 += Horn->Step2;
    Horn->Phase2 -= (Horn->Phase2 >= 1.0f) ? 1.0f : 0.0f;

    /* Two square waves, softened by a low-pass filter. */
    Tone = ((Horn->Phase1 < 0.5f) ? 0.5f : -0.5f) + ((Horn->Phase2 < 0.5f) ? 0.5f : -0.5f);
    Horn->ToneLow += ToneCoefficient * (Tone - Horn->ToneLow);

    return Horn->ToneLow * Horn->Level * ((FromEdge < RampSamples) ? ((float)FromEdge / (float)RampSamples) : 1.0f);
}

static void WindowWasherAudio_StartBell(WindowWasherAudio_CityStateTypeDef *State)
{
    WindowWasherAudio_BellTypeDef *Bell = &State->Bell;
    const float PitchHz = Sound_RandomRange(&State->Random, 1500.0f, 2100.0f);
    const float Level = Sound_RandomRange(&State->Random, 0.08f, 0.18f);

    for(uint32_t Partial = 0U; Partial < WWA_CITY_BELL_PARTIALS; Partial++)
    {
        WindowWasherAudio_TuneResonator(&Bell->Partials[Partial], PitchHz * WindowWasherAudio_BellPartialRatios[Partial], WWA_CITY_BELL_DECAY_SECONDS / (1.0f + (float)Partial));
        WindowWasherAudio_StrikeResonator(&Bell->Partials[Partial], Level * WindowWasherAudio_BellPartialLevels[Partial]);
    }

    /* Bicycle bells ring twice: "ring-ring". */
    Bell->SecondStrikeCountdown = Sound_Seconds(WWA_CITY_RING_GAP_SECONDS);
    Bell->SecondStrikeLevel = Level;
}

static float WindowWasherAudio_RunBell(WindowWasherAudio_BellTypeDef *Bell)
{
    float Output = 0.0f;

    if(Bell->SecondStrikeCountdown != 0U)
    {
        Bell->SecondStrikeCountdown--;

        if(Bell->SecondStrikeCountdown == 0U)
        {
            for(uint32_t Partial = 0U; Partial < WWA_CITY_BELL_PARTIALS; Partial++)
            {
                WindowWasherAudio_StrikeResonator(&Bell->Partials[Partial], Bell->SecondStrikeLevel * WindowWasherAudio_BellPartialLevels[Partial]);
            }
        }
    }

    for(uint32_t Partial = 0U; Partial < WWA_CITY_BELL_PARTIALS; Partial++)
    {
        Output += WindowWasherAudio_RunResonator(&Bell->Partials[Partial]);
    }

    return Output;
}

/*
 * Pick this round's city events: a honk shortly after the start, the other
 * honks and one or two bells at random times, played in time order.
 */
static void WindowWasherAudio_ScheduleCity(WindowWasherAudio_CityStateTypeDef *State)
{
    const uint32_t BellCount = (Sound_Noise(&State->Random) > 0.0f) ? 2U : 1U;

    State->EventCount = 0U;
    State->NextEvent = 0U;

    for(uint32_t Honk = 0U; Honk < WWA_CITY_HONK_COUNT; Honk++)
    {
        const float Earliest = (Honk == 0U) ? 0.1f : 0.8f;
        const float Latest = (Honk == 0U) ? 0.6f : WWA_CITY_EVENT_LATEST_SECONDS;

        State->Events[State->EventCount].Sample = Sound_Seconds(Sound_RandomRange(&State->Random, Earliest, Latest));
        State->Events[State->EventCount].Bell = false;
        State->EventCount++;
    }

    for(uint32_t Bell = 0U; Bell < BellCount; Bell++)
    {
        State->Events[State->EventCount].Sample = Sound_Seconds(Sound_RandomRange(&State->Random, 0.5f, WWA_CITY_EVENT_LATEST_SECONDS));
        State->Events[State->EventCount].Bell = true;
        State->EventCount++;
    }

    /* Insertion sort by start time. */
    for(uint32_t Index = 1U; Index < State->EventCount; Index++)
    {
        const WindowWasherAudio_CityEventTypeDef Event = State->Events[Index];
        uint32_t Position = Index;

        while((Position > 0U) && (State->Events[Position - 1U].Sample > Event.Sample))
        {
            State->Events[Position] = State->Events[Position - 1U];
            Position--;
        }

        State->Events[Position] = Event;
    }

    /* Keep events apart so each honk and bell is heard on its own. */
    for(uint32_t Index = 1U; Index < State->EventCount; Index++)
    {
        const uint32_t Earliest = State->Events[Index - 1U].Sample + Sound_Seconds(WWA_CITY_EVENT_MINIMUM_GAP_SECONDS);

        if(State->Events[Index].Sample < Earliest)
        {
            State->Events[Index].Sample = Earliest;
        }
    }
}

static void WindowWasherAudio_RenderCity(float *Samples, uint32_t SampleCount, void *Context)
{
    WindowWasherAudio_CityStateTypeDef *State = &WindowWasherAudio_City;
    const uint32_t HoldSamples = Sound_Seconds((float)WWA_CITY_HOLD_MILLISECONDS / 1000.0f);
    const uint32_t FadeSamples = Sound_Seconds((float)WWA_CITY_FADE_MILLISECONDS / 1000.0f);
    const float TrafficCoefficient = Sound_LowPassCoefficient(WWA_CITY_TRAFFIC_CUTOFF_HZ);
    const float ToneCoefficient = Sound_LowPassCoefficient(WWA_CITY_HORN_TONE_CUTOFF_HZ);
    const float NoiseHighPassCoefficient = Sound_LowPassCoefficient(WWA_CITY_NOISE_HIGH_PASS_HZ);
    const float NoiseLowCoefficient = Sound_LowPassCoefficient(WWA_CITY_NOISE_CUTOFF_HZ);
    const float SwellCoefficient = Sound_LowPassCoefficient(0.5f);

    (void)Context;

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        float Fade;
        float Output;
        float Roar;

        if(State->Sample >= (HoldSamples + FadeSamples))
        {
            Samples[Index] = 0.0f;
            continue;
        }

        while((State->NextEvent < State->EventCount) && (State->Sample >= State->Events[State->NextEvent].Sample))
        {
            if(State->Events[State->NextEvent].Bell)
            {
                WindowWasherAudio_StartBell(State);
            }
            else
            {
                WindowWasherAudio_StartHorn(State);
            }

            State->NextEvent++;
        }

        /* Distant traffic: deep, twice low-passed noise. */
        State->Traffic1 += TrafficCoefficient * (Sound_Noise(&State->Random) - State->Traffic1);
        State->Traffic2 += TrafficCoefficient * (State->Traffic1 - State->Traffic2);

        /* City roar: soft mid-band noise that slowly swells and settles. */
        if(State->SwellCountdown == 0U)
        {
            State->SwellTarget = 1.0f + (WWA_CITY_NOISE_SWELL_DEPTH * Sound_Noise(&State->Random));
            State->SwellCountdown = Sound_Seconds(Sound_RandomRange(&State->Random, WWA_CITY_NOISE_SWELL_MINIMUM_SECONDS, WWA_CITY_NOISE_SWELL_MAXIMUM_SECONDS));
        }

        State->SwellCountdown--;
        State->Swell += SwellCoefficient * (State->SwellTarget - State->Swell);

        Roar = Sound_Noise(&State->Random);
        State->NoiseHighPass1 += NoiseHighPassCoefficient * (Roar - State->NoiseHighPass1);
        Roar -= State->NoiseHighPass1;
        State->NoiseHighPass2 += NoiseHighPassCoefficient * (Roar - State->NoiseHighPass2);
        Roar -= State->NoiseHighPass2;
        State->NoiseLow1 += NoiseLowCoefficient * (Roar - State->NoiseLow1);
        State->NoiseLow2 += NoiseLowCoefficient * (State->NoiseLow1 - State->NoiseLow2);

        Output = (State->Traffic2 * 2.5f) + WindowWasherAudio_RunHorn(&State->Horn, ToneCoefficient) + WindowWasherAudio_RunBell(&State->Bell);
        Output += State->NoiseLow2 * State->Swell * WWA_CITY_NOISE_LEVEL;
        /* Full level, then a steady fade to silence. */
        Fade = (State->Sample < HoldSamples) ? 1.0f : (1.0f - ((float)(State->Sample - HoldSamples) / (float)FadeSamples));
        State->Sample++;

        Samples[Index] = (Output * Fade * WWA_CITY_LEVEL) * WWA_MASTER_LEVEL;
    }
}

static void WindowWasherAudio_StartCity(void *Context)
{
    WindowWasherAudio_CityStateTypeDef *State = &WindowWasherAudio_City;
    const uint32_t RestartCount = State->RestartCount + 1U;

    (void)Context;

    *State = (WindowWasherAudio_CityStateTypeDef){ 0 };
    State->RestartCount = RestartCount;
    State->Random = 0x9E3779B9U ^ (State->RestartCount * 0x85EBCA6BU);
    WindowWasherAudio_ScheduleCity(State);
    State->Swell = 1.0f;
    State->SwellTarget = 1.0f;
}

static const Mixer_SynthTypeDef WindowWasherAudio_CitySynth =
{
    .Start = WindowWasherAudio_StartCity,
    .Receive = NULL,
    .Render = WindowWasherAudio_RenderCity
};

/* -------------------------------------------------------------------------- */
/* Squeegee                                                                   */
/* -------------------------------------------------------------------------- */

/* Begin the next stroke with a slightly random length and a higher pitch. */
static void WindowWasherAudio_StartStroke(WindowWasherAudio_SqueegeeStateTypeDef *State)
{
    State->StrokeSample = 0U;
    State->StrokeSamples = Sound_Seconds(WWA_SQUEEGEE_STROKE_SECONDS * (1.0f + (WWA_SQUEEGEE_STROKE_VARIATION * Sound_Noise(&State->Random))));
    State->StrokeSamples = (State->StrokeSamples > 0U) ? State->StrokeSamples : 1U;

    if(State->Stroke > 0U)
    {
        State->Pitch *= WWA_SQUEEGEE_STROKE_PITCH_STEP;
        State->GapSamples = Sound_Seconds(WWA_SQUEEGEE_GAP_SECONDS);
    }
}

/*
 * Rubber on wet glass. Each stroke speeds up and slows down. At speed the
 * blade sticks and slips evenly, a squeal whose pitch follows the speed; when
 * slow it catches irregularly, a gritty chatter. Both ring the glass, over
 * the swish of water under the blade.
 */
static void WindowWasherAudio_RenderSqueegee(float *Samples, uint32_t SampleCount, void *Context)
{
    WindowWasherAudio_SqueegeeStateTypeDef *State = &WindowWasherAudio_Squeegee;
    const float ToneCoefficient = Sound_LowPassCoefficient(WWA_SQUEEGEE_TONE_CUTOFF_HZ);
    const uint32_t AttackSamples = Sound_Seconds(WWA_SQUEEGEE_ATTACK_SECONDS) + 1U;
    const uint32_t ReleaseSamples = Sound_Seconds(WWA_SQUEEGEE_RELEASE_SECONDS) + 1U;

    (void)Context;

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Noise = Sound_Noise(&State->Random);
        float Progress;
        float Speed;
        float Lock;
        float SqueakHz;
        float Wave;
        float Grit = 0.0f;
        float Direct;
        float Ring;
        float Swish;
        float Envelope;

        if((State->Stroke >= WWA_SQUEEGEE_STROKES) || (State->GapSamples > 0U))
        {
            State->GapSamples -= (State->GapSamples > 0U) ? 1U : 0U;
            Samples[Index] = 0.0f;
            continue;
        }

        /* Blade speed rises and falls over the stroke. */
        Progress = (float)State->StrokeSample / (float)State->StrokeSamples;
        Speed = sinf(0.5f * SOUND_TWO_PI * Progress);

        /* Fast enough and the blade locks into an even squeal. */
        Lock = (Speed - WWA_SQUEEGEE_LOCK_SPEED) / WWA_SQUEEGEE_LOCK_SOFTNESS;
        Lock = (Lock < 0.0f) ? 0.0f : ((Lock > 1.0f) ? 1.0f : Lock);

        SqueakHz = (WWA_SQUEEGEE_LOW_HZ + ((WWA_SQUEEGEE_HIGH_HZ - WWA_SQUEEGEE_LOW_HZ) * Speed)) * State->Pitch * (1.0f + State->Jitter);
        State->Phase += SqueakHz / SOUND_SAMPLE_RATE;

        /* Every slip varies a little in timing and strength. */
        if(State->Phase >= 1.0f)
        {
            State->Phase -= 1.0f;
            State->Jitter = WWA_SQUEEGEE_JITTER * Noise;
            State->CycleLevel = 1.0f + (WWA_SQUEEGEE_ROUGHNESS * Sound_Noise(&State->Random));
        }

        /* Stick-slip: a slow build while the rubber sticks, then a sudden slip. */
        if(State->Phase < WWA_SQUEEGEE_STICK_FRACTION)
        {
            Wave = -1.0f + (2.0f * State->Phase / WWA_SQUEEGEE_STICK_FRACTION);
        }
        else
        {
            Wave = 1.0f - (2.0f * (State->Phase - WWA_SQUEEGEE_STICK_FRACTION) / (1.0f - WWA_SQUEEGEE_STICK_FRACTION));
        }

        State->Tone += ToneCoefficient * (Wave - State->Tone);

        /* Slow blade: random catches instead of a steady tone. */
        if((0.5f + (0.5f * Sound_Noise(&State->Random))) < ((WWA_SQUEEGEE_GRIT_RATE_HZ * (1.0f - Lock)) / SOUND_SAMPLE_RATE))
        {
            Grit = Sound_Noise(&State->Random);
        }

        Direct = (State->Tone * Lock * State->CycleLevel) + (Grit * WWA_SQUEEGEE_GRIT_LEVEL);
        Ring = Sound_BandPass(&State->Resonance, Direct, Sound_BandPassCoefficient(WWA_SQUEEGEE_RESONANCE_HZ), WWA_SQUEEGEE_RESONANCE_DAMPING) * WWA_SQUEEGEE_RESONANCE_DAMPING;
        Swish = Sound_BandPass(&State->Swish, Noise, Sound_BandPassCoefficient(WWA_SQUEEGEE_SWISH_HZ), WWA_SQUEEGEE_SWISH_DAMPING) * Speed;

        Envelope = 1.0f;
        Envelope = (State->StrokeSample < AttackSamples) ? ((float)State->StrokeSample / (float)AttackSamples) : Envelope;
        Envelope = ((State->StrokeSamples - State->StrokeSample) < ReleaseSamples) ? ((float)(State->StrokeSamples - State->StrokeSample) / (float)ReleaseSamples) : Envelope;
        Envelope *= (1.0f - WWA_SQUEEGEE_SPEED_LOUDNESS) + (WWA_SQUEEGEE_SPEED_LOUDNESS * Speed);

        Samples[Index] = (((Direct * (1.0f - WWA_SQUEEGEE_RESONANCE_LEVEL)) + (Ring * WWA_SQUEEGEE_RESONANCE_LEVEL) + (Swish * WWA_SQUEEGEE_SWISH_LEVEL)) * Envelope * WWA_SQUEEGEE_LEVEL) * WWA_MASTER_LEVEL;

        State->StrokeSample++;

        if(State->StrokeSample >= State->StrokeSamples)
        {
            State->Stroke++;
            WindowWasherAudio_StartStroke(State);
        }
    }
}

static void WindowWasherAudio_StartSqueegee(void *Context)
{
    WindowWasherAudio_SqueegeeStateTypeDef *State = &WindowWasherAudio_Squeegee;
    const uint32_t RestartCount = State->RestartCount + 1U;

    (void)Context;

    *State = (WindowWasherAudio_SqueegeeStateTypeDef){ 0 };
    State->RestartCount = RestartCount;
    State->Random = 0x51ED27A3U ^ (State->RestartCount * 0x27D4EB2FU);
    State->Pitch = 1.0f + (WWA_SQUEEGEE_PITCH_VARIATION * Sound_Noise(&State->Random));
    State->CycleLevel = 1.0f;
    WindowWasherAudio_StartStroke(State);
}

static const Mixer_SynthTypeDef WindowWasherAudio_SqueegeeSynth =
{
    .Start = WindowWasherAudio_StartSqueegee,
    .Receive = NULL,
    .Render = WindowWasherAudio_RenderSqueegee
};

/* -------------------------------------------------------------------------- */
/* Crash                                                                      */
/* -------------------------------------------------------------------------- */

/*
 * The cart hitting the end of its track: a falling bass thump, a burst of
 * impact noise, and a short clank. Harder hits are louder and brighter.
 */
static void WindowWasherAudio_RenderCrash(float *Samples, uint32_t SampleCount, void *Context)
{
    WindowWasherAudio_CrashStateTypeDef *State = &WindowWasherAudio_Crash;
    const uint32_t LengthSamples = Sound_Seconds(WWA_CRASH_SECONDS);
    const float BodyDecay = Sound_DecayCoefficient(WWA_CRASH_BODY_DECAY_SECONDS);
    const float PitchDrop = Sound_DecayCoefficient(WWA_CRASH_PITCH_DROP_SECONDS);
    const float ImpactDecay = Sound_DecayCoefficient(WWA_CRASH_IMPACT_DECAY_SECONDS);

    (void)Context;

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        float Output;

        if(State->Sample >= LengthSamples)
        {
            Samples[Index] = 0.0f;
            continue;
        }

        State->Sample++;

        State->BodyHz = WWA_CRASH_BODY_END_HZ + ((State->BodyHz - WWA_CRASH_BODY_END_HZ) * PitchDrop);
        State->BodyPhase += State->BodyHz / SOUND_SAMPLE_RATE;
        State->BodyPhase -= (State->BodyPhase >= 1.0f) ? 1.0f : 0.0f;
        State->BodyEnvelope *= BodyDecay;
        State->ImpactEnvelope *= ImpactDecay;
        State->Impact += State->ImpactCoefficient * ((Sound_Noise(&State->Random) * State->ImpactEnvelope) - State->Impact);

        Output = State->Strength * ((sinf(SOUND_TWO_PI * State->BodyPhase) * State->BodyEnvelope * 0.9f) + (State->Impact * 2.5f));
        Output += WindowWasherAudio_RunResonator(&State->Clank[0]) + WindowWasherAudio_RunResonator(&State->Clank[1]);

        Samples[Index] = (Output * WWA_CRASH_LEVEL) * WWA_MASTER_LEVEL;
    }
}

static void WindowWasherAudio_StartCrash(void *Context)
{
    WindowWasherAudio_CrashStateTypeDef *State = &WindowWasherAudio_Crash;
    const uint32_t RestartCount = State->RestartCount + 1U;

    (void)Context;

    *State = (WindowWasherAudio_CrashStateTypeDef){ 0 };
    State->RestartCount = RestartCount;
    State->Random = 0x7F4A7C15U ^ (State->RestartCount * 0x94D049BBU);
    State->BodyHz = WWA_CRASH_BODY_START_HZ;
    State->BodyEnvelope = 1.0f;
    State->ImpactEnvelope = 1.0f;
    WindowWasherAudio_TuneResonator(&State->Clank[0], 330.0f, 0.22f);
    WindowWasherAudio_TuneResonator(&State->Clank[1], 710.0f, 0.15f);
}

/* The message is the impact strength, 0 to 1: harder hits are louder and brighter. */
static void WindowWasherAudio_ReceiveCrash(const void *Message, uint32_t Size, void *Context)
{
    WindowWasherAudio_CrashStateTypeDef *State = &WindowWasherAudio_Crash;
    float Strength;

    (void)Context;

    if(Size != sizeof(Strength))
    {
        return;
    }

    (void)memcpy(&Strength, Message, sizeof(Strength));
    State->Strength = WWA_CRASH_MINIMUM_STRENGTH + ((1.0f - WWA_CRASH_MINIMUM_STRENGTH) * Strength);
    State->ImpactCoefficient = Sound_LowPassCoefficient(400.0f + (800.0f * Strength));
    WindowWasherAudio_StrikeResonator(&State->Clank[0], 0.25f * State->Strength);
    WindowWasherAudio_StrikeResonator(&State->Clank[1], 0.15f * State->Strength);
}

static const Mixer_SynthTypeDef WindowWasherAudio_CrashSynth =
{
    .Start = WindowWasherAudio_StartCrash,
    .Receive = WindowWasherAudio_ReceiveCrash,
    .Render = WindowWasherAudio_RenderCrash
};

/* -------------------------------------------------------------------------- */
/* Yell                                                                       */
/* -------------------------------------------------------------------------- */

/*
 * The worker's falling "Waaaah!": a pulsed airflow source like the vocal
 * folds, with breath noise, run through four vowel formants. The pitch jumps
 * up in panic, then falls; the voice starts strained and raspy, wavers, and
 * sags toward "oh" as he runs out of breath. Distance fades and dulls it,
 * and a high-pass keeps the low voice out of the small speaker's excursion
 * limit.
 */
static void WindowWasherAudio_RenderYell(float *Samples, uint32_t SampleCount, void *Context)
{
    static const float StartHz[WWA_YELL_FORMANTS] = { 350.0f, 750.0f, 2400.0f, 3300.0f };     /* "w"         */
    static const float OpenHz[WWA_YELL_FORMANTS] = { 850.0f, 1300.0f, 2600.0f, 3400.0f };     /* shouted "a" */
    static const float CloseHz[WWA_YELL_FORMANTS] = { 600.0f, 950.0f, 2500.0f, 3300.0f };     /* "oh"        */
    static const float BandwidthHz[WWA_YELL_FORMANTS] = { 90.0f, 110.0f, 160.0f, 250.0f };
    WindowWasherAudio_YellStateTypeDef *State = &WindowWasherAudio_Yell;
    const uint32_t LengthSamples = Sound_Seconds(WWA_YELL_SECONDS);
    const float HighPassCoefficient = Sound_LowPassCoefficient(WWA_YELL_HIGH_PASS_HZ);
    const float OpenPeak = WWA_YELL_OPEN_QUOTIENT * (1.0f - WWA_YELL_CLOSING_FRACTION);
    float BlockSeconds;
    float BlockProgress;
    float DistanceCoefficient;

    (void)Context;

    BlockSeconds = (float)State->Sample / SOUND_SAMPLE_RATE;
    BlockProgress = (BlockSeconds < WWA_YELL_SECONDS) ? (BlockSeconds / WWA_YELL_SECONDS) : 1.0f;

    /* The vowel opens from "w" to "a", holds, then sags toward "oh". */
    for(uint32_t Formant = 0U; Formant < WWA_YELL_FORMANTS; Formant++)
    {
        float FrequencyHz = OpenHz[Formant];

        if(BlockSeconds < WWA_YELL_VOWEL_OPEN_SECONDS)
        {
            FrequencyHz = StartHz[Formant] + ((OpenHz[Formant] - StartHz[Formant]) * (BlockSeconds / WWA_YELL_VOWEL_OPEN_SECONDS));
        }
        else if((BlockSeconds > WWA_YELL_VOWEL_CLOSE_SECONDS) && (BlockSeconds < WWA_YELL_SECONDS))
        {
            FrequencyHz += (CloseHz[Formant] - OpenHz[Formant]) * ((BlockSeconds - WWA_YELL_VOWEL_CLOSE_SECONDS) / (WWA_YELL_SECONDS - WWA_YELL_VOWEL_CLOSE_SECONDS));
        }

        WindowWasherAudio_TuneFormant(&State->Formants[Formant], FrequencyHz, BandwidthHz[Formant]);
    }

    /* Air absorption: the highs drop away first as he falls further. */
    DistanceCoefficient = Sound_LowPassCoefficient(6000.0f - (4800.0f * BlockProgress));

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Noise = Sound_Noise(&State->Random);
        float Seconds;
        float Progress;
        float PitchHz;
        float Flow;
        float Voice;
        float Envelope;

        if(State->Sample >= LengthSamples)
        {
            Samples[Index] = 0.0f;
            continue;
        }

        Seconds = (float)State->Sample / SOUND_SAMPLE_RATE;
        Progress = Seconds / WWA_YELL_SECONDS;
        State->Sample++;

        if(Seconds < WWA_YELL_RISE_SECONDS)
        {
            PitchHz = WWA_YELL_START_HZ + ((WWA_YELL_PEAK_HZ - WWA_YELL_START_HZ) * (Seconds / WWA_YELL_RISE_SECONDS));
        }
        else
        {
            PitchHz = WWA_YELL_PEAK_HZ + ((WWA_YELL_END_HZ - WWA_YELL_PEAK_HZ) * ((Seconds - WWA_YELL_RISE_SECONDS) / (WWA_YELL_SECONDS - WWA_YELL_RISE_SECONDS)));
        }

        State->VibratoPhase += WWA_YELL_VIBRATO_HZ / SOUND_SAMPLE_RATE;
        State->VibratoPhase -= (State->VibratoPhase >= 1.0f) ? 1.0f : 0.0f;
        PitchHz *= State->Pitch * (1.0f + (WWA_YELL_VIBRATO_DEPTH * sinf(SOUND_TWO_PI * State->VibratoPhase)) + (WWA_YELL_JITTER * State->Jitter));

        /* A real voice never repeats a cycle exactly: vary each one's pitch and level. */
        State->Phase += PitchHz / SOUND_SAMPLE_RATE;

        if(State->Phase >= 1.0f)
        {
            const float Strain = (Seconds < WWA_YELL_STRAIN_SECONDS) ? (1.0f - (Seconds / WWA_YELL_STRAIN_SECONDS)) : 0.0f;

            State->Phase -= 1.0f;
            State->Jitter += 0.3f * (Noise - State->Jitter);
            State->CycleLevel = 1.0f + (WWA_YELL_SHIMMER * Sound_Noise(&State->Random));
            State->OddCycle = !State->OddCycle;

            /* Strained voice: every other pulse weaker, for a raspy undertone. */
            if(State->OddCycle)
            {
                State->CycleLevel *= 1.0f - (WWA_YELL_ROUGHNESS * Strain);
            }
        }

        /* Airflow through the vocal folds: a smooth opening, a quicker close. */
        if(State->Phase < OpenPeak)
        {
            Flow = 0.5f * (1.0f - cosf(0.5f * SOUND_TWO_PI * State->Phase / OpenPeak));
        }
        else if(State->Phase < WWA_YELL_OPEN_QUOTIENT)
        {
            Flow = cosf(0.25f * SOUND_TWO_PI * (State->Phase - OpenPeak) / (WWA_YELL_OPEN_QUOTIENT - OpenPeak));
        }
        else
        {
            Flow = 0.0f;
        }

        Flow *= State->CycleLevel;

        /* The mouth radiates the change in airflow, plus breath while the folds are open. */
        Voice = (Flow - State->PreviousFlow) * (SOUND_SAMPLE_RATE / PitchHz) * WWA_YELL_CLOSING_FRACTION * 0.5f;
        Voice += Noise * WWA_YELL_BREATH * (0.3f + Flow) * (1.0f + Progress);
        State->PreviousFlow = Flow;

        /* A shout is bright: lift the upper harmonics. */
        Flow = Voice;
        Voice -= WWA_YELL_BRIGHTNESS * State->PreviousSource;
        State->PreviousSource = Flow;

        for(uint32_t Formant = 0U; Formant < WWA_YELL_FORMANTS; Formant++)
        {
            Voice = WindowWasherAudio_RunFormant(&State->Formants[Formant], Voice);
        }

        /* Keep the low voice out of the small speaker's excursion limit. */
        State->HighPass1 += HighPassCoefficient * (Voice - State->HighPass1);
        Voice -= State->HighPass1;
        State->HighPass2 += HighPassCoefficient * (Voice - State->HighPass2);
        Voice -= State->HighPass2;

        State->Distance += DistanceCoefficient * (Voice - State->Distance);

        Envelope = (Seconds < WWA_YELL_ATTACK_SECONDS) ? (Seconds / WWA_YELL_ATTACK_SECONDS) : 1.0f;
        Envelope *= (1.0f - Progress) / (1.0f + (3.0f * Progress * Progress));

        Samples[Index] = (State->Distance * Envelope * WWA_YELL_LEVEL) * WWA_MASTER_LEVEL;
    }
}

static void WindowWasherAudio_StartYell(void *Context)
{
    WindowWasherAudio_YellStateTypeDef *State = &WindowWasherAudio_Yell;
    const uint32_t RestartCount = State->RestartCount + 1U;

    (void)Context;

    *State = (WindowWasherAudio_YellStateTypeDef){ 0 };
    State->RestartCount = RestartCount;
    State->Random = 0x3C6EF372U ^ (State->RestartCount * 0xA54FF53AU);
    State->Pitch = Sound_RandomRange(&State->Random, 0.92f, 1.08f);
    State->CycleLevel = 1.0f;
}

static const Mixer_SynthTypeDef WindowWasherAudio_YellSynth =
{
    .Start = WindowWasherAudio_StartYell,
    .Receive = NULL,
    .Render = WindowWasherAudio_RenderYell
};

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

/* Send the cart or wind synth a target, restarting it first when Restart is true. */
static void WindowWasherAudio_SendControl(Mixer_ChannelTypeDef Channel, bool Restart, float Target)
{
    const WindowWasherAudio_ControlMessageTypeDef Message = { Restart, Target };

    (void)Mixer_Send(Channel, &Message, sizeof(Message));
}

void WindowWasherAudio_StartRound(void)
{
    /* A new round cuts off the previous round's fall. */
    (void)Mixer_Stop(WWA_SQUEEGEE_CHANNEL);
    (void)Mixer_Stop(WWA_CRASH_CHANNEL);
    (void)Mixer_Stop(WWA_YELL_CHANNEL);

    (void)Mixer_PlaySynth(WWA_CART_CHANNEL, &WindowWasherAudio_CartSynth, NULL);
    WindowWasherAudio_SendControl(WWA_CART_CHANNEL, true, 0.0f);
    (void)Mixer_PlaySynth(WWA_WIND_CHANNEL, &WindowWasherAudio_WindSynth, NULL);
    WindowWasherAudio_SendControl(WWA_WIND_CHANNEL, true, WWA_WIND_MINIMUM_LEVEL);
    WindowWasherAudio_CityPlaying = Mixer_PlaySynth(WWA_CITY_CHANNEL, &WindowWasherAudio_CitySynth, NULL);
}

void WindowWasherAudio_Update(float CartSpeed, uint64_t RoundElapsedMilliseconds)
{
    float WindRamp = (float)RoundElapsedMilliseconds / (float)WWA_WIND_RAMP_MILLISECONDS;

    WindRamp = (WindRamp > 1.0f) ? 1.0f : WindRamp;
    CartSpeed = (CartSpeed > 1.0f) ? 1.0f : ((CartSpeed < 0.0f) ? 0.0f : CartSpeed);

    WindowWasherAudio_SendControl(WWA_CART_CHANNEL, false, CartSpeed);
    WindowWasherAudio_SendControl(WWA_WIND_CHANNEL, false, WWA_WIND_MINIMUM_LEVEL + ((WWA_WIND_MAXIMUM_LEVEL - WWA_WIND_MINIMUM_LEVEL) * WindRamp));

    /* The city has faded out; free its channel. */
    if(WindowWasherAudio_CityPlaying && (RoundElapsedMilliseconds >= WWA_CITY_STOP_MILLISECONDS))
    {
        (void)Mixer_Stop(WWA_CITY_CHANNEL);
        WindowWasherAudio_CityPlaying = false;
    }
}

void WindowWasherAudio_Stop(void)
{
    (void)Mixer_Stop(WWA_CART_CHANNEL);
    (void)Mixer_Stop(WWA_WIND_CHANNEL);
    (void)Mixer_Stop(WWA_CITY_CHANNEL);
    (void)Mixer_Stop(WWA_SQUEEGEE_CHANNEL);
    (void)Mixer_Stop(WWA_CRASH_CHANNEL);
    (void)Mixer_Stop(WWA_YELL_CHANNEL);
    WindowWasherAudio_CityPlaying = false;
}

void WindowWasherAudio_PlaySqueegee(void)
{
    (void)Mixer_PlaySynth(WWA_SQUEEGEE_CHANNEL, &WindowWasherAudio_SqueegeeSynth, NULL);
}

void WindowWasherAudio_PlayCrash(float ImpactSpeed)
{
    const float Strength = (ImpactSpeed > 1.0f) ? 1.0f : ((ImpactSpeed < 0.0f) ? 0.0f : ImpactSpeed);

    (void)Mixer_PlaySynth(WWA_CRASH_CHANNEL, &WindowWasherAudio_CrashSynth, NULL);
    (void)Mixer_Send(WWA_CRASH_CHANNEL, &Strength, sizeof(Strength));
    (void)Mixer_PlaySynth(WWA_YELL_CHANNEL, &WindowWasherAudio_YellSynth, NULL);
}

/* The cart and wind carry on from where they were paused. */
void WindowWasherAudio_Resume(void)
{
    (void)Mixer_PlaySynth(WWA_CART_CHANNEL, &WindowWasherAudio_CartSynth, NULL);
    (void)Mixer_PlaySynth(WWA_WIND_CHANNEL, &WindowWasherAudio_WindSynth, NULL);
}
