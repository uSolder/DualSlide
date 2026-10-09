/**
 * @file save.c
 * @brief Named saves on top of the key/value storage.
 *
 * A save's name becomes its storage key, the four letters packed
 * most-significant first ("PONG" is 0x504F4E47), so names match the keys
 * games used before this module existed. A save only loads when its stored
 * size matches the structure asking for it.
 */

#include "save.h"

#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Saves up to this size are compared before writing; larger ones are always written. */
#define SAVE_COMPARE_BYTES                  (256U)

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* Up to four characters, most significant first; missing characters are spaces. */
static Storage_KeyTypeDef Save_Key(const char *Name)
{
    Storage_KeyTypeDef Key = 0U;
    bool Ended = (Name == NULL);

    for(uint32_t Index = 0U; Index < 4U; Index++)
    {
        uint8_t Character = (uint8_t)' ';

        if(!Ended && (Name[Index] != '\0'))
        {
            Character = (uint8_t)Name[Index];
        }
        else
        {
            Ended = true;
        }

        Key = (Key << 8U) | Character;
    }

    return Key;
}

/* The size of the save under Key, or 0 when there is none. */
static uint32_t Save_StoredSize(Storage_KeyTypeDef Key)
{
    uint32_t StoredSize = 0U;
    const Storage_ResultTypeDef Result = Storage_Read(Key, NULL, 0U, &StoredSize);

    return ((Result == STORAGE_RESULT_OK) || (Result == STORAGE_RESULT_BUFFER_TOO_SMALL)) ? StoredSize : 0U;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Save_Load(const char *Name, void *Data, uint32_t Size)
{
    const Storage_KeyTypeDef Key = Save_Key(Name);

    if((Data == NULL) || (Size == 0U) || (Save_StoredSize(Key) != Size))
    {
        return false;
    }

    return Storage_Read(Key, Data, Size, NULL) == STORAGE_RESULT_OK;
}

bool Save_Store(const char *Name, const void *Data, uint32_t Size)
{
    const Storage_KeyTypeDef Key = Save_Key(Name);

    if((Data == NULL) || (Size == 0U))
    {
        return false;
    }

    /* Flash wears with every write, so an unchanged save is not written again. */
    if((Size <= SAVE_COMPARE_BYTES) && (Save_StoredSize(Key) == Size))
    {
        uint8_t Stored[SAVE_COMPARE_BYTES];

        if((Storage_Read(Key, Stored, Size, NULL) == STORAGE_RESULT_OK) && (memcmp(Stored, Data, Size) == 0))
        {
            return true;
        }
    }

    return Storage_Write(Key, Data, Size) == STORAGE_RESULT_OK;
}

bool Save_Erase(const char *Name)
{
    const Storage_ResultTypeDef Result = Storage_Delete(Save_Key(Name));

    return (Result == STORAGE_RESULT_OK) || (Result == STORAGE_RESULT_NOT_FOUND);
}
