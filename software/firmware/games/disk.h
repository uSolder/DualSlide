/**
 * @file disk.h
 * @brief The game disk: a block device in the internal Flash of Bank 2.
 *
 * The game disk is a block device of DISK_BLOCK_SIZE-byte blocks that keeps
 * its contents when the device is off. It holds a FAT filesystem, which the
 * device shares with a USB host as a mass-storage drive.
 *
 * Writes may be held in a cache. They reach permanent storage on
 * Disk_Flush(), or from Disk_Process() once writing has paused. Every
 * function must be called from the main loop, never from an interrupt.
 */

#ifndef DISK_H
#define DISK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one disk block, in bytes. */
#define DISK_BLOCK_SIZE                     (512U)

/**
 * @brief Prepare the disk for use.
 *
 * A disk that has never been used reads as zeros.
 *
 * @return true on success; otherwise false.
 */
bool Disk_Init(void);

/** @brief Return the number of DISK_BLOCK_SIZE blocks on the disk. */
uint32_t Disk_GetBlockCount(void);

/**
 * @brief Read blocks.
 *
 * @param Block First block to read.
 * @param Data  Receives Count * DISK_BLOCK_SIZE bytes.
 * @param Count Number of blocks.
 *
 * @return true on success; otherwise false.
 */
bool Disk_Read(uint32_t Block, void *Data, uint32_t Count);

/**
 * @brief Write blocks.
 *
 * @param Block First block to write.
 * @param Data  Count * DISK_BLOCK_SIZE bytes to write.
 * @param Count Number of blocks.
 *
 * @return true on success; otherwise false.
 */
bool Disk_Write(uint32_t Block, const void *Data, uint32_t Count);

/**
 * @brief Write any cached blocks to permanent storage.
 *
 * @return true on success; otherwise false.
 */
bool Disk_Flush(void);

/**
 * @brief Flush cached blocks once writing has paused. Call every frame.
 */
void Disk_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* DISK_H */
