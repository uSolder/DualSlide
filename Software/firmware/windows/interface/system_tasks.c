/**
 * @file system_tasks.c
 * @brief Windows implementation of target-specific system tasks.
 */

#include "system_tasks.h"

#include <SDL3/SDL.h>

bool SystemTasks_Process(void)
{
    SDL_PumpEvents();

    if(SDL_HasEvent(SDL_EVENT_QUIT) || SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED))
    {
        return false;
    }

    return true;
}