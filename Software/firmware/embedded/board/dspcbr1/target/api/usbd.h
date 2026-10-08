/**
 * @file usbd.h
 * @brief Hardware-independent USB device-controller interface contract.
 *
 * This interface represents the USB device peripheral of the selected target.
 * It is deliberately not a USB class stack: the target-specific implementation
 * owns the controller, its pins, FIFOs, and interrupt, and moves packets on
 * behalf of a higher layer.
 *
 * The higher layer (the USB device layer and the class drivers built on it)
 * owns descriptors, standard requests, and class requests. It receives bus and
 * endpoint events through USBD_Callbacks and drives endpoints through the
 * endpoint and transfer functions below.
 *
 * Endpoints are identified by their USB endpoint address: bits 0-3 hold the
 * endpoint number and bit 7 (USBD_ENDPOINT_DIRECTION_IN) selects the IN
 * direction.
 */

#ifndef TARGET_INTERFACE_USBD_H
#define TARGET_INTERFACE_USBD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Endpoint addressing                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Number of endpoints supported in each direction, including EP0.
 */
#define USBD_MAX_ENDPOINTS                  8U

/**
 * @brief Endpoint-address bit that selects the IN (device-to-host) direction.
 */
#define USBD_ENDPOINT_DIRECTION_IN          0x80U

/**
 * @brief Endpoint-address bits that hold the endpoint number.
 */
#define USBD_ENDPOINT_NUMBER_MASK           0x0FU

/**
 * @brief Control endpoint OUT and IN addresses.
 */
#define USBD_ENDPOINT0_OUT                  0x00U
#define USBD_ENDPOINT0_IN                   0x80U

/**
 * @brief EP0 maximum packet size used when the configuration does not set one.
 */
#define USBD_DEFAULT_EP0_MAX_PACKET_SIZE    64U

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Result returned by a USB device-controller operation.
 */
typedef enum
{
    USBD_RESULT_OK = 0,
    USBD_RESULT_INVALID_ARGUMENT,
    USBD_RESULT_NOT_INITIALIZED,
    USBD_RESULT_UNSUPPORTED,
    USBD_RESULT_BUSY,
    USBD_RESULT_TIMEOUT
} USBD_Result;

/**
 * @brief USB endpoint transfer type, encoded as in an endpoint descriptor.
 */
typedef enum
{
    USBD_ENDPOINT_CONTROL = 0U,
    USBD_ENDPOINT_ISOCHRONOUS = 1U,
    USBD_ENDPOINT_BULK = 2U,
    USBD_ENDPOINT_INTERRUPT = 3U
} USBD_EndpointType;

/**
 * @brief Decoded eight-byte SETUP packet received on EP0.
 */
typedef struct
{
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} USBD_SetupPacket;

/**
 * @brief Bus and endpoint event callbacks supplied by the higher layer.
 *
 * Every callback runs in USB interrupt context. Callbacks must not block and
 * may call the endpoint and transfer functions of this interface. Any member
 * may be NULL when the event is not required.
 *
 * Endpoint arguments are full endpoint addresses, including
 * USBD_ENDPOINT_DIRECTION_IN for IN endpoints.
 */
typedef struct
{
    /** USB bus reset. Every endpoint except EP0 has been closed. */
    void (*Reset)(void *context);

    /** SETUP packet received on EP0. EP0 is already re-armed for the next SETUP. */
    void (*SetupReceived)(void *context, const USBD_SetupPacket *setup);

    /** OUT transfer started by USBD_Receive() has completed. */
    void (*OutTransferComplete)(void *context, uint8_t endpoint_address, uint8_t *data, uint16_t length);

    /** IN transfer started by USBD_Transmit() has completed. */
    void (*InTransferComplete)(void *context, uint8_t endpoint_address);

    /** Bus entered the suspended state. */
    void (*Suspend)(void *context);

    /** Bus resumed from the suspended state. */
    void (*Resume)(void *context);

    /** Start-of-frame received. The SOF interrupt is enabled only when this is set. */
    void (*StartOfFrame)(void *context, uint16_t frame_number);
} USBD_Callbacks;

/**
 * @brief USB device-controller configuration.
 *
 * Unset members use target defaults. A NULL configuration may be passed to
 * USBD_Init() to initialize the controller without callbacks.
 */
typedef struct
{
    const USBD_Callbacks *callbacks;
    void *callback_context;
    uint16_t ep0_max_packet_size;
} USBD_Config;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the USB device controller and VBUS sensing.
 *
 * The device remains disconnected from the bus until USBD_Connect() is called.
 * EP0 is opened using the configured maximum packet size.
 *
 * @param config Controller configuration. May be NULL.
 *
 * @return USBD_RESULT_OK on success.
 */
USBD_Result USBD_Init(const USBD_Config *config);

/**
 * @brief Disconnect from the bus and power down the USB device controller.
 */
void USBD_Deinit(void);

/**
 * @brief Register or replace the higher-layer event callbacks.
 *
 * @param callbacks        Event callbacks, or NULL to detach the higher layer.
 * @param callback_context Value passed to every callback.
 *
 * @return USBD_RESULT_OK on success.
 */
USBD_Result USBD_SetCallbacks(const USBD_Callbacks *callbacks, void *callback_context);

/* -------------------------------------------------------------------------- */
/* Bus control                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Attach the device to the bus by enabling the D+ pull-up.
 */
void USBD_Connect(void);

/**
 * @brief Detach the device from the bus by disabling the D+ pull-up.
 */
void USBD_Disconnect(void);

/**
 * @brief Determine whether a host or charger is supplying VBUS.
 *
 * @return true when a valid VBUS level is present; otherwise false.
 */
bool USBD_IsVbusPresent(void);

/**
 * @brief Program the device address assigned by the host.
 *
 * @param address Device address in the range 0 to 127.
 */
void USBD_SetAddress(uint8_t address);

/**
 * @brief Signal remote wakeup to a suspended host.
 *
 * Only valid while the bus is suspended and the host has enabled remote
 * wakeup. This function blocks for the duration of the resume signal.
 *
 * @return USBD_RESULT_OK on success;
 *         USBD_RESULT_BUSY if the bus is not suspended.
 */
USBD_Result USBD_SignalRemoteWakeup(void);

/**
 * @brief Return the frame number of the most recent start-of-frame.
 *
 * @return Eleven-bit USB frame number.
 */
uint16_t USBD_GetFrameNumber(void);

/* -------------------------------------------------------------------------- */
/* Endpoint management                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Open an endpoint and reset its data toggle.
 *
 * @param endpoint_address Endpoint address.
 * @param type             Endpoint transfer type.
 * @param max_packet_size  Endpoint maximum packet size in bytes.
 *
 * Isochronous OUT endpoints are supported; isochronous IN endpoints are not
 * yet supported by every target.
 *
 * @return USBD_RESULT_OK on success;
 *         USBD_RESULT_UNSUPPORTED for transfer types the target cannot serve.
 */
USBD_Result USBD_OpenEndpoint(uint8_t endpoint_address, USBD_EndpointType type, uint16_t max_packet_size);

/**
 * @brief Close an endpoint and abandon any transfer in progress on it.
 *
 * No completion callback is issued for an abandoned transfer. EP0 cannot be
 * closed.
 *
 * @param endpoint_address Endpoint address.
 */
void USBD_CloseEndpoint(uint8_t endpoint_address);

/**
 * @brief Set or clear the halt (STALL) condition of an endpoint.
 *
 * Clearing the halt condition also resets the endpoint's data toggle.
 *
 * @param endpoint_address Endpoint address.
 * @param stall            true to halt the endpoint; false to clear the halt.
 */
void USBD_StallEndpoint(uint8_t endpoint_address, bool stall);

/**
 * @brief Determine whether an endpoint is halted.
 *
 * @param endpoint_address Endpoint address.
 *
 * @return true when the endpoint is halted; otherwise false.
 */
bool USBD_IsEndpointStalled(uint8_t endpoint_address);

/**
 * @brief Determine whether a transfer is in progress on an endpoint.
 *
 * @param endpoint_address Endpoint address.
 *
 * @return true while a transfer is in progress; otherwise false.
 */
bool USBD_IsEndpointBusy(uint8_t endpoint_address);

/* -------------------------------------------------------------------------- */
/* Transfers                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start an IN transfer to the host.
 *
 * Transfers longer than the endpoint's maximum packet size are split into
 * packets by the controller. A length of zero sends a zero-length packet.
 * The buffer must remain valid until InTransferComplete is reported for the
 * endpoint.
 *
 * @param endpoint_address IN endpoint address.
 * @param data             Data to transmit. May be NULL when length is zero.
 * @param length           Number of bytes to transmit.
 *
 * @return USBD_RESULT_OK if the transfer was started;
 *         USBD_RESULT_BUSY if a transfer is already in progress.
 */
USBD_Result USBD_Transmit(uint8_t endpoint_address, const uint8_t *data, uint16_t length);

/**
 * @brief Start an OUT transfer from the host.
 *
 * The transfer completes when length bytes have been received or the host
 * sends a short packet. Bytes beyond length are discarded. The buffer must
 * remain valid until OutTransferComplete is reported for the endpoint.
 *
 * An isochronous transfer always completes after one packet, so the caller
 * re-arms the endpoint once per frame from OutTransferComplete. A frame whose
 * packet is lost is skipped without completing the transfer.
 *
 * @param endpoint_address OUT endpoint address.
 * @param data             Destination buffer. May be NULL when length is zero.
 * @param length           Destination buffer size in bytes.
 *
 * @return USBD_RESULT_OK if the transfer was started;
 *         USBD_RESULT_BUSY if a transfer is already in progress.
 */
USBD_Result USBD_Receive(uint8_t endpoint_address, uint8_t *data, uint16_t length);

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handle USB device-controller interrupts.
 *
 * This function is intended to be called directly by the USB peripheral
 * interrupt handler in the target interrupt-vector file.
 */
void USBD_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_USBD_H */
