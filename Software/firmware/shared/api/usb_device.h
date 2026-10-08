/**
 * @file usb_device.h
 * @brief Platform-neutral USB device-layer contract.
 *
 * The USB device layer owns the default control endpoint (EP0): device state,
 * standard requests, and descriptor routing. One USB function (a class driver
 * such as USB audio) registers with it through USBDevice_Config.
 *
 * The function supplies descriptors and handles class and vendor requests.
 * It moves data on its own endpoints through the endpoint and transfer
 * functions below, which hide the platform's USB controller.
 *
 * Endpoints are identified by their USB endpoint address: bits 0-3 hold the
 * endpoint number and bit 7 (USB_DEVICE_ENDPOINT_DIRECTION_IN) selects IN.
 */

#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/** Endpoint-address bit that selects the IN (device-to-host) direction. */
#define USB_DEVICE_ENDPOINT_DIRECTION_IN    0x80U

/** EP0 maximum packet size the device descriptor must advertise. */
#define USB_DEVICE_EP0_MAX_PACKET_SIZE      64U

/**
 * @brief USB device state as defined by chapter 9 of the USB specification.
 */
typedef enum
{
    USB_DEVICE_STATE_DEFAULT = 0U,
    USB_DEVICE_STATE_ADDRESS,
    USB_DEVICE_STATE_CONFIGURED
} USBDevice_State;

/**
 * @brief Transfer type of a function endpoint, encoded as in its descriptor.
 */
typedef enum
{
    USB_DEVICE_ENDPOINT_ISOCHRONOUS = 1U,
    USB_DEVICE_ENDPOINT_BULK = 2U,
    USB_DEVICE_ENDPOINT_INTERRUPT = 3U
} USBDevice_EndpointType;

/**
 * @brief Decoded eight-byte SETUP packet of a control request.
 */
typedef struct
{
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} USBDevice_SetupPacket;

/**
 * @brief Data stage selected by a class or vendor request handler.
 *
 * The handler receives a zeroed structure. For a device-to-host request it
 * sets in_data and in_length; the device layer truncates the reply to the
 * requested length. For a host-to-device request with a data stage it sets
 * out_data to a buffer of at least setup->length bytes; the received bytes
 * are delivered through control_out_complete. Requests without a data stage
 * leave the structure untouched.
 */
typedef struct
{
    const uint8_t *in_data;
    uint16_t in_length;
    uint8_t *out_data;
} USBDevice_ControlData;

/** Supplies a complete descriptor identified by a GET_DESCRIPTOR request. */
typedef bool (*USBDevice_GetDescriptorFunction)(void *context,
                                                uint8_t descriptor_type,
                                                uint8_t descriptor_index,
                                                uint16_t language_id,
                                                const uint8_t **data,
                                                uint16_t *length);

/** Applies a configuration value. A value of zero unconfigures the function. */
typedef bool (*USBDevice_SetConfigurationFunction)(void *context, uint8_t configuration_value);

/** Applies an alternate setting for an interface. */
typedef bool (*USBDevice_SetInterfaceFunction)(void *context, uint8_t interface_number, uint8_t alternate_setting);

/** Returns the active alternate setting for an interface. */
typedef bool (*USBDevice_GetInterfaceFunction)(void *context, uint8_t interface_number, uint8_t *alternate_setting);

/** Accepts a class or vendor request and selects its data stage. */
typedef bool (*USBDevice_RequestFunction)(void *context, const USBDevice_SetupPacket *setup, USBDevice_ControlData *data);

/** Consumes the data stage of an accepted host-to-device request. */
typedef bool (*USBDevice_ControlOutFunction)(void *context, const USBDevice_SetupPacket *setup, const uint8_t *data, uint16_t length);

/** Reports completion of a USBDevice_Receive() transfer. */
typedef void (*USBDevice_OutCompleteFunction)(void *context, uint8_t endpoint_address, uint8_t *data, uint16_t length);

/** Reports completion of a USBDevice_Transmit() transfer. */
typedef void (*USBDevice_InCompleteFunction)(void *context, uint8_t endpoint_address);

/**
 * @brief USB function registered with the device layer.
 *
 * Only get_descriptor is required. Every callback may execute in USB
 * interrupt context and must not block.
 */
typedef struct
{
    USBDevice_GetDescriptorFunction get_descriptor;
    USBDevice_SetConfigurationFunction set_configuration;
    USBDevice_SetInterfaceFunction set_interface;
    USBDevice_GetInterfaceFunction get_interface;
    USBDevice_RequestFunction handle_request;
    USBDevice_ControlOutFunction control_out_complete;
    USBDevice_OutCompleteFunction out_transfer_complete;
    USBDevice_InCompleteFunction in_transfer_complete;
    void *context;
} USBDevice_Config;

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register a USB function and attach the device to the bus.
 *
 * @param config USB function. Must remain valid until USBDevice_Deinit().
 *
 * @return true on success; otherwise false.
 */
bool USBDevice_Init(const USBDevice_Config *config);

/**
 * @brief Detach the device from the bus and unregister the USB function.
 */
void USBDevice_Deinit(void);

/** @brief Return the current USB device state. */
USBDevice_State USBDevice_GetState(void);

/** @brief Return the active configuration value, or zero when unconfigured. */
uint8_t USBDevice_GetConfiguration(void);

/** @brief Return whether the host has enabled remote wakeup. */
bool USBDevice_IsRemoteWakeupEnabled(void);

/* -------------------------------------------------------------------------- */
/* Function endpoints                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Open a function endpoint.
 *
 * @param endpoint_address Endpoint address. EP0 is reserved.
 * @param type             Endpoint transfer type.
 * @param max_packet_size  Endpoint maximum packet size in bytes.
 *
 * @return true on success; otherwise false.
 */
bool USBDevice_OpenEndpoint(uint8_t endpoint_address, USBDevice_EndpointType type, uint16_t max_packet_size);

/**
 * @brief Close a function endpoint and abandon any transfer in progress.
 *
 * @param endpoint_address Endpoint address.
 */
void USBDevice_CloseEndpoint(uint8_t endpoint_address);

/**
 * @brief Set or clear the halt condition of a function endpoint.
 *
 * @param endpoint_address Endpoint address.
 * @param stall            true to halt the endpoint; false to clear the halt.
 */
void USBDevice_StallEndpoint(uint8_t endpoint_address, bool stall);

/**
 * @brief Determine whether a transfer is in progress on an endpoint.
 *
 * @param endpoint_address Endpoint address.
 *
 * @return true while a transfer is in progress; otherwise false.
 */
bool USBDevice_IsEndpointBusy(uint8_t endpoint_address);

/**
 * @brief Start an IN transfer on a function endpoint.
 *
 * Completion is reported through in_transfer_complete. The buffer must remain
 * valid until then.
 *
 * @return true if the transfer was started; otherwise false.
 */
bool USBDevice_Transmit(uint8_t endpoint_address, const uint8_t *data, uint16_t length);

/**
 * @brief Start an OUT transfer on a function endpoint.
 *
 * Completion is reported through out_transfer_complete. The buffer must
 * remain valid until then. Isochronous transfers complete after one packet.
 *
 * @return true if the transfer was started; otherwise false.
 */
bool USBDevice_Receive(uint8_t endpoint_address, uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEVICE_H */
