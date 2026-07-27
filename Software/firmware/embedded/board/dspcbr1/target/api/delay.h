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
 * @param delay_us Delay duration in microseconds.
 */
void Delay_us(uint32_t delay_us);

/**
 * @brief Block execution for at least the requested number of milliseconds.
 *
 * @param delay_ms Delay duration in milliseconds.
 */
void Delay_ms(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_DELAY_H */