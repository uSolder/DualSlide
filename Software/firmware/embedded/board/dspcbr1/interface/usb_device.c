/**
 * @file usb_device.c
 * @brief Embedded implementation of the USB device-layer contract.
 *
 * This layer runs on the target USB device controller (usbd.h). It handles
 * EP0 control transfers and standard requests, and forwards class requests and
 * function-endpoint events to the registered USB function.
 */

#include "usb_device.h"

#include "usbd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Request definitions                                                        */
/* -------------------------------------------------------------------------- */

#define USB_DEVICE_REQUEST_DIRECTION_IN                 0x80U
#define USB_DEVICE_REQUEST_TYPE_MASK                    0x60U
#define USB_DEVICE_REQUEST_TYPE_STANDARD                0x00U
#define USB_DEVICE_REQUEST_RECIPIENT_MASK               0x1FU
#define USB_DEVICE_RECIPIENT_DEVICE                     0x00U
#define USB_DEVICE_RECIPIENT_INTERFACE                  0x01U
#define USB_DEVICE_RECIPIENT_ENDPOINT                   0x02U

#define USB_DEVICE_REQUEST_GET_STATUS                   0U
#define USB_DEVICE_REQUEST_CLEAR_FEATURE                1U
#define USB_DEVICE_REQUEST_SET_FEATURE                  3U
#define USB_DEVICE_REQUEST_SET_ADDRESS                  5U
#define USB_DEVICE_REQUEST_GET_DESCRIPTOR               6U
#define USB_DEVICE_REQUEST_GET_CONFIGURATION            8U
#define USB_DEVICE_REQUEST_SET_CONFIGURATION            9U
#define USB_DEVICE_REQUEST_GET_INTERFACE                10U
#define USB_DEVICE_REQUEST_SET_INTERFACE                11U

#define USB_DEVICE_FEATURE_ENDPOINT_HALT                0U
#define USB_DEVICE_FEATURE_REMOTE_WAKEUP                1U

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Progress of the EP0 control transfer in flight.
 */
typedef enum
{
    USB_DEVICE_CONTROL_IDLE = 0,
    USB_DEVICE_CONTROL_DATA_IN,
    USB_DEVICE_CONTROL_DATA_OUT,
    USB_DEVICE_CONTROL_STATUS_IN
} USBDevice_ControlStage;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBDevice_Config *USBDevice_Function;
static USBDevice_State USBDevice_CurrentState;
static uint8_t USBDevice_Configuration;
static bool USBDevice_RemoteWakeupEnabled;
static USBDevice_SetupPacket USBDevice_Setup;
static USBDevice_ControlStage USBDevice_Stage;
static bool USBDevice_ZeroLengthPending;
static uint8_t USBDevice_ControlResponse[2];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void USBDevice_HandleReset(void *context);
static void USBDevice_HandleSetup(void *context, const USBD_SetupPacket *setup);
static void USBDevice_HandleOutComplete(void *context, uint8_t endpoint_address, uint8_t *data, uint16_t length);
static void USBDevice_HandleInComplete(void *context, uint8_t endpoint_address);
static bool USBDevice_HandleStandardRequest(const USBDevice_SetupPacket *setup);
static bool USBDevice_HandleFunctionRequest(const USBDevice_SetupPacket *setup);
static void USBDevice_Unconfigure(void);
static bool USBDevice_SendControlData(const uint8_t *data, uint16_t length);
static bool USBDevice_SendControlStatus(void);
static void USBDevice_StallControl(void);
static bool USBDevice_IsFunctionEndpoint(uint8_t endpoint_address);

static const USBD_Callbacks USBDevice_ControllerCallbacks =
{
    .Reset = USBDevice_HandleReset,
    .SetupReceived = USBDevice_HandleSetup,
    .OutTransferComplete = USBDevice_HandleOutComplete,
    .InTransferComplete = USBDevice_HandleInComplete,
    .Suspend = NULL,
    .Resume = NULL,
    .StartOfFrame = NULL
};

/* -------------------------------------------------------------------------- */
/* Controller events                                                          */
/* -------------------------------------------------------------------------- */

static void USBDevice_HandleReset(void *context)
{
    (void)context;

    USBDevice_Unconfigure();
    USBDevice_CurrentState = USB_DEVICE_STATE_DEFAULT;
    USBDevice_RemoteWakeupEnabled = false;
    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
    USBDevice_ZeroLengthPending = false;
}

static void USBDevice_HandleSetup(void *context, const USBD_SetupPacket *setup)
{
    bool handled;

    (void)context;

    USBDevice_Setup.request_type = setup->request_type;
    USBDevice_Setup.request = setup->request;
    USBDevice_Setup.value = setup->value;
    USBDevice_Setup.index = setup->index;
    USBDevice_Setup.length = setup->length;

    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
    USBDevice_ZeroLengthPending = false;

    if ((USBDevice_Setup.request_type & USB_DEVICE_REQUEST_TYPE_MASK) == USB_DEVICE_REQUEST_TYPE_STANDARD)
    {
        handled = USBDevice_HandleStandardRequest(&USBDevice_Setup);
    }
    else
    {
        handled = USBDevice_HandleFunctionRequest(&USBDevice_Setup);
    }

    if (!handled)
    {
        USBDevice_StallControl();
    }
}

static void USBDevice_HandleOutComplete(void *context, uint8_t endpoint_address, uint8_t *data, uint16_t length)
{
    (void)context;

    if (endpoint_address != USBD_ENDPOINT0_OUT)
    {
        if ((USBDevice_Function != NULL) && (USBDevice_Function->out_transfer_complete != NULL))
        {
            USBDevice_Function->out_transfer_complete(USBDevice_Function->context, endpoint_address, data, length);
        }

        return;
    }

    if (USBDevice_Stage != USB_DEVICE_CONTROL_DATA_OUT)
    {
        return;
    }

    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;

    if ((USBDevice_Function == NULL) ||
        (USBDevice_Function->control_out_complete == NULL) ||
        !USBDevice_Function->control_out_complete(USBDevice_Function->context, &USBDevice_Setup, data, length) ||
        !USBDevice_SendControlStatus())
    {
        USBDevice_StallControl();
    }
}

static void USBDevice_HandleInComplete(void *context, uint8_t endpoint_address)
{
    (void)context;

    if (endpoint_address != USBD_ENDPOINT0_IN)
    {
        if ((USBDevice_Function != NULL) && (USBDevice_Function->in_transfer_complete != NULL))
        {
            USBDevice_Function->in_transfer_complete(USBDevice_Function->context, endpoint_address);
        }

        return;
    }

    if ((USBDevice_Stage == USB_DEVICE_CONTROL_DATA_IN) && USBDevice_ZeroLengthPending)
    {
        /* A reply shorter than requested that fills its last packet ends with a ZLP. */
        USBDevice_ZeroLengthPending = false;
        (void)USBD_Transmit(USBD_ENDPOINT0_IN, NULL, 0U);
        return;
    }

    /* The host's status stage is accepted by the EP0 SETUP arming. */
    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
}

/* -------------------------------------------------------------------------- */
/* Requests                                                                   */
/* -------------------------------------------------------------------------- */

static bool USBDevice_HandleStandardRequest(const USBDevice_SetupPacket *setup)
{
    uint8_t recipient = setup->request_type & USB_DEVICE_REQUEST_RECIPIENT_MASK;
    bool direction_in = (setup->request_type & USB_DEVICE_REQUEST_DIRECTION_IN) != 0U;

    switch (setup->request)
    {
        case USB_DEVICE_REQUEST_GET_STATUS:
            if (!direction_in || (setup->length != 2U))
            {
                return false;
            }

            USBDevice_ControlResponse[0] = 0U;
            USBDevice_ControlResponse[1] = 0U;

            if (recipient == USB_DEVICE_RECIPIENT_DEVICE)
            {
                USBDevice_ControlResponse[0] = USBDevice_RemoteWakeupEnabled ? 0x02U : 0U;
            }
            else if (recipient == USB_DEVICE_RECIPIENT_ENDPOINT)
            {
                USBDevice_ControlResponse[0] = USBD_IsEndpointStalled((uint8_t)setup->index) ? 0x01U : 0U;
            }
            else if (recipient != USB_DEVICE_RECIPIENT_INTERFACE)
            {
                return false;
            }

            return USBDevice_SendControlData(USBDevice_ControlResponse, 2U);

        case USB_DEVICE_REQUEST_CLEAR_FEATURE:
        case USB_DEVICE_REQUEST_SET_FEATURE:
            if (direction_in || (setup->length != 0U))
            {
                return false;
            }

            if ((recipient == USB_DEVICE_RECIPIENT_DEVICE) && (setup->value == USB_DEVICE_FEATURE_REMOTE_WAKEUP))
            {
                USBDevice_RemoteWakeupEnabled = (setup->request == USB_DEVICE_REQUEST_SET_FEATURE);
                return USBDevice_SendControlStatus();
            }

            if ((recipient == USB_DEVICE_RECIPIENT_ENDPOINT) && (setup->value == USB_DEVICE_FEATURE_ENDPOINT_HALT))
            {
                USBD_StallEndpoint((uint8_t)setup->index, setup->request == USB_DEVICE_REQUEST_SET_FEATURE);
                return USBDevice_SendControlStatus();
            }

            return false;

        case USB_DEVICE_REQUEST_SET_ADDRESS:
            if (direction_in || (recipient != USB_DEVICE_RECIPIENT_DEVICE) || (setup->index != 0U) ||
                (setup->length != 0U) || (setup->value > 127U))
            {
                return false;
            }

            /*
             * The OTG core must hold the new address before the status stage;
             * it completes the status stage at the old address itself.
             */
            USBD_SetAddress((uint8_t)setup->value);
            USBDevice_CurrentState = (setup->value == 0U) ? USB_DEVICE_STATE_DEFAULT : USB_DEVICE_STATE_ADDRESS;
            return USBDevice_SendControlStatus();

        case USB_DEVICE_REQUEST_GET_DESCRIPTOR:
        {
            const uint8_t *descriptor = NULL;
            uint16_t descriptor_length = 0U;

            if (!direction_in || (USBDevice_Function == NULL) ||
                !USBDevice_Function->get_descriptor(USBDevice_Function->context,
                                                    (uint8_t)(setup->value >> 8U),
                                                    (uint8_t)setup->value,
                                                    setup->index,
                                                    &descriptor,
                                                    &descriptor_length) ||
                (descriptor == NULL))
            {
                return false;
            }

            return USBDevice_SendControlData(descriptor, descriptor_length);
        }

        case USB_DEVICE_REQUEST_GET_CONFIGURATION:
            if (!direction_in || (recipient != USB_DEVICE_RECIPIENT_DEVICE) || (setup->length != 1U))
            {
                return false;
            }

            USBDevice_ControlResponse[0] = USBDevice_Configuration;
            return USBDevice_SendControlData(USBDevice_ControlResponse, 1U);

        case USB_DEVICE_REQUEST_SET_CONFIGURATION:
            if (direction_in || (recipient != USB_DEVICE_RECIPIENT_DEVICE) || (setup->index != 0U) ||
                (setup->length != 0U) || (setup->value > 255U) ||
                (USBDevice_CurrentState == USB_DEVICE_STATE_DEFAULT))
            {
                return false;
            }

            USBDevice_Unconfigure();

            if (setup->value != 0U)
            {
                if ((USBDevice_Function == NULL) ||
                    (USBDevice_Function->set_configuration == NULL) ||
                    !USBDevice_Function->set_configuration(USBDevice_Function->context, (uint8_t)setup->value))
                {
                    return false;
                }

                USBDevice_Configuration = (uint8_t)setup->value;
                USBDevice_CurrentState = USB_DEVICE_STATE_CONFIGURED;
            }

            return USBDevice_SendControlStatus();

        case USB_DEVICE_REQUEST_GET_INTERFACE:
            if (!direction_in || (recipient != USB_DEVICE_RECIPIENT_INTERFACE) || (setup->length != 1U) ||
                (USBDevice_CurrentState != USB_DEVICE_STATE_CONFIGURED) ||
                (USBDevice_Function == NULL) || (USBDevice_Function->get_interface == NULL))
            {
                return false;
            }

            if (!USBDevice_Function->get_interface(USBDevice_Function->context, (uint8_t)setup->index, &USBDevice_ControlResponse[0]))
            {
                return false;
            }

            return USBDevice_SendControlData(USBDevice_ControlResponse, 1U);

        case USB_DEVICE_REQUEST_SET_INTERFACE:
            if (direction_in || (recipient != USB_DEVICE_RECIPIENT_INTERFACE) || (setup->length != 0U) ||
                (USBDevice_CurrentState != USB_DEVICE_STATE_CONFIGURED) ||
                (USBDevice_Function == NULL) || (USBDevice_Function->set_interface == NULL))
            {
                return false;
            }

            if (!USBDevice_Function->set_interface(USBDevice_Function->context, (uint8_t)setup->index, (uint8_t)setup->value))
            {
                return false;
            }

            return USBDevice_SendControlStatus();

        default:
            return false;
    }
}

static bool USBDevice_HandleFunctionRequest(const USBDevice_SetupPacket *setup)
{
    USBDevice_ControlData data = { 0 };

    if ((USBDevice_Function == NULL) ||
        (USBDevice_Function->handle_request == NULL) ||
        !USBDevice_Function->handle_request(USBDevice_Function->context, setup, &data))
    {
        return false;
    }

    if (setup->length == 0U)
    {
        return USBDevice_SendControlStatus();
    }

    if ((setup->request_type & USB_DEVICE_REQUEST_DIRECTION_IN) != 0U)
    {
        if ((data.in_data == NULL) && (data.in_length != 0U))
        {
            return false;
        }

        return USBDevice_SendControlData(data.in_data, data.in_length);
    }

    if (data.out_data == NULL)
    {
        return false;
    }

    USBDevice_Stage = USB_DEVICE_CONTROL_DATA_OUT;

    return USBD_Receive(USBD_ENDPOINT0_OUT, data.out_data, setup->length) == USBD_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Control helpers                                                            */
/* -------------------------------------------------------------------------- */

static void USBDevice_Unconfigure(void)
{
    if (USBDevice_Configuration == 0U)
    {
        return;
    }

    USBDevice_Configuration = 0U;
    USBDevice_CurrentState = USB_DEVICE_STATE_ADDRESS;

    if ((USBDevice_Function != NULL) && (USBDevice_Function->set_configuration != NULL))
    {
        (void)USBDevice_Function->set_configuration(USBDevice_Function->context, 0U);
    }
}

static bool USBDevice_SendControlData(const uint8_t *data, uint16_t length)
{
    if (length > USBDevice_Setup.length)
    {
        length = USBDevice_Setup.length;
    }

    USBDevice_ZeroLengthPending = (length < USBDevice_Setup.length) &&
                                  (length != 0U) &&
                                  ((length % USB_DEVICE_EP0_MAX_PACKET_SIZE) == 0U);
    USBDevice_Stage = USB_DEVICE_CONTROL_DATA_IN;

    return USBD_Transmit(USBD_ENDPOINT0_IN, data, length) == USBD_RESULT_OK;
}

static bool USBDevice_SendControlStatus(void)
{
    USBDevice_Stage = USB_DEVICE_CONTROL_STATUS_IN;

    return USBD_Transmit(USBD_ENDPOINT0_IN, NULL, 0U) == USBD_RESULT_OK;
}

static void USBDevice_StallControl(void)
{
    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
    USBD_StallEndpoint(USBD_ENDPOINT0_OUT, true);
    USBD_StallEndpoint(USBD_ENDPOINT0_IN, true);
}

static bool USBDevice_IsFunctionEndpoint(uint8_t endpoint_address)
{
    return (endpoint_address & USBD_ENDPOINT_NUMBER_MASK) != 0U;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool USBDevice_Init(const USBDevice_Config *config)
{
    if ((config == NULL) || (config->get_descriptor == NULL))
    {
        return false;
    }

    USBDevice_Function = config;
    USBDevice_Configuration = 0U;
    USBDevice_HandleReset(NULL);

    if (USBD_SetCallbacks(&USBDevice_ControllerCallbacks, NULL) != USBD_RESULT_OK)
    {
        USBDevice_Function = NULL;
        return false;
    }

    USBD_Connect();

    return true;
}

void USBDevice_Deinit(void)
{
    USBD_Disconnect();
    (void)USBD_SetCallbacks(NULL, NULL);

    USBDevice_HandleReset(NULL);
    USBDevice_Function = NULL;
}

USBDevice_State USBDevice_GetState(void)
{
    return USBDevice_CurrentState;
}

uint8_t USBDevice_GetConfiguration(void)
{
    return USBDevice_Configuration;
}

bool USBDevice_IsRemoteWakeupEnabled(void)
{
    return USBDevice_RemoteWakeupEnabled;
}

bool USBDevice_OpenEndpoint(uint8_t endpoint_address, USBDevice_EndpointType type, uint16_t max_packet_size)
{
    if (!USBDevice_IsFunctionEndpoint(endpoint_address))
    {
        return false;
    }

    return USBD_OpenEndpoint(endpoint_address, (USBD_EndpointType)type, max_packet_size) == USBD_RESULT_OK;
}

void USBDevice_CloseEndpoint(uint8_t endpoint_address)
{
    if (USBDevice_IsFunctionEndpoint(endpoint_address))
    {
        USBD_CloseEndpoint(endpoint_address);
    }
}

void USBDevice_StallEndpoint(uint8_t endpoint_address, bool stall)
{
    if (USBDevice_IsFunctionEndpoint(endpoint_address))
    {
        USBD_StallEndpoint(endpoint_address, stall);
    }
}

bool USBDevice_IsEndpointBusy(uint8_t endpoint_address)
{
    return USBD_IsEndpointBusy(endpoint_address);
}

bool USBDevice_Transmit(uint8_t endpoint_address, const uint8_t *data, uint16_t length)
{
    if (!USBDevice_IsFunctionEndpoint(endpoint_address))
    {
        return false;
    }

    return USBD_Transmit(endpoint_address, data, length) == USBD_RESULT_OK;
}

bool USBDevice_Receive(uint8_t endpoint_address, uint8_t *data, uint16_t length)
{
    if (!USBDevice_IsFunctionEndpoint(endpoint_address))
    {
        return false;
    }

    return USBD_Receive(endpoint_address, data, length) == USBD_RESULT_OK;
}
