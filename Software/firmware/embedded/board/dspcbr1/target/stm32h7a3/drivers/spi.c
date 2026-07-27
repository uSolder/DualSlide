/**
 * @file spi.c
 * @brief STM32H7A3 blocking SPI implementation.
 *
 * This driver implements the hardware-independent SPI contract using the
 * STM32H7A3 SPI peripheral in polling mode.
 */


#include "spi.h"

#include "STM32H7A3_Defs.h"
#include "time.h"
#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define SPI_MAX_REGISTERED_BUSES     6U
#define SPI_DEFAULT_TIMEOUT_MS       100U

#define SPI_GPIO_MODE_OUTPUT         1U
#define SPI_GPIO_MODE_ALTERNATE      2U
#define SPI_GPIO_SPEED_VERY_HIGH     3U


/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
* @brief Runtime state associated with one initialized SPI bus.
*/
typedef struct
{
    SPI_Bus_Handle *handle;
    SPI_TypeDef *instance;
    bool initialized;
    bool busy;
} SPI_BusState;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static SPI_BusState SPI_BusRegistry[SPI_MAX_REGISTERED_BUSES];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *SPI_GetGPIOPort(SPI_Pin pin);
static uint32_t SPI_GetGPIOPinNumber(SPI_Pin pin);
static SPI_TypeDef *SPI_GetPeripheral(SPI_Port port);
static SPI_BusState *SPI_FindBusState(const SPI_Bus_Handle *bus);
static SPI_BusState *SPI_AllocateBusState(SPI_Bus_Handle *bus);
static SPI_Result SPI_ValidateBus(const SPI_Bus_Handle *bus);
static SPI_Result SPI_ValidateDevice(const SPI_Device_Handle *device);
static SPI_Result SPI_GetAlternateFunction(SPI_Port port, SPI_Pin pin, uint32_t *alternate_function);
static SPI_Result SPI_ConfigureSignalPin(SPI_Port port, SPI_Pin pin);
static SPI_Result SPI_ConfigureChipSelectPin(const SPI_Device_Handle *device);
static SPI_Result SPI_SetChipSelect(const SPI_Device_Handle *device, bool asserted);
static SPI_Result SPI_ApplyDeviceConfiguration(SPI_BusState *state, const SPI_Device_Handle *device);
static SPI_Result SPI_BeginTransfer(SPI_BusState *state, const SPI_Device_Handle *device, size_t count);
static SPI_Result SPI_EndTransfer(SPI_BusState *state, const SPI_Device_Handle *device);
static SPI_Result SPI_AbortTransfer(SPI_BusState *state, const SPI_Device_Handle *device, SPI_Result result);
static SPI_Result SPI_ClearStatusFlags(SPI_TypeDef *instance);
static uint32_t SPI_GetEffectiveTimeout(uint32_t timeout_ms);
static SPI_Result SPI_WaitForSet(volatile uint32_t *reg, uint32_t mask, uint32_t timeout_ms);
static uint32_t SPI_GetBaudRateEncoding(uint32_t kernel_frequency_hz, uint32_t requested_frequency_hz);
static uint32_t SPI_GetFrameMask(SPI_FrameSize frame_size);
static uint16_t SPI_LoadFrame(const void *data, size_t index, SPI_FrameSize frame_size);
static void SPI_StoreFrame(void *data, size_t index, SPI_FrameSize frame_size, uint16_t frame);
static void SPI_WriteTXDR(SPI_TypeDef *instance, SPI_FrameSize frame_size, uint16_t frame);
static uint16_t SPI_ReadRXDR(SPI_TypeDef *instance, SPI_FrameSize frame_size);

/* -------------------------------------------------------------------------- */
/* GPIO helpers                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *SPI_GetGPIOPort(SPI_Pin pin)
{
    uint32_t port_index;

    if (pin == SPI_PIN_UNUSED)
    {
        return NULL;
    }

    port_index = ((uint32_t)pin >> 4U) & 0x0FU;

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

        case 9U:
            return GPIOJ;

        case 10U:
            return GPIOK;

        default:
            return NULL;
    }
}

static uint32_t SPI_GetGPIOPinNumber(SPI_Pin pin)
{
    return (uint32_t)pin & 0x0FU;
}

/* -------------------------------------------------------------------------- */
/* Peripheral helpers                                                         */
/* -------------------------------------------------------------------------- */

static SPI_TypeDef *SPI_GetPeripheral(SPI_Port port)
{
    switch (port)
    {
        case SPI_PORT_1:
            return SPI1;

        case SPI_PORT_2:
            return SPI2;

        case SPI_PORT_3:
            return SPI3;

        case SPI_PORT_4:
            return SPI4;

        case SPI_PORT_5:
            return SPI5;

#ifdef SPI6
        case SPI_PORT_6:
            return SPI6;
#endif

        default:
            return NULL;
    }
}

/* -------------------------------------------------------------------------- */
/* Bus registry                                                               */
/* -------------------------------------------------------------------------- */

static SPI_BusState *SPI_FindBusState(const SPI_Bus_Handle *bus)
{
    size_t index;

    if (bus == NULL)
    {
        return NULL;
    }

    for (index = 0U; index < SPI_MAX_REGISTERED_BUSES; index++)
    {
        if (SPI_BusRegistry[index].initialized && (SPI_BusRegistry[index].handle == bus))
        {
            return &SPI_BusRegistry[index];
        }
    }

    return NULL;
}

static SPI_BusState *SPI_AllocateBusState(SPI_Bus_Handle *bus)
{
    size_t index;

    for (index = 0U; index < SPI_MAX_REGISTERED_BUSES; index++)
    {
        if (!SPI_BusRegistry[index].initialized)
        {
            SPI_BusRegistry[index].handle = bus;
            SPI_BusRegistry[index].instance = NULL;
            SPI_BusRegistry[index].busy = false;

            return &SPI_BusRegistry[index];
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static SPI_Result SPI_ValidateBus(const SPI_Bus_Handle *bus)
{
    if (bus == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (bus->sclk_pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((bus->mosi_pin == SPI_PIN_UNUSED) && (bus->miso_pin == SPI_PIN_UNUSED))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (SPI_GetPeripheral(bus->port) == NULL)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    if (SPI_GetGPIOPort(bus->sclk_pin) == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((bus->mosi_pin != SPI_PIN_UNUSED) && (SPI_GetGPIOPort(bus->mosi_pin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((bus->miso_pin != SPI_PIN_UNUSED) && (SPI_GetGPIOPort(bus->miso_pin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    return SPI_RESULT_OK;
}

static SPI_Result SPI_ValidateDevice(const SPI_Device_Handle *device)
{
    if ((device == NULL) || (device->bus == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (SPI_FindBusState(device->bus) == NULL)
    {
        return SPI_RESULT_NOT_INITIALIZED;
    }

    if (device->frequency_hz == 0U)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((device->mode < SPI_MODE_0) || (device->mode > SPI_MODE_3))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((device->bit_order != SPI_BIT_ORDER_MSB_FIRST) && (device->bit_order != SPI_BIT_ORDER_LSB_FIRST))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((device->frame_size != SPI_FRAME_SIZE_8_BIT) &&
        (device->frame_size != SPI_FRAME_SIZE_9_BIT) &&
        (device->frame_size != SPI_FRAME_SIZE_16_BIT))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    if ((device->chip_select_polarity != SPI_CHIP_SELECT_ACTIVE_LOW) &&
        (device->chip_select_polarity != SPI_CHIP_SELECT_ACTIVE_HIGH))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((device->chip_select_pin != SPI_PIN_UNUSED) &&
        (SPI_GetGPIOPort(device->chip_select_pin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Alternate-function mapping                                                 */
/* -------------------------------------------------------------------------- */

static SPI_Result SPI_GetAlternateFunction(SPI_Port port, SPI_Pin pin, uint32_t *alternate_function)
{
    if ((pin == SPI_PIN_UNUSED) || (alternate_function == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    /*
    * Only explicitly supported routes are accepted.
    *
    * Add additional routes from the STM32H7A3 datasheet as they are required
    * by a board. SPI alternate-function numbers are pin-dependent and cannot
    * safely be inferred from the SPI peripheral number alone.
    */

    switch (port)
    {
        case SPI_PORT_1:
            switch (pin)
            {
                case PA5:
                case PA6:
                case PA7:
                    * alternate_function = 5U;
                    return SPI_RESULT_OK;

                default:
                    return SPI_RESULT_UNSUPPORTED;
            }

        case SPI_PORT_2:
            switch (pin)
            {
                case PB13:
                case PB14:
                case PB15:
                case PC3:
                    * alternate_function = 5U;
                    return SPI_RESULT_OK;

                default:
                    return SPI_RESULT_UNSUPPORTED;
            }

        case SPI_PORT_3:
            switch (pin)
            {
                case PC10:
                case PC11:
                case PC12:
                    * alternate_function = 6U;
                    return SPI_RESULT_OK;

                default:
                    return SPI_RESULT_UNSUPPORTED;
            }

        default:
            return SPI_RESULT_UNSUPPORTED;
    }
}

/* -------------------------------------------------------------------------- */
/* Pin configuration                                                          */
/* -------------------------------------------------------------------------- */

static SPI_Result SPI_ConfigureSignalPin(SPI_Port port, SPI_Pin pin)
{
    GPIO_TypeDef *gpio;
    uint32_t pin_number;
    uint32_t alternate_function;
    uint32_t afr_index;
    uint32_t afr_position;
    SPI_Result result;

    if (pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    gpio = SPI_GetGPIOPort(pin);

    if (gpio == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    result = SPI_GetAlternateFunction(port, pin, &alternate_function);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    if (RCC_EnablePeripheralClock(gpio) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    pin_number = SPI_GetGPIOPinNumber(pin);
    afr_index = pin_number / 8U;
    afr_position = (pin_number % 8U) * 4U;

    gpio->MODER &= ~(0x3UL << (pin_number * 2U));
    gpio->MODER |= SPI_GPIO_MODE_ALTERNATE << (pin_number * 2U);

    gpio->OTYPER &= ~(1UL << pin_number);

    gpio->OSPEEDR &= ~(0x3UL << (pin_number * 2U));
    gpio->OSPEEDR |= SPI_GPIO_SPEED_VERY_HIGH << (pin_number * 2U);

    gpio->PUPDR &= ~(0x3UL << (pin_number * 2U));

    gpio->AFR[afr_index] &= ~(0xFUL << afr_position);
    gpio->AFR[afr_index] |= alternate_function << afr_position;

    return SPI_RESULT_OK;
}

static SPI_Result SPI_ConfigureChipSelectPin(const SPI_Device_Handle *device)
{
    GPIO_TypeDef *gpio;
    uint32_t pin_number;

    if (device->chip_select_pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    gpio = SPI_GetGPIOPort(device->chip_select_pin);

    if (gpio == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (RCC_EnablePeripheralClock(gpio) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    pin_number = SPI_GetGPIOPinNumber(device->chip_select_pin);

    /*
    * Establish the inactive output value before changing the pin into output
    * mode to avoid a chip-select pulse during initialization.
    */
    if (SPI_SetChipSelect(device, false) != SPI_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    gpio->MODER &= ~(0x3UL << (pin_number * 2U));
    gpio->MODER |= SPI_GPIO_MODE_OUTPUT << (pin_number * 2U);

    gpio->OTYPER &= ~(1UL << pin_number);

    gpio->OSPEEDR &= ~(0x3UL << (pin_number * 2U));
    gpio->OSPEEDR |= SPI_GPIO_SPEED_VERY_HIGH << (pin_number * 2U);

    gpio->PUPDR &= ~(0x3UL << (pin_number * 2U));

    return SPI_RESULT_OK;
}

static SPI_Result SPI_SetChipSelect(const SPI_Device_Handle *device, bool asserted)
{
    GPIO_TypeDef *gpio;
    uint32_t pin_mask;
    bool drive_high;

    if (device->chip_select_pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    gpio = SPI_GetGPIOPort(device->chip_select_pin);

    if (gpio == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    pin_mask = 1UL << SPI_GetGPIOPinNumber(device->chip_select_pin);

    if (device->chip_select_polarity == SPI_CHIP_SELECT_ACTIVE_HIGH)
    {
        drive_high = asserted;
    }
    else
    {
        drive_high = !asserted;
    }

    gpio->BSRR = drive_high ? pin_mask : (pin_mask << 16U);

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Timeout helpers                                                            */
/* -------------------------------------------------------------------------- */

static uint32_t SPI_GetEffectiveTimeout(uint32_t timeout_ms)
{
    return (timeout_ms == 0U) ? SPI_DEFAULT_TIMEOUT_MS : timeout_ms;
}

static SPI_Result SPI_WaitForSet(volatile uint32_t *reg, uint32_t mask, uint32_t timeout_ms)
{
    uint32_t start_time;
    uint32_t effective_timeout;

    if (reg == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    effective_timeout = SPI_GetEffectiveTimeout(timeout_ms);
    start_time = Time_GetMilliseconds();

    while ((*reg & mask) == 0U)
    {
        if ((uint32_t)(Time_GetMilliseconds() - start_time) >= effective_timeout)
        {
            return SPI_RESULT_TIMEOUT;
        }
    }

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Peripheral configuration                                                   */
/* -------------------------------------------------------------------------- */

static uint32_t SPI_GetBaudRateEncoding(uint32_t kernel_frequency_hz, uint32_t requested_frequency_hz)
{
    uint32_t divider;
    uint32_t encoding;

    divider = 2U;
    encoding = 0U;

    while ((encoding < 7U) && ((kernel_frequency_hz / divider) > requested_frequency_hz))
    {
        divider <<= 1U;
        encoding++;
    }

    return encoding;
}

static uint32_t SPI_GetFrameMask(SPI_FrameSize frame_size)
{
    switch (frame_size)
    {
        case SPI_FRAME_SIZE_8_BIT:
            return 0x00FFU;

        case SPI_FRAME_SIZE_9_BIT:
            return 0x01FFU;

        case SPI_FRAME_SIZE_16_BIT:
            return 0xFFFFU;

        default:
            return 0U;
    }
}

static SPI_Result SPI_ClearStatusFlags(SPI_TypeDef *instance)
{
    if (instance == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    instance->IFCR =
        SPI_IFCR_EOTC |
        SPI_IFCR_TXTFC |
        SPI_IFCR_OVRC |
        SPI_IFCR_UDRC |
        SPI_IFCR_TIFREC |
        SPI_IFCR_CRCEC |
        SPI_IFCR_SUSPC;

    return SPI_RESULT_OK;
}

static SPI_Result SPI_ApplyDeviceConfiguration(SPI_BusState *state, const SPI_Device_Handle *device)
{
    SPI_TypeDef *instance;
    uint32_t kernel_frequency_hz;
    uint32_t baud_rate;
    uint32_t cfg1;
    uint32_t cfg2;

    if ((state == NULL) || (device == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    instance = state->instance;

    if ((instance->CR1 & SPI_CR1_SPE) != 0U)
    {
        return SPI_RESULT_BUSY;
    }

    kernel_frequency_hz = RCC_GetKernelFrequency(instance);

    if (kernel_frequency_hz == 0U)
    {
        return SPI_RESULT_IO_ERROR;
    }

    baud_rate = SPI_GetBaudRateEncoding(kernel_frequency_hz, device->frequency_hz);

    cfg1 = 0U;
    cfg1 |= (((uint32_t)device->frame_size - 1U) << SPI_CFG1_DSIZE_Pos) & SPI_CFG1_DSIZE_Msk;
    cfg1 |= (baud_rate << SPI_CFG1_MBR_Pos) & SPI_CFG1_MBR_Msk;
    instance->CFG1 = cfg1;

    cfg2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR;

    if ((device->bus->mosi_pin != SPI_PIN_UNUSED) && (device->bus->miso_pin == SPI_PIN_UNUSED))
    {
        cfg2 |= SPI_CFG2_COMM_0;
    }

    switch (device->mode)
    {
        case SPI_MODE_0:
            break;

        case SPI_MODE_1:
            cfg2 |= SPI_CFG2_CPHA;
            break;

        case SPI_MODE_2:
            cfg2 |= SPI_CFG2_CPOL;
            break;

        case SPI_MODE_3:
            cfg2 |= SPI_CFG2_CPOL | SPI_CFG2_CPHA;
            break;

        default:
            return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (device->bit_order == SPI_BIT_ORDER_LSB_FIRST)
    {
        cfg2 |= SPI_CFG2_LSBFRST;
    }

    instance->CFG2 = cfg2;
    instance->CR1 = SPI_CR1_SSI;
    instance->CR2 = 0U;
    instance->IER = 0U;

    return SPI_ClearStatusFlags(instance);
}

/* -------------------------------------------------------------------------- */
/* Frame access                                                               */
/* -------------------------------------------------------------------------- */

static uint16_t SPI_LoadFrame(const void *data, size_t index, SPI_FrameSize frame_size)
{
    if (frame_size == SPI_FRAME_SIZE_8_BIT)
    {
        return ((const uint8_t *)data)[index];
    }

    return ((const uint16_t *)data)[index] & (uint16_t)SPI_GetFrameMask(frame_size);
}

static void SPI_StoreFrame(void *data, size_t index, SPI_FrameSize frame_size, uint16_t frame)
{
    frame &= (uint16_t)SPI_GetFrameMask(frame_size);

    if (frame_size == SPI_FRAME_SIZE_8_BIT)
    {
        ((uint8_t *)data)[index] = (uint8_t)frame;
    }
    else
    {
        ((uint16_t *)data)[index] = frame;
    }
}

static void SPI_WriteTXDR(SPI_TypeDef *instance, SPI_FrameSize frame_size, uint16_t frame)
{
    if (frame_size == SPI_FRAME_SIZE_9_BIT)
    {
        * (volatile uint16_t *)&instance->TXDR = frame;
    }
    else
    {
        * (volatile uint8_t *)&instance->TXDR = (uint8_t)frame;
    }
}

static uint16_t SPI_ReadRXDR(SPI_TypeDef *instance, SPI_FrameSize frame_size)
{
    if (frame_size == SPI_FRAME_SIZE_9_BIT)
    {
        return *(volatile uint16_t *)&instance->RXDR;
    }

    return *(volatile uint8_t *)&instance->RXDR;
}

/* -------------------------------------------------------------------------- */
/* Transfer control                                                           */
/* -------------------------------------------------------------------------- */

static SPI_Result SPI_BeginTransfer(SPI_BusState *state, const SPI_Device_Handle *device, size_t count)
{
    SPI_Result result;

    if ((state == NULL) || (device == NULL) || (count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if (state->busy)
    {
        return SPI_RESULT_BUSY;
    }

    if (count > (size_t)(SPI_CR2_TSIZE_Msk >> SPI_CR2_TSIZE_Pos))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    state->busy = true;

    result = SPI_ApplyDeviceConfiguration(state, device);

    if (result != SPI_RESULT_OK)
    {
        state->busy = false;
        return result;
    }

    result = SPI_SetChipSelect(device, true);

    if (result != SPI_RESULT_OK)
    {
        state->busy = false;
        return result;
    }

    state->instance->IFCR = 0xFFFFFFFFUL;
    state->instance->CR2 =
        ((uint32_t)count << SPI_CR2_TSIZE_Pos) &
        SPI_CR2_TSIZE_Msk;
    state->instance->CR1 |= SPI_CR1_SPE;

    return SPI_RESULT_OK;
}

static SPI_Result SPI_EndTransfer(SPI_BusState *state, const SPI_Device_Handle *device)
{
    SPI_Result result;

    result = SPI_WaitForSet(&state->instance->SR, SPI_SR_EOT, device->timeout_ms);

    state->instance->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    state->instance->CR1 &= ~SPI_CR1_SPE;

    (void)SPI_SetChipSelect(device, false);

    state->busy = false;

    return result;
}

static SPI_Result SPI_AbortTransfer(SPI_BusState *state, const SPI_Device_Handle *device, SPI_Result result)
{
    if ((state != NULL) && (state->instance != NULL))
    {
        state->instance->CR1 &= ~SPI_CR1_SPE;
        (void)SPI_ClearStatusFlags(state->instance);
        state->busy = false;
    }

    if (device != NULL)
    {
        (void)SPI_SetChipSelect(device, false);
    }

    return result;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

SPI_Result SPI_BusInit(SPI_Bus_Handle *bus)
{
    SPI_BusState *state;
    SPI_TypeDef *instance;
    SPI_Result result;

    result = SPI_ValidateBus(bus);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    if (SPI_FindBusState(bus) != NULL)
    {
        return SPI_RESULT_OK;
    }

    instance = SPI_GetPeripheral(bus->port);
    state = SPI_AllocateBusState(bus);

    if (state == NULL)
    {
        return SPI_RESULT_BUSY;
    }

    if (RCC_EnablePeripheralClock(instance) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    if (RCC_ResetPeripheral(instance) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    /*
     * Keep SSI asserted while software slave management is enabled to prevent
     * the peripheral from entering a mode-fault condition.
     */
    instance->CR1 &= ~SPI_CR1_SPE;
    instance->CR1 = SPI_CR1_SSI;
    instance->CR2 = 0U;
    instance->CFG1 = 0U;
    instance->CFG2 = 0U;
    instance->IER = 0U;
    instance->IFCR = 0xFFFFFFFFUL;

    result = SPI_ConfigureSignalPin(bus->port, bus->sclk_pin);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    result = SPI_ConfigureSignalPin(bus->port, bus->mosi_pin);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    result = SPI_ConfigureSignalPin(bus->port, bus->miso_pin);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    state->instance = instance;
    state->initialized = true;
    state->busy = false;

    return SPI_RESULT_OK;
}

SPI_Result SPI_DeviceInit(SPI_Device_Handle *device)
{
    SPI_Result result;

    result = SPI_ValidateDevice(device);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    return SPI_ConfigureChipSelectPin(device);
}

SPI_Result SPI_Write(SPI_Device_Handle *device, const void *data, size_t count)
{
    SPI_BusState *state;
    SPI_Result result;
    uint16_t received_frame;
    size_t index;

    if ((data == NULL) || (count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    result = SPI_ValidateDevice(device);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    if (device->bus->mosi_pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    state = SPI_FindBusState(device->bus);

    result = SPI_BeginTransfer(state, device, count);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    /*
     * Preload the first frame before asserting CSTART, as required by the
     * STM32H7 master-transfer sequence.
     */
    result = SPI_WaitForSet(
        &state->instance->SR,
        SPI_SR_TXP,
        device->timeout_ms);

    if (result != SPI_RESULT_OK)
    {
        return SPI_AbortTransfer(state, device, result);
    }

    SPI_WriteTXDR(
        state->instance,
        device->frame_size,
        SPI_LoadFrame(data, 0U, device->frame_size));

    state->instance->CR1 |= SPI_CR1_CSTART;

    for (index = 1U; index < count; index++)
    {
        result = SPI_WaitForSet(
            &state->instance->SR,
            SPI_SR_TXP,
            device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }

        SPI_WriteTXDR(
            state->instance,
            device->frame_size,
            SPI_LoadFrame(data, index, device->frame_size));

        if ((device->bus->miso_pin != SPI_PIN_UNUSED) &&
            ((state->instance->SR & SPI_SR_RXP) != 0U))
        {
            received_frame =
                SPI_ReadRXDR(state->instance, device->frame_size);
            (void)received_frame;
        }
    }

    if (device->bus->miso_pin != SPI_PIN_UNUSED)
    {
        uint32_t start_time;
        uint32_t effective_timeout;

        effective_timeout =
            SPI_GetEffectiveTimeout(device->timeout_ms);
        start_time = Time_GetMilliseconds();

        while ((state->instance->SR & SPI_SR_EOT) == 0U)
        {
            if ((state->instance->SR & SPI_SR_RXP) != 0U)
            {
                received_frame =
                    SPI_ReadRXDR(state->instance, device->frame_size);
                (void)received_frame;
            }

            if ((uint32_t)(Time_GetMilliseconds() - start_time) >=
                effective_timeout)
            {
                return SPI_AbortTransfer(
                    state,
                    device,
                    SPI_RESULT_TIMEOUT);
            }
        }
    }
    else
    {
        result = SPI_WaitForSet(
            &state->instance->SR,
            SPI_SR_EOT,
            device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }
    }

    state->instance->IFCR =
        SPI_IFCR_EOTC |
        SPI_IFCR_TXTFC;

    state->instance->CR1 &= ~SPI_CR1_SPE;
    (void)SPI_SetChipSelect(device, false);
    state->busy = false;

    return SPI_RESULT_OK;
}

SPI_Result SPI_Read(SPI_Device_Handle *device, void *data, size_t count)
{
    SPI_BusState *state;
    SPI_Result result;
    uint16_t idle_frame;
    uint16_t received_frame;
    size_t index;

    if ((data == NULL) || (count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    result = SPI_ValidateDevice(device);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    if (device->bus->miso_pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    state = SPI_FindBusState(device->bus);

    idle_frame = (uint16_t)SPI_GetFrameMask(device->frame_size);

    result = SPI_BeginTransfer(state, device, count);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    for (index = 0U; index < count; index++)
    {
        result = SPI_WaitForSet(&state->instance->SR, SPI_SR_TXP, device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }

        SPI_WriteTXDR(state->instance, device->frame_size, idle_frame);

        if (index == 0U)
        {
            state->instance->CR1 |= SPI_CR1_CSTART;
        }

        result = SPI_WaitForSet(&state->instance->SR, SPI_SR_RXP, device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }

        received_frame = SPI_ReadRXDR(state->instance, device->frame_size);
        SPI_StoreFrame(data, index, device->frame_size, received_frame);
    }

    return SPI_EndTransfer(state, device);
}

SPI_Result SPI_ReadWrite(SPI_Device_Handle *device, const void *tx_data, void *rx_data, size_t count)
{
    SPI_BusState *state;
    SPI_Result result;
    uint16_t received_frame;
    size_t index;

    if ((tx_data == NULL) || (rx_data == NULL) || (count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    result = SPI_ValidateDevice(device);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    if ((device->bus->mosi_pin == SPI_PIN_UNUSED) || (device->bus->miso_pin == SPI_PIN_UNUSED))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    state = SPI_FindBusState(device->bus);

    result = SPI_BeginTransfer(state, device, count);

    if (result != SPI_RESULT_OK)
    {
        return result;
    }

    for (index = 0U; index < count; index++)
    {
        result = SPI_WaitForSet(&state->instance->SR, SPI_SR_TXP, device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }

        SPI_WriteTXDR(
            state->instance,
            device->frame_size,
            SPI_LoadFrame(tx_data, index, device->frame_size));

        if (index == 0U)
        {
            state->instance->CR1 |= SPI_CR1_CSTART;
        }

        result = SPI_WaitForSet(&state->instance->SR, SPI_SR_RXP, device->timeout_ms);

        if (result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(state, device, result);
        }

        received_frame = SPI_ReadRXDR(state->instance, device->frame_size);
        SPI_StoreFrame(rx_data, index, device->frame_size, received_frame);
    }

    return SPI_EndTransfer(state, device);
}