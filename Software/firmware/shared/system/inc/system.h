/**
 * @file system.h
 * @brief Dualslide system file.
 *
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Dualslide system, does not return.
 */
int System_Run(void);

/**
 * @brief CPU blocking delay.
 */
void SystemTime_DelayMilliseconds(uint32_t DelayMilliseconds);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */