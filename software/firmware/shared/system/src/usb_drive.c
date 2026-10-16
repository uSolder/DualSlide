/**
 * @file usb_drive.c
 * @brief Shares the game disk with a USB host as a mass-storage drive.
 */

#include "usb_drive.h"

#include "disk.h"
#include "fat_format.h"
#include "usb_msc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(USB_MSC_BLOCK_SIZE == DISK_BLOCK_SIZE, "USB and disk block sizes must match");

#define USB_DRIVE_VOLUME_LABEL              "DUALSLIDE"

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
    if(!Disk_Init())
    {
        return false;
    }

    if(!FATFormat_IsFormatted() && !FATFormat_Format(USB_DRIVE_VOLUME_LABEL))
    {
        return false;
    }

    USBDrive_Media.BlockCount = Disk_GetBlockCount();

    return USBMSC_Init(&USBDrive_Media);
}

void USBDrive_Process(void)
{
    USBMSC_Process();
    Disk_Process();
}

void USBDrive_Deinit(void)
{
    USBMSC_Deinit();
    (void)Disk_Flush();
}
