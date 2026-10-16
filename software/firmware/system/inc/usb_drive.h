/**
 * @file usb_drive.h
 * @brief Prepares the game disk (disk.h) and shares it with a USB host as a drive.
 *
 * A disk without a filesystem is formatted as FAT12, labelled DUALSLIDE,
 * before it is shared.
 */

#ifndef USB_DRIVE_H
#define USB_DRIVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prepare the game disk and, if asked, attach to the bus as a USB drive.
 *
 * @param ShareOverUSB false prepares the disk only, leaving USB to another
 *                     function (such as the USB audio test).
 *
 * @return true on success.
 */
bool USBDrive_Init(bool ShareOverUSB);

/**
 * @brief Serve the host and write cached data to the disk. Call every frame.
 */
void USBDrive_Process(void);

/**
 * @brief Report, once, that the host has finished changing the disk.
 *
 * The host counts as finished when it has written nothing for a second, or
 * has ejected the drive.
 *
 * @return true when the disk changed since the last true; the caller should
 *         read it afresh.
 */
bool USBDrive_TakeChanges(void);

/**
 * @brief Detach from the bus and make everything written permanent.
 */
void USBDrive_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DRIVE_H */
