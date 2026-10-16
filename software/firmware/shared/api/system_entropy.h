/**
 * @file system_entropy.h
 * @brief Platform-neutral source of seeds for pseudo-random generators.
 */

#ifndef SYSTEM_ENTROPY_H
#define SYSTEM_ENTROPY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return a value that is unpredictable and different on every call.
 *
 * Uses hardware randomness where the platform has it. Suited to seeding,
 * not to producing every random number: it may be slow.
 */
uint32_t SystemEntropy_GetSeed(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_ENTROPY_H */
