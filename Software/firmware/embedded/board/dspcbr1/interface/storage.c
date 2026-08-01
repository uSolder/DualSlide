/**
 * @file storage.c
 * @brief Persistent two-sector key/value storage implementation.
 */

#include "storage.h"

#include "flash_controller.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Storage format                                                             */
/* -------------------------------------------------------------------------- */

#define STORAGE_FORMAT_VERSION                    (1UL)
#define STORAGE_SECTOR_MAGIC                      (0x53544F52UL)
#define STORAGE_SECTOR_ACTIVATION_MAGIC           (0x53544143UL)
#define STORAGE_RECORD_MAGIC                      (0x53545243UL)
#define STORAGE_RECORD_COMMIT_MAGIC               (0x5354434DUL)
#define STORAGE_ERASED_WORD                       (0xFFFFFFFFUL)
#define STORAGE_DELETED_DATA_SIZE                 (0xFFFFFFFFUL)
#define STORAGE_MAXIMUM_ENTRY_SIZE_BYTES          (4096UL)
#define STORAGE_MAXIMUM_LIVE_KEYS                 (128UL)

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t format_version;
    uint32_t crc;
} Storage_SectorHeader;

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t sector_header_crc;
    uint32_t crc;
} Storage_SectorActivation;

typedef struct
{
    uint32_t magic;
    uint32_t key;
    uint32_t data_size;
    uint32_t data_crc;
} Storage_RecordHeader;

typedef struct
{
    uint32_t magic;
    uint32_t header_crc;
    uint32_t data_crc;
    uint32_t crc;
} Storage_RecordCommit;

typedef struct
{
    Storage_KeyTypeDef key;
    uintptr_t data_address;
    uint32_t data_size;
    bool present;
} Storage_LiveRecord;

typedef struct
{
    FlashController_Handle flash_controller;
    FlashController_SectorInformation sectors[2];
    uintptr_t active_sector_address;
    size_t active_sector_size;
    size_t active_write_offset;
    uint32_t active_generation;
    bool initialized;
} Storage_Context;

typedef enum
{
    STORAGE_RECORD_STATE_EMPTY,
    STORAGE_RECORD_STATE_VALID,
    STORAGE_RECORD_STATE_INVALID
} Storage_RecordState;

static Storage_Context storage_context;

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */

static size_t Storage_AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t Storage_CalculateCrc(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;
    uint8_t bit;

    for (index = 0U; index < size; index++)
    {
        crc ^= bytes[index];

        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }

    return ~crc;
}

static Storage_ResultTypeDef Storage_MapFlashResult(FlashController_Result result)
{
    return result == FLASH_CONTROLLER_RESULT_OK ? STORAGE_RESULT_OK : STORAGE_RESULT_ERROR;
}

static bool Storage_IsInitialized(void)
{
    return storage_context.initialized;
}

static size_t Storage_GetProgramUnitSize(void)
{
    return storage_context.flash_controller.program_unit_bytes;
}

static uintptr_t Storage_GetOtherSectorAddress(void)
{
    return storage_context.active_sector_address == storage_context.sectors[0].start_address ? storage_context.sectors[1].start_address : storage_context.sectors[0].start_address;
}

static size_t Storage_GetRecordSize(uint32_t dataSize)
{
    size_t payloadSize = dataSize == STORAGE_DELETED_DATA_SIZE ? 0U : dataSize;

    return sizeof(Storage_RecordHeader) + Storage_AlignUp(payloadSize, Storage_GetProgramUnitSize()) + sizeof(Storage_RecordCommit);
}

static bool Storage_IsRecordDataSizeValid(uint32_t dataSize)
{
    return (dataSize == STORAGE_DELETED_DATA_SIZE) || ((dataSize != 0U) && (dataSize <= STORAGE_MAXIMUM_ENTRY_SIZE_BYTES));
}

static Storage_ResultTypeDef Storage_ReadFlash(uintptr_t address, void *data, size_t size)
{
    return Storage_MapFlashResult(FlashController_Read(&storage_context.flash_controller, address, data, size));
}

static Storage_ResultTypeDef Storage_ProgramFlash(uintptr_t address, const void *data, size_t size)
{
    FlashController_Result flashResult = FlashController_Program(&storage_context.flash_controller, address, data, size);
    return Storage_MapFlashResult(flashResult);
}

static uint32_t Storage_CalculateFlashCrc(uintptr_t address, uint32_t size)
{
    uint8_t buffer[16];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t remaining = size;

    while (remaining != 0U)
    {
        size_t chunkSize = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        const uint8_t *bytes = buffer;
        size_t index;
        uint8_t bit;

        if (Storage_ReadFlash(address, buffer, chunkSize) != STORAGE_RESULT_OK)
        {
            return 0U;
        }

        for (index = 0U; index < chunkSize; index++)
        {
            crc ^= bytes[index];

            for (bit = 0U; bit < 8U; bit++)
            {
                crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
            }
        }

        address += chunkSize;
        remaining -= (uint32_t)chunkSize;
    }

    return ~crc;
}

static bool Storage_IsSectorHeaderValid(const Storage_SectorHeader *header)
{
    return (header->magic == STORAGE_SECTOR_MAGIC) && (header->format_version == STORAGE_FORMAT_VERSION) && (header->crc == Storage_CalculateCrc(header, offsetof(Storage_SectorHeader, crc)));
}

static bool Storage_IsSectorActivationValid(const Storage_SectorActivation *activation, const Storage_SectorHeader *header)
{
    return (activation->magic == STORAGE_SECTOR_ACTIVATION_MAGIC) && (activation->generation == header->generation) && (activation->sector_header_crc == header->crc) && (activation->crc == Storage_CalculateCrc(activation, offsetof(Storage_SectorActivation, crc)));
}

static bool Storage_IsSectorActive(uintptr_t sectorAddress, Storage_SectorHeader *header)
{
    Storage_SectorActivation activation;

    if (Storage_ReadFlash(sectorAddress, header, sizeof(*header)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    if (!Storage_IsSectorHeaderValid(header))
    {
        return false;
    }

    if (Storage_ReadFlash(sectorAddress + sizeof(*header), &activation, sizeof(activation)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    return Storage_IsSectorActivationValid(&activation, header);
}

static Storage_ResultTypeDef Storage_CreateSectorHeader(uintptr_t sectorAddress, uint32_t generation)
{
    Storage_SectorHeader header;

    header.magic = STORAGE_SECTOR_MAGIC;
    header.generation = generation;
    header.format_version = STORAGE_FORMAT_VERSION;
    header.crc = Storage_CalculateCrc(&header, offsetof(Storage_SectorHeader, crc));

    return Storage_ProgramFlash(sectorAddress, &header, sizeof(header));
}

static Storage_ResultTypeDef Storage_ActivateSector(uintptr_t sectorAddress, uint32_t generation)
{
    Storage_SectorHeader header;
    Storage_SectorActivation activation;
    Storage_ResultTypeDef result;

    result = Storage_ReadFlash(sectorAddress, &header, sizeof(header));

    if ((result != STORAGE_RESULT_OK) || !Storage_IsSectorHeaderValid(&header) ||
        (header.generation != generation))
    {
        return STORAGE_RESULT_ERROR;
    }

    activation.magic = STORAGE_SECTOR_ACTIVATION_MAGIC;
    activation.generation = generation;
    activation.sector_header_crc = header.crc;
    activation.crc = Storage_CalculateCrc(&activation, offsetof(Storage_SectorActivation, crc));

    return Storage_ProgramFlash(sectorAddress + sizeof(header), &activation, sizeof(activation));
}

static Storage_ResultTypeDef Storage_CreateActiveSector(uintptr_t sectorAddress, uint32_t generation)
{
    Storage_ResultTypeDef result = Storage_CreateSectorHeader(sectorAddress, generation);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    return Storage_ActivateSector(sectorAddress, generation);
}

static Storage_RecordState Storage_ReadRecord(uintptr_t sectorAddress, size_t sectorSize, size_t offset, Storage_LiveRecord *record, size_t *recordSize)
{
    Storage_RecordHeader header;
    Storage_RecordCommit commit;
    size_t payloadSize;
    size_t totalSize;

    if ((offset + sizeof(header)) > sectorSize)
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if (Storage_ReadFlash(sectorAddress + offset, &header, sizeof(header)) != STORAGE_RESULT_OK)
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if ((header.magic == STORAGE_ERASED_WORD) &&
        (header.key == STORAGE_ERASED_WORD) &&
        (header.data_size == STORAGE_ERASED_WORD) &&
        (header.data_crc == STORAGE_ERASED_WORD))
    {
        return STORAGE_RECORD_STATE_EMPTY;
    }

    if ((header.magic != STORAGE_RECORD_MAGIC) || !Storage_IsRecordDataSizeValid(header.data_size))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    payloadSize = header.data_size == STORAGE_DELETED_DATA_SIZE ? 0U : header.data_size;
    totalSize = Storage_GetRecordSize(header.data_size);

    if ((totalSize > (sectorSize - offset)) ||
        (Storage_ReadFlash(sectorAddress + offset + sizeof(header) + Storage_AlignUp(payloadSize, Storage_GetProgramUnitSize()), &commit, sizeof(commit)) != STORAGE_RESULT_OK))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if ((commit.magic != STORAGE_RECORD_COMMIT_MAGIC) ||
        (commit.header_crc != Storage_CalculateCrc(&header, sizeof(header))) ||
        (commit.data_crc != header.data_crc) ||
        (commit.crc != Storage_CalculateCrc(&commit, offsetof(Storage_RecordCommit, crc))))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if ((payloadSize != 0U) &&
        (Storage_CalculateFlashCrc(sectorAddress + offset + sizeof(header), (uint32_t)payloadSize) != header.data_crc))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if (record != NULL)
    {
        record->key = header.key;
        record->data_address = sectorAddress + offset + sizeof(header);
        record->data_size = header.data_size;
        record->present = header.data_size != STORAGE_DELETED_DATA_SIZE;
    }

    if (recordSize != NULL)
    {
        *recordSize = totalSize;
    }

    return STORAGE_RECORD_STATE_VALID;
}

static size_t Storage_FindWriteOffset(uintptr_t sectorAddress, size_t sectorSize)
{
    size_t offset = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);

    while (offset < sectorSize)
    {
        size_t recordSize;
        Storage_RecordState state = Storage_ReadRecord(sectorAddress, sectorSize, offset, NULL, &recordSize);

        if (state == STORAGE_RECORD_STATE_EMPTY)
        {
            return offset;
        }

        if (state != STORAGE_RECORD_STATE_VALID)
        {
            return sectorSize;
        }

        offset += recordSize;
    }

    return sectorSize;
}

static Storage_ResultTypeDef Storage_WriteRecord(uintptr_t sectorAddress, size_t sectorSize, size_t *offset, Storage_KeyTypeDef key, const void *data, uint32_t dataSize)
{
    Storage_RecordHeader header;
    Storage_RecordCommit commit;
    uint8_t programUnit[16];
    const uint8_t *source = data;
    size_t payloadSize = dataSize == STORAGE_DELETED_DATA_SIZE ? 0U : dataSize;
    size_t payloadPaddedSize = Storage_AlignUp(payloadSize, Storage_GetProgramUnitSize());
    size_t totalSize = Storage_GetRecordSize(dataSize);
    size_t written;
    Storage_ResultTypeDef result;

    if ((totalSize > (sectorSize - *offset)) ||
        ((dataSize != STORAGE_DELETED_DATA_SIZE) && ((data == NULL) || (dataSize == 0U))))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    header.magic = STORAGE_RECORD_MAGIC;
    header.key = key;
    header.data_size = dataSize;
    header.data_crc = payloadSize == 0U ? Storage_CalculateCrc(NULL, 0U) : Storage_CalculateCrc(data, payloadSize);
    result = Storage_ProgramFlash(sectorAddress + *offset, &header, sizeof(header));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    for (written = 0U; written < payloadPaddedSize; written += sizeof(programUnit))
    {
        size_t remaining = payloadSize > written ? payloadSize - written : 0U;
        size_t copySize = remaining > sizeof(programUnit) ? sizeof(programUnit) : remaining;

        memset(programUnit, 0xFF, sizeof(programUnit));

        if (copySize != 0U)
        {
            memcpy(programUnit, source + written, copySize);
        }

        result = Storage_ProgramFlash(sectorAddress + *offset + sizeof(header) + written, programUnit, sizeof(programUnit));

        if (result != STORAGE_RESULT_OK)
        {
            return result;
        }
    }

    commit.magic = STORAGE_RECORD_COMMIT_MAGIC;
    commit.header_crc = Storage_CalculateCrc(&header, sizeof(header));
    commit.data_crc = header.data_crc;
    commit.crc = Storage_CalculateCrc(&commit, offsetof(Storage_RecordCommit, crc));
    result = Storage_ProgramFlash(sectorAddress + *offset + sizeof(header) + payloadPaddedSize, &commit, sizeof(commit));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    if (Storage_ReadRecord(sectorAddress, sectorSize, *offset, NULL, NULL) != STORAGE_RECORD_STATE_VALID)
    {
        return STORAGE_RESULT_ERROR;
    }

    *offset += totalSize;

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_CollectLiveRecords(Storage_LiveRecord *records, size_t *recordCount, uint32_t *totalDataSize)
{
    size_t offset = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);
    size_t count = 0U;
    uint32_t totalSize = 0U;

    while (offset < storage_context.active_sector_size)
    {
        Storage_LiveRecord record;
        size_t recordSize;
        Storage_RecordState state = Storage_ReadRecord(storage_context.active_sector_address, storage_context.active_sector_size, offset, &record, &recordSize);
        size_t index;

        if (state != STORAGE_RECORD_STATE_VALID)
        {
            break;
        }

        for (index = 0U; index < count; index++)
        {
            if (records[index].key == record.key)
            {
                break;
            }
        }

        if (index == count)
        {
            if (count >= STORAGE_MAXIMUM_LIVE_KEYS)
            {
                return STORAGE_RESULT_NO_SPACE;
            }

            records[count].key = record.key;
            records[count].present = false;
            count++;
        }

        if (records[index].present)
        {
            totalSize -= records[index].data_size;
        }

        records[index] = record;

        if (record.present)
        {
            totalSize += record.data_size;
        }

        offset += recordSize;
    }

    *recordCount = count;
    *totalDataSize = totalSize;

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_CompactAndWrite(Storage_KeyTypeDef key, const void *data, uint32_t dataSize)
{
    Storage_LiveRecord records[STORAGE_MAXIMUM_LIVE_KEYS];
    uintptr_t sourceAddress = storage_context.active_sector_address;
    uintptr_t destinationAddress = Storage_GetOtherSectorAddress();
    size_t destinationSize = storage_context.sectors[0].size_bytes;
    size_t recordCount;
    size_t destinationOffset = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);
    uint32_t totalDataSize;
    size_t index;
    Storage_ResultTypeDef result;

    result = Storage_CollectLiveRecords(records, &recordCount, &totalDataSize);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    result = Storage_MapFlashResult(FlashController_EraseSector(&storage_context.flash_controller, destinationAddress == storage_context.sectors[0].start_address ? storage_context.flash_controller.sector_count - 2U : storage_context.flash_controller.sector_count - 1U));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    result = Storage_CreateSectorHeader(destinationAddress, storage_context.active_generation + 1U);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    for (index = 0U; index < recordCount; index++)
    {
        if (records[index].present && (records[index].key != key))
        {
            result = Storage_WriteRecord(destinationAddress, destinationSize, &destinationOffset, records[index].key, (const void *)records[index].data_address, records[index].data_size);

            if (result != STORAGE_RESULT_OK)
            {
                return result;
            }
        }
    }

    if (dataSize != STORAGE_DELETED_DATA_SIZE)
    {
        result = Storage_WriteRecord(destinationAddress, destinationSize, &destinationOffset, key, data, dataSize);

        if (result != STORAGE_RESULT_OK)
        {
            return result;
        }
    }

    result = Storage_ActivateSector(destinationAddress, storage_context.active_generation + 1U);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    storage_context.active_sector_address = destinationAddress;
    storage_context.active_sector_size = destinationSize;
    storage_context.active_write_offset = destinationOffset;
    storage_context.active_generation++;

    result = Storage_MapFlashResult(FlashController_EraseSector(&storage_context.flash_controller, sourceAddress == storage_context.sectors[0].start_address ? storage_context.flash_controller.sector_count - 2U : storage_context.flash_controller.sector_count - 1U));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_ValidateWrite(Storage_KeyTypeDef key, uint32_t dataSize, bool deleting)
{
    Storage_LiveRecord records[STORAGE_MAXIMUM_LIVE_KEYS];
    size_t recordCount;
    uint32_t totalDataSize;
    size_t totalRecordSize = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);
    size_t index;
    Storage_ResultTypeDef result;

    result = Storage_CollectLiveRecords(records, &recordCount, &totalDataSize);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    for (index = 0U; index < recordCount; index++)
    {
        if (records[index].present)
        {
            totalRecordSize += Storage_GetRecordSize(records[index].data_size);
        }

        if (records[index].key == key)
        {
            break;
        }
    }

    if (index < recordCount)
    {
        size_t remainingIndex;

        for (remainingIndex = index + 1U; remainingIndex < recordCount; remainingIndex++)
        {
            if (records[remainingIndex].present)
            {
                totalRecordSize += Storage_GetRecordSize(records[remainingIndex].data_size);
            }
        }
    }

    if (deleting && ((index == recordCount) || !records[index].present))
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    if ((index != recordCount) && records[index].present)
    {
        totalDataSize -= records[index].data_size;
        totalRecordSize -= Storage_GetRecordSize(records[index].data_size);
    }

    if (!deleting)
    {
        totalDataSize += dataSize;
        totalRecordSize += Storage_GetRecordSize(dataSize);
    }

    return totalRecordSize <= storage_context.active_sector_size ? STORAGE_RESULT_OK : STORAGE_RESULT_NO_SPACE;
}

/* -------------------------------------------------------------------------- */
/* Public interface                                                           */
/* -------------------------------------------------------------------------- */

Storage_ResultTypeDef Storage_Init(void)
{
    Storage_SectorHeader headers[2];
    bool active[2];
    size_t selected;
    Storage_ResultTypeDef result;

    memset(&storage_context, 0, sizeof(storage_context));

    result = Storage_MapFlashResult(FlashController_Init(&storage_context.flash_controller));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    if ((storage_context.flash_controller.sector_count < 2U) ||
        (storage_context.flash_controller.program_unit_bytes != sizeof(Storage_RecordHeader)))
    {
        return STORAGE_RESULT_ERROR;
    }

    result = Storage_MapFlashResult(FlashController_GetSectorInformation(&storage_context.flash_controller, storage_context.flash_controller.sector_count - 2U, &storage_context.sectors[0]));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    result = Storage_MapFlashResult(FlashController_GetSectorInformation(&storage_context.flash_controller, storage_context.flash_controller.sector_count - 1U, &storage_context.sectors[1]));

    if ((result != STORAGE_RESULT_OK) ||
        (storage_context.sectors[0].size_bytes != storage_context.sectors[1].size_bytes) ||
        (storage_context.sectors[0].size_bytes < (2U * Storage_GetProgramUnitSize())))
    {
        return STORAGE_RESULT_ERROR;
    }

    active[0] = Storage_IsSectorActive(storage_context.sectors[0].start_address, &headers[0]);
    active[1] = Storage_IsSectorActive(storage_context.sectors[1].start_address, &headers[1]);

    if (!active[0] && !active[1])
    {
        result = Storage_MapFlashResult(FlashController_EraseSector(&storage_context.flash_controller, storage_context.flash_controller.sector_count - 2U));

        if (result != STORAGE_RESULT_OK)
        {
            return result;
        }

        result = Storage_CreateActiveSector(storage_context.sectors[0].start_address, 1U);

        if (result != STORAGE_RESULT_OK)
        {
            return result;
        }

        selected = 0U;
        headers[selected].generation = 1U;
    }
    else if (active[0] && (!active[1] || (headers[0].generation >= headers[1].generation)))
    {
        selected = 0U;
    }
    else
    {
        selected = 1U;
    }

    storage_context.active_sector_address = storage_context.sectors[selected].start_address;
    storage_context.active_sector_size = storage_context.sectors[selected].size_bytes;
    storage_context.active_generation = headers[selected].generation;
    storage_context.active_write_offset = Storage_FindWriteOffset(storage_context.active_sector_address, storage_context.active_sector_size);
    storage_context.initialized = true;

    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_Read(Storage_KeyTypeDef key, void *data, uint32_t dataSize, uint32_t *storedDataSize)
{
    size_t offset;
    Storage_LiveRecord newestRecord;
    bool found = false;

    if (!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    if ((data == NULL) && (dataSize != 0U))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    offset = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);

    while (offset < storage_context.active_sector_size)
    {
        Storage_LiveRecord record;
        size_t recordSize;
        Storage_RecordState state = Storage_ReadRecord(storage_context.active_sector_address, storage_context.active_sector_size, offset, &record, &recordSize);

        if (state != STORAGE_RECORD_STATE_VALID)
        {
            break;
        }

        if (record.key == key)
        {
            newestRecord = record;
            found = true;
        }

        offset += recordSize;
    }

    if (!found || !newestRecord.present)
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    if (storedDataSize != NULL)
    {
        *storedDataSize = newestRecord.data_size;
    }

    if ((data == NULL) || (dataSize < newestRecord.data_size))
    {
        return STORAGE_RESULT_BUFFER_TOO_SMALL;
    }

    return Storage_ReadFlash(newestRecord.data_address, data, newestRecord.data_size);
}

Storage_ResultTypeDef Storage_Write(Storage_KeyTypeDef key, const void *data, uint32_t dataSize)
{
    Storage_ResultTypeDef result;
    size_t recordSize;

    if (!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    if ((data == NULL) || (dataSize == 0U) || (dataSize > STORAGE_MAXIMUM_ENTRY_SIZE_BYTES))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    result = Storage_ValidateWrite(key, dataSize, false);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    recordSize = Storage_GetRecordSize(dataSize);

    if (recordSize <= (storage_context.active_sector_size - storage_context.active_write_offset))
    {
        result = Storage_WriteRecord(storage_context.active_sector_address, storage_context.active_sector_size, &storage_context.active_write_offset, key, data, dataSize);
    }
    else
    {
        result = Storage_CompactAndWrite(key, data, dataSize);
    }

    return result;
}

Storage_ResultTypeDef Storage_Delete(Storage_KeyTypeDef key)
{
    Storage_ResultTypeDef result;
    size_t recordSize;

    if (!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    result = Storage_ValidateWrite(key, 0U, true);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    recordSize = Storage_GetRecordSize(STORAGE_DELETED_DATA_SIZE);

    if (recordSize <= (storage_context.active_sector_size - storage_context.active_write_offset))
    {
        result = Storage_WriteRecord(storage_context.active_sector_address, storage_context.active_sector_size, &storage_context.active_write_offset, key, NULL, STORAGE_DELETED_DATA_SIZE);
    }
    else
    {
        result = Storage_CompactAndWrite(key, NULL, STORAGE_DELETED_DATA_SIZE);
    }

    return result;
}

Storage_ResultTypeDef Storage_EraseAll(void)
{
    Storage_ResultTypeDef result;

    if (!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    result = Storage_MapFlashResult(FlashController_EraseSector(&storage_context.flash_controller, storage_context.flash_controller.sector_count - 2U));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    result = Storage_MapFlashResult(FlashController_EraseSector(&storage_context.flash_controller, storage_context.flash_controller.sector_count - 1U));

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    result = Storage_CreateActiveSector(storage_context.sectors[0].start_address, 1U);

    if (result != STORAGE_RESULT_OK)
    {
        return result;
    }

    storage_context.active_sector_address = storage_context.sectors[0].start_address;
    storage_context.active_sector_size = storage_context.sectors[0].size_bytes;
    storage_context.active_write_offset = sizeof(Storage_SectorHeader) + sizeof(Storage_SectorActivation);
    storage_context.active_generation = 1U;

    return STORAGE_RESULT_OK;
}