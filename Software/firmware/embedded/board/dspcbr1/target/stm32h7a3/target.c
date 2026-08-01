/**
 * @file target.c
 * @brief STM32H7A3 target initialization implementation.
 */

#include "target.h"

#include <stdint.h>

/*
 * stm32h7xx.h must be included before mpu_armv7.h. It provides the Cortex-M7
 * core definitions used by the CMSIS MPU helpers, including MPU, __DMB(),
 * __STATIC_INLINE, and the MPU register masks.
 */
#include "stm32h7xx.h"
#include "mpu_armv7.h"

#include "rcc.h"

#define TARGET_AXI_SRAM_BASE_ADDRESS 0x24000000UL
#define TARGET_FRAMEBUFFER_MPU_REGION_NUMBER 0U
#define TARGET_FRAMEBUFFER_SUBREGION_DISABLE_MASK 0x03U
#define TARGET_STORAGE_FLASH_BASE_ADDRESS 0x080FC000UL
#define TARGET_STORAGE_FLASH_MPU_REGION_NUMBER 1U

extern uint8_t __storage_flash_start__;

static void Target_ConfigureMemoryProtection(void)
{
    const uint32_t nonCacheableNormalMemoryAttributes = ARM_MPU_ACCESS_NORMAL(ARM_MPU_CACHEP_NOCACHE, ARM_MPU_CACHEP_NOCACHE, 1U);

    ARM_MPU_Disable();

    ARM_MPU_ClrRegion(TARGET_FRAMEBUFFER_MPU_REGION_NUMBER);
    ARM_MPU_SetRegion(ARM_MPU_RBAR(TARGET_FRAMEBUFFER_MPU_REGION_NUMBER, TARGET_AXI_SRAM_BASE_ADDRESS), ARM_MPU_RASR_EX(1U, ARM_MPU_AP_FULL, nonCacheableNormalMemoryAttributes, TARGET_FRAMEBUFFER_SUBREGION_DISABLE_MASK, ARM_MPU_REGION_SIZE_1MB));

    ARM_MPU_ClrRegion(TARGET_STORAGE_FLASH_MPU_REGION_NUMBER);
    ARM_MPU_SetRegion(ARM_MPU_RBAR(TARGET_STORAGE_FLASH_MPU_REGION_NUMBER, (uintptr_t)&__storage_flash_start__), ARM_MPU_RASR_EX(1U, ARM_MPU_AP_FULL, nonCacheableNormalMemoryAttributes, 0U, ARM_MPU_REGION_SIZE_16KB));
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

void Target_Init(void)
{
    SCB->CPACR |= (3UL << 20U) | (3UL << 22U);

    __DSB();
    __ISB();

    Target_ConfigureMemoryProtection();

    SCB_EnableICache();
    SCB_EnableDCache();

    RCC_Init();
    SystemCoreClockUpdate();
}

void Target_PowerOff(void)
{
    NVIC_SystemReset();
}