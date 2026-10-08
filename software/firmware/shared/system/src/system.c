/**
 * @file system.c
 * @brief DualSlide system main loop.
 *
 * Initializes the platform services and the application manager, then runs
 * the frame loop: global button handling, application update, and render.
 */

#include "system.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "storage.h"
#include "system_tasks.h"
#include "system_time.h"

#ifdef DS_USB_AUDIO_TEST
#include "usb_audio.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* System audio format, shared by application audio and the USB audio test. */
#define SYSTEM_AUDIO_SAMPLE_RATE_HZ          (24000U)
#define SYSTEM_AUDIO_CHANNEL_COUNT           (1U)

#ifdef DS_USB_AUDIO_TEST
/*
 * USB audio test: the board also enumerates as a USB speaker, and the host's
 * PCM replaces application audio. Everything else runs normally.
 */
#define SYSTEM_AUDIO_FILL_CALLBACK           (USBAudio_FillAudioBuffer)

_Static_assert(USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ == SYSTEM_AUDIO_SAMPLE_RATE_HZ, "USB audio output rate must match the system audio rate");
_Static_assert(USB_AUDIO_OUTPUT_CHANNEL_COUNT == SYSTEM_AUDIO_CHANNEL_COUNT, "USB audio output channels must match the system audio format");
#else
#define SYSTEM_AUDIO_FILL_CALLBACK           (AppManager_FillAudioBuffer)
#endif

#define SYSTEM_INPUT_PRIMARY_BUTTON          ((Input_NumberTypeDef)3U)
#define SYSTEM_INPUT_SECONDARY_BUTTON        ((Input_NumberTypeDef)4U)
#define SYSTEM_INPUT_BATTERY_DEPLETED        ((Input_NumberTypeDef)7U)
#define SYSTEM_BUTTON_HOLD_TIME_MILLISECONDS (1500ULL)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool Pressed;
    bool HoldDetected;
    uint64_t PressStartTimeMilliseconds;
} System_ButtonHoldStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static System_ButtonHoldStateTypeDef System_PrimaryButtonHoldState;
static System_ButtonHoldStateTypeDef System_SecondaryButtonHoldState;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Update one button hold state.
 *
 * A hold is reported exactly once for each uninterrupted button press. A
 * failed input read clears the state so that stale input cannot cause an
 * action when the input backend later recovers.
 *
 * @param State            Button hold state to update.
 * @param ButtonNumber     Input number assigned to the button.
 * @param TimeMilliseconds Current system time in milliseconds.
 *
 * @return true when the button has just reached the hold duration.
 */
static bool System_UpdateButtonHold(System_ButtonHoldStateTypeDef *State, Input_NumberTypeDef ButtonNumber, uint64_t TimeMilliseconds)
{
    int32_t Value;

    if(State == NULL)
    {
        return false;
    }

    if(!Input_GetValue(ButtonNumber, &Value) || (Value == 0))
    {
        State->Pressed = false;
        State->HoldDetected = false;
        State->PressStartTimeMilliseconds = 0ULL;

        return false;
    }

    if(!State->Pressed)
    {
        State->Pressed = true;
        State->HoldDetected = false;
        State->PressStartTimeMilliseconds = TimeMilliseconds;

        return false;
    }

    if(!State->HoldDetected &&
       ((TimeMilliseconds - State->PressStartTimeMilliseconds) >= SYSTEM_BUTTON_HOLD_TIME_MILLISECONDS))
    {
        State->HoldDetected = true;

        return true;
    }

    return false;
}

/**
 * @brief Return whether the input backend reports a depleted battery.
 *
 * A failed input read is treated as non-depleted so an unavailable optional
 * battery input cannot command an unexpected shutdown.
 */
static bool System_IsBatteryDepleted(void)
{
    int32_t BatteryDepleted;

    if(!Input_GetValue(SYSTEM_INPUT_BATTERY_DEPLETED, &BatteryDepleted))
    {
        return false;
    }

    return BatteryDepleted != 0;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

int System_Run(void)
{
    const Audio_ConfigTypeDef AudioConfig =
    {
        .SampleRateHz = SYSTEM_AUDIO_SAMPLE_RATE_HZ,
        .ChannelCount = SYSTEM_AUDIO_CHANNEL_COUNT,
        .FillCallback = SYSTEM_AUDIO_FILL_CALLBACK,
        .CallbackContext = NULL
    };

    uint64_t FrameStartTimeMilliseconds;
    uint64_t PreviousFrameTimeMilliseconds;
    uint32_t DeltaTimeMilliseconds;
    bool Running = true;

    System_PrimaryButtonHoldState = (System_ButtonHoldStateTypeDef){ 0 };
    System_SecondaryButtonHoldState = (System_ButtonHoldStateTypeDef){ 0 };

    if(Storage_Init() != STORAGE_RESULT_OK)
    {
        return 1;
    }

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

#ifdef DS_USB_AUDIO_TEST
    if(!USBAudio_Init())
    {
        Audio_Stop();
        AppManager_Shutdown();

        return 1;
    }
#endif

    PreviousFrameTimeMilliseconds = SystemTime_GetMilliseconds();

    while(Running)
    {
        Display_WaitForFrame();

        FrameStartTimeMilliseconds = SystemTime_GetMilliseconds();
        DeltaTimeMilliseconds = (uint32_t)(FrameStartTimeMilliseconds - PreviousFrameTimeMilliseconds);
        PreviousFrameTimeMilliseconds = FrameStartTimeMilliseconds;

        if(System_IsBatteryDepleted())
        {
            SystemTasks_PowerOff();
            Running = false;
        }

        if(System_UpdateButtonHold(&System_PrimaryButtonHoldState, SYSTEM_INPUT_PRIMARY_BUTTON, FrameStartTimeMilliseconds))
        {
            SystemTasks_PowerOff();
            Running = false;
        }

        if(System_UpdateButtonHold(&System_SecondaryButtonHoldState, SYSTEM_INPUT_SECONDARY_BUTTON, FrameStartTimeMilliseconds))
        {
            AppManager_OpenLauncher();
        }

        AppManager_Update(DeltaTimeMilliseconds);
        AppManager_Render();

        if(!SystemTasks_Process())
        {
            Running = false;
        }
    }

#ifdef DS_USB_AUDIO_TEST
    USBAudio_Deinit();
#endif

    Audio_Stop();
    AppManager_Shutdown();

    return 0;
}
