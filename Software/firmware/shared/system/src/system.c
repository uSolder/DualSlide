
#include "system.h"
#include "app_manager.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "system_time.h"
#include "system_tasks.h"

#define AUDIO_SAMPLE_RATE_HZ    (48000U)
#define AUDIO_CHANNEL_COUNT     (1U)
#define FRAME_INTERVAL_MS       (16U)

int System_Run(void)
{
    const Audio_ConfigTypeDef AudioConfig =
    {
        .SampleRateHz = AUDIO_SAMPLE_RATE_HZ,
        .ChannelCount = AUDIO_CHANNEL_COUNT,
        .FillCallback = AppManager_FillAudioBuffer,
        .CallbackContext = NULL
    };

    uint64_t PreviousFrameTimeMilliseconds;
    bool Running = true;

    if(!Display_Init())     return 1;

    if(!Input_Init())       return 1;
    
    if(!AppManager_Init())  return 1;

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

    while(Running)
    {
        uint64_t FrameStartTimeMilliseconds;
        uint64_t FrameElapsedMilliseconds;
        uint32_t DeltaTimeMilliseconds;

        FrameStartTimeMilliseconds = SystemTime_GetMilliseconds();
        DeltaTimeMilliseconds = (uint32_t)(FrameStartTimeMilliseconds - PreviousFrameTimeMilliseconds);
        PreviousFrameTimeMilliseconds = FrameStartTimeMilliseconds;

        if(!Running)
        {
            break;
        }

        AppManager_Update(DeltaTimeMilliseconds);
        AppManager_Render();

        FrameElapsedMilliseconds = SystemTime_GetMilliseconds() - FrameStartTimeMilliseconds;

        if(FrameElapsedMilliseconds < FRAME_INTERVAL_MS)
        {
            SystemTime_DelayMilliseconds((uint32_t)(FRAME_INTERVAL_MS - FrameElapsedMilliseconds));
        }

        if(!SystemTasks_Process())
        {
            Running = false;
            break;
        }
    }

    Audio_Stop();
    AppManager_Shutdown();
}