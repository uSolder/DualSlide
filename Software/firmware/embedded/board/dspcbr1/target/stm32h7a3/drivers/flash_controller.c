/**
 * @file flash_controller.c
 * @brief STM32H7A3 internal Flash-controller implementation.
 */

#include "flash_controller.h"

#include <string.h>

#include "stm32h7xx.h"

#define FLASH_CONTROLLER_FLASH_BASE_ADDRESS ((uintptr_t)FLASH_BANK1_BASE)
#define FLASH_CONTROLLER_FLASH_SIZE_BYTES (1UL * 1024UL * 1024UL)
#define FLASH_CONTROLLER_BANK_SIZE_BYTES (512UL * 1024UL)
#define FLASH_CONTROLLER_BANK_1_BASE_ADDRESS ((uintptr_t)FLASH_BANK1_BASE)
#define FLASH_CONTROLLER_BANK_2_BASE_ADDRESS ((uintptr_t)FLASH_BANK2_BASE)
#define FLASH_CONTROLLER_SECTOR_SIZE_BYTES ((size_t)FLASH_SECTOR_SIZE)
#define FLASH_CONTROLLER_SECTOR_COUNT ((size_t)FLASH_SECTOR_TOTAL)
#define FLASH_CONTROLLER_SECTORS_PER_BANK (64UL)
#define FLASH_CONTROLLER_PROGRAM_UNIT_BYTES ((size_t)(FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(uint32_t)))
#define FLASH_CONTROLLER_KEY_1 (0x45670123UL)
#define FLASH_CONTROLLER_KEY_2 (0xCDEF89ABUL)
#define FLASH_CONTROLLER_CONTROL_LOCK (1UL << 0U)
#define FLASH_CONTROLLER_CONTROL_PROGRAM (1UL << 1U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_ERASE (1UL << 2U)
#define FLASH_CONTROLLER_CONTROL_START (1UL << 5U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION (6U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK (127UL << FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION)
#define FLASH_CONTROLLER_STATUS_BUSY (1UL << 0U)
#define FLASH_CONTROLLER_STATUS_WRITE_BUFFER_NOT_EMPTY (1UL << 1U)
#define FLASH_CONTROLLER_STATUS_QUEUE_WAIT (1UL << 2U)
#define FLASH_CONTROLLER_STATUS_END_OF_OPERATION (1UL << 16U)
#define FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR (1UL << 17U)
#define FLASH_CONTROLLER_STATUS_PROGRAMMING_SEQUENCE_ERROR (1UL << 18U)
#define FLASH_CONTROLLER_STATUS_STROBE_ERROR (1UL << 19U)
#define FLASH_CONTROLLER_STATUS_INCONSISTENCY_ERROR (1UL << 21U)
#define FLASH_CONTROLLER_STATUS_READ_PROTECTION_ERROR (1UL << 23U)
#define FLASH_CONTROLLER_STATUS_READ_SECURE_ERROR (1UL << 24U)
#define FLASH_CONTROLLER_STATUS_SINGLE_ECC_ERROR (1UL << 25U)
#define FLASH_CONTROLLER_STATUS_DOUBLE_ECC_ERROR (1UL << 26U)
#define FLASH_CONTROLLER_STATUS_ERROR_MASK (FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR | FLASH_CONTROLLER_STATUS_PROGRAMMING_SEQUENCE_ERROR | FLASH_CONTROLLER_STATUS_STROBE_ERROR | FLASH_CONTROLLER_STATUS_INCONSISTENCY_ERROR | FLASH_CONTROLLER_STATUS_READ_PROTECTION_ERROR | FLASH_CONTROLLER_STATUS_READ_SECURE_ERROR | FLASH_CONTROLLER_STATUS_SINGLE_ECC_ERROR | FLASH_CONTROLLER_STATUS_DOUBLE_ECC_ERROR)
#define FLASH_CONTROLLER_STATUS_CLEAR_MASK (FLASH_CONTROLLER_STATUS_END_OF_OPERATION | FLASH_CONTROLLER_STATUS_ERROR_MASK)
#define FLASH_CONTROLLER_OPERATION_TIMEOUT_CYCLES (100000000UL)
#define FLASH_CONTROLLER_CACHE_LINE_BYTES (32UL)
#define FLASH_CONTROLLER_SCB_DCIMVAC_ADDRESS ((uintptr_t)0xE000EF5CUL)



static bool FlashController_IsInitialized(const FlashController_Handle *controller)
{
    return (controller != NULL) && (controller->flash_start_address == FLASH_CONTROLLER_FLASH_BASE_ADDRESS) && (controller->flash_size_bytes == FLASH_CONTROLLER_FLASH_SIZE_BYTES) && (controller->sector_count == FLASH_CONTROLLER_SECTOR_COUNT) && (controller->program_unit_bytes == FLASH_CONTROLLER_PROGRAM_UNIT_BYTES);
}

static bool FlashController_IsRangeWithinBank(uintptr_t address, size_t size, uintptr_t bankBaseAddress)
{
    uintptr_t bankEndAddress = bankBaseAddress + FLASH_CONTROLLER_BANK_SIZE_BYTES;
    return (size != 0U) && (address >= bankBaseAddress) && (address < bankEndAddress) && (size <= (bankEndAddress - address));
}

static bool FlashController_IsRangeValid(uintptr_t address, size_t size)
{
    return FlashController_IsRangeWithinBank(address, size, FLASH_CONTROLLER_BANK_1_BASE_ADDRESS) || FlashController_IsRangeWithinBank(address, size, FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
}

static bool FlashController_IsBank1Address(uintptr_t address)
{
    bool lowerAddressBank = address < FLASH_CONTROLLER_BANK_2_BASE_ADDRESS;
    bool banksSwapped = (FLASH->OPTCR & FLASH_OPTCR_SWAP_BANK) != 0U;
    return lowerAddressBank != banksSwapped;
}

static volatile uint32_t *FlashController_GetControlRegister(uintptr_t address)
{
    return FlashController_IsBank1Address(address) ? &FLASH->CR1 : &FLASH->CR2;
}

static volatile uint32_t *FlashController_GetStatusRegister(uintptr_t address)
{
    return FlashController_IsBank1Address(address) ? &FLASH->SR1 : &FLASH->SR2;
}

static volatile uint32_t *FlashController_GetClearControlRegister(uintptr_t address)
{
    return FlashController_IsBank1Address(address) ? &FLASH->CCR1 : &FLASH->CCR2;
}

static void FlashController_DataSynchronizationBarrier(void)
{
    __DSB();
}

static void FlashController_InstructionSynchronizationBarrier(void)
{
    __ISB();
}

static void FlashController_InvalidateDataCache(uintptr_t address, size_t size)
{
    volatile uint32_t *invalidateByAddress = (volatile uint32_t *)FLASH_CONTROLLER_SCB_DCIMVAC_ADDRESS;
    uintptr_t cacheAddress = address & ~(FLASH_CONTROLLER_CACHE_LINE_BYTES - 1UL);
    uintptr_t endAddress = address + size;
    FlashController_DataSynchronizationBarrier();
    while (cacheAddress < endAddress)
    {
        *invalidateByAddress = (uint32_t)cacheAddress;
        cacheAddress += FLASH_CONTROLLER_CACHE_LINE_BYTES;
    }
    FlashController_DataSynchronizationBarrier();
    FlashController_InstructionSynchronizationBarrier();
}

static FlashController_Result FlashController_UnlockBank(uintptr_t address)
{
    volatile uint32_t *controlRegister = FlashController_GetControlRegister(address);
    if ((*controlRegister & FLASH_CONTROLLER_CONTROL_LOCK) == 0U)
    {
        return FLASH_CONTROLLER_RESULT_OK;
    }
    if (FlashController_IsBank1Address(address))
    {
        FLASH->KEYR1 = FLASH_CONTROLLER_KEY_1;
        FLASH->KEYR1 = FLASH_CONTROLLER_KEY_2;
    }
    else
    {
        FLASH->KEYR2 = FLASH_CONTROLLER_KEY_1;
        FLASH->KEYR2 = FLASH_CONTROLLER_KEY_2;
    }
    FlashController_DataSynchronizationBarrier();
    return (*controlRegister & FLASH_CONTROLLER_CONTROL_LOCK) == 0U ? FLASH_CONTROLLER_RESULT_OK : FLASH_CONTROLLER_RESULT_WRITE_PROTECTED;
}

static void FlashController_LockBank(uintptr_t address)
{
    *FlashController_GetControlRegister(address) |= FLASH_CONTROLLER_CONTROL_LOCK;
}

static FlashController_Result FlashController_GetOperationResult(uint32_t status)
{
    if ((status & FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_WRITE_PROTECTED;
    }
    if ((status & FLASH_CONTROLLER_STATUS_ERROR_MASK) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_IO_ERROR;
    }
    return FLASH_CONTROLLER_RESULT_OK;
}

static FlashController_Result FlashController_WaitForOperation(uintptr_t address)
{
    volatile uint32_t *statusRegister = FlashController_GetStatusRegister(address);
    uint32_t timeout = FLASH_CONTROLLER_OPERATION_TIMEOUT_CYCLES;
    uint32_t status;
    do
    {
        status = *statusRegister;
        timeout--;
    } while (((status & FLASH_CONTROLLER_STATUS_QUEUE_WAIT) != 0U) && (timeout != 0U));
    if ((status & FLASH_CONTROLLER_STATUS_QUEUE_WAIT) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_TIMEOUT;
    }
    *FlashController_GetClearControlRegister(address) = status & FLASH_CONTROLLER_STATUS_CLEAR_MASK;
    return FlashController_GetOperationResult(status);
}

static void FlashController_ClearStatus(uintptr_t address)
{
    *FlashController_GetClearControlRegister(address) = FLASH_CONTROLLER_STATUS_CLEAR_MASK;
}

static FlashController_Result FlashController_ProgramFlashWord(uintptr_t address, const uint8_t *data)
{
    volatile uint32_t *controlRegister = FlashController_GetControlRegister(address);
    volatile uint32_t *destination = (volatile uint32_t *)address;
    uint32_t sourceWords[FLASH_NB_32BITWORD_IN_FLASHWORD];
    size_t index;
    FlashController_Result result;
    memcpy(sourceWords, data, sizeof(sourceWords));
    FlashController_ClearStatus(address);
    *controlRegister |= FLASH_CONTROLLER_CONTROL_PROGRAM;
    FlashController_InstructionSynchronizationBarrier();
    FlashController_DataSynchronizationBarrier();
    for (index = 0U; index < FLASH_NB_32BITWORD_IN_FLASHWORD; index++)
    {
        destination[index] = sourceWords[index];
    }
    FlashController_InstructionSynchronizationBarrier();
    FlashController_DataSynchronizationBarrier();
    result = FlashController_WaitForOperation(address);
    *controlRegister &= ~FLASH_CONTROLLER_CONTROL_PROGRAM;
    return result;
}

FlashController_Result FlashController_Init(FlashController_Handle *controller)
{
    if (controller == NULL)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    controller->flash_start_address = FLASH_CONTROLLER_FLASH_BASE_ADDRESS;
    controller->flash_size_bytes = FLASH_CONTROLLER_FLASH_SIZE_BYTES;
    controller->sector_count = FLASH_CONTROLLER_SECTOR_COUNT;
    controller->program_unit_bytes = FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_Result FlashController_GetSectorInformation(const FlashController_Handle *controller, size_t sector, FlashController_SectorInformation *information)
{
    uintptr_t bankBaseAddress;
    size_t sectorInBank;
    if ((controller == NULL) || (information == NULL))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if (!FlashController_IsInitialized(controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    if (sector >= FLASH_CONTROLLER_SECTOR_COUNT)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    bankBaseAddress = sector < FLASH_CONTROLLER_SECTORS_PER_BANK ? FLASH_CONTROLLER_BANK_1_BASE_ADDRESS : FLASH_CONTROLLER_BANK_2_BASE_ADDRESS;
    sectorInBank = sector % FLASH_CONTROLLER_SECTORS_PER_BANK;
    information->start_address = bankBaseAddress + (sectorInBank * FLASH_CONTROLLER_SECTOR_SIZE_BYTES);
    information->size_bytes = FLASH_CONTROLLER_SECTOR_SIZE_BYTES;
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_Result FlashController_Read(const FlashController_Handle *controller, uintptr_t address, void *data, size_t size)
{
    if ((controller == NULL) || (data == NULL) || !FlashController_IsRangeValid(address, size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if (!FlashController_IsInitialized(controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    FlashController_InvalidateDataCache(address, size);
    memcpy(data, (const void *)address, size);
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_Result FlashController_Program(FlashController_Handle *controller, uintptr_t address, const void *data, size_t size)
{
    const uint8_t *source = data;
    uintptr_t programAddress = address;
    size_t remainingSize = size;
    bool bank1Used;
    bool bank2Used;
    FlashController_Result result = FLASH_CONTROLLER_RESULT_OK;
    if ((controller == NULL) || (data == NULL) || !FlashController_IsRangeValid(address, size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if (!FlashController_IsInitialized(controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    if (((address % FLASH_CONTROLLER_PROGRAM_UNIT_BYTES) != 0U) || ((size % FLASH_CONTROLLER_PROGRAM_UNIT_BYTES) != 0U))
    {
        return FLASH_CONTROLLER_RESULT_ALIGNMENT_ERROR;
    }
    bank1Used = FlashController_IsBank1Address(address);
    bank2Used = !bank1Used;
    result = FlashController_WaitForOperation(address);
    if ((result == FLASH_CONTROLLER_RESULT_OK) && bank1Used)
    {
        result = FlashController_UnlockBank(FLASH_CONTROLLER_BANK_1_BASE_ADDRESS);
    }
    if ((result == FLASH_CONTROLLER_RESULT_OK) && bank2Used)
    {
        result = FlashController_UnlockBank(FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
    }
    while ((remainingSize != 0U) && (result == FLASH_CONTROLLER_RESULT_OK))
    {
        result = FlashController_ProgramFlashWord(programAddress, source);
        programAddress += FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
        source += FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
        remainingSize -= FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
    }
    if (bank1Used)
    {
        FlashController_LockBank(FLASH_CONTROLLER_BANK_1_BASE_ADDRESS);
    }
    if (bank2Used)
    {
        FlashController_LockBank(FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
    }
    FlashController_InvalidateDataCache(address, size);
    return result;
}

FlashController_Result FlashController_EraseSector(FlashController_Handle *controller, size_t sector)
{
    FlashController_SectorInformation information;
    volatile uint32_t *controlRegister;
    uint32_t sectorInBank;
    FlashController_Result result;
    if (controller == NULL)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    result = FlashController_GetSectorInformation(controller, sector, &information);
    if (result != FLASH_CONTROLLER_RESULT_OK)
    {
        return result;
    }
    sectorInBank = (uint32_t)(sector % FLASH_CONTROLLER_SECTORS_PER_BANK);
    controlRegister = FlashController_GetControlRegister(information.start_address);
    result = FlashController_WaitForOperation(information.start_address);
    if (result != FLASH_CONTROLLER_RESULT_OK)
    {
        return result;
    }
    result = FlashController_UnlockBank(information.start_address);
    if (result != FLASH_CONTROLLER_RESULT_OK)
    {
        return result;
    }
    FlashController_ClearStatus(information.start_address);
    *controlRegister = (*controlRegister & ~FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK) | (sectorInBank << FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION) | FLASH_CONTROLLER_CONTROL_SECTOR_ERASE;
    *controlRegister |= FLASH_CONTROLLER_CONTROL_START;
    FlashController_DataSynchronizationBarrier();
    result = FlashController_WaitForOperation(information.start_address);
    *controlRegister &= ~(FLASH_CONTROLLER_CONTROL_SECTOR_ERASE | FLASH_CONTROLLER_CONTROL_START | FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK);
    FlashController_LockBank(information.start_address);
    FlashController_InvalidateDataCache(information.start_address, information.size_bytes);
    return result;
}

FlashController_Result FlashController_Verify(const FlashController_Handle *controller, uintptr_t address, const void *data, size_t size)
{
    if ((controller == NULL) || (data == NULL) || !FlashController_IsRangeValid(address, size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if (!FlashController_IsInitialized(controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    FlashController_InvalidateDataCache(address, size);
    return memcmp((const void *)address, data, size) == 0 ? FLASH_CONTROLLER_RESULT_OK : FLASH_CONTROLLER_RESULT_VERIFY_ERROR;
}

bool FlashController_IsBusy(const FlashController_Handle *controller)
{
    uint32_t status;
    if (!FlashController_IsInitialized(controller))
    {
        return false;
    }
    status = FLASH->SR1 | FLASH->SR2;
    return (status & (FLASH_CONTROLLER_STATUS_BUSY | FLASH_CONTROLLER_STATUS_WRITE_BUFFER_NOT_EMPTY | FLASH_CONTROLLER_STATUS_QUEUE_WAIT)) != 0U;
}