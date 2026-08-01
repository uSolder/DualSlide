/**
 * @file storage.h
 * @brief Persistent key/value storage contract.
 *
 * Storage records survive reset and loss of system power. Keys identify data
 * owned by an application or system service; their physical Flash location is
 * intentionally hidden from callers.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

typedef uint32_t Storage_KeyTypeDef;

typedef enum
{
    STORAGE_RESULT_OK,
    STORAGE_RESULT_NOT_FOUND,
    STORAGE_RESULT_INVALID_ARGUMENT,
    STORAGE_RESULT_BUFFER_TOO_SMALL,
    STORAGE_RESULT_NO_SPACE,
    STORAGE_RESULT_ERROR
} Storage_ResultTypeDef;

/**
 * @brief Initializes persistent storage and selects the newest valid sector.
 *
 * @return STORAGE_RESULT_OK on success.
 */
Storage_ResultTypeDef Storage_Init(void);

/**
 * @brief Reads the newest valid value associated with a key.
 *
 * @param key             Key assigned by the owning application or service.
 * @param data            Destination buffer for the stored value.
 * @param dataSize        Destination buffer size in bytes.
 * @param storedDataSize  Receives the stored value size in bytes. May be NULL.
 *
 * @return STORAGE_RESULT_OK if the value was copied;
 *         STORAGE_RESULT_NOT_FOUND if no value exists for key;
 *         STORAGE_RESULT_BUFFER_TOO_SMALL if dataSize is insufficient.
 */
Storage_ResultTypeDef Storage_Read(Storage_KeyTypeDef key, void *data, uint32_t dataSize, uint32_t *storedDataSize);

/**
 * @brief Atomically replaces the value associated with a key.
 *
 * A successful write survives reset and power loss. If compaction is needed,
 * the storage implementation preserves the previous valid sector until the
 * replacement sector has been completely verified.
 *
 * @param key       Key assigned by the owning application or service.
 * @param data      Value to store.
 * @param dataSize  Value size in bytes.
 *
 * @return STORAGE_RESULT_OK on success.
 */
Storage_ResultTypeDef Storage_Write(Storage_KeyTypeDef key, const void *data, uint32_t dataSize);

/**
 * @brief Removes the value associated with a key.
 *
 * The implementation records a deletion entry; it does not immediately erase
 * either Flash sector.
 *
 * @param key Key assigned by the owning application or service.
 *
 * @return STORAGE_RESULT_OK on success or STORAGE_RESULT_NOT_FOUND if absent.
 */
Storage_ResultTypeDef Storage_Delete(Storage_KeyTypeDef key);

/**
 * @brief Erases all persistent data.
 *
 * Intended only for an explicit factory-reset action.
 *
 * @return STORAGE_RESULT_OK on success.
 */
Storage_ResultTypeDef Storage_EraseAll(void);

#endif