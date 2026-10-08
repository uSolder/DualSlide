/**
 * @file storage.c
 * @brief Windows implementation of the persistent key/value storage contract.
 *
 * The simulator keeps records in memory, so stored values last for the
 * lifetime of the process rather than across runs.
 */

#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define WINDOWS_STORAGE_MAXIMUM_KEYS          (128U)
#define WINDOWS_STORAGE_MAXIMUM_ENTRY_BYTES   (4096U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    Storage_KeyTypeDef Key;
    uint8_t *Data;
    uint32_t DataSize;
    bool Present;
} Windows_StorageRecordTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Windows_StorageRecordTypeDef Windows_StorageRecords[WINDOWS_STORAGE_MAXIMUM_KEYS];

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static Windows_StorageRecordTypeDef *Windows_StorageFind(Storage_KeyTypeDef Key)
{
    for(uint32_t Index = 0U; Index < WINDOWS_STORAGE_MAXIMUM_KEYS; Index++)
    {
        if(Windows_StorageRecords[Index].Present && (Windows_StorageRecords[Index].Key == Key))
        {
            return &Windows_StorageRecords[Index];
        }
    }

    return NULL;
}

static Windows_StorageRecordTypeDef *Windows_StorageAllocate(void)
{
    for(uint32_t Index = 0U; Index < WINDOWS_STORAGE_MAXIMUM_KEYS; Index++)
    {
        if(!Windows_StorageRecords[Index].Present)
        {
            return &Windows_StorageRecords[Index];
        }
    }

    return NULL;
}

static void Windows_StorageRelease(Windows_StorageRecordTypeDef *Record)
{
    free(Record->Data);
    Record->Data = NULL;
    Record->DataSize = 0U;
    Record->Present = false;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

Storage_ResultTypeDef Storage_Init(void)
{
    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_Read(Storage_KeyTypeDef Key, void *Data, uint32_t DataSize, uint32_t *StoredDataSize)
{
    const Windows_StorageRecordTypeDef *Record = Windows_StorageFind(Key);

    if((Data == NULL) && (DataSize != 0U))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    if(Record == NULL)
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    if(StoredDataSize != NULL)
    {
        *StoredDataSize = Record->DataSize;
    }

    if(DataSize < Record->DataSize)
    {
        return STORAGE_RESULT_BUFFER_TOO_SMALL;
    }

    if(Record->DataSize != 0U)
    {
        memcpy(Data, Record->Data, Record->DataSize);
    }

    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_Write(Storage_KeyTypeDef Key, const void *Data, uint32_t DataSize)
{
    Windows_StorageRecordTypeDef *Record;
    uint8_t *Copy = NULL;

    if(((Data == NULL) && (DataSize != 0U)) || (DataSize > WINDOWS_STORAGE_MAXIMUM_ENTRY_BYTES))
    {
        return STORAGE_RESULT_INVALID_ARGUMENT;
    }

    if(DataSize != 0U)
    {
        Copy = malloc(DataSize);

        if(Copy == NULL)
        {
            return STORAGE_RESULT_NO_SPACE;
        }

        memcpy(Copy, Data, DataSize);
    }

    Record = Windows_StorageFind(Key);

    if(Record != NULL)
    {
        Windows_StorageRelease(Record);
    }
    else
    {
        Record = Windows_StorageAllocate();

        if(Record == NULL)
        {
            free(Copy);
            return STORAGE_RESULT_NO_SPACE;
        }
    }

    Record->Key = Key;
    Record->Data = Copy;
    Record->DataSize = DataSize;
    Record->Present = true;

    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_Delete(Storage_KeyTypeDef Key)
{
    Windows_StorageRecordTypeDef *Record = Windows_StorageFind(Key);

    if(Record == NULL)
    {
        return STORAGE_RESULT_NOT_FOUND;
    }

    Windows_StorageRelease(Record);

    return STORAGE_RESULT_OK;
}

Storage_ResultTypeDef Storage_EraseAll(void)
{
    for(uint32_t Index = 0U; Index < WINDOWS_STORAGE_MAXIMUM_KEYS; Index++)
    {
        if(Windows_StorageRecords[Index].Present)
        {
            Windows_StorageRelease(&Windows_StorageRecords[Index]);
        }
    }

    return STORAGE_RESULT_OK;
}
