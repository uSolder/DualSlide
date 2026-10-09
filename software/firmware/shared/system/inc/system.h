/**
 * @file system.h
 * @brief Dualslide system file.
 *
 * Also the system settings: screen brightness and global volume. They are
 * kept in persistent storage and applied at startup. Changes take effect
 * immediately; System_SaveSettings() writes them to storage, so callers can
 * adjust freely (while a slider moves, say) and save once at the end.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lowest screen brightness, so the screen never goes fully dark. */
#define SYSTEM_BRIGHTNESS_MINIMUM_PERCENT (20U)

/** Highest brightness and volume. */
#define SYSTEM_PERCENT_MAXIMUM            (100U)

/**
 * @brief Start the Dualslide system, does not return.
 */
int System_Run(void);

/**
 * @brief Return the screen brightness, SYSTEM_BRIGHTNESS_MINIMUM_PERCENT to 100.
 */
uint8_t System_GetBrightness(void);

/**
 * @brief Set and apply the screen brightness; out-of-range values are clamped.
 */
void System_SetBrightness(uint8_t Percent);

/**
 * @brief Return the global volume, 0 to 100.
 */
uint8_t System_GetVolume(void);

/**
 * @brief Set and apply the global volume; values above 100 are clamped.
 */
void System_SetVolume(uint8_t Percent);

/**
 * @brief Write the brightness and volume to persistent storage if they changed.
 *
 * @return true if the settings are saved.
 */
bool System_SaveSettings(void);

/**
 * @brief Erase every application's saved data (high scores and records),
 *        keeping the settings.
 *
 * @return true on success.
 */
bool System_EraseSavedData(void);

/**
 * @brief CPU blocking delay.
 */
void SystemTime_DelayMilliseconds(uint32_t DelayMilliseconds);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */
