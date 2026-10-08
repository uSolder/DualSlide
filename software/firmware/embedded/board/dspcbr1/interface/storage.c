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
    uint32_t Magic;
    uint32_t Generation;
    uint32_t FormatVersion;
    uint32_t CRC;
} Storage_SectorHeaderTypeDef;

typedef struct
{
    uint32_t Magic;
    uint32_t Generation;
    uint32_t SectorHeaderCRC;
    uint32_t CRC;
} Storage_SectorActivationTypeDef;

typedef struct
{
    uint32_t Magic;
    uint32_t Key;
    uint32_t DataSize;
    uint32_t DataCRC;
} Storage_RecordHeaderTypeDef;

typedef struct
{
    uint32_t Magic;
    uint32_t HeaderCRC;
    uint32_t DataCRC;
    uint32_t CRC;
} Storage_RecordCommitTypeDef;

typedef struct
{
    Storage_KeyTypeDef Key;
    uintptr_t DataAddress;
    uint32_t DataSize;
    bool Present;
} Storage_LiveRecordTypeDef;

typedef struct
{
    FlashController_HandleTypeDef FlashController;
    FlashController_SectorInformationTypeDef Sectors[2];
    uintptr_t ActiveSectorAddress;
    size_t ActiveSectorSize;
    size_t ActiveWriteOffset;
    uint32_t ActiveGeneration;
    bool Initialized;
} Storage_ContextTypeDef;

typedef enum
{
    STORAGE_RECORD_STATE_EMPTY,
    STORAGE_RECORD_STATE_VALID,
    STORAGE_RECORD_STATE_INVALID
} Storage_RecordStateTypeDef;

static Storage_ContextTypeDef Storage_Context;

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */

static size_t Storage_AlignUp(size_t Value, size_t Alignment)
{
    return (Value + Alignment - 1U) & ~(Alignment - 1U);
}

static uint32_t Storage_CalculateCrc(const void *Data, size_t Size)
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

static Storage_ResultTypeDef Storage_MapFlashResult(FlashController_ResultTypeDef Result)
{
    return Result == FLASH_CONTROLLER_RESULT_OK ? STORAGE_RESULT_OK : STORAGE_RESULT_ERROR;
}

static bool Storage_IsInitialized(void)
{
    return Storage_Context.Initialized;
}

static size_t Storage_GetProgramUnitSize(void)
{
    return Storage_Context.FlashController.ProgramUnitBytes;
}

static uintptr_t Storage_GetOtherSectorAddress(void)
{
    return Storage_Context.ActiveSectorAddress == Storage_Context.Sectors[0].StartAddress ? Storage_Context.Sectors[1].StartAddress : Storage_Context.Sectors[0].StartAddress;
}

static size_t Storage_GetRecordSize(uint32_t DataSize)
{
    size_t PayloadSize = DataSize == STORAGE_DELETED_DATA_SIZE ? 0U : DataSize;

    return sizeof(Storage_RecordHeaderTypeDef) + Storage_AlignUp(PayloadSize, Storage_GetProgramUnitSize()) + sizeof(Storage_RecordCommitTypeDef);
}

static bool Storage_IsRecordDataSizeValid(uint32_t DataSize)
{
    return (DataSize == STORAGE_DELETED_DATA_SIZE) || ((DataSize != 0U) && (DataSize <= STORAGE_MAXIMUM_ENTRY_SIZE_BYTES));
}

static Storage_ResultTypeDef Storage_ReadFlash(uintptr_t Address, void *Data, size_t Size)
{
    return Storage_MapFlashResult(FlashController_Read(&Storage_Context.FlashController, Address, Data, Size));
}

static Storage_ResultTypeDef Storage_ProgramFlash(uintptr_t Address, const void *Data, size_t Size)
{
    FlashController_ResultTypeDef FlashResult = FlashController_Program(&Storage_Context.FlashController, Address, Data, Size);
    return Storage_MapFlashResult(FlashResult);
}

static uint32_t Storage_CalculateFlashCrc(uintptr_t Address, uint32_t Size)
{
    uint8_t Buffer[16];
    uint32_t CRC = 0xFFFFFFFFUL;
    uint32_t Remaining = Size;

    while(Remaining != 0U)
    {
        size_t ChunkSize = Remaining > sizeof(Buffer) ? sizeof(Buffer) : Remaining;
        const uint8_t *Bytes = Buffer;
        size_t Index;
        uint8_t Bit;

        if(Storage_ReadFlash(Address, Buffer, ChunkSize) != STORAGE_RESULT_OK)
        {
            return 0U;
        }

        for(Index = 0U; Index < ChunkSize; Index++)
        {
            CRC ^= Bytes[Index];

            for(Bit = 0U; Bit < 8U; Bit++)
            {
                CRC = (CRC >> 1U) ^ ((CRC & 1U) != 0U ? 0xEDB88320UL : 0U);
            }
        }

        Address += ChunkSize;
        Remaining -= (uint32_t)ChunkSize;
    }

    return ~CRC;
}

static bool Storage_IsSectorHeaderValid(const Storage_SectorHeaderTypeDef *Header)
{
    return (Header->Magic == STORAGE_SECTOR_MAGIC) && (Header->FormatVersion == STORAGE_FORMAT_VERSION) && (Header->CRC == Storage_CalculateCrc(Header, offsetof(Storage_SectorHeaderTypeDef, CRC)));
}

static bool Storage_IsSectorActivationValid(const Storage_SectorActivationTypeDef *Activation, const Storage_SectorHeaderTypeDef *Header)
{
    return (Activation->Magic == STORAGE_SECTOR_ACTIVATION_MAGIC) && (Activation->Generation == Header->Generation) && (Activation->SectorHeaderCRC == Header->CRC) && (Activation->CRC == Storage_CalculateCrc(Activation, offsetof(Storage_SectorActivationTypeDef, CRC)));
}

static bool Storage_IsSectorActive(uintptr_t SectorAddress, Storage_SectorHeaderTypeDef *Header)
{
    Storage_SectorActivationTypeDef Activation;

    if(Storage_ReadFlash(SectorAddress, Header, sizeof(*Header)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    if(!Storage_IsSectorHeaderValid(Header))
    {
        return false;
    }

    if(Storage_ReadFlash(SectorAddress + sizeof(*Header), &Activation, sizeof(Activation)) != STORAGE_RESULT_OK)
    {
        return false;
    }

    return Storage_IsSectorActivationValid(&Activation, Header);
}

static Storage_ResultTypeDef Storage_CreateSectorHeader(uintptr_t SectorAddress, uint32_t Generation)
{
    Storage_SectorHeaderTypeDef Header;

    Header.Magic = STORAGE_SECTOR_MAGIC;
    Header.Generation = Generation;
    Header.FormatVersion = STORAGE_FORMAT_VERSION;
    Header.CRC = Storage_CalculateCrc(&Header, offsetof(Storage_SectorHeaderTypeDef, CRC));

    return Storage_ProgramFlash(SectorAddress, &Header, sizeof(Header));
}

static Storage_ResultTypeDef Storage_ActivateSector(uintptr_t SectorAddress, uint32_t Generation)
{
    Storage_SectorHeaderTypeDef Header;
    Storage_SectorActivationTypeDef Activation;
    Storage_ResultTypeDef Result;

    Result = Storage_ReadFlash(SectorAddress, &Header, sizeof(Header));

    if((Result != STORAGE_RESULT_OK) || !Storage_IsSectorHeaderValid(&Header) ||
       (Header.Generation != Generation))
    {
        return STORAGE_RESULT_ERROR;
    }

    Activation.Magic = STORAGE_SECTOR_ACTIVATION_MAGIC;
    Activation.Generation = Generation;
    Activation.SectorHeaderCRC = Header.CRC;
    Activation.CRC = Storage_CalculateCrc(&Activation, offsetof(Storage_SectorActivationTypeDef, CRC));

    return Storage_ProgramFlash(SectorAddress + sizeof(Header), &Activation, sizeof(Activation));
}

static Storage_ResultTypeDef Storage_CreateActiveSector(uintptr_t SectorAddress, uint32_t Generation)
{
    Storage_ResultTypeDef Result = Storage_CreateSectorHeader(SectorAddress, Generation);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    return Storage_ActivateSector(SectorAddress, Generation);
}

static Storage_RecordStateTypeDef Storage_ReadRecord(uintptr_t SectorAddress, size_t SectorSize, size_t Offset, Storage_LiveRecordTypeDef *Record, size_t *RecordSize)
{
    Storage_RecordHeaderTypeDef Header;
    Storage_RecordCommitTypeDef Commit;
    size_t PayloadSize;
    size_t TotalSize;

    if((Offset + sizeof(Header)) > SectorSize)
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if(Storage_ReadFlash(SectorAddress + Offset, &Header, sizeof(Header)) != STORAGE_RESULT_OK)
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if((Header.Magic == STORAGE_ERASED_WORD) &&
       (Header.Key == STORAGE_ERASED_WORD) &&
       (Header.DataSize == STORAGE_ERASED_WORD) &&
       (Header.DataCRC == STORAGE_ERASED_WORD))
    {
        return STORAGE_RECORD_STATE_EMPTY;
    }

    if((Header.Magic != STORAGE_RECORD_MAGIC) || !Storage_IsRecordDataSizeValid(Header.DataSize))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    PayloadSize = Header.DataSize == STORAGE_DELETED_DATA_SIZE ? 0U : Header.DataSize;
    TotalSize = Storage_GetRecordSize(Header.DataSize);

    if((TotalSize > (SectorSize - Offset)) ||
       (Storage_ReadFlash(SectorAddress + Offset + sizeof(Header) + Storage_AlignUp(PayloadSize, Storage_GetProgramUnitSize()), &Commit, sizeof(Commit)) != STORAGE_RESULT_OK))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if((Commit.Magic != STORAGE_RECORD_COMMIT_MAGIC) ||
       (Commit.HeaderCRC != Storage_CalculateCrc(&Header, sizeof(Header))) ||
       (Commit.DataCRC != Header.DataCRC) ||
       (Commit.CRC != Storage_CalculateCrc(&Commit, offsetof(Storage_RecordCommitTypeDef, CRC))))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if((PayloadSize != 0U) &&
       (Storage_CalculateFlashCrc(SectorAddress + Offset + sizeof(Header), (uint32_t)PayloadSize) != Header.DataCRC))
    {
        return STORAGE_RECORD_STATE_INVALID;
    }

    if(Record != NULL)
    {
        Record->Key = Header.Key;
        Record->DataAddress = SectorAddress + Offset + sizeof(Header);
        Record->DataSize = Header.DataSize;
        Record->Present = Header.DataSize != STORAGE_DELETED_DATA_SIZE;
    }

    if(RecordSize != NULL)
    {
        *RecordSize = TotalSize;
    }

    return STORAGE_RECORD_STATE_VALID;
}

static size_t Storage_FindWriteOffset(uintptr_t SectorAddress, size_t SectorSize)
{
    size_t Offset = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);

    while(Offset < SectorSize)
    {
        size_t RecordSize;
        Storage_RecordStateTypeDef State = Storage_ReadRecord(SectorAddress, SectorSize, Offset, NULL, &RecordSize);

        if(State == STORAGE_RECORD_STATE_EMPTY)
        {
            return Offset;
        }

        if(State != STORAGE_RECORD_STATE_VALID)
        {
            return SectorSize;
        }

        Offset += RecordSize;
    }

    return SectorSize;
}

static Storage_ResultTypeDef Storage_WriteRecord(uintptr_t SectorAddress, size_t SectorSize, size_t *Offset, Storage_KeyTypeDef Key, const void *Data, uint32_t DataSize)
{
    Storage_RecordHeaderTypeDef Header;
    Storage_RecordCommitTypeDef Commit;
    uint8_t ProgramUnit[16];
    const uint8_t *Source = Data;
    size_t PayloadSize = DataSize == STORAGE_DELETED_DATA_SIZE ? 0U : DataSize;
    size_t PayloadPaddedSize = Storage_AlignUp(PayloadSize, Storage_GetProgramUnitSize());
    size_t TotalSize = Storage_GetRecordSize(DataSize);
    size_t Written;
    Storage_ResultTypeDef Result;

    if((TotalSize > (SectorSize - *Offset)) ||
       ((DataSize != STORAGE_DELETED_DATA_SIZE) && ((Data == NULL) || (DataSize == 0U))))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    Header.Magic = STORAGE_RECORD_MAGIC;
    Header.Key = Key;
    Header.DataSize = DataSize;
    Header.DataCRC = PayloadSize == 0U ? Storage_CalculateCrc(NULL, 0U) : Storage_CalculateCrc(Data, PayloadSize);
    Result = Storage_ProgramFlash(SectorAddress + *Offset, &Header, sizeof(Header));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    for(Written = 0U; Written < PayloadPaddedSize; Written += sizeof(ProgramUnit))
    {
        size_t Remaining = PayloadSize > Written ? PayloadSize - Written : 0U;
        size_t CopySize = Remaining > sizeof(ProgramUnit) ? sizeof(ProgramUnit) : Remaining;

        memset(ProgramUnit, 0xFF, sizeof(ProgramUnit));

        if(CopySize != 0U)
        {
            memcpy(ProgramUnit, Source + Written, CopySize);
        }

        Result = Storage_ProgramFlash(SectorAddress + *Offset + sizeof(Header) + Written, ProgramUnit, sizeof(ProgramUnit));

        if(Result != STORAGE_RESULT_OK)
        {
            return Result;
        }
    }

    Commit.Magic = STORAGE_RECORD_COMMIT_MAGIC;
    Commit.HeaderCRC = Storage_CalculateCrc(&Header, sizeof(Header));
    Commit.DataCRC = Header.DataCRC;
    Commit.CRC = Storage_CalculateCrc(&Commit, offsetof(Storage_RecordCommitTypeDef, CRC));
    Result = Storage_ProgramFlash(SectorAddress + *Offset + sizeof(Header) + PayloadPaddedSize, &Commit, sizeof(Commit));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    if(Storage_ReadRecord(SectorAddress, SectorSize, *Offset, NULL, NULL) != STORAGE_RECORD_STATE_VALID)
    {
        return STORAGE_RESULT_ERROR;
    }

    *Offset += TotalSize;

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_CollectLiveRecords(Storage_LiveRecordTypeDef *Records, size_t *RecordCount, uint32_t *TotalDataSize)
{
    size_t Offset = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);
    size_t Count = 0U;
    uint32_t TotalSize = 0U;

    while(Offset < Storage_Context.ActiveSectorSize)
    {
        Storage_LiveRecordTypeDef Record;
        size_t RecordSize;
        Storage_RecordStateTypeDef State = Storage_ReadRecord(Storage_Context.ActiveSectorAddress, Storage_Context.ActiveSectorSize, Offset, &Record, &RecordSize);
        size_t Index;

        if(State != STORAGE_RECORD_STATE_VALID)
        {
            break;
        }

        for(Index = 0U; Index < Count; Index++)
        {
            if(Records[Index].Key == Record.Key)
            {
                break;
            }
        }

        if(Index == Count)
        {
            if(Count >= STORAGE_MAXIMUM_LIVE_KEYS)
            {
                return STORAGE_RESULT_NO_SPACE;
            }

            Records[Count].Key = Record.Key;
            Records[Count].Present = false;
            Count++;
        }

        if(Records[Index].Present)
        {
            TotalSize -= Records[Index].DataSize;
        }

        Records[Index] = Record;

        if(Record.Present)
        {
            TotalSize += Record.DataSize;
        }

        Offset += RecordSize;
    }

    *RecordCount = Count;
    *TotalDataSize = TotalSize;

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_CompactAndWrite(Storage_KeyTypeDef Key, const void *Data, uint32_t DataSize)
{
    Storage_LiveRecordTypeDef Records[STORAGE_MAXIMUM_LIVE_KEYS];
    uintptr_t SourceAddress = Storage_Context.ActiveSectorAddress;
    uintptr_t DestinationAddress = Storage_GetOtherSectorAddress();
    size_t DestinationSize = Storage_Context.Sectors[0].SizeBytes;
    size_t RecordCount;
    size_t DestinationOffset = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);
    uint32_t TotalDataSize;
    size_t Index;
    Storage_ResultTypeDef Result;

    Result = Storage_CollectLiveRecords(Records, &RecordCount, &TotalDataSize);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Result = Storage_MapFlashResult(FlashController_EraseSector(&Storage_Context.FlashController, DestinationAddress == Storage_Context.Sectors[0].StartAddress ? Storage_Context.FlashController.SectorCount - 2U : Storage_Context.FlashController.SectorCount - 1U));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Result = Storage_CreateSectorHeader(DestinationAddress, Storage_Context.ActiveGeneration + 1U);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    for(Index = 0U; Index < RecordCount; Index++)
    {
        if(Records[Index].Present && (Records[Index].Key != Key))
        {
            Result = Storage_WriteRecord(DestinationAddress, DestinationSize, &DestinationOffset, Records[Index].Key, (const void *)Records[Index].DataAddress, Records[Index].DataSize);

            if(Result != STORAGE_RESULT_OK)
            {
                return Result;
            }
        }
    }

    if(DataSize != STORAGE_DELETED_DATA_SIZE)
    {
        Result = Storage_WriteRecord(DestinationAddress, DestinationSize, &DestinationOffset, Key, Data, DataSize);

        if(Result != STORAGE_RESULT_OK)
        {
            return Result;
        }
    }

    Result = Storage_ActivateSector(DestinationAddress, Storage_Context.ActiveGeneration + 1U);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Storage_Context.ActiveSectorAddress = DestinationAddress;
    Storage_Context.ActiveSectorSize = DestinationSize;
    Storage_Context.ActiveWriteOffset = DestinationOffset;
    Storage_Context.ActiveGeneration++;

    Result = Storage_MapFlashResult(FlashController_EraseSector(&Storage_Context.FlashController, SourceAddress == Storage_Context.Sectors[0].StartAddress ? Storage_Context.FlashController.SectorCount - 2U : Storage_Context.FlashController.SectorCount - 1U));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    return STORAGE_RESULT_OK;
}

static Storage_ResultTypeDef Storage_ValidateWrite(Storage_KeyTypeDef Key, uint32_t DataSize, bool Deleting)
{
    Storage_LiveRecordTypeDef Records[STORAGE_MAXIMUM_LIVE_KEYS];
    size_t RecordCount;
    uint32_t TotalDataSize;
    size_t TotalRecordSize = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);
    size_t Index;
    Storage_ResultTypeDef Result;

    Result = Storage_CollectLiveRecords(Records, &RecordCount, &TotalDataSize);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    for(Index = 0U; Index < RecordCount; Index++)
    {
        if(Records[Index].Present)
        {
            TotalRecordSize += Storage_GetRecordSize(Records[Index].DataSize);
        }

        if(Records[Index].Key == Key)
        {
            break;
        }
    }

    if(Index < RecordCount)
    {
        size_t RemainingIndex;

        for(RemainingIndex = Index + 1U; RemainingIndex < RecordCount; RemainingIndex++)
        {
            if(Records[RemainingIndex].Present)
            {
                TotalRecordSize += Storage_GetRecordSize(Records[RemainingIndex].DataSize);
            }
        }
    }

    if(Deleting && ((Index == RecordCount) || !Records[Index].Present))
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    if((Index != RecordCount) && Records[Index].Present)
    {
        TotalDataSize -= Records[Index].DataSize;
        TotalRecordSize -= Storage_GetRecordSize(Records[Index].DataSize);
    }

    if(!Deleting)
    {
        TotalDataSize += DataSize;
        TotalRecordSize += Storage_GetRecordSize(DataSize);
    }

    return TotalRecordSize <= Storage_Context.ActiveSectorSize ? STORAGE_RESULT_OK : STORAGE_RESULT_NO_SPACE;
}

/* -------------------------------------------------------------------------- */
/* Public interface                                                           */
/* -------------------------------------------------------------------------- */

Storage_ResultTypeDef Storage_Init(void)
{
    Storage_SectorHeaderTypeDef Headers[2];
    bool Active[2];
    size_t Selected;
    Storage_ResultTypeDef Result;

    memset(&Storage_Context, 0, sizeof(Storage_Context));

    Result = Storage_MapFlashResult(FlashController_Init(&Storage_Context.FlashController));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    if((Storage_Context.FlashController.SectorCount < 2U) ||
       (Storage_Context.FlashController.ProgramUnitBytes != sizeof(Storage_RecordHeaderTypeDef)))
    {
        return STORAGE_RESULT_ERROR;
    }

    Result = Storage_MapFlashResult(FlashController_GetSectorInformation(&Storage_Context.FlashController, Storage_Context.FlashController.SectorCount - 2U, &Storage_Context.Sectors[0]));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Result = Storage_MapFlashResult(FlashController_GetSectorInformation(&Storage_Context.FlashController, Storage_Context.FlashController.SectorCount - 1U, &Storage_Context.Sectors[1]));

    if((Result != STORAGE_RESULT_OK) ||
       (Storage_Context.Sectors[0].SizeBytes != Storage_Context.Sectors[1].SizeBytes) ||
       (Storage_Context.Sectors[0].SizeBytes < (2U * Storage_GetProgramUnitSize())))
    {
        return STORAGE_RESULT_ERROR;
    }

    Active[0] = Storage_IsSectorActive(Storage_Context.Sectors[0].StartAddress, &Headers[0]);
    Active[1] = Storage_IsSectorActive(Storage_Context.Sectors[1].StartAddress, &Headers[1]);

    if(!Active[0] && !Active[1])
    {
        Result = Storage_MapFlashResult(FlashController_EraseSector(&Storage_Context.FlashController, Storage_Context.FlashController.SectorCount - 2U));

        if(Result != STORAGE_RESULT_OK)
        {
            return Result;
        }

        Result = Storage_CreateActiveSector(Storage_Context.Sectors[0].StartAddress, 1U);

        if(Result != STORAGE_RESULT_OK)
        {
            return Result;
        }

        Selected = 0U;
        Headers[Selected].Generation = 1U;
    }
    else if(Active[0] && (!Active[1] || (Headers[0].Generation >= Headers[1].Generation)))
    {
        Selected = 0U;
    }
    else
    {
        Selected = 1U;
    }

    Storage_Context.ActiveSectorAddress = Storage_Context.Sectors[Selected].StartAddress;
    Storage_Context.ActiveSectorSize = Storage_Context.Sectors[Selected].SizeBytes;
    Storage_Context.ActiveGeneration = Headers[Selected].Generation;
    Storage_Context.ActiveWriteOffset = Storage_FindWriteOffset(Storage_Context.ActiveSectorAddress, Storage_Context.ActiveSectorSize);
    Storage_Context.Initialized = true;

    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_Read(Storage_KeyTypeDef Key, void *Data, uint32_t DataSize, uint32_t *StoredDataSize)
{
    size_t Offset;
    Storage_LiveRecordTypeDef NewestRecord;
    bool Found = false;

    if(!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    if((Data == NULL) && (DataSize != 0U))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    Offset = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);

    while(Offset < Storage_Context.ActiveSectorSize)
    {
        Storage_LiveRecordTypeDef Record;
        size_t RecordSize;
        Storage_RecordStateTypeDef State = Storage_ReadRecord(Storage_Context.ActiveSectorAddress, Storage_Context.ActiveSectorSize, Offset, &Record, &RecordSize);

        if(State != STORAGE_RECORD_STATE_VALID)
        {
            break;
        }

        if(Record.Key == Key)
        {
            NewestRecord = Record;
            Found = true;
        }

        Offset += RecordSize;
    }

    if(!Found || !NewestRecord.Present)
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    if(StoredDataSize != NULL)
    {
        *StoredDataSize = NewestRecord.DataSize;
    }

    if((Data == NULL) || (DataSize < NewestRecord.DataSize))
    {
        return STORAGE_RESULT_BUFFER_TOO_SMALL;
    }

    return Storage_ReadFlash(NewestRecord.DataAddress, Data, NewestRecord.DataSize);
}

Storage_ResultTypeDef Storage_Write(Storage_KeyTypeDef Key, const void *Data, uint32_t DataSize)
{
    Storage_ResultTypeDef Result;
    size_t RecordSize;

    if(!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    if((Data == NULL) || (DataSize == 0U) || (DataSize > STORAGE_MAXIMUM_ENTRY_SIZE_BYTES))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    Result = Storage_ValidateWrite(Key, DataSize, false);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    RecordSize = Storage_GetRecordSize(DataSize);

    if(RecordSize <= (Storage_Context.ActiveSectorSize - Storage_Context.ActiveWriteOffset))
    {
        Result = Storage_WriteRecord(Storage_Context.ActiveSectorAddress, Storage_Context.ActiveSectorSize, &Storage_Context.ActiveWriteOffset, Key, Data, DataSize);
    }
    else
    {
        Result = Storage_CompactAndWrite(Key, Data, DataSize);
    }

    return Result;
}

Storage_ResultTypeDef Storage_Delete(Storage_KeyTypeDef Key)
{
    Storage_ResultTypeDef Result;
    size_t RecordSize;

    if(!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    Result = Storage_ValidateWrite(Key, 0U, true);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    RecordSize = Storage_GetRecordSize(STORAGE_DELETED_DATA_SIZE);

    if(RecordSize <= (Storage_Context.ActiveSectorSize - Storage_Context.ActiveWriteOffset))
    {
        Result = Storage_WriteRecord(Storage_Context.ActiveSectorAddress, Storage_Context.ActiveSectorSize, &Storage_Context.ActiveWriteOffset, Key, NULL, STORAGE_DELETED_DATA_SIZE);
    }
    else
    {
        Result = Storage_CompactAndWrite(Key, NULL, STORAGE_DELETED_DATA_SIZE);
    }

    return Result;
}

Storage_ResultTypeDef Storage_EraseAll(void)
{
    Storage_ResultTypeDef Result;

    if(!Storage_IsInitialized())
    {
        return STORAGE_RESULT_ERROR;
    }

    Result = Storage_MapFlashResult(FlashController_EraseSector(&Storage_Context.FlashController, Storage_Context.FlashController.SectorCount - 2U));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Result = Storage_MapFlashResult(FlashController_EraseSector(&Storage_Context.FlashController, Storage_Context.FlashController.SectorCount - 1U));

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Result = Storage_CreateActiveSector(Storage_Context.Sectors[0].StartAddress, 1U);

    if(Result != STORAGE_RESULT_OK)
    {
        return Result;
    }

    Storage_Context.ActiveSectorAddress = Storage_Context.Sectors[0].StartAddress;
    Storage_Context.ActiveSectorSize = Storage_Context.Sectors[0].SizeBytes;
    Storage_Context.ActiveWriteOffset = sizeof(Storage_SectorHeaderTypeDef) + sizeof(Storage_SectorActivationTypeDef);
    Storage_Context.ActiveGeneration = 1U;

    return STORAGE_RESULT_OK;
}
