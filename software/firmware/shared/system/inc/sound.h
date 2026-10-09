/**
 * @file sound.h
 * @brief Sound effects engine: describe a sound as a small table, play it with one call.
 *
 * QUICK START
 *
 *   A sound is a list of layers. Each layer is one tone or noise with its own
 *   pitch, volume shape and filter. Leave out anything you don't need: every
 *   field left at 0 uses a sensible default.
 *
 *       static const Sound_LayerTypeDef BeepLayers[] = {
 *           { .Wave = SOUND_SQUARE, .Hz = 880.0f, .Decay = 0.1f },
 *       };
 *       static const Sound_TypeDef Beep = { SOUND_LAYERS(BeepLayers) };
 *
 *       Sound_Play(&Beep);
 *
 *   A falling laser:      { .Wave = SOUND_SAW, .Hz = 1500, .EndHz = 200, .Decay = 0.3f }
 *   An explosion:         { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 3000,
 *                           .FilterEndHz = 200, .Decay = 0.8f, .Drive = 2.0f }
 *   A coin:               two layers, { .Hz = 988, .Hold = 0.06f, .Decay = 0.05f } then
 *                           { .Delay = 0.06f, .Hz = 1319, .Decay = 0.3f }
 *   A tune:               one layer per note, each with its own .Delay.
 *
 *   Make it louder, quieter, higher or lower when you play it:
 *
 *       Sound_PlayWith(&Beep, 0.5f, 1.2f);       // half volume, 20% higher
 *
 *   Sounds that last until you stop them (engines, alarms, music loops) set
 *   .Loop or .RepeatSeconds, and you keep the handle Sound_Play() returns:
 *
 *       Sound_HandleTypeDef Engine = Sound_Play(&EngineHum);
 *       Sound_SetPitch(Engine, 1.0f + Speed);    // follow the game every frame
 *       Sound_Stop(Engine);                      // fades out over its Decay
 *
 * HOW A LAYER PLAYS
 *
 *   After .Delay seconds the layer starts. Its volume rises over .Attack,
 *   stays full for .Hold, then fades away over .Decay. Meanwhile its pitch
 *   slides from .Hz to .EndHz, and its filter from .FilterHz to .FilterEndHz,
 *   both over about .SlideSeconds, fast at first and easing into the end.
 *   In a looping sound every layer stays at full volume until the sound is
 *   stopped, then fades over .Decay.
 *
 * PATCHES
 *
 *   When the built-in waves can't make a sound, a game can supply its own
 *   oscillator: a Sound_PatchTypeDef with a Next() function that returns one
 *   sample at a time. Put it in the layer's .Patch field; the layer's
 *   envelope, filter, drive and volume still apply on top. Keep patches in a
 *   "<game>_sound_patch.c" file, next to the game's sound tables. The
 *   building blocks at the end of this file (noise, a fast sine, filters,
 *   timing) are there for writing patches.
 *
 * All functions are called from the game loop. Sounds stop automatically when
 * the game exits. The engine plays on its own mixer channel, so a game can
 * still use the other channels directly through mixer.h.
 */

#ifndef SOUND_H
#define SOUND_H

#include "mixer.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Limits                                                                     */
/* -------------------------------------------------------------------------- */

/** Most layers in one sound. */
#define SOUND_MAX_LAYERS                    (32U)

/** Most sounds playing at once; playing another replaces the oldest. */
#define SOUND_MAX_PLAYING                   (16U)

/** Most layers sounding at once, over all sounds; the quietest is replaced. */
#define SOUND_MAX_VOICES                    (24U)

/** Size of a patch's own memory, in floats. */
#define SOUND_PATCH_STATE_FLOATS            (32U)

/* -------------------------------------------------------------------------- */
/* Describing a sound                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief What a layer sounds like.
 */
typedef enum
{
    SOUND_SINE = 0,   /**< Pure, soft tone (the default). */
    SOUND_TRIANGLE,   /**< Mellow tone, a little brighter than sine. */
    SOUND_SQUARE,     /**< Hollow, retro tone; .Duty changes its shape. */
    SOUND_SAW,        /**< Bright, buzzy tone. */
    SOUND_NOISE       /**< Hiss. With .Hz set, a pitched, crunchy noise. */
} Sound_WaveTypeDef;

/**
 * @brief How a layer's filter shapes it.
 */
typedef enum
{
    SOUND_NO_FILTER = 0, /**< Unfiltered (the default). */
    SOUND_LOWPASS,       /**< Keep below FilterHz: darker, muffled. */
    SOUND_HIGHPASS,      /**< Keep above FilterHz: thinner, brighter. */
    SOUND_BANDPASS       /**< Keep around FilterHz: nasal, whistling. */
} Sound_FilterTypeDef;

struct Sound_PatchTypeDef;

/**
 * @brief One layer of a sound: a tone or noise with its own shape.
 *
 * Every field is optional. A field left at 0 means:
 *   Hz 440 (noise: plain hiss), Volume 1.0, Decay 0.2 s, Duty 0.5,
 *   Resonance 0.7, VibratoHz 6, TremoloHz 6, FmRatio 1, and no slide,
 *   filter, vibrato, tremolo, FM or drive.
 */
typedef struct
{
    Sound_WaveTypeDef Wave;     /**< Tone or noise to play. */
    float Hz;                   /**< Pitch in hertz. */
    float EndHz;                /**< Pitch to slide to; 0 for no slide. */
    float SlideSeconds;         /**< About how long to reach EndHz and FilterEndHz, easing in; 0 for the layer's length. */

    float Delay;                /**< Seconds after the sound starts that this layer starts. */
    float Attack;               /**< Seconds to rise to full volume. */
    float Hold;                 /**< Seconds to stay at full volume. */
    float Decay;                /**< Seconds to fade to silence (also the fade when stopped). */
    float Volume;               /**< Layer volume, 1.0 is full. */

    float Duty;                 /**< SOUND_SQUARE only: fraction of each cycle that is high, 0.5 is square. */

    Sound_FilterTypeDef Filter; /**< Filter type. */
    float FilterHz;             /**< Filter frequency. */
    float FilterEndHz;          /**< Filter frequency to slide to; 0 for no slide. */
    float Resonance;            /**< Filter sharpness: 0.5 gentle, 5 ringing, 20 whistling. */

    float Vibrato;              /**< Pitch wobble, as a fraction of the pitch (0.02 is gentle). */
    float VibratoHz;            /**< Wobbles per second. */
    float Tremolo;              /**< Volume wobble, 0.0 to 1.0. */
    float TremoloHz;            /**< Volume wobbles per second. */

    float FmDepth;              /**< FM brightness: a second tone bends this one's phase. 1 to 5 is typical. */
    float FmRatio;              /**< FM tone pitch / layer pitch: 1 or 2 is warm, 3.5 is bell-like. */
    float FmDecay;              /**< Seconds for the FM brightness to fade away; 0 to keep it. */

    float Drive;                /**< Distortion: 1 is warm, 5 is crunchy, 20 is fuzz. */

    const struct Sound_PatchTypeDef *Patch; /**< A game's own oscillator, instead of Wave. */
    float Settings[4];          /**< Numbers for the patch to use as it likes. */
} Sound_LayerTypeDef;

/**
 * @brief A sound: its layers and how it plays.
 *
 * Fields left at 0 mean: volume 1.0, plays once, no random variation.
 */
typedef struct
{
    const Sound_LayerTypeDef *Layers; /**< Use SOUND_LAYERS(Array) to fill this and LayerCount. */
    uint8_t LayerCount;
    float Volume;                     /**< Volume of the whole sound, 1.0 is full. */
    bool Loop;                        /**< Keep playing until Sound_Stop(). */
    float RepeatSeconds;              /**< Play again every this many seconds; 0 to play once. */
    uint16_t RepeatCount;             /**< Times to play in all when repeating; 0 until stopped. */
    float RandomPitch;                /**< Vary the pitch of each play by up to this fraction (0.05 is subtle). */
    float RandomVolume;               /**< Vary the volume of each play by up to this fraction. */
    bool Single;                      /**< Playing it again stops the earlier play first. */
} Sound_TypeDef;

/** Fills a Sound_TypeDef's Layers and LayerCount from an array of layers. */
#define SOUND_LAYERS(Array) .Layers = (Array), .LayerCount = (uint8_t)(sizeof(Array) / sizeof((Array)[0]))

/** Identifies one play of a sound; 0 is never a playing sound. */
typedef uint16_t Sound_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/* Patches: a game's own oscillators                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief What a patch sees of the layer it is playing.
 */
typedef struct
{
    const Sound_LayerTypeDef *Layer;       /**< The layer: read Hz, Settings and so on. */
    float Hz;                              /**< Pitch now, with slide, vibrato and play pitch applied. */
    float Step;                            /**< Hz / sample rate: add to a phase once per sample. */
    float Control;                         /**< The value from Sound_SetControl(), 0 until set. */
    float Seconds;                         /**< Time since the layer started. */
    uint32_t Random;                       /**< Noise state for Sound_Noise(). */
    float State[SOUND_PATCH_STATE_FLOATS]; /**< The patch's own memory, zeroed when the layer starts. */
} Sound_PatchVoiceTypeDef;

/** The patch's memory as a structure of the patch's own: SOUND_PATCH_STATE(Voice, MyStateTypeDef). */
#define SOUND_PATCH_STATE(Voice, Type) ((Type *)(void *)(Voice)->State)

/**
 * @brief A game's own oscillator, used by a layer's .Patch.
 *
 * Both functions run in the audio context: keep them quick and don't call
 * any other Sound_ function from them.
 */
typedef struct Sound_PatchTypeDef
{
    void (*Start)(Sound_PatchVoiceTypeDef *Voice); /**< Set up State when the layer starts; may be NULL. */
    float (*Next)(Sound_PatchVoiceTypeDef *Voice); /**< Return the next sample, -1.0 to 1.0. */
} Sound_PatchTypeDef;

/* -------------------------------------------------------------------------- */
/* Playing sounds                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start the engine. Called once by the system at startup.
 */
void Sound_Init(void);

/**
 * @brief Play a sound.
 *
 * @return A handle for changing or stopping this play; ignore it if you don't need it.
 */
Sound_HandleTypeDef Sound_Play(const Sound_TypeDef *Sound);

/**
 * @brief Play a sound louder or quieter, higher or lower.
 *
 * @param Volume 1.0 as designed, 0.5 half, 2.0 double.
 * @param Pitch  1.0 as designed, 2.0 an octave up, 0.5 an octave down.
 */
Sound_HandleTypeDef Sound_PlayWith(const Sound_TypeDef *Sound, float Volume, float Pitch);

/**
 * @brief Stop a sound, letting it fade over its layers' Decay.
 */
void Sound_Stop(Sound_HandleTypeDef Handle);

/**
 * @brief Stop every sound quickly.
 */
void Sound_StopAll(void);

/**
 * @brief Change a playing sound's volume (1.0 as designed); changes are smoothed.
 */
void Sound_SetVolume(Sound_HandleTypeDef Handle, float Volume);

/**
 * @brief Change a playing sound's pitch (1.0 as designed); changes are smoothed.
 */
void Sound_SetPitch(Sound_HandleTypeDef Handle, float Pitch);

/**
 * @brief Pass a value to a playing sound's patches (Sound_PatchVoiceTypeDef.Control).
 */
void Sound_SetControl(Sound_HandleTypeDef Handle, float Value);

/**
 * @brief Return whether a sound is still playing (or about to start).
 */
bool Sound_IsPlaying(Sound_HandleTypeDef Handle);

/* -------------------------------------------------------------------------- */
/* Building blocks for patches                                                */
/* -------------------------------------------------------------------------- */

/*
 * Small helpers for writing a patch's Next() function, or a synth of your own
 * on a mixer channel. Times are in seconds, frequencies in hertz, at the
 * mixer's sample rate.
 */

#define SOUND_SAMPLE_RATE                   ((float)MIXER_SAMPLE_RATE_HZ)
#define SOUND_SAMPLE_PERIOD                 (1.0f / SOUND_SAMPLE_RATE)
#define SOUND_TWO_PI                        (6.2831853f)

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief State-variable filter state, used by Sound_BandPass().
 */
typedef struct
{
    float Low;
    float Band;
} Sound_BandPassStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Noise                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief White noise from -1.0 to 1.0, from a xorshift generator.
 *
 * @param State Generator state; seed it with any non-zero value.
 */
static inline float Sound_Noise(uint32_t *State)
{
    uint32_t Value = *State;

    Value ^= Value << 13U;
    Value ^= Value >> 17U;
    Value ^= Value << 5U;
    *State = Value;

    return ((float)Value / 2147483648.0f) - 1.0f;
}

/**
 * @brief Uniform random value from Minimum to Maximum.
 */
static inline float Sound_RandomRange(uint32_t *State, float Minimum, float Maximum)
{
    return Minimum + ((Maximum - Minimum) * (0.5f + (0.5f * Sound_Noise(State))));
}

/* -------------------------------------------------------------------------- */
/* Timing                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Number of samples in Seconds.
 */
static inline uint32_t Sound_Seconds(float Seconds)
{
    return (uint32_t)(Seconds * SOUND_SAMPLE_RATE);
}

/**
 * @brief Per-sample multiplier that decays to 1/e in Seconds; 0 stops at once.
 */
static inline float Sound_DecayCoefficient(float Seconds)
{
    return (Seconds > 0.0f) ? expf(-1.0f / (Seconds * SOUND_SAMPLE_RATE)) : 0.0f;
}

/* -------------------------------------------------------------------------- */
/* Oscillators and shaping                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fast sine of a phase in turns (1.0 is one cycle).
 *
 * Within 0.0002 of sinf() at a fraction of the cost: fold to a quarter
 * cycle, then a 7th-order polynomial.
 */
static inline float Sound_Sine(float Phase)
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

    X = SOUND_TWO_PI * Turns;
    Square = X * X;

    return -X * (1.0f - (Square * (0.16666667f - (Square * (0.0083333333f - (Square * 0.00019841270f))))));
}

/**
 * @brief Correction that smooths the jump in a saw or square wave, so high
 *        notes don't alias.
 *
 * @param Phase Oscillator phase in turns, 0.0 to 1.0.
 * @param Step  Phase advance per sample (frequency / sample rate).
 */
static inline float Sound_PolyBlep(float Phase, float Step)
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

/**
 * @brief Soft clipper: gently squashes large values towards -1.0 and 1.0.
 */
static inline float Sound_SoftClip(float Value)
{
    return Value / (1.0f + fabsf(Value));
}

/* -------------------------------------------------------------------------- */
/* Filters                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Coefficient for a one-pole low-pass filter:
 *        State += Coefficient * (Input - State).
 */
static inline float Sound_LowPassCoefficient(float CutoffHz)
{
    return 1.0f - expf(-SOUND_TWO_PI * CutoffHz * SOUND_SAMPLE_PERIOD);
}

/**
 * @brief Coefficient for Sound_BandPass() at a centre frequency.
 */
static inline float Sound_BandPassCoefficient(float FrequencyHz)
{
    return SOUND_TWO_PI * FrequencyHz * SOUND_SAMPLE_PERIOD;
}

/**
 * @brief State-variable band-pass filter.
 *
 * Keep the centre frequency below about MIXER_SAMPLE_RATE_HZ / 7.
 *
 * @param Filter      Filter state, zeroed to start.
 * @param Input       Input sample.
 * @param Coefficient From Sound_BandPassCoefficient().
 * @param Damping     Width of the band: small rings, about 1 is broad.
 *
 * @return The band-passed sample.
 */
static inline float Sound_BandPass(Sound_BandPassStateTypeDef *Filter, float Input, float Coefficient, float Damping)
{
    float High;

    Filter->Low += Coefficient * Filter->Band;
    High = Input - Filter->Low - (Damping * Filter->Band);
    Filter->Band += Coefficient * High;

    return Filter->Band;
}

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
