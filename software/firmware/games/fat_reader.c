/**
 * @file fat_reader.c
 * @brief Reads files from the FAT12 or FAT16 filesystem on the game disk.
 */

#include "fat_reader.h"

#include "disk.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Filesystem definitions                                                     */
/* -------------------------------------------------------------------------- */

#define FAT_READER_ENTRY_SIZE               (32U)
#define FAT_READER_ENTRIES_PER_SECTOR       (DISK_BLOCK_SIZE / FAT_READER_ENTRY_SIZE)
#define FAT_READER_ENTRY_END                (0x00U)
#define FAT_READER_ENTRY_DELETED            (0xE5U)
#define FAT_READER_ATTRIBUTE_VOLUME_ID      (0x08U)
#define FAT_READER_ATTRIBUTE_DIRECTORY      (0x10U)
#define FAT_READER_ATTRIBUTE_LONG_NAME      (0x0FU)

#define FAT_READER_MAX_FAT12_CLUSTERS       (4084U)
#define FAT_READER_MAX_FAT16_CLUSTERS       (65524U)
#define FAT_READER_FIRST_CLUSTER            (2U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool Mounted;
    bool Fat16;
    uint32_t SectorsPerCluster;
    uint32_t FatSector;
    uint32_t RootSector;
    uint32_t RootEntryCount;
    uint32_t DataSector;
    uint32_t ClusterCount;
} FAT_VolumeTypeDef;

/* Where the last read ended, so the next one can continue from there. */
typedef struct
{
    uint32_t FirstCluster;
    uint32_t ClusterIndex;
    uint32_t Cluster;
} FAT_PositionTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static FAT_VolumeTypeDef FAT_Volume;
static FAT_PositionTypeDef FAT_Position;

/* One sector of the FAT, or of the root directory, at a time. */
static uint8_t FAT_Sector[DISK_BLOCK_SIZE] __attribute__((aligned(4)));
static uint32_t FAT_SectorNumber = UINT32_MAX;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint16_t FAT_ReadLittleEndian16(const uint8_t *Data)
{
    return (uint16_t)(Data[0] | ((uint16_t)Data[1] << 8U));
}

static uint32_t FAT_ReadLittleEndian32(const uint8_t *Data)
{
    return (uint32_t)Data[0] | ((uint32_t)Data[1] << 8U) | ((uint32_t)Data[2] << 16U) | ((uint32_t)Data[3] << 24U);
}

static bool FAT_LoadSector(uint32_t Sector)
{
    if(Sector == FAT_SectorNumber)
    {
        return true;
    }

    FAT_SectorNumber = UINT32_MAX;

    if(!Disk_Read(Sector, FAT_Sector, 1U))
    {
        return false;
    }

    FAT_SectorNumber = Sector;
    return true;
}

static bool FAT_ReadFatByte(uint32_t Offset, uint8_t *Value)
{
    if(!FAT_LoadSector(FAT_Volume.FatSector + (Offset / DISK_BLOCK_SIZE)))
    {
        return false;
    }

    *Value = FAT_Sector[Offset % DISK_BLOCK_SIZE];
    return true;
}

/* The cluster after Cluster in its chain, from the first FAT. */
static bool FAT_NextCluster(uint32_t Cluster, uint32_t *Next)
{
    uint8_t Low;
    uint8_t High;
    uint32_t Offset = FAT_Volume.Fat16 ? (Cluster * 2U) : (Cluster + (Cluster / 2U));

    /* A FAT12 entry may straddle two sectors, so read it a byte at a time. */
    if(!FAT_ReadFatByte(Offset, &Low) || !FAT_ReadFatByte(Offset + 1U, &High))
    {
        return false;
    }

    *Next = (uint32_t)Low | ((uint32_t)High << 8U);

    if(!FAT_Volume.Fat16)
    {
        *Next = ((Cluster & 1U) != 0U) ? (*Next >> 4U) : (*Next & 0x0FFFU);
    }

    return true;
}

static bool FAT_IsDataCluster(uint32_t Cluster)
{
    return (Cluster >= FAT_READER_FIRST_CLUSTER) && (Cluster < (FAT_READER_FIRST_CLUSTER + FAT_Volume.ClusterCount));
}

/* Finds the Index'th cluster of the file that starts at FirstCluster. */
static bool FAT_SeekCluster(uint32_t FirstCluster, uint32_t Index, uint32_t *Cluster)
{
    uint32_t Step = 0U;
    uint32_t Current = FirstCluster;

    if((FAT_Position.FirstCluster == FirstCluster) && (FAT_Position.ClusterIndex <= Index))
    {
        Step = FAT_Position.ClusterIndex;
        Current = FAT_Position.Cluster;
    }

    for(; Step < Index; Step++)
    {
        if(!FAT_NextCluster(Current, &Current) || !FAT_IsDataCluster(Current))
        {
            return false;
        }
    }

    if(!FAT_IsDataCluster(Current))
    {
        return false;
    }

    FAT_Position.FirstCluster = FirstCluster;
    FAT_Position.ClusterIndex = Index;
    FAT_Position.Cluster = Current;
    *Cluster = Current;

    return true;
}

/* Writes a short name such as "PONG    DSG" as "PONG.DSG". */
static void FAT_FormatName(const uint8_t *Entry, char Name[FAT_READER_NAME_SIZE])
{
    uint32_t Length = 0U;
    uint32_t Index;

    for(Index = 0U; (Index < 8U) && (Entry[Index] != ' '); Index++)
    {
        Name[Length++] = (char)Entry[Index];
    }

    if(Entry[8] != ' ')
    {
        Name[Length++] = '.';

        for(Index = 8U; (Index < 11U) && (Entry[Index] != ' '); Index++)
        {
            Name[Length++] = (char)Entry[Index];
        }
    }

    Name[Length] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool FAT_Mount(void)
{
    uint8_t Boot[DISK_BLOCK_SIZE];
    uint32_t ReservedSectors;
    uint32_t FatCount;
    uint32_t SectorsPerFat;
    uint32_t TotalSectors;
    uint32_t RootSectors;

    memset(&FAT_Volume, 0, sizeof(FAT_Volume));
    memset(&FAT_Position, 0, sizeof(FAT_Position));
    FAT_SectorNumber = UINT32_MAX;

    if(!Disk_Read(0U, Boot, 1U) || (Boot[510] != 0x55U) || (Boot[511] != 0xAAU) ||
       (FAT_ReadLittleEndian16(&Boot[11]) != DISK_BLOCK_SIZE))
    {
        return false;
    }

    FAT_Volume.SectorsPerCluster = Boot[13];
    ReservedSectors = FAT_ReadLittleEndian16(&Boot[14]);
    FatCount = Boot[16];
    FAT_Volume.RootEntryCount = FAT_ReadLittleEndian16(&Boot[17]);
    TotalSectors = FAT_ReadLittleEndian16(&Boot[19]);
    SectorsPerFat = FAT_ReadLittleEndian16(&Boot[22]);

    if(TotalSectors == 0U)
    {
        TotalSectors = FAT_ReadLittleEndian32(&Boot[32]);
    }

    /* FAT32 keeps its root directory in clusters and has no 16-bit FAT size. */
    if((FAT_Volume.SectorsPerCluster == 0U) || ((FAT_Volume.SectorsPerCluster & (FAT_Volume.SectorsPerCluster - 1U)) != 0U) ||
       (ReservedSectors == 0U) || (FatCount == 0U) || (SectorsPerFat == 0U) || (FAT_Volume.RootEntryCount == 0U) ||
       (TotalSectors > Disk_GetBlockCount()))
    {
        return false;
    }

    RootSectors = ((FAT_Volume.RootEntryCount * FAT_READER_ENTRY_SIZE) + DISK_BLOCK_SIZE - 1U) / DISK_BLOCK_SIZE;
    FAT_Volume.FatSector = ReservedSectors;
    FAT_Volume.RootSector = ReservedSectors + (FatCount * SectorsPerFat);
    FAT_Volume.DataSector = FAT_Volume.RootSector + RootSectors;

    if(FAT_Volume.DataSector >= TotalSectors)
    {
        return false;
    }

    FAT_Volume.ClusterCount = (TotalSectors - FAT_Volume.DataSector) / FAT_Volume.SectorsPerCluster;

    if(FAT_Volume.ClusterCount > FAT_READER_MAX_FAT16_CLUSTERS)
    {
        return false;
    }

    FAT_Volume.Fat16 = FAT_Volume.ClusterCount > FAT_READER_MAX_FAT12_CLUSTERS;

    /* The FAT must have an entry for every cluster. */
    if((FAT_Volume.Fat16 ? ((FAT_Volume.ClusterCount + 2U) * 2U) : ((((FAT_Volume.ClusterCount + 2U) * 3U) + 1U) / 2U)) >
       (SectorsPerFat * DISK_BLOCK_SIZE))
    {
        return false;
    }

    FAT_Volume.Mounted = true;
    return true;
}

bool FAT_NextFile(uint32_t *Position, FAT_FileTypeDef *File)
{
    while(FAT_Volume.Mounted && (*Position < FAT_Volume.RootEntryCount))
    {
        const uint32_t Index = (*Position)++;
        const uint8_t *Entry;
        uint8_t Attributes;

        if(!FAT_LoadSector(FAT_Volume.RootSector + (Index / FAT_READER_ENTRIES_PER_SECTOR)))
        {
            return false;
        }

        Entry = &FAT_Sector[(Index % FAT_READER_ENTRIES_PER_SECTOR) * FAT_READER_ENTRY_SIZE];
        Attributes = Entry[11];

        if(Entry[0] == FAT_READER_ENTRY_END)
        {
            *Position = FAT_Volume.RootEntryCount;
            return false;
        }

        if((Entry[0] == FAT_READER_ENTRY_DELETED) || (Attributes == FAT_READER_ATTRIBUTE_LONG_NAME) ||
           ((Attributes & (FAT_READER_ATTRIBUTE_VOLUME_ID | FAT_READER_ATTRIBUTE_DIRECTORY)) != 0U))
        {
            continue;
        }

        FAT_FormatName(Entry, File->Name);
        File->Attributes = Attributes;
        File->FirstCluster = FAT_ReadLittleEndian16(&Entry[26]);
        File->Size = FAT_ReadLittleEndian32(&Entry[28]);

        return true;
    }

    return false;
}

bool FAT_Read(const FAT_FileTypeDef *File, uint32_t Offset, void *Data, uint32_t Size)
{
    const uint32_t ClusterBytes = FAT_Volume.SectorsPerCluster * DISK_BLOCK_SIZE;
    uint8_t *Destination = Data;

    if(!FAT_Volume.Mounted || (Offset > File->Size) || (Size > (File->Size - Offset)))
    {
        return false;
    }

    while(Size != 0U)
    {
        uint32_t Cluster;
        uint32_t InCluster = Offset % ClusterBytes;
        uint32_t Sector;
        uint32_t InSector = InCluster % DISK_BLOCK_SIZE;
        uint32_t Count;

        if(!FAT_SeekCluster(File->FirstCluster, Offset / ClusterBytes, &Cluster))
        {
            return false;
        }

        Sector = FAT_Volume.DataSector + ((Cluster - FAT_READER_FIRST_CLUSTER) * FAT_Volume.SectorsPerCluster) + (InCluster / DISK_BLOCK_SIZE);

        if((InSector == 0U) && (Size >= DISK_BLOCK_SIZE))
        {
            /* Whole sectors, up to the end of this cluster, straight into Data. */
            uint32_t Sectors = (ClusterBytes - InCluster) / DISK_BLOCK_SIZE;

            if(Sectors > (Size / DISK_BLOCK_SIZE))
            {
                Sectors = Size / DISK_BLOCK_SIZE;
            }

            if(!Disk_Read(Sector, Destination, Sectors))
            {
                return false;
            }

            Count = Sectors * DISK_BLOCK_SIZE;
        }
        else
        {
            if(!FAT_LoadSector(Sector))
            {
                return false;
            }

            Count = DISK_BLOCK_SIZE - InSector;
            Count = (Count > Size) ? Size : Count;
            memcpy(Destination, &FAT_Sector[InSector], Count);
        }

        Destination += Count;
        Offset += Count;
        Size -= Count;
    }

    return true;
}
