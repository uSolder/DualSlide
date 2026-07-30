/**
 * @file target.h
 * @brief Hardware-target initialization interface.
 */

#ifndef TARGET_H
#define TARGET_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the selected hardware target.
 *
 * Configures the MCU clock system, processor-level facilities, and other
 * target-specific resources required before board initialization.
 */
void Target_Init(void);

void Target_PowerOff();

#ifdef __cplusplus
}
#endif

#endif /* TARGET_H */