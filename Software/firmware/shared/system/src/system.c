#include "system.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "system_tasks.h"
#include "system_time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_SAMPLE_RATE_HZ          (48000U)
#define AUDIO_CHANNEL_COUNT           (1U)

#define FPS_SAMPLE_CAPACITY           (600U)
#define FPS_UPDATE_INTERVAL_MS        (1000ULL)
#define FPS_SLOW_SAMPLE_CAPACITY      ((FPS_SAMPLE_CAPACITY + 99U) / 100U)

/*
 * These variables are intentionally global and volatile so they can be added
 * directly to the debugger watch list.
 */
volatile float System_DebugAverageFps = 0.0f;
volatile float System_DebugOnePercentLowFps = 0.0f;

/*
 * Alternates between zero and one immediately before each call to
 * AppManager_Render(). The diagnostic game overlay can use this value to draw
 * a different labelled box into each alternating framebuffer.
 */
volatile uint32_t System_DebugRenderBufferIndex = 0U;
volatile uint32_t System_DebugBuffer0RenderCount = 0U;
volatile uint32_t System_DebugBuffer1RenderCount = 0U;

static uint32_t System_FrameTimeSamples[FPS_SAMPLE_CAPACITY];
static uint32_t System_FrameTimeSampleIndex;
static uint32_t System_FrameTimeSampleCount;

/**
 * @brief Store one complete frame duration in the rolling sample buffer.
 *
 * Frame duration is measured from the start of one frame to the start of the
 * following frame.
 *
 * @param FrameTimeMilliseconds Complete frame duration in milliseconds.
 */
static void System_RecordFrameTime(uint32_t FrameTimeMilliseconds)
{
    if(FrameTimeMilliseconds == 0U)
    {
        return;
    }

    System_FrameTimeSamples[System_FrameTimeSampleIndex] = FrameTimeMilliseconds;
    System_FrameTimeSampleIndex++;

    if(System_FrameTimeSampleIndex >= FPS_SAMPLE_CAPACITY)
    {
        System_FrameTimeSampleIndex = 0U;
    }

    if(System_FrameTimeSampleCount < FPS_SAMPLE_CAPACITY)
    {
        System_FrameTimeSampleCount++;
    }
}

/**
 * @brief Insert a frame duration into the descending slow-frame list.
 *
 * @param SlowFrameTimes Destination list sorted from largest to smallest.
 * @param StoredCount Current number of stored entries.
 * @param Capacity Maximum number of stored entries.
 * @param FrameTime Frame duration to insert.
 *
 * @return Updated number of stored entries.
 */
static uint32_t System_InsertSlowFrameTime(uint32_t *SlowFrameTimes, uint32_t StoredCount, uint32_t Capacity, uint32_t FrameTime)
{
    uint32_t InsertIndex;

    if((SlowFrameTimes == NULL) || (Capacity == 0U))
    {
        return StoredCount;
    }

    if(StoredCount < Capacity)
    {
        InsertIndex = StoredCount;

        while((InsertIndex > 0U) && (SlowFrameTimes[InsertIndex - 1U] < FrameTime))
        {
            SlowFrameTimes[InsertIndex] = SlowFrameTimes[InsertIndex - 1U];
            InsertIndex--;
        }

        SlowFrameTimes[InsertIndex] = FrameTime;

        return StoredCount + 1U;
    }

    if(FrameTime <= SlowFrameTimes[Capacity - 1U])
    {
        return StoredCount;
    }

    InsertIndex = Capacity - 1U;

    while((InsertIndex > 0U) && (SlowFrameTimes[InsertIndex - 1U] < FrameTime))
    {
        SlowFrameTimes[InsertIndex] = SlowFrameTimes[InsertIndex - 1U];
        InsertIndex--;
    }

    SlowFrameTimes[InsertIndex] = FrameTime;

    return StoredCount;
}

/**
 * @brief Update the debugger-visible average and one-percent-low FPS values.
 */
static void System_UpdateFpsStatistics(void)
{
    uint32_t SlowFrameTimes[FPS_SLOW_SAMPLE_CAPACITY] = {0U};
    uint32_t SlowFrameCount;
    uint32_t StoredSlowFrameCount = 0U;
    uint64_t TotalFrameTimeMilliseconds = 0ULL;
    uint64_t TotalSlowFrameTimeMilliseconds = 0ULL;

    if(System_FrameTimeSampleCount == 0U)
    {
        System_DebugAverageFps = 0.0f;
        System_DebugOnePercentLowFps = 0.0f;

        return;
    }

    SlowFrameCount = (System_FrameTimeSampleCount + 99U) / 100U;

    if(SlowFrameCount > FPS_SLOW_SAMPLE_CAPACITY)
    {
        SlowFrameCount = FPS_SLOW_SAMPLE_CAPACITY;
    }

    for(uint32_t SampleIndex = 0U; SampleIndex < System_FrameTimeSampleCount; SampleIndex++)
    {
        const uint32_t FrameTimeMilliseconds = System_FrameTimeSamples[SampleIndex];

        TotalFrameTimeMilliseconds += FrameTimeMilliseconds;
        StoredSlowFrameCount = System_InsertSlowFrameTime(SlowFrameTimes, StoredSlowFrameCount, SlowFrameCount, FrameTimeMilliseconds);
    }

    for(uint32_t SlowFrameIndex = 0U; SlowFrameIndex < StoredSlowFrameCount; SlowFrameIndex++)
    {
        TotalSlowFrameTimeMilliseconds += SlowFrameTimes[SlowFrameIndex];
    }

    if(TotalFrameTimeMilliseconds > 0ULL)
    {
        System_DebugAverageFps = ((float)System_FrameTimeSampleCount * 1000.0f) / (float)TotalFrameTimeMilliseconds;
    }
    else
    {
        System_DebugAverageFps = 0.0f;
    }

    if((StoredSlowFrameCount > 0U) && (TotalSlowFrameTimeMilliseconds > 0ULL))
    {
        System_DebugOnePercentLowFps = ((float)StoredSlowFrameCount * 1000.0f) / (float)TotalSlowFrameTimeMilliseconds;
    }
    else
    {
        System_DebugOnePercentLowFps = 0.0f;
    }
}

int System_Run(void)
{
    const Audio_ConfigTypeDef AudioConfig =
    {
        .SampleRateHz = AUDIO_SAMPLE_RATE_HZ,
        .ChannelCount = AUDIO_CHANNEL_COUNT,
        .FillCallback = AppManager_FillAudioBuffer,
        .CallbackContext = NULL
    };

    uint64_t FrameStartTimeMilliseconds;
    uint64_t PreviousFrameTimeMilliseconds;
    uint64_t PreviousStatisticsTimeMilliseconds;
    uint32_t DeltaTimeMilliseconds;
    bool Running = true;

    System_FrameTimeSampleIndex = 0U;
    System_FrameTimeSampleCount = 0U;
    System_DebugAverageFps = 0.0f;
    System_DebugOnePercentLowFps = 0.0f;
    System_DebugRenderBufferIndex = 0U;
    System_DebugBuffer0RenderCount = 0U;
    System_DebugBuffer1RenderCount = 0U;

    if(!Display_Init())
    {
        return 1;
    }

    if(!Input_Init())
    {
        return 1;
    }

    if(!AppManager_Init())
    {
        return 1;
    }

    if(!Audio_Init(&AudioConfig))
    {
        AppManager_Shutdown();

        return 1;
    }

    if(!Audio_Start())
    {
        Audio_Stop();
        AppManager_Shutdown();

        return 1;
    }

    PreviousFrameTimeMilliseconds = SystemTime_GetMilliseconds();
    PreviousStatisticsTimeMilliseconds = PreviousFrameTimeMilliseconds;

    while(Running)
    {
        Display_WaitForFrame();

        FrameStartTimeMilliseconds = SystemTime_GetMilliseconds();
        DeltaTimeMilliseconds = (uint32_t)(FrameStartTimeMilliseconds - PreviousFrameTimeMilliseconds);
        PreviousFrameTimeMilliseconds = FrameStartTimeMilliseconds;

        System_RecordFrameTime(DeltaTimeMilliseconds);

        if((FrameStartTimeMilliseconds - PreviousStatisticsTimeMilliseconds) >= FPS_UPDATE_INTERVAL_MS)
        {
            System_UpdateFpsStatistics();
            PreviousStatisticsTimeMilliseconds = FrameStartTimeMilliseconds;
        }

        AppManager_Update(DeltaTimeMilliseconds);

        if(System_DebugRenderBufferIndex == 0U)
        {
            System_DebugBuffer0RenderCount++;
        }
        else
        {
            System_DebugBuffer1RenderCount++;
        }

        AppManager_Render();
        System_DebugRenderBufferIndex ^= 1U;

        if(!SystemTasks_Process())
        {
            Running = false;
        }
    }

    Audio_Stop();
    AppManager_Shutdown();

    return 0;
}