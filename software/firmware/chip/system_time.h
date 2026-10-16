/**
 * @file system_time.h
 * @brief Milliseconds since start-up, from the Cortex-M7 SysTick timer.
 */

#ifndef SYSTEM_TIME_H
#define SYSTEM_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the millisecond counter. Call once the core clock is set.
 *
 * @return true on success.
 */
bool SystemTime_Init(void);

/**
 * @brief Gets the monotonic time elapsed since system startup.
 *
 * The returned value wraps naturally after approximately 49.7 days.
 * Elapsed-time calculations remain valid when unsigned subtraction is used.
 *
 * @return Elapsed system time in milliseconds.
 */
uint32_t SystemTime_GetMilliseconds(void);

/**
 * @brief Wait, doing nothing else, for at least Milliseconds.
 */
void SystemTime_DelayMilliseconds(uint32_t Milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_TIME_H */
