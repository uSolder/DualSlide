/**
 * @file flash_controller.h
 * @brief Hardware-independent non-volatile Flash-controller interface contract.
 *
 * This interface represents memory-mapped non-volatile Flash controlled by an
 * MCU Flash peripheral. The target-specific implementation translates sector
 * erase and program requests into the appropriate controller operations.
 *
 * Flash is erased one sector at a time. Programming may only change bits from
 * one to zero; callers must erase a sector before programming data that would
 * require a zero-to-one bit transition.
 */

#ifndef TARGET_INTERFACE_FLASH_CONTROLLER_H
#define TARGET_INTERFACE_FLASH_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Result returned by a Flash-controller operation.
 */
typedef enum
{
    FLASH_CONTROLLER_RESULT_OK = 0,
    FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT,
    FLASH_CONTROLLER_RESULT_NOT_INITIALIZED,
    FLASH_CONTROLLER_RESULT_ALIGNMENT_ERROR,
    FLASH_CONTROLLER_RESULT_WRITE_PROTECTED,
    FLASH_CONTROLLER_RESULT_TIMEOUT,
    FLASH_CONTROLLER_RESULT_VERIFY_ERROR,
    FLASH_CONTROLLER_RESULT_IO_ERROR
} FlashController_Result;

/**
 * @brief Description of one erasable Flash sector.
 */
typedef struct
{
    uintptr_t start_address;
    size_t size_bytes;
} FlashController_SectorInformation;

/**
 * @brief Physical configuration and state of one Flash controller.
 *
 * The target-specific implementation populates the geometry fields during
 * initialization. Applications must treat all members as read-only.
 */
typedef struct
{
    uintptr_t flash_start_address;
    size_t flash_size_bytes;
    size_t sector_count;
    size_t program_unit_bytes;
} FlashController_Handle;

/* -------------------------------------------------------------------------- */
/* Initialization and geometry                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a Flash controller and read its physical geometry.
 *
 * @param controller Flash-controller handle.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_Result FlashController_Init(FlashController_Handle *controller);

/**
 * @brief Return information about one erasable Flash sector.
 *
 * @param controller Initialized Flash-controller handle.
 * @param sector      Zero-based physical sector number.
 * @param information Receives sector address and size.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_Result FlashController_GetSectorInformation(const FlashController_Handle *controller,
                                                             size_t sector,
                                                             FlashController_SectorInformation *information);

/* -------------------------------------------------------------------------- */
/* Flash access                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read bytes from memory-mapped Flash.
 *
 * The requested range must lie wholly within the controller's Flash region.
 *
 * @param controller Initialized Flash-controller handle.
 * @param address    Flash address to read.
 * @param data       Destination buffer.
 * @param size       Number of bytes to read.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_Result FlashController_Read(const FlashController_Handle *controller, uintptr_t address, void *data, size_t size);

/**
 * @brief Program erased Flash using the controller's native program unit.
 *
 * The address and size must be aligned to controller->program_unit_bytes.
 * This operation does not erase Flash and fails if programming would require
 * any bit to change from zero to one.
 *
 * @param controller Initialized Flash-controller handle.
 * @param address    Aligned Flash address to program.
 * @param data       Source buffer containing programmed bytes.
 * @param size       Number of bytes to program, aligned to the program unit.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_Result FlashController_Program(FlashController_Handle *controller, uintptr_t address, const void *data, size_t size);

/**
 * @brief Erase one physical Flash sector.
 *
 * Erasing sets every bit in the selected sector to one. This operation blocks
 * until completion or timeout.
 *
 * @param controller Initialized Flash-controller handle.
 * @param sector     Zero-based physical sector number.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_Result FlashController_EraseSector(FlashController_Handle *controller, size_t sector);

/**
 * @brief Verify that a range of Flash matches expected data.
 *
 * @param controller Initialized Flash-controller handle.
 * @param address    Flash address to verify.
 * @param data       Expected bytes.
 * @param size       Number of bytes to verify.
 *
 * @return FLASH_CONTROLLER_RESULT_OK if every byte matches;
 *         FLASH_CONTROLLER_RESULT_VERIFY_ERROR otherwise.
 */
FlashController_Result FlashController_Verify(const FlashController_Handle *controller,  uintptr_t address, const void *data, size_t size);

/* -------------------------------------------------------------------------- */
/* Controller state                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether the controller is performing a Flash operation.
 *
 * @param controller Initialized Flash-controller handle.
 *
 * @return true while an erase or program operation is active; otherwise false.
 */
bool FlashController_IsBusy(const FlashController_Handle *controller);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_FLASH_CONTROLLER_H */