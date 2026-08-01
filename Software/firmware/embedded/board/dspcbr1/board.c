/**
 * @file board.c
 * @brief DSPCBR1 board initialization implementation.
 */

#include "board.h"

#include "adc.h"
#include "delay.h"
#include "display_controller.h"
#include "gpio.h"
#include "power.h"
#include "spi.h"
#include "st7701s.h"
#include "stm32h7a3_defs.h"
#include "target.h"
#include "time.h"
#include "timer.h"
#include "w430wvc004_a.h"

#include <stdbool.h>
#include <stdint.h>

#define LCD_HORIZONTAL_SYNC_WIDTH                     10U
#define LCD_HORIZONTAL_BACK_PORCH                     20U
#define LCD_HORIZONTAL_FRONT_PORCH                    40U
#define LCD_VERTICAL_SYNC_HEIGHT                       2U
#define LCD_VERTICAL_BACK_PORCH                       18U
#define LCD_VERTICAL_FRONT_PORCH                      20U
#define LCD_REFRESH_RATE_MILLIHZ                   57720U

#define POT_A_INPUT_INDEX                              0U
#define POT_B_INPUT_INDEX                              1U
#define USB_CC1_INPUT_INDEX                            2U
#define USB_CC2_INPUT_INDEX                            3U
#define BATTERY_VOLTAGE_INPUT_INDEX                    4U
#define VREFINT_INPUT_INDEX                            5U
#define ADC_INPUT_COUNT                                6U

#define ADC_FULL_SCALE_VALUE                       65535U
#define VREFINT_CALIBRATION_ADDRESS          0x08FFF810UL
#define VREFINT_CALIBRATION_MILLIVOLTS            3300U
#define BATTERY_VOLTAGE_DIVIDER_RATIO                 2U

static void Board_SetLCDReset(bool asserted);
static void Board_InitFailure(void);
static void Board_InitTarget(void);
static void Board_InitCriticalInterfaces(void);
static void Board_InitInterfaces(void);
static void Board_InitDevices(void);
static void Board_InitPower(void);
static Board_WakeReasonTypeDef Board_DetectWakeReason(void);
static uint16_t Board_GetADCInputMillivolts(const ADC_InputTypeDef *input);
static uint16_t Board_GetCC1Millivolts(void);
static uint16_t Board_GetCC2Millivolts(void);
static uint16_t Board_GetBatteryMillivolts(void);

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

static const GPIO_PinTypeDef ChargerCurrentLimitPin =
{
    .Pin = PC15
};

static const GPIO_ConfigTypeDef ChargerCurrentLimitConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef ChargerStatusPin =
{
    .Pin = PC14
};

static const GPIO_ConfigTypeDef ChargerStatusConfig =
{
    .Mode = GPIO_MODE_INPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_UP,
    .InitialLevel = GPIO_LEVEL_LOW
};

static Timer_Handle ChargeLEDTimer =
{
    .timer = TIM4_CH2,
    .frequency_hz = 1000U,
    .update_callback = Power_TimerUpdate,
    .callback_context = NULL
};

static Timer_PWMChannel_Handle ChargeLEDChannel =
{
    .timer = &ChargeLEDTimer,
    .output = TIM4_CH2,
    .pin = PB7,
    .polarity = TIMER_PWM_POLARITY_ACTIVE_HIGH,
    .duty_permille = 500U
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

static const GPIO_PinTypeDef USBPowerDetectPin =
{
    .Pin = PA9
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

/*
 * VREFINT is routed through ADC2 by the STM32H7A3 ADC driver. All external
 * board inputs remain on ADC1.
 */
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
    },
    {
        .Pin = PC2
    },
    {
        .Pin = ADC_PIN_VREFINT
    }
};

static ADC_ValueTypeDef ADC_Values[ADC_INPUT_COUNT];

static Power_Handle BoardPowerHandle =
{
    .charger_current_limit_pin = &ChargerCurrentLimitPin,
    .charger_status_pin = &ChargerStatusPin,
    .charge_led_channel = &ChargeLEDChannel,
    .get_cc1_millivolts = Board_GetCC1Millivolts,
    .get_cc2_millivolts = Board_GetCC2Millivolts,
    .get_battery_millivolts = Board_GetBatteryMillivolts
};

static Board_WakeReasonTypeDef WakeReason;

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
            DISPLAY_CONTROLLER_PIN_UNUSED,
            DISPLAY_CONTROLLER_PIN_UNUSED,
            PC10,
            PB0,
            PA5,
            PC0,
            PB1,
            PC4
        },
        .green_pins =
        {
            DISPLAY_CONTROLLER_PIN_UNUSED,
            DISPLAY_CONTROLLER_PIN_UNUSED,
            PA6,
            PC9,
            PB10,
            PC1,
            PC7,
            PB15
        },
        .blue_pins =
        {
            DISPLAY_CONTROLLER_PIN_UNUSED,
            DISPLAY_CONTROLLER_PIN_UNUSED,
            PD2,
            PA8,
            PC11,
            PB5,
            PB8,
            PB9
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

void Board_Init(void)
{
    Board_InitTarget();
    Board_InitCriticalInterfaces();
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

const GPIO_PinTypeDef *Board_GetUSBPowerInput(void)
{
    return &USBPowerDetectPin;
}

const GPIO_PinTypeDef *Board_GetPrimaryButtonInput(void)
{
    return &PrimaryButtonPin;
}

const GPIO_PinTypeDef *Board_GetSecondaryButtonInput(void)
{
    return &SecondaryButtonPin;
}

void Board_PowerOff(void)
{
    GPIO_Clear(&PowerEnablePin);
    GPIO_Clear(&LCD_BacklightPin);

    while(GPIO_IsHigh(&PrimaryButtonPin));

    Delay_ms(500U);
    Target_PowerOff();
}

static void Board_SetLCDReset(bool asserted)
{
    GPIO_LevelTypeDef level;

    level = asserted ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH;

    if(GPIO_Write(&LCD_ResetPin, level) != GPIO_RESULT_OK)
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

    if(GPIO_Init(&USBPowerDetectPin, &ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Init(ADC_Inputs, ADC_Values, ADC_INPUT_COUNT) != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Start() != ADC_RESULT_OK)
    {
        Board_InitFailure();
    }

    Board_InitPower();
}

static void Board_InitPower(void)
{
    if(GPIO_Init(&ChargerCurrentLimitPin, &ChargerCurrentLimitConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&ChargerStatusPin, &ChargerStatusConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Init(&ChargeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_PWMChannelInit(&ChargeLEDChannel) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Power_Init(&BoardPowerHandle) != POWER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Start(&ChargeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static Board_WakeReasonTypeDef Board_DetectWakeReason(void)
{
    return !GPIO_IsHigh(&PrimaryButtonPin) ? BOARD_WAKE_REASON_EXTERNAL_POWER : BOARD_WAKE_REASON_POWER_BUTTON;
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

static uint16_t Board_GetADCInputMillivolts(const ADC_InputTypeDef *input)
{
    ADC_ValueTypeDef reference_value;
    ADC_ValueTypeDef input_value;
    uint16_t reference_calibration;
    uint32_t supply_millivolts;

    reference_value = ADC_GetValue(&ADC_Inputs[VREFINT_INPUT_INDEX]);

    if(reference_value == 0U)
    {
        return 0U;
    }

    reference_calibration = *((const uint16_t *)VREFINT_CALIBRATION_ADDRESS);
    supply_millivolts = ((uint32_t)VREFINT_CALIBRATION_MILLIVOLTS * reference_calibration) / reference_value;
    input_value = ADC_GetValue(input);

    return (uint16_t)(((uint32_t)input_value * supply_millivolts) / ADC_FULL_SCALE_VALUE);
}

static uint16_t Board_GetCC1Millivolts(void)
{
    return Board_GetADCInputMillivolts(&ADC_Inputs[USB_CC1_INPUT_INDEX]);
}

static uint16_t Board_GetCC2Millivolts(void)
{
    return Board_GetADCInputMillivolts(&ADC_Inputs[USB_CC2_INPUT_INDEX]);
}

static uint16_t Board_GetBatteryMillivolts(void)
{
    return (uint16_t)(Board_GetADCInputMillivolts(&ADC_Inputs[BATTERY_VOLTAGE_INPUT_INDEX]) * BATTERY_VOLTAGE_DIVIDER_RATIO);
}