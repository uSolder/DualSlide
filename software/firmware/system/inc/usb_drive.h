/**
 * @file usb_drive.h
 * @brief Shares the game disk (disk.h) with a USB host as a mass-storage drive.
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
 * @brief Prepare the game disk and attach to the bus as a USB drive.
 *
 * @return true on success; otherwise false.
 */
bool USBDrive_Init(void);

/**
 * @brief Serve the host and write cached data to the disk. Call every frame.
 */
void USBDrive_Process(void);

/**
 * @brief Detach from the bus and make everything written permanent.
 */
void USBDrive_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DRIVE_H */
