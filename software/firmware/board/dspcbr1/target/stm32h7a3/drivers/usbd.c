/**
 * @file usbd.c
 * @brief STM32H7A3 USB device-controller implementation.
 *
 * This driver implements the hardware-independent USB device-controller
 * contract using the OTG_HS peripheral with its internal full-speed PHY in
 * slave (non-DMA) mode. Endpoint FIFOs are serviced from USBD_IRQHandler().
 */

#include "usbd.h"

#include "delay.h"
#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define USBD_IRQ_PRIORITY                   5U
#define USBD_TIMEOUT_ITERATIONS             1000000UL
#define USBD_ROLE_SETTLE_MS                 50U
#define USBD_REMOTE_WAKEUP_SIGNAL_MS        5U

/* Clock recovery: count 48 MHz HSI48 cycles between 1 kHz USB SOF packets. */
#define USBD_CRS_SYNC_SOURCE_USB1_SOF       2U
#define USBD_CRS_RELOAD                     ((48000000UL / 1000UL) - 1UL)
#define USBD_CRS_FREQUENCY_ERROR_LIMIT      34U

/* Turnaround time for full speed with an AHB clock of at least 32 MHz. */
#define USBD_TURNAROUND_TIME                6U

/* FIFO RAM allocation in 32-bit words. OTG_HS provides 1024 words. */
#define USBD_RX_FIFO_WORDS                  256U
#define USBD_TX0_FIFO_WORDS                 64U
#define USBD_TX_FIFO_WORDS                  64U

#define USBD_FULL_SPEED_MAX_PACKET_SIZE     64U
#define USBD_FULL_SPEED_MAX_ISO_PACKET_SIZE 1023U
#define USBD_MAX_PACKETS_PER_TRANSFER       1023U
#define USBD_ALL_TX_FIFOS                   0x10U

#define USBD_EP0_SETUP_SIZE                 8U
#define USBD_EP0_SETUP_PACKET_COUNT         3U

#define USBD_PKTSTS_OUT_DATA                2U
#define USBD_PKTSTS_SETUP_DATA              6U

/* PA9 VBUS sense, PA10 OTG-ID (shared with LCD chip select), PA11 D-, PA12 D+. */
#define USBD_VBUS_PIN                       9U
#define USBD_ID_PIN                         10U
#define USBD_DM_PIN                         11U
#define USBD_DP_PIN                         12U

#define USBD_GPIO_MODE_INPUT                0U
#define USBD_GPIO_MODE_OUTPUT               1U
#define USBD_GPIO_MODE_ALTERNATE            2U
#define USBD_GPIO_SPEED_VERY_HIGH           3U
#define USBD_GPIO_ALTERNATE_FUNCTION        10U

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Runtime state of one IN endpoint.
 *
 * A transfer is programmed into the controller in chunks ending at chunk_end.
 * EP0 chunks are one packet long; other endpoints use one chunk unless the
 * transfer exceeds the controller's packet-count limit.
 */
typedef struct
{
    const uint8_t *Buffer;
    uint16_t Length;
    uint16_t Written;
    uint16_t ChunkEnd;
    uint16_t MaxPacket;
    bool Busy;
} USBD_InEndpointStateTypeDef;

/**
 * @brief Runtime state of one OUT endpoint.
 */
typedef struct
{
    uint8_t *Buffer;
    uint16_t Length;
    uint16_t Received;
    uint16_t ChunkEnd;
    uint16_t MaxPacket;
    bool Isochronous;
    bool Busy;
} USBD_OutEndpointStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBD_CallbacksTypeDef *USBD_RegisteredCallbacks;
static void *USBD_CallbackContext;
static USBD_InEndpointStateTypeDef USBD_InState[USBD_MAX_ENDPOINTS];
static USBD_OutEndpointStateTypeDef USBD_OutState[USBD_MAX_ENDPOINTS];
static uint8_t USBD_SetupBuffer[USBD_EP0_SETUP_SIZE];
static uint16_t USBD_EP0MaxPacketSize;
static bool USBD_Initialized;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static USB_OTG_DeviceTypeDef *USBD_Device(void);
static USB_OTG_INEndpointTypeDef *USBD_InEndpoint(uint8_t Number);
static USB_OTG_OUTEndpointTypeDef *USBD_OutEndpoint(uint8_t Number);
static volatile uint32_t *USBD_Fifo(uint8_t Number);
static uint32_t USBD_EnterCritical(void);
static void USBD_ExitCritical(uint32_t Primask);
static bool USBD_WaitForClear(volatile uint32_t *Reg, uint32_t Mask);
static bool USBD_WaitForSet(volatile uint32_t *Reg, uint32_t Mask);
static void USBD_ConfigurePin(uint32_t PinNumber, uint32_t Mode);
static void USBD_ConfigurePins(void);
static void USBD_ConfigureFifos(void);
static void USBD_FlushRxFifo(void);
static void USBD_FlushTxFifo(uint8_t Number);
static void USBD_WriteFifo(uint8_t Number, const uint8_t *Data, uint16_t Length);
static void USBD_ReadFifo(uint8_t *Data, uint16_t Count, uint16_t Capacity);
static void USBD_UpdateStartOfFrameInterrupt(void);
static bool USBD_EncodeEP0MaxPacketSize(uint16_t MaxPacketSize, uint32_t *Encoding);
static USBD_ResultTypeDef USBD_ActivateEndpoint(uint8_t EndpointAddress, USBD_EndpointTypeTypeDef Type, uint16_t MaxPacketSize);
static void USBD_DeactivateEndpoint(uint8_t EndpointAddress);
static uint16_t USBD_GetChunkLimit(uint8_t Number, uint16_t MaxPacket);
static void USBD_ArmSetup(void);
static void USBD_StartInChunk(uint8_t Number);
static void USBD_FillTxFifo(uint8_t Number);
static void USBD_StartOutChunk(uint8_t Number);
static void USBD_HandleIncompleteIsochronousOut(void);
static void USBD_HandleRxFifoLevel(void);
static void USBD_HandleBusReset(void);
static void USBD_HandleSetup(void);
static void USBD_HandleOutEndpointInterrupt(uint8_t Number);
static void USBD_HandleInEndpointInterrupt(uint8_t Number);

/* -------------------------------------------------------------------------- */
/* Register helpers                                                           */
/* -------------------------------------------------------------------------- */

static USB_OTG_DeviceTypeDef *USBD_Device(void)
{
    return (USB_OTG_DeviceTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
}

static USB_OTG_INEndpointTypeDef *USBD_InEndpoint(uint8_t Number)
{
    return (USB_OTG_INEndpointTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE + ((uint32_t)Number * USB_OTG_EP_REG_SIZE));
}

static USB_OTG_OUTEndpointTypeDef *USBD_OutEndpoint(uint8_t Number)
{
    return (USB_OTG_OUTEndpointTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE + ((uint32_t)Number * USB_OTG_EP_REG_SIZE));
}

static volatile uint32_t *USBD_Fifo(uint8_t Number)
{
    return (volatile uint32_t *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_FIFO_BASE + ((uint32_t)Number * USB_OTG_FIFO_SIZE));
}

static uint32_t USBD_EnterCritical(void)
{
    uint32_t Primask = __get_PRIMASK();

    __disable_irq();

    return Primask;
}

static void USBD_ExitCritical(uint32_t Primask)
{
    __set_PRIMASK(Primask);
}

static bool USBD_WaitForClear(volatile uint32_t *Reg, uint32_t Mask)
{
    uint32_t Timeout = USBD_TIMEOUT_ITERATIONS;

    while(((*Reg & Mask) != 0U) && (Timeout != 0U))
    {
        Timeout--;
    }

    return Timeout != 0U;
}

static bool USBD_WaitForSet(volatile uint32_t *Reg, uint32_t Mask)
{
    uint32_t Timeout = USBD_TIMEOUT_ITERATIONS;

    while(((*Reg & Mask) != Mask) && (Timeout != 0U))
    {
        Timeout--;
    }

    return Timeout != 0U;
}

/* -------------------------------------------------------------------------- */
/* Pin configuration                                                          */
/* -------------------------------------------------------------------------- */

static void USBD_ConfigurePin(uint32_t PinNumber, uint32_t Mode)
{
    uint32_t AFRIndex = PinNumber / 8U;
    uint32_t AFRPosition = (PinNumber % 8U) * 4U;

    GPIOA->MODER &= ~(0x3UL << (PinNumber * 2U));
    GPIOA->MODER |= Mode << (PinNumber * 2U);

    GPIOA->OTYPER &= ~(1UL << PinNumber);
    GPIOA->PUPDR &= ~(0x3UL << (PinNumber * 2U));

    if(Mode == USBD_GPIO_MODE_ALTERNATE)
    {
        GPIOA->OSPEEDR &= ~(0x3UL << (PinNumber * 2U));
        GPIOA->OSPEEDR |= USBD_GPIO_SPEED_VERY_HIGH << (PinNumber * 2U);

        GPIOA->AFR[AFRIndex] &= ~(0xFUL << AFRPosition);
        GPIOA->AFR[AFRIndex] |= USBD_GPIO_ALTERNATE_FUNCTION << AFRPosition;
    }
}

static void USBD_ConfigurePins(void)
{
    (void)RCC_EnablePeripheralClock(GPIOA);

    /*
     * PA9 is consumed directly by the OTG VBUS detector and must remain a
     * floating GPIO input.
     */
    USBD_ConfigurePin(USBD_VBUS_PIN, USBD_GPIO_MODE_INPUT);

    /*
     * PA10 is physically USB OTG-ID, but on DualSlide it is also the LCD chip
     * select. Its inactive (high) level selects the USB peripheral role and
     * keeps the LCD deselected during USB startup. The level is set before
     * the pin becomes an output to avoid a chip-select pulse.
     */
    GPIOA->BSRR = 1UL << USBD_ID_PIN;
    USBD_ConfigurePin(USBD_ID_PIN, USBD_GPIO_MODE_OUTPUT);

    /* Only D- and D+ use the OTG alternate function. */
    USBD_ConfigurePin(USBD_DM_PIN, USBD_GPIO_MODE_ALTERNATE);
    USBD_ConfigurePin(USBD_DP_PIN, USBD_GPIO_MODE_ALTERNATE);
}

/* -------------------------------------------------------------------------- */
/* FIFO helpers                                                               */
/* -------------------------------------------------------------------------- */

static void USBD_ConfigureFifos(void)
{
    uint32_t Start = USBD_RX_FIFO_WORDS + USBD_TX0_FIFO_WORDS;
    uint8_t Number;

    USB1_OTG_HS->GRXFSIZ = USBD_RX_FIFO_WORDS;
    USB1_OTG_HS->DIEPTXF0_HNPTXFSIZ = (USBD_TX0_FIFO_WORDS << 16U) | USBD_RX_FIFO_WORDS;

    for(Number = 1U; Number < USBD_MAX_ENDPOINTS; Number++)
    {
        USB1_OTG_HS->DIEPTXF[Number - 1U] = (USBD_TX_FIFO_WORDS << 16U) | Start;
        Start += USBD_TX_FIFO_WORDS;
    }

    USBD_FlushRxFifo();
    USBD_FlushTxFifo(USBD_ALL_TX_FIFOS);
}

static void USBD_FlushRxFifo(void)
{
    USB1_OTG_HS->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    (void)USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_RXFFLSH);
}

static void USBD_FlushTxFifo(uint8_t Number)
{
    USB1_OTG_HS->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | ((uint32_t)Number << USB_OTG_GRSTCTL_TXFNUM_Pos);
    (void)USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_TXFFLSH);
}

static void USBD_WriteFifo(uint8_t Number, const uint8_t *Data, uint16_t Length)
{
    volatile uint32_t *FIFO = USBD_Fifo(Number);
    uint16_t Index;
    uint16_t Byte;

    for(Index = 0U; Index < Length; Index += 4U)
    {
        uint32_t Word = 0U;

        for(Byte = 0U; (Byte < 4U) && ((uint16_t)(Index + Byte) < Length); Byte++)
        {
            Word |= (uint32_t)Data[Index + Byte] << (8U * Byte);
        }

        *FIFO = Word;
    }
}

/*
 * Pops one received packet of count bytes from the shared RX FIFO. Bytes
 * beyond capacity are popped and discarded so the FIFO stays word-aligned.
 */
static void USBD_ReadFifo(uint8_t *Data, uint16_t Count, uint16_t Capacity)
{
    volatile uint32_t *FIFO = USBD_Fifo(0U);
    uint16_t Index;
    uint16_t Byte;

    for(Index = 0U; Index < Count; Index += 4U)
    {
        uint32_t Word = *FIFO;

        for(Byte = 0U; (Byte < 4U) && ((uint16_t)(Index + Byte) < Count); Byte++)
        {
            if((Data != NULL) && ((uint16_t)(Index + Byte) < Capacity))
            {
                Data[Index + Byte] = (uint8_t)(Word >> (8U * Byte));
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Endpoint helpers                                                           */
/* -------------------------------------------------------------------------- */

static void USBD_UpdateStartOfFrameInterrupt(void)
{
    if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->StartOfFrame != NULL))
    {
        USB1_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_SOFM;
    }
    else
    {
        USB1_OTG_HS->GINTMSK &= ~USB_OTG_GINTMSK_SOFM;
    }
}

static bool USBD_EncodeEP0MaxPacketSize(uint16_t MaxPacketSize, uint32_t *Encoding)
{
    switch(MaxPacketSize)
    {
        case 64U:
            *Encoding = 0U;
            return true;

        case 32U:
            *Encoding = 1U;
            return true;

        case 16U:
            *Encoding = 2U;
            return true;

        case 8U:
            *Encoding = 3U;
            return true;

        default:
            return false;
    }
}

static USBD_ResultTypeDef USBD_ActivateEndpoint(uint8_t EndpointAddress, USBD_EndpointTypeTypeDef Type, uint16_t MaxPacketSize)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;
    uint32_t MaxPacketField = MaxPacketSize;
    uint32_t Control;

    if((Number >= USBD_MAX_ENDPOINTS) || (MaxPacketSize == 0U))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    if((Number == 0U) != (Type == USBD_ENDPOINT_CONTROL))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    if(Number == 0U)
    {
        if(!USBD_EncodeEP0MaxPacketSize(MaxPacketSize, &MaxPacketField))
        {
            return USBD_RESULT_INVALID_ARGUMENT;
        }
    }
    else if(Type == USBD_ENDPOINT_ISOCHRONOUS)
    {
        if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
        {
            /* Isochronous IN frame-parity handling is not implemented yet. */
            return USBD_RESULT_UNSUPPORTED;
        }

        if(MaxPacketSize > USBD_FULL_SPEED_MAX_ISO_PACKET_SIZE)
        {
            return USBD_RESULT_INVALID_ARGUMENT;
        }
    }
    else if((Type != USBD_ENDPOINT_BULK) && (Type != USBD_ENDPOINT_INTERRUPT))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }
    else if(MaxPacketSize > USBD_FULL_SPEED_MAX_PACKET_SIZE)
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    Control = MaxPacketField | ((uint32_t)Type << USB_OTG_DIEPCTL_EPTYP_Pos) | USB_OTG_DIEPCTL_USBAEP;

    if((Number != 0U) && (Type != USBD_ENDPOINT_ISOCHRONOUS))
    {
        Control |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
    }

    if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USBD_InState[Number] = (USBD_InEndpointStateTypeDef){ .MaxPacket = MaxPacketSize };
        USBD_InEndpoint(Number)->DIEPCTL = Control | ((uint32_t)Number << USB_OTG_DIEPCTL_TXFNUM_Pos);
        USBD_Device()->DAINTMSK |= 1UL << Number;
    }
    else
    {
        USBD_OutState[Number] = (USBD_OutEndpointStateTypeDef)
        {
            .MaxPacket = MaxPacketSize,
            .Isochronous = (Type == USBD_ENDPOINT_ISOCHRONOUS)
        };
        USBD_OutEndpoint(Number)->DOEPCTL = Control;
        USBD_Device()->DAINTMSK |= 1UL << (16U + Number);
    }

    return USBD_RESULT_OK;
}

static void USBD_DeactivateEndpoint(uint8_t EndpointAddress)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;

    if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USB_OTG_INEndpointTypeDef *Endpoint = USBD_InEndpoint(Number);

        if((Endpoint->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U)
        {
            Endpoint->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
            Endpoint->DIEPCTL |= USB_OTG_DIEPCTL_EPDIS;
        }

        Endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_USBAEP;
        USBD_Device()->DAINTMSK &= ~(1UL << Number);
        USBD_Device()->DIEPEMPMSK &= ~(1UL << Number);
        USBD_FlushTxFifo(Number);
        USBD_InState[Number] = (USBD_InEndpointStateTypeDef){ 0 };
    }
    else
    {
        USB_OTG_OUTEndpointTypeDef *Endpoint = USBD_OutEndpoint(Number);

        if((Endpoint->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0U)
        {
            Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
            Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_EPDIS;
        }

        Endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_USBAEP;
        USBD_Device()->DAINTMSK &= ~(1UL << (16U + Number));
        USBD_OutState[Number] = (USBD_OutEndpointStateTypeDef){ 0 };
    }
}

/*
 * EP0 transfer-size registers hold only one packet; other endpoints are
 * limited by the ten-bit packet counter.
 */
static uint16_t USBD_GetChunkLimit(uint8_t Number, uint16_t MaxPacket)
{
    uint32_t Limit = (Number == 0U) ? MaxPacket : ((uint32_t)MaxPacket * USBD_MAX_PACKETS_PER_TRANSFER);

    return (Limit > UINT16_MAX) ? UINT16_MAX : (uint16_t)Limit;
}

/* -------------------------------------------------------------------------- */
/* Transfer engine                                                            */
/* -------------------------------------------------------------------------- */

static void USBD_ArmSetup(void)
{
    USBD_OutEndpoint(0U)->DOEPTSIZ = USB_OTG_DOEPTSIZ_STUPCNT |
                                     (1UL << USB_OTG_DOEPTSIZ_PKTCNT_Pos) |
                                     (USBD_EP0_SETUP_PACKET_COUNT * USBD_EP0_SETUP_SIZE);
    USBD_OutEndpoint(0U)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
}

static void USBD_StartInChunk(uint8_t Number)
{
    USBD_InEndpointStateTypeDef *State = &USBD_InState[Number];
    USB_OTG_INEndpointTypeDef *Endpoint = USBD_InEndpoint(Number);
    uint16_t Size = (uint16_t)(State->Length - State->ChunkEnd);
    uint16_t Limit = USBD_GetChunkLimit(Number, State->MaxPacket);
    uint32_t Packets;

    if(Size > Limit)
    {
        Size = Limit;
    }

    Packets = (Size == 0U) ? 1U : (((uint32_t)Size + State->MaxPacket - 1U) / State->MaxPacket);
    State->ChunkEnd = (uint16_t)(State->ChunkEnd + Size);

    Endpoint->DIEPTSIZ = (Packets << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | Size;
    Endpoint->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;

    if(Size != 0U)
    {
        USBD_Device()->DIEPEMPMSK |= 1UL << Number;
    }
}

static void USBD_FillTxFifo(uint8_t Number)
{
    USBD_InEndpointStateTypeDef *State = &USBD_InState[Number];
    USB_OTG_INEndpointTypeDef *Endpoint = USBD_InEndpoint(Number);

    while(State->Written < State->ChunkEnd)
    {
        uint16_t Length = (uint16_t)(State->ChunkEnd - State->Written);
        uint32_t Words;

        if(Length > State->MaxPacket)
        {
            Length = State->MaxPacket;
        }

        Words = ((uint32_t)Length + 3U) / 4U;

        if((Endpoint->DTXFSTS & USB_OTG_DTXFSTS_INEPTFSAV) < Words)
        {
            break;
        }

        USBD_WriteFifo(Number, &State->Buffer[State->Written], Length);
        State->Written = (uint16_t)(State->Written + Length);
    }

    if(State->Written >= State->ChunkEnd)
    {
        USBD_Device()->DIEPEMPMSK &= ~(1UL << Number);
    }
}

static void USBD_StartOutChunk(uint8_t Number)
{
    USBD_OutEndpointStateTypeDef *State = &USBD_OutState[Number];
    USB_OTG_OUTEndpointTypeDef *Endpoint = USBD_OutEndpoint(Number);
    uint16_t Size = (uint16_t)(State->Length - State->ChunkEnd);
    uint16_t Limit = State->Isochronous ? State->MaxPacket : USBD_GetChunkLimit(Number, State->MaxPacket);
    uint32_t Packets;
    uint32_t TransferSize;

    if(Size > Limit)
    {
        Size = Limit;
    }

    Packets = (Size == 0U) ? 1U : (((uint32_t)Size + State->MaxPacket - 1U) / State->MaxPacket);
    State->ChunkEnd = (uint16_t)(State->ChunkEnd + Size);

    /* OUT transfer sizes must be a whole number of maximum-size packets. */
    TransferSize = (Packets << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | (Packets * State->MaxPacket);

    if(Number == 0U)
    {
        TransferSize |= USB_OTG_DOEPTSIZ_STUPCNT;
    }

    Endpoint->DOEPTSIZ = TransferSize;

    if(State->Isochronous)
    {
        /* Accept the packet in the frame after the current one. */
        if((USBD_Device()->DSTS & (1UL << USB_OTG_DSTS_FNSOF_Pos)) == 0U)
        {
            Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SODDFRM;
        }
        else
        {
            Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
        }
    }

    Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
}

/*
 * Reported when an armed isochronous OUT endpoint received nothing in its
 * frame, such as before the host starts streaming. The endpoint is left armed:
 * it accepts the next packet in a frame of the same parity, so a missed frame
 * costs one packet. Disabling the endpoint instead would require a global OUT
 * NAK handshake, and without it the disable may never complete.
 */
static void USBD_HandleIncompleteIsochronousOut(void)
{
    USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_PXFR_INCOMPISOOUT;
}

/* -------------------------------------------------------------------------- */
/* Interrupt helpers                                                          */
/* -------------------------------------------------------------------------- */

static void USBD_HandleRxFifoLevel(void)
{
    uint32_t Status = USB1_OTG_HS->GRXSTSP;
    uint8_t Number = (uint8_t)(Status & USB_OTG_GRXSTSP_EPNUM);
    uint16_t Count = (uint16_t)((Status & USB_OTG_GRXSTSP_BCNT) >> USB_OTG_GRXSTSP_BCNT_Pos);
    uint32_t PacketStatus = (Status & USB_OTG_GRXSTSP_PKTSTS) >> USB_OTG_GRXSTSP_PKTSTS_Pos;

    if(PacketStatus == USBD_PKTSTS_SETUP_DATA)
    {
        USBD_ReadFifo(USBD_SetupBuffer, Count, sizeof(USBD_SetupBuffer));
    }
    else if((PacketStatus == USBD_PKTSTS_OUT_DATA) && (Count != 0U))
    {
        USBD_OutEndpointStateTypeDef *State = (Number < USBD_MAX_ENDPOINTS) ? &USBD_OutState[Number] : NULL;

        if((State != NULL) && State->Busy && (State->Buffer != NULL))
        {
            uint16_t Capacity = (uint16_t)(State->Length - State->Received);

            USBD_ReadFifo(&State->Buffer[State->Received], Count, Capacity);
            State->Received = (uint16_t)(State->Received + ((Count < Capacity) ? Count : Capacity));
        }
        else
        {
            USBD_ReadFifo(NULL, Count, 0U);
        }
    }
}

static void USBD_HandleBusReset(void)
{
    uint8_t Number;

    USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_USBRST;
    USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

    for(Number = 1U; Number < USBD_MAX_ENDPOINTS; Number++)
    {
        USBD_DeactivateEndpoint((uint8_t)(Number | USBD_ENDPOINT_DIRECTION_IN));
        USBD_DeactivateEndpoint(Number);
    }

    USBD_FlushTxFifo(USBD_ALL_TX_FIFOS);
    USBD_Device()->DIEPEMPMSK = 0U;
    USBD_SetAddress(0U);

    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_OUT, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_IN, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    USBD_ArmSetup();

    if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Reset != NULL))
    {
        USBD_RegisteredCallbacks->Reset(USBD_CallbackContext);
    }
}

static void USBD_HandleSetup(void)
{
    USBD_SetupPacketTypeDef Setup;

    /* A new SETUP packet supersedes any control transfer still in progress. */
    if(USBD_InState[0].Busy)
    {
        USBD_InState[0].Busy = false;
        USBD_InState[0].Buffer = NULL;
        USBD_Device()->DIEPEMPMSK &= ~1UL;
        USBD_FlushTxFifo(0U);
    }

    USBD_OutState[0].Busy = false;
    USBD_OutState[0].Buffer = NULL;

    /* Re-arm before the callback so it may start an OUT data stage on EP0. */
    USBD_ArmSetup();

    Setup.RequestType = USBD_SetupBuffer[0];
    Setup.Request = USBD_SetupBuffer[1];
    Setup.Value = (uint16_t)(USBD_SetupBuffer[2] | ((uint16_t)USBD_SetupBuffer[3] << 8U));
    Setup.Index = (uint16_t)(USBD_SetupBuffer[4] | ((uint16_t)USBD_SetupBuffer[5] << 8U));
    Setup.Length = (uint16_t)(USBD_SetupBuffer[6] | ((uint16_t)USBD_SetupBuffer[7] << 8U));

    if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->SetupReceived != NULL))
    {
        USBD_RegisteredCallbacks->SetupReceived(USBD_CallbackContext, &Setup);
    }
}

static void USBD_HandleOutEndpointInterrupt(uint8_t Number)
{
    USB_OTG_OUTEndpointTypeDef *Endpoint = USBD_OutEndpoint(Number);
    USBD_OutEndpointStateTypeDef *State = &USBD_OutState[Number];
    uint32_t Flags = Endpoint->DOEPINT;

    Endpoint->DOEPINT = Flags;
    Flags &= USBD_Device()->DOEPMSK;

    if((Flags & USB_OTG_DOEPINT_XFRC) != 0U)
    {
        if(!State->Busy)
        {
            /* Unrequested EP0 packet, such as a control status stage. */
            if(Number == 0U)
            {
                USBD_ArmSetup();
            }
        }
        else if(!State->Isochronous && (State->Received == State->ChunkEnd) && (State->ChunkEnd < State->Length))
        {
            USBD_StartOutChunk(Number);
        }
        else
        {
            uint8_t *Data = State->Buffer;

            State->Busy = false;
            State->Buffer = NULL;

            if(Number == 0U)
            {
                USBD_ArmSetup();
            }

            if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->OutTransferComplete != NULL))
            {
                USBD_RegisteredCallbacks->OutTransferComplete(USBD_CallbackContext, Number, Data, State->Received);
            }
        }
    }

    if((Flags & USB_OTG_DOEPINT_STUP) != 0U)
    {
        USBD_HandleSetup();
    }
}

static void USBD_HandleInEndpointInterrupt(uint8_t Number)
{
    USB_OTG_INEndpointTypeDef *Endpoint = USBD_InEndpoint(Number);
    USBD_InEndpointStateTypeDef *State = &USBD_InState[Number];
    uint32_t Flags = Endpoint->DIEPINT;

    Endpoint->DIEPINT = Flags & ~USB_OTG_DIEPINT_TXFE;
    Flags &= USBD_Device()->DIEPMSK;

    if(((Flags & USB_OTG_DIEPINT_XFRC) != 0U) && State->Busy)
    {
        if(State->ChunkEnd < State->Length)
        {
            USBD_StartInChunk(Number);
        }
        else
        {
            State->Busy = false;
            State->Buffer = NULL;

            if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->InTransferComplete != NULL))
            {
                USBD_RegisteredCallbacks->InTransferComplete(USBD_CallbackContext, (uint8_t)(Number | USBD_ENDPOINT_DIRECTION_IN));
            }
        }
    }

    if(((USBD_Device()->DIEPEMPMSK & (1UL << Number)) != 0U) &&
       ((Endpoint->DIEPINT & USB_OTG_DIEPINT_TXFE) != 0U))
    {
        USBD_FillTxFifo(Number);
    }
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

USBD_ResultTypeDef USBD_Init(const USBD_ConfigTypeDef *Config)
{
    USB_OTG_DeviceTypeDef *Device;
    uint16_t EP0MaxPacketSize = USBD_DEFAULT_EP0_MAX_PACKET_SIZE;
    uint32_t EP0Encoding;

    if(USBD_Initialized)
    {
        return USBD_RESULT_BUSY;
    }

    if((Config != NULL) && (Config->EP0MaxPacketSize != 0U))
    {
        EP0MaxPacketSize = Config->EP0MaxPacketSize;
    }

    if(!USBD_EncodeEP0MaxPacketSize(EP0MaxPacketSize, &EP0Encoding))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    USBD_RegisteredCallbacks = (Config != NULL) ? Config->Callbacks : NULL;
    USBD_CallbackContext = (Config != NULL) ? Config->CallbackContext : NULL;
    USBD_EP0MaxPacketSize = EP0MaxPacketSize;

    USBD_ConfigurePins();

    if((RCC_EnablePeripheralClock(USB_OTG_HS) != RCC_RESULT_OK) ||
       (RCC_ResetPeripheral(USB_OTG_HS) != RCC_RESULT_OK))
    {
        return USBD_RESULT_UNSUPPORTED;
    }

    /*
     * Keep the core clocked while the CPU sleeps (WFI), but not the ULPI
     * clock: with the internal FS PHY there is no ULPI PHY, and leaving its
     * sleep clock enabled stops the core whenever the CPU enters Sleep mode.
     */
    RCC->AHB1LPENR = (RCC->AHB1LPENR & ~RCC_AHB1LPENR_USB1OTGHSULPILPEN) | RCC_AHB1LPENR_USB1OTGHSLPEN;
    RCC->APB1HLPENR |= RCC_APB1HLPENR_CRSLPEN;

    /*
     * The STM32H7A3 LQFP USB supply pins use the externally supplied/bypassed
     * VDD33USB path. USB33RDY reports only the integrated-regulator path, so
     * it intentionally remains clear here and must not be used as a wait gate.
     */
    PWR->CR3 = (PWR->CR3 & ~PWR_CR3_USBREGEN) | PWR_CR3_USB33DEN;

    /*
     * The USB kernel clock is HSI48, whose untrimmed accuracy is far outside
     * the +/-0.25% full-speed USB requires. The clock recovery system trims
     * HSI48 continuously against the host's 1 ms start-of-frame packets.
     */
    if(RCC_EnablePeripheralClock(CRS) != RCC_RESULT_OK)
    {
        return USBD_RESULT_UNSUPPORTED;
    }

    CRS->CR &= ~(CRS_CR_CEN | CRS_CR_AUTOTRIMEN);
    CRS->CFGR = (USBD_CRS_SYNC_SOURCE_USB1_SOF << CRS_CFGR_SYNCSRC_Pos) |
                (USBD_CRS_FREQUENCY_ERROR_LIMIT << CRS_CFGR_FELIM_Pos) |
                (USBD_CRS_RELOAD << CRS_CFGR_RELOAD_Pos);
    CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

    USB1_OTG_HS->GAHBCFG = 0U;

    /*
     * Select the internal full-speed PHY and request peripheral mode before
     * resetting the core. The reset latches the requested role on this core.
     */
    USB1_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL;
    USB1_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_FHMOD;
    USB1_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;
    Delay_Milliseconds(USBD_ROLE_SETTLE_MS);

    if(!USBD_WaitForSet(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL))
    {
        return USBD_RESULT_TIMEOUT;
    }

    USB1_OTG_HS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;

    if(!USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_CSRST))
    {
        return USBD_RESULT_TIMEOUT;
    }

    /* Power the internal FS PHY and use PA9 through the USB VBUS detector. */
    USB1_OTG_HS->GCCFG |= USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_VBDEN;
    USB1_OTG_HS->GOTGCTL &= ~(USB_OTG_GOTGCTL_BVALOEN | USB_OTG_GOTGCTL_BVALOVAL);

    /*
     * Retain forced peripheral mode after the reset as well. PA10 is shared
     * with the LCD chip select and must never choose the USB role.
     */
    USB1_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_FHMOD;
    USB1_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;
    USB1_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_TRDT;
    USB1_OTG_HS->GUSBCFG |= USBD_TURNAROUND_TIME << USB_OTG_GUSBCFG_TRDT_Pos;
    Delay_Milliseconds(USBD_ROLE_SETTLE_MS);

    if((USB1_OTG_HS->GINTSTS & USB_OTG_GINTSTS_CMOD) != 0U)
    {
        return USBD_RESULT_TIMEOUT;
    }

    USB1_OTG_HS->GINTSTS = 0xBFFFFFFFUL;
    USBD_ConfigureFifos();

    Device = USBD_Device();
    Device->DCFG = USB_OTG_DCFG_DSPD_0; /* Full speed using the internal PHY. */
    Device->DCTL |= USB_OTG_DCTL_SDIS;
    Device->DIEPMSK = USB_OTG_DIEPMSK_XFRCM;
    Device->DOEPMSK = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM;
    Device->DIEPEMPMSK = 0U;
    Device->DAINTMSK = 0U;

    for(uint8_t Number = 0U; Number < USBD_MAX_ENDPOINTS; Number++)
    {
        USBD_InState[Number] = (USBD_InEndpointStateTypeDef){ 0 };
        USBD_OutState[Number] = (USBD_OutEndpointStateTypeDef){ 0 };
    }

    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_OUT, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_IN, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    USBD_ArmSetup();

    USB1_OTG_HS->GINTMSK = USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_RXFLVLM |
                           USB_OTG_GINTMSK_IEPINT | USB_OTG_GINTMSK_OEPINT |
                           USB_OTG_GINTMSK_USBSUSPM | USB_OTG_GINTMSK_WUIM |
                           USB_OTG_GINTMSK_PXFRM_IISOOXFRM;
    USBD_UpdateStartOfFrameInterrupt();

    NVIC_SetPriority(OTG_HS_IRQn, USBD_IRQ_PRIORITY);
    NVIC_EnableIRQ(OTG_HS_IRQn);
    USB1_OTG_HS->GAHBCFG |= USB_OTG_GAHBCFG_GINT;

    USBD_Initialized = true;

    return USBD_RESULT_OK;
}

void USBD_Deinit(void)
{
    if(!USBD_Initialized)
    {
        return;
    }

    USBD_Disconnect();

    NVIC_DisableIRQ(OTG_HS_IRQn);
    USB1_OTG_HS->GAHBCFG &= ~USB_OTG_GAHBCFG_GINT;
    USB1_OTG_HS->GINTMSK = 0U;
    USB1_OTG_HS->GINTSTS = 0xBFFFFFFFUL;

    USBD_FlushTxFifo(USBD_ALL_TX_FIFOS);
    USBD_FlushRxFifo();

    PWR->CR3 &= ~PWR_CR3_USB33DEN;
    (void)RCC_DisablePeripheralClock(USB_OTG_HS);

    USBD_RegisteredCallbacks = NULL;
    USBD_CallbackContext = NULL;
    USBD_Initialized = false;
}

USBD_ResultTypeDef USBD_SetCallbacks(const USBD_CallbacksTypeDef *Callbacks, void *CallbackContext)
{
    uint32_t Primask;

    if(!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    Primask = USBD_EnterCritical();

    USBD_RegisteredCallbacks = Callbacks;
    USBD_CallbackContext = CallbackContext;
    USBD_UpdateStartOfFrameInterrupt();

    USBD_ExitCritical(Primask);

    return USBD_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Bus control                                                                */
/* -------------------------------------------------------------------------- */

void USBD_Connect(void)
{
    if(USBD_Initialized)
    {
        USBD_Device()->DCTL &= ~USB_OTG_DCTL_SDIS;
    }
}

void USBD_Disconnect(void)
{
    if(USBD_Initialized)
    {
        USBD_Device()->DCTL |= USB_OTG_DCTL_SDIS;
    }
}

bool USBD_IsVbusPresent(void)
{
    if(!USBD_Initialized)
    {
        return false;
    }

    return (USB1_OTG_HS->GOTGCTL & USB_OTG_GOTGCTL_BSESVLD) != 0U;
}

void USBD_SetAddress(uint8_t Address)
{
    USB_OTG_DeviceTypeDef *Device = USBD_Device();

    Device->DCFG = (Device->DCFG & ~USB_OTG_DCFG_DAD) | ((uint32_t)(Address & 0x7FU) << USB_OTG_DCFG_DAD_Pos);
}

USBD_ResultTypeDef USBD_SignalRemoteWakeup(void)
{
    if(!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if((USBD_Device()->DSTS & USB_OTG_DSTS_SUSPSTS) == 0U)
    {
        return USBD_RESULT_BUSY;
    }

    USBD_Device()->DCTL |= USB_OTG_DCTL_RWUSIG;
    Delay_Milliseconds(USBD_REMOTE_WAKEUP_SIGNAL_MS);
    USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

    return USBD_RESULT_OK;
}

uint16_t USBD_GetFrameNumber(void)
{
    if(!USBD_Initialized)
    {
        return 0U;
    }

    return (uint16_t)((USBD_Device()->DSTS & USB_OTG_DSTS_FNSOF) >> USB_OTG_DSTS_FNSOF_Pos);
}

/* -------------------------------------------------------------------------- */
/* Endpoint management                                                        */
/* -------------------------------------------------------------------------- */

USBD_ResultTypeDef USBD_OpenEndpoint(uint8_t EndpointAddress, USBD_EndpointTypeTypeDef Type, uint16_t MaxPacketSize)
{
    uint32_t Primask;
    USBD_ResultTypeDef Result;

    if(!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    Primask = USBD_EnterCritical();
    Result = USBD_ActivateEndpoint(EndpointAddress, Type, MaxPacketSize);
    USBD_ExitCritical(Primask);

    return Result;
}

void USBD_CloseEndpoint(uint8_t EndpointAddress)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;
    uint32_t Primask;

    if(!USBD_Initialized || (Number == 0U) || (Number >= USBD_MAX_ENDPOINTS))
    {
        return;
    }

    Primask = USBD_EnterCritical();
    USBD_DeactivateEndpoint(EndpointAddress);
    USBD_ExitCritical(Primask);
}

void USBD_StallEndpoint(uint8_t EndpointAddress, bool Stall)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;

    if(!USBD_Initialized || (Number >= USBD_MAX_ENDPOINTS))
    {
        return;
    }

    if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USB_OTG_INEndpointTypeDef *Endpoint = USBD_InEndpoint(Number);

        if(Stall)
        {
            if(((Endpoint->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U) && (Number != 0U))
            {
                Endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_EPDIS;
            }

            Endpoint->DIEPCTL |= USB_OTG_DIEPCTL_STALL;
        }
        else
        {
            Endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_STALL;

            if(Number != 0U)
            {
                Endpoint->DIEPCTL |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
            }
        }
    }
    else
    {
        USB_OTG_OUTEndpointTypeDef *Endpoint = USBD_OutEndpoint(Number);

        if(Stall)
        {
            if(((Endpoint->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U) && (Number != 0U))
            {
                Endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_EPDIS;
            }

            Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
        }
        else
        {
            Endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_STALL;

            if(Number != 0U)
            {
                Endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
            }
        }
    }
}

bool USBD_IsEndpointStalled(uint8_t EndpointAddress)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;

    if(!USBD_Initialized || (Number >= USBD_MAX_ENDPOINTS))
    {
        return false;
    }

    if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        return (USBD_InEndpoint(Number)->DIEPCTL & USB_OTG_DIEPCTL_STALL) != 0U;
    }

    return (USBD_OutEndpoint(Number)->DOEPCTL & USB_OTG_DOEPCTL_STALL) != 0U;
}

bool USBD_IsEndpointBusy(uint8_t EndpointAddress)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;

    if(Number >= USBD_MAX_ENDPOINTS)
    {
        return false;
    }

    if((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        return USBD_InState[Number].Busy;
    }

    return USBD_OutState[Number].Busy;
}

/* -------------------------------------------------------------------------- */
/* Transfers                                                                  */
/* -------------------------------------------------------------------------- */

USBD_ResultTypeDef USBD_Transmit(uint8_t EndpointAddress, const uint8_t *Data, uint16_t Length)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;
    USBD_InEndpointStateTypeDef *State;
    USBD_ResultTypeDef Result = USBD_RESULT_OK;
    uint32_t Primask;

    if(!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if(((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) == 0U) ||
       (Number >= USBD_MAX_ENDPOINTS) ||
       ((Length != 0U) && (Data == NULL)))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    State = &USBD_InState[Number];
    Primask = USBD_EnterCritical();

    if(State->MaxPacket == 0U)
    {
        Result = USBD_RESULT_INVALID_ARGUMENT;
    }
    else if(State->Busy)
    {
        Result = USBD_RESULT_BUSY;
    }
    else
    {
        State->Buffer = Data;
        State->Length = Length;
        State->Written = 0U;
        State->ChunkEnd = 0U;
        State->Busy = true;
        USBD_StartInChunk(Number);
    }

    USBD_ExitCritical(Primask);

    return Result;
}

USBD_ResultTypeDef USBD_Receive(uint8_t EndpointAddress, uint8_t *Data, uint16_t Length)
{
    uint8_t Number = EndpointAddress & USBD_ENDPOINT_NUMBER_MASK;
    USBD_OutEndpointStateTypeDef *State;
    USBD_ResultTypeDef Result = USBD_RESULT_OK;
    uint32_t Primask;

    if(!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if(((EndpointAddress & USBD_ENDPOINT_DIRECTION_IN) != 0U) ||
       (Number >= USBD_MAX_ENDPOINTS) ||
       ((Length != 0U) && (Data == NULL)))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    State = &USBD_OutState[Number];
    Primask = USBD_EnterCritical();

    if(State->MaxPacket == 0U)
    {
        Result = USBD_RESULT_INVALID_ARGUMENT;
    }
    else if(State->Busy)
    {
        Result = USBD_RESULT_BUSY;
    }
    else
    {
        State->Buffer = Data;
        State->Length = Length;
        State->Received = 0U;
        State->ChunkEnd = 0U;
        State->Busy = true;
        USBD_StartOutChunk(Number);
    }

    USBD_ExitCritical(Primask);

    return Result;
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

void USBD_IRQHandler(void)
{
    uint32_t Pending;
    uint32_t Endpoints;
    uint8_t Number;

    if(!USBD_Initialized)
    {
        return;
    }

    Pending = USB1_OTG_HS->GINTSTS & USB1_OTG_HS->GINTMSK;

    if((Pending & USB_OTG_GINTSTS_RXFLVL) != 0U)
    {
        USBD_HandleRxFifoLevel();
    }

    if((Pending & USB_OTG_GINTSTS_USBRST) != 0U)
    {
        USBD_HandleBusReset();
    }

    if((Pending & USB_OTG_GINTSTS_OEPINT) != 0U)
    {
        Endpoints = (USBD_Device()->DAINT & USBD_Device()->DAINTMSK) >> 16U;

        for(Number = 0U; Number < USBD_MAX_ENDPOINTS; Number++)
        {
            if((Endpoints & (1UL << Number)) != 0U)
            {
                USBD_HandleOutEndpointInterrupt(Number);
            }
        }
    }

    if((Pending & USB_OTG_GINTSTS_IEPINT) != 0U)
    {
        Endpoints = USBD_Device()->DAINT & USBD_Device()->DAINTMSK & 0xFFFFU;

        for(Number = 0U; Number < USBD_MAX_ENDPOINTS; Number++)
        {
            if((Endpoints & (1UL << Number)) != 0U)
            {
                USBD_HandleInEndpointInterrupt(Number);
            }
        }
    }

    if((Pending & USB_OTG_GINTSTS_PXFR_INCOMPISOOUT) != 0U)
    {
        USBD_HandleIncompleteIsochronousOut();
    }

    if((Pending & USB_OTG_GINTSTS_USBSUSP) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_USBSUSP;

        if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Suspend != NULL))
        {
            USBD_RegisteredCallbacks->Suspend(USBD_CallbackContext);
        }
    }

    if((Pending & USB_OTG_GINTSTS_WKUINT) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_WKUINT;
        USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

        if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Resume != NULL))
        {
            USBD_RegisteredCallbacks->Resume(USBD_CallbackContext);
        }
    }

    if((Pending & USB_OTG_GINTSTS_SOF) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_SOF;

        if((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->StartOfFrame != NULL))
        {
            USBD_RegisteredCallbacks->StartOfFrame(USBD_CallbackContext, USBD_GetFrameNumber());
        }
    }
}
