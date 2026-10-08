/**
 * @file usb_audio.c
 * @brief USB Audio Class 1.0 speaker service.
 *
 * Topology: USB streaming input terminal (ID 1) -> feature unit (ID 3) ->
 * speaker output terminal (ID 2). The feature unit exposes master mute and
 * volume, so hosts such as iOS and Windows drive the volume on the device;
 * it is applied as a gain during rate conversion.
 *
 * The streaming interface (1) has an idle alternate setting 0 and an active
 * alternate setting 1 with one adaptive isochronous OUT endpoint carrying
 * 48 kHz, 16-bit, stereo PCM.
 *
 * Each pair of stereo frames is averaged into one mono sample, giving 24 kHz
 * mono. The two-frame average also acts as a simple low-pass filter ahead of
 * the rate reduction.
 *
 * Samples are queued in a single-producer, single-consumer jitter buffer: the
 * USB interrupt produces samples and the audio output interrupt consumes them.
 * Each index is written by one side only.
 */

#include "usb_audio.h"

#include "usb_device.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Descriptor definitions                                                     */
/* -------------------------------------------------------------------------- */

#define USB_AUDIO_VENDOR_ID                     (0x1209U) /* pid.codes */
#define USB_AUDIO_PRODUCT_ID                    (0x0001U) /* pid.codes test PID */
#define USB_AUDIO_DEVICE_RELEASE                (0x0101U)

#define USB_AUDIO_CONFIGURATION_VALUE           (1U)
#define USB_AUDIO_CONTROL_INTERFACE             (0U)
#define USB_AUDIO_STREAMING_INTERFACE           (1U)
#define USB_AUDIO_STREAMING_ALTERNATE_IDLE      (0U)
#define USB_AUDIO_STREAMING_ALTERNATE_ACTIVE    (1U)

#define USB_AUDIO_ENDPOINT                      (0x01U)
#define USB_AUDIO_BYTES_PER_FRAME               (USB_AUDIO_HOST_CHANNEL_COUNT * 2U)
#define USB_AUDIO_FRAMES_PER_SAMPLE             (USB_AUDIO_HOST_SAMPLE_RATE_HZ / USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ)

/* One spare frame per packet tolerates a host running slightly fast. */
#define USB_AUDIO_MAX_PACKET_SIZE               (((USB_AUDIO_HOST_SAMPLE_RATE_HZ / 1000U) + 1U) * USB_AUDIO_BYTES_PER_FRAME)

#define USB_AUDIO_DESCRIPTOR_DEVICE             (0x01U)
#define USB_AUDIO_DESCRIPTOR_CONFIGURATION      (0x02U)
#define USB_AUDIO_DESCRIPTOR_STRING             (0x03U)
#define USB_AUDIO_DESCRIPTOR_INTERFACE          (0x04U)
#define USB_AUDIO_DESCRIPTOR_ENDPOINT           (0x05U)
#define USB_AUDIO_DESCRIPTOR_CS_INTERFACE       (0x24U)
#define USB_AUDIO_DESCRIPTOR_CS_ENDPOINT        (0x25U)

#define USB_AUDIO_STRING_LANGUAGE               (0U)
#define USB_AUDIO_STRING_MANUFACTURER           (1U)
#define USB_AUDIO_STRING_PRODUCT                (2U)
#define USB_AUDIO_STRING_SERIAL                 (3U)
#define USB_AUDIO_STRING_BUFFER_SIZE            (64U)

#define USB_AUDIO_INPUT_TERMINAL_ID             (1U)
#define USB_AUDIO_OUTPUT_TERMINAL_ID            (2U)
#define USB_AUDIO_FEATURE_UNIT_ID               (3U)

#define USB_AUDIO_CONTROL_HEADER_LENGTH         (40U)
#define USB_AUDIO_CONFIGURATION_LENGTH          (110U)

#define USB_AUDIO_LOW_BYTE(Value)               ((uint8_t)((Value) & 0xFFU))
#define USB_AUDIO_HIGH_BYTE(Value)              ((uint8_t)(((Value) >> 8U) & 0xFFU))

/* -------------------------------------------------------------------------- */
/* Class request definitions                                                  */
/* -------------------------------------------------------------------------- */

#define USB_AUDIO_REQUEST_RECIPIENT_MASK        (0x1FU)
#define USB_AUDIO_REQUEST_RECIPIENT_ENDPOINT    (0x02U)
#define USB_AUDIO_REQUEST_SET_CUR               (0x01U)
#define USB_AUDIO_REQUEST_GET_CUR               (0x81U)
#define USB_AUDIO_SAMPLING_FREQUENCY_CONTROL    (0x01U)
#define USB_AUDIO_SAMPLING_FREQUENCY_LENGTH     (3U)

#define USB_AUDIO_REQUEST_RECIPIENT_INTERFACE   (0x01U)
#define USB_AUDIO_REQUEST_GET_MIN               (0x82U)
#define USB_AUDIO_REQUEST_GET_MAX               (0x83U)
#define USB_AUDIO_REQUEST_GET_RES               (0x84U)
#define USB_AUDIO_MUTE_CONTROL                  (0x01U)
#define USB_AUDIO_VOLUME_CONTROL                (0x02U)
#define USB_AUDIO_MUTE_LENGTH                   (1U)
#define USB_AUDIO_VOLUME_LENGTH                 (2U)
#define USB_AUDIO_CONTROL_BUFFER_SIZE           (3U)

/* Volume in 1/256 dB steps: -50 dB to 0 dB in 1 dB steps. */
#define USB_AUDIO_VOLUME_MIN                    (-50 * 256)
#define USB_AUDIO_VOLUME_MAX                    (0)
#define USB_AUDIO_VOLUME_RESOLUTION             (256)
#define USB_AUDIO_UNITY_GAIN_Q15                (32768)

/* -------------------------------------------------------------------------- */
/* Jitter buffer                                                              */
/* -------------------------------------------------------------------------- */

/* Capacity in output samples; must be a power of two. 1024 samples is ~43 ms. */
#define USB_AUDIO_BUFFER_SAMPLES                (1024U)
#define USB_AUDIO_BUFFER_MASK                   (USB_AUDIO_BUFFER_SAMPLES - 1U)
#define USB_AUDIO_BUFFER_START_LEVEL            (USB_AUDIO_BUFFER_SAMPLES / 2U)
#define USB_AUDIO_BUFFER_HIGH_LEVEL             ((USB_AUDIO_BUFFER_SAMPLES * 3U) / 4U)
#define USB_AUDIO_BUFFER_LOW_LEVEL              (USB_AUDIO_BUFFER_SAMPLES / 4U)

/* Orders buffer stores before the index store that publishes them. */
#define USB_AUDIO_COMPILER_BARRIER()            __asm volatile ("" ::: "memory")

_Static_assert((USB_AUDIO_HOST_SAMPLE_RATE_HZ % USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ) == 0U, "Host rate must be a multiple of the output rate");

/* -------------------------------------------------------------------------- */
/* Descriptors                                                                */
/* -------------------------------------------------------------------------- */

static const uint8_t USBAudio_DeviceDescriptor[] =
{
    18U, USB_AUDIO_DESCRIPTOR_DEVICE,
    0x00U, 0x02U,                                   /* bcdUSB 2.00 */
    0x00U, 0x00U, 0x00U,                            /* Class defined per interface */
    USB_DEVICE_EP0_MAX_PACKET_SIZE,
    USB_AUDIO_LOW_BYTE(USB_AUDIO_VENDOR_ID), USB_AUDIO_HIGH_BYTE(USB_AUDIO_VENDOR_ID),
    USB_AUDIO_LOW_BYTE(USB_AUDIO_PRODUCT_ID), USB_AUDIO_HIGH_BYTE(USB_AUDIO_PRODUCT_ID),
    USB_AUDIO_LOW_BYTE(USB_AUDIO_DEVICE_RELEASE), USB_AUDIO_HIGH_BYTE(USB_AUDIO_DEVICE_RELEASE),
    USB_AUDIO_STRING_MANUFACTURER,
    USB_AUDIO_STRING_PRODUCT,
    USB_AUDIO_STRING_SERIAL,
    1U                                              /* bNumConfigurations */
};

static const uint8_t USBAudio_ConfigurationDescriptor[] =
{
    /* Configuration */
    9U, USB_AUDIO_DESCRIPTOR_CONFIGURATION,
    USB_AUDIO_LOW_BYTE(USB_AUDIO_CONFIGURATION_LENGTH), USB_AUDIO_HIGH_BYTE(USB_AUDIO_CONFIGURATION_LENGTH),
    2U,                                             /* bNumInterfaces */
    USB_AUDIO_CONFIGURATION_VALUE,
    0U,                                             /* iConfiguration */
    0x80U,                                          /* Bus powered */
    50U,                                            /* 100 mA */

    /* Audio control interface */
    9U, USB_AUDIO_DESCRIPTOR_INTERFACE,
    USB_AUDIO_CONTROL_INTERFACE, 0U, 0U,
    0x01U, 0x01U, 0x00U,                            /* Audio, audio control */
    0U,

    /* Audio control header */
    9U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x01U,
    0x00U, 0x01U,                                   /* bcdADC 1.00 */
    USB_AUDIO_LOW_BYTE(USB_AUDIO_CONTROL_HEADER_LENGTH), USB_AUDIO_HIGH_BYTE(USB_AUDIO_CONTROL_HEADER_LENGTH),
    1U,                                             /* bInCollection */
    USB_AUDIO_STREAMING_INTERFACE,

    /* Input terminal 1: USB streaming */
    12U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x02U,
    USB_AUDIO_INPUT_TERMINAL_ID,
    0x01U, 0x01U,                                   /* USB streaming */
    0U,
    USB_AUDIO_HOST_CHANNEL_COUNT,
    0x03U, 0x00U,                                   /* Left front, right front */
    0U, 0U,

    /* Feature unit 3: master mute and volume */
    10U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x06U,
    USB_AUDIO_FEATURE_UNIT_ID,
    USB_AUDIO_INPUT_TERMINAL_ID,                    /* bSourceID */
    1U,                                             /* bControlSize */
    0x03U,                                          /* Master: mute, volume */
    0x00U,                                          /* Left: none */
    0x00U,                                          /* Right: none */
    0U,

    /* Output terminal 2: speaker */
    9U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x03U,
    USB_AUDIO_OUTPUT_TERMINAL_ID,
    0x01U, 0x03U,                                   /* Speaker */
    0U,
    USB_AUDIO_FEATURE_UNIT_ID,                      /* bSourceID */
    0U,

    /* Audio streaming interface, idle alternate setting */
    9U, USB_AUDIO_DESCRIPTOR_INTERFACE,
    USB_AUDIO_STREAMING_INTERFACE, USB_AUDIO_STREAMING_ALTERNATE_IDLE, 0U,
    0x01U, 0x02U, 0x00U,                            /* Audio, audio streaming */
    0U,

    /* Audio streaming interface, active alternate setting */
    9U, USB_AUDIO_DESCRIPTOR_INTERFACE,
    USB_AUDIO_STREAMING_INTERFACE, USB_AUDIO_STREAMING_ALTERNATE_ACTIVE, 1U,
    0x01U, 0x02U, 0x00U,
    0U,

    /* Audio streaming general */
    7U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x01U,
    1U,                                             /* bTerminalLink */
    1U,                                             /* bDelay */
    0x01U, 0x00U,                                   /* PCM */

    /* Type I format */
    11U, USB_AUDIO_DESCRIPTOR_CS_INTERFACE, 0x02U,
    0x01U,                                          /* Format type I */
    USB_AUDIO_HOST_CHANNEL_COUNT,
    2U,                                             /* bSubframeSize */
    16U,                                            /* bBitResolution */
    1U,                                             /* One discrete sample rate */
    (uint8_t)(USB_AUDIO_HOST_SAMPLE_RATE_HZ & 0xFFU),
    (uint8_t)((USB_AUDIO_HOST_SAMPLE_RATE_HZ >> 8U) & 0xFFU),
    (uint8_t)((USB_AUDIO_HOST_SAMPLE_RATE_HZ >> 16U) & 0xFFU),

    /* Isochronous OUT endpoint, adaptive */
    9U, USB_AUDIO_DESCRIPTOR_ENDPOINT,
    USB_AUDIO_ENDPOINT,
    0x09U,
    USB_AUDIO_LOW_BYTE(USB_AUDIO_MAX_PACKET_SIZE), USB_AUDIO_HIGH_BYTE(USB_AUDIO_MAX_PACKET_SIZE),
    1U,                                             /* bInterval: 1 ms */
    0U, 0U,                                         /* bRefresh, bSynchAddress */

    /* Audio data endpoint */
    7U, USB_AUDIO_DESCRIPTOR_CS_ENDPOINT, 0x01U,
    0x00U,                                          /* No endpoint controls */
    0U,
    0x00U, 0x00U
};

_Static_assert(sizeof(USBAudio_ConfigurationDescriptor) == USB_AUDIO_CONFIGURATION_LENGTH,
               "Configuration descriptor length does not match wTotalLength");

static const uint8_t USBAudio_LanguageDescriptor[] =
{
    4U, USB_AUDIO_DESCRIPTOR_STRING, 0x09U, 0x04U   /* English (United States) */
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool USBAudio_GetDescriptor(void *Context, uint8_t DescriptorType, uint8_t DescriptorIndex,
                                   uint16_t LanguageId, const uint8_t **Data, uint16_t *Length);
static bool USBAudio_SetConfiguration(void *Context, uint8_t ConfigurationValue);
static bool USBAudio_SetInterface(void *Context, uint8_t InterfaceNumber, uint8_t AlternateSetting);
static bool USBAudio_GetInterface(void *Context, uint8_t InterfaceNumber, uint8_t *AlternateSetting);
static bool USBAudio_HandleRequest(void *Context, const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data);
static bool USBAudio_HandleSamplingFrequencyRequest(const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data);
static bool USBAudio_HandleFeatureUnitRequest(const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data);
static bool USBAudio_HandleControlOut(void *Context, const USBDevice_SetupPacketTypeDef *Setup, const uint8_t *Data, uint16_t Length);
static void USBAudio_UpdateGain(void);
static void USBAudio_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length);
static uint16_t USBAudio_BuildString(const char *Text);
static void USBAudio_StartStreaming(void);
static void USBAudio_StopStreaming(void);
static void USBAudio_ResetBuffer(void);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const USBDevice_ConfigTypeDef USBAudio_Function =
{
    .GetDescriptor = USBAudio_GetDescriptor,
    .SetConfiguration = USBAudio_SetConfiguration,
    .SetInterface = USBAudio_SetInterface,
    .GetInterface = USBAudio_GetInterface,
    .HandleRequest = USBAudio_HandleRequest,
    .ControlOutComplete = USBAudio_HandleControlOut,
    .OutTransferComplete = USBAudio_HandleOutComplete,
    .InTransferComplete = NULL,
    .Context = NULL
};

static uint8_t USBAudio_StringBuffer[USB_AUDIO_STRING_BUFFER_SIZE];
static uint8_t USBAudio_ControlBuffer[USB_AUDIO_CONTROL_BUFFER_SIZE];
static uint8_t USBAudio_PacketBuffer[USB_AUDIO_MAX_PACKET_SIZE] __attribute__((aligned(4)));
static uint8_t USBAudio_StreamingAlternate;
static volatile bool USBAudio_Streaming;

/* Feature unit state, owned by the USB interrupt. */
static int16_t USBAudio_Volume = USB_AUDIO_VOLUME_MAX;
static bool USBAudio_Muted;
static int32_t USBAudio_GainQ15 = USB_AUDIO_UNITY_GAIN_Q15;

/* Rate-conversion accumulator, owned by the USB interrupt. */
static int32_t USBAudio_FrameSum;
static uint32_t USBAudio_FrameSumCount;

static Audio_SampleTypeDef USBAudio_Buffer[USB_AUDIO_BUFFER_SAMPLES];
static volatile uint32_t USBAudio_WriteIndex;
static volatile uint32_t USBAudio_ReadIndex;
static volatile bool USBAudio_Playing;

/* -------------------------------------------------------------------------- */
/* USB function callbacks                                                     */
/* -------------------------------------------------------------------------- */

static bool USBAudio_GetDescriptor(void *Context, uint8_t DescriptorType, uint8_t DescriptorIndex,
                                   uint16_t LanguageId, const uint8_t **Data, uint16_t *Length)
{
    (void)Context;
    (void)LanguageId;

    switch(DescriptorType)
    {
        case USB_AUDIO_DESCRIPTOR_DEVICE:
            *Data = USBAudio_DeviceDescriptor;
            *Length = sizeof(USBAudio_DeviceDescriptor);
            return true;

        case USB_AUDIO_DESCRIPTOR_CONFIGURATION:
            *Data = USBAudio_ConfigurationDescriptor;
            *Length = sizeof(USBAudio_ConfigurationDescriptor);
            return true;

        case USB_AUDIO_DESCRIPTOR_STRING:
            switch(DescriptorIndex)
            {
                case USB_AUDIO_STRING_LANGUAGE:
                    *Data = USBAudio_LanguageDescriptor;
                    *Length = sizeof(USBAudio_LanguageDescriptor);
                    return true;

                case USB_AUDIO_STRING_MANUFACTURER:
                    *Length = USBAudio_BuildString("DualSlide");
                    break;

                case USB_AUDIO_STRING_PRODUCT:
                    *Length = USBAudio_BuildString("DualSlide USB Audio Test");
                    break;

                case USB_AUDIO_STRING_SERIAL:
                    *Length = USBAudio_BuildString("0001");
                    break;

                default:
                    return false;
            }

            *Data = USBAudio_StringBuffer;
            return true;

        default:
            return false;
    }
}

static bool USBAudio_SetConfiguration(void *Context, uint8_t ConfigurationValue)
{
    (void)Context;

    USBAudio_StopStreaming();

    return (ConfigurationValue == 0U) || (ConfigurationValue == USB_AUDIO_CONFIGURATION_VALUE);
}

static bool USBAudio_SetInterface(void *Context, uint8_t InterfaceNumber, uint8_t AlternateSetting)
{
    (void)Context;

    if(InterfaceNumber == USB_AUDIO_CONTROL_INTERFACE)
    {
        return AlternateSetting == 0U;
    }

    if(InterfaceNumber != USB_AUDIO_STREAMING_INTERFACE)
    {
        return false;
    }

    if(AlternateSetting == USB_AUDIO_STREAMING_ALTERNATE_IDLE)
    {
        USBAudio_StopStreaming();
        return true;
    }

    if(AlternateSetting == USB_AUDIO_STREAMING_ALTERNATE_ACTIVE)
    {
        USBAudio_StopStreaming();
        USBAudio_StartStreaming();
        return USBAudio_Streaming;
    }

    return false;
}

static bool USBAudio_GetInterface(void *Context, uint8_t InterfaceNumber, uint8_t *AlternateSetting)
{
    (void)Context;

    if(InterfaceNumber == USB_AUDIO_CONTROL_INTERFACE)
    {
        *AlternateSetting = 0U;
        return true;
    }

    if(InterfaceNumber == USB_AUDIO_STREAMING_INTERFACE)
    {
        *AlternateSetting = USBAudio_StreamingAlternate;
        return true;
    }

    return false;
}

static bool USBAudio_HandleRequest(void *Context, const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data)
{
    uint8_t Recipient = Setup->RequestType & USB_AUDIO_REQUEST_RECIPIENT_MASK;

    (void)Context;

    if(Recipient == USB_AUDIO_REQUEST_RECIPIENT_ENDPOINT)
    {
        return USBAudio_HandleSamplingFrequencyRequest(Setup, Data);
    }

    if(Recipient == USB_AUDIO_REQUEST_RECIPIENT_INTERFACE)
    {
        return USBAudio_HandleFeatureUnitRequest(Setup, Data);
    }

    return false;
}

/*
 * The endpoint advertises no controls, but some hosts still set or query the
 * sampling frequency. Only the advertised rate is accepted.
 */
static bool USBAudio_HandleSamplingFrequencyRequest(const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data)
{
    if(((uint8_t)Setup->Index != USB_AUDIO_ENDPOINT) ||
       ((Setup->Value >> 8U) != USB_AUDIO_SAMPLING_FREQUENCY_CONTROL) ||
       (Setup->Length != USB_AUDIO_SAMPLING_FREQUENCY_LENGTH))
    {
        return false;
    }

    if(Setup->Request == USB_AUDIO_REQUEST_SET_CUR)
    {
        Data->OutData = USBAudio_ControlBuffer;
        return true;
    }

    if(Setup->Request == USB_AUDIO_REQUEST_GET_CUR)
    {
        USBAudio_ControlBuffer[0] = (uint8_t)(USB_AUDIO_HOST_SAMPLE_RATE_HZ & 0xFFU);
        USBAudio_ControlBuffer[1] = (uint8_t)((USB_AUDIO_HOST_SAMPLE_RATE_HZ >> 8U) & 0xFFU);
        USBAudio_ControlBuffer[2] = (uint8_t)((USB_AUDIO_HOST_SAMPLE_RATE_HZ >> 16U) & 0xFFU);
        Data->InData = USBAudio_ControlBuffer;
        Data->InLength = USB_AUDIO_SAMPLING_FREQUENCY_LENGTH;
        return true;
    }

    return false;
}

/*
 * Master mute and volume of the feature unit. wIndex carries the unit ID and
 * the audio control interface; wValue carries the control and the channel.
 */
static bool USBAudio_HandleFeatureUnitRequest(const USBDevice_SetupPacketTypeDef *Setup, USBDevice_ControlDataTypeDef *Data)
{
    uint8_t Control = (uint8_t)(Setup->Value >> 8U);
    int16_t Value;

    if(((Setup->Index >> 8U) != USB_AUDIO_FEATURE_UNIT_ID) ||
       ((uint8_t)Setup->Index != USB_AUDIO_CONTROL_INTERFACE) ||
       ((uint8_t)Setup->Value != 0U))
    {
        return false;
    }

    if(Control == USB_AUDIO_MUTE_CONTROL)
    {
        if(Setup->Length != USB_AUDIO_MUTE_LENGTH)
        {
            return false;
        }

        if(Setup->Request == USB_AUDIO_REQUEST_SET_CUR)
        {
            Data->OutData = USBAudio_ControlBuffer;
            return true;
        }

        if(Setup->Request == USB_AUDIO_REQUEST_GET_CUR)
        {
            USBAudio_ControlBuffer[0] = USBAudio_Muted ? 1U : 0U;
            Data->InData = USBAudio_ControlBuffer;
            Data->InLength = USB_AUDIO_MUTE_LENGTH;
            return true;
        }

        return false;
    }

    if((Control != USB_AUDIO_VOLUME_CONTROL) || (Setup->Length != USB_AUDIO_VOLUME_LENGTH))
    {
        return false;
    }

    switch(Setup->Request)
    {
        case USB_AUDIO_REQUEST_SET_CUR:
            Data->OutData = USBAudio_ControlBuffer;
            return true;

        case USB_AUDIO_REQUEST_GET_CUR:
            Value = USBAudio_Volume;
            break;

        case USB_AUDIO_REQUEST_GET_MIN:
            Value = USB_AUDIO_VOLUME_MIN;
            break;

        case USB_AUDIO_REQUEST_GET_MAX:
            Value = USB_AUDIO_VOLUME_MAX;
            break;

        case USB_AUDIO_REQUEST_GET_RES:
            Value = USB_AUDIO_VOLUME_RESOLUTION;
            break;

        default:
            return false;
    }

    USBAudio_ControlBuffer[0] = USB_AUDIO_LOW_BYTE((uint16_t)Value);
    USBAudio_ControlBuffer[1] = USB_AUDIO_HIGH_BYTE((uint16_t)Value);
    Data->InData = USBAudio_ControlBuffer;
    Data->InLength = USB_AUDIO_VOLUME_LENGTH;
    return true;
}

static bool USBAudio_HandleControlOut(void *Context, const USBDevice_SetupPacketTypeDef *Setup, const uint8_t *Data, uint16_t Length)
{
    uint8_t Control = (uint8_t)(Setup->Value >> 8U);
    uint32_t SampleRate;
    int32_t Volume;

    (void)Context;

    if((Setup->RequestType & USB_AUDIO_REQUEST_RECIPIENT_MASK) == USB_AUDIO_REQUEST_RECIPIENT_ENDPOINT)
    {
        if(Length != USB_AUDIO_SAMPLING_FREQUENCY_LENGTH)
        {
            return false;
        }

        SampleRate = (uint32_t)Data[0] | ((uint32_t)Data[1] << 8U) | ((uint32_t)Data[2] << 16U);

        return SampleRate == USB_AUDIO_HOST_SAMPLE_RATE_HZ;
    }

    if((Control == USB_AUDIO_MUTE_CONTROL) && (Length == USB_AUDIO_MUTE_LENGTH))
    {
        USBAudio_Muted = (Data[0] != 0U);
        USBAudio_UpdateGain();
        return true;
    }

    if((Control == USB_AUDIO_VOLUME_CONTROL) && (Length == USB_AUDIO_VOLUME_LENGTH))
    {
        Volume = (int16_t)((uint16_t)Data[0] | ((uint16_t)Data[1] << 8U));

        if(Volume < USB_AUDIO_VOLUME_MIN)
        {
            Volume = USB_AUDIO_VOLUME_MIN;
        }
        else if(Volume > USB_AUDIO_VOLUME_MAX)
        {
            Volume = USB_AUDIO_VOLUME_MAX;
        }

        USBAudio_Volume = (int16_t)Volume;
        USBAudio_UpdateGain();
        return true;
    }

    return false;
}

/*
 * Converts one packet of 48 kHz stereo frames to 24 kHz mono samples. A frame
 * pair split across two packets is completed by the next packet.
 */
static void USBAudio_HandleOutComplete(void *Context, uint8_t EndpointAddress, uint8_t *Data, uint16_t Length)
{
    uint32_t WriteIndex;
    uint32_t ReadIndex;
    uint32_t FrameCount;
    uint32_t Frame;

    (void)Context;

    if((EndpointAddress != USB_AUDIO_ENDPOINT) || !USBAudio_Streaming)
    {
        return;
    }

    WriteIndex = USBAudio_WriteIndex;
    ReadIndex = USBAudio_ReadIndex;
    FrameCount = Length / USB_AUDIO_BYTES_PER_FRAME;

    for(Frame = 0U; Frame < FrameCount; Frame++)
    {
        const uint8_t *Source = &Data[Frame * USB_AUDIO_BYTES_PER_FRAME];
        int16_t Left = (int16_t)((uint16_t)Source[0] | ((uint16_t)Source[1] << 8U));
        int16_t Right = (int16_t)((uint16_t)Source[2] | ((uint16_t)Source[3] << 8U));

        USBAudio_FrameSum += (int32_t)Left + (int32_t)Right;
        USBAudio_FrameSumCount++;

        if(USBAudio_FrameSumCount < USB_AUDIO_FRAMES_PER_SAMPLE)
        {
            continue;
        }

        /* Drop the sample on overflow; rate matching normally prevents it. */
        if((WriteIndex - ReadIndex) < USB_AUDIO_BUFFER_SAMPLES)
        {
            int32_t Sample = USBAudio_FrameSum / (int32_t)(USB_AUDIO_FRAMES_PER_SAMPLE * USB_AUDIO_HOST_CHANNEL_COUNT);

            USBAudio_Buffer[WriteIndex & USB_AUDIO_BUFFER_MASK] = (Audio_SampleTypeDef)((Sample * USBAudio_GainQ15) >> 15);
            WriteIndex++;
        }

        USBAudio_FrameSum = 0;
        USBAudio_FrameSumCount = 0U;
    }

    USB_AUDIO_COMPILER_BARRIER();
    USBAudio_WriteIndex = WriteIndex;

    (void)USBDevice_Receive(USB_AUDIO_ENDPOINT, USBAudio_PacketBuffer, sizeof(USBAudio_PacketBuffer));
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* Converts the host's volume (1/256 dB) and mute into a linear Q15 gain. */
static void USBAudio_UpdateGain(void)
{
    float Decibels = (float)USBAudio_Volume / 256.0f;

    USBAudio_GainQ15 = USBAudio_Muted ? 0 : (int32_t)(powf(10.0f, Decibels / 20.0f) * (float)USB_AUDIO_UNITY_GAIN_Q15);
}

static uint16_t USBAudio_BuildString(const char *Text)
{
    uint16_t Length = 2U;

    while((*Text != '\0') && ((Length + 2U) <= sizeof(USBAudio_StringBuffer)))
    {
        USBAudio_StringBuffer[Length] = (uint8_t)*Text;
        USBAudio_StringBuffer[Length + 1U] = 0U;
        Length = (uint16_t)(Length + 2U);
        Text++;
    }

    USBAudio_StringBuffer[0] = (uint8_t)Length;
    USBAudio_StringBuffer[1] = USB_AUDIO_DESCRIPTOR_STRING;

    return Length;
}

static void USBAudio_StartStreaming(void)
{
    USBAudio_ResetBuffer();

    if(!USBDevice_OpenEndpoint(USB_AUDIO_ENDPOINT, USB_DEVICE_ENDPOINT_ISOCHRONOUS, USB_AUDIO_MAX_PACKET_SIZE))
    {
        return;
    }

    USBAudio_StreamingAlternate = USB_AUDIO_STREAMING_ALTERNATE_ACTIVE;
    USBAudio_Streaming = true;

    if(!USBDevice_Receive(USB_AUDIO_ENDPOINT, USBAudio_PacketBuffer, sizeof(USBAudio_PacketBuffer)))
    {
        USBAudio_StopStreaming();
    }
}

static void USBAudio_StopStreaming(void)
{
    if(USBAudio_Streaming)
    {
        USBAudio_Streaming = false;
        USBDevice_CloseEndpoint(USB_AUDIO_ENDPOINT);
    }

    USBAudio_StreamingAlternate = USB_AUDIO_STREAMING_ALTERNATE_IDLE;
    USBAudio_ResetBuffer();
}

/*
 * Called from the USB interrupt. The consumer runs at a higher interrupt
 * priority and never advances the read index while USBAudio_Playing is false.
 */
static void USBAudio_ResetBuffer(void)
{
    USBAudio_Playing = false;
    USBAudio_FrameSum = 0;
    USBAudio_FrameSumCount = 0U;
    USB_AUDIO_COMPILER_BARRIER();
    USBAudio_ReadIndex = USBAudio_WriteIndex;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool USBAudio_Init(void)
{
    USBAudio_Streaming = false;
    USBAudio_StreamingAlternate = USB_AUDIO_STREAMING_ALTERNATE_IDLE;
    USBAudio_ResetBuffer();

    return USBDevice_Init(&USBAudio_Function);
}

void USBAudio_Deinit(void)
{
    USBDevice_Deinit();
    USBAudio_StopStreaming();
}

bool USBAudio_IsStreaming(void)
{
    return USBAudio_Streaming;
}

void USBAudio_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context)
{
    uint32_t WriteIndex = USBAudio_WriteIndex;
    uint32_t ReadIndex = USBAudio_ReadIndex;
    uint32_t Level = WriteIndex - ReadIndex;
    uint32_t Frame = 0U;
    bool RepeatSample = false;

    (void)Context;

    if(!USBAudio_Playing && (Level >= USB_AUDIO_BUFFER_START_LEVEL))
    {
        USBAudio_Playing = true;
    }

    if(USBAudio_Playing)
    {
        /* Rate matching: drop a sample when filling, repeat one when draining. */
        if(Level > USB_AUDIO_BUFFER_HIGH_LEVEL)
        {
            ReadIndex++;
            Level--;
        }
        else if(Level < USB_AUDIO_BUFFER_LOW_LEVEL)
        {
            RepeatSample = true;
        }

        for(Frame = 0U; (Frame < FrameCount) && (Level != 0U); Frame++)
        {
            Samples[Frame] = USBAudio_Buffer[ReadIndex & USB_AUDIO_BUFFER_MASK];

            if(RepeatSample)
            {
                RepeatSample = false;
            }
            else
            {
                ReadIndex++;
                Level--;
            }
        }

        USBAudio_ReadIndex = ReadIndex;

        if(Frame < FrameCount)
        {
            /* Underrun: refill to the start level before playing again. */
            USBAudio_Playing = false;
        }
    }

    for(; Frame < FrameCount; Frame++)
    {
        Samples[Frame] = 0;
    }
}
