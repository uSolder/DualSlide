/**
 * @file input.c
 * @brief Embedded implementation of the generic input-control contract.
 *
 * This backend reads the board-mounted analog sliders through the assigned
 * ADC inputs, board GPIO inputs, and battery charge state through the power
 * driver.
 */

#include "input.h"

#include "adc.h"
#include "board.h"
#include "gpio.h"
#include "power.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMBEDDED_INPUT_LEFT_SLIDER_NUMBER       ((Input_NumberTypeDef)1U)
#define EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER      ((Input_NumberTypeDef)2U)
#define EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER    ((Input_NumberTypeDef)3U)
#define EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER  ((Input_NumberTypeDef)4U)
#define EMBEDDED_INPUT_BATTERY_NUMBER           ((Input_NumberTypeDef)5U)
#define EMBEDDED_INPUT_USB_POWER_NUMBER         ((Input_NumberTypeDef)6U)

#define EMBEDDED_INPUT_SLIDER_MINIMUM           (0)
#define EMBEDDED_INPUT_SLIDER_MAXIMUM           (65535)
#define EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS      (8U)
#define EMBEDDED_INPUT_SLIDER_FILTER_SLOW_SHIFT            (2U)
#define EMBEDDED_INPUT_SLIDER_FILTER_FAST_SHIFT            (1U)
#define EMBEDDED_INPUT_SLIDER_FILTER_FAST_THRESHOLD        (768U)

#define EMBEDDED_INPUT_DIGITAL_LOW               (0)
#define EMBEDDED_INPUT_DIGITAL_HIGH              (1)

#define EMBEDDED_INPUT_BATTERY_MINIMUM           (0)
#define EMBEDDED_INPUT_BATTERY_MAXIMUM           (100)
#define EMBEDDED_INPUT_BATTERY_PERMILLE_DIVISOR  (10U)

static const ADC_InputTypeDef *Embedded_POTAInput;
static const ADC_InputTypeDef *Embedded_POTBInput;
static const GPIO_PinTypeDef *Embedded_PrimaryButtonInput;
static const GPIO_PinTypeDef *Embedded_SecondaryButtonInput;
static const GPIO_PinTypeDef *Embedded_USBPowerInput;
static int32_t Embedded_LeftSliderFilteredValue;
static int32_t Embedded_RightSliderFilteredValue;
static bool Embedded_LeftSliderFilterInitialised;
static bool Embedded_RightSliderFilterInitialised;
static bool Embedded_InputInitialised;

static int32_t Embedded_FilterSlider(ADC_ValueTypeDef raw_value, int32_t *filtered_value, bool *filter_initialised);

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
        .Minimum = EMBEDDED_INPUT_BATTERY_MINIMUM,
        .Maximum = EMBEDDED_INPUT_BATTERY_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = EMBEDDED_INPUT_USB_POWER_NUMBER,
        .Minimum = EMBEDDED_INPUT_DIGITAL_LOW,
        .Maximum = EMBEDDED_INPUT_DIGITAL_HIGH,
        .Type = INPUT_TYPE_DIGITAL
    }
};

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
    Embedded_USBPowerInput = Board_GetUSBPowerInput();

    if((Embedded_POTAInput == NULL) ||
       (Embedded_POTBInput == NULL) ||
       (Embedded_PrimaryButtonInput == NULL) ||
       (Embedded_SecondaryButtonInput == NULL) ||
       (Embedded_USBPowerInput == NULL))
    {
        return false;
    }

    if(!ADC_IsAssigned(Embedded_POTAInput) ||
       !ADC_IsAssigned(Embedded_POTBInput) ||
       !GPIO_IsAssigned(Embedded_PrimaryButtonInput) ||
       !GPIO_IsAssigned(Embedded_SecondaryButtonInput) ||
       !GPIO_IsAssigned(Embedded_USBPowerInput))
    {
        return false;
    }

    Embedded_LeftSliderFilterInitialised = false;
    Embedded_RightSliderFilterInitialised = false;
    Embedded_InputInitialised = true;

    return true;
}

uint8_t Input_Get_Count(void)
{
    return (uint8_t)(sizeof(Embedded_InputInfo) / sizeof(Embedded_InputInfo[0]));
}

bool Input_Get_Info(uint8_t Index, Input_InfoTypeDef *Info)
{
    if((Info == NULL) || (Index >= Input_Get_Count()))
    {
        return false;
    }

    *Info = Embedded_InputInfo[Index];

    return true;
}

bool Input_Get_Value(Input_NumberTypeDef Number, int32_t *Value)
{
    ADC_ValueTypeDef ADCValue;
    GPIO_LevelTypeDef GPIOLevel;
    uint16_t battery_charge_percent;

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
            battery_charge_percent = Power_GetBatteryChargePermille() / EMBEDDED_INPUT_BATTERY_PERMILLE_DIVISOR;

            if(battery_charge_percent > EMBEDDED_INPUT_BATTERY_MAXIMUM)
            {
                battery_charge_percent = EMBEDDED_INPUT_BATTERY_MAXIMUM;
            }

            *Value = (int32_t)battery_charge_percent;
            return true;

        case EMBEDDED_INPUT_USB_POWER_NUMBER:
            if(GPIO_Read(Embedded_USBPowerInput, &GPIOLevel) != GPIO_RESULT_OK)
            {
                return false;
            }

            *Value = (GPIOLevel == GPIO_LEVEL_HIGH) ? EMBEDDED_INPUT_DIGITAL_HIGH : EMBEDDED_INPUT_DIGITAL_LOW;
            return true;

        default:
            return false;
    }
}

static int32_t Embedded_FilterSlider(ADC_ValueTypeDef raw_value, int32_t *filtered_value, bool *filter_initialised)
{
    int32_t target_value;
    int32_t difference;
    uint8_t filter_shift;

    target_value = (int32_t)raw_value << EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS;

    if(!*filter_initialised)
    {
        *filtered_value = target_value;
        *filter_initialised = true;
    }
    else
    {
        difference = target_value - *filtered_value;
        filter_shift = ((difference >= 0) ? difference : -difference) > ((int32_t)EMBEDDED_INPUT_SLIDER_FILTER_FAST_THRESHOLD << EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS) ? EMBEDDED_INPUT_SLIDER_FILTER_FAST_SHIFT : EMBEDDED_INPUT_SLIDER_FILTER_SLOW_SHIFT;
        *filtered_value += difference / (int32_t)(1UL << filter_shift);
    }

    return (*filtered_value + (1L << (EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS - 1U))) >> EMBEDDED_INPUT_SLIDER_FILTER_FRACTIONAL_BITS;
}