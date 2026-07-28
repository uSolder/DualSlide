/**
 * @file adc.c
 * @brief STM32H7A3 interrupt-driven ADC implementation.
 *
 * This implementation samples a fixed array of board-assigned analog inputs.
 * Each completed conversion is stored in the caller-provided value array, then
 * the next input is selected and conversion begins again.
 */

#include "adc.h"

#include "STM32H7A3_Defs.h"
#include "delay.h"
#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define ADC_MAX_INPUT_COUNT             (16U)
#define ADC_INTERRUPT_PRIORITY          (6U)
#define ADC_REGULATOR_STARTUP_DELAY_US  (100U)
#define ADC_CALIBRATION_TIMEOUT_US      (100000U)
#define ADC_ENABLE_TIMEOUT_US           (10000U)
#define ADC_DISABLE_TIMEOUT_US          (10000U)

#define ADC_GPIO_MODE_ANALOG            (3UL)
#define ADC_SAMPLE_TIME                 (7UL)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    GPIO_TypeDef *GPIO;
    uint8_t GPIOPin;
    uint8_t Channel;
} ADC_PinMappingTypeDef;

typedef struct
{
    const ADC_InputTypeDef *Inputs;
    ADC_ValueTypeDef *Values;
    ADC_PinMappingTypeDef Mappings[ADC_MAX_INPUT_COUNT];
    uint8_t InputCount;
    volatile uint8_t CurrentInput;
    bool Initialized;
    bool Running;
} ADC_StateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static ADC_StateTypeDef ADC_State;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static ADC_ResultTypeDef ADC_MapPin(ADC_PinIdTypeDef Pin, ADC_PinMappingTypeDef *Mapping);
static ADC_ResultTypeDef ADC_ConfigureGPIO(const ADC_PinMappingTypeDef *Mapping);
static ADC_ResultTypeDef ADC_ConfigurePeripheral(void);
static ADC_ResultTypeDef ADC_Calibrate(void);
static ADC_ResultTypeDef ADC_EnablePeripheral(void);
static ADC_ResultTypeDef ADC_DisablePeripheral(void);
static void ADC_SelectInput(uint8_t InputIndex);
static int32_t ADC_FindInputIndex(const ADC_InputTypeDef *Input);
static void ADC_ResetState(void);

/* -------------------------------------------------------------------------- */
/* Pin mapping                                                                */
/* -------------------------------------------------------------------------- */

static ADC_ResultTypeDef ADC_MapPin(ADC_PinIdTypeDef Pin, ADC_PinMappingTypeDef *Mapping)
{
    if(Mapping == NULL)
    {
        return ADC_RESULT_INVALID_ARGUMENT;
    }

    switch(Pin)
    {
        case PC0:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 0U;
            Mapping->Channel = 10U;
            return ADC_RESULT_OK;

        case PC1:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 1U;
            Mapping->Channel = 11U;
            return ADC_RESULT_OK;

        case PC2:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 2U;
            Mapping->Channel = 12U;
            return ADC_RESULT_OK;

        case PC3:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 3U;
            Mapping->Channel = 13U;
            return ADC_RESULT_OK;

        case PA0:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 0U;
            Mapping->Channel = 16U;
            return ADC_RESULT_OK;

        case PA1:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 1U;
            Mapping->Channel = 17U;
            return ADC_RESULT_OK;

        case PA2:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 2U;
            Mapping->Channel = 14U;
            return ADC_RESULT_OK;

        case PA3:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 3U;
            Mapping->Channel = 15U;
            return ADC_RESULT_OK;

        case PA4:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 4U;
            Mapping->Channel = 18U;
            return ADC_RESULT_OK;

        case PA5:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 5U;
            Mapping->Channel = 19U;
            return ADC_RESULT_OK;

        case PA6:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 6U;
            Mapping->Channel = 3U;
            return ADC_RESULT_OK;

        case PA7:
            Mapping->GPIO = GPIOA;
            Mapping->GPIOPin = 7U;
            Mapping->Channel = 7U;
            return ADC_RESULT_OK;

        case PC4:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 4U;
            Mapping->Channel = 4U;
            return ADC_RESULT_OK;

        case PC5:
            Mapping->GPIO = GPIOC;
            Mapping->GPIOPin = 5U;
            Mapping->Channel = 8U;
            return ADC_RESULT_OK;

        case PB0:
            Mapping->GPIO = GPIOB;
            Mapping->GPIOPin = 0U;
            Mapping->Channel = 9U;
            return ADC_RESULT_OK;

        case PB1:
            Mapping->GPIO = GPIOB;
            Mapping->GPIOPin = 1U;
            Mapping->Channel = 5U;
            return ADC_RESULT_OK;

        default:
            return ADC_RESULT_INVALID_PIN;
    }
}

/* -------------------------------------------------------------------------- */
/* GPIO configuration                                                         */
/* -------------------------------------------------------------------------- */

static ADC_ResultTypeDef ADC_ConfigureGPIO(const ADC_PinMappingTypeDef *Mapping)
{
    uint32_t Position;

    if((Mapping == NULL) || (Mapping->GPIO == NULL) || (Mapping->GPIOPin > 15U))
    {
        return ADC_RESULT_INVALID_ARGUMENT;
    }

    if(RCC_EnablePeripheralClock(Mapping->GPIO) != RCC_RESULT_OK)
    {
        return ADC_RESULT_HARDWARE_ERROR;
    }

    Position = (uint32_t)Mapping->GPIOPin * 2U;

    Mapping->GPIO->MODER &= ~(0x3UL << Position);
    Mapping->GPIO->MODER |= ADC_GPIO_MODE_ANALOG << Position;
    Mapping->GPIO->PUPDR &= ~(0x3UL << Position);

    return ADC_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Peripheral configuration                                                   */
/* -------------------------------------------------------------------------- */

static ADC_ResultTypeDef ADC_Calibrate(void)
{
    uint32_t TimeoutMicroseconds = ADC_CALIBRATION_TIMEOUT_US;

    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCALLIN;
    ADC1->CR |= ADC_CR_ADCAL;

    while((ADC1->CR & ADC_CR_ADCAL) != 0U)
    {
        if(TimeoutMicroseconds == 0U)
        {
            return ADC_RESULT_HARDWARE_ERROR;
        }

        Delay_us(1U);
        TimeoutMicroseconds--;
    }

    return ADC_RESULT_OK;
}

static ADC_ResultTypeDef ADC_EnablePeripheral(void)
{
    uint32_t TimeoutMicroseconds = ADC_ENABLE_TIMEOUT_US;

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;

    while((ADC1->ISR & ADC_ISR_ADRDY) == 0U)
    {
        if(TimeoutMicroseconds == 0U)
        {
            return ADC_RESULT_HARDWARE_ERROR;
        }

        Delay_us(1U);
        TimeoutMicroseconds--;
    }

    ADC1->ISR = ADC_ISR_ADRDY;

    return ADC_RESULT_OK;
}

static ADC_ResultTypeDef ADC_DisablePeripheral(void)
{
    uint32_t TimeoutMicroseconds = ADC_DISABLE_TIMEOUT_US;

    if((ADC1->CR & ADC_CR_ADEN) == 0U)
    {
        return ADC_RESULT_OK;
    }

    if((ADC1->CR & ADC_CR_ADSTART) != 0U)
    {
        ADC1->CR |= ADC_CR_ADSTP;

        while((ADC1->CR & ADC_CR_ADSTP) != 0U)
        {
            if(TimeoutMicroseconds == 0U)
            {
                return ADC_RESULT_HARDWARE_ERROR;
            }

            Delay_us(1U);
            TimeoutMicroseconds--;
        }
    }

    TimeoutMicroseconds = ADC_DISABLE_TIMEOUT_US;
    ADC1->CR |= ADC_CR_ADDIS;

    while((ADC1->CR & ADC_CR_ADEN) != 0U)
    {
        if(TimeoutMicroseconds == 0U)
        {
            return ADC_RESULT_HARDWARE_ERROR;
        }

        Delay_us(1U);
        TimeoutMicroseconds--;
    }

    return ADC_RESULT_OK;
}

static ADC_ResultTypeDef ADC_ConfigurePeripheral(void)
{
    ADC_ResultTypeDef Result;

    if(RCC_EnablePeripheralClock(ADC1) != RCC_RESULT_OK)
    {
        return ADC_RESULT_HARDWARE_ERROR;
    }

    if(RCC_ResetPeripheral(ADC1) != RCC_RESULT_OK)
    {
        return ADC_RESULT_HARDWARE_ERROR;
    }

    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;

    Delay_us(ADC_REGULATOR_STARTUP_DELAY_US);

    ADC12_COMMON->CCR &= ~(ADC_CCR_CKMODE | ADC_CCR_PRESC);

    ADC1->CFGR = 0U;
    ADC1->CFGR2 = 0U;
    ADC1->CFGR &= ~ADC_CFGR_RES;
    ADC1->CFGR |= ADC_CFGR_OVRMOD;

    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = 0U;
    ADC1->SQR4 = 0U;
    ADC1->SMPR1 = 0U;
    ADC1->SMPR2 = 0U;

    for(uint8_t Channel = 0U; Channel <= 9U; Channel++)
    {
        ADC1->SMPR1 |= ADC_SAMPLE_TIME << ((uint32_t)Channel * 3U);
    }

    for(uint8_t Channel = 10U; Channel <= 19U; Channel++)
    {
        ADC1->SMPR2 |= ADC_SAMPLE_TIME << (((uint32_t)Channel - 10U) * 3U);
    }

    ADC1->PCSEL = 0U;

    for(uint8_t InputIndex = 0U; InputIndex < ADC_State.InputCount; InputIndex++)
    {
        ADC1->PCSEL |= 1UL << ADC_State.Mappings[InputIndex].Channel;
    }

    Result = ADC_Calibrate();

    if(Result != ADC_RESULT_OK)
    {
        return Result;
    }

    Result = ADC_EnablePeripheral();

    if(Result != ADC_RESULT_OK)
    {
        return Result;
    }

    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    ADC1->IER = ADC_IER_EOCIE | ADC_IER_OVRIE;

    NVIC_ClearPendingIRQ(ADC_IRQn);
    NVIC_SetPriority(ADC_IRQn, ADC_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(ADC_IRQn);

    return ADC_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Conversion control                                                         */
/* -------------------------------------------------------------------------- */

static void ADC_SelectInput(uint8_t InputIndex)
{
    uint32_t Channel = ADC_State.Mappings[InputIndex].Channel;

    ADC1->SQR1 = Channel << ADC_SQR1_SQ1_Pos;
}

/* -------------------------------------------------------------------------- */
/* Input lookup                                                               */
/* -------------------------------------------------------------------------- */

static int32_t ADC_FindInputIndex(const ADC_InputTypeDef *Input)
{
    if(Input == NULL)
    {
        return -1;
    }

    for(uint8_t InputIndex = 0U; InputIndex < ADC_State.InputCount; InputIndex++)
    {
        if(ADC_State.Inputs[InputIndex].Pin == Input->Pin)
        {
            return (int32_t)InputIndex;
        }
    }

    return -1;
}

/* -------------------------------------------------------------------------- */
/* State management                                                           */
/* -------------------------------------------------------------------------- */

static void ADC_ResetState(void)
{
    ADC_State.Inputs = NULL;
    ADC_State.Values = NULL;
    ADC_State.InputCount = 0U;
    ADC_State.CurrentInput = 0U;
    ADC_State.Initialized = false;
    ADC_State.Running = false;

    for(uint8_t InputIndex = 0U; InputIndex < ADC_MAX_INPUT_COUNT; InputIndex++)
    {
        ADC_State.Mappings[InputIndex].GPIO = NULL;
        ADC_State.Mappings[InputIndex].GPIOPin = 0U;
        ADC_State.Mappings[InputIndex].Channel = 0U;
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

ADC_ResultTypeDef ADC_Init(const ADC_InputTypeDef *Inputs, ADC_ValueTypeDef *Values, uint8_t InputCount)
{
    ADC_ResultTypeDef Result;

    if((Inputs == NULL) || (Values == NULL) || (InputCount == 0U) || (InputCount > ADC_MAX_INPUT_COUNT))
    {
        return ADC_RESULT_INVALID_ARGUMENT;
    }

    if(ADC_State.Initialized)
    {
        return ADC_RESULT_ALREADY_INITIALIZED;
    }

    ADC_ResetState();

    ADC_State.Inputs = Inputs;
    ADC_State.Values = Values;
    ADC_State.InputCount = InputCount;

    for(uint8_t InputIndex = 0U; InputIndex < InputCount; InputIndex++)
    {
        if(!ADC_IsAssigned(&Inputs[InputIndex]))
        {
            ADC_ResetState();
            return ADC_RESULT_INVALID_PIN;
        }

        Result = ADC_MapPin(Inputs[InputIndex].Pin, &ADC_State.Mappings[InputIndex]);

        if(Result != ADC_RESULT_OK)
        {
            ADC_ResetState();
            return Result;
        }

        Result = ADC_ConfigureGPIO(&ADC_State.Mappings[InputIndex]);

        if(Result != ADC_RESULT_OK)
        {
            ADC_ResetState();
            return Result;
        }

        Values[InputIndex] = 0U;
    }

    Result = ADC_ConfigurePeripheral();

    if(Result != ADC_RESULT_OK)
    {
        ADC_ResetState();
        return Result;
    }

    ADC_State.CurrentInput = 0U;
    ADC_State.Initialized = true;

    return ADC_RESULT_OK;
}

ADC_ResultTypeDef ADC_Start(void)
{
    if(!ADC_State.Initialized)
    {
        return ADC_RESULT_NOT_INITIALIZED;
    }

    if(ADC_State.Running)
    {
        return ADC_RESULT_OK;
    }

    ADC_State.CurrentInput = 0U;
    ADC_State.Running = true;

    ADC_SelectInput(ADC_State.CurrentInput);

    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    ADC1->CR |= ADC_CR_ADSTART;

    return ADC_RESULT_OK;
}

ADC_ResultTypeDef ADC_Stop(void)
{
    ADC_ResultTypeDef Result;

    if(!ADC_State.Initialized)
    {
        return ADC_RESULT_NOT_INITIALIZED;
    }

    ADC_State.Running = false;

    NVIC_DisableIRQ(ADC_IRQn);
    NVIC_ClearPendingIRQ(ADC_IRQn);

    ADC1->IER = 0U;
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;

    Result = ADC_DisablePeripheral();

    if(Result != ADC_RESULT_OK)
    {
        return Result;
    }

    ADC_ResetState();

    return ADC_RESULT_OK;
}

ADC_ResultTypeDef ADC_Read(const ADC_InputTypeDef *Input, ADC_ValueTypeDef *Value)
{
    int32_t InputIndex;

    if((Input == NULL) || (Value == NULL))
    {
        return ADC_RESULT_INVALID_ARGUMENT;
    }

    if(!ADC_State.Initialized)
    {
        return ADC_RESULT_NOT_INITIALIZED;
    }

    InputIndex = ADC_FindInputIndex(Input);

    if(InputIndex < 0)
    {
        return ADC_RESULT_INVALID_PIN;
    }

    *Value = ADC_State.Values[InputIndex];

    return ADC_RESULT_OK;
}

ADC_ValueTypeDef ADC_GetValue(const ADC_InputTypeDef *Input)
{
    ADC_ValueTypeDef Value = 0U;

    (void)ADC_Read(Input, &Value);

    return Value;
}

bool ADC_IsAssigned(const ADC_InputTypeDef *Input)
{
    return (Input != NULL) && (Input->Pin != ADC_PIN_NONE);
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

void ADC_IRQHandler(void)
{
    uint32_t InterruptStatus = ADC1->ISR;

    if((InterruptStatus & ADC_ISR_OVR) != 0U)
    {
        ADC1->ISR = ADC_ISR_OVR;
    }

    if((InterruptStatus & ADC_ISR_EOC) == 0U)
    {
        return;
    }

    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS;

    if(!ADC_State.Initialized || !ADC_State.Running)
    {
        (void)ADC1->DR;
        return;
    }

    ADC_State.Values[ADC_State.CurrentInput] = (ADC_ValueTypeDef)ADC1->DR;
    ADC_State.CurrentInput++;

    if(ADC_State.CurrentInput >= ADC_State.InputCount)
    {
        ADC_State.CurrentInput = 0U;
    }

    ADC_SelectInput(ADC_State.CurrentInput);
    ADC1->CR |= ADC_CR_ADSTART;
}