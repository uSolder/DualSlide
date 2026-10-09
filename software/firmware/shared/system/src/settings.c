/**
 * @file settings.c
 * @brief System settings: screen brightness and global volume.
 *
 * Volume follows a square law, so equal steps on the slider sound like equal
 * changes in loudness; the display backend applies its own brightness curve.
 */

#include "settings.h"

#include "display.h"
#include "mixer.h"
#include "storage.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define SETTINGS_STORAGE_KEY                (0x53455453UL) /* "SETS" */
#define SETTINGS_SAVE_DATA_VERSION          (1U)
#define SETTINGS_DEFAULT_BRIGHTNESS_PERCENT (100U)
#define SETTINGS_DEFAULT_VOLUME_PERCENT     (100U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    uint32_t Version;
    uint8_t BrightnessPercent;
    uint8_t VolumePercent;
    uint8_t Reserved[2];
} Settings_SaveDataTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint8_t Settings_BrightnessPercent = SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
static uint8_t Settings_VolumePercent = SETTINGS_DEFAULT_VOLUME_PERCENT;
static uint8_t Settings_SavedBrightnessPercent;
static uint8_t Settings_SavedVolumePercent;
static bool Settings_Stored;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint8_t Settings_ClampBrightness(uint8_t Percent)
{
    if(Percent < SETTINGS_BRIGHTNESS_MINIMUM_PERCENT)
    {
        return SETTINGS_BRIGHTNESS_MINIMUM_PERCENT;
    }

    return (Percent > SETTINGS_PERCENT_MAXIMUM) ? SETTINGS_PERCENT_MAXIMUM : Percent;
}

static void Settings_ApplyVolume(void)
{
    const uint32_t Percent = Settings_VolumePercent;

    (void)Mixer_SetMasterVolume((uint16_t)(((Percent * Percent * MIXER_VOLUME_MAX) + 5000U) / 10000U));
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Settings_Init(void)
{
    Settings_SaveDataTypeDef SaveData;

    Settings_BrightnessPercent = SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    Settings_VolumePercent = SETTINGS_DEFAULT_VOLUME_PERCENT;
    Settings_Stored = false;

    if((Storage_Read(SETTINGS_STORAGE_KEY, &SaveData, sizeof(SaveData), NULL) == STORAGE_RESULT_OK) &&
       (SaveData.Version == SETTINGS_SAVE_DATA_VERSION))
    {
        Settings_BrightnessPercent = Settings_ClampBrightness(SaveData.BrightnessPercent);
        Settings_VolumePercent = (SaveData.VolumePercent > SETTINGS_PERCENT_MAXIMUM) ? SETTINGS_PERCENT_MAXIMUM : SaveData.VolumePercent;
        Settings_SavedBrightnessPercent = Settings_BrightnessPercent;
        Settings_SavedVolumePercent = Settings_VolumePercent;
        Settings_Stored = true;
    }

    (void)Display_SetBrightness(Settings_BrightnessPercent);
    Settings_ApplyVolume();
}

uint8_t Settings_GetBrightness(void)
{
    return Settings_BrightnessPercent;
}

void Settings_SetBrightness(uint8_t Percent)
{
    Settings_BrightnessPercent = Settings_ClampBrightness(Percent);
    (void)Display_SetBrightness(Settings_BrightnessPercent);
}

uint8_t Settings_GetVolume(void)
{
    return Settings_VolumePercent;
}

void Settings_SetVolume(uint8_t Percent)
{
    Settings_VolumePercent = (Percent > SETTINGS_PERCENT_MAXIMUM) ? SETTINGS_PERCENT_MAXIMUM : Percent;
    Settings_ApplyVolume();
}

bool Settings_Save(void)
{
    const Settings_SaveDataTypeDef SaveData =
    {
        .Version = SETTINGS_SAVE_DATA_VERSION,
        .BrightnessPercent = Settings_BrightnessPercent,
        .VolumePercent = Settings_VolumePercent,
        .Reserved = { 0U, 0U }
    };

    /* Flash wears with every write, so unchanged settings are not rewritten. */
    if(Settings_Stored &&
       (Settings_SavedBrightnessPercent == Settings_BrightnessPercent) &&
       (Settings_SavedVolumePercent == Settings_VolumePercent))
    {
        return true;
    }

    if(Storage_Write(SETTINGS_STORAGE_KEY, &SaveData, sizeof(SaveData)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    Settings_SavedBrightnessPercent = Settings_BrightnessPercent;
    Settings_SavedVolumePercent = Settings_VolumePercent;
    Settings_Stored = true;

    return true;
}

bool Settings_EraseSavedData(void)
{
    if(Storage_EraseAll() != STORAGE_RESULT_OK)
    {
        return false;
    }

    Settings_Stored = false;

    return Settings_Save();
}
