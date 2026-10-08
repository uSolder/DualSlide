/**
 * @file input.c
 * @brief Embedded implementation of the generic input-control contract.
 *
 * This backend reads the board-mounted analog sliders through the assigned
 * ADC inputs, board GPIO inputs, and battery state through the power driver.
 */

#include "input.h"

#include "adc.h"
#include "board.h"
#include "gpio.h"
#include "power.h"
#include "usbd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define EMBEDDED_INPUT_LEFT_SLIDER_NUMBER       ((Input_NumberTypeDef)1U)
#define EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER      ((Input_NumberTypeDef)2U)
#define EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER    ((Input_NumberTypeDef)3U)
#define EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER  ((Input_NumberTypeDef)4U)
#define EMBEDDED_INPUT_BATTERY_NUMBER           ((Input_NumberTypeDef)5U)
#define EMBEDDED_INPUT_USB_POWER_NUMBER         ((Input_NumberTypeDef)6U)
#define EMBEDDED_INPUT_BATTERY_DEPLETED_NUMBER  ((Input_NumberTypeDef)7U)

#define EMBEDDED_INPUT_SLIDER_MINIMUM                (0)
#define EMBEDDED_INPUT_SLIDER_MAXIMUM                (65535)
#define EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS (8U)
#define EMBEDDED_INPUT_SLIDER_FILTER_SLOW_SHIFT      (2U)
#define EMBEDDED_INPUT_SLIDER_FILTER_FAST_SHIFT      (1U)
#define EMBEDDED_INPUT_SLIDER_FILTER_FAST_THRESHOLD  (768U)

#define EMBEDDED_INPUT_DIGITAL_LOW               (0)
#define EMBEDDED_INPUT_DIGITAL_HIGH              (1)

#define EMBEDDED_INPUT_BATTERY_MINIMUM_MILLIVOLTS    (0)
#define EMBEDDED_INPUT_BATTERY_MAXIMUM_MILLIVOLTS    (5000)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const ADC_InputTypeDef *Embedded_POTAInput;
static const ADC_InputTypeDef *Embedded_POTBInput;
static const GPIO_PinTypeDef *Embedded_PrimaryButtonInput;
static const GPIO_PinTypeDef *Embedded_SecondaryButtonInput;
static int32_t Embedded_LeftSliderFilteredValue;
static int32_t Embedded_RightSliderFilteredValue;
static bool Embedded_LeftSliderFilterInitialised;
static bool Embedded_RightSliderFilterInitialised;
static bool Embedded_InputInitialised;

static const Input_InfoTypeDef Embedded_InputInfo[] =
{
    {
        .Number = EMBEDDED_INPUT_LEFT_SLIDER_NUMBER,
        .Minimum = EMBEDDED_INPUT_SLIDER_MINIMUM,
        .Maximum = EMBEDDED_INPUT_SLIDER_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER,
        .Minimum = EMBEDDED_INPUT_SLIDER_MINIMUM,
        .Maximum = EMBEDDED_INPUT_SLIDER_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER,
        .Minimum = EMBEDDED_INPUT_DIGITAL_LOW,
        .Maximum = EMBEDDED_INPUT_DIGITAL_HIGH,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER,
        .Minimum = EMBEDDED_INPUT_DIGITAL_LOW,
        .Maximum = EMBEDDED_INPUT_DIGITAL_HIGH,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = EMBEDDED_INPUT_BATTERY_NUMBER,
        .Minimum = EMBEDDED_INPUT_BATTERY_MINIMUM_MILLIVOLTS,
        .Maximum = EMBEDDED_INPUT_BATTERY_MAXIMUM_MILLIVOLTS,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = EMBEDDED_INPUT_USB_POWER_NUMBER,
        .Minimum = EMBEDDED_INPUT_DIGITAL_LOW,
        .Maximum = EMBEDDED_INPUT_DIGITAL_HIGH,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = EMBEDDED_INPUT_BATTERY_DEPLETED_NUMBER,
        .Minimum = EMBEDDED_INPUT_DIGITAL_LOW,
        .Maximum = EMBEDDED_INPUT_DIGITAL_HIGH,
        .Type = INPUT_TYPE_DIGITAL
    }
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static int32_t Embedded_FilterSlider(ADC_ValueTypeDef RawValue, int32_t *FilteredValue, bool *FilterInitialised);

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Input_Init(void)
{
    if(Embedded_InputInitialised)
    {
        return true;
    }

    Embedded_POTAInput = Board_GetPOTAInput();
    Embedded_POTBInput = Board_GetPOTBInput();
    Embedded_PrimaryButtonInput = Board_GetPrimaryButtonInput();
    Embedded_SecondaryButtonInput = Board_GetSecondaryButtonInput();

    if((Embedded_POTAInput == NULL) ||
       (Embedded_POTBInput == NULL) ||
       (Embedded_PrimaryButtonInput == NULL) ||
       (Embedded_SecondaryButtonInput == NULL))
    {
        return false;
    }

    if(!ADC_IsAssigned(Embedded_POTAInput) ||
       !ADC_IsAssigned(Embedded_POTBInput) ||
       !GPIO_IsAssigned(Embedded_PrimaryButtonInput) ||
       !GPIO_IsAssigned(Embedded_SecondaryButtonInput))
    {
        return false;
    }

    Embedded_LeftSliderFilterInitialised = false;
    Embedded_RightSliderFilterInitialised = false;
    Embedded_InputInitialised = true;

    return true;
}

uint8_t Input_GetCount(void)
{
    return (uint8_t)(sizeof(Embedded_InputInfo) / sizeof(Embedded_InputInfo[0]));
}

bool Input_GetInfo(uint8_t Index, Input_InfoTypeDef *Info)
{
    if((Info == NULL) || (Index >= Input_GetCount()))
    {
        return false;
    }

    *Info = Embedded_InputInfo[Index];

    return true;
}

bool Input_GetValue(Input_NumberTypeDef Number, int32_t *Value)
{
    ADC_ValueTypeDef ADCValue;
    GPIO_LevelTypeDef GPIOLevel;

    if(!Embedded_InputInitialised || (Value == NULL))
    {
        return false;
    }

    switch(Number)
    {
        case EMBEDDED_INPUT_LEFT_SLIDER_NUMBER:
            if(ADC_Read(Embedded_POTAInput, &ADCValue) != ADC_RESULT_OK)
            {
                return false;
            }

            *Value = Embedded_FilterSlider(ADCValue, &Embedded_LeftSliderFilteredValue, &Embedded_LeftSliderFilterInitialised);
            return true;

        case EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER:
            if(ADC_Read(Embedded_POTBInput, &ADCValue) != ADC_RESULT_OK)
            {
                return false;
            }

            *Value = Embedded_FilterSlider(ADCValue, &Embedded_RightSliderFilteredValue, &Embedded_RightSliderFilterInitialised);
            return true;

        case EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER:
            if(GPIO_Read(Embedded_PrimaryButtonInput, &GPIOLevel) != GPIO_RESULT_OK)
            {
                return false;
            }

            *Value = (GPIOLevel == GPIO_LEVEL_HIGH) ? EMBEDDED_INPUT_DIGITAL_HIGH : EMBEDDED_INPUT_DIGITAL_LOW;
            return true;

        case EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER:
            if(GPIO_Read(Embedded_SecondaryButtonInput, &GPIOLevel) != GPIO_RESULT_OK)
            {
                return false;
            }

            *Value = (GPIOLevel == GPIO_LEVEL_HIGH) ? EMBEDDED_INPUT_DIGITAL_HIGH : EMBEDDED_INPUT_DIGITAL_LOW;
            return true;

        case EMBEDDED_INPUT_BATTERY_NUMBER:
            *Value = (int32_t)Power_GetBatteryVoltageMillivolts();
            return true;

        case EMBEDDED_INPUT_USB_POWER_NUMBER:
            *Value = USBD_IsVbusPresent() ? EMBEDDED_INPUT_DIGITAL_HIGH : EMBEDDED_INPUT_DIGITAL_LOW;
            return true;

        case EMBEDDED_INPUT_BATTERY_DEPLETED_NUMBER:
            *Value = Power_IsBatteryDepleted() ? EMBEDDED_INPUT_DIGITAL_HIGH : EMBEDDED_INPUT_DIGITAL_LOW;
            return true;

        default:
            return false;
    }
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static int32_t Embedded_FilterSlider(ADC_ValueTypeDef RawValue, int32_t *FilteredValue, bool *FilterInitialised)
{
    int32_t TargetValue;
    int32_t Difference;
    uint8_t FilterShift;

    TargetValue = (int32_t)RawValue << EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS;

    if(!*FilterInitialised)
    {
        *FilteredValue = TargetValue;
        *FilterInitialised = true;
    }
    else
    {
        Difference = TargetValue - *FilteredValue;
        FilterShift = ((Difference >= 0) ? Difference : -Difference) > ((int32_t)EMBEDDED_INPUT_SLIDER_FILTER_FAST_THRESHOLD << EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS) ? EMBEDDED_INPUT_SLIDER_FILTER_FAST_SHIFT : EMBEDDED_INPUT_SLIDER_FILTER_SLOW_SHIFT;
        *FilteredValue += Difference / (int32_t)(1UL << FilterShift);
    }

    return (*FilteredValue + (1L << (EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS - 1U))) >> EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS;
}
