/**
 * @file system.c
 * @brief DualSlide system main loop.
 *
 * Initializes the platform services and the application manager, then runs
 * the frame loop: global button handling, application update, and render.
 *
 * Holding both buttons is the system gesture: after a moment it returns to
 * the menu, and held on in the menu it powers the device off.
 */

#include "system.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "mixer.h"
#include "settings.h"
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

#ifdef DS_USB_AUDIO_TEST
/*
 * USB audio test: the board also enumerates as a USB speaker, and the host's
 * PCM plays on the system mixer channel. Everything else runs normally.
 */
_Static_assert(USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ == MIXER_SAMPLE_RATE_HZ, "USB audio output rate must match the mixer rate");
_Static_assert(USB_AUDIO_OUTPUT_CHANNEL_COUNT == 1U, "USB audio output must be mono to feed a mixer channel");
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
    bool Held;
    uint64_t NextActionTimeMilliseconds;
} System_ButtonChordStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static System_ButtonChordStateTypeDef System_ButtonChordState;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool System_IsButtonPressed(Input_NumberTypeDef ButtonNumber)
{
    int32_t Value;

    return Input_GetValue(ButtonNumber, &Value) && (Value != 0);
}

/**
 * @brief Update the two-button gesture.
 *
 * Once both buttons have been held together for the hold time, an action is
 * reported, then another after each further hold time while they stay held.
 * Releasing either button, or a failed input read, starts the count again.
 *
 * @param State            Gesture state to update.
 * @param TimeMilliseconds Current system time in milliseconds.
 *
 * @return true when the gesture has just reached the next hold time.
 */
static bool System_UpdateButtonChord(System_ButtonChordStateTypeDef *State, uint64_t TimeMilliseconds)
{
    if(State == NULL)
    {
        return false;
    }

    if(!System_IsButtonPressed(SYSTEM_INPUT_PRIMARY_BUTTON) || !System_IsButtonPressed(SYSTEM_INPUT_SECONDARY_BUTTON))
    {
        State->Held = false;

        return false;
    }

    if(!State->Held)
    {
        State->Held = true;
        State->NextActionTimeMilliseconds = TimeMilliseconds + SYSTEM_BUTTON_HOLD_TIME_MILLISECONDS;

        return false;
    }

    if(TimeMilliseconds >= State->NextActionTimeMilliseconds)
    {
        State->NextActionTimeMilliseconds = TimeMilliseconds + SYSTEM_BUTTON_HOLD_TIME_MILLISECONDS;

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
        .SampleRateHz = MIXER_SAMPLE_RATE_HZ,
        .ChannelCount = 1U,
        .FillCallback = Mixer_FillAudioBuffer,
        .CallbackContext = NULL
    };

    uint64_t FrameStartTimeMilliseconds;
    uint64_t PreviousFrameTimeMilliseconds;
    uint32_t DeltaTimeMilliseconds;
    bool Running = true;

    System_ButtonChordState = (System_ButtonChordStateTypeDef){ 0 };

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

    /* Before the application manager, so applications can play sounds from Init. */
    Mixer_Init();

    /* Saved brightness and volume, once the display and mixer are ready. */
    Settings_Init();

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
    if(!USBAudio_Init() || !Mixer_PlayGenerator(MIXER_SYSTEM_CHANNEL, USBAudio_FillAudioBuffer, NULL))
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

        /* Both buttons held: back to the menu, or off when already there. */
        if(System_UpdateButtonChord(&System_ButtonChordState, FrameStartTimeMilliseconds))
        {
            if(AppManager_IsLauncherActive())
            {
                SystemTasks_PowerOff();
                Running = false;
            }
            else
            {
                AppManager_OpenLauncher();
            }
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
