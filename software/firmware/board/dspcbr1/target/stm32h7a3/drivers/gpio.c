/**
 * @file gpio.c
 * @brief STM32H7A3 digital GPIO implementation.
 *
 * This driver implements the hardware-independent digital GPIO contract using
 * the STM32H7A3 GPIO and EXTI peripherals.
 */

#include "gpio.h"

#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define GPIO_PORT_COUNT                  11U
#define GPIO_PIN_COUNT_PER_PORT          16U

#define GPIO_PIN_NUMBER_MASK             0x0FU
#define GPIO_PORT_INDEX_SHIFT            4U
#define GPIO_PORT_INDEX_MASK             0x0FU

#define GPIO_REGISTER_FIELD_MASK         0x03UL
#define GPIO_EXTICR_FIELD_MASK           0x0FUL

#define GPIO_MODER_INPUT                 0x00UL
#define GPIO_MODER_OUTPUT                0x01UL
#define GPIO_MODER_ANALOG                0x03UL

#define GPIO_OTYPER_PUSH_PULL            0x00UL
#define GPIO_OTYPER_OPEN_DRAIN           0x01UL

#define GPIO_PUPDR_NONE                  0x00UL
#define GPIO_PUPDR_UP                    0x01UL
#define GPIO_PUPDR_DOWN                  0x02UL

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bit mask of initialized pins for each GPIO port.
 *
 * Bit n corresponds to pin n on the associated GPIO port.
 */
static uint16_t GPIO_InitializedPins[GPIO_PORT_COUNT];

/**
 * @brief Registered interrupt configuration for each STM32 EXTI line.
 *
 * STM32 EXTI line n is shared by GPIO pin n across all ports. Therefore,
 * only one GPIO interrupt can be registered for each line.
 */
static GPIO_InterruptConfigTypeDef GPIO_InterruptConfigurations[GPIO_PIN_COUNT_PER_PORT];

/**
 * @brief Registration state for each STM32 EXTI line.
 */
static bool GPIO_InterruptRegistered[GPIO_PIN_COUNT_PER_PORT];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *GPIO_GetPort(GPIO_PinIdTypeDef PinId);
static uint32_t GPIO_GetPortIndex(GPIO_PinIdTypeDef PinId);
static uint32_t GPIO_GetPinNumber(GPIO_PinIdTypeDef PinId);
static GPIO_ResultTypeDef GPIO_ValidatePin(const GPIO_PinTypeDef *Pin);
static GPIO_ResultTypeDef GPIO_ValidateConfiguration(const GPIO_ConfigTypeDef *Config);
static GPIO_ResultTypeDef GPIO_ValidateInterruptConfiguration(const GPIO_InterruptConfigTypeDef *Config);
static bool GPIO_IsInitializedInternal(const GPIO_PinTypeDef *Pin);
static bool GPIO_IsInputInternal(const GPIO_PinTypeDef *Pin);
static bool GPIO_IsOutputInternal(const GPIO_PinTypeDef *Pin);
static void GPIO_SetInitialized(const GPIO_PinTypeDef *Pin, bool Initialized);
static IRQn_Type GPIO_GetInterruptNumber(uint32_t PinNumber);

/* -------------------------------------------------------------------------- */
/* Pin decoding                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *GPIO_GetPort(GPIO_PinIdTypeDef PinId)
{
    uint32_t PortIndex;

    PortIndex = (PinId >> GPIO_PORT_INDEX_SHIFT) & GPIO_PORT_INDEX_MASK;

    switch(PortIndex)
    {
        case 0U:
            return GPIOA;

        case 1U:
            return GPIOB;

        case 2U:
            return GPIOC;

        case 3U:
            return GPIOD;

        case 4U:
            return GPIOE;

        case 5U:
            return GPIOF;

        case 6U:
            return GPIOG;

        case 7U:
            return GPIOH;

        case 8U:
            return GPIOI;

#if defined(GPIOJ)
        case 9U:
            return GPIOJ;
#endif

#if defined(GPIOK)
        case 10U:
            return GPIOK;
#endif

        default:
            return NULL;
    }
}

static uint32_t GPIO_GetPortIndex(GPIO_PinIdTypeDef PinId)
{
    return (PinId >> GPIO_PORT_INDEX_SHIFT) & GPIO_PORT_INDEX_MASK;
}

static uint32_t GPIO_GetPinNumber(GPIO_PinIdTypeDef PinId)
{
    return PinId & GPIO_PIN_NUMBER_MASK;
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static GPIO_ResultTypeDef GPIO_ValidatePin(const GPIO_PinTypeDef *Pin)
{
    uint32_t PortIndex;
    uint32_t PinNumber;

    if(Pin == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if(Pin->Pin == GPIO_PIN_NONE)
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    PortIndex = GPIO_GetPortIndex(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    if((PortIndex >= GPIO_PORT_COUNT) || (PinNumber >= GPIO_PIN_COUNT_PER_PORT))
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    if(GPIO_GetPort(Pin->Pin) == NULL)
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    return GPIO_RESULT_OK;
}

static GPIO_ResultTypeDef GPIO_ValidateConfiguration(const GPIO_ConfigTypeDef *Config)
{
    if(Config == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if((Config->Mode != GPIO_MODE_INPUT) && (Config->Mode != GPIO_MODE_OUTPUT))
    {
        return GPIO_RESULT_UNSUPPORTED;
    }

    if((Config->OutputType != GPIO_OUTPUT_PUSH_PULL) && (Config->OutputType != GPIO_OUTPUT_OPEN_DRAIN))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if((Config->Pull != GPIO_PULL_NONE) && (Config->Pull != GPIO_PULL_UP) && (Config->Pull != GPIO_PULL_DOWN))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if((Config->InitialLevel != GPIO_LEVEL_LOW) && (Config->InitialLevel != GPIO_LEVEL_HIGH))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    return GPIO_RESULT_OK;
}

static GPIO_ResultTypeDef GPIO_ValidateInterruptConfiguration(const GPIO_InterruptConfigTypeDef *Config)
{
    if(Config == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if(Config->Callback == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if((Config->Mode != GPIO_INTERRUPT_RISING_EDGE)
       && (Config->Mode != GPIO_INTERRUPT_FALLING_EDGE)
       && (Config->Mode != GPIO_INTERRUPT_BOTH_EDGES))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* State helpers                                                              */
/* -------------------------------------------------------------------------- */

static bool GPIO_IsInitializedInternal(const GPIO_PinTypeDef *Pin)
{
    uint32_t PortIndex;
    uint32_t PinNumber;

    PortIndex = GPIO_GetPortIndex(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    return (GPIO_InitializedPins[PortIndex] & (uint16_t)(1UL << PinNumber)) != 0U;
}

static bool GPIO_IsInputInternal(const GPIO_PinTypeDef *Pin)
{
    GPIO_TypeDef *Port;
    uint32_t PinNumber;
    uint32_t Position;
    uint32_t Mode;

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    Position = PinNumber * 2U;
    Mode = (Port->MODER >> Position) & GPIO_REGISTER_FIELD_MASK;

    return Mode == GPIO_MODER_INPUT;
}

static bool GPIO_IsOutputInternal(const GPIO_PinTypeDef *Pin)
{
    GPIO_TypeDef *Port;
    uint32_t PinNumber;
    uint32_t Position;
    uint32_t Mode;

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    Position = PinNumber * 2U;
    Mode = (Port->MODER >> Position) & GPIO_REGISTER_FIELD_MASK;

    return Mode == GPIO_MODER_OUTPUT;
}

static void GPIO_SetInitialized(const GPIO_PinTypeDef *Pin, bool Initialized)
{
    uint32_t PortIndex;
    uint32_t PinNumber;
    uint16_t PinMask;

    PortIndex = GPIO_GetPortIndex(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    PinMask = (uint16_t)(1UL << PinNumber);

    if(Initialized)
    {
        GPIO_InitializedPins[PortIndex] |= PinMask;
    }
    else
    {
        GPIO_InitializedPins[PortIndex] &= (uint16_t)~PinMask;
    }
}

static IRQn_Type GPIO_GetInterruptNumber(uint32_t PinNumber)
{
    switch(PinNumber)
    {
        case 0U:
            return EXTI0_IRQn;

        case 1U:
            return EXTI1_IRQn;

        case 2U:
            return EXTI2_IRQn;

        case 3U:
            return EXTI3_IRQn;

        case 4U:
            return EXTI4_IRQn;

        case 5U:
        case 6U:
        case 7U:
        case 8U:
        case 9U:
            return EXTI9_5_IRQn;

        default:
            return EXTI15_10_IRQn;
    }
}

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_Init(const GPIO_PinTypeDef *Pin, const GPIO_ConfigTypeDef *Config)
{
    GPIO_TypeDef *Port;
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;
    uint32_t Position;
    uint32_t OutputType;
    uint32_t Pull;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    Result = GPIO_ValidateConfiguration(Config);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    Position = PinNumber * 2U;

    if(RCC_EnablePeripheralClock(Port) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    if(Config->Mode == GPIO_MODE_OUTPUT)
    {
        if(Config->InitialLevel == GPIO_LEVEL_HIGH)
        {
            Port->BSRR = 1UL << PinNumber;
        }
        else
        {
            Port->BSRR = 1UL << (PinNumber + 16U);
        }
    }

    switch(Config->OutputType)
    {
        case GPIO_OUTPUT_PUSH_PULL:
            OutputType = GPIO_OTYPER_PUSH_PULL;
            break;

        case GPIO_OUTPUT_OPEN_DRAIN:
            OutputType = GPIO_OTYPER_OPEN_DRAIN;
            break;

        default:
            return GPIO_RESULT_INVALID_ARGUMENT;
    }

    switch(Config->Pull)
    {
        case GPIO_PULL_NONE:
            Pull = GPIO_PUPDR_NONE;
            break;

        case GPIO_PULL_UP:
            Pull = GPIO_PUPDR_UP;
            break;

        case GPIO_PULL_DOWN:
            Pull = GPIO_PUPDR_DOWN;
            break;

        default:
            return GPIO_RESULT_INVALID_ARGUMENT;
    }

    Port->OTYPER &= ~(1UL << PinNumber);
    Port->OTYPER |= OutputType << PinNumber;

    Port->OSPEEDR &= ~(GPIO_REGISTER_FIELD_MASK << Position);

    Port->PUPDR &= ~(GPIO_REGISTER_FIELD_MASK << Position);
    Port->PUPDR |= Pull << Position;

    Port->MODER &= ~(GPIO_REGISTER_FIELD_MASK << Position);

    if(Config->Mode == GPIO_MODE_OUTPUT)
    {
        Port->MODER |= GPIO_MODER_OUTPUT << Position;
    }
    else
    {
        Port->MODER |= GPIO_MODER_INPUT << Position;
    }

    GPIO_SetInitialized(Pin, true);

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_Deinit(const GPIO_PinTypeDef *Pin)
{
    GPIO_TypeDef *Port;
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;
    uint32_t Position;
    uint32_t AFRIndex;
    uint32_t AFRPosition;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    (void)GPIO_UnregisterInterrupt(Pin);

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    Position = PinNumber * 2U;
    AFRIndex = PinNumber / 8U;
    AFRPosition = (PinNumber % 8U) * 4U;

    if(RCC_EnablePeripheralClock(Port) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    Port->MODER &= ~(GPIO_REGISTER_FIELD_MASK << Position);
    Port->MODER |= GPIO_MODER_ANALOG << Position;

    Port->OTYPER &= ~(1UL << PinNumber);
    Port->OSPEEDR &= ~(GPIO_REGISTER_FIELD_MASK << Position);
    Port->PUPDR &= ~(GPIO_REGISTER_FIELD_MASK << Position);
    Port->AFR[AFRIndex] &= ~(0xFUL << AFRPosition);

    GPIO_SetInitialized(Pin, false);

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Interrupts                                                                 */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_RegisterInterrupt(const GPIO_PinTypeDef *Pin, const GPIO_InterruptConfigTypeDef *Config)
{
    GPIO_ResultTypeDef Result;
    uint32_t PortIndex;
    uint32_t PinNumber;
    uint32_t EXTICRIndex;
    uint32_t EXTICRPosition;
    uint32_t PinMask;
    IRQn_Type InterruptNumber;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    Result = GPIO_ValidateInterruptConfiguration(Config);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    if(!GPIO_IsInitializedInternal(Pin) || !GPIO_IsInputInternal(Pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    if(GPIO_InterruptRegistered[PinNumber])
    {
        return GPIO_RESULT_BUSY;
    }

    PortIndex = GPIO_GetPortIndex(Pin->Pin);
    EXTICRIndex = PinNumber / 4U;
    EXTICRPosition = (PinNumber % 4U) * 4U;
    PinMask = 1UL << PinNumber;
    InterruptNumber = GPIO_GetInterruptNumber(PinNumber);

    if(RCC_EnablePeripheralClock(SYSCFG) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    EXTI->IMR1 &= ~PinMask;

    SYSCFG->EXTICR[EXTICRIndex] &= ~(GPIO_EXTICR_FIELD_MASK << EXTICRPosition);
    SYSCFG->EXTICR[EXTICRIndex] |= PortIndex << EXTICRPosition;

    EXTI->RTSR1 &= ~PinMask;
    EXTI->FTSR1 &= ~PinMask;

    if((Config->Mode == GPIO_INTERRUPT_RISING_EDGE) || (Config->Mode == GPIO_INTERRUPT_BOTH_EDGES))
    {
        EXTI->RTSR1 |= PinMask;
    }

    if((Config->Mode == GPIO_INTERRUPT_FALLING_EDGE) || (Config->Mode == GPIO_INTERRUPT_BOTH_EDGES))
    {
        EXTI->FTSR1 |= PinMask;
    }

    EXTI->PR1 = PinMask;

    GPIO_InterruptConfigurations[PinNumber] = *Config;
    GPIO_InterruptRegistered[PinNumber] = true;

    EXTI->IMR1 |= PinMask;

    NVIC_ClearPendingIRQ(InterruptNumber);
    NVIC_EnableIRQ(InterruptNumber);

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_UnregisterInterrupt(const GPIO_PinTypeDef *Pin)
{
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;
    uint32_t PinMask;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    PinNumber = GPIO_GetPinNumber(Pin->Pin);
    PinMask = 1UL << PinNumber;

    EXTI->IMR1 &= ~PinMask;
    EXTI->RTSR1 &= ~PinMask;
    EXTI->FTSR1 &= ~PinMask;

    EXTI->PR1 = PinMask;

    GPIO_InterruptConfigurations[PinNumber].Callback = NULL;
    GPIO_InterruptConfigurations[PinNumber].Context = NULL;
    GPIO_InterruptRegistered[PinNumber] = false;

    return GPIO_RESULT_OK;
}

void GPIO_InterruptHandler(uint32_t line)
{
    GPIO_InterruptCallbackTypeDef Callback;
    void *Context;
    uint32_t LineMask;

    if(line >= GPIO_PIN_COUNT_PER_PORT)
    {
        return;
    }

    LineMask = 1UL << line;

    if((EXTI->PR1 & LineMask) == 0U)
    {
        return;
    }

    EXTI->PR1 = LineMask;

    if(!GPIO_InterruptRegistered[line])
    {
        return;
    }

    Callback = GPIO_InterruptConfigurations[line].Callback;
    Context = GPIO_InterruptConfigurations[line].Context;

    if(Callback != NULL)
    {
        Callback(Context);
    }
}

/* -------------------------------------------------------------------------- */
/* Digital output                                                             */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_Write(const GPIO_PinTypeDef *Pin, GPIO_LevelTypeDef Level)
{
    GPIO_TypeDef *Port;
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    if((Level != GPIO_LEVEL_LOW) && (Level != GPIO_LEVEL_HIGH))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if(!GPIO_IsInitializedInternal(Pin) || !GPIO_IsOutputInternal(Pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    if(Level == GPIO_LEVEL_HIGH)
    {
        Port->BSRR = 1UL << PinNumber;
    }
    else
    {
        Port->BSRR = 1UL << (PinNumber + 16U);
    }

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_Set(const GPIO_PinTypeDef *Pin)
{
    return GPIO_Write(Pin, GPIO_LEVEL_HIGH);
}

GPIO_ResultTypeDef GPIO_Clear(const GPIO_PinTypeDef *Pin)
{
    return GPIO_Write(Pin, GPIO_LEVEL_LOW);
}

GPIO_ResultTypeDef GPIO_Toggle(const GPIO_PinTypeDef *Pin)
{
    GPIO_TypeDef *Port;
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    if(!GPIO_IsInitializedInternal(Pin) || !GPIO_IsOutputInternal(Pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    if((Port->ODR & (1UL << PinNumber)) != 0U)
    {
        Port->BSRR = 1UL << (PinNumber + 16U);
    }
    else
    {
        Port->BSRR = 1UL << PinNumber;
    }

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Digital input                                                              */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_Read(const GPIO_PinTypeDef *Pin, GPIO_LevelTypeDef *Level)
{
    GPIO_TypeDef *Port;
    GPIO_ResultTypeDef Result;
    uint32_t PinNumber;

    if(Level == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    Result = GPIO_ValidatePin(Pin);

    if(Result != GPIO_RESULT_OK)
    {
        return Result;
    }

    if(!GPIO_IsInitializedInternal(Pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    Port = GPIO_GetPort(Pin->Pin);
    PinNumber = GPIO_GetPinNumber(Pin->Pin);

    if((Port->IDR & (1UL << PinNumber)) != 0U)
    {
        *Level = GPIO_LEVEL_HIGH;
    }
    else
    {
        *Level = GPIO_LEVEL_LOW;
    }

    return GPIO_RESULT_OK;
}

bool GPIO_IsHigh(const GPIO_PinTypeDef *Pin)
{
    GPIO_LevelTypeDef Level;

    if(GPIO_Read(Pin, &Level) != GPIO_RESULT_OK)
    {
        return false;
    }

    return Level == GPIO_LEVEL_HIGH;
}

bool GPIO_IsLow(const GPIO_PinTypeDef *Pin)
{
    GPIO_LevelTypeDef Level;

    if(GPIO_Read(Pin, &Level) != GPIO_RESULT_OK)
    {
        return false;
    }

    return Level == GPIO_LEVEL_LOW;
}

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

bool GPIO_IsAssigned(const GPIO_PinTypeDef *Pin)
{
    return (Pin != NULL) && (Pin->Pin != GPIO_PIN_NONE);
}
