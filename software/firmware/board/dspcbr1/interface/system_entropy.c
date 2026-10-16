/**
 * @file system_entropy.c
 * @brief Embedded seeds from the hardware random number generator.
 */

#include "system_entropy.h"

#include "entropy.h"
#include "system_time.h"

#include <stdint.h>

uint32_t SystemEntropy_GetSeed(void)
{
    static uint32_t Calls;
    uint32_t Seed;

    if(Entropy_Read(&Seed))
    {
        return Seed;
    }

    /* Should the generator fail, still differ from call to call. */
    Calls++;

    return (SystemTime_GetMilliseconds() * 0x9E3779B9UL) ^ (Calls * 0x85EBCA6BUL);
}
