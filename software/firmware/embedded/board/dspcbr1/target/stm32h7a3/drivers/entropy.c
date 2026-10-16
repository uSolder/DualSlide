/**
 * @file entropy.c
 * @brief STM32H7A3 true random number source, using the RNG peripheral.
 *
 * The RNG's kernel clock is HSI48 (the reset selection), which the clock
 * setup already runs for USB. The generator keeps its reset configuration.
 */

#include "entropy.h"

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* A value is ready about 40 RNG clock cycles after the last; this allows far more. */
#define ENTROPY_TIMEOUT_ITERATIONS          (100000UL)

#define ENTROPY_FAULTS                      (RNG_SR_SECS | RNG_SR_CECS)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static bool Entropy_Started;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Entropy_Read(uint32_t *Value)
{
    uint32_t Timeout;

    if(Value == NULL)
    {
        return false;
    }

    if(!Entropy_Started)
    {
        RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
        (void)RCC->AHB2ENR;
        RNG->CR |= RNG_CR_RNGEN;
        Entropy_Started = true;
    }

    for(Timeout = 0U; Timeout < ENTROPY_TIMEOUT_ITERATIONS; Timeout++)
    {
        uint32_t Status = RNG->SR;

        if((Status & ENTROPY_FAULTS) != 0U)
        {
            return false;
        }

        if((Status & RNG_SR_DRDY) != 0U)
        {
            *Value = RNG->DR;

            /* A zero read means the value was discarded after a fault. */
            if(*Value != 0U)
            {
                return true;
            }
        }
    }

    return false;
}
