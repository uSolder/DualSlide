/**
 * @file chip.h
 * @brief STM32H7A3 start-up: clocks, caches and memory protection, and the
 *        chip's unique ID.
 */

#ifndef CHIP_H
#define CHIP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set up the processor: FPU, memory protection, caches and clocks.
 *
 * Call first, before the board is set up.
 */
void Chip_Init(void);

/**
 * @brief Reset the processor; the board has already cut its own power.
 */
void Chip_PowerOff(void);

/** Words in the chip's unique ID. */
#define CHIP_UNIQUE_ID_WORDS                (3U)

/**
 * @brief Read the 96-bit ID that ST programs into every chip at the factory.
 *
 * @param Id Destination for CHIP_UNIQUE_ID_WORDS words.
 */
void Chip_GetUniqueId(uint32_t Id[CHIP_UNIQUE_ID_WORDS]);

/**
 * @brief A 32-bit number for this DualSlide alone, made from the unique ID.
 *
 * The same chip always gives the same number, and it is never 0.
 */
uint32_t Chip_GetSerialNumber(void);

#ifdef __cplusplus
}
#endif

#endif /* CHIP_H */
