/**
 * @file w430wvc004_a.c
 * @brief W430WVC004-A LCD panel driver implementation.
 */

#include "w430wvc004_a.h"

#include <stddef.h>
#include <stdint.h>

#define W430WVC004_A_SLEEP_EXIT_DELAY_MS     120U
#define W430WVC004_A_POWER_SEQUENCE_DELAY_MS 10U

static W430WVC004_A_Result W430WVC004_A_ConvertControllerResult(ST7701S_Result result);
static W430WVC004_A_Result W430WVC004_A_WriteRegister(W430WVC004_A_Handle *handle, uint8_t command, const uint8_t *data, size_t length);

static W430WVC004_A_Result W430WVC004_A_ConvertControllerResult(ST7701S_Result result)
{
    switch (result)
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

static W430WVC004_A_Result W430WVC004_A_WriteRegister(W430WVC004_A_Handle *handle, uint8_t command, const uint8_t *data, size_t length)
{
    return W430WVC004_A_ConvertControllerResult(ST7701S_WriteRegister(handle->controller, command, data, length));
}

W430WVC004_A_Result W430WVC004_A_Init(W430WVC004_A_Handle *handle)
{
    W430WVC004_A_Result result;
    ST7701S_Result controller_result;

    static const uint8_t ff_page_13[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x13U};
    static const uint8_t ef_08[] = {0x08U};
    static const uint8_t ff_page_10[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x10U};
    static const uint8_t c0[] = {0x63U, 0x00U};
    static const uint8_t c1[] = {0x0AU, 0x0CU};
    static const uint8_t c2[] = {0x01U, 0x08U};
    static const uint8_t c7[] = {0x04U};
    static const uint8_t cc[] = {0x18U};

    static const uint8_t b0_gamma[] =
    {
        0x00U, 0x0AU, 0x10U, 0x0FU, 0x11U, 0x06U, 0x01U, 0x09U,
        0x09U, 0x1EU, 0x06U, 0x13U, 0x11U, 0x24U, 0x2BU, 0x1FU
    };

    static const uint8_t b1_gamma[] =
    {
        0x0CU, 0x13U, 0x18U, 0x0AU, 0x0EU, 0x04U, 0x07U, 0x07U,
        0x06U, 0x24U, 0x05U, 0x12U, 0x11U, 0x29U, 0x30U, 0x1FU
    };

    static const uint8_t ff_page_11[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x11U};
    static const uint8_t b0_4d[] = {0x4DU};
    static const uint8_t b1_2f[] = {0x2FU};
    static const uint8_t b2_87[] = {0x87U};
    static const uint8_t b3_80[] = {0x80U};
    static const uint8_t b5_47[] = {0x47U};
    static const uint8_t b7_8a[] = {0x8AU};
    static const uint8_t b8_20[] = {0x20U};
    static const uint8_t b9[] = {0x10U, 0x13U};
    static const uint8_t c0_09[] = {0x09U};
    static const uint8_t c1_78[] = {0x78U};
    static const uint8_t c2_78[] = {0x78U};
    static const uint8_t d0_88[] = {0x88U};
    static const uint8_t e0[] = {0x00U, 0x00U, 0x02U};

    static const uint8_t e1[] =
    {
        0x04U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x20U, 0x20U
    };

    static const uint8_t e2[] =
    {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t e3[] = {0x00U, 0x00U, 0x33U, 0x00U};
    static const uint8_t e4[] = {0x22U, 0x00U};

    static const uint8_t e5[] =
    {
        0x04U, 0x34U, 0xAAU, 0xAAU, 0x06U, 0x34U, 0xAAU, 0xAAU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t e6[] = {0x00U, 0x00U, 0x33U, 0x00U};
    static const uint8_t e7[] = {0x22U, 0x00U};

    static const uint8_t e8[] =
    {
        0x05U, 0x34U, 0xAAU, 0xAAU, 0x07U, 0x34U, 0xAAU, 0xAAU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    static const uint8_t eb[] = {0x02U, 0x00U, 0x40U, 0x40U, 0x00U, 0x00U, 0x00U};
    static const uint8_t ec[] = {0x00U, 0x00U};

    static const uint8_t ed[] =
    {
        0xAAU, 0x45U, 0x0BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xB0U, 0x54U, 0xAAU
    };

    static const uint8_t ef[] = {0x08U, 0x08U, 0x08U, 0x45U, 0x3FU, 0x54U};
    static const uint8_t e8_000e[] = {0x00U, 0x0EU};
    static const uint8_t ff_page_00[] = {0x77U, 0x01U, 0x00U, 0x00U, 0x00U};
    static const uint8_t e8_000c[] = {0x00U, 0x0CU};
    static const uint8_t e8_0000[] = {0x00U, 0x00U};
    static const uint8_t command_36[] = {0x10U};
    static const uint8_t command_3a[] = {0x66U};

#define WRITE_REGISTER(command, data)                                                \
    do                                                                                \
    {                                                                                 \
        result = W430WVC004_A_WriteRegister(handle, (command), (data), sizeof(data)); \
                                                                                      \
        if (result != W430WVC004_A_RESULT_OK)                                         \
        {                                                                             \
            return result;                                                            \
        }                                                                             \
    } while (0)

#define WRITE_COMMAND(command)                                                        \
    do                                                                                \
    {                                                                                 \
        result = W430WVC004_A_WriteRegister(handle, (command), NULL, 0U);             \
                                                                                      \
        if (result != W430WVC004_A_RESULT_OK)                                         \
        {                                                                             \
            return result;                                                            \
        }                                                                             \
    } while (0)

    if ((handle == NULL) || (handle->controller == NULL) || (handle->controller->delay_ms == NULL))
    {
        return W430WVC004_A_RESULT_INVALID_ARGUMENT;
    }

    controller_result = ST7701S_HardwareReset(handle->controller);

    if (controller_result != ST7701S_RESULT_OK)
    {
        return W430WVC004_A_ConvertControllerResult(controller_result);
    }

    WRITE_REGISTER(0xFFU, ff_page_13);
    WRITE_REGISTER(0xEFU, ef_08);
    WRITE_REGISTER(0xFFU, ff_page_10);
    WRITE_REGISTER(0xC0U, c0);
    WRITE_REGISTER(0xC1U, c1);
    WRITE_REGISTER(0xC2U, c2);
    WRITE_REGISTER(0xC7U, c7);
    WRITE_REGISTER(0xCCU, cc);
    WRITE_REGISTER(0xB0U, b0_gamma);
    WRITE_REGISTER(0xB1U, b1_gamma);

    WRITE_REGISTER(0xFFU, ff_page_11);
    WRITE_REGISTER(0xB0U, b0_4d);
    WRITE_REGISTER(0xB1U, b1_2f);
    WRITE_REGISTER(0xB2U, b2_87);
    WRITE_REGISTER(0xB3U, b3_80);
    WRITE_REGISTER(0xB5U, b5_47);
    WRITE_REGISTER(0xB7U, b7_8a);
    WRITE_REGISTER(0xB8U, b8_20);
    WRITE_REGISTER(0xB9U, b9);
    WRITE_REGISTER(0xC0U, c0_09);
    WRITE_REGISTER(0xC1U, c1_78);
    WRITE_REGISTER(0xC2U, c2_78);
    WRITE_REGISTER(0xD0U, d0_88);
    WRITE_REGISTER(0xE0U, e0);
    WRITE_REGISTER(0xE1U, e1);
    WRITE_REGISTER(0xE2U, e2);
    WRITE_REGISTER(0xE3U, e3);
    WRITE_REGISTER(0xE4U, e4);
    WRITE_REGISTER(0xE5U, e5);
    WRITE_REGISTER(0xE6U, e6);
    WRITE_REGISTER(0xE7U, e7);
    WRITE_REGISTER(0xE8U, e8);
    WRITE_REGISTER(0xEBU, eb);
    WRITE_REGISTER(0xECU, ec);
    WRITE_REGISTER(0xEDU, ed);
    WRITE_REGISTER(0xEFU, ef);

    WRITE_REGISTER(0xFFU, ff_page_13);
    WRITE_REGISTER(0xE8U, e8_000e);

    WRITE_REGISTER(0xFFU, ff_page_00);
    WRITE_COMMAND(0x11U);
    handle->controller->delay_ms(W430WVC004_A_SLEEP_EXIT_DELAY_MS);

    WRITE_REGISTER(0xFFU, ff_page_13);
    WRITE_REGISTER(0xE8U, e8_000c);
    handle->controller->delay_ms(W430WVC004_A_POWER_SEQUENCE_DELAY_MS);
    WRITE_REGISTER(0xE8U, e8_0000);

    WRITE_REGISTER(0xFFU, ff_page_00);
    WRITE_REGISTER(0x36U, command_36);
    WRITE_REGISTER(0xFFU, ff_page_00);
    WRITE_REGISTER(0x3AU, command_3a);
    WRITE_COMMAND(0x29U);

#undef WRITE_COMMAND
#undef WRITE_REGISTER

    return W430WVC004_A_RESULT_OK;
}