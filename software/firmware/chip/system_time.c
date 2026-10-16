/**
 * @file system_time.c
 * @brief Milliseconds since start-up, from the Cortex-M7 SysTick timer.
 */

#include "system_time.h"

#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define SYSTEM_TIME_TICKS_PER_SECOND 1000U

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Milliseconds elapsed since SystemTime_Init().
 *
 * This value is updated by SysTick_Handler() and read by application code.
 * Aligned 32-bit accesses are atomic on the Cortex-M7.
 */
static volatile uint32_t SystemTime_Milliseconds;

static bool SystemTime_Initialized;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool SystemTime_Init(void)
{
    uint32_t ReloadValue;

    if(SystemTime_Initialized)
    {
        return true;
    }

    if(SystemCoreClock < SYSTEM_TIME_TICKS_PER_SECOND)
    {
        return false;
    }

    /*
     * SysTick counts from the reload value down to zero.
     * A period of N processor cycles therefore requires a reload value of N - 1.
     */
    ReloadValue = (SystemCoreClock / SYSTEM_TIME_TICKS_PER_SECOND) - 1U;

    /*
     * The SysTick reload register is 24 bits wide.
     */
    if(ReloadValue > SysTick_LOAD_RELOAD_Msk)
    {
        return false;
    }

    SystemTime_Milliseconds = 0U;

    /*
     * SysTick_Config() configures SysTick to use the processor clock, enables
     * its interrupt, clears the current counter, and starts the timer.
     */
    if(SysTick_Config(ReloadValue + 1U) != 0U)
    {
        return false;
    }

    SystemTime_Initialized = true;

    return true;
}

uint32_t SystemTime_GetMilliseconds(void)
{
    if(!SystemTime_Initialized)
    {
        return 0U;
    }

    return SystemTime_Milliseconds;
}

void SystemTime_DelayMilliseconds(uint32_t Milliseconds)
{
    const uint32_t Start = SystemTime_GetMilliseconds();

    while((uint32_t)(SystemTime_GetMilliseconds() - Start) < Milliseconds)
    {
    }
}

/* -------------------------------------------------------------------------- */
/* Interrupt handlers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handles the Cortex-M7 SysTick interrupt.
 *
 * This handler executes once per millisecond after SystemTime_Init() succeeds.
 */
void SysTick_Handler(void)
{
    SystemTime_Milliseconds++;
}
