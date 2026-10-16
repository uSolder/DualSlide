/**
 * @file fat_format.c
 * @brief Formats the game disk with an empty FAT12 filesystem.
 *
 * Layout, in DISK_BLOCK_SIZE sectors with one sector per cluster:
 *
 *   0                    boot sector and BIOS parameter block
 *   1 ...                two FATs, each as long as the cluster count needs
 *   ...                  root directory (64 entries)
 *   ... to the end       data area
 *
 * Only these structures are written; the data area keeps whatever it held,
 * which the empty FAT marks as free.
 */

#include "fat_format.h"

#include "disk.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Filesystem layout                                                          */
/* -------------------------------------------------------------------------- */

#define FAT_FORMAT_RESERVED_SECTORS         (1U)
#define FAT_FORMAT_FAT_COUNT                (2U)
#define FAT_FORMAT_ROOT_ENTRIES             (64U)
#define FAT_FORMAT_DIRECTORY_ENTRY_SIZE     (32U)
#define FAT_FORMAT_ROOT_SECTORS             ((FAT_FORMAT_ROOT_ENTRIES * FAT_FORMAT_DIRECTORY_ENTRY_SIZE) / DISK_BLOCK_SIZE)
#define FAT_FORMAT_MEDIA_DESCRIPTOR         (0xF8U)
#define FAT_FORMAT_MAX_FAT12_CLUSTERS       (4084U)
#define FAT_FORMAT_LABEL_LENGTH             (11U)
#define FAT_FORMAT_ATTRIBUTE_VOLUME_ID      (0x08U)

/* The extended boot record, which holds the volume serial number. */
#define FAT_FORMAT_EXTENDED_SIGNATURE_OFFSET (38U)
#define FAT_FORMAT_EXTENDED_SIGNATURE       (0x29U)
#define FAT_FORMAT_SERIAL_NUMBER_OFFSET     (39U)

/* 2026-10-15 12:00:00 in FAT date and time format. */
#define FAT_FORMAT_DATE                     ((uint16_t)(((2026U - 1980U) << 9U) | (10U << 5U) | 15U))
#define FAT_FORMAT_TIME                     ((uint16_t)(12U << 11U))

_Static_assert(((FAT_FORMAT_ROOT_ENTRIES * FAT_FORMAT_DIRECTORY_ENTRY_SIZE) % DISK_BLOCK_SIZE) == 0U,
               "Root directory must fill whole sectors");

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void FATFormat_WriteLittleEndian16(uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8U);
}

static void FATFormat_WriteLittleEndian32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8U);
    Data[2] = (uint8_t)(Value >> 16U);
    Data[3] = (uint8_t)(Value >> 24U);
}

/* Copies Text into an 11-character field, padded with spaces. */
static void FATFormat_CopyLabel(uint8_t *Field, const char *Text)
{
    size_t Index;

    for(Index = 0U; Index < FAT_FORMAT_LABEL_LENGTH; Index++)
    {
        Field[Index] = (*Text != '\0') ? (uint8_t)*Text++ : (uint8_t)' ';
    }
}

/*
 * Picks the smallest FAT that can index every cluster left after the FATs,
 * root directory and boot sector. Returns false if the disk needs more
 * clusters than FAT12 allows.
 */
static bool FATFormat_GetSectorsPerFat(uint32_t SectorCount, uint32_t *SectorsPerFat)
{
    uint32_t Sectors;

    for(Sectors = 1U; Sectors <= 12U; Sectors++)
    {
        uint32_t Overhead = FAT_FORMAT_RESERVED_SECTORS + (FAT_FORMAT_FAT_COUNT * Sectors) + FAT_FORMAT_ROOT_SECTORS;
        uint32_t Clusters;

        if(SectorCount <= Overhead)
        {
            return false;
        }

        Clusters = SectorCount - Overhead;

        /* Twelve-bit entries for every cluster, plus the two reserved entries. */
        if(((((Clusters + 2U) * 3U) + 1U) / 2U) <= (Sectors * DISK_BLOCK_SIZE))
        {
            *SectorsPerFat = Sectors;
            return Clusters <= FAT_FORMAT_MAX_FAT12_CLUSTERS;
        }
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool FATFormat_IsFormatted(void)
{
    uint8_t Sector[DISK_BLOCK_SIZE];

    return Disk_Read(0U, Sector, 1U) && (Sector[510] == 0x55U) && (Sector[511] == 0xAAU) &&
           (Sector[11] == (uint8_t)DISK_BLOCK_SIZE) && (Sector[12] == (uint8_t)(DISK_BLOCK_SIZE >> 8U));
}

bool FATFormat_Format(const char *VolumeLabel, uint32_t SerialNumber)
{
    uint8_t Sector[DISK_BLOCK_SIZE];
    uint32_t SectorCount = Disk_GetBlockCount();
    uint32_t SectorsPerFat;
    uint32_t MetadataSectors;
    uint32_t Index;

    if((SectorCount > UINT16_MAX) || !FATFormat_GetSectorsPerFat(SectorCount, &SectorsPerFat))
    {
        return false;
    }

    MetadataSectors = FAT_FORMAT_RESERVED_SECTORS + (FAT_FORMAT_FAT_COUNT * SectorsPerFat) + FAT_FORMAT_ROOT_SECTORS;

    /* Boot sector and BIOS parameter block. */
    memset(Sector, 0, sizeof(Sector));
    Sector[0] = 0xEBU;                                              /* Jump over the BPB */
    Sector[1] = 0x3CU;
    Sector[2] = 0x90U;
    memcpy(&Sector[3], "MSDOS5.0", 8U);
    FATFormat_WriteLittleEndian16(&Sector[11], DISK_BLOCK_SIZE);    /* Bytes per sector */
    Sector[13] = 1U;                                                /* Sectors per cluster */
    FATFormat_WriteLittleEndian16(&Sector[14], FAT_FORMAT_RESERVED_SECTORS);
    Sector[16] = FAT_FORMAT_FAT_COUNT;
    FATFormat_WriteLittleEndian16(&Sector[17], FAT_FORMAT_ROOT_ENTRIES);
    FATFormat_WriteLittleEndian16(&Sector[19], (uint16_t)SectorCount);
    Sector[21] = FAT_FORMAT_MEDIA_DESCRIPTOR;
    FATFormat_WriteLittleEndian16(&Sector[22], (uint16_t)SectorsPerFat);
    FATFormat_WriteLittleEndian16(&Sector[24], 1U);                 /* Sectors per track */
    FATFormat_WriteLittleEndian16(&Sector[26], 1U);                 /* Heads */
    Sector[36] = 0x80U;                                             /* Drive number */
    Sector[FAT_FORMAT_EXTENDED_SIGNATURE_OFFSET] = FAT_FORMAT_EXTENDED_SIGNATURE;
    FATFormat_WriteLittleEndian32(&Sector[FAT_FORMAT_SERIAL_NUMBER_OFFSET], SerialNumber);
    FATFormat_CopyLabel(&Sector[43], VolumeLabel);
    memcpy(&Sector[54], "FAT12   ", 8U);
    Sector[510] = 0x55U;
    Sector[511] = 0xAAU;

    if(!Disk_Write(0U, Sector, 1U))
    {
        return false;
    }

    /* Each FAT starts with the media descriptor and a reserved entry; all clusters are free. */
    for(Index = FAT_FORMAT_RESERVED_SECTORS; Index < MetadataSectors; Index++)
    {
        uint32_t FatSector = (Index - FAT_FORMAT_RESERVED_SECTORS) % SectorsPerFat;
        bool InFat = Index < (FAT_FORMAT_RESERVED_SECTORS + (FAT_FORMAT_FAT_COUNT * SectorsPerFat));

        memset(Sector, 0, sizeof(Sector));

        if(InFat && (FatSector == 0U))
        {
            Sector[0] = FAT_FORMAT_MEDIA_DESCRIPTOR;
            Sector[1] = 0xFFU;
            Sector[2] = 0xFFU;
        }
        else if(Index == (MetadataSectors - FAT_FORMAT_ROOT_SECTORS))
        {
            /* First root directory sector: the volume label entry. */
            FATFormat_CopyLabel(&Sector[0], VolumeLabel);
            Sector[11] = FAT_FORMAT_ATTRIBUTE_VOLUME_ID;
            FATFormat_WriteLittleEndian16(&Sector[22], FAT_FORMAT_TIME);
            FATFormat_WriteLittleEndian16(&Sector[24], FAT_FORMAT_DATE);
        }

        if(!Disk_Write(Index, Sector, 1U))
        {
            return false;
        }
    }

    return Disk_Flush();
}
