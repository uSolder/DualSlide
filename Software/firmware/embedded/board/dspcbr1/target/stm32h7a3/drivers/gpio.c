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

static GPIO_TypeDef *GPIO_GetPort(GPIO_PinIdTypeDef pin_id);
static uint32_t GPIO_GetPortIndex(GPIO_PinIdTypeDef pin_id);
static uint32_t GPIO_GetPinNumber(GPIO_PinIdTypeDef pin_id);
static GPIO_ResultTypeDef GPIO_ValidatePin(const GPIO_PinTypeDef *pin);
static GPIO_ResultTypeDef GPIO_ValidateConfiguration(const GPIO_ConfigTypeDef *config);
static GPIO_ResultTypeDef GPIO_ValidateInterruptConfiguration(const GPIO_InterruptConfigTypeDef *config);
static bool GPIO_IsInitializedInternal(const GPIO_PinTypeDef *pin);
static bool GPIO_IsInputInternal(const GPIO_PinTypeDef *pin);
static bool GPIO_IsOutputInternal(const GPIO_PinTypeDef *pin);
static void GPIO_SetInitialized(const GPIO_PinTypeDef *pin, bool initialized);
static IRQn_Type GPIO_GetInterruptNumber(uint32_t pin_number);

/* -------------------------------------------------------------------------- */
/* Pin decoding                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *GPIO_GetPort(GPIO_PinIdTypeDef pin_id)
{
    uint32_t port_index;

    port_index = (pin_id >> GPIO_PORT_INDEX_SHIFT) & GPIO_PORT_INDEX_MASK;

    switch (port_index)
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

static uint32_t GPIO_GetPortIndex(GPIO_PinIdTypeDef pin_id)
{
    return (pin_id >> GPIO_PORT_INDEX_SHIFT) & GPIO_PORT_INDEX_MASK;
}

static uint32_t GPIO_GetPinNumber(GPIO_PinIdTypeDef pin_id)
{
    return pin_id & GPIO_PIN_NUMBER_MASK;
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static GPIO_ResultTypeDef GPIO_ValidatePin(const GPIO_PinTypeDef *pin)
{
    uint32_t port_index;
    uint32_t pin_number;

    if (pin == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if (pin->Pin == GPIO_PIN_NONE)
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    port_index = GPIO_GetPortIndex(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);

    if ((port_index >= GPIO_PORT_COUNT) || (pin_number >= GPIO_PIN_COUNT_PER_PORT))
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    if (GPIO_GetPort(pin->Pin) == NULL)
    {
        return GPIO_RESULT_INVALID_PIN;
    }

    return GPIO_RESULT_OK;
}

static GPIO_ResultTypeDef GPIO_ValidateConfiguration(const GPIO_ConfigTypeDef *config)
{
    if (config == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if ((config->Mode != GPIO_MODE_INPUT) && (config->Mode != GPIO_MODE_OUTPUT))
    {
        return GPIO_RESULT_UNSUPPORTED;
    }

    if ((config->OutputType != GPIO_OUTPUT_PUSH_PULL) && (config->OutputType != GPIO_OUTPUT_OPEN_DRAIN))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if ((config->Pull != GPIO_PULL_NONE) && (config->Pull != GPIO_PULL_UP) && (config->Pull != GPIO_PULL_DOWN))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if ((config->InitialLevel != GPIO_LEVEL_LOW) && (config->InitialLevel != GPIO_LEVEL_HIGH))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    return GPIO_RESULT_OK;
}

static GPIO_ResultTypeDef GPIO_ValidateInterruptConfiguration(const GPIO_InterruptConfigTypeDef *config)
{
    if (config == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if (config->Callback == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if ((config->Mode != GPIO_INTERRUPT_RISING_EDGE)
        && (config->Mode != GPIO_INTERRUPT_FALLING_EDGE)
        && (config->Mode != GPIO_INTERRUPT_BOTH_EDGES))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* State helpers                                                              */
/* -------------------------------------------------------------------------- */

static bool GPIO_IsInitializedInternal(const GPIO_PinTypeDef *pin)
{
    uint32_t port_index;
    uint32_t pin_number;

    port_index = GPIO_GetPortIndex(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);

    return (GPIO_InitializedPins[port_index] & (uint16_t)(1UL << pin_number)) != 0U;
}

static bool GPIO_IsInputInternal(const GPIO_PinTypeDef *pin)
{
    GPIO_TypeDef *port;
    uint32_t pin_number;
    uint32_t position;
    uint32_t mode;

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);
    position = pin_number * 2U;
    mode = (port->MODER >> position) & GPIO_REGISTER_FIELD_MASK;

    return mode == GPIO_MODER_INPUT;
}

static bool GPIO_IsOutputInternal(const GPIO_PinTypeDef *pin)
{
    GPIO_TypeDef *port;
    uint32_t pin_number;
    uint32_t position;
    uint32_t mode;

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);
    position = pin_number * 2U;
    mode = (port->MODER >> position) & GPIO_REGISTER_FIELD_MASK;

    return mode == GPIO_MODER_OUTPUT;
}

static void GPIO_SetInitialized(const GPIO_PinTypeDef *pin, bool initialized)
{
    uint32_t port_index;
    uint32_t pin_number;
    uint16_t pin_mask;

    port_index = GPIO_GetPortIndex(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);
    pin_mask = (uint16_t)(1UL << pin_number);

    if (initialized)
    {
        GPIO_InitializedPins[port_index] |= pin_mask;
    }
    else
    {
        GPIO_InitializedPins[port_index] &= (uint16_t)~pin_mask;
    }
}

static IRQn_Type GPIO_GetInterruptNumber(uint32_t pin_number)
{
    switch (pin_number)
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

GPIO_ResultTypeDef GPIO_Init(const GPIO_PinTypeDef *pin, const GPIO_ConfigTypeDef *config)
{
    GPIO_TypeDef *port;
    GPIO_ResultTypeDef result;
    uint32_t pin_number;
    uint32_t position;
    uint32_t output_type;
    uint32_t pull;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    result = GPIO_ValidateConfiguration(config);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);
    position = pin_number * 2U;

    if (RCC_EnablePeripheralClock(port) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    if (config->Mode == GPIO_MODE_OUTPUT)
    {
        if (config->InitialLevel == GPIO_LEVEL_HIGH)
        {
            port->BSRR = 1UL << pin_number;
        }
        else
        {
            port->BSRR = 1UL << (pin_number + 16U);
        }
    }

    switch (config->OutputType)
    {
        case GPIO_OUTPUT_PUSH_PULL:
            output_type = GPIO_OTYPER_PUSH_PULL;
            break;

        case GPIO_OUTPUT_OPEN_DRAIN:
            output_type = GPIO_OTYPER_OPEN_DRAIN;
            break;

        default:
            return GPIO_RESULT_INVALID_ARGUMENT;
    }

    switch (config->Pull)
    {
        case GPIO_PULL_NONE:
            pull = GPIO_PUPDR_NONE;
            break;

        case GPIO_PULL_UP:
            pull = GPIO_PUPDR_UP;
            break;

        case GPIO_PULL_DOWN:
            pull = GPIO_PUPDR_DOWN;
            break;

        default:
            return GPIO_RESULT_INVALID_ARGUMENT;
    }

    port->OTYPER &= ~(1UL << pin_number);
    port->OTYPER |= output_type << pin_number;

    port->OSPEEDR &= ~(GPIO_REGISTER_FIELD_MASK << position);

    port->PUPDR &= ~(GPIO_REGISTER_FIELD_MASK << position);
    port->PUPDR |= pull << position;

    port->MODER &= ~(GPIO_REGISTER_FIELD_MASK << position);

    if (config->Mode == GPIO_MODE_OUTPUT)
    {
        port->MODER |= GPIO_MODER_OUTPUT << position;
    }
    else
    {
        port->MODER |= GPIO_MODER_INPUT << position;
    }

    GPIO_SetInitialized(pin, true);

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_Deinit(const GPIO_PinTypeDef *pin)
{
    GPIO_TypeDef *port;
    GPIO_ResultTypeDef result;
    uint32_t pin_number;
    uint32_t position;
    uint32_t afr_index;
    uint32_t afr_position;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    (void)GPIO_UnregisterInterrupt(pin);

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);
    position = pin_number * 2U;
    afr_index = pin_number / 8U;
    afr_position = (pin_number % 8U) * 4U;

    if (RCC_EnablePeripheralClock(port) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    port->MODER &= ~(GPIO_REGISTER_FIELD_MASK << position);
    port->MODER |= GPIO_MODER_ANALOG << position;

    port->OTYPER &= ~(1UL << pin_number);
    port->OSPEEDR &= ~(GPIO_REGISTER_FIELD_MASK << position);
    port->PUPDR &= ~(GPIO_REGISTER_FIELD_MASK << position);
    port->AFR[afr_index] &= ~(0xFUL << afr_position);

    GPIO_SetInitialized(pin, false);

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Interrupts                                                                 */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_RegisterInterrupt(const GPIO_PinTypeDef *pin, const GPIO_InterruptConfigTypeDef *config)
{
    GPIO_ResultTypeDef result;
    uint32_t port_index;
    uint32_t pin_number;
    uint32_t exticr_index;
    uint32_t exticr_position;
    uint32_t pin_mask;
    IRQn_Type interrupt_number;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    result = GPIO_ValidateInterruptConfiguration(config);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    if (!GPIO_IsInitializedInternal(pin) || !GPIO_IsInputInternal(pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    pin_number = GPIO_GetPinNumber(pin->Pin);

    if (GPIO_InterruptRegistered[pin_number])
    {
        return GPIO_RESULT_BUSY;
    }

    port_index = GPIO_GetPortIndex(pin->Pin);
    exticr_index = pin_number / 4U;
    exticr_position = (pin_number % 4U) * 4U;
    pin_mask = 1UL << pin_number;
    interrupt_number = GPIO_GetInterruptNumber(pin_number);

    if (RCC_EnablePeripheralClock(SYSCFG) != RCC_RESULT_OK)
    {
        return GPIO_RESULT_HARDWARE_ERROR;
    }

    EXTI->IMR1 &= ~pin_mask;

    SYSCFG->EXTICR[exticr_index] &= ~(GPIO_EXTICR_FIELD_MASK << exticr_position);
    SYSCFG->EXTICR[exticr_index] |= port_index << exticr_position;

    EXTI->RTSR1 &= ~pin_mask;
    EXTI->FTSR1 &= ~pin_mask;

    if ((config->Mode == GPIO_INTERRUPT_RISING_EDGE) || (config->Mode == GPIO_INTERRUPT_BOTH_EDGES))
    {
        EXTI->RTSR1 |= pin_mask;
    }

    if ((config->Mode == GPIO_INTERRUPT_FALLING_EDGE) || (config->Mode == GPIO_INTERRUPT_BOTH_EDGES))
    {
        EXTI->FTSR1 |= pin_mask;
    }

    EXTI->PR1 = pin_mask;

    GPIO_InterruptConfigurations[pin_number] = *config;
    GPIO_InterruptRegistered[pin_number] = true;

    EXTI->IMR1 |= pin_mask;

    NVIC_ClearPendingIRQ(interrupt_number);
    NVIC_EnableIRQ(interrupt_number);

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_UnregisterInterrupt(const GPIO_PinTypeDef *pin)
{
    GPIO_ResultTypeDef result;
    uint32_t pin_number;
    uint32_t pin_mask;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    pin_number = GPIO_GetPinNumber(pin->Pin);
    pin_mask = 1UL << pin_number;

    EXTI->IMR1 &= ~pin_mask;
    EXTI->RTSR1 &= ~pin_mask;
    EXTI->FTSR1 &= ~pin_mask;

    EXTI->PR1 = pin_mask;

    GPIO_InterruptConfigurations[pin_number].Callback = NULL;
    GPIO_InterruptConfigurations[pin_number].Context = NULL;
    GPIO_InterruptRegistered[pin_number] = false;

    return GPIO_RESULT_OK;
}

void GPIO_InterruptHandler(uint32_t line)
{
    GPIO_InterruptCallbackTypeDef callback;
    void *context;
    uint32_t line_mask;

    if (line >= GPIO_PIN_COUNT_PER_PORT)
    {
        return;
    }

    line_mask = 1UL << line;

    if ((EXTI->PR1 & line_mask) == 0U)
    {
        return;
    }

    EXTI->PR1 = line_mask;

    if (!GPIO_InterruptRegistered[line])
    {
        return;
    }

    callback = GPIO_InterruptConfigurations[line].Callback;
    context = GPIO_InterruptConfigurations[line].Context;

    if (callback != NULL)
    {
        callback(context);
    }
}

/* -------------------------------------------------------------------------- */
/* Digital output                                                             */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_Write(const GPIO_PinTypeDef *pin, GPIO_LevelTypeDef level)
{
    GPIO_TypeDef *port;
    GPIO_ResultTypeDef result;
    uint32_t pin_number;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    if ((level != GPIO_LEVEL_LOW) && (level != GPIO_LEVEL_HIGH))
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    if (!GPIO_IsInitializedInternal(pin) || !GPIO_IsOutputInternal(pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);

    if (level == GPIO_LEVEL_HIGH)
    {
        port->BSRR = 1UL << pin_number;
    }
    else
    {
        port->BSRR = 1UL << (pin_number + 16U);
    }

    return GPIO_RESULT_OK;
}

GPIO_ResultTypeDef GPIO_Set(const GPIO_PinTypeDef *pin)
{
    return GPIO_Write(pin, GPIO_LEVEL_HIGH);
}

GPIO_ResultTypeDef GPIO_Clear(const GPIO_PinTypeDef *pin)
{
    return GPIO_Write(pin, GPIO_LEVEL_LOW);
}

GPIO_ResultTypeDef GPIO_Toggle(const GPIO_PinTypeDef *pin)
{
    GPIO_TypeDef *port;
    GPIO_ResultTypeDef result;
    uint32_t pin_number;

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    if (!GPIO_IsInitializedInternal(pin) || !GPIO_IsOutputInternal(pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);

    if ((port->ODR & (1UL << pin_number)) != 0U)
    {
        port->BSRR = 1UL << (pin_number + 16U);
    }
    else
    {
        port->BSRR = 1UL << pin_number;
    }

    return GPIO_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Digital input                                                              */
/* -------------------------------------------------------------------------- */

GPIO_ResultTypeDef GPIO_Read(const GPIO_PinTypeDef *pin, GPIO_LevelTypeDef *level)
{
    GPIO_TypeDef *port;
    GPIO_ResultTypeDef result;
    uint32_t pin_number;

    if (level == NULL)
    {
        return GPIO_RESULT_INVALID_ARGUMENT;
    }

    result = GPIO_ValidatePin(pin);

    if (result != GPIO_RESULT_OK)
    {
        return result;
    }

    if (!GPIO_IsInitializedInternal(pin))
    {
        return GPIO_RESULT_NOT_INITIALIZED;
    }

    port = GPIO_GetPort(pin->Pin);
    pin_number = GPIO_GetPinNumber(pin->Pin);

    if ((port->IDR & (1UL << pin_number)) != 0U)
    {
        *level = GPIO_LEVEL_HIGH;
    }
    else
    {
        *level = GPIO_LEVEL_LOW;
    }

    return GPIO_RESULT_OK;
}

bool GPIO_IsHigh(const GPIO_PinTypeDef *pin)
{
    GPIO_LevelTypeDef level;

    if (GPIO_Read(pin, &level) != GPIO_RESULT_OK)
    {
        return false;
    }

    return level == GPIO_LEVEL_HIGH;
}

bool GPIO_IsLow(const GPIO_PinTypeDef *pin)
{
    GPIO_LevelTypeDef level;

    if (GPIO_Read(pin, &level) != GPIO_RESULT_OK)
    {
        return false;
    }

    return level == GPIO_LEVEL_LOW;
}

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

bool GPIO_IsAssigned(const GPIO_PinTypeDef *pin)
{
    return (pin != NULL) && (pin->Pin != GPIO_PIN_NONE);
}