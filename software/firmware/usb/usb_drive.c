/**
 * @file usb_drive.c
 * @brief Prepares the game disk and shares it with a USB host as a drive.
 */

#include "usb_drive.h"

#include "disk.h"
#include "fat_format.h"
#include "system_time.h"
#include "usb_msc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(USB_MSC_BLOCK_SIZE == DISK_BLOCK_SIZE, "USB and disk block sizes must match");

#define USB_DRIVE_VOLUME_LABEL              "DUALSLIDE"

/* The host counts as finished once it has written nothing for this long. */
#define USB_DRIVE_QUIET_MILLISECONDS        (1000U)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static bool USBDrive_Shared;
static bool USBDrive_Changed;
static uint32_t USBDrive_LastWriteMilliseconds;

/* -------------------------------------------------------------------------- */
/* Medium                                                                     */
/* -------------------------------------------------------------------------- */

static bool USBDrive_Read(void *Context, uint32_t Block, uint8_t *Data, uint32_t Count)
{
    (void)Context;

    return Disk_Read(Block, Data, Count);
}

static bool USBDrive_Write(void *Context, uint32_t Block, const uint8_t *Data, uint32_t Count)
{
    (void)Context;

    USBDrive_Changed = true;
    USBDrive_LastWriteMilliseconds = SystemTime_GetMilliseconds();

    return Disk_Write(Block, Data, Count);
}

static bool USBDrive_Flush(void *Context)
{
    (void)Context;

    return Disk_Flush();
}

static USBMSC_MediaTypeDef USBDrive_Media =
{
    .BlockCount = 0U,
    .Read = USBDrive_Read,
    .Write = USBDrive_Write,
    .Flush = USBDrive_Flush,
    .Context = NULL
};

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool USBDrive_Init(void)
{
    USBDrive_Shared = false;
    USBDrive_Changed = false;

    if(!Disk_Init())
    {
        return false;
    }

    if(!FATFormat_IsFormatted() && !FATFormat_Format(USB_DRIVE_VOLUME_LABEL))
    {
        return false;
    }

    USBDrive_Media.BlockCount = Disk_GetBlockCount();
    USBDrive_Shared = USBMSC_Init(&USBDrive_Media);

    return USBDrive_Shared;
}

void USBDrive_Process(void)
{
    if(USBDrive_Shared)
    {
        USBMSC_Process();
    }

    Disk_Process();
}

bool USBDrive_TakeChanges(void)
{
    if(!USBDrive_Changed)
    {
        return false;
    }

    if(!USBMSC_IsEjected() &&
       ((uint32_t)(SystemTime_GetMilliseconds() - USBDrive_LastWriteMilliseconds) < USB_DRIVE_QUIET_MILLISECONDS))
    {
        return false;
    }

    USBDrive_Changed = false;

    /* Read the disk as the host left it, including writes still in the cache. */
    return true;
}

void USBDrive_Deinit(void)
{
    if(USBDrive_Shared)
    {
        USBMSC_Deinit();
        USBDrive_Shared = false;
    }

    (void)Disk_Flush();
}
