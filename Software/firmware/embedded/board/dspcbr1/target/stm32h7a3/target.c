/**
 * @file target.c
 * @brief STM32H7A3 target initialization implementation.
 */

#include "target.h"

#include "rcc.h"
#include "stm32h7xx.h"

void Target_Init(void)
{
	SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));

	__DSB();
	__ISB();

	SCB_EnableDCache();

    RCC_Init();
    SystemCoreClockUpdate();
}