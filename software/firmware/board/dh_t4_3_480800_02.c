/**
 * @file dh_t4_3_480800_02.c
 * @brief DH_T4-3_480800-02 LCD panel driver implementation.
 *
 * The init sequence is the supplier's, for the BOE QV043WVQ-N40-DQP0 glass
 * (ST7701S_BOE4.3IPS(QV043WVQ-N40-DQP0)_BW_V1.0.INI). The additions set how
 * this board drives the panel: 3Ah for the 18-bit RGB bus, 36h for its BGR
 * wiring and C7h to mirror the picture along the 480-pixel axis.
 */

#include "dh_t4_3_480800_02.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define DH_T4_3_480800_02_SLEEP_EXIT_DELAY_MS 120U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_ConvertControllerResult(ST7701S_ResultTypeDef Result);
static DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_WriteRegister(DH_T4_3_480800_02_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_ConvertControllerResult(ST7701S_ResultTypeDef Result)
{
    switch(Result)
    {
        case ST7701S_RESULT_OK:
            return DH_T4_3_480800_02_RESULT_OK;

        case ST7701S_RESULT_INVALID_ARGUMENT:
            return DH_T4_3_480800_02_RESULT_INVALID_ARGUMENT;

        case ST7701S_RESULT_TIMEOUT:
            return DH_T4_3_480800_02_RESULT_TIMEOUT;

        case ST7701S_RESULT_BUSY:
            return DH_T4_3_480800_02_RESULT_BUSY;

        case ST7701S_RESULT_UNSUPPORTED:
            return DH_T4_3_480800_02_RESULT_UNSUPPORTED;

        case ST7701S_RESULT_IO_ERROR:
        default:
            return DH_T4_3_480800_02_RESULT_IO_ERROR;
    }
}

static DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_WriteRegister(DH_T4_3_480800_02_HandleTypeDef *Handle, uint8_t Command, const uint8_t *Data, size_t Length)
{
    return DH_T4_3_480800_02_ConvertControllerResult(ST7701S_WriteRegister(Handle->Controller, Command, Data, Length));
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_Init(DH_T4_3_480800_02_HandleTypeDef *Handle)
{
    DH_T4_3_480800_02_ResultTypeDef Result;
    ST7701S_ResultTypeDef ControllerResult;

    static const uint8_t SelectPage13[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x13U};
    static const uint8_t Page13EF[] = {0x08U};
    static const uint8_t SelectPage10[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x10U};
    static const uint8_t Page10C0[] = {0x63U, 0x00U};
    static const uint8_t Page10C1[] = {0x0DU, 0x02U};
    static const uint8_t Page10C2[] = {0x17U, 0x08U};
    static const uint8_t Page10C7[] = {0x04U};
    static const uint8_t Page10CC[] = {0x10U};

    static const uint8_t Page10PositiveGamma[] =
    {
        0xC0U, 0x0DU, 0x95U, 0x0DU, 0x11U, 0x06U, 0x12U, 0x05U,
        0x07U, 0x22U, 0x01U, 0x8EU, 0x0BU, 0xE9U, 0x2DU, 0x68U
    };

    static const uint8_t Page10NegativeGamma[] =
    {
        0xC0U, 0x4DU, 0xD5U, 0x0EU, 0x11U, 0x07U, 0x00U, 0x0DU,
        0x0AU, 0x28U, 0x07U, 0x13U, 0x11U, 0x6EU, 0x76U, 0x56U
    };

    static const uint8_t SelectPage11[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x11U};
    static const uint8_t Page11B0[] = {0x5DU};
    static const uint8_t Page11B1[] = {0x56U};
    static const uint8_t Page11B2[] = {0x87U};
    static const uint8_t Page11B3[] = {0x80U};
    static const uint8_t Page11B5[] = {0x4EU};
    static const uint8_t Page11B7[] = {0x85U};
    static const uint8_t Page11B8[] = {0x21U};
    static const uint8_t Page11B9[] = {0x10U, 0x1FU};
    static const uint8_t Page11BB[] = {0x03U};
    static const uint8_t Page11BC[] = {0x00U};
    static const uint8_t Page11C1[] = {0x78U};
    static const uint8_t Page11C2[] = {0x78U};
    static const uint8_t Page11D0[] = {0x88U};
    static const uint8_t Page11E0[] = {0x00U, 0x3AU, 0x02U};

    static const uint8_t Page11E1[] =
    {
        0x05U, 0xA0U, 0x00U, 0xA0U, 0x04U, 0xA0U,
        0x00U, 0xA0U, 0x00U, 0x40U, 0x40U
    };

    static const uint8_t Page11E2[] =
    {
        0x30U, 0x00U, 0x40U, 0x40U, 0x32U, 0xA0U, 0x00U,
        0xA0U, 0x00U, 0xA0U, 0x00U, 0xA0U, 0x00U
    };

    static const uint8_t Page11E3[] = {0x00U, 0x00U, 0x33U, 0x33U};
    static const uint8_t Page11E4[] = {0x44U, 0x44U};

    static const uint8_t Page11E5[] =
    {
        0x07U, 0x2BU, 0xA0U, 0xA0U, 0x05U, 0x29U, 0xA0U, 0xA0U,
        0x0BU, 0x2FU, 0xA0U, 0xA0U, 0x09U, 0x2DU, 0xA0U, 0xA0U
    };

    static const uint8_t Page11E6[] = {0x00U, 0x00U, 0x33U, 0x33U};
    static const uint8_t Page11E7[] = {0x44U, 0x44U};

    static const uint8_t Page11E8[] =
    {
        0x08U, 0x2CU, 0xA0U, 0xA0U, 0x06U, 0x2AU, 0xA0U, 0xA0U,
        0x0CU, 0x30U, 0xA0U, 0xA0U, 0x0AU, 0x2EU, 0xA0U, 0xA0U
    };

    static const uint8_t Page11EB[] = {0x00U, 0x00U, 0x4EU, 0x4EU, 0x00U, 0x00U, 0x00U};
    static const uint8_t Page11EC[] = {0x02U, 0x00U};

    static const uint8_t Page11ED[] =
    {
        0xA0U, 0x2BU, 0x98U, 0xB4U, 0x56U, 0x7FU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xF7U, 0x65U, 0x4BU, 0x89U, 0xB2U, 0x0AU
    };

    static const uint8_t SelectPage00[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x00U};
    static const uint8_t MemoryAccessControl[] = {0x08U};
    static const uint8_t PixelFormat[] = {0x66U};

#define WRITE_REGISTER(Command, Data)                                                     \
    do                                                                                     \
    {                                                                                      \
        Result = DH_T4_3_480800_02_WriteRegister(Handle, (Command), (Data), sizeof(Data)); \
                                                                                           \
        if(Result != DH_T4_3_480800_02_RESULT_OK)                                         \
        {                                                                                  \
            return Result;                                                                 \
        }                                                                                  \
    } while(0)

#define WRITE_COMMAND(Command)                                                             \
    do                                                                                     \
    {                                                                                      \
        Result = DH_T4_3_480800_02_WriteRegister(Handle, (Command), NULL, 0U);             \
                                                                                           \
        if(Result != DH_T4_3_480800_02_RESULT_OK)                                         \
        {                                                                                  \
            return Result;                                                                 \
        }                                                                                  \
    } while(0)

    if((Handle == NULL) || (Handle->Controller == NULL) || (Handle->Controller->DelayMilliseconds == NULL))
    {
        return DH_T4_3_480800_02_RESULT_INVALID_ARGUMENT;
    }

    ControllerResult = ST7701S_HardwareReset(Handle->Controller);

    if(ControllerResult != ST7701S_RESULT_OK)
    {
        return DH_T4_3_480800_02_ConvertControllerResult(ControllerResult);
    }

    /* Unlike the W430WVC004-A, this glass leaves sleep before it is configured. */
    WRITE_COMMAND(0x11U);
    Handle->Controller->DelayMilliseconds(DH_T4_3_480800_02_SLEEP_EXIT_DELAY_MS);

    WRITE_REGISTER(0xFFU, SelectPage13);
    WRITE_REGISTER(0xEFU, Page13EF);
    WRITE_REGISTER(0xFFU, SelectPage10);
    WRITE_REGISTER(0xC0U, Page10C0);
    WRITE_REGISTER(0xC1U, Page10C1);
    WRITE_REGISTER(0xC2U, Page10C2);
    /* Not in the supplier's sequence: sources run 479 to 0, mirroring the picture left to right. */
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
    WRITE_REGISTER(0xBBU, Page11BB);
    WRITE_REGISTER(0xBCU, Page11BC);
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

    WRITE_REGISTER(0xFFU, SelectPage00);
    /* Not in the supplier's sequence: BGR order for this board's wiring. Reverse gate scan (ML) garbles this glass. */
    WRITE_REGISTER(0x36U, MemoryAccessControl);
    /* Not in the supplier's sequence, whose 24-bit default does not match this board's 18-bit bus. */
    WRITE_REGISTER(0x3AU, PixelFormat);
    WRITE_COMMAND(0x29U);

#undef WRITE_COMMAND
#undef WRITE_REGISTER

    return DH_T4_3_480800_02_RESULT_OK;
}
