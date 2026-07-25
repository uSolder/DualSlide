#include "system_time.h"

#include <SDL3/SDL.h>

uint32_t SystemTime_GetMilliseconds(void)
{
    return (uint32_t)SDL_GetTicks();
}