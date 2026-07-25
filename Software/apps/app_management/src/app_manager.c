/**
 * @file app_manager.c
 * @brief Controls the currently active DualSlide application.
 */

#include "app_manager.h"

#include "window_washer.h"

#include <stddef.h>

static bool AppManager_Initialized = false;
static bool AppManager_Paused = false;

bool AppManager_Init(void)
{
    if(AppManager_Initialized)
    {
        return true;
    }

    if(!WindowWasher_Init())
    {
        return false;
    }

    AppManager_Paused = false;
    AppManager_Initialized = true;

    return true;
}

void AppManager_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    WindowWasher_Update(DeltaTimeMilliseconds);
}

void AppManager_Render(void)
{
    if(!AppManager_Initialized)
    {
        return;
    }

    WindowWasher_Render();
}

void AppManager_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context)
{
    (void)Context;

    if(Samples == NULL)
    {
        return;
    }

    for(uint32_t FrameIndex = 0U; FrameIndex < FrameCount; FrameIndex++)
    {
        Samples[FrameIndex] = 0;
    }
}

void AppManager_Pause(void)
{
    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    WindowWasher_Pause();
    AppManager_Paused = true;
}

void AppManager_Resume(void)
{
    if(!AppManager_Initialized || !AppManager_Paused)
    {
        return;
    }

    WindowWasher_Resume();
    AppManager_Paused = false;
}

void AppManager_Shutdown(void)
{
    if(!AppManager_Initialized)
    {
        return;
    }

    WindowWasher_Shutdown();

    AppManager_Paused = false;
    AppManager_Initialized = false;
}