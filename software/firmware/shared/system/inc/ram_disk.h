/**
 * @file ram_disk.h
 * @brief FAT12-formatted RAM disk for testing USB mass storage.
 *
 * The disk is formatted with the volume label DUALSLIDE and one file,
 * README.TXT. Its contents are lost when the device powers off.
 */

#ifndef RAM_DISK_H
#define RAM_DISK_H

#include "usb_msc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the disk in USB_MSC_BLOCK_SIZE blocks (128 KB). */
#define RAM_DISK_BLOCK_COUNT                (256U)

/** The disk, as a medium for USBMSC_Init(). */
extern const USBMSC_MediaTypeDef RamDisk_Media;

/**
 * @brief Erase the disk and write an empty FAT12 filesystem holding README.TXT.
 */
void RamDisk_Format(void);

#ifdef __cplusplus
}
#endif

#endif /* RAM_DISK_H */
