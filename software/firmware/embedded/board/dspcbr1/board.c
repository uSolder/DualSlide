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
#include "usbd.h"
#include "w430wvc004_a.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

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

#define ADC_FULL_SCALE_VALUE                        65535U
#define VREFINT_CALIBRATION_ADDRESS           0x08FFF810UL
#define VREFINT_CALIBRATION_MILLIVOLTS               3300U
#define BATTERY_VOLTAGE_DIVIDER_RATIO                   2U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Board_SetLCDReset(bool Asserted);
static void Board_InitFailure(void);
static void Board_InitTarget(void);
static void Board_InitCriticalInterfaces(void);
static void Board_InitInterfaces(void);
static void Board_InitDevices(void);
static void Board_InitPower(void);
static Board_WakeReasonTypeDef Board_DetectWakeReason(void);
static uint16_t Board_GetADCInputMillivolts(const ADC_InputTypeDef *Input);
static uint16_t Board_GetCC1Millivolts(void);
static uint16_t Board_GetCC2Millivolts(void);
static uint16_t Board_GetBatteryMillivolts(void);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static SPI_BusTypeDef Board_LCDSPIBus =
{
    .MosiPin = PC3,
    .MisoPin = SPI_PIN_UNUSED,
    .SclkPin = PB13,
    .Port = SPI_PORT_2
};

static SPI_DeviceTypeDef Board_LCDSPIDevice =
{
    .Bus = &Board_LCDSPIBus,
    .ChipSelectPin = PA10,
    .ChipSelectPolarity = SPI_CHIP_SELECT_ACTIVE_LOW,
    .FrequencyHz = 1000000U,
    .TimeoutMilliseconds = 1000U,
    .Mode = SPI_MODE_0,
    .BitOrder = SPI_BIT_ORDER_MSB_FIRST,
    .FrameSize = SPI_FRAME_SIZE_9_BIT
};

static const GPIO_PinTypeDef Board_PowerEnablePin =
{
    .Pin = PC13
};

static const GPIO_ConfigTypeDef Board_PowerEnableConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_LOW
};

static const GPIO_PinTypeDef Board_ChargerCurrentLimitPin =
{
    .Pin = PC15
};

static const GPIO_ConfigTypeDef Board_ChargerCurrentLimitConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef Board_ChargerStatusPin =
{
    .Pin = PC14
};

static const GPIO_ConfigTypeDef Board_ChargerStatusConfig =
{
    .Mode = GPIO_MODE_INPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_UP,
    .InitialLevel = GPIO_LEVEL_LOW
};

static Timer_HandleTypeDef Board_ChargeLEDTimer =
{
    .Timer = TIM4_CH2,
    .FrequencyHz = 1000U,
    .UpdateCallback = Power_TimerUpdate,
    .CallbackContext = NULL
};

static Timer_PWMChannelTypeDef Board_ChargeLEDChannel =
{
    .Timer = &Board_ChargeLEDTimer,
    .Output = TIM4_CH2,
    .Pin = PB7,
    .Polarity = TIMER_PWM_POLARITY_ACTIVE_HIGH,
    .DutyPermille = 500U
};

static const GPIO_PinTypeDef Board_LCDResetPin =
{
    .Pin = PB12
};

static const GPIO_ConfigTypeDef Board_LCDResetConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef Board_LCDBacklightPin =
{
    .Pin = PB4
};

static const GPIO_ConfigTypeDef Board_LCDBacklightConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_LOW
};

static const GPIO_PinTypeDef Board_RedLEDPin =
{
    .Pin = PB6
};

static const GPIO_ConfigTypeDef Board_LEDConfig =
{
    .Mode = GPIO_MODE_OUTPUT,
    .OutputType = GPIO_OUTPUT_PUSH_PULL,
    .Pull = GPIO_PULL_NONE,
    .InitialLevel = GPIO_LEVEL_HIGH
};

static const GPIO_PinTypeDef Board_PrimaryButtonPin =
{
    .Pin = PC12
};

static const GPIO_PinTypeDef Board_SecondaryButtonPin =
{
    .Pin = PB2
};

/*
 * Both buttons drive the input high when pressed. The GPIO contract ignores
 * OutputType and InitialLevel while a pin is configured as an input.
 */
static const GPIO_ConfigTypeDef Board_ButtonInputConfig =
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
static const ADC_InputTypeDef Board_ADCInputs[ADC_INPUT_COUNT] =
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

static ADC_ValueTypeDef Board_ADCValues[ADC_INPUT_COUNT];

static Power_HandleTypeDef Board_PowerHandle =
{
    .ChargerCurrentLimitPin = &Board_ChargerCurrentLimitPin,
    .ChargerStatusPin = &Board_ChargerStatusPin,
    .ChargeLEDChannel = &Board_ChargeLEDChannel,
    .GetCC1Millivolts = Board_GetCC1Millivolts,
    .GetCC2Millivolts = Board_GetCC2Millivolts,
    .GetBatteryMillivolts = Board_GetBatteryMillivolts
};

static Board_WakeReasonTypeDef Board_WakeReason;

static DisplayController_HandleTypeDef Board_LCDDisplayController =
{
    .Pins =
    {
        .HorizontalSyncPin = PC6,
        .VerticalSyncPin = PA7,
        .DataEnablePin = PC5,
        .PixelClockPin = PB14,
        .RedPins =
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
        .GreenPins =
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
        .BluePins =
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
    .Timing =
    {
        .ActiveWidth = W430WVC004_A_WIDTH,
        .ActiveHeight = W430WVC004_A_HEIGHT,
        .HorizontalSyncWidth = LCD_HORIZONTAL_SYNC_WIDTH,
        .HorizontalBackPorch = LCD_HORIZONTAL_BACK_PORCH,
        .HorizontalFrontPorch = LCD_HORIZONTAL_FRONT_PORCH,
        .VerticalSyncHeight = LCD_VERTICAL_SYNC_HEIGHT,
        .VerticalBackPorch = LCD_VERTICAL_BACK_PORCH,
        .VerticalFrontPorch = LCD_VERTICAL_FRONT_PORCH,
        .RefreshRateMilliHz = LCD_REFRESH_RATE_MILLIHZ
    },
    .Signals =
    {
        .HorizontalSync = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .VerticalSync = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .DataEnable = DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW,
        .PixelClockEdge = DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE
    },
    .BackgroundColour =
    {
        .Red = 0U,
        .Green = 0U,
        .Blue = 0U
    }
};

static ST7701S_HandleTypeDef Board_LCDController =
{
    .SPI = &Board_LCDSPIDevice,
    .SetReset = Board_SetLCDReset,
    .DelayMilliseconds = Delay_Milliseconds
};

static W430WVC004_A_HandleTypeDef Board_LCDPanel =
{
    .Controller = &Board_LCDController
};

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Board_Init(void)
{
    Board_InitTarget();
    Board_InitCriticalInterfaces();
    Board_WakeReason = Board_DetectWakeReason();

    if(Board_WakeReason == BOARD_WAKE_REASON_EXTERNAL_POWER)
    {
        while(GPIO_IsLow(&Board_PrimaryButtonPin))
        {
        }
    }

    if(GPIO_Set(&Board_PowerEnablePin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    Board_InitInterfaces();
    Board_InitDevices();
}

Board_WakeReasonTypeDef Board_GetWakeReason(void)
{
    return Board_WakeReason;
}

DisplayController_HandleTypeDef *Board_GetDisplayController(void)
{
    return &Board_LCDDisplayController;
}

const ADC_InputTypeDef *Board_GetPOTAInput(void)
{
    return &Board_ADCInputs[POT_A_INPUT_INDEX];
}

const ADC_InputTypeDef *Board_GetPOTBInput(void)
{
    return &Board_ADCInputs[POT_B_INPUT_INDEX];
}

const GPIO_PinTypeDef *Board_GetPrimaryButtonInput(void)
{
    return &Board_PrimaryButtonPin;
}

const GPIO_PinTypeDef *Board_GetSecondaryButtonInput(void)
{
    return &Board_SecondaryButtonPin;
}

void Board_PowerOff(void)
{
    GPIO_Clear(&Board_PowerEnablePin);
    GPIO_Clear(&Board_LCDBacklightPin);
    GPIO_Clear(&Board_RedLEDPin);
    while(GPIO_IsHigh(&Board_PrimaryButtonPin))
    {
    }

    Delay_Milliseconds(500U);
    Target_PowerOff();
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Board_SetLCDReset(bool Asserted)
{
    GPIO_LevelTypeDef Level;

    Level = Asserted ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH;

    if(GPIO_Write(&Board_LCDResetPin, Level) != GPIO_RESULT_OK)
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

    if(USBD_Init(NULL) != USBD_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Time_Init() != TIME_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static void Board_InitCriticalInterfaces(void)
{
    if(GPIO_Init(&Board_PowerEnablePin, &Board_PowerEnableConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&Board_PrimaryButtonPin, &Board_ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(ADC_Init(Board_ADCInputs, Board_ADCValues, ADC_INPUT_COUNT) != ADC_RESULT_OK)
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
    if(GPIO_Init(&Board_ChargerCurrentLimitPin, &Board_ChargerCurrentLimitConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&Board_ChargerStatusPin, &Board_ChargerStatusConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Init(&Board_ChargeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_PWMChannelInit(&Board_ChargeLEDChannel) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Power_Init(&Board_PowerHandle) != POWER_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(Timer_Start(&Board_ChargeLEDTimer) != TIMER_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static Board_WakeReasonTypeDef Board_DetectWakeReason(void)
{
    return !GPIO_IsHigh(&Board_PrimaryButtonPin) ? BOARD_WAKE_REASON_EXTERNAL_POWER : BOARD_WAKE_REASON_POWER_BUTTON;
}

static void Board_InitInterfaces(void)
{
    if(GPIO_Init(&Board_RedLEDPin, &Board_LEDConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&Board_LCDResetPin, &Board_LCDResetConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&Board_LCDBacklightPin, &Board_LCDBacklightConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Init(&Board_SecondaryButtonPin, &Board_ButtonInputConfig) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(SPI_BusInit(&Board_LCDSPIBus) != SPI_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(SPI_DeviceInit(&Board_LCDSPIDevice) != SPI_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(DisplayController_Init(&Board_LCDDisplayController) != DISPLAY_CONTROLLER_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static void Board_InitDevices(void)
{
    if(W430WVC004_A_Init(&Board_LCDPanel) != W430WVC004_A_RESULT_OK)
    {
        Board_InitFailure();
    }

    if(GPIO_Set(&Board_LCDBacklightPin) != GPIO_RESULT_OK)
    {
        Board_InitFailure();
    }
}

static uint16_t Board_GetADCInputMillivolts(const ADC_InputTypeDef *Input)
{
    ADC_ValueTypeDef ReferenceValue;
    ADC_ValueTypeDef InputValue;
    uint16_t ReferenceCalibration;
    uint32_t SupplyMillivolts;

    ReferenceValue = ADC_GetValue(&Board_ADCInputs[VREFINT_INPUT_INDEX]);

    if(ReferenceValue == 0U)
    {
        return 0U;
    }

    ReferenceCalibration = *((const uint16_t *)VREFINT_CALIBRATION_ADDRESS);
    SupplyMillivolts = ((uint32_t)VREFINT_CALIBRATION_MILLIVOLTS * ReferenceCalibration) / ReferenceValue;
    InputValue = ADC_GetValue(Input);

    return (uint16_t)(((uint32_t)InputValue * SupplyMillivolts) / ADC_FULL_SCALE_VALUE);
}

static uint16_t Board_GetCC1Millivolts(void)
{
    return Board_GetADCInputMillivolts(&Board_ADCInputs[USB_CC1_INPUT_INDEX]);
}

static uint16_t Board_GetCC2Millivolts(void)
{
    return Board_GetADCInputMillivolts(&Board_ADCInputs[USB_CC2_INPUT_INDEX]);
}

static uint16_t Board_GetBatteryMillivolts(void)
{
    return (uint16_t)(Board_GetADCInputMillivolts(&Board_ADCInputs[BATTERY_VOLTAGE_INPUT_INDEX]) * BATTERY_VOLTAGE_DIVIDER_RATIO);
}
