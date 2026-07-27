/**
 * @file system_time.c
 * @brief Embedded monotonic system-time implementation.
 */

#include "system_time.h"

#include "time.h"

#include <stdint.h>

uint32_t SystemTime_GetMilliseconds(void)
{
    return Time_GetMilliseconds();
}

void SystemTime_DelayMilliseconds(uint32_t Milliseconds)
{
    const uint32_t start = Time_GetMilliseconds();

    while ((uint32_t)(Time_GetMilliseconds() - start) < Milliseconds)
    {
    }
}