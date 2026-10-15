/**
 * @file usb_msc.h
 * @brief USB Mass Storage Class (Bulk-Only Transport, SCSI) device service.
 *
 * The device enumerates as one removable disk. Its blocks come from a
 * medium described by USBMSC_MediaTypeDef; the class driver itself knows
 * nothing about filesystems.
 *
 * The host may eject the disk (START STOP UNIT). The medium then reports
 * itself absent until the host configures the device again.
 */

#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one block of the medium, in bytes. */
#define USB_MSC_BLOCK_SIZE                  (512U)

/**
 * @brief Block medium presented to the host.
 *
 * Read and Write run in USB interrupt context and must not block for long.
 * A NULL Write makes the medium write-protected.
 */
typedef struct
{
    /** Number of USB_MSC_BLOCK_SIZE blocks on the medium. */
    uint32_t BlockCount;

    /** Read Count blocks starting at Block into Data. Return false on failure. */
    bool (*Read)(void *Context, uint32_t Block, uint8_t *Data, uint32_t Count);

    /** Write Count blocks starting at Block from Data. Return false on failure. */
    bool (*Write)(void *Context, uint32_t Block, const uint8_t *Data, uint32_t Count);

    /** Value passed to Read and Write. */
    void *Context;
} USBMSC_MediaTypeDef;

/**
 * @brief Register the mass-storage function and attach to the bus.
 *
 * @param Media Medium to present. Must remain valid until USBMSC_Deinit().
 *
 * @return true on success; otherwise false.
 */
bool USBMSC_Init(const USBMSC_MediaTypeDef *Media);

/**
 * @brief Detach from the bus and stop presenting the medium.
 */
void USBMSC_Deinit(void);

/**
 * @brief Determine whether the host has ejected the disk.
 *
 * @return true after the host ejects the disk, until it configures the
 *         device again; otherwise false.
 */
bool USBMSC_IsEjected(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_MSC_H */
