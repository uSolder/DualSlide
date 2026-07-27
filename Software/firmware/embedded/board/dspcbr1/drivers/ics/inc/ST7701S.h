/**
 * @file st7701s.h
 * @brief ST7701S LCD controller driver interface.
 */

#ifndef ST7701S_H
#define ST7701S_H

#include "spi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Callback types                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Control the LCD controller hardware-reset signal.
 *
 * @param asserted true to assert reset; false to release reset.
 */
typedef void (*ST7701S_ResetFunction)(bool asserted);

/**
 * @brief Blocking millisecond delay callback.
 *
 * @param delay_ms Delay duration in milliseconds.
 */
typedef void (*ST7701S_DelayFunction)(uint32_t delay_ms);

/* -------------------------------------------------------------------------- */
/* Public types                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Result returned by an ST7701S operation.
 */
typedef enum
{
    ST7701S_RESULT_OK = 0,
    ST7701S_RESULT_INVALID_ARGUMENT,
    ST7701S_RESULT_TIMEOUT,
    ST7701S_RESULT_BUSY,
    ST7701S_RESULT_UNSUPPORTED,
    ST7701S_RESULT_IO_ERROR
} ST7701S_Result;

/**
 * @brief ST7701S device handle.
 *
 * The board supplies the SPI device and target-independent callbacks used for
 * reset control and timing.
 */
typedef struct
{
    SPI_Device_Handle *spi;
    ST7701S_ResetFunction set_reset;
    ST7701S_DelayFunction delay_ms;
} ST7701S_Handle;

/* -------------------------------------------------------------------------- */
/* Register access                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Write one command byte.
 *
 * @param handle  ST7701S handle.
 * @param command Command byte.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_WriteCommand(ST7701S_Handle *handle, uint8_t command);

/**
 * @brief Write one data byte.
 *
 * @param handle ST7701S handle.
 * @param data   Data byte.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_WriteData(ST7701S_Handle *handle, uint8_t data);

/**
 * @brief Write a sequence of data bytes.
 *
 * @param handle ST7701S handle.
 * @param data   Data buffer.
 * @param length Number of bytes to write.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_WriteDataBuffer(ST7701S_Handle *handle, const uint8_t *data, size_t length);

/**
 * @brief Write a command followed by zero or more data bytes.
 *
 * @param handle  ST7701S handle.
 * @param command Command byte.
 * @param data    Data buffer, or NULL when length is zero.
 * @param length  Number of data bytes.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_WriteRegister(ST7701S_Handle *handle, uint8_t command, const uint8_t *data, size_t length);

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Perform the ST7701S hardware-reset sequence.
 *
 * @param handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_HardwareReset(ST7701S_Handle *handle);

/**
 * @brief Exit sleep mode and wait for the controller to become ready.
 *
 * @param handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_ExitSleep(ST7701S_Handle *handle);

/**
 * @brief Enable display output.
 *
 * @param handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_DisplayOn(ST7701S_Handle *handle);

/**
 * @brief Disable display output.
 *
 * @param handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_DisplayOff(ST7701S_Handle *handle);

/**
 * @brief Enter sleep mode and wait for the transition to complete.
 *
 * @param handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_Result ST7701S_EnterSleep(ST7701S_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* ST7701S_H */