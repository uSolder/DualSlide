/**
 * @file ram_disk.c
 * @brief FAT12-formatted RAM disk for testing USB mass storage.
 *
 * Layout, in 512-byte sectors:
 *
 *   0       boot sector and BIOS parameter block
 *   1       first FAT
 *   2       second FAT
 *   3-6     root directory (64 entries)
 *   7-255   data area: clusters 2-250, one sector each
 *
 * README.TXT occupies cluster 2.
 */

#include "ram_disk.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Filesystem layout                                                          */
/* -------------------------------------------------------------------------- */

#define RAM_DISK_SIZE                       (RAM_DISK_BLOCK_COUNT * USB_MSC_BLOCK_SIZE)

#define RAM_DISK_RESERVED_SECTORS           (1U)
#define RAM_DISK_FAT_COUNT                  (2U)
#define RAM_DISK_SECTORS_PER_FAT            (1U)
#define RAM_DISK_ROOT_ENTRIES               (64U)
#define RAM_DISK_DIRECTORY_ENTRY_SIZE       (32U)
#define RAM_DISK_ROOT_SECTORS               ((RAM_DISK_ROOT_ENTRIES * RAM_DISK_DIRECTORY_ENTRY_SIZE) / USB_MSC_BLOCK_SIZE)
#define RAM_DISK_MEDIA_DESCRIPTOR           (0xF8U)

#define RAM_DISK_FAT_SECTOR                 (RAM_DISK_RESERVED_SECTORS)
#define RAM_DISK_ROOT_SECTOR                (RAM_DISK_FAT_SECTOR + (RAM_DISK_FAT_COUNT * RAM_DISK_SECTORS_PER_FAT))
#define RAM_DISK_DATA_SECTOR                (RAM_DISK_ROOT_SECTOR + RAM_DISK_ROOT_SECTORS)
#define RAM_DISK_CLUSTER_COUNT              (RAM_DISK_BLOCK_COUNT - RAM_DISK_DATA_SECTOR)

#define RAM_DISK_ATTRIBUTE_ARCHIVE          (0x20U)
#define RAM_DISK_ATTRIBUTE_VOLUME_ID        (0x08U)

/* 2026-10-15 12:00:00 in FAT date and time format. */
#define RAM_DISK_FAT_DATE                   ((uint16_t)(((2026U - 1980U) << 9U) | (10U << 5U) | 15U))
#define RAM_DISK_FAT_TIME                   ((uint16_t)(12U << 11U))

_Static_assert(((RAM_DISK_ROOT_ENTRIES * RAM_DISK_DIRECTORY_ENTRY_SIZE) % USB_MSC_BLOCK_SIZE) == 0U,
               "Root directory must fill whole sectors");
_Static_assert(RAM_DISK_CLUSTER_COUNT < 4085U, "Too many clusters for FAT12");
_Static_assert((((RAM_DISK_CLUSTER_COUNT + 2U) * 3U) / 2U) <= (RAM_DISK_SECTORS_PER_FAT * USB_MSC_BLOCK_SIZE),
               "FAT too small for the cluster count");

static const char RamDisk_ReadmeText[] =
    "DualSlide game storage test drive.\r\n"
    "\r\n"
    "This disk lives in the device's RAM, so anything copied here\r\n"
    "is lost when the device powers off.\r\n";

_Static_assert((sizeof(RamDisk_ReadmeText) - 1U) <= USB_MSC_BLOCK_SIZE, "README.TXT must fit in one cluster");

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool RamDisk_Read(void *Context, uint32_t Block, uint8_t *Data, uint32_t Count);
static bool RamDisk_Write(void *Context, uint32_t Block, const uint8_t *Data, uint32_t Count);
static void RamDisk_WriteLittleEndian16(uint8_t *Data, uint16_t Value);
static void RamDisk_WriteLittleEndian32(uint8_t *Data, uint32_t Value);
static void RamDisk_WriteDirectoryEntry(uint8_t *Entry, const char *Name, uint8_t Attributes,
                                        uint16_t FirstCluster, uint32_t Size);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint8_t RamDisk_Data[RAM_DISK_SIZE] __attribute__((aligned(4)));

const USBMSC_MediaTypeDef RamDisk_Media =
{
    .BlockCount = RAM_DISK_BLOCK_COUNT,
    .Read = RamDisk_Read,
    .Write = RamDisk_Write,
    .Context = NULL
};

/* -------------------------------------------------------------------------- */
/* Medium                                                                     */
/* -------------------------------------------------------------------------- */

static bool RamDisk_Read(void *Context, uint32_t Block, uint8_t *Data, uint32_t Count)
{
    (void)Context;

    if((Block >= RAM_DISK_BLOCK_COUNT) || (Count > (RAM_DISK_BLOCK_COUNT - Block)))
    {
        return false;
    }

    memcpy(Data, &RamDisk_Data[Block * USB_MSC_BLOCK_SIZE], Count * USB_MSC_BLOCK_SIZE);
    return true;
}

static bool RamDisk_Write(void *Context, uint32_t Block, const uint8_t *Data, uint32_t Count)
{
    (void)Context;

    if((Block >= RAM_DISK_BLOCK_COUNT) || (Count > (RAM_DISK_BLOCK_COUNT - Block)))
    {
        return false;
    }

    memcpy(&RamDisk_Data[Block * USB_MSC_BLOCK_SIZE], Data, Count * USB_MSC_BLOCK_SIZE);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Formatting                                                                 */
/* -------------------------------------------------------------------------- */

static void RamDisk_WriteLittleEndian16(uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8U);
}

static void RamDisk_WriteLittleEndian32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8U);
    Data[2] = (uint8_t)(Value >> 16U);
    Data[3] = (uint8_t)(Value >> 24U);
}

/* Name is the 11-character space-padded 8.3 form, such as "README  TXT". */
static void RamDisk_WriteDirectoryEntry(uint8_t *Entry, const char *Name, uint8_t Attributes,
                                        uint16_t FirstCluster, uint32_t Size)
{
    memcpy(&Entry[0], Name, 11U);
    Entry[11] = Attributes;
    RamDisk_WriteLittleEndian16(&Entry[14], RAM_DISK_FAT_TIME);     /* Created */
    RamDisk_WriteLittleEndian16(&Entry[16], RAM_DISK_FAT_DATE);
    RamDisk_WriteLittleEndian16(&Entry[18], RAM_DISK_FAT_DATE);     /* Accessed */
    RamDisk_WriteLittleEndian16(&Entry[22], RAM_DISK_FAT_TIME);     /* Modified */
    RamDisk_WriteLittleEndian16(&Entry[24], RAM_DISK_FAT_DATE);
    RamDisk_WriteLittleEndian16(&Entry[26], FirstCluster);
    RamDisk_WriteLittleEndian32(&Entry[28], Size);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void RamDisk_Format(void)
{
    uint8_t *Boot = &RamDisk_Data[0];
    uint8_t *Root = &RamDisk_Data[RAM_DISK_ROOT_SECTOR * USB_MSC_BLOCK_SIZE];
    uint32_t Fat;

    memset(RamDisk_Data, 0, sizeof(RamDisk_Data));

    /* Boot sector and BIOS parameter block. */
    Boot[0] = 0xEBU;                                                /* Jump over the BPB */
    Boot[1] = 0x3CU;
    Boot[2] = 0x90U;
    memcpy(&Boot[3], "MSDOS5.0", 8U);
    RamDisk_WriteLittleEndian16(&Boot[11], USB_MSC_BLOCK_SIZE);     /* Bytes per sector */
    Boot[13] = 1U;                                                  /* Sectors per cluster */
    RamDisk_WriteLittleEndian16(&Boot[14], RAM_DISK_RESERVED_SECTORS);
    Boot[16] = RAM_DISK_FAT_COUNT;
    RamDisk_WriteLittleEndian16(&Boot[17], RAM_DISK_ROOT_ENTRIES);
    RamDisk_WriteLittleEndian16(&Boot[19], RAM_DISK_BLOCK_COUNT);   /* Total sectors */
    Boot[21] = RAM_DISK_MEDIA_DESCRIPTOR;
    RamDisk_WriteLittleEndian16(&Boot[22], RAM_DISK_SECTORS_PER_FAT);
    RamDisk_WriteLittleEndian16(&Boot[24], 1U);                     /* Sectors per track */
    RamDisk_WriteLittleEndian16(&Boot[26], 1U);                     /* Heads */
    Boot[36] = 0x80U;                                               /* Drive number */
    Boot[38] = 0x29U;                                               /* Extended boot signature */
    RamDisk_WriteLittleEndian32(&Boot[39], 0x44534C44UL);           /* Volume serial number */
    memcpy(&Boot[43], "DUALSLIDE  ", 11U);
    memcpy(&Boot[54], "FAT12   ", 8U);
    Boot[510] = 0x55U;
    Boot[511] = 0xAAU;

    /*
     * Each FAT: entry 0 holds the media descriptor, entry 1 is reserved, and
     * entry 2 (README.TXT) is end-of-chain. Twelve-bit entries pack in pairs.
     */
    for(Fat = 0U; Fat < RAM_DISK_FAT_COUNT; Fat++)
    {
        uint8_t *Table = &RamDisk_Data[(RAM_DISK_FAT_SECTOR + (Fat * RAM_DISK_SECTORS_PER_FAT)) * USB_MSC_BLOCK_SIZE];

        Table[0] = RAM_DISK_MEDIA_DESCRIPTOR;
        Table[1] = 0xFFU;
        Table[2] = 0xFFU;
        Table[3] = 0xFFU;
        Table[4] = 0x0FU;
    }

    RamDisk_WriteDirectoryEntry(&Root[0], "DUALSLIDE  ", RAM_DISK_ATTRIBUTE_VOLUME_ID, 0U, 0U);
    RamDisk_WriteDirectoryEntry(&Root[RAM_DISK_DIRECTORY_ENTRY_SIZE], "README  TXT", RAM_DISK_ATTRIBUTE_ARCHIVE,
                                2U, sizeof(RamDisk_ReadmeText) - 1U);

    memcpy(&RamDisk_Data[RAM_DISK_DATA_SECTOR * USB_MSC_BLOCK_SIZE], RamDisk_ReadmeText, sizeof(RamDisk_ReadmeText) - 1U);
}
