/**
 * @file entropy.h
 * @brief STM32H7A3 hardware random number generator (RNG).
 *
 * Reads unpredictable values from the RNG peripheral. Values are suited to seeding a pseudo-random generator.
 */

#ifndef CHIP_ENTROPY_H
#define CHIP_ENTROPY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read one random 32-bit value, starting the generator if needed.
 *
 * Blocks for at most a few microseconds while the generator produces it.
 *
 * @param Value Receives the value.
 *
 * @return true on success; false if the generator reports a fault or times out.
 */
bool Entropy_Read(uint32_t *Value);

#ifdef __cplusplus
}
#endif

#endif /* CHIP_ENTROPY_H */
