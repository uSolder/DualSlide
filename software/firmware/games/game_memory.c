/**
 * @file game_memory.c
 * @brief Game memory in AXI SRAM1 (.game_memory in the linker script).
 *
 * The region is ordinary cacheable, executable RAM. Code written through the
 * data cache must reach memory, and the instruction cache must forget what it
 * held, before the CPU runs it.
 */

#include "game_memory.h"

#include "stm32h7xx.h"

#include <stdint.h>

extern uint8_t __game_memory_start__;
extern uint8_t __game_memory_end__;

void *GameMemory_GetAddress(void)
{
    return &__game_memory_start__;
}

uint32_t GameMemory_GetSize(void)
{
    return (uint32_t)(&__game_memory_end__ - &__game_memory_start__);
}

void GameMemory_PrepareCode(const void *Address, uint32_t Size)
{
    uintptr_t Start = (uintptr_t)Address & ~(uintptr_t)(__SCB_DCACHE_LINE_SIZE - 1U);
    uintptr_t End = (uintptr_t)Address + Size;

    SCB_CleanDCache_by_Addr((uint32_t *)Start, (int32_t)(End - Start));
    SCB_InvalidateICache();
}
