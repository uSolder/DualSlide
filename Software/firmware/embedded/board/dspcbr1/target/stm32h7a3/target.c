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

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/*
 * The linker places normal program data in AXI SRAM1 and the two LTDC
 * framebuffers in AXI SRAM2 and AXI SRAM3:
 *
 *   AXI SRAM1: 0x24000000-0x2403FFFF
 *   AXI SRAM2: 0x24040000-0x2409FFFF
 *   AXI SRAM3: 0x240A0000-0x240FFFFF
 *
 * One 1 MB MPU region is divided into eight 128 KB subregions. Disabling
 * subregions 0 and 1 leaves 0x24040000-0x240FFFFF covered, which exactly spans
 * AXI SRAM2 and AXI SRAM3.
 */
#define TARGET_AXI_SRAM_BASE_ADDRESS                  0x24000000UL
#define TARGET_FRAMEBUFFER_MPU_REGION_NUMBER          0U
#define TARGET_FRAMEBUFFER_SUBREGION_DISABLE_MASK     0x03U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Target_ConfigureFramebufferMemory(void);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Mark AXI SRAM2 and AXI SRAM3 as normal, shareable, non-cacheable memory.
 *
 * LTDC reads the framebuffers directly while the Cortex-M7 writes the alternate
 * framebuffer. Making these banks non-cacheable removes the need for explicit
 * framebuffer cache maintenance and prevents stale cache lines from being
 * presented.
 */
static void Target_ConfigureFramebufferMemory(void)
{
    const uint32_t framebuffer_access_attributes =
        ARM_MPU_ACCESS_NORMAL(
            ARM_MPU_CACHEP_NOCACHE,
            ARM_MPU_CACHEP_NOCACHE,
            1U);

    ARM_MPU_Disable();
    ARM_MPU_ClrRegion(TARGET_FRAMEBUFFER_MPU_REGION_NUMBER);

    ARM_MPU_SetRegion(
        ARM_MPU_RBAR(
            TARGET_FRAMEBUFFER_MPU_REGION_NUMBER,
            TARGET_AXI_SRAM_BASE_ADDRESS),
        ARM_MPU_RASR_EX(
            1U,
            ARM_MPU_AP_FULL,
            framebuffer_access_attributes,
            TARGET_FRAMEBUFFER_SUBREGION_DISABLE_MASK,
            ARM_MPU_REGION_SIZE_1MB));

    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Target_Init(void)
{
    SCB->CPACR |= (3UL << 20U) | (3UL << 22U);

    __DSB();
    __ISB();

    Target_ConfigureFramebufferMemory();

    SCB_EnableICache();
    SCB_EnableDCache();

    RCC_Init();
    SystemCoreClockUpdate();
}