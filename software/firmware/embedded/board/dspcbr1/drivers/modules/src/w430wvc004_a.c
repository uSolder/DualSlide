/**
 * @file w430wvc004_a.c
 * @brief W430WVC004-A LCD panel driver implementation.
 */

#include "w430wvc004_a.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define W430WVC004_A_SLEEP_EXIT_DELAY_MS     120U
#define W430WVC004_A_POWER_SEQUENCE_DELAY_MS 10U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static W430WVC004_A_ResultTypeDef W430WVC004_A_ConvertControllerResult(ST7701S_ResultTypeDef Result);
static W430WVC004_A_ResultTypeDef W430WVC004_A_WriteRegister(W430WVC004_A_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static W430WVC004_A_ResultTypeDef W430WVC004_A_ConvertControllerResult(ST7701S_ResultTypeDef Result)
{
    switch(Result)
    {
        case ST7701S_RESULT_OK:
            return W430WVC004_A_RESULT_OK;

        case ST7701S_RESULT_INVALID_ARGUMENT:
            return W430WVC004_A_RESULT_INVALID_ARGUMENT;

        case ST7701S_RESULT_TIMEOUT:
            return W430WVC004_A_RESULT_TIMEOUT;

        case ST7701S_RESULT_BUSY:
            return W430WVC004_A_RESULT_BUSY;

        case ST7701S_RESULT_UNSUPPORTED:
            return W430WVC004_A_RESULT_UNSUPPORTED;

        case ST7701S_RESULT_IO_ERROR:
        default:
            return W430WVC004_A_RESULT_IO_ERROR;
    }
}

static W430WVC004_A_ResultTypeDef W430WVC004_A_WriteRegister(W430WVC004_A_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length)
{
    return W430WVC004_A_ConvertControllerResult(ST7701S_WriteRegister(Handle->Controller, Command, Data, Length));
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

W430WVC004_A_ResultTypeDef W430WVC004_A_Init(W430WVC004_A_HandleTypeDef *Handle)
{
    W430WVC004_A_ResultTypeDef Result;
    ST7701S_ResultTypeDef ControllerResult;

    static const uint8_t SelectPage13[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x13U};
    static const uint8_t Page13EF[] = {0x08U};
    static const uint8_t SelectPage10[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x10U};
    static const uint8_t Page10C0[] = {0x63U, 0x00U};
    static const uint8_t Page10C1[] = {0x0AU, 0x0CU};
    static const uint8_t Page10C2[] = {0x01U, 0x08U};
    static const uint8_t Page10C7[] = {0x04U};
    static const uint8_t Page10CC[] = {0x18U};

    static const uint8_t Page10PositiveGamma[] =
    {
        0x00U, 0x0AU, 0x10U, 0x0FU, 0x11U, 0x06U, 0x01U, 0x09U,
        0x09U, 0x1EU, 0x06U, 0x13U, 0x11U, 0x24U, 0x2BU, 0x1FU
    };

    static const uint8_t Page10NegativeGamma[] =
    {
        0x0CU, 0x13U, 0x18U, 0x0AU, 0x0EU, 0x04U, 0x07U, 0x07U,
        0x06U, 0x24U, 0x05U, 0x12U, 0x11U, 0x29U, 0x30U, 0x1FU
    };

    static const uint8_t SelectPage11[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x11U};
    static const uint8_t Page11B0[] = {0x4DU};
    static const uint8_t Page11B1[] = {0x2FU};
    static const uint8_t Page11B2[] = {0x87U};
    static const uint8_t Page11B3[] = {0x80U};
    static const uint8_t Page11B5[] = {0x47U};
    static const uint8_t Page11B7[] = {0x8AU};
    static const uint8_t Page11B8[] = {0x20U};
    static const uint8_t Page11B9[] = {0x10U, 0x13U};
    static const uint8_t Page11C0[] = {0x09U};
    static const uint8_t Page11C1[] = {0x78U};
    static const uint8_t Page11C2[] = {0x78U};
    static const uint8_t Page11D0[] = {0x88U};
    static const uint8_t Page11E0[] = {0x00U, 0x00U, 0x02U};

    static const uint8_t Page11E1[] =
    {
        0x04U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x20U, 0x20U
    };

    static const uint8_t Page11E2[] =
    {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t Page11E3[] = {0x00U, 0x00U, 0x33U, 0x00U};
    static const uint8_t Page11E4[] = {0x22U, 0x00U};

    static const uint8_t Page11E5[] =
    {
        0x04U, 0x34U, 0xAAU, 0xAAU, 0x06U, 0x34U, 0xAAU, 0xAAU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t Page11E6[] = {0x00U, 0x00U, 0x33U, 0x00U};
    static const uint8_t Page11E7[] = {0x22U, 0x00U};

    static const uint8_t Page11E8[] =
    {
        0x05U, 0x34U, 0xAAU, 0xAAU, 0x07U, 0x34U, 0xAAU, 0xAAU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t Page11EB[] = {0x02U, 0x00U, 0x40U, 0x40U, 0x00U, 0x00U, 0x00U};
    static const uint8_t Page11EC[] = {0x00U, 0x00U};

    static const uint8_t Page11ED[] =
    {
        0xAAU, 0x45U, 0x0BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xB0U, 0x54U, 0xAAU
    };

    static const uint8_t Page11EF[] = {0x08U, 0x08U, 0x08U, 0x45U, 0x3FU, 0x54U};
    static const uint8_t Page13E8Step1[] = {0x00U, 0x0EU};
    static const uint8_t SelectPage00[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x00U};
    static const uint8_t Page13E8Step2[] = {0x00U, 0x0CU};
    static const uint8_t Page13E8Step3[] = {0x00U, 0x00U};
    static const uint8_t MemoryAccessControl[] = {0x10U};
    static const uint8_t PixelFormat[] = {0x66U};

#define WRITE_REGISTER(Command, Data)                                                \
    do                                                                                \
    {                                                                                 \
        Result = W430WVC004_A_WriteRegister(Handle, (Command), (Data), sizeof(Data)); \
                                                                                      \
        if(Result != W430WVC004_A_RESULT_OK)                                         \
        {                                                                             \
            return Result;                                                            \
        }                                                                             \
    } while(0)

#define WRITE_COMMAND(Command)                                                        \
    do                                                                                \
    {                                                                                 \
        Result = W430WVC004_A_WriteRegister(Handle, (Command), NULL, 0U);             \
                                                                                      \
        if(Result != W430WVC004_A_RESULT_OK)                                         \
        {                                                                             \
            return Result;                                                            \
        }                                                                             \
    } while(0)

    if((Handle == NULL) || (Handle->Controller == NULL) || (Handle->Controller->DelayMilliseconds == NULL))
    {
        return W430WVC004_A_RESULT_INVALID_ARGUMENT;
    }

    ControllerResult = ST7701S_HardwareReset(Handle->Controller);

    if(ControllerResult != ST7701S_RESULT_OK)
    {
        return W430WVC004_A_ConvertControllerResult(ControllerResult);
    }

    WRITE_REGISTER(0xFFU, SelectPage13);
    WRITE_REGISTER(0xEFU, Page13EF);
    WRITE_REGISTER(0xFFU, SelectPage10);
    WRITE_REGISTER(0xC0U, Page10C0);
    WRITE_REGISTER(0xC1U, Page10C1);
    WRITE_REGISTER(0xC2U, Page10C2);
    WRITE_REGISTER(0xC7U, Page10C7);
    WRITE_REGISTER(0xCCU, Page10CC);
    WRITE_REGISTER(0xB0U, Page10PositiveGamma);
    WRITE_REGISTER(0xB1U, Page10NegativeGamma);

    WRITE_REGISTER(0xFFU, SelectPage11);
    WRITE_REGISTER(0xB0U, Page11B0);
    WRITE_REGISTER(0xB1U, Page11B1);
    WRITE_REGISTER(0xB2U, Page11B2);
    WRITE_REGISTER(0xB3U, Page11B3);
    WRITE_REGISTER(0xB5U, Page11B5);
    WRITE_REGISTER(0xB7U, Page11B7);
    WRITE_REGISTER(0xB8U, Page11B8);
    WRITE_REGISTER(0xB9U, Page11B9);
    WRITE_REGISTER(0xC0U, Page11C0);
    WRITE_REGISTER(0xC1U, Page11C1);
    WRITE_REGISTER(0xC2U, Page11C2);
    WRITE_REGISTER(0xD0U, Page11D0);
    WRITE_REGISTER(0xE0U, Page11E0);
    WRITE_REGISTER(0xE1U, Page11E1);
    WRITE_REGISTER(0xE2U, Page11E2);
    WRITE_REGISTER(0xE3U, Page11E3);
    WRITE_REGISTER(0xE4U, Page11E4);
    WRITE_REGISTER(0xE5U, Page11E5);
    WRITE_REGISTER(0xE6U, Page11E6);
    WRITE_REGISTER(0xE7U, Page11E7);
    WRITE_REGISTER(0xE8U, Page11E8);
    WRITE_REGISTER(0xEBU, Page11EB);
    WRITE_REGISTER(0xECU, Page11EC);
    WRITE_REGISTER(0xEDU, Page11ED);
    WRITE_REGISTER(0xEFU, Page11EF);

    WRITE_REGISTER(0xFFU, SelectPage13);
    WRITE_REGISTER(0xE8U, Page13E8Step1);

    WRITE_REGISTER(0xFFU, SelectPage00);
    WRITE_COMMAND(0x11U);
    Handle->Controller->DelayMilliseconds(W430WVC004_A_SLEEP_EXIT_DELAY_MS);

    WRITE_REGISTER(0xFFU, SelectPage13);
    WRITE_REGISTER(0xE8U, Page13E8Step2);
    Handle->Controller->DelayMilliseconds(W430WVC004_A_POWER_SEQUENCE_DELAY_MS);
    WRITE_REGISTER(0xE8U, Page13E8Step3);

    WRITE_REGISTER(0xFFU, SelectPage00);
    WRITE_REGISTER(0x36U, MemoryAccessControl);
    WRITE_REGISTER(0xFFU, SelectPage00);
    WRITE_REGISTER(0x3AU, PixelFormat);
    WRITE_COMMAND(0x29U);

#undef WRITE_COMMAND
#undef WRITE_REGISTER

    return W430WVC004_A_RESULT_OK;
}
