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

#define DELAY_MICROSECONDS_PER_SECOND      1000000U
#define DELAY_MICROSECONDS_PER_MILLISECOND 1000U
#define DELAY_DWT_UNLOCK_KEY               0xC5ACCE55UL

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint32_t Delay_CyclesPerMicrosecond;
static bool Delay_Initialized;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Delay_Initialize(void);
static void Delay_WaitCycles(uint32_t Cycles);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool Delay_Initialize(void)
{
    uint32_t InitialCount;

    if(Delay_Initialized)
    {
        return true;
    }

    if(SystemCoreClock < DELAY_MICROSECONDS_PER_SECOND)
    {
        return false;
    }

    Delay_CyclesPerMicrosecond = SystemCoreClock / DELAY_MICROSECONDS_PER_SECOND;

    if(Delay_CyclesPerMicrosecond == 0U)
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

    if((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        Delay_CyclesPerMicrosecond = 0U;
        return false;
    }

    /*
     * Confirm that the cycle counter is advancing.
     */
    InitialCount = DWT->CYCCNT;

    __NOP();
    __NOP();
    __NOP();
    __NOP();

    if(DWT->CYCCNT == InitialCount)
    {
        Delay_CyclesPerMicrosecond = 0U;
        return false;
    }

    Delay_Initialized = true;

    return true;
}

static void Delay_WaitCycles(uint32_t Cycles)
{
    uint32_t StartCycles;

    StartCycles = DWT->CYCCNT;

    while((uint32_t)(DWT->CYCCNT - StartCycles) < Cycles)
    {
        __NOP();
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Delay_Microseconds(uint32_t DelayMicroseconds)
{
    uint32_t MaximumChunkMicroseconds;
    uint32_t CurrentChunkMicroseconds;
    uint32_t RequiredCycles;

    if(DelayMicroseconds == 0U)
    {
        return;
    }

    if(!Delay_Initialize())
    {
        return;
    }

    /*
     * Restrict each wait so the requested cycle count fits within 32 bits.
     * Unsigned subtraction in Delay_WaitCycles() remains valid if CYCCNT wraps
     * during the wait.
     */
    MaximumChunkMicroseconds = UINT32_MAX / Delay_CyclesPerMicrosecond;

    while(DelayMicroseconds > 0U)
    {
        CurrentChunkMicroseconds = DelayMicroseconds;

        if(CurrentChunkMicroseconds > MaximumChunkMicroseconds)
        {
            CurrentChunkMicroseconds = MaximumChunkMicroseconds;
        }

        RequiredCycles = CurrentChunkMicroseconds * Delay_CyclesPerMicrosecond;

        Delay_WaitCycles(RequiredCycles);

        DelayMicroseconds -= CurrentChunkMicroseconds;
    }
}



void Delay_Milliseconds(uint32_t DelayMilliseconds)
{
    while(DelayMilliseconds > 0U)
    {
        Delay_Microseconds(DELAY_MICROSECONDS_PER_MILLISECOND);
        DelayMilliseconds--;
    }
}
