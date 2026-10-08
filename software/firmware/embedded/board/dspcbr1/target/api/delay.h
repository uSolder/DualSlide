/**
 * @file delay.h
 * @brief Hardware-independent blocking delay interface.
 *
 * The selected target provides timing delays using an appropriate hardware
 * timer, system timer, or processor cycle counter.
 */

#ifndef TARGET_API_DELAY_H
#define TARGET_API_DELAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Delay functions                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Block execution for at least the requested number of microseconds.
 *
 * @param DelayMicroseconds Delay duration in microseconds.
 */
void Delay_Microseconds(uint32_t DelayMicroseconds);

/**
 * @brief Block execution for at least the requested number of milliseconds.
 *
 * @param DelayMilliseconds Delay duration in milliseconds.
 */
void Delay_Milliseconds(uint32_t DelayMilliseconds);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_DELAY_H */
