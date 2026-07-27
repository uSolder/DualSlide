/**
 * @file rcc.h
 * @brief Internal STM32H7A3 reset and clock-control driver.
 *
 * This interface is private to the STM32H7A3 target. Board and application
 * code must not include this file directly.
 */

#ifndef STM32H7A3_INTERNAL_RCC_H
#define STM32H7A3_INTERNAL_RCC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Result returned by an RCC operation.
 */
typedef enum
{
    RCC_RESULT_OK = 0,
    RCC_RESULT_INVALID_ARGUMENT,
    RCC_RESULT_UNSUPPORTED,
    RCC_RESULT_TIMEOUT,
    RCC_RESULT_CLOCK_FAILURE
} RCC_Result;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure the STM32H7A3 system and peripheral clock tree.
 *
 * This function configures the power supply, voltage scaling, flash latency,
 * oscillators, PLLs, bus prescalers, peripheral clock sources, and system
 * clock source.
 *
 * @return RCC_RESULT_OK on success.
 */
RCC_Result RCC_Init(void);

/* -------------------------------------------------------------------------- */
/* Clock information                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Retrieve the kernel clock frequency of an MCU peripheral.
 *
 * @param peripheral CMSIS peripheral instance, such as SPI2 or TIM6.
 *
 * @return Peripheral kernel frequency in hertz, or zero when unsupported.
 */
uint32_t RCC_GetKernelFrequency(const void *peripheral);

/**
 * @brief Retrieve the configured system clock frequency.
 *
 * @return System clock frequency in hertz.
 */
uint32_t RCC_GetSystemClockFrequency(void);

/**
 * @brief Retrieve the configured CPU clock frequency.
 *
 * @return CPU clock frequency in hertz.
 */
uint32_t RCC_GetCPUClockFrequency(void);

/**
 * @brief Retrieve the configured AHB clock frequency.
 *
 * @return AHB clock frequency in hertz.
 */
uint32_t RCC_GetAHBClockFrequency(void);

/* -------------------------------------------------------------------------- */
/* Peripheral control                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enable the clock for an MCU peripheral.
 *
 * @param peripheral CMSIS peripheral instance.
 *
 * @return RCC_RESULT_OK on success.
 */
RCC_Result RCC_EnablePeripheralClock(const void *peripheral);

/**
 * @brief Disable the clock for an MCU peripheral.
 *
 * @param peripheral CMSIS peripheral instance.
 *
 * @return RCC_RESULT_OK on success.
 */
RCC_Result RCC_DisablePeripheralClock(const void *peripheral);

/**
 * @brief Reset an MCU peripheral through RCC.
 *
 * @param peripheral CMSIS peripheral instance.
 *
 * @return RCC_RESULT_OK on success.
 */
RCC_Result RCC_ResetPeripheral(const void *peripheral);

#ifdef __cplusplus
}
#endif

#endif /* STM32H7A3_INTERNAL_RCC_H */