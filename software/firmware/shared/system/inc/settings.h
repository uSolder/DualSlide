/**
 * @file settings.h
 * @brief System settings: screen brightness and global volume.
 *
 * Settings are kept in persistent storage and applied at startup. Changes
 * take effect immediately; Settings_Save() writes them to storage, so callers
 * can adjust freely (while a slider moves, say) and save once at the end.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lowest screen brightness, so the screen never goes fully dark. */
#define SETTINGS_BRIGHTNESS_MINIMUM_PERCENT (20U)

/** Highest brightness and volume. */
#define SETTINGS_PERCENT_MAXIMUM            (100U)

/**
 * @brief Load the saved settings, or the defaults, and apply them.
 *
 * Call after storage, the display, and the mixer are initialised.
 */
void Settings_Init(void);

/**
 * @brief Return the screen brightness, SETTINGS_BRIGHTNESS_MINIMUM_PERCENT to 100.
 */
uint8_t Settings_GetBrightness(void);

/**
 * @brief Set and apply the screen brightness; out-of-range values are clamped.
 */
void Settings_SetBrightness(uint8_t Percent);

/**
 * @brief Return the global volume, 0 to 100.
 */
uint8_t Settings_GetVolume(void);

/**
 * @brief Set and apply the global volume; values above 100 are clamped.
 */
void Settings_SetVolume(uint8_t Percent);

/**
 * @brief Write the settings to persistent storage if they changed.
 *
 * @return true if the settings are saved.
 */
bool Settings_Save(void);

/**
 * @brief Erase every application's saved data (high scores and records),
 *        keeping the settings.
 *
 * @return true on success.
 */
bool Settings_EraseSavedData(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
