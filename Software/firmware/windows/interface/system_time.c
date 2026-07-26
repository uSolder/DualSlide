#include "system_time.h"

#include <SDL3/SDL.h>

uint32_t SystemTime_GetMilliseconds(void)
{
    return (uint32_t)SDL_GetTicks();
}


void SystemTime_DelayMilliseconds(uint32_t DelayMilliseconds)
{
    const uint32_t StartTimeMilliseconds = SystemTime_GetMilliseconds();

    while((SystemTime_GetMilliseconds() - StartTimeMilliseconds) < DelayMilliseconds);
}