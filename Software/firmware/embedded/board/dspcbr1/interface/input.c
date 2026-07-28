/**
 * @file input.c
 * @brief Embedded implementation of the generic input-control contract.
 *
 * This backend reads the two board-mounted analog sliders through the
 * board-assigned ADC inputs. Button and battery inputs remain fixed until
 * their board drivers are connected.
 */

#include "input.h"

#include "adc.h"
#include "board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Input identifiers                                                          */
/* -------------------------------------------------------------------------- */

#define EMBEDDED_INPUT_LEFT_SLIDER_NUMBER       ((Input_NumberTypeDef)1U)
#define EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER      ((Input_NumberTypeDef)2U)
#define EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER    ((Input_NumberTypeDef)3U)
#define EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER  ((Input_NumberTypeDef)4U)
#define EMBEDDED_INPUT_BATTERY_NUMBER           ((Input_NumberTypeDef)5U)

/* -------------------------------------------------------------------------- */
/* Input ranges and default values                                             */
/* -------------------------------------------------------------------------- */

#define EMBEDDED_INPUT_SLIDER_MINIMUM           (0)
#define EMBEDDED_INPUT_SLIDER_MAXIMUM           (65535)

#define EMBEDDED_INPUT_BUTTON_RELEASED           (0)
#define EMBEDDED_INPUT_BUTTON_PRESSED            (1)

#define EMBEDDED_INPUT_BATTERY_MINIMUM           (0)
#define EMBEDDED_INPUT_BATTERY_MAXIMUM           (100)
#define EMBEDDED_INPUT_BATTERY_DEFAULT           (100)

/* -------------------------------------------------------------------------- */
/* Private state                                                              */
/* -------------------------------------------------------------------------- */

static const ADC_InputTypeDef *Embedded_POTAInput;
static const ADC_InputTypeDef *Embedded_POTBInput;
static bool Embedded_InputInitialised;

/* -------------------------------------------------------------------------- */
/* Input descriptions                                                         */
/* -------------------------------------------------------------------------- */

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
        .Minimum = EMBEDDED_INPUT_BUTTON_RELEASED,
        .Maximum = EMBEDDED_INPUT_BUTTON_PRESSED,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER,
        .Minimum = EMBEDDED_INPUT_BUTTON_RELEASED,
        .Maximum = EMBEDDED_INPUT_BUTTON_PRESSED,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = EMBEDDED_INPUT_BATTERY_NUMBER,
        .Minimum = EMBEDDED_INPUT_BATTERY_MINIMUM,
        .Maximum = EMBEDDED_INPUT_BATTERY_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    }
};

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

    if((Embedded_POTAInput == NULL) || (Embedded_POTBInput == NULL))
    {
        return false;
    }

    if(!ADC_IsAssigned(Embedded_POTAInput) || !ADC_IsAssigned(Embedded_POTBInput))
    {
        return false;
    }

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

            *Value = (int32_t)ADCValue;
            return true;

        case EMBEDDED_INPUT_RIGHT_SLIDER_NUMBER:
            if(ADC_Read(Embedded_POTBInput, &ADCValue) != ADC_RESULT_OK)
            {
                return false;
            }

            *Value = (int32_t)ADCValue;
            return true;

        case EMBEDDED_INPUT_PRIMARY_BUTTON_NUMBER:
        case EMBEDDED_INPUT_SECONDARY_BUTTON_NUMBER:
            *Value = EMBEDDED_INPUT_BUTTON_RELEASED;
            return true;

        case EMBEDDED_INPUT_BATTERY_NUMBER:
            *Value = EMBEDDED_INPUT_BATTERY_DEFAULT;
            return true;

        default:
            return false;
    }
}