/**
 * @file time.c
 * @brief STM32H7A3 monotonic system-time implementation.
 *
 * This implementation uses the Cortex-M7 SysTick peripheral to maintain a
 * monotonic millisecond counter.
 */

#include "time.h"

#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define TIME_TICKS_PER_SECOND 1000U

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Milliseconds elapsed since time-service initialization.
 *
 * This value is updated by SysTick_Handler() and read by application code.
 * Aligned 32-bit accesses are atomic on the Cortex-M7.
 */
static volatile uint32_t Time_Milliseconds;

/**
 * @brief Indicates whether the time service has been initialized.
 */
static bool Time_Initialized;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

Time_Result Time_Init(void)
{
    uint32_t reload_value;

    if (Time_Initialized)
    {
        return TIME_RESULT_OK;
    }

    if (SystemCoreClock < TIME_TICKS_PER_SECOND)
    {
        return TIME_RESULT_ERROR;
    }

    /*
     * SysTick counts from the reload value down to zero.
     * A period of N processor cycles therefore requires a reload value of N - 1.
     */
    reload_value = (SystemCoreClock / TIME_TICKS_PER_SECOND) - 1U;

    /*
     * The SysTick reload register is 24 bits wide.
     */
    if (reload_value > SysTick_LOAD_RELOAD_Msk)
    {
        return TIME_RESULT_UNSUPPORTED;
    }

    Time_Milliseconds = 0U;

    /*
     * SysTick_Config() configures SysTick to use the processor clock, enables
     * its interrupt, clears the current counter, and starts the timer.
     */
    if (SysTick_Config(reload_value + 1U) != 0U)
    {
        return TIME_RESULT_ERROR;
    }

    Time_Initialized = true;

    return TIME_RESULT_OK;
}

uint32_t Time_GetMilliseconds(void)
{
    if (!Time_Initialized)
    {
        return 0U;
    }

    return Time_Milliseconds;
}

/* -------------------------------------------------------------------------- */
/* Interrupt handlers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handles the Cortex-M7 SysTick interrupt.
 *
 * This handler executes once per millisecond after Time_Init() succeeds.
 */
void SysTick_Handler(void)
{
    Time_Milliseconds++;
}