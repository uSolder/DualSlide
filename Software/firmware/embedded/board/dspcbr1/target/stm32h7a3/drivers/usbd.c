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
    const uint8_t *buffer;
    uint16_t length;
    uint16_t written;
    uint16_t chunk_end;
    uint16_t max_packet;
    bool busy;
} USBD_InEndpointState;

/**
 * @brief Runtime state of one OUT endpoint.
 */
typedef struct
{
    uint8_t *buffer;
    uint16_t length;
    uint16_t received;
    uint16_t chunk_end;
    uint16_t max_packet;
    bool isochronous;
    bool busy;
} USBD_OutEndpointState;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBD_Callbacks *USBD_RegisteredCallbacks;
static void *USBD_CallbackContext;
static USBD_InEndpointState USBD_InState[USBD_MAX_ENDPOINTS];
static USBD_OutEndpointState USBD_OutState[USBD_MAX_ENDPOINTS];
static uint8_t USBD_SetupBuffer[USBD_EP0_SETUP_SIZE];
static uint16_t USBD_EP0MaxPacketSize;
static bool USBD_Initialized;

/*
 * Intentionally global and volatile so it can be added directly to the
 * debugger watch list.
 */
volatile uint32_t USBD_DebugIncompleteIsochronousOutCount = 0U;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static USB_OTG_DeviceTypeDef *USBD_Device(void);
static USB_OTG_INEndpointTypeDef *USBD_InEndpoint(uint8_t number);
static USB_OTG_OUTEndpointTypeDef *USBD_OutEndpoint(uint8_t number);
static volatile uint32_t *USBD_Fifo(uint8_t number);
static uint32_t USBD_EnterCritical(void);
static void USBD_ExitCritical(uint32_t primask);
static bool USBD_WaitForClear(volatile uint32_t *reg, uint32_t mask);
static bool USBD_WaitForSet(volatile uint32_t *reg, uint32_t mask);
static void USBD_ConfigurePin(uint32_t pin_number, uint32_t mode);
static void USBD_ConfigurePins(void);
static void USBD_ConfigureFifos(void);
static void USBD_FlushRxFifo(void);
static void USBD_FlushTxFifo(uint8_t number);
static void USBD_WriteFifo(uint8_t number, const uint8_t *data, uint16_t length);
static void USBD_ReadFifo(uint8_t *data, uint16_t count, uint16_t capacity);
static void USBD_UpdateStartOfFrameInterrupt(void);
static bool USBD_EncodeEP0MaxPacketSize(uint16_t max_packet_size, uint32_t *encoding);
static USBD_Result USBD_ActivateEndpoint(uint8_t endpoint_address, USBD_EndpointType type, uint16_t max_packet_size);
static void USBD_DeactivateEndpoint(uint8_t endpoint_address);
static uint16_t USBD_GetChunkLimit(uint8_t number, uint16_t max_packet);
static void USBD_ArmSetup(void);
static void USBD_StartInChunk(uint8_t number);
static void USBD_FillTxFifo(uint8_t number);
static void USBD_StartOutChunk(uint8_t number);
static void USBD_HandleIncompleteIsochronousOut(void);
static void USBD_HandleRxFifoLevel(void);
static void USBD_HandleBusReset(void);
static void USBD_HandleSetup(void);
static void USBD_HandleOutEndpointInterrupt(uint8_t number);
static void USBD_HandleInEndpointInterrupt(uint8_t number);

/* -------------------------------------------------------------------------- */
/* Register helpers                                                           */
/* -------------------------------------------------------------------------- */

static USB_OTG_DeviceTypeDef *USBD_Device(void)
{
    return (USB_OTG_DeviceTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
}

static USB_OTG_INEndpointTypeDef *USBD_InEndpoint(uint8_t number)
{
    return (USB_OTG_INEndpointTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE + ((uint32_t)number * USB_OTG_EP_REG_SIZE));
}

static USB_OTG_OUTEndpointTypeDef *USBD_OutEndpoint(uint8_t number)
{
    return (USB_OTG_OUTEndpointTypeDef *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE + ((uint32_t)number * USB_OTG_EP_REG_SIZE));
}

static volatile uint32_t *USBD_Fifo(uint8_t number)
{
    return (volatile uint32_t *)(USB1_OTG_HS_PERIPH_BASE + USB_OTG_FIFO_BASE + ((uint32_t)number * USB_OTG_FIFO_SIZE));
}

static uint32_t USBD_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    return primask;
}

static void USBD_ExitCritical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static bool USBD_WaitForClear(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = USBD_TIMEOUT_ITERATIONS;

    while (((*reg & mask) != 0U) && (timeout != 0U))
    {
        timeout--;
    }

    return timeout != 0U;
}

static bool USBD_WaitForSet(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = USBD_TIMEOUT_ITERATIONS;

    while (((*reg & mask) != mask) && (timeout != 0U))
    {
        timeout--;
    }

    return timeout != 0U;
}

/* -------------------------------------------------------------------------- */
/* Pin configuration                                                          */
/* -------------------------------------------------------------------------- */

static void USBD_ConfigurePin(uint32_t pin_number, uint32_t mode)
{
    uint32_t afr_index = pin_number / 8U;
    uint32_t afr_position = (pin_number % 8U) * 4U;

    GPIOA->MODER &= ~(0x3UL << (pin_number * 2U));
    GPIOA->MODER |= mode << (pin_number * 2U);

    GPIOA->OTYPER &= ~(1UL << pin_number);
    GPIOA->PUPDR &= ~(0x3UL << (pin_number * 2U));

    if (mode == USBD_GPIO_MODE_ALTERNATE)
    {
        GPIOA->OSPEEDR &= ~(0x3UL << (pin_number * 2U));
        GPIOA->OSPEEDR |= USBD_GPIO_SPEED_VERY_HIGH << (pin_number * 2U);

        GPIOA->AFR[afr_index] &= ~(0xFUL << afr_position);
        GPIOA->AFR[afr_index] |= USBD_GPIO_ALTERNATE_FUNCTION << afr_position;
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
    uint32_t start = USBD_RX_FIFO_WORDS + USBD_TX0_FIFO_WORDS;
    uint8_t number;

    USB1_OTG_HS->GRXFSIZ = USBD_RX_FIFO_WORDS;
    USB1_OTG_HS->DIEPTXF0_HNPTXFSIZ = (USBD_TX0_FIFO_WORDS << 16U) | USBD_RX_FIFO_WORDS;

    for (number = 1U; number < USBD_MAX_ENDPOINTS; number++)
    {
        USB1_OTG_HS->DIEPTXF[number - 1U] = (USBD_TX_FIFO_WORDS << 16U) | start;
        start += USBD_TX_FIFO_WORDS;
    }

    USBD_FlushRxFifo();
    USBD_FlushTxFifo(USBD_ALL_TX_FIFOS);
}

static void USBD_FlushRxFifo(void)
{
    USB1_OTG_HS->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    (void)USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_RXFFLSH);
}

static void USBD_FlushTxFifo(uint8_t number)
{
    USB1_OTG_HS->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | ((uint32_t)number << USB_OTG_GRSTCTL_TXFNUM_Pos);
    (void)USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_TXFFLSH);
}

static void USBD_WriteFifo(uint8_t number, const uint8_t *data, uint16_t length)
{
    volatile uint32_t *fifo = USBD_Fifo(number);
    uint16_t index;
    uint16_t byte;

    for (index = 0U; index < length; index += 4U)
    {
        uint32_t word = 0U;

        for (byte = 0U; (byte < 4U) && ((uint16_t)(index + byte) < length); byte++)
        {
            word |= (uint32_t)data[index + byte] << (8U * byte);
        }

        *fifo = word;
    }
}

/*
 * Pops one received packet of count bytes from the shared RX FIFO. Bytes
 * beyond capacity are popped and discarded so the FIFO stays word-aligned.
 */
static void USBD_ReadFifo(uint8_t *data, uint16_t count, uint16_t capacity)
{
    volatile uint32_t *fifo = USBD_Fifo(0U);
    uint16_t index;
    uint16_t byte;

    for (index = 0U; index < count; index += 4U)
    {
        uint32_t word = *fifo;

        for (byte = 0U; (byte < 4U) && ((uint16_t)(index + byte) < count); byte++)
        {
            if ((data != NULL) && ((uint16_t)(index + byte) < capacity))
            {
                data[index + byte] = (uint8_t)(word >> (8U * byte));
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Endpoint helpers                                                           */
/* -------------------------------------------------------------------------- */

static void USBD_UpdateStartOfFrameInterrupt(void)
{
    if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->StartOfFrame != NULL))
    {
        USB1_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_SOFM;
    }
    else
    {
        USB1_OTG_HS->GINTMSK &= ~USB_OTG_GINTMSK_SOFM;
    }
}

static bool USBD_EncodeEP0MaxPacketSize(uint16_t max_packet_size, uint32_t *encoding)
{
    switch (max_packet_size)
    {
        case 64U:
            *encoding = 0U;
            return true;

        case 32U:
            *encoding = 1U;
            return true;

        case 16U:
            *encoding = 2U;
            return true;

        case 8U:
            *encoding = 3U;
            return true;

        default:
            return false;
    }
}

static USBD_Result USBD_ActivateEndpoint(uint8_t endpoint_address, USBD_EndpointType type, uint16_t max_packet_size)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;
    uint32_t max_packet_field = max_packet_size;
    uint32_t control;

    if ((number >= USBD_MAX_ENDPOINTS) || (max_packet_size == 0U))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    if ((number == 0U) != (type == USBD_ENDPOINT_CONTROL))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    if (number == 0U)
    {
        if (!USBD_EncodeEP0MaxPacketSize(max_packet_size, &max_packet_field))
        {
            return USBD_RESULT_INVALID_ARGUMENT;
        }
    }
    else if (type == USBD_ENDPOINT_ISOCHRONOUS)
    {
        if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
        {
            /* Isochronous IN frame-parity handling is not implemented yet. */
            return USBD_RESULT_UNSUPPORTED;
        }

        if (max_packet_size > USBD_FULL_SPEED_MAX_ISO_PACKET_SIZE)
        {
            return USBD_RESULT_INVALID_ARGUMENT;
        }
    }
    else if ((type != USBD_ENDPOINT_BULK) && (type != USBD_ENDPOINT_INTERRUPT))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }
    else if (max_packet_size > USBD_FULL_SPEED_MAX_PACKET_SIZE)
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    control = max_packet_field | ((uint32_t)type << USB_OTG_DIEPCTL_EPTYP_Pos) | USB_OTG_DIEPCTL_USBAEP;

    if ((number != 0U) && (type != USBD_ENDPOINT_ISOCHRONOUS))
    {
        control |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
    }

    if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USBD_InState[number] = (USBD_InEndpointState){ .max_packet = max_packet_size };
        USBD_InEndpoint(number)->DIEPCTL = control | ((uint32_t)number << USB_OTG_DIEPCTL_TXFNUM_Pos);
        USBD_Device()->DAINTMSK |= 1UL << number;
    }
    else
    {
        USBD_OutState[number] = (USBD_OutEndpointState)
        {
            .max_packet = max_packet_size,
            .isochronous = (type == USBD_ENDPOINT_ISOCHRONOUS)
        };
        USBD_OutEndpoint(number)->DOEPCTL = control;
        USBD_Device()->DAINTMSK |= 1UL << (16U + number);
    }

    return USBD_RESULT_OK;
}

static void USBD_DeactivateEndpoint(uint8_t endpoint_address)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;

    if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USB_OTG_INEndpointTypeDef *endpoint = USBD_InEndpoint(number);

        if ((endpoint->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U)
        {
            endpoint->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
            endpoint->DIEPCTL |= USB_OTG_DIEPCTL_EPDIS;
        }

        endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_USBAEP;
        USBD_Device()->DAINTMSK &= ~(1UL << number);
        USBD_Device()->DIEPEMPMSK &= ~(1UL << number);
        USBD_FlushTxFifo(number);
        USBD_InState[number] = (USBD_InEndpointState){ 0 };
    }
    else
    {
        USB_OTG_OUTEndpointTypeDef *endpoint = USBD_OutEndpoint(number);

        if ((endpoint->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0U)
        {
            endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
            endpoint->DOEPCTL |= USB_OTG_DOEPCTL_EPDIS;
        }

        endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_USBAEP;
        USBD_Device()->DAINTMSK &= ~(1UL << (16U + number));
        USBD_OutState[number] = (USBD_OutEndpointState){ 0 };
    }
}

/*
 * EP0 transfer-size registers hold only one packet; other endpoints are
 * limited by the ten-bit packet counter.
 */
static uint16_t USBD_GetChunkLimit(uint8_t number, uint16_t max_packet)
{
    uint32_t limit = (number == 0U) ? max_packet : ((uint32_t)max_packet * USBD_MAX_PACKETS_PER_TRANSFER);

    return (limit > UINT16_MAX) ? UINT16_MAX : (uint16_t)limit;
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

static void USBD_StartInChunk(uint8_t number)
{
    USBD_InEndpointState *state = &USBD_InState[number];
    USB_OTG_INEndpointTypeDef *endpoint = USBD_InEndpoint(number);
    uint16_t size = (uint16_t)(state->length - state->chunk_end);
    uint16_t limit = USBD_GetChunkLimit(number, state->max_packet);
    uint32_t packets;

    if (size > limit)
    {
        size = limit;
    }

    packets = (size == 0U) ? 1U : (((uint32_t)size + state->max_packet - 1U) / state->max_packet);
    state->chunk_end = (uint16_t)(state->chunk_end + size);

    endpoint->DIEPTSIZ = (packets << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | size;
    endpoint->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;

    if (size != 0U)
    {
        USBD_Device()->DIEPEMPMSK |= 1UL << number;
    }
}

static void USBD_FillTxFifo(uint8_t number)
{
    USBD_InEndpointState *state = &USBD_InState[number];
    USB_OTG_INEndpointTypeDef *endpoint = USBD_InEndpoint(number);

    while (state->written < state->chunk_end)
    {
        uint16_t length = (uint16_t)(state->chunk_end - state->written);
        uint32_t words;

        if (length > state->max_packet)
        {
            length = state->max_packet;
        }

        words = ((uint32_t)length + 3U) / 4U;

        if ((endpoint->DTXFSTS & USB_OTG_DTXFSTS_INEPTFSAV) < words)
        {
            break;
        }

        USBD_WriteFifo(number, &state->buffer[state->written], length);
        state->written = (uint16_t)(state->written + length);
    }

    if (state->written >= state->chunk_end)
    {
        USBD_Device()->DIEPEMPMSK &= ~(1UL << number);
    }
}

static void USBD_StartOutChunk(uint8_t number)
{
    USBD_OutEndpointState *state = &USBD_OutState[number];
    USB_OTG_OUTEndpointTypeDef *endpoint = USBD_OutEndpoint(number);
    uint16_t size = (uint16_t)(state->length - state->chunk_end);
    uint16_t limit = state->isochronous ? state->max_packet : USBD_GetChunkLimit(number, state->max_packet);
    uint32_t packets;
    uint32_t transfer_size;

    if (size > limit)
    {
        size = limit;
    }

    packets = (size == 0U) ? 1U : (((uint32_t)size + state->max_packet - 1U) / state->max_packet);
    state->chunk_end = (uint16_t)(state->chunk_end + size);

    /* OUT transfer sizes must be a whole number of maximum-size packets. */
    transfer_size = (packets << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | (packets * state->max_packet);

    if (number == 0U)
    {
        transfer_size |= USB_OTG_DOEPTSIZ_STUPCNT;
    }

    endpoint->DOEPTSIZ = transfer_size;

    if (state->isochronous)
    {
        /* Accept the packet in the frame after the current one. */
        if ((USBD_Device()->DSTS & (1UL << USB_OTG_DSTS_FNSOF_Pos)) == 0U)
        {
            endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SODDFRM;
        }
        else
        {
            endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
        }
    }

    endpoint->DOEPCTL |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
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
    USBD_DebugIncompleteIsochronousOutCount++;
}

/* -------------------------------------------------------------------------- */
/* Interrupt helpers                                                          */
/* -------------------------------------------------------------------------- */

static void USBD_HandleRxFifoLevel(void)
{
    uint32_t status = USB1_OTG_HS->GRXSTSP;
    uint8_t number = (uint8_t)(status & USB_OTG_GRXSTSP_EPNUM);
    uint16_t count = (uint16_t)((status & USB_OTG_GRXSTSP_BCNT) >> USB_OTG_GRXSTSP_BCNT_Pos);
    uint32_t packet_status = (status & USB_OTG_GRXSTSP_PKTSTS) >> USB_OTG_GRXSTSP_PKTSTS_Pos;

    if (packet_status == USBD_PKTSTS_SETUP_DATA)
    {
        USBD_ReadFifo(USBD_SetupBuffer, count, sizeof(USBD_SetupBuffer));
    }
    else if ((packet_status == USBD_PKTSTS_OUT_DATA) && (count != 0U))
    {
        USBD_OutEndpointState *state = (number < USBD_MAX_ENDPOINTS) ? &USBD_OutState[number] : NULL;

        if ((state != NULL) && state->busy && (state->buffer != NULL))
        {
            uint16_t capacity = (uint16_t)(state->length - state->received);

            USBD_ReadFifo(&state->buffer[state->received], count, capacity);
            state->received = (uint16_t)(state->received + ((count < capacity) ? count : capacity));
        }
        else
        {
            USBD_ReadFifo(NULL, count, 0U);
        }
    }
}

static void USBD_HandleBusReset(void)
{
    uint8_t number;

    USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_USBRST;
    USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

    for (number = 1U; number < USBD_MAX_ENDPOINTS; number++)
    {
        USBD_DeactivateEndpoint((uint8_t)(number | USBD_ENDPOINT_DIRECTION_IN));
        USBD_DeactivateEndpoint(number);
    }

    USBD_FlushTxFifo(USBD_ALL_TX_FIFOS);
    USBD_Device()->DIEPEMPMSK = 0U;
    USBD_SetAddress(0U);

    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_OUT, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    (void)USBD_ActivateEndpoint(USBD_ENDPOINT0_IN, USBD_ENDPOINT_CONTROL, USBD_EP0MaxPacketSize);
    USBD_ArmSetup();

    if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Reset != NULL))
    {
        USBD_RegisteredCallbacks->Reset(USBD_CallbackContext);
    }
}

static void USBD_HandleSetup(void)
{
    USBD_SetupPacket setup;

    /* A new SETUP packet supersedes any control transfer still in progress. */
    if (USBD_InState[0].busy)
    {
        USBD_InState[0].busy = false;
        USBD_InState[0].buffer = NULL;
        USBD_Device()->DIEPEMPMSK &= ~1UL;
        USBD_FlushTxFifo(0U);
    }

    USBD_OutState[0].busy = false;
    USBD_OutState[0].buffer = NULL;

    /* Re-arm before the callback so it may start an OUT data stage on EP0. */
    USBD_ArmSetup();

    setup.request_type = USBD_SetupBuffer[0];
    setup.request = USBD_SetupBuffer[1];
    setup.value = (uint16_t)(USBD_SetupBuffer[2] | ((uint16_t)USBD_SetupBuffer[3] << 8U));
    setup.index = (uint16_t)(USBD_SetupBuffer[4] | ((uint16_t)USBD_SetupBuffer[5] << 8U));
    setup.length = (uint16_t)(USBD_SetupBuffer[6] | ((uint16_t)USBD_SetupBuffer[7] << 8U));

    if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->SetupReceived != NULL))
    {
        USBD_RegisteredCallbacks->SetupReceived(USBD_CallbackContext, &setup);
    }
}

static void USBD_HandleOutEndpointInterrupt(uint8_t number)
{
    USB_OTG_OUTEndpointTypeDef *endpoint = USBD_OutEndpoint(number);
    USBD_OutEndpointState *state = &USBD_OutState[number];
    uint32_t flags = endpoint->DOEPINT;

    endpoint->DOEPINT = flags;
    flags &= USBD_Device()->DOEPMSK;

    if ((flags & USB_OTG_DOEPINT_XFRC) != 0U)
    {
        if (!state->busy)
        {
            /* Unrequested EP0 packet, such as a control status stage. */
            if (number == 0U)
            {
                USBD_ArmSetup();
            }
        }
        else if (!state->isochronous && (state->received == state->chunk_end) && (state->chunk_end < state->length))
        {
            USBD_StartOutChunk(number);
        }
        else
        {
            uint8_t *data = state->buffer;

            state->busy = false;
            state->buffer = NULL;

            if (number == 0U)
            {
                USBD_ArmSetup();
            }

            if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->OutTransferComplete != NULL))
            {
                USBD_RegisteredCallbacks->OutTransferComplete(USBD_CallbackContext, number, data, state->received);
            }
        }
    }

    if ((flags & USB_OTG_DOEPINT_STUP) != 0U)
    {
        USBD_HandleSetup();
    }
}

static void USBD_HandleInEndpointInterrupt(uint8_t number)
{
    USB_OTG_INEndpointTypeDef *endpoint = USBD_InEndpoint(number);
    USBD_InEndpointState *state = &USBD_InState[number];
    uint32_t flags = endpoint->DIEPINT;

    endpoint->DIEPINT = flags & ~USB_OTG_DIEPINT_TXFE;
    flags &= USBD_Device()->DIEPMSK;

    if (((flags & USB_OTG_DIEPINT_XFRC) != 0U) && state->busy)
    {
        if (state->chunk_end < state->length)
        {
            USBD_StartInChunk(number);
        }
        else
        {
            state->busy = false;
            state->buffer = NULL;

            if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->InTransferComplete != NULL))
            {
                USBD_RegisteredCallbacks->InTransferComplete(USBD_CallbackContext, (uint8_t)(number | USBD_ENDPOINT_DIRECTION_IN));
            }
        }
    }

    if (((USBD_Device()->DIEPEMPMSK & (1UL << number)) != 0U) &&
        ((endpoint->DIEPINT & USB_OTG_DIEPINT_TXFE) != 0U))
    {
        USBD_FillTxFifo(number);
    }
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

USBD_Result USBD_Init(const USBD_Config *config)
{
    USB_OTG_DeviceTypeDef *device;
    uint16_t ep0_max_packet_size = USBD_DEFAULT_EP0_MAX_PACKET_SIZE;
    uint32_t ep0_encoding;

    if (USBD_Initialized)
    {
        return USBD_RESULT_BUSY;
    }

    if ((config != NULL) && (config->ep0_max_packet_size != 0U))
    {
        ep0_max_packet_size = config->ep0_max_packet_size;
    }

    if (!USBD_EncodeEP0MaxPacketSize(ep0_max_packet_size, &ep0_encoding))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    USBD_RegisteredCallbacks = (config != NULL) ? config->callbacks : NULL;
    USBD_CallbackContext = (config != NULL) ? config->callback_context : NULL;
    USBD_EP0MaxPacketSize = ep0_max_packet_size;

    USBD_ConfigurePins();

    if ((RCC_EnablePeripheralClock(USB_OTG_HS) != RCC_RESULT_OK) ||
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
    if (RCC_EnablePeripheralClock(CRS) != RCC_RESULT_OK)
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
    Delay_ms(USBD_ROLE_SETTLE_MS);

    if (!USBD_WaitForSet(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL))
    {
        return USBD_RESULT_TIMEOUT;
    }

    USB1_OTG_HS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;

    if (!USBD_WaitForClear(&USB1_OTG_HS->GRSTCTL, USB_OTG_GRSTCTL_CSRST))
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
    Delay_ms(USBD_ROLE_SETTLE_MS);

    if ((USB1_OTG_HS->GINTSTS & USB_OTG_GINTSTS_CMOD) != 0U)
    {
        return USBD_RESULT_TIMEOUT;
    }

    USB1_OTG_HS->GINTSTS = 0xBFFFFFFFUL;
    USBD_ConfigureFifos();

    device = USBD_Device();
    device->DCFG = USB_OTG_DCFG_DSPD_0; /* Full speed using the internal PHY. */
    device->DCTL |= USB_OTG_DCTL_SDIS;
    device->DIEPMSK = USB_OTG_DIEPMSK_XFRCM;
    device->DOEPMSK = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM;
    device->DIEPEMPMSK = 0U;
    device->DAINTMSK = 0U;

    for (uint8_t number = 0U; number < USBD_MAX_ENDPOINTS; number++)
    {
        USBD_InState[number] = (USBD_InEndpointState){ 0 };
        USBD_OutState[number] = (USBD_OutEndpointState){ 0 };
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
    if (!USBD_Initialized)
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

USBD_Result USBD_SetCallbacks(const USBD_Callbacks *callbacks, void *callback_context)
{
    uint32_t primask;

    if (!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    primask = USBD_EnterCritical();

    USBD_RegisteredCallbacks = callbacks;
    USBD_CallbackContext = callback_context;
    USBD_UpdateStartOfFrameInterrupt();

    USBD_ExitCritical(primask);

    return USBD_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Bus control                                                                */
/* -------------------------------------------------------------------------- */

void USBD_Connect(void)
{
    if (USBD_Initialized)
    {
        USBD_Device()->DCTL &= ~USB_OTG_DCTL_SDIS;
    }
}

void USBD_Disconnect(void)
{
    if (USBD_Initialized)
    {
        USBD_Device()->DCTL |= USB_OTG_DCTL_SDIS;
    }
}

bool USBD_IsVbusPresent(void)
{
    if (!USBD_Initialized)
    {
        return false;
    }

    return (USB1_OTG_HS->GOTGCTL & USB_OTG_GOTGCTL_BSESVLD) != 0U;
}

void USBD_SetAddress(uint8_t address)
{
    USB_OTG_DeviceTypeDef *device = USBD_Device();

    device->DCFG = (device->DCFG & ~USB_OTG_DCFG_DAD) | ((uint32_t)(address & 0x7FU) << USB_OTG_DCFG_DAD_Pos);
}

USBD_Result USBD_SignalRemoteWakeup(void)
{
    if (!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if ((USBD_Device()->DSTS & USB_OTG_DSTS_SUSPSTS) == 0U)
    {
        return USBD_RESULT_BUSY;
    }

    USBD_Device()->DCTL |= USB_OTG_DCTL_RWUSIG;
    Delay_ms(USBD_REMOTE_WAKEUP_SIGNAL_MS);
    USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

    return USBD_RESULT_OK;
}

uint16_t USBD_GetFrameNumber(void)
{
    if (!USBD_Initialized)
    {
        return 0U;
    }

    return (uint16_t)((USBD_Device()->DSTS & USB_OTG_DSTS_FNSOF) >> USB_OTG_DSTS_FNSOF_Pos);
}

/* -------------------------------------------------------------------------- */
/* Endpoint management                                                        */
/* -------------------------------------------------------------------------- */

USBD_Result USBD_OpenEndpoint(uint8_t endpoint_address, USBD_EndpointType type, uint16_t max_packet_size)
{
    uint32_t primask;
    USBD_Result result;

    if (!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    primask = USBD_EnterCritical();
    result = USBD_ActivateEndpoint(endpoint_address, type, max_packet_size);
    USBD_ExitCritical(primask);

    return result;
}

void USBD_CloseEndpoint(uint8_t endpoint_address)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;
    uint32_t primask;

    if (!USBD_Initialized || (number == 0U) || (number >= USBD_MAX_ENDPOINTS))
    {
        return;
    }

    primask = USBD_EnterCritical();
    USBD_DeactivateEndpoint(endpoint_address);
    USBD_ExitCritical(primask);
}

void USBD_StallEndpoint(uint8_t endpoint_address, bool stall)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;

    if (!USBD_Initialized || (number >= USBD_MAX_ENDPOINTS))
    {
        return;
    }

    if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        USB_OTG_INEndpointTypeDef *endpoint = USBD_InEndpoint(number);

        if (stall)
        {
            if (((endpoint->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U) && (number != 0U))
            {
                endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_EPDIS;
            }

            endpoint->DIEPCTL |= USB_OTG_DIEPCTL_STALL;
        }
        else
        {
            endpoint->DIEPCTL &= ~USB_OTG_DIEPCTL_STALL;

            if (number != 0U)
            {
                endpoint->DIEPCTL |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
            }
        }
    }
    else
    {
        USB_OTG_OUTEndpointTypeDef *endpoint = USBD_OutEndpoint(number);

        if (stall)
        {
            if (((endpoint->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U) && (number != 0U))
            {
                endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_EPDIS;
            }

            endpoint->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
        }
        else
        {
            endpoint->DOEPCTL &= ~USB_OTG_DOEPCTL_STALL;

            if (number != 0U)
            {
                endpoint->DOEPCTL |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
            }
        }
    }
}

bool USBD_IsEndpointStalled(uint8_t endpoint_address)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;

    if (!USBD_Initialized || (number >= USBD_MAX_ENDPOINTS))
    {
        return false;
    }

    if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        return (USBD_InEndpoint(number)->DIEPCTL & USB_OTG_DIEPCTL_STALL) != 0U;
    }

    return (USBD_OutEndpoint(number)->DOEPCTL & USB_OTG_DOEPCTL_STALL) != 0U;
}

bool USBD_IsEndpointBusy(uint8_t endpoint_address)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;

    if (number >= USBD_MAX_ENDPOINTS)
    {
        return false;
    }

    if ((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U)
    {
        return USBD_InState[number].busy;
    }

    return USBD_OutState[number].busy;
}

/* -------------------------------------------------------------------------- */
/* Transfers                                                                  */
/* -------------------------------------------------------------------------- */

USBD_Result USBD_Transmit(uint8_t endpoint_address, const uint8_t *data, uint16_t length)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;
    USBD_InEndpointState *state;
    USBD_Result result = USBD_RESULT_OK;
    uint32_t primask;

    if (!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if (((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) == 0U) ||
        (number >= USBD_MAX_ENDPOINTS) ||
        ((length != 0U) && (data == NULL)))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    state = &USBD_InState[number];
    primask = USBD_EnterCritical();

    if (state->max_packet == 0U)
    {
        result = USBD_RESULT_INVALID_ARGUMENT;
    }
    else if (state->busy)
    {
        result = USBD_RESULT_BUSY;
    }
    else
    {
        state->buffer = data;
        state->length = length;
        state->written = 0U;
        state->chunk_end = 0U;
        state->busy = true;
        USBD_StartInChunk(number);
    }

    USBD_ExitCritical(primask);

    return result;
}

USBD_Result USBD_Receive(uint8_t endpoint_address, uint8_t *data, uint16_t length)
{
    uint8_t number = endpoint_address & USBD_ENDPOINT_NUMBER_MASK;
    USBD_OutEndpointState *state;
    USBD_Result result = USBD_RESULT_OK;
    uint32_t primask;

    if (!USBD_Initialized)
    {
        return USBD_RESULT_NOT_INITIALIZED;
    }

    if (((endpoint_address & USBD_ENDPOINT_DIRECTION_IN) != 0U) ||
        (number >= USBD_MAX_ENDPOINTS) ||
        ((length != 0U) && (data == NULL)))
    {
        return USBD_RESULT_INVALID_ARGUMENT;
    }

    state = &USBD_OutState[number];
    primask = USBD_EnterCritical();

    if (state->max_packet == 0U)
    {
        result = USBD_RESULT_INVALID_ARGUMENT;
    }
    else if (state->busy)
    {
        result = USBD_RESULT_BUSY;
    }
    else
    {
        state->buffer = data;
        state->length = length;
        state->received = 0U;
        state->chunk_end = 0U;
        state->busy = true;
        USBD_StartOutChunk(number);
    }

    USBD_ExitCritical(primask);

    return result;
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

void USBD_IRQHandler(void)
{
    uint32_t pending;
    uint32_t endpoints;
    uint8_t number;

    if (!USBD_Initialized)
    {
        return;
    }

    pending = USB1_OTG_HS->GINTSTS & USB1_OTG_HS->GINTMSK;

    if ((pending & USB_OTG_GINTSTS_RXFLVL) != 0U)
    {
        USBD_HandleRxFifoLevel();
    }

    if ((pending & USB_OTG_GINTSTS_USBRST) != 0U)
    {
        USBD_HandleBusReset();
    }

    if ((pending & USB_OTG_GINTSTS_OEPINT) != 0U)
    {
        endpoints = (USBD_Device()->DAINT & USBD_Device()->DAINTMSK) >> 16U;

        for (number = 0U; number < USBD_MAX_ENDPOINTS; number++)
        {
            if ((endpoints & (1UL << number)) != 0U)
            {
                USBD_HandleOutEndpointInterrupt(number);
            }
        }
    }

    if ((pending & USB_OTG_GINTSTS_IEPINT) != 0U)
    {
        endpoints = USBD_Device()->DAINT & USBD_Device()->DAINTMSK & 0xFFFFU;

        for (number = 0U; number < USBD_MAX_ENDPOINTS; number++)
        {
            if ((endpoints & (1UL << number)) != 0U)
            {
                USBD_HandleInEndpointInterrupt(number);
            }
        }
    }

    if ((pending & USB_OTG_GINTSTS_PXFR_INCOMPISOOUT) != 0U)
    {
        USBD_HandleIncompleteIsochronousOut();
    }

    if ((pending & USB_OTG_GINTSTS_USBSUSP) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_USBSUSP;

        if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Suspend != NULL))
        {
            USBD_RegisteredCallbacks->Suspend(USBD_CallbackContext);
        }
    }

    if ((pending & USB_OTG_GINTSTS_WKUINT) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_WKUINT;
        USBD_Device()->DCTL &= ~USB_OTG_DCTL_RWUSIG;

        if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->Resume != NULL))
        {
            USBD_RegisteredCallbacks->Resume(USBD_CallbackContext);
        }
    }

    if ((pending & USB_OTG_GINTSTS_SOF) != 0U)
    {
        USB1_OTG_HS->GINTSTS = USB_OTG_GINTSTS_SOF;

        if ((USBD_RegisteredCallbacks != NULL) && (USBD_RegisteredCallbacks->StartOfFrame != NULL))
        {
            USBD_RegisteredCallbacks->StartOfFrame(USBD_CallbackContext, USBD_GetFrameNumber());
        }
    }
}
