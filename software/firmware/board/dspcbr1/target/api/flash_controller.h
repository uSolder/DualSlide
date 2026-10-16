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

#ifndef TARGET_API_FLASH_CONTROLLER_H
#define TARGET_API_FLASH_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
} FlashController_ResultTypeDef;

/**
 * @brief Description of one erasable Flash sector.
 */
typedef struct
{
    uintptr_t StartAddress;
    size_t SizeBytes;
} FlashController_SectorInformationTypeDef;

/**
 * @brief Physical configuration and state of one Flash controller.
 *
 * The target-specific implementation populates the geometry fields during
 * initialization. Applications must treat all members as read-only.
 */
typedef struct
{
    uintptr_t FlashStartAddress;
    size_t FlashSizeBytes;
    size_t SectorCount;
    size_t ProgramUnitBytes;
} FlashController_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/* Initialization and geometry                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a Flash controller and read its physical geometry.
 *
 * @param Controller Flash-controller handle.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_ResultTypeDef FlashController_Init(FlashController_HandleTypeDef *Controller);

/**
 * @brief Return information about one erasable Flash sector.
 *
 * @param Controller Initialized Flash-controller handle.
 * @param Sector      Zero-based physical sector number.
 * @param Information Receives sector address and size.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_ResultTypeDef FlashController_GetSectorInformation(const FlashController_HandleTypeDef *Controller,
                                                             size_t Sector,
                                                             FlashController_SectorInformationTypeDef *Information);

/* -------------------------------------------------------------------------- */
/* Flash access                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read bytes from memory-mapped Flash.
 *
 * The requested range must lie wholly within the controller's Flash region.
 *
 * @param Controller Initialized Flash-controller handle.
 * @param Address    Flash address to read.
 * @param Data       Destination buffer.
 * @param Size       Number of bytes to read.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_ResultTypeDef FlashController_Read(const FlashController_HandleTypeDef *Controller, uintptr_t Address, void *Data, size_t Size);

/**
 * @brief Program erased Flash using the controller's native program unit.
 *
 * The address and size must be aligned to controller->program_unit_bytes.
 * This operation does not erase Flash and fails if programming would require
 * any bit to change from zero to one.
 *
 * @param Controller Initialized Flash-controller handle.
 * @param Address    Aligned Flash address to program.
 * @param Data       Source buffer containing programmed bytes.
 * @param Size       Number of bytes to program, aligned to the program unit.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_ResultTypeDef FlashController_Program(FlashController_HandleTypeDef *Controller, uintptr_t Address, const void *Data, size_t Size);

/**
 * @brief Erase one physical Flash sector.
 *
 * Erasing sets every bit in the selected sector to one. This operation blocks
 * until completion or timeout.
 *
 * @param Controller Initialized Flash-controller handle.
 * @param Sector     Zero-based physical sector number.
 *
 * @return FLASH_CONTROLLER_RESULT_OK on success.
 */
FlashController_ResultTypeDef FlashController_EraseSector(FlashController_HandleTypeDef *Controller, size_t Sector);

/**
 * @brief Verify that a range of Flash matches expected data.
 *
 * @param Controller Initialized Flash-controller handle.
 * @param Address    Flash address to verify.
 * @param Data       Expected bytes.
 * @param Size       Number of bytes to verify.
 *
 * @return FLASH_CONTROLLER_RESULT_OK if every byte matches;
 *         FLASH_CONTROLLER_RESULT_VERIFY_ERROR otherwise.
 */
FlashController_ResultTypeDef FlashController_Verify(const FlashController_HandleTypeDef *Controller,  uintptr_t Address, const void *Data, size_t Size);

/* -------------------------------------------------------------------------- */
/* Controller state                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether the controller is performing a Flash operation.
 *
 * @param Controller Initialized Flash-controller handle.
 *
 * @return true while an erase or program operation is active; otherwise false.
 */
bool FlashController_IsBusy(const FlashController_HandleTypeDef *Controller);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_FLASH_CONTROLLER_H */
