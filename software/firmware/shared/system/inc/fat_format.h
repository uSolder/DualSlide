/**
 * @file fat_format.h
 * @brief Formats the game disk (disk.h) with an empty FAT12 filesystem.
 */

#ifndef FAT_FORMAT_H
#define FAT_FORMAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine whether the disk starts with a FAT boot sector.
 *
 * @return true when the disk holds a FAT filesystem; otherwise false.
 */
bool FATFormat_IsFormatted(void);

/**
 * @brief Write an empty FAT12 filesystem over the whole disk.
 *
 * @param VolumeLabel Volume label of at most 11 characters, in capitals.
 *
 * @return true on success; otherwise false.
 */
bool FATFormat_Format(const char *VolumeLabel);

#ifdef __cplusplus
}
#endif

#endif /* FAT_FORMAT_H */
