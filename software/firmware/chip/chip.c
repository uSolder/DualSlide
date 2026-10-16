/**
 * @file chip.c
 * @brief STM32H7A3 start-up: clocks, caches and memory protection.
 */

#include "chip.h"

#include <stdint.h>

/*
 * stm32h7xx.h must be included before mpu_armv7.h. It provides the Cortex-M7
 * core definitions used by the CMSIS MPU helpers, including MPU, __DMB(),
 * __STATIC_INLINE, and the MPU register masks.
 */
#include "stm32h7xx.h"
#include "mpu_armv7.h"

#include "rcc.h"

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define CHIP_AXI_SRAM_BASE_ADDRESS              0x24000000UL
#define CHIP_FRAMEBUFFER_MPU_REGION_NUMBER      0U
#define CHIP_FRAMEBUFFER_SUBREGION_DISABLE_MASK 0x03U
#define CHIP_STORAGE_FLASH_BASE_ADDRESS         0x080FC000UL
#define CHIP_STORAGE_FLASH_MPU_REGION_NUMBER    1U

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

extern uint8_t __storage_flash_start__;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Chip_ConfigureMemoryProtection(void)
{
    const uint32_t NonCacheableNormalMemoryAttributes = ARM_MPU_ACCESS_NORMAL(ARM_MPU_CACHEP_NOCACHE, ARM_MPU_CACHEP_NOCACHE, 1U);

    ARM_MPU_Disable();

    ARM_MPU_ClrRegion(CHIP_FRAMEBUFFER_MPU_REGION_NUMBER);
    ARM_MPU_SetRegion(ARM_MPU_RBAR(CHIP_FRAMEBUFFER_MPU_REGION_NUMBER, CHIP_AXI_SRAM_BASE_ADDRESS), ARM_MPU_RASR_EX(1U, ARM_MPU_AP_FULL, NonCacheableNormalMemoryAttributes, CHIP_FRAMEBUFFER_SUBREGION_DISABLE_MASK, ARM_MPU_REGION_SIZE_1MB));

    ARM_MPU_ClrRegion(CHIP_STORAGE_FLASH_MPU_REGION_NUMBER);
    ARM_MPU_SetRegion(ARM_MPU_RBAR(CHIP_STORAGE_FLASH_MPU_REGION_NUMBER, (uintptr_t)&__storage_flash_start__), ARM_MPU_RASR_EX(1U, ARM_MPU_AP_FULL, NonCacheableNormalMemoryAttributes, 0U, ARM_MPU_REGION_SIZE_16KB));
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Chip_Init(void)
{
    SCB->CPACR |= (3UL << 20U) | (3UL << 22U);

    __DSB();
    __ISB();

    Chip_ConfigureMemoryProtection();

    SCB_EnableICache();
    SCB_EnableDCache();

    RCC_Init();
    SystemCoreClockUpdate();
}

void Chip_PowerOff(void)
{
    NVIC_SystemReset();
}
