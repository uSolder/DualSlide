/**
 * @file st7701s.c
 * @brief ST7701S LCD controller driver implementation.
 */

#include "st7701s.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define ST7701S_COMMAND_EXIT_SLEEP        0x11U
#define ST7701S_COMMAND_ENTER_SLEEP       0x10U
#define ST7701S_COMMAND_DISPLAY_OFF       0x28U
#define ST7701S_COMMAND_DISPLAY_ON        0x29U

#define ST7701S_RESET_PRE_DELAY_MS        10U
#define ST7701S_RESET_ASSERT_DELAY_MS     20U
#define ST7701S_RESET_RECOVERY_DELAY_MS   120U
#define ST7701S_SLEEP_TRANSITION_DELAY_MS 120U

#define ST7701S_COMMAND_FRAME(value) ((uint16_t)(value) & 0x00FFU)
#define ST7701S_DATA_FRAME(value)    (0x0100U | ((uint16_t)(value) & 0x00FFU))

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static ST7701S_Result ST7701S_ValidateHandle(const ST7701S_Handle *handle);
static ST7701S_Result ST7701S_ConvertSPIResult(SPI_Result result);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static ST7701S_Result ST7701S_ValidateHandle(const ST7701S_Handle *handle)
{
    if ((handle == NULL) || (handle->spi == NULL) || (handle->spi->bus == NULL) || (handle->set_reset == NULL) || (handle->delay_ms == NULL))
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    if (handle->spi->frame_size != SPI_FRAME_SIZE_9_BIT)
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    return ST7701S_RESULT_OK;
}

static ST7701S_Result ST7701S_ConvertSPIResult(SPI_Result result)
{
    switch (result)
    {
        case SPI_RESULT_OK:
            return ST7701S_RESULT_OK;

        case SPI_RESULT_INVALID_ARGUMENT:
            return ST7701S_RESULT_INVALID_ARGUMENT;

        case SPI_RESULT_TIMEOUT:
            return ST7701S_RESULT_TIMEOUT;

        case SPI_RESULT_BUSY:
            return ST7701S_RESULT_BUSY;

        case SPI_RESULT_UNSUPPORTED:
            return ST7701S_RESULT_UNSUPPORTED;

        case SPI_RESULT_NOT_INITIALIZED:
        case SPI_RESULT_IO_ERROR:
        default:
            return ST7701S_RESULT_IO_ERROR;
    }
}

/* -------------------------------------------------------------------------- */
/* Register access                                                            */
/* -------------------------------------------------------------------------- */

ST7701S_Result ST7701S_WriteCommand(ST7701S_Handle *handle, uint8_t command)
{
    uint16_t frame;
    ST7701S_Result result;

    result = ST7701S_ValidateHandle(handle);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    frame = ST7701S_COMMAND_FRAME(command);

    return ST7701S_ConvertSPIResult(SPI_Write(handle->spi, &frame, 1U));
}

ST7701S_Result ST7701S_WriteData(ST7701S_Handle *handle, uint8_t data)
{
    uint16_t frame;
    ST7701S_Result result;

    result = ST7701S_ValidateHandle(handle);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    frame = ST7701S_DATA_FRAME(data);

    return ST7701S_ConvertSPIResult(SPI_Write(handle->spi, &frame, 1U));
}

ST7701S_Result ST7701S_WriteDataBuffer(ST7701S_Handle *handle, const uint8_t *data, size_t length)
{
    size_t index;
    ST7701S_Result result;

    result = ST7701S_ValidateHandle(handle);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    if ((data == NULL) || (length == 0U))
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    for (index = 0U; index < length; index++)
    {
        result = ST7701S_WriteData(handle, data[index]);

        if (result != ST7701S_RESULT_OK)
        {
            return result;
        }
    }

    return ST7701S_RESULT_OK;
}

ST7701S_Result ST7701S_WriteRegister(ST7701S_Handle *handle, uint8_t command, const uint8_t *data, size_t length)
{
    ST7701S_Result result;

    result = ST7701S_WriteCommand(handle, command);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    if (length == 0U)
    {
        return ST7701S_RESULT_OK;
    }

    if (data == NULL)
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    return ST7701S_WriteDataBuffer(handle, data, length);
}

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

ST7701S_Result ST7701S_HardwareReset(ST7701S_Handle *handle)
{
    ST7701S_Result result;

    result = ST7701S_ValidateHandle(handle);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    handle->set_reset(false);
    handle->delay_ms(ST7701S_RESET_PRE_DELAY_MS);

    handle->set_reset(true);
    handle->delay_ms(ST7701S_RESET_ASSERT_DELAY_MS);

    handle->set_reset(false);
    handle->delay_ms(ST7701S_RESET_RECOVERY_DELAY_MS);

    return ST7701S_RESULT_OK;
}

ST7701S_Result ST7701S_ExitSleep(ST7701S_Handle *handle)
{
    ST7701S_Result result;

    result = ST7701S_WriteCommand(handle, ST7701S_COMMAND_EXIT_SLEEP);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    handle->delay_ms(ST7701S_SLEEP_TRANSITION_DELAY_MS);

    return ST7701S_RESULT_OK;
}

ST7701S_Result ST7701S_DisplayOn(ST7701S_Handle *handle)
{
    return ST7701S_WriteCommand(handle, ST7701S_COMMAND_DISPLAY_ON);
}

ST7701S_Result ST7701S_DisplayOff(ST7701S_Handle *handle)
{
    return ST7701S_WriteCommand(handle, ST7701S_COMMAND_DISPLAY_OFF);
}

ST7701S_Result ST7701S_EnterSleep(ST7701S_Handle *handle)
{
    ST7701S_Result result;

    result = ST7701S_WriteCommand(handle, ST7701S_COMMAND_ENTER_SLEEP);

    if (result != ST7701S_RESULT_OK)
    {
        return result;
    }

    handle->delay_ms(ST7701S_SLEEP_TRANSITION_DELAY_MS);

    return ST7701S_RESULT_OK;
}