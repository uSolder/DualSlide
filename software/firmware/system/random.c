/**
 * @file random.c
 * @brief Random numbers for games: xoshiro128** seeded from the hardware RNG.
 */

#include "random.h"

#include "entropy.h"
#include "system_time.h"

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Seeded with a fixed value too, so the generator works even before Random_Init(). */
static uint32_t Random_State[4] = { 0x9E3779B9UL, 0x243F6A88UL, 0xB7E15162UL, 0x6A09E667UL };

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint32_t Random_RotateLeft(uint32_t Value, uint32_t Bits)
{
    return (Value << Bits) | (Value >> (32U - Bits));
}

/* A seed from the hardware generator, or should it fail, one that still differs each call. */
static uint32_t Random_GetSeed(uint32_t Index)
{
    uint32_t Seed;

    if(Entropy_Read(&Seed))
    {
        return Seed;
    }

    return (SystemTime_GetMilliseconds() * 0x9E3779B9UL) ^ ((Index + 1U) * 0x85EBCA6BUL);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Random_Init(void)
{
    uint32_t Index;

    for(Index = 0U; Index < 4U; Index++)
    {
        Random_State[Index] = Random_GetSeed(Index);
    }

    /* The all-zero state would only ever produce zeros. */
    if((Random_State[0] | Random_State[1] | Random_State[2] | Random_State[3]) == 0U)
    {
        Random_State[0] = 1U;
    }
}

uint32_t Random_Next(void)
{
    const uint32_t Result = Random_RotateLeft(Random_State[1] * 5U, 7U) * 9U;
    const uint32_t Shifted = Random_State[1] << 9U;

    Random_State[2] ^= Random_State[0];
    Random_State[3] ^= Random_State[1];
    Random_State[1] ^= Random_State[2];
    Random_State[0] ^= Random_State[3];
    Random_State[2] ^= Shifted;
    Random_State[3] = Random_RotateLeft(Random_State[3], 11U);

    return Result;
}

int32_t Random_Between(int32_t Minimum, int32_t Maximum)
{
    uint32_t Range;
    uint32_t Threshold;
    uint32_t Value;

    if(Maximum < Minimum)
    {
        const int32_t Swap = Minimum;

        Minimum = Maximum;
        Maximum = Swap;
    }

    /* Number of possible results; zero means all 2^32 of them. */
    Range = ((uint32_t)Maximum - (uint32_t)Minimum) + 1U;

    if(Range == 0U)
    {
        return (int32_t)Random_Next();
    }

    /* Reject the few values that would make the low results more likely. */
    Threshold = (0U - Range) % Range;

    do
    {
        Value = Random_Next();
    } while(Value < Threshold);

    return (int32_t)((uint32_t)Minimum + (Value % Range));
}
