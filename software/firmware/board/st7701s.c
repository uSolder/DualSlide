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

#define ST7701S_COMMAND_FRAME(Value) ((uint16_t)(Value) & 0x00FFU)
#define ST7701S_DATA_FRAME(Value)    (0x0100U | ((uint16_t)(Value) & 0x00FFU))

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static ST7701S_ResultTypeDef ST7701S_ValidateHandle(const ST7701S_HandleTypeDef *Handle);
static ST7701S_ResultTypeDef ST7701S_ConvertSPIResult(SPI_ResultTypeDef Result);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static ST7701S_ResultTypeDef ST7701S_ValidateHandle(const ST7701S_HandleTypeDef *Handle)
{
    if((Handle == NULL) || (Handle->SPI == NULL) || (Handle->SPI->Bus == NULL) || (Handle->SetReset == NULL) || (Handle->DelayMilliseconds == NULL))
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    if(Handle->SPI->FrameSize != SPI_FRAME_SIZE_9_BIT)
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    return ST7701S_RESULT_OK;
}

static ST7701S_ResultTypeDef ST7701S_ConvertSPIResult(SPI_ResultTypeDef Result)
{
    switch(Result)
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

ST7701S_ResultTypeDef ST7701S_WriteCommand(ST7701S_HandleTypeDef *Handle, uint8_t Command)
{
    uint16_t Frame;
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_ValidateHandle(Handle);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    Frame = ST7701S_COMMAND_FRAME(Command);

    return ST7701S_ConvertSPIResult(SPI_Write(Handle->SPI, &Frame, 1U));
}

ST7701S_ResultTypeDef ST7701S_WriteData(ST7701S_HandleTypeDef *Handle, uint8_t Data)
{
    uint16_t Frame;
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_ValidateHandle(Handle);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    Frame = ST7701S_DATA_FRAME(Data);

    return ST7701S_ConvertSPIResult(SPI_Write(Handle->SPI, &Frame, 1U));
}

ST7701S_ResultTypeDef ST7701S_WriteDataBuffer(ST7701S_HandleTypeDef *Handle, const uint8_t *Data, size_t Length)
{
    size_t Index;
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_ValidateHandle(Handle);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    if((Data == NULL) || (Length == 0U))
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    for(Index = 0U; Index < Length; Index++)
    {
        Result = ST7701S_WriteData(Handle, Data[Index]);

        if(Result != ST7701S_RESULT_OK)
        {
            return Result;
        }
    }

    return ST7701S_RESULT_OK;
}

ST7701S_ResultTypeDef ST7701S_WriteRegister(ST7701S_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length)
{
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_WriteCommand(Handle, Command);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    if(Length == 0U)
    {
        return ST7701S_RESULT_OK;
    }

    if(Data == NULL)
    {
        return ST7701S_RESULT_INVALID_ARGUMENT;
    }

    return ST7701S_WriteDataBuffer(Handle, Data, Length);
}

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

ST7701S_ResultTypeDef ST7701S_HardwareReset(ST7701S_HandleTypeDef *Handle)
{
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_ValidateHandle(Handle);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    Handle->SetReset(false);
    Handle->DelayMilliseconds(ST7701S_RESET_PRE_DELAY_MS);

    Handle->SetReset(true);
    Handle->DelayMilliseconds(ST7701S_RESET_ASSERT_DELAY_MS);

    Handle->SetReset(false);
    Handle->DelayMilliseconds(ST7701S_RESET_RECOVERY_DELAY_MS);

    return ST7701S_RESULT_OK;
}

ST7701S_ResultTypeDef ST7701S_ExitSleep(ST7701S_HandleTypeDef *Handle)
{
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_WriteCommand(Handle, ST7701S_COMMAND_EXIT_SLEEP);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    Handle->DelayMilliseconds(ST7701S_SLEEP_TRANSITION_DELAY_MS);

    return ST7701S_RESULT_OK;
}

ST7701S_ResultTypeDef ST7701S_DisplayOn(ST7701S_HandleTypeDef *Handle)
{
    return ST7701S_WriteCommand(Handle, ST7701S_COMMAND_DISPLAY_ON);
}

ST7701S_ResultTypeDef ST7701S_DisplayOff(ST7701S_HandleTypeDef *Handle)
{
    return ST7701S_WriteCommand(Handle, ST7701S_COMMAND_DISPLAY_OFF);
}

ST7701S_ResultTypeDef ST7701S_EnterSleep(ST7701S_HandleTypeDef *Handle)
{
    ST7701S_ResultTypeDef Result;

    Result = ST7701S_WriteCommand(Handle, ST7701S_COMMAND_ENTER_SLEEP);

    if(Result != ST7701S_RESULT_OK)
    {
        return Result;
    }

    Handle->DelayMilliseconds(ST7701S_SLEEP_TRANSITION_DELAY_MS);

    return ST7701S_RESULT_OK;
}
