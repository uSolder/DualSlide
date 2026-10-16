/**
 * @file game_memory.h
 * @brief Platform-neutral contract for the memory loaded games run in.
 *
 * One game is loaded at a time, into a single block of RAM that can hold
 * both code and data.
 */

#ifndef GAME_MEMORY_H
#define GAME_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Alignment of the game memory block, in bytes. */
#define GAME_MEMORY_ALIGNMENT               (32U)

/** @brief Return the start of the game memory block. */
void *GameMemory_GetAddress(void);

/** @brief Return the size of the game memory block, in bytes. */
uint32_t GameMemory_GetSize(void);

/**
 * @brief Make code just written into game memory safe to run.
 *
 * Call after copying and relocating a game and before calling into it.
 *
 * @param Address Start of the written range.
 * @param Size    Bytes written.
 */
void GameMemory_PrepareCode(const void *Address, uint32_t Size);

#ifdef __cplusplus
}
#endif

#endif /* GAME_MEMORY_H */
