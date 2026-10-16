/**
 * @file usb_msc.c
 * @brief USB Mass Storage Class device: Bulk-Only Transport carrying SCSI.
 *
 * One interface (0) with a bulk OUT and a bulk IN endpoint. Every command
 * follows the Bulk-Only Transport sequence:
 *
 *   command block wrapper (CBW, OUT) -> optional data stage -> command status
 *   wrapper (CSW, IN)
 *
 * The SCSI command set is the minimum Windows, macOS and Linux use with a
 * removable disk: INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY,
 * READ FORMAT CAPACITIES, MODE SENSE, PREVENT ALLOW MEDIUM REMOVAL,
 * START STOP UNIT, READ(10), WRITE(10), VERIFY(10) and SYNCHRONIZE CACHE.
 *
 * A data stage the device cannot fill is ended with a short (or zero-length)
 * packet rather than an endpoint stall, and unwanted OUT data is read and
 * discarded. Only a malformed CBW stalls both endpoints, after which the host
 * must perform reset recovery.
 *
 * The transport runs in USB interrupt context, but the medium may be slow
 * (a Flash erase takes milliseconds), so it is only accessed from
 * USBMSC_Process() in the main loop. When a command needs the medium, the
 * interrupt leaves a pending operation and starts no transfer; the controller
 * NAKs the host until USBMSC_Process() completes the operation and starts the
 * next transfer. The host sends no new command meanwhile; only a class reset
 * can arrive, which abandons the pending operation.
 */

#include "usb_msc.h"

#include "usb_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Descriptor definitions                                                     */
/* -------------------------------------------------------------------------- */

#define USB_MSC_VENDOR_ID                       (0x1209U) /* pid.codes */
#define USB_MSC_PRODUCT_ID                      (0x0002U) /* pid.codes test PID */
#define USB_MSC_DEVICE_RELEASE                  (0x0100U)

#define USB_MSC_CONFIGURATION_VALUE             (1U)
#define USB_MSC_INTERFACE                       (0U)

#define USB_MSC_ENDPOINT_OUT                    (0x01U)
#define USB_MSC_ENDPOINT_IN                     (0x81U)
#define USB_MSC_MAX_PACKET_SIZE                 (64U)

#define USB_MSC_DESCRIPTOR_DEVICE               (0x01U)
#define USB_MSC_DESCRIPTOR_CONFIGURATION        (0x02U)
#define USB_MSC_DESCRIPTOR_STRING               (0x03U)
#define USB_MSC_DESCRIPTOR_INTERFACE            (0x04U)
#define USB_MSC_DESCRIPTOR_ENDPOINT             (0x05U)

#define USB_MSC_STRING_LANGUAGE                 (0U)
#define USB_MSC_STRING_MANUFACTURER             (1U)
#define USB_MSC_STRING_PRODUCT                  (2U)
#define USB_MSC_STRING_SERIAL                   (3U)
#define USB_MSC_STRING_BUFFER_SIZE              (64U)

#define USB_MSC_CONFIGURATION_LENGTH            (32U)

#define USB_MSC_LOW_BYTE(Value)                 ((uint8_t)((Value) & 0xFFU))
#define USB_MSC_HIGH_BYTE(Value)                ((uint8_t)(((Value) >> 8U) & 0xFFU))

/* -------------------------------------------------------------------------- */
/* Bulk-Only Transport definitions                                            */
/* -------------------------------------------------------------------------- */

#define USB_MSC_REQUEST_RECIPIENT_MASK          (0x1FU)
#define USB_MSC_REQUEST_RECIPIENT_INTERFACE     (0x01U)
#define USB_MSC_REQUEST_DIRECTION_IN            (0x80U)
#define USB_MSC_REQUEST_RESET                   (0xFFU)
#define USB_MSC_REQUEST_GET_MAX_LUN             (0xFEU)

#define USB_MSC_CBW_SIGNATURE                   (0x43425355UL) /* "USBC" */
#define USB_MSC_CBW_LENGTH                      (31U)
#define USB_MSC_CBW_FLAG_DIRECTION_IN           (0x80U)
#define USB_MSC_CBW_MAX_COMMAND_LENGTH          (16U)

#define USB_MSC_CSW_SIGNATURE                   (0x53425355UL) /* "USBS" */
#define USB_MSC_CSW_LENGTH                      (13U)
#define USB_MSC_CSW_STATUS_PASSED               (0U)
#define USB_MSC_CSW_STATUS_FAILED               (1U)
#define USB_MSC_CSW_STATUS_PHASE_ERROR          (2U)

/*
 * Data-stage buffer: a whole number of blocks and of packets. Each pass of
 * USBMSC_Process() moves at most one buffer, so it sets the throughput.
 */
#define USB_MSC_BUFFER_BLOCKS                   (16U)
#define USB_MSC_BUFFER_SIZE                     (USB_MSC_BUFFER_BLOCKS * USB_MSC_BLOCK_SIZE)

_Static_assert((USB_MSC_BUFFER_SIZE % USB_MSC_MAX_PACKET_SIZE) == 0U, "Buffer must hold whole packets");

/* -------------------------------------------------------------------------- */
/* SCSI definitions                                                           */
/* -------------------------------------------------------------------------- */

#define USB_MSC_SCSI_TEST_UNIT_READY            (0x00U)
#define USB_MSC_SCSI_REQUEST_SENSE              (0x03U)
#define USB_MSC_SCSI_INQUIRY                    (0x12U)
#define USB_MSC_SCSI_MODE_SENSE_6               (0x1AU)
#define USB_MSC_SCSI_START_STOP_UNIT            (0x1BU)
#define USB_MSC_SCSI_PREVENT_ALLOW_REMOVAL      (0x1EU)
#define USB_MSC_SCSI_READ_FORMAT_CAPACITIES     (0x23U)
#define USB_MSC_SCSI_READ_CAPACITY_10           (0x25U)
#define USB_MSC_SCSI_READ_10                    (0x28U)
#define USB_MSC_SCSI_WRITE_10                   (0x2AU)
#define USB_MSC_SCSI_VERIFY_10                  (0x2FU)
#define USB_MSC_SCSI_SYNCHRONIZE_CACHE_10       (0x35U)
#define USB_MSC_SCSI_MODE_SENSE_10              (0x5AU)

#define USB_MSC_SENSE_NO_SENSE                  (0x00U)
#define USB_MSC_SENSE_NOT_READY                 (0x02U)
#define USB_MSC_SENSE_MEDIUM_ERROR              (0x03U)
#define USB_MSC_SENSE_ILLEGAL_REQUEST           (0x05U)
#define USB_MSC_SENSE_DATA_PROTECT              (0x07U)

#define USB_MSC_ASC_NONE                        (0x00U)
#define USB_MSC_ASC_WRITE_ERROR                 (0x0CU)
#define USB_MSC_ASC_UNRECOVERED_READ_ERROR      (0x11U)
#define USB_MSC_ASC_INVALID_COMMAND             (0x20U)
#define USB_MSC_ASC_LBA_OUT_OF_RANGE            (0x21U)
#define USB_MSC_ASC_INVALID_FIELD_IN_CDB        (0x24U)
#define USB_MSC_ASC_WRITE_PROTECTED             (0x27U)
#define USB_MSC_ASC_MEDIUM_NOT_PRESENT          (0x3AU)

#define USB_MSC_INQUIRY_LENGTH                  (36U)
#define USB_MSC_REQUEST_SENSE_LENGTH            (18U)
#define USB_MSC_READ_CAPACITY_LENGTH            (8U)
#define USB_MSC_FORMAT_CAPACITIES_LENGTH        (12U)
#define USB_MSC_MODE_SENSE_6_LENGTH             (4U)
#define USB_MSC_MODE_SENSE_10_LENGTH            (8U)
#define USB_MSC_MODE_WRITE_PROTECTED            (0x80U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bulk-Only Transport stage of the command in flight.
 */
typedef enum
{
    USB_MSC_STAGE_COMMAND = 0,
    USB_MSC_STAGE_DATA_IN,
    USB_MSC_STAGE_DATA_OUT,
    USB_MSC_STAGE_ZERO_LENGTH,
    USB_MSC_STAGE_STATUS,
    USB_MSC_STAGE_STALLED
} USBMSC_StageTypeDef;

/**
 * @brief Medium operation waiting for USBMSC_Process().
 */
typedef enum
{
    USB_MSC_PENDING_NONE = 0,
    USB_MSC_PENDING_READ,
    USB_MSC_PENDING_WRITE,
    USB_MSC_PENDING_FLUSH
} USBMSC_PendingTypeDef;

/**
 * @brief Source or destination of the data stage.
 */
typedef enum
{
    USB_MSC_DATA_NONE = 0,
    USB_MSC_DATA_REPLY,
    USB_MSC_DATA_MEDIA,
    USB_MSC_DATA_DISCARD
} USBMSC_DataTypeDef;

/* -------------------------------------------------------------------------- */
/* Descriptors                                                                */
/* -------------------------------------------------------------------------- */

static const uint8_t USBMSC_DeviceDescriptor[] =
{
    18U, USB_MSC_DESCRIPTOR_DEVICE,
    0x00U, 0x02U,                                   /* bcdUSB 2.00 */
    0x00U, 0x00U, 0x00U,                            /* Class defined per interface */
    USB_DEVICE_EP0_MAX_PACKET_SIZE,
    USB_MSC_LOW_BYTE(USB_MSC_VENDOR_ID), USB_MSC_HIGH_BYTE(USB_MSC_VENDOR_ID),
    USB_MSC_LOW_BYTE(USB_MSC_PRODUCT_ID), USB_MSC_HIGH_BYTE(USB_MSC_PRODUCT_ID),
    USB_MSC_LOW_BYTE(USB_MSC_DEVICE_RELEASE), USB_MSC_HIGH_BYTE(USB_MSC_DEVICE_RELEASE),
    USB_MSC_STRING_MANUFACTURER,
    USB_MSC_STRING_PRODUCT,
    USB_MSC_STRING_SERIAL,
    1U                                              /* bNumConfigurations */
};

static const uint8_t USBMSC_ConfigurationDescriptor[] =
{
    /* Configuration */
    9U, USB_MSC_DESCRIPTOR_CONFIGURATION,
    USB_MSC_LOW_BYTE(USB_MSC_CONFIGURATION_LENGTH), USB_MSC_HIGH_BYTE(USB_MSC_CONFIGURATION_LENGTH),
    1U,                                             /* bNumInterfaces */
    USB_MSC_CONFIGURATION_VALUE,
    0U,                                             /* iConfiguration */
    0x80U,                                          /* Bus powered */
    50U,                                            /* 100 mA */

    /* Mass storage interface */
    9U, USB_MSC_DESCRIPTOR_INTERFACE,
    USB_MSC_INTERFACE, 0U, 2U,
    0x08U, 0x06U, 0x50U,                            /* Mass storage, SCSI transparent, bulk-only */
    0U,

    /* Bulk IN endpoint */
    7U, USB_MSC_DESCRIPTOR_ENDPOINT,
    USB_MSC_ENDPOINT_IN,
    0x02U,
    USB_MSC_LOW_BYTE(USB_MSC_MAX_PACKET_SIZE), USB_MSC_HIGH_BYTE(USB_MSC_MAX_PACKET_SIZE),
    0U,

    /* Bulk OUT endpoint */
    7U, USB_MSC_DESCRIPTOR_ENDPOINT,
    USB_MSC_ENDPOINT_OUT,
    0x02U,
    USB_MSC_LOW_BYTE(USB_MSC_MAX_PACKET_SIZE), USB_MSC_HIGH_BYTE(USB_MSC_MAX_PACKET_SIZE),
    0U
};

_Static_assert(sizeof(USBMSC_ConfigurationDescriptor) == USB_MSC_CONFIGURATION_LENGTH,
               "Configuration descriptor length does not match wTotalLength");

static const uint8_t USBMSC_LanguageDescriptor[] =
{
    4U, USB_MSC_DESCRIPTOR_STRING, 0x09U, 0x04U     /* English (United States) */
};

/* Standard INQUIRY data: removable direct-access device. */
static const uint8_t USBMSC_InquiryData[USB_MSC_INQUIRY_LENGTH] =
{
    0x00U,                                          /* Direct-access block device */
    0x80U,                                          /* Removable */
    0x02U,                                          /* SCSI-2 */
    0x02U,                                          /* Response data format */
    USB_MSC_INQUIRY_LENGTH - 5U,                    /* Additional length */
    0x00U, 0x00U, 0x00U,
    'D', 'u', 'a', 'l', 'S', 'l', 'i', 'd',         /* Vendor, 8 characters */
    'G', 'a', 'm', 'e', ' ', 'S', 't', 'o',         /* Product, 16 characters */
    'r', 'a', 'g', 'e', ' ', ' ', ' ', ' ',
    '1', '.', '0', '0'                              /* Revision, 4 characters */
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool USBMSC_GetDescriptor(void *Context, uint8_t DescriptorType, uint8_t DescriptorIndex,
                                 uint16_t LanguageId, const uint8_t **Data, uint16_t *Length);
static bool USBMSC_SetConfiguration(void *Context, uint8_t ConfigurationValue);
static bool USBMSC_SetInterface(void *Context, uint8_t InterfaceNumber, uint8_t AlternateSetting);
static bool USBMSC_GetInterface(void *Context, uint8_t InterfaceNumber, uint8_t *AlternateSetting);
static bool USBMSC_HandleRequest(void *Context, const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data);
static void USBMSC_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length);
static void USBMSC_HandleInComplete(void *Context, uint8_t EndpointAddress);
static uint16_t USBMSC_BuildString(const char *Text);

static bool USBMSC_OpenEndpoints(void);
static void USBMSC_CloseEndpoints(void);
static void USBMSC_ReceiveCommand(void);
static void USBMSC_HandleCommand(const uint8_t *Data, uint16_t Length);
static void USBMSC_ExecuteCommand(const uint8_t *Command);
static void USBMSC_CommandReadWrite(const uint8_t *Command, bool Write);
static void USBMSC_SendReply(uint32_t Length);
static void USBMSC_Fail(uint8_t SenseKey, uint8_t AdditionalSense);
static void USBMSC_PhaseError(void);
static void USBMSC_FinishWithoutData(void);
static void USBMSC_ContinueIn(void);
static void USBMSC_SendData(uint32_t Length);
static void USBMSC_EndDataIn(void);
static void USBMSC_ContinueOut(void);
static void USBMSC_HandleDataOut(uint16_t Length);
static void USBMSC_FinishDataOut(uint32_t Length);
static void USBMSC_ProcessRead(void);
static void USBMSC_ProcessWrite(void);
static void USBMSC_ProcessFlush(void);
static void USBMSC_SendStatus(void);
static void USBMSC_SetSense(uint8_t SenseKey, uint8_t AdditionalSense);
static uint32_t USBMSC_ReadBigEndian32(const uint8_t *Data);
static uint16_t USBMSC_ReadBigEndian16(const uint8_t *Data);
static void USBMSC_WriteBigEndian32(uint8_t *Data, uint32_t Value);
static void USBMSC_WriteLittleEndian32(uint8_t *Data, uint32_t Value);
static uint32_t USBMSC_ReadLittleEndian32(const uint8_t *Data);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBDevice_ConfigTypeDef USBMSC_Function =
{
    .GetDescriptor = USBMSC_GetDescriptor,
    .SetConfiguration = USBMSC_SetConfiguration,
    .SetInterface = USBMSC_SetInterface,
    .GetInterface = USBMSC_GetInterface,
    .HandleRequest = USBMSC_HandleRequest,
    .ControlOutComplete = NULL,
    .OutTransferComplete = USBMSC_HandleOutComplete,
    .InTransferComplete = USBMSC_HandleInComplete,
    .Context = NULL
};

static const USBMSC_MediaTypeDef *USBMSC_Media;
static bool USBMSC_Configured;
static volatile bool USBMSC_Ejected;

static uint8_t USBMSC_StringBuffer[USB_MSC_STRING_BUFFER_SIZE];
static uint8_t USBMSC_ControlBuffer[1];
static uint8_t USBMSC_CommandBuffer[USB_MSC_MAX_PACKET_SIZE] __attribute__((aligned(4)));
static uint8_t USBMSC_StatusBuffer[USB_MSC_CSW_LENGTH] __attribute__((aligned(4)));
static uint8_t USBMSC_DataBuffer[USB_MSC_BUFFER_SIZE] __attribute__((aligned(4)));

/* Command in flight. */
static USBMSC_StageTypeDef USBMSC_Stage;
static uint32_t USBMSC_Tag;
static uint32_t USBMSC_HostLength;
static bool USBMSC_HostDirectionIn;
static uint8_t USBMSC_Status;

/* Data stage. */
static USBMSC_DataTypeDef USBMSC_DataKind;
static uint32_t USBMSC_Transferred;
static uint32_t USBMSC_Block;
static uint32_t USBMSC_BlocksRemaining;
static uint32_t USBMSC_ChunkLength;
static uint32_t USBMSC_ReceivedLength;

/* Set by the USB interrupt, cleared by USBMSC_Process() or a reset. */
static volatile USBMSC_PendingTypeDef USBMSC_Pending;

/* Sense data reported by the next REQUEST SENSE. */
static uint8_t USBMSC_SenseKey;
static uint8_t USBMSC_AdditionalSense;

/* -------------------------------------------------------------------------- */
/* USB function callbacks                                                     */
/* -------------------------------------------------------------------------- */

static bool USBMSC_GetDescriptor(void *Context, uint8_t DescriptorType, uint8_t DescriptorIndex,
                                 uint16_t LanguageId, const uint8_t **Data, uint16_t *Length)
{
    (void)Context;
    (void)LanguageId;

    switch(DescriptorType)
    {
        case USB_MSC_DESCRIPTOR_DEVICE:
            *Data = USBMSC_DeviceDescriptor;
            *Length = sizeof(USBMSC_DeviceDescriptor);
            return true;

        case USB_MSC_DESCRIPTOR_CONFIGURATION:
            *Data = USBMSC_ConfigurationDescriptor;
            *Length = sizeof(USBMSC_ConfigurationDescriptor);
            return true;

        case USB_MSC_DESCRIPTOR_STRING:
            switch(DescriptorIndex)
            {
                case USB_MSC_STRING_LANGUAGE:
                    *Data = USBMSC_LanguageDescriptor;
                    *Length = sizeof(USBMSC_LanguageDescriptor);
                    return true;

                case USB_MSC_STRING_MANUFACTURER:
                    *Length = USBMSC_BuildString("DualSlide");
                    break;

                case USB_MSC_STRING_PRODUCT:
                    *Length = USBMSC_BuildString("DualSlide Game Storage");
                    break;

                case USB_MSC_STRING_SERIAL:
                    /* Bulk-Only Transport asks for at least 12 hexadecimal digits. */
                    *Length = USBMSC_BuildString("000000000001");
                    break;

                default:
                    return false;
            }

            *Data = USBMSC_StringBuffer;
            return true;

        default:
            return false;
    }
}

static bool USBMSC_SetConfiguration(void *Context, uint8_t ConfigurationValue)
{
    (void)Context;

    USBMSC_CloseEndpoints();

    if(ConfigurationValue == 0U)
    {
        return true;
    }

    if(ConfigurationValue != USB_MSC_CONFIGURATION_VALUE)
    {
        return false;
    }

    /* A new configuration puts the disk back in the drive. */
    USBMSC_Ejected = false;
    USBMSC_SetSense(USB_MSC_SENSE_NO_SENSE, USB_MSC_ASC_NONE);

    if(!USBMSC_OpenEndpoints())
    {
        return false;
    }

    USBMSC_ReceiveCommand();

    return true;
}

static bool USBMSC_SetInterface(void *Context, uint8_t InterfaceNumber, uint8_t AlternateSetting)
{
    (void)Context;

    return (InterfaceNumber == USB_MSC_INTERFACE) && (AlternateSetting == 0U);
}

static bool USBMSC_GetInterface(void *Context, uint8_t InterfaceNumber, uint8_t *AlternateSetting)
{
    (void)Context;

    if(InterfaceNumber != USB_MSC_INTERFACE)
    {
        return false;
    }

    *AlternateSetting = 0U;
    return true;
}

static bool USBMSC_HandleRequest(void *Context, const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data)
{
    bool DirectionIn = (Setup->RequestType & USB_MSC_REQUEST_DIRECTION_IN) != 0U;

    (void)Context;

    if(((Setup->RequestType & USB_MSC_REQUEST_RECIPIENT_MASK) != USB_MSC_REQUEST_RECIPIENT_INTERFACE) ||
       (Setup->Index != USB_MSC_INTERFACE) || (Setup->Value != 0U) || !USBMSC_Configured)
    {
        return false;
    }

    switch(Setup->Request)
    {
        case USB_MSC_REQUEST_RESET:
            if(DirectionIn || (Setup->Length != 0U))
            {
                return false;
            }

            /* Abandon the command in flight and wait for a new CBW. */
            USBMSC_CloseEndpoints();

            if(!USBMSC_OpenEndpoints())
            {
                return false;
            }

            USBMSC_ReceiveCommand();
            return true;

        case USB_MSC_REQUEST_GET_MAX_LUN:
            if(!DirectionIn || (Setup->Length != 1U))
            {
                return false;
            }

            USBMSC_ControlBuffer[0] = 0U;                  /* One logical unit */
            Data->InData = USBMSC_ControlBuffer;
            Data->InLength = 1U;
            return true;

        default:
            return false;
    }
}

static void USBMSC_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length)
{
    (void)Context;

    if(EndpointAddress != USB_MSC_ENDPOINT_OUT)
    {
        return;
    }

    if(USBMSC_Stage == USB_MSC_STAGE_COMMAND)
    {
        USBMSC_HandleCommand(Data, Length);
    }
    else if(USBMSC_Stage == USB_MSC_STAGE_DATA_OUT)
    {
        USBMSC_HandleDataOut(Length);
    }
}

static void USBMSC_HandleInComplete(void *Context, uint8_t EndpointAddress)
{
    (void)Context;

    if(EndpointAddress != USB_MSC_ENDPOINT_IN)
    {
        return;
    }

    switch(USBMSC_Stage)
    {
        case USB_MSC_STAGE_DATA_IN:
            USBMSC_ContinueIn();
            break;

        case USB_MSC_STAGE_ZERO_LENGTH:
            USBMSC_SendStatus();
            break;

        case USB_MSC_STAGE_STATUS:
            USBMSC_ReceiveCommand();
            break;

        default:
            break;
    }
}

static uint16_t USBMSC_BuildString(const char *Text)
{
    uint16_t Length = 2U;

    while((*Text != '\0') && ((Length + 2U) <= sizeof(USBMSC_StringBuffer)))
    {
        USBMSC_StringBuffer[Length] = (uint8_t)*Text;
        USBMSC_StringBuffer[Length + 1U] = 0U;
        Length = (uint16_t)(Length + 2U);
        Text++;
    }

    USBMSC_StringBuffer[0] = (uint8_t)Length;
    USBMSC_StringBuffer[1] = USB_MSC_DESCRIPTOR_STRING;

    return Length;
}

/* -------------------------------------------------------------------------- */
/* Bulk-Only Transport                                                        */
/* -------------------------------------------------------------------------- */

static bool USBMSC_OpenEndpoints(void)
{
    if(!USBDevice_OpenEndpoint(USB_MSC_ENDPOINT_OUT, USB_DEVICE_ENDPOINT_BULK, USB_MSC_MAX_PACKET_SIZE) ||
       !USBDevice_OpenEndpoint(USB_MSC_ENDPOINT_IN, USB_DEVICE_ENDPOINT_BULK, USB_MSC_MAX_PACKET_SIZE))
    {
        USBMSC_CloseEndpoints();
        return false;
    }

    USBMSC_Configured = true;
    return true;
}

static void USBMSC_CloseEndpoints(void)
{
    if(USBMSC_Configured)
    {
        USBMSC_Configured = false;
        USBDevice_CloseEndpoint(USB_MSC_ENDPOINT_OUT);
        USBDevice_CloseEndpoint(USB_MSC_ENDPOINT_IN);
    }

    USBMSC_Pending = USB_MSC_PENDING_NONE;
    USBMSC_Stage = USB_MSC_STAGE_COMMAND;
}

static void USBMSC_ReceiveCommand(void)
{
    USBMSC_Stage = USB_MSC_STAGE_COMMAND;

    /* A whole packet, so an oversized CBW shows up as the wrong length. */
    (void)USBDevice_Receive(USB_MSC_ENDPOINT_OUT, USBMSC_CommandBuffer, sizeof(USBMSC_CommandBuffer));
}

static void USBMSC_HandleCommand(const uint8_t *Data, uint16_t Length)
{
    uint8_t CommandLength;

    if((Length != USB_MSC_CBW_LENGTH) || (USBMSC_ReadLittleEndian32(&Data[0]) != USB_MSC_CBW_SIGNATURE))
    {
        /* Not a valid CBW: stall until the host performs reset recovery. */
        USBMSC_Stage = USB_MSC_STAGE_STALLED;
        USBDevice_StallEndpoint(USB_MSC_ENDPOINT_OUT, true);
        USBDevice_StallEndpoint(USB_MSC_ENDPOINT_IN, true);
        return;
    }

    USBMSC_Tag = USBMSC_ReadLittleEndian32(&Data[4]);
    USBMSC_HostLength = USBMSC_ReadLittleEndian32(&Data[8]);
    USBMSC_HostDirectionIn = (Data[12] & USB_MSC_CBW_FLAG_DIRECTION_IN) != 0U;
    CommandLength = Data[14];

    USBMSC_Status = USB_MSC_CSW_STATUS_PASSED;
    USBMSC_DataKind = USB_MSC_DATA_NONE;
    USBMSC_Transferred = 0U;
    USBMSC_BlocksRemaining = 0U;

    if((Data[13] != 0U) || (CommandLength == 0U) || (CommandLength > USB_MSC_CBW_MAX_COMMAND_LENGTH))
    {
        USBMSC_Fail(USB_MSC_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_FIELD_IN_CDB);
        return;
    }

    USBMSC_ExecuteCommand(&Data[15]);
}

/*
 * Starts the data stage of a reply already placed in USBMSC_DataBuffer. A
 * reply longer than the host asked for is cut short, as allocation lengths
 * require.
 */
static void USBMSC_SendReply(uint32_t Length)
{
    if((USBMSC_HostLength != 0U) && !USBMSC_HostDirectionIn)
    {
        USBMSC_PhaseError();
        return;
    }

    if(Length > USBMSC_HostLength)
    {
        Length = USBMSC_HostLength;
    }

    USBMSC_DataKind = USB_MSC_DATA_REPLY;
    USBMSC_ChunkLength = Length;
    USBMSC_FinishWithoutData();
}

/* Completes the command as failed with the given sense, skipping its data. */
static void USBMSC_Fail(uint8_t SenseKey, uint8_t AdditionalSense)
{
    USBMSC_SetSense(SenseKey, AdditionalSense);
    USBMSC_Status = USB_MSC_CSW_STATUS_FAILED;
    USBMSC_DataKind = USB_MSC_DATA_NONE;
    USBMSC_FinishWithoutData();
}

/* The host and device disagree on the data stage; the host will reset. */
static void USBMSC_PhaseError(void)
{
    USBMSC_Status = USB_MSC_CSW_STATUS_PHASE_ERROR;
    USBMSC_DataKind = USB_MSC_DATA_NONE;
    USBMSC_FinishWithoutData();
}

/*
 * Runs whatever data stage the host announced, then the status stage. With
 * no reply or media transfer set up, IN stages end with a zero-length packet
 * and OUT data is discarded.
 */
static void USBMSC_FinishWithoutData(void)
{
    if(USBMSC_HostLength == 0U)
    {
        USBMSC_SendStatus();
    }
    else if(USBMSC_HostDirectionIn)
    {
        USBMSC_Stage = USB_MSC_STAGE_DATA_IN;
        USBMSC_ContinueIn();
    }
    else
    {
        if(USBMSC_DataKind != USB_MSC_DATA_MEDIA)
        {
            USBMSC_DataKind = USB_MSC_DATA_DISCARD;
        }

        USBMSC_Stage = USB_MSC_STAGE_DATA_OUT;
        USBMSC_ContinueOut();
    }
}

static void USBMSC_ContinueIn(void)
{
    if((USBMSC_DataKind == USB_MSC_DATA_REPLY) && (USBMSC_ChunkLength != 0U))
    {
        USBMSC_DataKind = USB_MSC_DATA_NONE;
        USBMSC_SendData(USBMSC_ChunkLength);
        return;
    }

    if((USBMSC_DataKind == USB_MSC_DATA_MEDIA) && (USBMSC_BlocksRemaining != 0U))
    {
        USBMSC_Pending = USB_MSC_PENDING_READ;
        return;
    }

    USBMSC_EndDataIn();
}

static void USBMSC_SendData(uint32_t Length)
{
    USBMSC_Transferred += Length;
    (void)USBDevice_Transmit(USB_MSC_ENDPOINT_IN, USBMSC_DataBuffer, (uint16_t)Length);
}

static void USBMSC_EndDataIn(void)
{
    /*
     * Data stage over. A shortfall that ends on a packet boundary needs a
     * zero-length packet so the host knows no more data is coming.
     */
    if((USBMSC_Transferred < USBMSC_HostLength) && ((USBMSC_Transferred % USB_MSC_MAX_PACKET_SIZE) == 0U))
    {
        USBMSC_Stage = USB_MSC_STAGE_ZERO_LENGTH;
        (void)USBDevice_Transmit(USB_MSC_ENDPOINT_IN, NULL, 0U);
        return;
    }

    USBMSC_SendStatus();
}

static void USBMSC_ContinueOut(void)
{
    uint32_t Remaining = USBMSC_HostLength - USBMSC_Transferred;

    if(Remaining == 0U)
    {
        USBMSC_SendStatus();
        return;
    }

    USBMSC_ChunkLength = (Remaining < USB_MSC_BUFFER_SIZE) ? Remaining : USB_MSC_BUFFER_SIZE;
    (void)USBDevice_Receive(USB_MSC_ENDPOINT_OUT, USBMSC_DataBuffer, (uint16_t)USBMSC_ChunkLength);
}

static void USBMSC_HandleDataOut(uint16_t Length)
{
    if(USBMSC_DataKind == USB_MSC_DATA_MEDIA)
    {
        USBMSC_ReceivedLength = Length;
        USBMSC_Pending = USB_MSC_PENDING_WRITE;
        return;
    }

    USBMSC_FinishDataOut(Length);
}

static void USBMSC_FinishDataOut(uint32_t Length)
{
    USBMSC_Transferred += Length;

    /* A short packet ends the data stage early. */
    if(Length < USBMSC_ChunkLength)
    {
        USBMSC_SendStatus();
        return;
    }

    USBMSC_ContinueOut();
}

static void USBMSC_SendStatus(void)
{
    uint32_t Residue = (USBMSC_Transferred < USBMSC_HostLength) ? (USBMSC_HostLength - USBMSC_Transferred) : 0U;

    USBMSC_WriteLittleEndian32(&USBMSC_StatusBuffer[0], USB_MSC_CSW_SIGNATURE);
    USBMSC_WriteLittleEndian32(&USBMSC_StatusBuffer[4], USBMSC_Tag);
    USBMSC_WriteLittleEndian32(&USBMSC_StatusBuffer[8], Residue);
    USBMSC_StatusBuffer[12] = USBMSC_Status;

    USBMSC_Stage = USB_MSC_STAGE_STATUS;
    (void)USBDevice_Transmit(USB_MSC_ENDPOINT_IN, USBMSC_StatusBuffer, USB_MSC_CSW_LENGTH);
}

/* -------------------------------------------------------------------------- */
/* SCSI commands                                                              */
/* -------------------------------------------------------------------------- */

static void USBMSC_ExecuteCommand(const uint8_t *Command)
{
    uint8_t *Reply = USBMSC_DataBuffer;
    uint32_t LastBlock;

    switch(Command[0])
    {
        case USB_MSC_SCSI_TEST_UNIT_READY:
            if(USBMSC_Ejected)
            {
                USBMSC_Fail(USB_MSC_SENSE_NOT_READY, USB_MSC_ASC_MEDIUM_NOT_PRESENT);
                return;
            }

            USBMSC_FinishWithoutData();
            return;

        case USB_MSC_SCSI_REQUEST_SENSE:
            memset(Reply, 0, USB_MSC_REQUEST_SENSE_LENGTH);
            Reply[0] = 0x70U;                               /* Current error, fixed format */
            Reply[2] = USBMSC_SenseKey;
            Reply[7] = USB_MSC_REQUEST_SENSE_LENGTH - 8U;   /* Additional sense length */
            Reply[12] = USBMSC_AdditionalSense;
            USBMSC_SetSense(USB_MSC_SENSE_NO_SENSE, USB_MSC_ASC_NONE);
            USBMSC_SendReply((Command[4] < USB_MSC_REQUEST_SENSE_LENGTH) ? Command[4] : USB_MSC_REQUEST_SENSE_LENGTH);
            return;

        case USB_MSC_SCSI_INQUIRY:
        {
            uint16_t Allocation = USBMSC_ReadBigEndian16(&Command[3]);

            /* Vital product data pages are not provided. */
            if((Command[1] & 0x01U) != 0U)
            {
                USBMSC_Fail(USB_MSC_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_FIELD_IN_CDB);
                return;
            }

            memcpy(Reply, USBMSC_InquiryData, USB_MSC_INQUIRY_LENGTH);
            USBMSC_SendReply((Allocation < USB_MSC_INQUIRY_LENGTH) ? Allocation : USB_MSC_INQUIRY_LENGTH);
            return;
        }

        case USB_MSC_SCSI_READ_FORMAT_CAPACITIES:
        {
            uint16_t Allocation = USBMSC_ReadBigEndian16(&Command[7]);

            memset(Reply, 0, USB_MSC_FORMAT_CAPACITIES_LENGTH);
            Reply[3] = 8U;                                  /* Capacity list length */
            USBMSC_WriteBigEndian32(&Reply[4], USBMSC_Media->BlockCount);
            Reply[8] = USBMSC_Ejected ? 0x03U : 0x02U;      /* No medium / formatted medium */
            Reply[9] = (uint8_t)((USB_MSC_BLOCK_SIZE >> 16U) & 0xFFU);
            Reply[10] = (uint8_t)((USB_MSC_BLOCK_SIZE >> 8U) & 0xFFU);
            Reply[11] = (uint8_t)(USB_MSC_BLOCK_SIZE & 0xFFU);
            USBMSC_SendReply((Allocation < USB_MSC_FORMAT_CAPACITIES_LENGTH) ? Allocation : USB_MSC_FORMAT_CAPACITIES_LENGTH);
            return;
        }

        case USB_MSC_SCSI_READ_CAPACITY_10:
            if(USBMSC_Ejected)
            {
                USBMSC_Fail(USB_MSC_SENSE_NOT_READY, USB_MSC_ASC_MEDIUM_NOT_PRESENT);
                return;
            }

            LastBlock = USBMSC_Media->BlockCount - 1U;
            USBMSC_WriteBigEndian32(&Reply[0], LastBlock);
            USBMSC_WriteBigEndian32(&Reply[4], USB_MSC_BLOCK_SIZE);
            USBMSC_SendReply(USB_MSC_READ_CAPACITY_LENGTH);
            return;

        case USB_MSC_SCSI_MODE_SENSE_6:
            /* Header only: no block descriptors or mode pages. */
            memset(Reply, 0, USB_MSC_MODE_SENSE_6_LENGTH);
            Reply[0] = USB_MSC_MODE_SENSE_6_LENGTH - 1U;    /* Mode data length */
            Reply[2] = (USBMSC_Media->Write == NULL) ? USB_MSC_MODE_WRITE_PROTECTED : 0U;
            USBMSC_SendReply((Command[4] < USB_MSC_MODE_SENSE_6_LENGTH) ? Command[4] : USB_MSC_MODE_SENSE_6_LENGTH);
            return;

        case USB_MSC_SCSI_MODE_SENSE_10:
        {
            uint16_t Allocation = USBMSC_ReadBigEndian16(&Command[7]);

            memset(Reply, 0, USB_MSC_MODE_SENSE_10_LENGTH);
            Reply[1] = USB_MSC_MODE_SENSE_10_LENGTH - 2U;   /* Mode data length */
            Reply[3] = (USBMSC_Media->Write == NULL) ? USB_MSC_MODE_WRITE_PROTECTED : 0U;
            USBMSC_SendReply((Allocation < USB_MSC_MODE_SENSE_10_LENGTH) ? Allocation : USB_MSC_MODE_SENSE_10_LENGTH);
            return;
        }

        case USB_MSC_SCSI_START_STOP_UNIT:
            /* LoEj with Start clear ejects the disk; with Start set, loads it. */
            if((Command[4] & 0x02U) != 0U)
            {
                USBMSC_Ejected = (Command[4] & 0x01U) == 0U;
            }

            /* Everything the host wrote must be permanent before the eject completes. */
            USBMSC_Pending = USB_MSC_PENDING_FLUSH;
            return;

        case USB_MSC_SCSI_SYNCHRONIZE_CACHE_10:
            USBMSC_Pending = USB_MSC_PENDING_FLUSH;
            return;

        case USB_MSC_SCSI_PREVENT_ALLOW_REMOVAL:
            USBMSC_FinishWithoutData();
            return;

        case USB_MSC_SCSI_VERIFY_10:
            /* Only the no-data form (BYTCHK clear) is supported. */
            if((Command[1] & 0x02U) != 0U)
            {
                USBMSC_Fail(USB_MSC_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_FIELD_IN_CDB);
                return;
            }

            USBMSC_FinishWithoutData();
            return;

        case USB_MSC_SCSI_READ_10:
            USBMSC_CommandReadWrite(Command, false);
            return;

        case USB_MSC_SCSI_WRITE_10:
            USBMSC_CommandReadWrite(Command, true);
            return;

        default:
            USBMSC_Fail(USB_MSC_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_INVALID_COMMAND);
            return;
    }
}

static void USBMSC_CommandReadWrite(const uint8_t *Command, bool Write)
{
    uint32_t Block = USBMSC_ReadBigEndian32(&Command[2]);
    uint32_t Count = USBMSC_ReadBigEndian16(&Command[7]);

    if(USBMSC_Ejected)
    {
        USBMSC_Fail(USB_MSC_SENSE_NOT_READY, USB_MSC_ASC_MEDIUM_NOT_PRESENT);
        return;
    }

    if((Block >= USBMSC_Media->BlockCount) || (Count > (USBMSC_Media->BlockCount - Block)))
    {
        USBMSC_Fail(USB_MSC_SENSE_ILLEGAL_REQUEST, USB_MSC_ASC_LBA_OUT_OF_RANGE);
        return;
    }

    if(Write && (USBMSC_Media->Write == NULL))
    {
        USBMSC_Fail(USB_MSC_SENSE_DATA_PROTECT, USB_MSC_ASC_WRITE_PROTECTED);
        return;
    }

    /* The host must expect exactly the blocks requested, in the right direction. */
    if((Count != 0U) &&
       ((USBMSC_HostLength != (Count * USB_MSC_BLOCK_SIZE)) || (USBMSC_HostDirectionIn == Write)))
    {
        USBMSC_PhaseError();
        return;
    }

    USBMSC_DataKind = USB_MSC_DATA_MEDIA;
    USBMSC_Block = Block;
    USBMSC_BlocksRemaining = Count;
    USBMSC_FinishWithoutData();
}

/* -------------------------------------------------------------------------- */
/* Medium operations                                                          */
/* -------------------------------------------------------------------------- */

static void USBMSC_ProcessRead(void)
{
    uint32_t Count = (USBMSC_BlocksRemaining < USB_MSC_BUFFER_BLOCKS) ? USBMSC_BlocksRemaining : USB_MSC_BUFFER_BLOCKS;

    if(!USBMSC_Media->Read(USBMSC_Media->Context, USBMSC_Block, USBMSC_DataBuffer, Count))
    {
        USBMSC_SetSense(USB_MSC_SENSE_MEDIUM_ERROR, USB_MSC_ASC_UNRECOVERED_READ_ERROR);
        USBMSC_Status = USB_MSC_CSW_STATUS_FAILED;
        USBMSC_DataKind = USB_MSC_DATA_NONE;
        USBMSC_EndDataIn();
        return;
    }

    USBMSC_Block += Count;
    USBMSC_BlocksRemaining -= Count;
    USBMSC_SendData(Count * USB_MSC_BLOCK_SIZE);
}

static void USBMSC_ProcessWrite(void)
{
    uint32_t Count = USBMSC_ReceivedLength / USB_MSC_BLOCK_SIZE;

    if(Count > USBMSC_BlocksRemaining)
    {
        Count = USBMSC_BlocksRemaining;
    }

    if((Count != 0U) && !USBMSC_Media->Write(USBMSC_Media->Context, USBMSC_Block, USBMSC_DataBuffer, Count))
    {
        /* Keep reading the host's data, but write no more of it. */
        USBMSC_SetSense(USB_MSC_SENSE_MEDIUM_ERROR, USB_MSC_ASC_WRITE_ERROR);
        USBMSC_Status = USB_MSC_CSW_STATUS_FAILED;
        USBMSC_DataKind = USB_MSC_DATA_DISCARD;
    }
    else
    {
        USBMSC_Block += Count;
        USBMSC_BlocksRemaining -= Count;
    }

    USBMSC_FinishDataOut(USBMSC_ReceivedLength);
}

static void USBMSC_ProcessFlush(void)
{
    if((USBMSC_Media->Flush != NULL) && !USBMSC_Media->Flush(USBMSC_Media->Context))
    {
        USBMSC_Fail(USB_MSC_SENSE_MEDIUM_ERROR, USB_MSC_ASC_WRITE_ERROR);
        return;
    }

    USBMSC_FinishWithoutData();
}

static void USBMSC_SetSense(uint8_t SenseKey, uint8_t AdditionalSense)
{
    USBMSC_SenseKey = SenseKey;
    USBMSC_AdditionalSense = AdditionalSense;
}

/* -------------------------------------------------------------------------- */
/* Byte order                                                                 */
/* -------------------------------------------------------------------------- */

static uint32_t USBMSC_ReadBigEndian32(const uint8_t *Data)
{
    return ((uint32_t)Data[0] << 24U) | ((uint32_t)Data[1] << 16U) | ((uint32_t)Data[2] << 8U) | (uint32_t)Data[3];
}

static uint16_t USBMSC_ReadBigEndian16(const uint8_t *Data)
{
    return (uint16_t)(((uint16_t)Data[0] << 8U) | (uint16_t)Data[1]);
}

static void USBMSC_WriteBigEndian32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)(Value >> 24U);
    Data[1] = (uint8_t)(Value >> 16U);
    Data[2] = (uint8_t)(Value >> 8U);
    Data[3] = (uint8_t)Value;
}

static uint32_t USBMSC_ReadLittleEndian32(const uint8_t *Data)
{
    return (uint32_t)Data[0] | ((uint32_t)Data[1] << 8U) | ((uint32_t)Data[2] << 16U) | ((uint32_t)Data[3] << 24U);
}

static void USBMSC_WriteLittleEndian32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8U);
    Data[2] = (uint8_t)(Value >> 16U);
    Data[3] = (uint8_t)(Value >> 24U);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool USBMSC_Init(const USBMSC_MediaTypeDef *Media)
{
    if((Media == NULL) || (Media->Read == NULL) || (Media->BlockCount == 0U))
    {
        return false;
    }

    USBMSC_Media = Media;
    USBMSC_Configured = false;
    USBMSC_Ejected = false;
    USBMSC_Stage = USB_MSC_STAGE_COMMAND;
    USBMSC_Pending = USB_MSC_PENDING_NONE;
    USBMSC_SetSense(USB_MSC_SENSE_NO_SENSE, USB_MSC_ASC_NONE);

    return USBDevice_Init(&USBMSC_Function);
}

void USBMSC_Process(void)
{
    USBMSC_PendingTypeDef Pending = USBMSC_Pending;

    if(Pending == USB_MSC_PENDING_NONE)
    {
        return;
    }

    USBMSC_Pending = USB_MSC_PENDING_NONE;

    switch(Pending)
    {
        case USB_MSC_PENDING_READ:
            USBMSC_ProcessRead();
            break;

        case USB_MSC_PENDING_WRITE:
            USBMSC_ProcessWrite();
            break;

        case USB_MSC_PENDING_FLUSH:
            USBMSC_ProcessFlush();
            break;

        default:
            break;
    }
}

void USBMSC_Deinit(void)
{
    USBDevice_Deinit();
    USBMSC_CloseEndpoints();
}

bool USBMSC_IsEjected(void)
{
    return USBMSC_Ejected;
}
