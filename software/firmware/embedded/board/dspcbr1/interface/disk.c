/**
 * @file disk.c
 * @brief Game disk in the internal Flash of Bank 2.
 *
 * The disk region (DISK in the linker script) is divided into Flash sectors.
 * The first two are map sectors; the rest are data sectors. The disk is
 * divided into groups of one Flash sector each, and one data sector more
 * than there are groups is kept, so a free sector always exists.
 *
 * A group is never rewritten in place. Flushing a group writes it to the free
 * data sector, then appends a record "group -> data sector" to the active map
 * sector. The sector the group used to occupy becomes the free one. Losing
 * power at any moment therefore leaves either the old or the new copy mapped.
 *
 * The map is rebuilt at start-up by replaying the records of the active map
 * sector in order. When the active map sector is full, the current map is
 * written to the other map sector, whose header is programmed last with a
 * higher generation, and that sector becomes active.
 *
 * Writes go to a one-group RAM cache, which is flushed when a write moves to
 * another group, when writing pauses, or on Disk_Flush().
 */

#include "disk.h"

#include "flash_controller.h"
#include "system_time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Disk format                                                                */
/* -------------------------------------------------------------------------- */

#define DISK_MAP_SECTOR_COUNT               (2U)
#define DISK_MAX_SECTORS                    (64U)
#define DISK_MAX_GROUPS                     (DISK_MAX_SECTORS - DISK_MAP_SECTOR_COUNT - 1U)
#define DISK_MAX_SECTOR_SIZE                (8192U)
#define DISK_UNMAPPED                       (0xFFU)

#define DISK_HEADER_MAGIC                   (0x4D4B5344UL) /* "DSKM" */
#define DISK_RECORD_MAGIC                   (0x524B5344UL) /* "DSKR" */
#define DISK_ERASED_WORD                    (0xFFFFFFFFUL)

/* Writing must pause this long before the cache is flushed. */
#define DISK_IDLE_FLUSH_MILLISECONDS        (250U)

/**
 * @brief First Flash word of a map sector.
 */
typedef struct
{
    uint32_t Magic;
    uint32_t Generation;
    uint32_t Geometry;                      /* Group count | (data sector count << 16) */
    uint32_t CRC;
} Disk_HeaderTypeDef;

/**
 * @brief One Flash word of a map sector after the header: group -> data sector.
 */
typedef struct
{
    uint32_t Magic;
    uint16_t Group;
    uint16_t DataSector;
    uint32_t Reserved;
    uint32_t CRC;
} Disk_RecordTypeDef;

_Static_assert(sizeof(Disk_HeaderTypeDef) == 16U, "Map header must be one Flash word");
_Static_assert(sizeof(Disk_RecordTypeDef) == 16U, "Map record must be one Flash word");

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

extern uint8_t __disk_flash_start__;
extern uint8_t __disk_flash_end__;

static FlashController_HandleTypeDef Disk_Flash;
static bool Disk_Initialized;

/* Geometry. */
static size_t Disk_FirstSector;
static size_t Disk_SectorSize;
static uint32_t Disk_DataSectorCount;
static uint32_t Disk_GroupCount;
static uint32_t Disk_BlocksPerGroup;

/* Map. */
static uint8_t Disk_Map[DISK_MAX_GROUPS];
static uint32_t Disk_ActiveMapSector;
static uint32_t Disk_Generation;
static size_t Disk_MapWriteOffset;
static uint32_t Disk_NextDataSector;

/* Cache of one group. */
static uint8_t Disk_Cache[DISK_MAX_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t Disk_CacheGroup;
static bool Disk_CacheValid;
static bool Disk_CacheDirty;
static uint32_t Disk_LastWriteMilliseconds;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint32_t Disk_CalculateCrc(const void *Data, size_t Size)
{
    const uint8_t *Bytes = Data;
    uint32_t CRC = 0xFFFFFFFFUL;
    size_t Index;
    uint8_t Bit;

    for(Index = 0U; Index < Size; Index++)
    {
        CRC ^= Bytes[Index];

        for(Bit = 0U; Bit < 8U; Bit++)
        {
            CRC = (CRC >> 1U) ^ ((CRC & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }

    return ~CRC;
}

static uintptr_t Disk_GetSectorAddress(uint32_t Sector)
{
    return (uintptr_t)&__disk_flash_start__ + ((uintptr_t)Sector * Disk_SectorSize);
}

static uintptr_t Disk_GetDataSectorAddress(uint32_t DataSector)
{
    return Disk_GetSectorAddress(DISK_MAP_SECTOR_COUNT + DataSector);
}

static bool Disk_EraseSector(uint32_t Sector)
{
    return FlashController_EraseSector(&Disk_Flash, Disk_FirstSector + Sector) == FLASH_CONTROLLER_RESULT_OK;
}

static uint32_t Disk_GetGeometry(void)
{
    return Disk_GroupCount | (Disk_DataSectorCount << 16U);
}

static bool Disk_IsHeaderValid(const Disk_HeaderTypeDef *Header)
{
    return (Header->Magic == DISK_HEADER_MAGIC) &&
           (Header->Geometry == Disk_GetGeometry()) &&
           (Header->CRC == Disk_CalculateCrc(Header, offsetof(Disk_HeaderTypeDef, CRC)));
}

static bool Disk_IsWordErased(const void *Word)
{
    const uint32_t *Words = Word;
    size_t Index;

    for(Index = 0U; Index < (Disk_Flash.ProgramUnitBytes / sizeof(uint32_t)); Index++)
    {
        if(Words[Index] != DISK_ERASED_WORD)
        {
            return false;
        }
    }

    return true;
}

static bool Disk_ProgramRecord(uint32_t MapSector, size_t Offset, uint32_t Group, uint32_t DataSector)
{
    Disk_RecordTypeDef Record =
    {
        .Magic = DISK_RECORD_MAGIC,
        .Group = (uint16_t)Group,
        .DataSector = (uint16_t)DataSector,
        .Reserved = 0U
    };

    Record.CRC = Disk_CalculateCrc(&Record, offsetof(Disk_RecordTypeDef, CRC));

    return FlashController_Program(&Disk_Flash, Disk_GetSectorAddress(MapSector) + Offset, &Record, sizeof(Record)) == FLASH_CONTROLLER_RESULT_OK;
}

static bool Disk_ProgramHeader(uint32_t MapSector, uint32_t Generation)
{
    Disk_HeaderTypeDef Header =
    {
        .Magic = DISK_HEADER_MAGIC,
        .Generation = Generation,
        .Geometry = Disk_GetGeometry()
    };

    Header.CRC = Disk_CalculateCrc(&Header, offsetof(Disk_HeaderTypeDef, CRC));

    return FlashController_Program(&Disk_Flash, Disk_GetSectorAddress(MapSector), &Header, sizeof(Header)) == FLASH_CONTROLLER_RESULT_OK;
}

/*
 * Writes the current map to the other map sector and makes it active. Its
 * header is programmed last, so until then the old map sector stays in use.
 */
static bool Disk_CompactMap(void)
{
    uint32_t MapSector = (Disk_ActiveMapSector + 1U) % DISK_MAP_SECTOR_COUNT;
    size_t Offset = sizeof(Disk_HeaderTypeDef);
    uint32_t Group;

    if(!Disk_EraseSector(MapSector))
    {
        return false;
    }

    for(Group = 0U; Group < Disk_GroupCount; Group++)
    {
        if(Disk_Map[Group] == DISK_UNMAPPED)
        {
            continue;
        }

        if(!Disk_ProgramRecord(MapSector, Offset, Group, Disk_Map[Group]))
        {
            return false;
        }

        Offset += sizeof(Disk_RecordTypeDef);
    }

    if(!Disk_ProgramHeader(MapSector, Disk_Generation + 1U))
    {
        return false;
    }

    Disk_ActiveMapSector = MapSector;
    Disk_Generation++;
    Disk_MapWriteOffset = Offset;

    return true;
}

static bool Disk_AppendRecord(uint32_t Group, uint32_t DataSector)
{
    if(((Disk_MapWriteOffset + sizeof(Disk_RecordTypeDef)) > Disk_SectorSize) && !Disk_CompactMap())
    {
        return false;
    }

    if(!Disk_ProgramRecord(Disk_ActiveMapSector, Disk_MapWriteOffset, Group, DataSector))
    {
        return false;
    }

    Disk_MapWriteOffset += sizeof(Disk_RecordTypeDef);

    return true;
}

/* Starts an empty map in the first map sector. */
static bool Disk_CreateMap(void)
{
    memset(Disk_Map, DISK_UNMAPPED, sizeof(Disk_Map));

    if(!Disk_EraseSector(0U) || !Disk_ProgramHeader(0U, 1U))
    {
        return false;
    }

    Disk_ActiveMapSector = 0U;
    Disk_Generation = 1U;
    Disk_MapWriteOffset = sizeof(Disk_HeaderTypeDef);

    return true;
}

/* Replays the records of the active map sector. */
static bool Disk_LoadMap(void)
{
    Disk_HeaderTypeDef Headers[DISK_MAP_SECTOR_COUNT];
    bool Valid[DISK_MAP_SECTOR_COUNT];
    Disk_RecordTypeDef Record;
    uint32_t MapSector;
    size_t Offset;

    for(MapSector = 0U; MapSector < DISK_MAP_SECTOR_COUNT; MapSector++)
    {
        Valid[MapSector] = (FlashController_Read(&Disk_Flash, Disk_GetSectorAddress(MapSector), &Headers[MapSector], sizeof(Headers[MapSector])) == FLASH_CONTROLLER_RESULT_OK) &&
                           Disk_IsHeaderValid(&Headers[MapSector]);
    }

    if(!Valid[0] && !Valid[1])
    {
        return Disk_CreateMap();
    }

    Disk_ActiveMapSector = (Valid[0] && (!Valid[1] || (Headers[0].Generation >= Headers[1].Generation))) ? 0U : 1U;
    Disk_Generation = Headers[Disk_ActiveMapSector].Generation;

    memset(Disk_Map, DISK_UNMAPPED, sizeof(Disk_Map));

    for(Offset = sizeof(Disk_HeaderTypeDef); Offset < Disk_SectorSize; Offset += sizeof(Record))
    {
        if(FlashController_Read(&Disk_Flash, Disk_GetSectorAddress(Disk_ActiveMapSector) + Offset, &Record, sizeof(Record)) != FLASH_CONTROLLER_RESULT_OK)
        {
            return false;
        }

        if(Disk_IsWordErased(&Record))
        {
            break;
        }

        /* A record cut short by a power loss fails its CRC and is skipped. */
        if((Record.Magic == DISK_RECORD_MAGIC) &&
           (Record.CRC == Disk_CalculateCrc(&Record, offsetof(Disk_RecordTypeDef, CRC))) &&
           (Record.Group < Disk_GroupCount) && (Record.DataSector < Disk_DataSectorCount))
        {
            Disk_Map[Record.Group] = (uint8_t)Record.DataSector;
        }
    }

    Disk_MapWriteOffset = Offset;

    return true;
}

static bool Disk_IsDataSectorUsed(uint32_t DataSector)
{
    uint32_t Group;

    for(Group = 0U; Group < Disk_GroupCount; Group++)
    {
        if(Disk_Map[Group] == DataSector)
        {
            return true;
        }
    }

    return false;
}

/* Picks the next unmapped data sector after the last one used, spreading wear. */
static bool Disk_FindFreeDataSector(uint32_t *DataSector)
{
    uint32_t Step;

    for(Step = 0U; Step < Disk_DataSectorCount; Step++)
    {
        uint32_t Candidate = (Disk_NextDataSector + Step) % Disk_DataSectorCount;

        if(!Disk_IsDataSectorUsed(Candidate))
        {
            *DataSector = Candidate;
            return true;
        }
    }

    return false;
}

static bool Disk_ReadGroup(uint32_t Group, uint8_t *Data)
{
    if(Disk_Map[Group] == DISK_UNMAPPED)
    {
        memset(Data, 0, Disk_SectorSize);
        return true;
    }

    return FlashController_Read(&Disk_Flash, Disk_GetDataSectorAddress(Disk_Map[Group]), Data, Disk_SectorSize) == FLASH_CONTROLLER_RESULT_OK;
}

/* Writes the cached group to the free data sector and maps it there. */
static bool Disk_FlushCache(void)
{
    uint32_t DataSector;
    uintptr_t Address;

    if(!Disk_CacheDirty)
    {
        return true;
    }

    /* Rewriting what is already there costs an erase for nothing. */
    if((Disk_Map[Disk_CacheGroup] != DISK_UNMAPPED) &&
       (FlashController_Verify(&Disk_Flash, Disk_GetDataSectorAddress(Disk_Map[Disk_CacheGroup]), Disk_Cache, Disk_SectorSize) == FLASH_CONTROLLER_RESULT_OK))
    {
        Disk_CacheDirty = false;
        return true;
    }

    if(!Disk_FindFreeDataSector(&DataSector))
    {
        return false;
    }

    Address = Disk_GetDataSectorAddress(DataSector);

    /*
     * Always erase rather than read to check: a free sector may hold a
     * program cut short by a power loss, and reading that raises an ECC error.
     */
    if(!Disk_EraseSector(DISK_MAP_SECTOR_COUNT + DataSector))
    {
        return false;
    }

    if((FlashController_Program(&Disk_Flash, Address, Disk_Cache, Disk_SectorSize) != FLASH_CONTROLLER_RESULT_OK) ||
       (FlashController_Verify(&Disk_Flash, Address, Disk_Cache, Disk_SectorSize) != FLASH_CONTROLLER_RESULT_OK) ||
       !Disk_AppendRecord(Disk_CacheGroup, DataSector))
    {
        return false;
    }

    Disk_Map[Disk_CacheGroup] = (uint8_t)DataSector;
    Disk_NextDataSector = (DataSector + 1U) % Disk_DataSectorCount;
    Disk_CacheDirty = false;

    return true;
}

/* Makes the cache hold Group, flushing whatever it held before. */
static bool Disk_SelectCacheGroup(uint32_t Group, bool Load)
{
    if(Disk_CacheValid && (Disk_CacheGroup == Group))
    {
        return true;
    }

    if(!Disk_FlushCache())
    {
        return false;
    }

    Disk_CacheValid = false;

    if(Load && !Disk_ReadGroup(Group, Disk_Cache))
    {
        return false;
    }

    Disk_CacheGroup = Group;
    Disk_CacheValid = true;

    return true;
}

static bool Disk_IsRangeValid(uint32_t Block, const void *Data, uint32_t Count)
{
    return Disk_Initialized && (Data != NULL) && (Block < Disk_GetBlockCount()) && (Count <= (Disk_GetBlockCount() - Block));
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Disk_Init(void)
{
    FlashController_SectorInformationTypeDef Information;
    uintptr_t Start = (uintptr_t)&__disk_flash_start__;
    uintptr_t End = (uintptr_t)&__disk_flash_end__;
    size_t SectorCount;
    size_t Sector;

    Disk_Initialized = false;
    Disk_CacheValid = false;
    Disk_CacheDirty = false;
    Disk_NextDataSector = 0U;

    if(FlashController_Init(&Disk_Flash) != FLASH_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    /* Find the Flash sector where the disk region starts. */
    for(Sector = 0U; Sector < Disk_Flash.SectorCount; Sector++)
    {
        if((FlashController_GetSectorInformation(&Disk_Flash, Sector, &Information) == FLASH_CONTROLLER_RESULT_OK) &&
           (Information.StartAddress == Start))
        {
            break;
        }
    }

    if((Sector == Disk_Flash.SectorCount) || (Information.SizeBytes > DISK_MAX_SECTOR_SIZE) ||
       (((End - Start) % Information.SizeBytes) != 0U))
    {
        return false;
    }

    SectorCount = (End - Start) / Information.SizeBytes;

    if((SectorCount > DISK_MAX_SECTORS) || (SectorCount < (DISK_MAP_SECTOR_COUNT + 2U)))
    {
        return false;
    }

    Disk_FirstSector = Sector;
    Disk_SectorSize = Information.SizeBytes;
    Disk_DataSectorCount = (uint32_t)(SectorCount - DISK_MAP_SECTOR_COUNT);
    Disk_GroupCount = Disk_DataSectorCount - 1U;
    Disk_BlocksPerGroup = (uint32_t)(Disk_SectorSize / DISK_BLOCK_SIZE);

    if(!Disk_LoadMap())
    {
        return false;
    }

    Disk_Initialized = true;

    return true;
}

uint32_t Disk_GetBlockCount(void)
{
    return Disk_Initialized ? (Disk_GroupCount * Disk_BlocksPerGroup) : 0U;
}

bool Disk_Read(uint32_t Block, void *Data, uint32_t Count)
{
    uint8_t *Destination = Data;

    if(!Disk_IsRangeValid(Block, Data, Count))
    {
        return false;
    }

    for(; Count != 0U; Count--, Block++, Destination += DISK_BLOCK_SIZE)
    {
        uint32_t Group = Block / Disk_BlocksPerGroup;
        size_t Offset = (size_t)(Block % Disk_BlocksPerGroup) * DISK_BLOCK_SIZE;

        if(Disk_CacheValid && (Disk_CacheGroup == Group))
        {
            memcpy(Destination, &Disk_Cache[Offset], DISK_BLOCK_SIZE);
        }
        else if(Disk_Map[Group] == DISK_UNMAPPED)
        {
            memset(Destination, 0, DISK_BLOCK_SIZE);
        }
        else if(FlashController_Read(&Disk_Flash, Disk_GetDataSectorAddress(Disk_Map[Group]) + Offset, Destination, DISK_BLOCK_SIZE) != FLASH_CONTROLLER_RESULT_OK)
        {
            return false;
        }
    }

    return true;
}

bool Disk_Write(uint32_t Block, const void *Data, uint32_t Count)
{
    const uint8_t *Source = Data;

    if(!Disk_IsRangeValid(Block, Data, Count))
    {
        return false;
    }

    while(Count != 0U)
    {
        uint32_t Group = Block / Disk_BlocksPerGroup;
        uint32_t First = Block % Disk_BlocksPerGroup;
        uint32_t Blocks = Disk_BlocksPerGroup - First;

        if(Blocks > Count)
        {
            Blocks = Count;
        }

        /* A write covering the whole group needs nothing read first. */
        if(!Disk_SelectCacheGroup(Group, Blocks != Disk_BlocksPerGroup))
        {
            return false;
        }

        memcpy(&Disk_Cache[First * DISK_BLOCK_SIZE], Source, Blocks * DISK_BLOCK_SIZE);
        Disk_CacheDirty = true;
        Disk_LastWriteMilliseconds = SystemTime_GetMilliseconds();

        Block += Blocks;
        Count -= Blocks;
        Source += Blocks * DISK_BLOCK_SIZE;
    }

    return true;
}

bool Disk_Flush(void)
{
    return Disk_Initialized && Disk_FlushCache();
}

void Disk_Process(void)
{
    if(Disk_Initialized && Disk_CacheDirty &&
       ((uint32_t)(SystemTime_GetMilliseconds() - Disk_LastWriteMilliseconds) >= DISK_IDLE_FLUSH_MILLISECONDS))
    {
        (void)Disk_FlushCache();
    }
}
