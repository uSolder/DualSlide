/**
 * @file random.h
 * @brief Random numbers for games.
 *
 * QUICK START
 *
 *   int32_t Roll = Random_Between(1, 6);       // 1 to 6, each equally likely
 *   uint32_t Bits = Random_Next();             // any 32-bit value
 *
 *   The sequence is seeded from hardware randomness when the device starts,
 *   so it differs every time. It is fast enough to call many times a frame,
 *   but is not for cryptography.
 */

#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return a random 32-bit value.
 */
uint32_t Random_Next(void);

/**
 * @brief Return a random whole number from Minimum to Maximum, both included.
 *
 * Every value in the range is equally likely. The bounds may be given in
 * either order.
 */
int32_t Random_Between(int32_t Minimum, int32_t Maximum);

/**
 * @brief Seed the generator. Called once by the system at start-up.
 */
void Random_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* RANDOM_H */
