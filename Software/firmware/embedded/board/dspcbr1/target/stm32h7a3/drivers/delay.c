/**
 * @file delay.c
 * @brief STM32H7A3 blocking delay implementation.
 *
 * This implementation uses the Cortex-M7 DWT cycle counter to provide
 * processor-clock-based microsecond and millisecond delays.
 */

#include "delay.h"

#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define DELAY_MICROSECONDS_PER_SECOND 1000000U
#define DELAY_MICROSECONDS_PER_MILLISECOND 1000U
#define DELAY_DWT_UNLOCK_KEY 0xC5ACCE55UL

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint32_t Delay_CyclesPerMicrosecond;
static bool Delay_Initialized;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Delay_Initialize(void);
static void Delay_WaitCycles(uint32_t cycles);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool Delay_Initialize(void)
{
    uint32_t initial_count;

    if (Delay_Initialized)
    {
        return true;
    }

    if (SystemCoreClock < DELAY_MICROSECONDS_PER_SECOND)
    {
        return false;
    }

    Delay_CyclesPerMicrosecond = SystemCoreClock / DELAY_MICROSECONDS_PER_SECOND;

    if (Delay_CyclesPerMicrosecond == 0U)
    {
        return false;
    }

    /*
     * Enable access to the Cortex-M debug and trace peripherals.
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /*
     * Unlock the DWT registers on implementations that provide a lock access
     * register.
     */
#if defined(DWT_LAR)
    DWT->LAR = DELAY_DWT_UNLOCK_KEY;
#endif

    /*
     * Reset and enable the cycle counter.
     */
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        Delay_CyclesPerMicrosecond = 0U;
        return false;
    }

    /*
     * Confirm that the cycle counter is advancing.
     */
    initial_count = DWT->CYCCNT;

    __NOP();
    __NOP();
    __NOP();
    __NOP();

    if (DWT->CYCCNT == initial_count)
    {
        Delay_CyclesPerMicrosecond = 0U;
        return false;
    }

    Delay_Initialized = true;

    return true;
}

static void Delay_WaitCycles(uint32_t cycles)
{
    uint32_t start_cycles;

    start_cycles = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start_cycles) < cycles)
    {
        __NOP();
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Delay_us(uint32_t delay_us)
{
    uint32_t maximum_chunk_us;
    uint32_t current_chunk_us;
    uint32_t required_cycles;

    if (delay_us == 0U)
    {
        return;
    }

    if (!Delay_Initialize())
    {
        return;
    }

    /*
     * Restrict each wait so the requested cycle count fits within 32 bits.
     * Unsigned subtraction in Delay_WaitCycles() remains valid if CYCCNT wraps
     * during the wait.
     */
    maximum_chunk_us = UINT32_MAX / Delay_CyclesPerMicrosecond;

    while (delay_us > 0U)
    {
        current_chunk_us = delay_us;

        if (current_chunk_us > maximum_chunk_us)
        {
            current_chunk_us = maximum_chunk_us;
        }

        required_cycles = current_chunk_us * Delay_CyclesPerMicrosecond;

        Delay_WaitCycles(required_cycles);

        delay_us -= current_chunk_us;
    }
}

void Delay_ms(uint32_t delay_ms)
{
    while (delay_ms > 0U)
    {
        Delay_us(DELAY_MICROSECONDS_PER_MILLISECOND);
        delay_ms--;
    }
}