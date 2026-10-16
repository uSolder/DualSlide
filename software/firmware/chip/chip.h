/**
 * @file chip.h
 * @brief STM32H7A3 start-up: clocks, caches and memory protection.
 */

#ifndef CHIP_H
#define CHIP_H

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

#ifdef __cplusplus
}
#endif

#endif /* CHIP_H */
