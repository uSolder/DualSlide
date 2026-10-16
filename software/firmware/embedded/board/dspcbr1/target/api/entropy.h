/**
 * @file entropy.h
 * @brief Hardware-independent true random number source interface.
 *
 * The selected target reads unpredictable values from a hardware random
 * number generator. Values are suited to seeding a pseudo-random generator.
 */

#ifndef TARGET_API_ENTROPY_H
#define TARGET_API_ENTROPY_H

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

#endif /* TARGET_API_ENTROPY_H */
