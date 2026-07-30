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
#include "timer.h"
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
#define USB_CC1_INPUT_INDEX                2U
#define USB_CC2_INPUT_INDEX                3U
#define ADC_INPUT_COUNT                    4U

/*
 * A Type-C source advertising its default current produces less than 0.66 V
 * on a 5.1-kOhm Rd. Sources advertising 1.5 A or 3.0 A exceed this level.
 * ADC1 uses its full 16-bit range with a 3.3-V analog supply.
 */
#define USB_CC_FAST_CURRENT_THRESHOLD      13107U

/* -------------------------------------------------------------------------- */
/* Charge indicator                                                           */
/* -------------------------------------------------------------------------- */

#define CHARGE_LED_MINIMUM_DUTY_PERMILLE   500U
#define CHARGE_LED_MAXIMUM_DUTY_PERMILLE   1000U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Board_SetLCDReset(bool asserted);
static void Board_InitFailure(void);
static void Board_InitTarget(void);
static void Board_InitCriticalInterfaces(void);
static void Board_InitInterfaces(void);
static void Board_InitDevices(void);
static Board_WakeReasonTypeDef Board_DetectWakeReason(void);
static void Board_BatteryChargeInterrupt(void *Context);
static void Board_UpdateOrangeLED(void *Context);
static bool Board_IsFastUSBCurrentAvailable(void);
static void Board_UpdateBatteryChargeCurrentLimit(bool fast_current_available);

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
    .InitialLevel = GPIO_LEVEL_LOW
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
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef BatteryChargePin =
{
    .Pin = PC14
};

static const GPIO_ConfigTypeDef BatteryChargeConfig =
{
    .Mode = GPIO_MODE_INPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_UP,
    .InitialLevel = GPIO_LEVEL_LOW
};

static const GPIO_InterruptConfigTypeDef BatteryChargeInterruptConfig =
{
    .Mode = GPIO_INTERRUPT_BOTH_EDGES,
    .Callback = Board_BatteryChargeInterrupt,
    .Context = NULL
};

static const GPIO_PinTypeDef RedLED_Pin =
{
    .Pin = PB6
};

static const GPIO_ConfigTypeDef LED_Config =
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
 * Both buttons drive the input high when pressed. The GPIO contract ignores
 * OutputType and InitialLevel while a pin is configured as an input.
 */
static const GPIO_ConfigTypeDef ButtonInputConfig =
{
    .Mode = GPIO_MODE_INPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const ADC_InputTypeDef ADC_Inputs[ADC_INPUT_COUNT] =
{
    {
        .Pin = PA1
    },
    {
        .Pin = PA0
    },
    {
        .Pin = PA2
    },
    {
        .Pin = PA3
    }
};

static ADC_ValueTypeDef ADC_Values[ADC_INPUT_COUNT];

static Board_WakeReasonTypeDef WakeReason;
static volatile bool Board_IsCharging;
static uint16_t OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
static bool OrangeLEDDutyIncreasing = true;
static bool OrangeLEDSlowUpdateToggle;
static bool Board_IsFastUSBCurrent;

static Timer_Handle OrangeLEDTimer =
{
    .timer = TIM4_CH2,
    .frequency_hz = 1000U,
    .update_callback = Board_UpdateOrangeLED,
    .callback_context = NULL
};

static Timer_PWMChannel_Handle OrangeLEDChannel =
{
    .timer = &OrangeLEDTimer,
    .output = TIM4_CH2,
    .pin = PB7,
    .polarity = TIMER_PWM_POLARITY_ACTIVE_HIGH,
    .duty_permille = CHARGE_LED_MINIMUM_DUTY_PERMILLE
};

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
    Board_InitCriticalInterfaces();
    Board_IsCharging = GPIO_IsLow(&BatteryChargePin);
    WakeReason = Board_DetectWakeReason();

    if(WakeReason == BOARD_WAKE_REASON_EXTERNAL_POWER)
    {
        while(GPIO_IsLow(&PrimaryButtonPin));
    }

    if(GPIO_Set(&PowerEnablePin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    Board_InitInterfaces();
    Board_InitDevices();
}

Board_WakeReasonTypeDef Board_GetWakeReason(void)
{
    return WakeReason;
}

DisplayController_Handle *Board_GetDisplayController(void)
{
    return &LCD_DisplayController;
}

const ADC_InputTypeDef *Board_GetPOTAInput(void)
{
    return &ADC_Inputs[POT_A_INPUT_INDEX];
}

const ADC_InputTypeDef *Board_GetPOTBInput(void)
{
    return &ADC_Inputs[POT_B_INPUT_INDEX];
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

static void Board_InitCriticalInterfaces(void)
{
    if(GPIO_Init(&PowerEnablePin, &PowerEnableConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&PrimaryButtonPin, &ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    /* High selects the charger IC's 500-mA input-current limit. */
    if(GPIO_Init(&BAT_IsetPin, &BAT_IsetConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&BatteryChargePin, &BatteryChargeConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    Board_IsCharging = GPIO_IsLow(&BatteryChargePin);

    if(GPIO_RegisterInterrupt(&BatteryChargePin, &BatteryChargeInterruptConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    /*
     * The ADC has a fixed conversion list. Initialize all board inputs here
     * so CC current advertisement is available while the device is charging
     * with its main power rail disabled.
     */
    if(ADC_Init(ADC_Inputs, ADC_Values, ADC_INPUT_COUNT) != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Start() != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Init(&OrangeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_PWMChannelInit(&OrangeLEDChannel) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Start(&OrangeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static Board_WakeReasonTypeDef Board_DetectWakeReason(void)
{
    /* The power button drives its input high while it is held. */
    return !GPIO_IsHigh(&PrimaryButtonPin) ? BOARD_WAKE_REASON_EXTERNAL_POWER : BOARD_WAKE_REASON_POWER_BUTTON;
}

static void Board_BatteryChargeInterrupt(void *Context)
{
    (void)Context;

    Board_IsCharging = GPIO_IsLow(&BatteryChargePin);
}

static void Board_UpdateOrangeLED(void *Context)
{
    bool FastUSBCurrentAvailable;
    bool UpdateDuty;

    (void)Context;

    FastUSBCurrentAvailable = Board_IsFastUSBCurrentAvailable();
    Board_UpdateBatteryChargeCurrentLimit(FastUSBCurrentAvailable);

    if(!Board_IsCharging)
    {
        (void)Timer_OutputDisable(&OrangeLEDChannel);
        return;
    }

    (void)Timer_OutputEnable(&OrangeLEDChannel);

    /*
     * TIM4 calls this callback every millisecond. One duty step per callback
     * gives a 1-Hz triangle wave. Updating every second callback gives 0.5 Hz.
     */
    UpdateDuty = FastUSBCurrentAvailable;

    if(!UpdateDuty)
    {
        OrangeLEDSlowUpdateToggle = !OrangeLEDSlowUpdateToggle;
        UpdateDuty = OrangeLEDSlowUpdateToggle;
    }

    if(!UpdateDuty)
    {
        return;
    }

    if(OrangeLEDDutyIncreasing)
    {
        OrangeLEDDutyPermille++;

        if(OrangeLEDDutyPermille >= CHARGE_LED_MAXIMUM_DUTY_PERMILLE)
        {
            OrangeLEDDutyPermille = CHARGE_LED_MAXIMUM_DUTY_PERMILLE;
            OrangeLEDDutyIncreasing = false;
        }
    }
    else
    {
        OrangeLEDDutyPermille--;

        if(OrangeLEDDutyPermille <= CHARGE_LED_MINIMUM_DUTY_PERMILLE)
        {
            OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
            OrangeLEDDutyIncreasing = true;
        }
    }

    (void)Timer_SetPWMDutyPermille(&OrangeLEDChannel, OrangeLEDDutyPermille);
}

static bool Board_IsFastUSBCurrentAvailable(void)
{
    ADC_ValueTypeDef CC1Voltage;
    ADC_ValueTypeDef CC2Voltage;

    CC1Voltage = ADC_GetValue(&ADC_Inputs[USB_CC1_INPUT_INDEX]);
    CC2Voltage = ADC_GetValue(&ADC_Inputs[USB_CC2_INPUT_INDEX]);

    return (CC1Voltage >= USB_CC_FAST_CURRENT_THRESHOLD) ||  (CC2Voltage >= USB_CC_FAST_CURRENT_THRESHOLD);
}

static void Board_UpdateBatteryChargeCurrentLimit(bool fast_current_available)
{
    if(fast_current_available == Board_IsFastUSBCurrent)
    {
        return;
    }

    /* Low selects 1 A; high selects the default 500-mA limit. */
    if(fast_current_available)
    {
        (void)GPIO_Clear(&BAT_IsetPin);
    }
    else
    {
        (void)GPIO_Set(&BAT_IsetPin);
    }

    Board_IsFastUSBCurrent = fast_current_available;
}

static void Board_InitInterfaces(void)
{
    if(GPIO_Init(&RedLED_Pin, &LED_Config) != GPIO_RESULT_OK)
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

    if(GPIO_Set(&LCD_BacklightPin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }
}

void Board_PowerOff(void)
{
    GPIO_Clear(&PowerEnablePin);
    GPIO_Clear(&LCD_BacklightPin);
    while(GPIO_IsHigh(&PrimaryButtonPin));
    Delay_ms(500);
    Target_PowerOff();
}