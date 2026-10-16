/**
 * @file system.c
 * @brief DualSlide system main loop.
 *
 * Initializes the system services and the application manager, then runs
 * the frame loop: global button handling, application update, and render.
 *
 * Holding both buttons is the system gesture: after a moment it returns to
 * the menu, and held on in the menu it powers the device off.
 *
 * The system also owns the brightness and volume settings. Volume follows a
 * square law, so equal steps on a slider sound like equal changes in
 * loudness; the display applies its own brightness curve.
 */

#include "system.h"

#include "app_manager.h"
#include "audio.h"
#include "board.h"
#include "controls.h"
#include "display.h"
#include "mixer.h"
#include "power.h"
#include "random.h"
#include "save.h"
#include "sound.h"
#include "storage.h"
#include "system_time.h"
#include "usb_drive.h"
#include "usbd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define SYSTEM_BUTTON_HOLD_TIME_MILLISECONDS (1500ULL)

#define SYSTEM_SETTINGS_SAVE_NAME                  "SETS"
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

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Update the two-button gesture.
 *
 * Once both buttons have been held together for the hold time, an action is
 * reported, then another after each further hold time while they stay held.
 * Releasing either button starts the count again.
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

    if(!Board_IsPrimaryButtonDown() || !Board_IsSecondaryButtonDown())
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

    if(Save_Load(SYSTEM_SETTINGS_SAVE_NAME, &SaveData, sizeof(SaveData)) && (SaveData.Version == SYSTEM_SETTINGS_SAVE_DATA_VERSION))
    {
        System_BrightnessPercent = System_ClampBrightness(SaveData.BrightnessPercent);
        System_VolumePercent = (SaveData.VolumePercent > SYSTEM_PERCENT_MAXIMUM) ? SYSTEM_PERCENT_MAXIMUM : SaveData.VolumePercent;
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

    Random_Init();

    if(Storage_Init() != STORAGE_RESULT_OK)
    {
        return 1;
    }

    if(!Display_Init())
    {
        return 1;
    }

    Controls_Init();

    /* Before the application manager, so applications can play sounds from Init. */
    Mixer_Init();
    Sound_Init();

    /* Saved brightness and volume, once the display and mixer are ready. */
    System_InitSettings();

    /*
     * The game disk, shared over USB as the DUALSLIDE drive, before the app
     * manager lists the games on it. Without it the device still runs, with
     * no games, so a disk fault never stops the system starting.
     */
    (void)USBDrive_Init();

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

    while(Running)
    {
        Display_WaitForFrame();

        FrameStartTimeMilliseconds = SystemTime_GetMilliseconds();
        DeltaTimeMilliseconds = (uint32_t)(FrameStartTimeMilliseconds - PreviousFrameTimeMilliseconds);
        PreviousFrameTimeMilliseconds = FrameStartTimeMilliseconds;

        if(Power_IsBatteryDepleted())
        {
            Running = false;
        }

        Controls_Update(DeltaTimeMilliseconds);

        /* Both buttons held: back to the menu, or off when already there. */
        if(System_UpdateButtonChord(&System_ButtonChordState, FrameStartTimeMilliseconds))
        {
            if(AppManager_IsLauncherActive())
            {
                Running = false;
            }
            else
            {
                AppManager_OpenLauncher();
            }
        }

        AppManager_Update(DeltaTimeMilliseconds);
        AppManager_Render();

        USBDrive_Process();
    }

    /* Whatever the host wrote is made permanent before the power goes. */
    USBDrive_Deinit();

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

    /* Only written when changed, as flash wears with every write. */
    return Save_Store(SYSTEM_SETTINGS_SAVE_NAME, &SaveData, sizeof(SaveData));
}

bool System_EraseSavedData(void)
{
    if(Storage_EraseAll() != STORAGE_RESULT_OK)
    {
        return false;
    }

    return System_SaveSettings();
}

uint16_t System_GetBatteryMillivolts(void)
{
    return Power_GetBatteryVoltageMillivolts();
}

bool System_IsExternalPowerPresent(void)
{
    return USBD_IsVbusPresent();
}
