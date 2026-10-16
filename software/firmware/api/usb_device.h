/**
 * @file usb_device.h
 * @brief Platform-neutral USB device-layer contract.
 *
 * The USB device layer owns the default control endpoint (EP0): device state,
 * standard requests, and descriptor routing. One USB function (a class driver
 * such as USB audio) registers with it through USBDevice_ConfigTypeDef.
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
} USBDevice_StateTypeDef;

/**
 * @brief Transfer type of a function endpoint, encoded as in its descriptor.
 */
typedef enum
{
    USB_DEVICE_ENDPOINT_ISOCHRONOUS = 1U,
    USB_DEVICE_ENDPOINT_BULK = 2U,
    USB_DEVICE_ENDPOINT_INTERRUPT = 3U
} USBDevice_EndpointTypeTypeDef;

/**
 * @brief Decoded eight-byte SETUP packet of a control request.
 */
typedef struct
{
    uint8_t RequestType;
    uint8_t Request;
    uint16_t Value;
    uint16_t Index;
    uint16_t Length;
} USBDevice_SetupPacketTypeDef;

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
    const uint8_t *InData;
    uint16_t InLength;
    uint8_t *OutData;
} USBDevice_ControlDataTypeDef;

/** Supplies a complete descriptor identified by a GET_DESCRIPTOR request. */
typedef bool (*USBDevice_GetDescriptorFunctionTypeDef)(void *Context,
                                                uint8_t DescriptorType,
                                                uint8_t DescriptorIndex,
                                                uint16_t LanguageId,
                                                const uint8_t **Data,
                                                uint16_t *Length);

/** Applies a configuration value. A value of zero unconfigures the function. */
typedef bool (*USBDevice_SetConfigurationFunctionTypeDef)(void *Context, uint8_t ConfigurationValue);

/** Applies an alternate setting for an interface. */
typedef bool (*USBDevice_SetInterfaceFunctionTypeDef)(void *Context, uint8_t InterfaceNumber, uint8_t AlternateSetting);

/** Returns the active alternate setting for an interface. */
typedef bool (*USBDevice_GetInterfaceFunctionTypeDef)(void *Context, uint8_t InterfaceNumber, uint8_t *AlternateSetting);

/** Accepts a class or vendor request and selects its data stage. */
typedef bool (*USBDevice_RequestFunctionTypeDef)(void *Context, const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data);

/** Consumes the data stage of an accepted host-to-device request. */
typedef bool (*USBDevice_ControlOutFunctionTypeDef)(void *Context, const USBDevice_SetupPacketTypeDef *Setup, const uint8_t *Data, uint16_t Length);

/** Reports completion of a USBDevice_Receive() transfer. */
typedef void (*USBDevice_OutCompleteFunctionTypeDef)(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length);

/** Reports completion of a USBDevice_Transmit() transfer. */
typedef void (*USBDevice_InCompleteFunctionTypeDef)(void *Context, uint8_t EndpointAddress);

/**
 * @brief USB function registered with the device layer.
 *
 * Only get_descriptor is required. Every callback may execute in USB
 * interrupt context and must not block.
 */
typedef struct
{
    USBDevice_GetDescriptorFunctionTypeDef GetDescriptor;
    USBDevice_SetConfigurationFunctionTypeDef SetConfiguration;
    USBDevice_SetInterfaceFunctionTypeDef SetInterface;
    USBDevice_GetInterfaceFunctionTypeDef GetInterface;
    USBDevice_RequestFunctionTypeDef HandleRequest;
    USBDevice_ControlOutFunctionTypeDef ControlOutComplete;
    USBDevice_OutCompleteFunctionTypeDef OutTransferComplete;
    USBDevice_InCompleteFunctionTypeDef InTransferComplete;
    void *Context;
} USBDevice_ConfigTypeDef;

/* -------------------------------------------------------------------------- */
/* Device control                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register a USB function and attach the device to the bus.
 *
 * @param Config USB function. Must remain valid until USBDevice_Deinit().
 *
 * @return true on success; otherwise false.
 */
bool USBDevice_Init(const USBDevice_ConfigTypeDef *Config);

/**
 * @brief Detach the device from the bus and unregister the USB function.
 */
void USBDevice_Deinit(void);

/** @brief Return the current USB device state. */
USBDevice_StateTypeDef USBDevice_GetState(void);

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
 * @param EndpointAddress Endpoint address. EP0 is reserved.
 * @param Type             Endpoint transfer type.
 * @param MaxPacketSize  Endpoint maximum packet size in bytes.
 *
 * @return true on success; otherwise false.
 */
bool USBDevice_OpenEndpoint(uint8_t EndpointAddress, USBDevice_EndpointTypeTypeDef Type, uint16_t MaxPacketSize);

/**
 * @brief Close a function endpoint and abandon any transfer in progress.
 *
 * @param EndpointAddress Endpoint address.
 */
void USBDevice_CloseEndpoint(uint8_t EndpointAddress);

/**
 * @brief Set or clear the halt condition of a function endpoint.
 *
 * @param EndpointAddress Endpoint address.
 * @param Stall            true to halt the endpoint; false to clear the halt.
 */
void USBDevice_StallEndpoint(uint8_t EndpointAddress, bool Stall);

/**
 * @brief Determine whether a transfer is in progress on an endpoint.
 *
 * @param EndpointAddress Endpoint address.
 *
 * @return true while a transfer is in progress; otherwise false.
 */
bool USBDevice_IsEndpointBusy(uint8_t EndpointAddress);

/**
 * @brief Start an IN transfer on a function endpoint.
 *
 * Completion is reported through in_transfer_complete. The buffer must remain
 * valid until then.
 *
 * @return true if the transfer was started; otherwise false.
 */
bool USBDevice_Transmit(uint8_t EndpointAddress, const uint8_t *Data, uint16_t Length);

/**
 * @brief Start an OUT transfer on a function endpoint.
 *
 * Completion is reported through out_transfer_complete. The buffer must
 * remain valid until then. Isochronous transfers complete after one packet.
 *
 * @return true if the transfer was started; otherwise false.
 */
bool USBDevice_Receive(uint8_t EndpointAddress, uint8_t *Data, uint16_t Length);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEVICE_H */
