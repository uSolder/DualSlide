/**
 * @file system.c
 * @brief DualSlide system main loop.
 *
 * Initializes the platform services and the application manager, then runs
 * the frame loop: global button handling, application update, and render.
 *
 * Holding both buttons is the system gesture: after a moment it returns to
 * the menu, and held on in the menu it powers the device off.
 *
 * The system also owns the brightness and volume settings. Volume follows a
 * square law, so equal steps on a slider sound like equal changes in
 * loudness; the display backend applies its own brightness curve.
 */

#include "system.h"

#include "app_manager.h"
#include "audio.h"
#include "display.h"
#include "input.h"
#include "mixer.h"
#include "sound.h"
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

#define SYSTEM_SETTINGS_STORAGE_KEY                (0x53455453UL) /* "SETS" */
#define SYSTEM_SETTINGS_SAVE_DATA_VERSION          (1U)
#define SYSTEM_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT (100U)
#define SYSTEM_SETTINGS_DEFAULT_VOLUME_PERCENT     (100U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool Held;
    uint64_t NextActionTimeMilliseconds;
} System_ButtonChordStateTypeDef;

typedef struct
{
    uint32_t Version;
    uint8_t BrightnessPercent;
    uint8_t VolumePercent;
    uint8_t Reserved[2];
} System_SettingsSaveDataTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static System_ButtonChordStateTypeDef System_ButtonChordState;

static uint8_t System_BrightnessPercent = SYSTEM_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
static uint8_t System_VolumePercent = SYSTEM_SETTINGS_DEFAULT_VOLUME_PERCENT;
static uint8_t System_SavedBrightnessPercent;
static uint8_t System_SavedVolumePercent;
static bool System_SettingsStored;

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

static uint8_t System_ClampBrightness(uint8_t Percent)
{
    if(Percent < SYSTEM_BRIGHTNESS_MINIMUM_PERCENT)
    {
        return SYSTEM_BRIGHTNESS_MINIMUM_PERCENT;
    }

    return (Percent > SYSTEM_PERCENT_MAXIMUM) ? SYSTEM_PERCENT_MAXIMUM : Percent;
}

static void System_ApplyVolume(void)
{
    const uint32_t Percent = System_VolumePercent;

    (void)Mixer_SetMasterVolume((uint16_t)(((Percent * Percent * MIXER_VOLUME_MAX) + 5000U) / 10000U));
}

/**
 * @brief Load the saved settings, or the defaults, and apply them.
 *
 * Called once storage, the display, and the mixer are initialised.
 */
static void System_InitSettings(void)
{
    System_SettingsSaveDataTypeDef SaveData;

    System_BrightnessPercent = SYSTEM_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    System_VolumePercent = SYSTEM_SETTINGS_DEFAULT_VOLUME_PERCENT;
    System_SettingsStored = false;

    if((Storage_Read(SYSTEM_SETTINGS_STORAGE_KEY, &SaveData, sizeof(SaveData), NULL) == STORAGE_RESULT_OK) &&
       (SaveData.Version == SYSTEM_SETTINGS_SAVE_DATA_VERSION))
    {
        System_BrightnessPercent = System_ClampBrightness(SaveData.BrightnessPercent);
        System_VolumePercent = (SaveData.VolumePercent > SYSTEM_PERCENT_MAXIMUM) ? SYSTEM_PERCENT_MAXIMUM : SaveData.VolumePercent;
        System_SavedBrightnessPercent = System_BrightnessPercent;
        System_SavedVolumePercent = System_VolumePercent;
        System_SettingsStored = true;
    }

    (void)Display_SetBrightness(System_BrightnessPercent);
    System_ApplyVolume();
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
    Sound_Init();

    /* Saved brightness and volume, once the display and mixer are ready. */
    System_InitSettings();

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

uint8_t System_GetBrightness(void)
{
    return System_BrightnessPercent;
}

void System_SetBrightness(uint8_t Percent)
{
    System_BrightnessPercent = System_ClampBrightness(Percent);
    (void)Display_SetBrightness(System_BrightnessPercent);
}

uint8_t System_GetVolume(void)
{
    return System_VolumePercent;
}

void System_SetVolume(uint8_t Percent)
{
    System_VolumePercent = (Percent > SYSTEM_PERCENT_MAXIMUM) ? SYSTEM_PERCENT_MAXIMUM : Percent;
    System_ApplyVolume();
}

bool System_SaveSettings(void)
{
    const System_SettingsSaveDataTypeDef SaveData =
    {
        .Version = SYSTEM_SETTINGS_SAVE_DATA_VERSION,
        .BrightnessPercent = System_BrightnessPercent,
        .VolumePercent = System_VolumePercent,
        .Reserved = { 0U, 0U }
    };

    /* Flash wears with every write, so unchanged settings are not rewritten. */
    if(System_SettingsStored &&
       (System_SavedBrightnessPercent == System_BrightnessPercent) &&
       (System_SavedVolumePercent == System_VolumePercent))
    {
        return true;
    }

    if(Storage_Write(SYSTEM_SETTINGS_STORAGE_KEY, &SaveData, sizeof(SaveData)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    System_SavedBrightnessPercent = System_BrightnessPercent;
    System_SavedVolumePercent = System_VolumePercent;
    System_SettingsStored = true;

    return true;
}

bool System_EraseSavedData(void)
{
    if(Storage_EraseAll() != STORAGE_RESULT_OK)
    {
        return false;
    }

    System_SettingsStored = false;

    return System_SaveSettings();
}
