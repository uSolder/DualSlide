/**
 * @file save.c
 * @brief Named saves on top of the key/value storage.
 *
 * A save's name becomes its storage key, the four letters packed
 * most-significant first ("PONG" is 0x504F4E47), so names match the keys
 * games used before this module existed. A save only loads when its stored
 * size matches the structure asking for it.
 *
 * A save in a namespace (a game's identifier) gets a key hashed from the
 * namespace and the name instead, with the top bit set so it never matches
 * a plain four-letter key.
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

/* A key for Name within Namespace: CRC-32 of "Namespace/NAME", top bit set. */
static Storage_KeyTypeDef Save_KeyIn(const char *Namespace, const char *Name)
{
    const Storage_KeyTypeDef PlainKey = Save_Key(Name);
    uint32_t CRC = 0xFFFFFFFFUL;
    uint32_t Index;
    uint8_t Bit;

    if(Namespace == NULL)
    {
        return PlainKey;
    }

    for(Index = 0U; Namespace[Index] != '\0'; Index++)
    {
        CRC ^= (uint8_t)Namespace[Index];

        for(Bit = 0U; Bit < 8U; Bit++)
        {
            CRC = (CRC >> 1U) ^ ((CRC & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }

    for(Index = 0U; Index < 5U; Index++)
    {
        /* The separator, then the four characters of the plain key. */
        CRC ^= (Index == 0U) ? (uint8_t)'/' : (uint8_t)(PlainKey >> (8U * (4U - Index)));

        for(Bit = 0U; Bit < 8U; Bit++)
        {
            CRC = (CRC >> 1U) ^ ((CRC & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }

    /* All ones reads as erased storage, so avoid it. */
    CRC = ~CRC | 0x80000000UL;

    return (CRC == 0xFFFFFFFFUL) ? 0xFFFFFFFEUL : CRC;
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
    return Save_LoadIn(NULL, Name, Data, Size);
}

bool Save_Store(const char *Name, const void *Data, uint32_t Size)
{
    return Save_StoreIn(NULL, Name, Data, Size);
}

bool Save_Erase(const char *Name)
{
    return Save_EraseIn(NULL, Name);
}

bool Save_LoadIn(const char *Namespace, const char *Name, void *Data, uint32_t Size)
{
    const Storage_KeyTypeDef Key = Save_KeyIn(Namespace, Name);

    if((Data == NULL) || (Size == 0U) || (Save_StoredSize(Key) != Size))
    {
        return false;
    }

    return Storage_Read(Key, Data, Size, NULL) == STORAGE_RESULT_OK;
}

bool Save_StoreIn(const char *Namespace, const char *Name, const void *Data, uint32_t Size)
{
    const Storage_KeyTypeDef Key = Save_KeyIn(Namespace, Name);

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

bool Save_EraseIn(const char *Namespace, const char *Name)
{
    const Storage_ResultTypeDef Result = Storage_Delete(Save_KeyIn(Namespace, Name));

    return (Result == STORAGE_RESULT_OK) || (Result == STORAGE_RESULT_NOT_FOUND);
}
