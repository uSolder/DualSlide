/**
 * @file delay.h
 * @brief Busy-wait delays, timed by the Cortex-M7 DWT cycle counter.
 */

#ifndef CHIP_DELAY_H
#define CHIP_DELAY_H

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

#endif /* CHIP_DELAY_H */
