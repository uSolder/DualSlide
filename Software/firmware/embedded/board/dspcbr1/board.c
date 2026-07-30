/**
 * @file board.c
 * @brief DSPCBR1 board initialization implementation.
 */

#include "board.h"

#include "adc.h"
#include "delay.h"
#include "display_controller.h"
#include "gpio.h"
#include "spi.h"
#include "st7701s.h"
#include "stm32h7a3_defs.h"
#include "target.h"
#include "time.h"
#include "w430wvc004_a.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* LCD timing                                                                 */
/* -------------------------------------------------------------------------- */

#define LCD_HORIZONTAL_SYNC_WIDTH         10U
#define LCD_HORIZONTAL_BACK_PORCH         20U
#define LCD_HORIZONTAL_FRONT_PORCH        40U

#define LCD_VERTICAL_SYNC_HEIGHT          2U
#define LCD_VERTICAL_BACK_PORCH           18U
#define LCD_VERTICAL_FRONT_PORCH          20U

#define LCD_REFRESH_RATE_MILLIHZ          57720U

/* -------------------------------------------------------------------------- */
/* ADC inputs                                                                 */
/* -------------------------------------------------------------------------- */

#define POT_A_INPUT_INDEX                 0U
#define POT_B_INPUT_INDEX                 1U
#define POT_INPUT_COUNT                   2U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Board_SetLCDReset(bool asserted);
static void Board_InitFailure(void);
static void Board_InitTarget(void);
static void Board_InitInterfaces(void);
static void Board_InitDevices(void);

/* -------------------------------------------------------------------------- */
/* Board interface declarations                                               */
/* -------------------------------------------------------------------------- */

static SPI_Bus_Handle SPI2_Bus =
{
    .mosi_pin = PC3,
    .miso_pin = SPI_PIN_UNUSED,
    .sclk_pin = PB13,
    .port = SPI_PORT_2
};

static SPI_Device_Handle LCD_SPI =
{
    .bus = &SPI2_Bus,

    .chip_select_pin = PA10,
    .chip_select_polarity = SPI_CHIP_SELECT_ACTIVE_LOW,

    .frequency_hz = 1000000U,
    .timeout_ms = 1000U,

    .mode = SPI_MODE_0,
    .bit_order = SPI_BIT_ORDER_MSB_FIRST,
    .frame_size = SPI_FRAME_SIZE_9_BIT
};

static const GPIO_PinTypeDef PowerEnablePin =
{
    .Pin = PC13
};

static const GPIO_ConfigTypeDef PowerEnableConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef LCD_ResetPin =
{
    .Pin = PB12
};

static const GPIO_ConfigTypeDef LCD_ResetConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef LCD_BacklightPin =
{
    .Pin = PB4
};

static const GPIO_ConfigTypeDef LCD_BacklightConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_LOW
};

static const GPIO_PinTypeDef BAT_IsetPin =
{
    .Pin = PC15
};

static const GPIO_ConfigTypeDef BAT_IsetConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_LOW
};

static const GPIO_PinTypeDef PrimaryButtonPin =
{
    .Pin = PC12
};

static const GPIO_PinTypeDef SecondaryButtonPin =
{
    .Pin = PB2
};

/*
 * Both buttons pull the input low when pressed. The GPIO contract ignores
 * OutputType and InitialLevel while a pin is configured as an input.
 */
static const GPIO_ConfigTypeDef ButtonInputConfig =
{
    .Mode = GPIO_MODE_INPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const ADC_InputTypeDef POT_Inputs[POT_INPUT_COUNT] =
{
    {
        .Pin = PA1
    },
    {
        .Pin = PA0
    }
};

static ADC_ValueTypeDef POT_Values[POT_INPUT_COUNT];

static DisplayController_Handle LCD_DisplayController =
{
    .pins =
    {
        .horizontal_sync_pin = PC6,
        .vertical_sync_pin = PA7,
        .data_enable_pin = PC5,
        .pixel_clock_pin = PB14,

        .red_pins =
        {
            DISPLAY_CONTROLLER_PIN_UNUSED, /* R0 */
            DISPLAY_CONTROLLER_PIN_UNUSED, /* R1 */
            PC10,                           /* R2 */
            PB0,                            /* R3 */
            PA5,                            /* R4 */
            PC0,                            /* R5 */
            PB1,                            /* R6 */
            PC4                             /* R7 */
        },

        .green_pins =
        {
            DISPLAY_CONTROLLER_PIN_UNUSED, /* G0 */
            DISPLAY_CONTROLLER_PIN_UNUSED, /* G1 */
            PA6,                            /* G2 */
            PC9,                            /* G3 */
            PB10,                           /* G4 */
            PC1,                            /* G5 */
            PC7,                            /* G6 */
            PB15                            /* G7 */
        },

        .blue_pins =
        {
            DISPLAY_CONTROLLER_PIN_UNUSED, /* B0 */
            DISPLAY_CONTROLLER_PIN_UNUSED, /* B1 */
            PD2,                            /* B2 */
            PA8,                            /* B3 */
            PC11,                           /* B4 */
            PB5,                            /* B5 */
            PB8,                            /* B6 */
            PB9                             /* B7 */
        }
    },

    .timing =
    {
        .active_width = W430WVC004_A_WIDTH,
        .active_height = W430WVC004_A_HEIGHT,

        .horizontal_sync_width = LCD_HORIZONTAL_SYNC_WIDTH,
        .horizontal_back_porch = LCD_HORIZONTAL_BACK_PORCH,
        .horizontal_front_porch = LCD_HORIZONTAL_FRONT_PORCH,

        .vertical_sync_height = LCD_VERTICAL_SYNC_HEIGHT,
        .vertical_back_porch = LCD_VERTICAL_BACK_PORCH,
        .vertical_front_porch = LCD_VERTICAL_FRONT_PORCH,

        .refresh_rate_millihz = LCD_REFRESH_RATE_MILLIHZ
    },

    /*
     * The panel uses active-low HSYNC and VSYNC, active-high data enable, and
     * requires an inverted LTDC pixel clock for stable RGB sampling.
     */
    .signals =
    {
        .horizontal_sync = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .vertical_sync = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .data_enable = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .pixel_clock_edge = DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE
    },

    .background_color =
    {
        .red = 0U,
        .green = 0U,
        .blue = 0U
    }
};

/* -------------------------------------------------------------------------- */
/* Board device declarations                                                  */
/* -------------------------------------------------------------------------- */

static ST7701S_Handle LCD_Controller =
{
    .spi = &LCD_SPI,
    .set_reset = Board_SetLCDReset,
    .delay_ms = Delay_ms
};

static W430WVC004_A_Handle LCD_Panel =
{
    .controller = &LCD_Controller
};

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Board_Init(void)
{
    Board_InitTarget();
    Board_InitInterfaces();
    Board_InitDevices();
}

DisplayController_Handle *Board_GetDisplayController(void)
{
    return &LCD_DisplayController;
}

const ADC_InputTypeDef *Board_GetPOTAInput(void)
{
    return &POT_Inputs[POT_A_INPUT_INDEX];
}

const ADC_InputTypeDef *Board_GetPOTBInput(void)
{
    return &POT_Inputs[POT_B_INPUT_INDEX];
}

const GPIO_PinTypeDef *Board_GetPrimaryButtonInput(void)
{
    return &PrimaryButtonPin;
}

const GPIO_PinTypeDef *Board_GetSecondaryButtonInput(void)
{
    return &SecondaryButtonPin;
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Board_SetLCDReset(bool asserted)
{
    GPIO_LevelTypeDef Level;

    /*
     * The ST7701S reset input is active low.
     */
    Level = asserted ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH;

    if(GPIO_Write(&LCD_ResetPin, Level) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static void Board_InitFailure(void)
{
    for(;;)
    {
        __asm volatile ("nop");
    }
}

static void Board_InitTarget(void)
{
    Target_Init();

    if(Time_Init() != TIME_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static void Board_InitInterfaces(void)
{
    if(GPIO_Init(&PowerEnablePin, &PowerEnableConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&LCD_ResetPin, &LCD_ResetConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&LCD_BacklightPin, &LCD_BacklightConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&BAT_IsetPin, &BAT_IsetConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&PrimaryButtonPin, &ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&SecondaryButtonPin, &ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(SPI_BusInit(&SPI2_Bus) != SPI_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(SPI_DeviceInit(&LCD_SPI) != SPI_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Init(POT_Inputs, POT_Values, POT_INPUT_COUNT) != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Start() != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(DisplayController_Init(&LCD_DisplayController) != DISPLAY_CONTROLLER_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static void Board_InitDevices(void)
{
    if(W430WVC004_A_Init(&LCD_Panel) != W430WVC004_A_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Clear(&BAT_IsetPin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Set(&LCD_BacklightPin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }
}