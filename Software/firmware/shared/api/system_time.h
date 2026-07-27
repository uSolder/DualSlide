/**
 * @file system_time.h
 * @brief Platform-independent monotonic timing contract.
 */

#ifndef SYSTEM_TIME_H
#define SYSTEM_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gets the monotonic time elapsed since system startup.
 *
 * The returned value wraps naturally after approximately 49.7 days.
 * Elapsed-time calculations remain valid when unsigned subtraction is used.
 *
 * @return Elapsed system time in milliseconds.
 */
uint32_t SystemTime_GetMilliseconds(void);

void SystemTime_DelayMilliseconds(uint32_t Milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_TIME_H */