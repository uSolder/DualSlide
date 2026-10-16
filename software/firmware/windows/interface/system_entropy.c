/**
 * @file system_entropy.c
 * @brief Windows simulator seeds from the clock.
 */

#include "system_entropy.h"

#include <stdint.h>
#include <time.h>

uint32_t SystemEntropy_GetSeed(void)
{
    static uint32_t Calls;

    Calls++;

    return ((uint32_t)time(NULL) * 0x9E3779B9UL) ^ ((uint32_t)clock() * 0xC2B2AE35UL) ^ (Calls * 0x85EBCA6BUL);
}
