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
} USBDevice_ControlStageTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBDevice_ConfigTypeDef *USBDevice_Function;
static USBDevice_StateTypeDef USBDevice_CurrentState;
static uint8_t USBDevice_Configuration;
static bool USBDevice_RemoteWakeupEnabled;
static USBDevice_SetupPacketTypeDef USBDevice_Setup;
static USBDevice_ControlStageTypeDef USBDevice_Stage;
static bool USBDevice_ZeroLengthPending;
static uint8_t USBDevice_ControlResponse[2];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void USBDevice_HandleReset(void *Context);
static void USBDevice_HandleSetup(void *Context, const USBD_SetupPacketTypeDef *Setup);
static void USBDevice_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length);
static void USBDevice_HandleInComplete(void *Context, uint8_t EndpointAddress);
static bool USBDevice_HandleStandardRequest(const USBDevice_SetupPacketTypeDef *Setup);
static bool USBDevice_HandleFunctionRequest(const USBDevice_SetupPacketTypeDef *Setup);
static void USBDevice_Unconfigure(void);
static bool USBDevice_SendControlData(const uint8_t *Data, uint16_t Length);
static bool USBDevice_SendControlStatus(void);
static void USBDevice_StallControl(void);
static bool USBDevice_IsFunctionEndpoint(uint8_t EndpointAddress);

static const USBD_CallbacksTypeDef USBDevice_ControllerCallbacks =
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

static void USBDevice_HandleReset(void *Context)
{
    (void)Context;

    USBDevice_Unconfigure();
    USBDevice_CurrentState = USB_DEVICE_STATE_DEFAULT;
    USBDevice_RemoteWakeupEnabled = false;
    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
    USBDevice_ZeroLengthPending = false;
}

static void USBDevice_HandleSetup(void *Context, const USBD_SetupPacketTypeDef *Setup)
{
    bool Handled;

    (void)Context;

    USBDevice_Setup.RequestType = Setup->RequestType;
    USBDevice_Setup.Request = Setup->Request;
    USBDevice_Setup.Value = Setup->Value;
    USBDevice_Setup.Index = Setup->Index;
    USBDevice_Setup.Length = Setup->Length;

    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;
    USBDevice_ZeroLengthPending = false;

    if((USBDevice_Setup.RequestType & USB_DEVICE_REQUEST_TYPE_MASK) == USB_DEVICE_REQUEST_TYPE_STANDARD)
    {
        Handled = USBDevice_HandleStandardRequest(&USBDevice_Setup);
    }
    else
    {
        Handled = USBDevice_HandleFunctionRequest(&USBDevice_Setup);
    }

    if(!Handled)
    {
        USBDevice_StallControl();
    }
}

static void USBDevice_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length)
{
    (void)Context;

    if(EndpointAddress != USBD_ENDPOINT0_OUT)
    {
        if((USBDevice_Function != NULL) && (USBDevice_Function->OutTransferComplete != NULL))
        {
            USBDevice_Function->OutTransferComplete(USBDevice_Function->Context, EndpointAddress, Data, Length);
        }

        return;
    }

    if(USBDevice_Stage != USB_DEVICE_CONTROL_DATA_OUT)
    {
        return;
    }

    USBDevice_Stage = USB_DEVICE_CONTROL_IDLE;

    if((USBDevice_Function == NULL) ||
       (USBDevice_Function->ControlOutComplete == NULL) ||
       !USBDevice_Function->ControlOutComplete(USBDevice_Function->Context, &USBDevice_Setup, Data, Length) ||
       !USBDevice_SendControlStatus())
    {
        USBDevice_StallControl();
    }
}

static void USBDevice_HandleInComplete(void *Context, uint8_t EndpointAddress)
{
    (void)Context;

    if(EndpointAddress != USBD_ENDPOINT0_IN)
    {
        if((USBDevice_Function != NULL) && (USBDevice_Function->InTransferComplete != NULL))
        {
            USBDevice_Function->InTransferComplete(USBDevice_Function->Context, EndpointAddress);
        }

        return;
    }

    if((USBDevice_Stage == USB_DEVICE_CONTROL_DATA_IN) && USBDevice_ZeroLengthPending)
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

static bool USBDevice_HandleStandardRequest(const USBDevice_SetupPacketTypeDef *Setup)
{
    uint8_t Recipient = Setup->RequestType & USB_DEVICE_REQUEST_RECIPIENT_MASK;
    bool DirectionIn = (Setup->RequestType & USB_DEVICE_REQUEST_DIRECTION_IN) != 0U;

    switch(Setup->Request)
    {
        case USB_DEVICE_REQUEST_GET_STATUS:
            if(!DirectionIn || (Setup->Length != 2U))
            {
                return false;
            }

            USBDevice_ControlResponse[0] = 0U;
            USBDevice_ControlResponse[1] = 0U;

            if(Recipient == USB_DEVICE_RECIPIENT_DEVICE)
            {
                USBDevice_ControlResponse[0] = USBDevice_RemoteWakeupEnabled ? 0x02U : 0U;
            }
            else if(Recipient == USB_DEVICE_RECIPIENT_ENDPOINT)
            {
                USBDevice_ControlResponse[0] = USBD_IsEndpointStalled((uint8_t)Setup->Index) ? 0x01U : 0U;
            }
            else if(Recipient != USB_DEVICE_RECIPIENT_INTERFACE)
            {
                return false;
            }

            return USBDevice_SendControlData(USBDevice_ControlResponse, 2U);

        case USB_DEVICE_REQUEST_CLEAR_FEATURE:
        case USB_DEVICE_REQUEST_SET_FEATURE:
            if(DirectionIn || (Setup->Length != 0U))
            {
                return false;
            }

            if((Recipient == USB_DEVICE_RECIPIENT_DEVICE) && (Setup->Value == USB_DEVICE_FEATURE_REMOTE_WAKEUP))
            {
                USBDevice_RemoteWakeupEnabled = (Setup->Request == USB_DEVICE_REQUEST_SET_FEATURE);
                return USBDevice_SendControlStatus();
            }

            if((Recipient == USB_DEVICE_RECIPIENT_ENDPOINT) && (Setup->Value == USB_DEVICE_FEATURE_ENDPOINT_HALT))
            {
                USBD_StallEndpoint((uint8_t)Setup->Index, Setup->Request == USB_DEVICE_REQUEST_SET_FEATURE);
                return USBDevice_SendControlStatus();
            }

            return false;

        case USB_DEVICE_REQUEST_SET_ADDRESS:
            if(DirectionIn || (Recipient != USB_DEVICE_RECIPIENT_DEVICE) || (Setup->Index != 0U) ||
               (Setup->Length != 0U) || (Setup->Value > 127U))
            {
                return false;
            }

            /*
             * The OTG core must hold the new address before the status stage;
             * it completes the status stage at the old address itself.
             */
            USBD_SetAddress((uint8_t)Setup->Value);
            USBDevice_CurrentState = (Setup->Value == 0U) ? USB_DEVICE_STATE_DEFAULT : USB_DEVICE_STATE_ADDRESS;
            return USBDevice_SendControlStatus();

        case USB_DEVICE_REQUEST_GET_DESCRIPTOR:
        {
            const uint8_t *Descriptor = NULL;
            uint16_t DescriptorLength = 0U;

            if(!DirectionIn || (USBDevice_Function == NULL) ||
               !USBDevice_Function->GetDescriptor(USBDevice_Function->Context,
                                                    (uint8_t)(Setup->Value >> 8U),
                                                    (uint8_t)Setup->Value,
                                                    Setup->Index,
                                                    &Descriptor,
                                                    &DescriptorLength) ||
               (Descriptor == NULL))
            {
                return false;
            }

            return USBDevice_SendControlData(Descriptor, DescriptorLength);
        }

        case USB_DEVICE_REQUEST_GET_CONFIGURATION:
            if(!DirectionIn || (Recipient != USB_DEVICE_RECIPIENT_DEVICE) || (Setup->Length != 1U))
            {
                return false;
            }

            USBDevice_ControlResponse[0] = USBDevice_Configuration;
            return USBDevice_SendControlData(USBDevice_ControlResponse, 1U);

        case USB_DEVICE_REQUEST_SET_CONFIGURATION:
            if(DirectionIn || (Recipient != USB_DEVICE_RECIPIENT_DEVICE) || (Setup->Index != 0U) ||
               (Setup->Length != 0U) || (Setup->Value > 255U) ||
               (USBDevice_CurrentState == USB_DEVICE_STATE_DEFAULT))
            {
                return false;
            }

            USBDevice_Unconfigure();

            if(Setup->Value != 0U)
            {
                if((USBDevice_Function == NULL) ||
                   (USBDevice_Function->SetConfiguration == NULL) ||
                   !USBDevice_Function->SetConfiguration(USBDevice_Function->Context, (uint8_t)Setup->Value))
                {
                    return false;
                }

                USBDevice_Configuration = (uint8_t)Setup->Value;
                USBDevice_CurrentState = USB_DEVICE_STATE_CONFIGURED;
            }

            return USBDevice_SendControlStatus();

        case USB_DEVICE_REQUEST_GET_INTERFACE:
            if(!DirectionIn || (Recipient != USB_DEVICE_RECIPIENT_INTERFACE) || (Setup->Length != 1U) ||
               (USBDevice_CurrentState != USB_DEVICE_STATE_CONFIGURED) ||
               (USBDevice_Function == NULL) || (USBDevice_Function->GetInterface == NULL))
            {
                return false;
            }

            if(!USBDevice_Function->GetInterface(USBDevice_Function->Context, (uint8_t)Setup->Index, &USBDevice_ControlResponse[0]))
            {
                return false;
            }

            return USBDevice_SendControlData(USBDevice_ControlResponse, 1U);

        case USB_DEVICE_REQUEST_SET_INTERFACE:
            if(DirectionIn || (Recipient != USB_DEVICE_RECIPIENT_INTERFACE) || (Setup->Length != 0U) ||
               (USBDevice_CurrentState != USB_DEVICE_STATE_CONFIGURED) ||
               (USBDevice_Function == NULL) || (USBDevice_Function->SetInterface == NULL))
            {
                return false;
            }

            if(!USBDevice_Function->SetInterface(USBDevice_Function->Context, (uint8_t)Setup->Index, (uint8_t)Setup->Value))
            {
                return false;
            }

            return USBDevice_SendControlStatus();

        default:
            return false;
    }
}

static bool USBDevice_HandleFunctionRequest(const USBDevice_SetupPacketTypeDef *Setup)
{
    USBDevice_ControlDataTypeDef Data = { 0 };

    if((USBDevice_Function == NULL) ||
       (USBDevice_Function->HandleRequest == NULL) ||
       !USBDevice_Function->HandleRequest(USBDevice_Function->Context, Setup, &Data))
    {
        return false;
    }

    if(Setup->Length == 0U)
    {
        return USBDevice_SendControlStatus();
    }

    if((Setup->RequestType & USB_DEVICE_REQUEST_DIRECTION_IN) != 0U)
    {
        if((Data.InData == NULL) && (Data.InLength != 0U))
        {
            return false;
        }

        return USBDevice_SendControlData(Data.InData, Data.InLength);
    }

    if(Data.OutData == NULL)
    {
        return false;
    }

    USBDevice_Stage = USB_DEVICE_CONTROL_DATA_OUT;

    return USBD_Receive(USBD_ENDPOINT0_OUT, Data.OutData, Setup->Length) == USBD_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Control helpers                                                            */
/* -------------------------------------------------------------------------- */

static void USBDevice_Unconfigure(void)
{
    if(USBDevice_Configuration == 0U)
    {
        return;
    }

    USBDevice_Configuration = 0U;
    USBDevice_CurrentState = USB_DEVICE_STATE_ADDRESS;

    if((USBDevice_Function != NULL) && (USBDevice_Function->SetConfiguration != NULL))
    {
        (void)USBDevice_Function->SetConfiguration(USBDevice_Function->Context, 0U);
    }
}

static bool USBDevice_SendControlData(const uint8_t *Data, uint16_t Length)
{
    if(Length > USBDevice_Setup.Length)
    {
        Length = USBDevice_Setup.Length;
    }

    USBDevice_ZeroLengthPending = (Length < USBDevice_Setup.Length) &&
                                  (Length != 0U) &&
                                  ((Length % USB_DEVICE_EP0_MAX_PACKET_SIZE) == 0U);
    USBDevice_Stage = USB_DEVICE_CONTROL_DATA_IN;

    return USBD_Transmit(USBD_ENDPOINT0_IN, Data, Length) == USBD_RESULT_OK;
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

static bool USBDevice_IsFunctionEndpoint(uint8_t EndpointAddress)
{
    return (EndpointAddress & USBD_ENDPOINT_NUMBER_MASK) != 0U;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool USBDevice_Init(const USBDevice_ConfigTypeDef *Config)
{
    if((Config == NULL) || (Config->GetDescriptor == NULL))
    {
        return false;
    }

    USBDevice_Function = Config;
    USBDevice_Configuration = 0U;
    USBDevice_HandleReset(NULL);

    if(USBD_SetCallbacks(&USBDevice_ControllerCallbacks, NULL) != USBD_RESULT_OK)
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

USBDevice_StateTypeDef USBDevice_GetState(void)
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

bool USBDevice_OpenEndpoint(uint8_t EndpointAddress, USBDevice_EndpointTypeTypeDef Type, uint16_t MaxPacketSize)
{
    if(!USBDevice_IsFunctionEndpoint(EndpointAddress))
    {
        return false;
    }

    return USBD_OpenEndpoint(EndpointAddress, (USBD_EndpointTypeTypeDef)Type, MaxPacketSize) == USBD_RESULT_OK;
}

void USBDevice_CloseEndpoint(uint8_t EndpointAddress)
{
    if(USBDevice_IsFunctionEndpoint(EndpointAddress))
    {
        USBD_CloseEndpoint(EndpointAddress);
    }
}

void USBDevice_StallEndpoint(uint8_t EndpointAddress, bool Stall)
{
    if(USBDevice_IsFunctionEndpoint(EndpointAddress))
    {
        USBD_StallEndpoint(EndpointAddress, Stall);
    }
}

bool USBDevice_IsEndpointBusy(uint8_t EndpointAddress)
{
    return USBD_IsEndpointBusy(EndpointAddress);
}

bool USBDevice_Transmit(uint8_t EndpointAddress, const uint8_t *Data, uint16_t Length)
{
    if(!USBDevice_IsFunctionEndpoint(EndpointAddress))
    {
        return false;
    }

    return USBD_Transmit(EndpointAddress, Data, Length) == USBD_RESULT_OK;
}

bool USBDevice_Receive(uint8_t EndpointAddress, uint8_t *Data, uint16_t Length)
{
    if(!USBDevice_IsFunctionEndpoint(EndpointAddress))
    {
        return false;
    }

    return USBD_Receive(EndpointAddress, Data, Length) == USBD_RESULT_OK;
}
