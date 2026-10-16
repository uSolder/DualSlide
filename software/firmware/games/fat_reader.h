/**
 * @file fat_reader.h
 * @brief Reads files from the FAT12 or FAT16 filesystem on the game disk.
 *
 * Read-only, and limited to what the firmware needs: the files in the root
 * directory. Only the host writes to the disk, through the USB drive.
 *
 * Every function must be called from the main loop (disk.h).
 */

#ifndef FAT_READER_H
#define FAT_READER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of a short (8.3) name as text, such as "PONG.DSG", with its terminator. */
#define FAT_READER_NAME_SIZE                (13U)

/**
 * @brief A file in the root directory.
 */
typedef struct
{
    char Name[FAT_READER_NAME_SIZE];        /* Short name, such as "PONG.DSG" */
    uint8_t Attributes;                     /* FAT_READER_ATTRIBUTE_* */
    uint32_t FirstCluster;
    uint32_t Size;                          /* Bytes */
} FAT_FileTypeDef;

#define FAT_READER_ATTRIBUTE_READ_ONLY      (0x01U)
#define FAT_READER_ATTRIBUTE_HIDDEN         (0x02U)
#define FAT_READER_ATTRIBUTE_SYSTEM         (0x04U)

/**
 * @brief Read the filesystem's layout from the disk.
 *
 * Call again whenever the host may have changed the disk.
 *
 * @return true when the disk holds a FAT12 or FAT16 filesystem.
 */
bool FAT_Mount(void);

/**
 * @brief Find the next file in the root directory.
 *
 * Directories, the volume label and deleted entries are skipped.
 *
 * @param Position Where to look from; start at 0. Advanced past the file found.
 * @param File     Receives the file.
 *
 * @return true when a file was found; false at the end of the directory.
 */
bool FAT_NextFile(uint32_t *Position, FAT_FileTypeDef *File);

/**
 * @brief Read bytes from a file.
 *
 * Reading onwards from where the last read of the same file ended is fast;
 * reading anywhere else follows the file's clusters from its start.
 *
 * @param File   From FAT_NextFile().
 * @param Offset Byte to start at.
 * @param Data   Receives Size bytes.
 * @param Size   Number of bytes; Offset + Size must not pass the end of the file.
 *
 * @return true on success.
 */
bool FAT_Read(const FAT_FileTypeDef *File, uint32_t Offset, void *Data, uint32_t Size);

#ifdef __cplusplus
}
#endif

#endif /* FAT_READER_H */
