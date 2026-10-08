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
 * @param Asserted true to assert reset; false to release reset.
 */
typedef void (*ST7701S_ResetFunctionTypeDef)(bool Asserted);

/**
 * @brief Blocking millisecond delay callback.
 *
 * @param DelayMilliseconds Delay duration in milliseconds.
 */
typedef void (*ST7701S_DelayFunctionTypeDef)(uint32_t DelayMilliseconds);

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
} ST7701S_ResultTypeDef;

/**
 * @brief ST7701S device handle.
 *
 * The board supplies the SPI device and target-independent callbacks used for
 * reset control and timing.
 */
typedef struct
{
    SPI_DeviceTypeDef *SPI;
    ST7701S_ResetFunctionTypeDef SetReset;
    ST7701S_DelayFunctionTypeDef DelayMilliseconds;
} ST7701S_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/* Register access                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Write one command byte.
 *
 * @param Handle  ST7701S handle.
 * @param Command Command byte.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_WriteCommand(ST7701S_HandleTypeDef *Handle, uint8_t Command);

/**
 * @brief Write one data byte.
 *
 * @param Handle ST7701S handle.
 * @param Data   Data byte.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_WriteData(ST7701S_HandleTypeDef *Handle, uint8_t Data);

/**
 * @brief Write a sequence of data bytes.
 *
 * @param Handle ST7701S handle.
 * @param Data   Data buffer.
 * @param Length Number of bytes to write.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_WriteDataBuffer(ST7701S_HandleTypeDef *Handle, const uint8_t *Data, size_t Length);

/**
 * @brief Write a command followed by zero or more data bytes.
 *
 * @param Handle  ST7701S handle.
 * @param Command Command byte.
 * @param Data    Data buffer, or NULL when length is zero.
 * @param Length  Number of data bytes.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_WriteRegister(ST7701S_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length);

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Perform the ST7701S hardware-reset sequence.
 *
 * @param Handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_HardwareReset(ST7701S_HandleTypeDef *Handle);

/**
 * @brief Exit sleep mode and wait for the controller to become ready.
 *
 * @param Handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_ExitSleep(ST7701S_HandleTypeDef *Handle);

/**
 * @brief Enable display output.
 *
 * @param Handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_DisplayOn(ST7701S_HandleTypeDef *Handle);

/**
 * @brief Disable display output.
 *
 * @param Handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_DisplayOff(ST7701S_HandleTypeDef *Handle);

/**
 * @brief Enter sleep mode and wait for the transition to complete.
 *
 * @param Handle ST7701S handle.
 *
 * @return ST7701S_RESULT_OK on success.
 */
ST7701S_ResultTypeDef ST7701S_EnterSleep(ST7701S_HandleTypeDef *Handle);

#ifdef __cplusplus
}
#endif

#endif /* ST7701S_H */
