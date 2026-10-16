/**
 * @file spi.c
 * @brief STM32H7A3 blocking SPI implementation.
 *
 * This driver implements the hardware-independent SPI contract using the
 * STM32H7A3 SPI peripheral in polling mode.
 */


#include "spi.h"

#include "stm32h7a3_defs.h"
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
    SPI_BusTypeDef *Handle;
    SPI_TypeDef *Instance;
    bool Initialized;
    bool Busy;
} SPI_BusStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static SPI_BusStateTypeDef SPI_BusRegistry[SPI_MAX_REGISTERED_BUSES];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *SPI_GetGPIOPort(SPI_PinTypeDef Pin);
static uint32_t SPI_GetGPIOPinNumber(SPI_PinTypeDef Pin);
static SPI_TypeDef *SPI_GetPeripheral(SPI_PortTypeDef Port);
static SPI_BusStateTypeDef *SPI_FindBusState(const SPI_BusTypeDef *Bus);
static SPI_BusStateTypeDef *SPI_AllocateBusState(SPI_BusTypeDef *Bus);
static SPI_ResultTypeDef SPI_ValidateBus(const SPI_BusTypeDef *Bus);
static SPI_ResultTypeDef SPI_ValidateDevice(const SPI_DeviceTypeDef *Device);
static SPI_ResultTypeDef SPI_GetAlternateFunction(SPI_PortTypeDef Port, SPI_PinTypeDef Pin, uint32_t *AlternateFunction);
static SPI_ResultTypeDef SPI_ConfigureSignalPin(SPI_PortTypeDef Port, SPI_PinTypeDef Pin);
static SPI_ResultTypeDef SPI_ConfigureChipSelectPin(const SPI_DeviceTypeDef *Device);
static SPI_ResultTypeDef SPI_SetChipSelect(const SPI_DeviceTypeDef *Device, bool Asserted);
static SPI_ResultTypeDef SPI_ApplyDeviceConfiguration(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device);
static SPI_ResultTypeDef SPI_BeginTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device, size_t Count);
static SPI_ResultTypeDef SPI_EndTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device);
static SPI_ResultTypeDef SPI_AbortTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device, SPI_ResultTypeDef Result);
static SPI_ResultTypeDef SPI_ClearStatusFlags(SPI_TypeDef *Instance);
static uint32_t SPI_GetEffectiveTimeout(uint32_t TimeoutMilliseconds);
static SPI_ResultTypeDef SPI_WaitForSet(volatile uint32_t *Reg, uint32_t Mask, uint32_t TimeoutMilliseconds);
static uint32_t SPI_GetBaudRateEncoding(uint32_t KernelFrequencyHz, uint32_t RequestedFrequencyHz);
static uint32_t SPI_GetFrameMask(SPI_FrameSizeTypeDef FrameSize);
static uint16_t SPI_LoadFrame(const void *Data, size_t Index, SPI_FrameSizeTypeDef FrameSize);
static void SPI_StoreFrame(void *Data, size_t Index, SPI_FrameSizeTypeDef FrameSize, uint16_t Frame);
static void SPI_WriteTXDR(SPI_TypeDef *Instance, SPI_FrameSizeTypeDef FrameSize, uint16_t Frame);
static uint16_t SPI_ReadRXDR(SPI_TypeDef *Instance, SPI_FrameSizeTypeDef FrameSize);

/* -------------------------------------------------------------------------- */
/* GPIO helpers                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *SPI_GetGPIOPort(SPI_PinTypeDef Pin)
{
    uint32_t PortIndex;

    if(Pin == SPI_PIN_UNUSED)
    {
        return NULL;
    }

    PortIndex = ((uint32_t)Pin >> 4U) & 0x0FU;

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

        case 9U:
            return GPIOJ;

        case 10U:
            return GPIOK;

        default:
            return NULL;
    }
}

static uint32_t SPI_GetGPIOPinNumber(SPI_PinTypeDef Pin)
{
    return (uint32_t)Pin & 0x0FU;
}

/* -------------------------------------------------------------------------- */
/* Peripheral helpers                                                         */
/* -------------------------------------------------------------------------- */

static SPI_TypeDef *SPI_GetPeripheral(SPI_PortTypeDef Port)
{
    switch(Port)
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

static SPI_BusStateTypeDef *SPI_FindBusState(const SPI_BusTypeDef *Bus)
{
    size_t Index;

    if(Bus == NULL)
    {
        return NULL;
    }

    for(Index = 0U; Index < SPI_MAX_REGISTERED_BUSES; Index++)
    {
        if(SPI_BusRegistry[Index].Initialized && (SPI_BusRegistry[Index].Handle == Bus))
        {
            return &SPI_BusRegistry[Index];
        }
    }

    return NULL;
}

static SPI_BusStateTypeDef *SPI_AllocateBusState(SPI_BusTypeDef *Bus)
{
    size_t Index;

    for(Index = 0U; Index < SPI_MAX_REGISTERED_BUSES; Index++)
    {
        if(!SPI_BusRegistry[Index].Initialized)
        {
            SPI_BusRegistry[Index].Handle = Bus;
            SPI_BusRegistry[Index].Instance = NULL;
            SPI_BusRegistry[Index].Busy = false;

            return &SPI_BusRegistry[Index];
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static SPI_ResultTypeDef SPI_ValidateBus(const SPI_BusTypeDef *Bus)
{
    if(Bus == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(Bus->SclkPin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Bus->MosiPin == SPI_PIN_UNUSED) && (Bus->MisoPin == SPI_PIN_UNUSED))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(SPI_GetPeripheral(Bus->Port) == NULL)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    if(SPI_GetGPIOPort(Bus->SclkPin) == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Bus->MosiPin != SPI_PIN_UNUSED) && (SPI_GetGPIOPort(Bus->MosiPin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Bus->MisoPin != SPI_PIN_UNUSED) && (SPI_GetGPIOPort(Bus->MisoPin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    return SPI_RESULT_OK;
}

static SPI_ResultTypeDef SPI_ValidateDevice(const SPI_DeviceTypeDef *Device)
{
    if((Device == NULL) || (Device->Bus == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(SPI_FindBusState(Device->Bus) == NULL)
    {
        return SPI_RESULT_NOT_INITIALIZED;
    }

    if(Device->FrequencyHz == 0U)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(Device->Mode > SPI_MODE_3)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Device->BitOrder != SPI_BIT_ORDER_MSB_FIRST) && (Device->BitOrder != SPI_BIT_ORDER_LSB_FIRST))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Device->FrameSize != SPI_FRAME_SIZE_8_BIT) &&
       (Device->FrameSize != SPI_FRAME_SIZE_9_BIT) &&
       (Device->FrameSize != SPI_FRAME_SIZE_16_BIT))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    if((Device->ChipSelectPolarity != SPI_CHIP_SELECT_ACTIVE_LOW) &&
       (Device->ChipSelectPolarity != SPI_CHIP_SELECT_ACTIVE_HIGH))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if((Device->ChipSelectPin != SPI_PIN_UNUSED) &&
       (SPI_GetGPIOPort(Device->ChipSelectPin) == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Alternate-function mapping                                                 */
/* -------------------------------------------------------------------------- */

static SPI_ResultTypeDef SPI_GetAlternateFunction(SPI_PortTypeDef Port, SPI_PinTypeDef Pin, uint32_t *AlternateFunction)
{
    if((Pin == SPI_PIN_UNUSED) || (AlternateFunction == NULL))
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

    switch(Port)
    {
        case SPI_PORT_1:
            switch(Pin)
            {
                case PA5:
                case PA6:
                case PA7:
                    * AlternateFunction = 5U;
                    return SPI_RESULT_OK;

                default:
                    return SPI_RESULT_UNSUPPORTED;
            }

        case SPI_PORT_2:
            switch(Pin)
            {
                case PB13:
                case PB14:
                case PB15:
                case PC3:
                    * AlternateFunction = 5U;
                    return SPI_RESULT_OK;

                default:
                    return SPI_RESULT_UNSUPPORTED;
            }

        case SPI_PORT_3:
            switch(Pin)
            {
                case PC10:
                case PC11:
                case PC12:
                    * AlternateFunction = 6U;
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

static SPI_ResultTypeDef SPI_ConfigureSignalPin(SPI_PortTypeDef Port, SPI_PinTypeDef Pin)
{
    GPIO_TypeDef *GPIO;
    uint32_t PinNumber;
    uint32_t AlternateFunction;
    uint32_t AFRIndex;
    uint32_t AFRPosition;
    SPI_ResultTypeDef Result;

    if(Pin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    GPIO = SPI_GetGPIOPort(Pin);

    if(GPIO == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Result = SPI_GetAlternateFunction(Port, Pin, &AlternateFunction);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    if(RCC_EnablePeripheralClock(GPIO) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    PinNumber = SPI_GetGPIOPinNumber(Pin);
    AFRIndex = PinNumber / 8U;
    AFRPosition = (PinNumber % 8U) * 4U;

    GPIO->MODER &= ~(0x3UL << (PinNumber * 2U));
    GPIO->MODER |= SPI_GPIO_MODE_ALTERNATE << (PinNumber * 2U);

    GPIO->OTYPER &= ~(1UL << PinNumber);

    GPIO->OSPEEDR &= ~(0x3UL << (PinNumber * 2U));
    GPIO->OSPEEDR |= SPI_GPIO_SPEED_VERY_HIGH << (PinNumber * 2U);

    GPIO->PUPDR &= ~(0x3UL << (PinNumber * 2U));

    GPIO->AFR[AFRIndex] &= ~(0xFUL << AFRPosition);
    GPIO->AFR[AFRIndex] |= AlternateFunction << AFRPosition;

    return SPI_RESULT_OK;
}

static SPI_ResultTypeDef SPI_ConfigureChipSelectPin(const SPI_DeviceTypeDef *Device)
{
    GPIO_TypeDef *GPIO;
    uint32_t PinNumber;

    if(Device->ChipSelectPin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    GPIO = SPI_GetGPIOPort(Device->ChipSelectPin);

    if(GPIO == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(RCC_EnablePeripheralClock(GPIO) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    PinNumber = SPI_GetGPIOPinNumber(Device->ChipSelectPin);

    /*
    * Establish the inactive output value before changing the pin into output
    * mode to avoid a chip-select pulse during initialization.
    */
    if(SPI_SetChipSelect(Device, false) != SPI_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    GPIO->MODER &= ~(0x3UL << (PinNumber * 2U));
    GPIO->MODER |= SPI_GPIO_MODE_OUTPUT << (PinNumber * 2U);

    GPIO->OTYPER &= ~(1UL << PinNumber);

    GPIO->OSPEEDR &= ~(0x3UL << (PinNumber * 2U));
    GPIO->OSPEEDR |= SPI_GPIO_SPEED_VERY_HIGH << (PinNumber * 2U);

    GPIO->PUPDR &= ~(0x3UL << (PinNumber * 2U));

    return SPI_RESULT_OK;
}

static SPI_ResultTypeDef SPI_SetChipSelect(const SPI_DeviceTypeDef *Device, bool Asserted)
{
    GPIO_TypeDef *GPIO;
    uint32_t PinMask;
    bool DriveHigh;

    if(Device->ChipSelectPin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_OK;
    }

    GPIO = SPI_GetGPIOPort(Device->ChipSelectPin);

    if(GPIO == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    PinMask = 1UL << SPI_GetGPIOPinNumber(Device->ChipSelectPin);

    if(Device->ChipSelectPolarity == SPI_CHIP_SELECT_ACTIVE_HIGH)
    {
        DriveHigh = Asserted;
    }
    else
    {
        DriveHigh = !Asserted;
    }

    GPIO->BSRR = DriveHigh ? PinMask : (PinMask << 16U);

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Timeout helpers                                                            */
/* -------------------------------------------------------------------------- */

static uint32_t SPI_GetEffectiveTimeout(uint32_t TimeoutMilliseconds)
{
    return (TimeoutMilliseconds == 0U) ? SPI_DEFAULT_TIMEOUT_MS : TimeoutMilliseconds;
}

static SPI_ResultTypeDef SPI_WaitForSet(volatile uint32_t *Reg, uint32_t Mask, uint32_t TimeoutMilliseconds)
{
    uint32_t StartTime;
    uint32_t EffectiveTimeout;

    if(Reg == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    EffectiveTimeout = SPI_GetEffectiveTimeout(TimeoutMilliseconds);
    StartTime = Time_GetMilliseconds();

    while((*Reg & Mask) == 0U)
    {
        if((uint32_t)(Time_GetMilliseconds() - StartTime) >= EffectiveTimeout)
        {
            return SPI_RESULT_TIMEOUT;
        }
    }

    return SPI_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Peripheral configuration                                                   */
/* -------------------------------------------------------------------------- */

static uint32_t SPI_GetBaudRateEncoding(uint32_t KernelFrequencyHz, uint32_t RequestedFrequencyHz)
{
    uint32_t Divider;
    uint32_t Encoding;

    Divider = 2U;
    Encoding = 0U;

    while((Encoding < 7U) && ((KernelFrequencyHz / Divider) > RequestedFrequencyHz))
    {
        Divider <<= 1U;
        Encoding++;
    }

    return Encoding;
}

static uint32_t SPI_GetFrameMask(SPI_FrameSizeTypeDef FrameSize)
{
    switch(FrameSize)
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

static SPI_ResultTypeDef SPI_ClearStatusFlags(SPI_TypeDef *Instance)
{
    if(Instance == NULL)
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Instance->IFCR =
        SPI_IFCR_EOTC |
        SPI_IFCR_TXTFC |
        SPI_IFCR_OVRC |
        SPI_IFCR_UDRC |
        SPI_IFCR_TIFREC |
        SPI_IFCR_CRCEC |
        SPI_IFCR_SUSPC;

    return SPI_RESULT_OK;
}

static SPI_ResultTypeDef SPI_ApplyDeviceConfiguration(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device)
{
    SPI_TypeDef *Instance;
    uint32_t KernelFrequencyHz;
    uint32_t BaudRate;
    uint32_t Cfg1;
    uint32_t Cfg2;

    if((State == NULL) || (Device == NULL))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Instance = State->Instance;

    if((Instance->CR1 & SPI_CR1_SPE) != 0U)
    {
        return SPI_RESULT_BUSY;
    }

    KernelFrequencyHz = RCC_GetKernelFrequency(Instance);

    if(KernelFrequencyHz == 0U)
    {
        return SPI_RESULT_IO_ERROR;
    }

    BaudRate = SPI_GetBaudRateEncoding(KernelFrequencyHz, Device->FrequencyHz);

    Cfg1 = 0U;
    Cfg1 |= (((uint32_t)Device->FrameSize - 1U) << SPI_CFG1_DSIZE_Pos) & SPI_CFG1_DSIZE_Msk;
    Cfg1 |= (BaudRate << SPI_CFG1_MBR_Pos) & SPI_CFG1_MBR_Msk;
    Instance->CFG1 = Cfg1;

    Cfg2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR;

    if((Device->Bus->MosiPin != SPI_PIN_UNUSED) && (Device->Bus->MisoPin == SPI_PIN_UNUSED))
    {
        Cfg2 |= SPI_CFG2_COMM_0;
    }

    switch(Device->Mode)
    {
        case SPI_MODE_0:
            break;

        case SPI_MODE_1:
            Cfg2 |= SPI_CFG2_CPHA;
            break;

        case SPI_MODE_2:
            Cfg2 |= SPI_CFG2_CPOL;
            break;

        case SPI_MODE_3:
            Cfg2 |= SPI_CFG2_CPOL | SPI_CFG2_CPHA;
            break;

        default:
            return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(Device->BitOrder == SPI_BIT_ORDER_LSB_FIRST)
    {
        Cfg2 |= SPI_CFG2_LSBFRST;
    }

    Instance->CFG2 = Cfg2;
    Instance->CR1 = SPI_CR1_SSI;
    Instance->CR2 = 0U;
    Instance->IER = 0U;

    return SPI_ClearStatusFlags(Instance);
}

/* -------------------------------------------------------------------------- */
/* Frame access                                                               */
/* -------------------------------------------------------------------------- */

static uint16_t SPI_LoadFrame(const void *Data, size_t Index, SPI_FrameSizeTypeDef FrameSize)
{
    if(FrameSize == SPI_FRAME_SIZE_8_BIT)
    {
        return ((const uint8_t *)Data)[Index];
    }

    return ((const uint16_t *)Data)[Index] & (uint16_t)SPI_GetFrameMask(FrameSize);
}

static void SPI_StoreFrame(void *Data, size_t Index, SPI_FrameSizeTypeDef FrameSize, uint16_t Frame)
{
    Frame &= (uint16_t)SPI_GetFrameMask(FrameSize);

    if(FrameSize == SPI_FRAME_SIZE_8_BIT)
    {
        ((uint8_t *)Data)[Index] = (uint8_t)Frame;
    }
    else
    {
        ((uint16_t *)Data)[Index] = Frame;
    }
}

static void SPI_WriteTXDR(SPI_TypeDef *Instance, SPI_FrameSizeTypeDef FrameSize, uint16_t Frame)
{
    if(FrameSize == SPI_FRAME_SIZE_9_BIT)
    {
        * (volatile uint16_t *)&Instance->TXDR = Frame;
    }
    else
    {
        * (volatile uint8_t *)&Instance->TXDR = (uint8_t)Frame;
    }
}

static uint16_t SPI_ReadRXDR(SPI_TypeDef *Instance, SPI_FrameSizeTypeDef FrameSize)
{
    if(FrameSize == SPI_FRAME_SIZE_9_BIT)
    {
        return *(volatile uint16_t *)&Instance->RXDR;
    }

    return *(volatile uint8_t *)&Instance->RXDR;
}

/* -------------------------------------------------------------------------- */
/* Transfer control                                                           */
/* -------------------------------------------------------------------------- */

static SPI_ResultTypeDef SPI_BeginTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device, size_t Count)
{
    SPI_ResultTypeDef Result;

    if((State == NULL) || (Device == NULL) || (Count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    if(State->Busy)
    {
        return SPI_RESULT_BUSY;
    }

    if(Count > (size_t)(SPI_CR2_TSIZE_Msk >> SPI_CR2_TSIZE_Pos))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    State->Busy = true;

    Result = SPI_ApplyDeviceConfiguration(State, Device);

    if(Result != SPI_RESULT_OK)
    {
        State->Busy = false;
        return Result;
    }

    Result = SPI_SetChipSelect(Device, true);

    if(Result != SPI_RESULT_OK)
    {
        State->Busy = false;
        return Result;
    }

    State->Instance->IFCR = 0xFFFFFFFFUL;
    State->Instance->CR2 =
        ((uint32_t)Count << SPI_CR2_TSIZE_Pos) &
        SPI_CR2_TSIZE_Msk;
    State->Instance->CR1 |= SPI_CR1_SPE;

    return SPI_RESULT_OK;
}

static SPI_ResultTypeDef SPI_EndTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device)
{
    SPI_ResultTypeDef Result;

    Result = SPI_WaitForSet(&State->Instance->SR, SPI_SR_EOT, Device->TimeoutMilliseconds);

    State->Instance->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    State->Instance->CR1 &= ~SPI_CR1_SPE;

    (void)SPI_SetChipSelect(Device, false);

    State->Busy = false;

    return Result;
}

static SPI_ResultTypeDef SPI_AbortTransfer(SPI_BusStateTypeDef *State, const SPI_DeviceTypeDef *Device, SPI_ResultTypeDef Result)
{
    if((State != NULL) && (State->Instance != NULL))
    {
        State->Instance->CR1 &= ~SPI_CR1_SPE;
        (void)SPI_ClearStatusFlags(State->Instance);
        State->Busy = false;
    }

    if(Device != NULL)
    {
        (void)SPI_SetChipSelect(Device, false);
    }

    return Result;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

SPI_ResultTypeDef SPI_BusInit(SPI_BusTypeDef *Bus)
{
    SPI_BusStateTypeDef *State;
    SPI_TypeDef *Instance;
    SPI_ResultTypeDef Result;

    Result = SPI_ValidateBus(Bus);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    if(SPI_FindBusState(Bus) != NULL)
    {
        return SPI_RESULT_OK;
    }

    Instance = SPI_GetPeripheral(Bus->Port);
    State = SPI_AllocateBusState(Bus);

    if(State == NULL)
    {
        return SPI_RESULT_BUSY;
    }

    if(RCC_EnablePeripheralClock(Instance) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    if(RCC_ResetPeripheral(Instance) != RCC_RESULT_OK)
    {
        return SPI_RESULT_IO_ERROR;
    }

    /*
     * Keep SSI asserted while software slave management is enabled to prevent
     * the peripheral from entering a mode-fault condition.
     */
    Instance->CR1 &= ~SPI_CR1_SPE;
    Instance->CR1 = SPI_CR1_SSI;
    Instance->CR2 = 0U;
    Instance->CFG1 = 0U;
    Instance->CFG2 = 0U;
    Instance->IER = 0U;
    Instance->IFCR = 0xFFFFFFFFUL;

    Result = SPI_ConfigureSignalPin(Bus->Port, Bus->SclkPin);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    Result = SPI_ConfigureSignalPin(Bus->Port, Bus->MosiPin);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    Result = SPI_ConfigureSignalPin(Bus->Port, Bus->MisoPin);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    State->Instance = Instance;
    State->Initialized = true;
    State->Busy = false;

    return SPI_RESULT_OK;
}

SPI_ResultTypeDef SPI_DeviceInit(SPI_DeviceTypeDef *Device)
{
    SPI_ResultTypeDef Result;

    Result = SPI_ValidateDevice(Device);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    return SPI_ConfigureChipSelectPin(Device);
}

SPI_ResultTypeDef SPI_Write(SPI_DeviceTypeDef *Device, const void *Data, size_t Count)
{
    SPI_BusStateTypeDef *State;
    SPI_ResultTypeDef Result;
    uint16_t ReceivedFrame;
    size_t Index;

    if((Data == NULL) || (Count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Result = SPI_ValidateDevice(Device);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    if(Device->Bus->MosiPin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    State = SPI_FindBusState(Device->Bus);

    Result = SPI_BeginTransfer(State, Device, Count);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    /*
     * Preload the first frame before asserting CSTART, as required by the
     * STM32H7 master-transfer sequence.
     */
    Result = SPI_WaitForSet(
        &State->Instance->SR,
        SPI_SR_TXP,
        Device->TimeoutMilliseconds);

    if(Result != SPI_RESULT_OK)
    {
        return SPI_AbortTransfer(State, Device, Result);
    }

    SPI_WriteTXDR(
        State->Instance,
        Device->FrameSize,
        SPI_LoadFrame(Data, 0U, Device->FrameSize));

    State->Instance->CR1 |= SPI_CR1_CSTART;

    for(Index = 1U; Index < Count; Index++)
    {
        Result = SPI_WaitForSet(
            &State->Instance->SR,
            SPI_SR_TXP,
            Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }

        SPI_WriteTXDR(
            State->Instance,
            Device->FrameSize,
            SPI_LoadFrame(Data, Index, Device->FrameSize));

        if((Device->Bus->MisoPin != SPI_PIN_UNUSED) &&
           ((State->Instance->SR & SPI_SR_RXP) != 0U))
        {
            ReceivedFrame =
                SPI_ReadRXDR(State->Instance, Device->FrameSize);
            (void)ReceivedFrame;
        }
    }

    if(Device->Bus->MisoPin != SPI_PIN_UNUSED)
    {
        uint32_t StartTime;
        uint32_t EffectiveTimeout;

        EffectiveTimeout =
            SPI_GetEffectiveTimeout(Device->TimeoutMilliseconds);
        StartTime = Time_GetMilliseconds();

        while((State->Instance->SR & SPI_SR_EOT) == 0U)
        {
            if((State->Instance->SR & SPI_SR_RXP) != 0U)
            {
                ReceivedFrame =
                    SPI_ReadRXDR(State->Instance, Device->FrameSize);
                (void)ReceivedFrame;
            }

            if((uint32_t)(Time_GetMilliseconds() - StartTime) >=
               EffectiveTimeout)
            {
                return SPI_AbortTransfer(
                    State,
                    Device,
                    SPI_RESULT_TIMEOUT);
            }
        }
    }
    else
    {
        Result = SPI_WaitForSet(
            &State->Instance->SR,
            SPI_SR_EOT,
            Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }
    }

    State->Instance->IFCR =
        SPI_IFCR_EOTC |
        SPI_IFCR_TXTFC;

    State->Instance->CR1 &= ~SPI_CR1_SPE;
    (void)SPI_SetChipSelect(Device, false);
    State->Busy = false;

    return SPI_RESULT_OK;
}

SPI_ResultTypeDef SPI_Read(SPI_DeviceTypeDef *Device, void *Data, size_t Count)
{
    SPI_BusStateTypeDef *State;
    SPI_ResultTypeDef Result;
    uint16_t IdleFrame;
    uint16_t ReceivedFrame;
    size_t Index;

    if((Data == NULL) || (Count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Result = SPI_ValidateDevice(Device);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    if(Device->Bus->MisoPin == SPI_PIN_UNUSED)
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    State = SPI_FindBusState(Device->Bus);

    IdleFrame = (uint16_t)SPI_GetFrameMask(Device->FrameSize);

    Result = SPI_BeginTransfer(State, Device, Count);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    for(Index = 0U; Index < Count; Index++)
    {
        Result = SPI_WaitForSet(&State->Instance->SR, SPI_SR_TXP, Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }

        SPI_WriteTXDR(State->Instance, Device->FrameSize, IdleFrame);

        if(Index == 0U)
        {
            State->Instance->CR1 |= SPI_CR1_CSTART;
        }

        Result = SPI_WaitForSet(&State->Instance->SR, SPI_SR_RXP, Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }

        ReceivedFrame = SPI_ReadRXDR(State->Instance, Device->FrameSize);
        SPI_StoreFrame(Data, Index, Device->FrameSize, ReceivedFrame);
    }

    return SPI_EndTransfer(State, Device);
}

SPI_ResultTypeDef SPI_ReadWrite(SPI_DeviceTypeDef *Device, const void *TxData, void *RxData, size_t Count)
{
    SPI_BusStateTypeDef *State;
    SPI_ResultTypeDef Result;
    uint16_t ReceivedFrame;
    size_t Index;

    if((TxData == NULL) || (RxData == NULL) || (Count == 0U))
    {
        return SPI_RESULT_INVALID_ARGUMENT;
    }

    Result = SPI_ValidateDevice(Device);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    if((Device->Bus->MosiPin == SPI_PIN_UNUSED) || (Device->Bus->MisoPin == SPI_PIN_UNUSED))
    {
        return SPI_RESULT_UNSUPPORTED;
    }

    State = SPI_FindBusState(Device->Bus);

    Result = SPI_BeginTransfer(State, Device, Count);

    if(Result != SPI_RESULT_OK)
    {
        return Result;
    }

    for(Index = 0U; Index < Count; Index++)
    {
        Result = SPI_WaitForSet(&State->Instance->SR, SPI_SR_TXP, Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }

        SPI_WriteTXDR(
            State->Instance,
            Device->FrameSize,
            SPI_LoadFrame(TxData, Index, Device->FrameSize));

        if(Index == 0U)
        {
            State->Instance->CR1 |= SPI_CR1_CSTART;
        }

        Result = SPI_WaitForSet(&State->Instance->SR, SPI_SR_RXP, Device->TimeoutMilliseconds);

        if(Result != SPI_RESULT_OK)
        {
            return SPI_AbortTransfer(State, Device, Result);
        }

        ReceivedFrame = SPI_ReadRXDR(State->Instance, Device->FrameSize);
        SPI_StoreFrame(RxData, Index, Device->FrameSize, ReceivedFrame);
    }

    return SPI_EndTransfer(State, Device);
}
