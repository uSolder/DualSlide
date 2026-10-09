/**
 * @file synth.h
 * @brief Building blocks for synths: noise, oscillators, filters, and timing.
 *
 * Small inline helpers shared by every game's sound code, for use in a
 * synth's Start, Receive and Render functions (see Mixer_SynthTypeDef in
 * mixer.h). Times are in seconds, frequencies in hertz, at the mixer's
 * sample rate.
 */

#ifndef SYNTH_H
#define SYNTH_H

#include "mixer.h"

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define SYNTH_SAMPLE_RATE                   ((float)MIXER_SAMPLE_RATE_HZ)
#define SYNTH_SAMPLE_PERIOD                 (1.0f / SYNTH_SAMPLE_RATE)
#define SYNTH_TWO_PI                        (6.2831853f)

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief State-variable filter state, used by Synth_BandPass().
 */
typedef struct
{
    float Low;
    float Band;
} Synth_FilterTypeDef;

/* -------------------------------------------------------------------------- */
/* Noise                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief White noise from -1.0 to 1.0, from a xorshift generator.
 *
 * @param State Generator state; seed it with any non-zero value.
 */
static inline float Synth_Noise(uint32_t *State)
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
static inline float Synth_RandomRange(uint32_t *State, float Minimum, float Maximum)
{
    return Minimum + ((Maximum - Minimum) * (0.5f + (0.5f * Synth_Noise(State))));
}

/* -------------------------------------------------------------------------- */
/* Timing                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Number of samples in Seconds.
 */
static inline uint32_t Synth_Seconds(float Seconds)
{
    return (uint32_t)(Seconds * SYNTH_SAMPLE_RATE);
}

/**
 * @brief Per-sample multiplier that decays to 1/e in Seconds; 0 stops at once.
 */
static inline float Synth_DecayCoefficient(float Seconds)
{
    return (Seconds > 0.0f) ? expf(-1.0f / (Seconds * SYNTH_SAMPLE_RATE)) : 0.0f;
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
static inline float Synth_Sine(float Phase)
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

    X = SYNTH_TWO_PI * Turns;
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
static inline float Synth_PolyBlep(float Phase, float Step)
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
static inline float Synth_SoftClip(float Value)
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
static inline float Synth_LowPassCoefficient(float CutoffHz)
{
    return 1.0f - expf(-SYNTH_TWO_PI * CutoffHz * SYNTH_SAMPLE_PERIOD);
}

/**
 * @brief Coefficient for Synth_BandPass() at a centre frequency.
 */
static inline float Synth_BandPassCoefficient(float FrequencyHz)
{
    return SYNTH_TWO_PI * FrequencyHz * SYNTH_SAMPLE_PERIOD;
}

/**
 * @brief State-variable band-pass filter.
 *
 * Keep the centre frequency below about MIXER_SAMPLE_RATE_HZ / 7.
 *
 * @param Filter      Filter state, zeroed to start.
 * @param Input       Input sample.
 * @param Coefficient From Synth_BandPassCoefficient().
 * @param Damping     Width of the band: small rings, about 1 is broad.
 *
 * @return The band-passed sample.
 */
static inline float Synth_BandPass(Synth_FilterTypeDef *Filter, float Input, float Coefficient, float Damping)
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

#endif /* SYNTH_H */
