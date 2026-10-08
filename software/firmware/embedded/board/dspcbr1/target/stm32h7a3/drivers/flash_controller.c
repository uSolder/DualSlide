/**
 * @file flash_controller.c
 * @brief STM32H7A3 internal Flash-controller implementation.
 */

#include "flash_controller.h"

#include "stm32h7xx.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define FLASH_CONTROLLER_FLASH_BASE_ADDRESS                ((uintptr_t)FLASH_BANK1_BASE)
#define FLASH_CONTROLLER_FLASH_SIZE_BYTES                  (1UL * 1024UL * 1024UL)
#define FLASH_CONTROLLER_BANK_SIZE_BYTES                   (512UL * 1024UL)
#define FLASH_CONTROLLER_BANK_1_BASE_ADDRESS               ((uintptr_t)FLASH_BANK1_BASE)
#define FLASH_CONTROLLER_BANK_2_BASE_ADDRESS               ((uintptr_t)FLASH_BANK2_BASE)
#define FLASH_CONTROLLER_SECTOR_SIZE_BYTES                 ((size_t)FLASH_SECTOR_SIZE)
#define FLASH_CONTROLLER_SECTOR_COUNT                      ((size_t)FLASH_SECTOR_TOTAL)
#define FLASH_CONTROLLER_SECTORS_PER_BANK                  (64UL)
#define FLASH_CONTROLLER_PROGRAM_UNIT_BYTES                ((size_t)(FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(uint32_t)))
#define FLASH_CONTROLLER_KEY_1                             (0x45670123UL)
#define FLASH_CONTROLLER_KEY_2                             (0xCDEF89ABUL)
#define FLASH_CONTROLLER_CONTROL_LOCK                      (1UL << 0U)
#define FLASH_CONTROLLER_CONTROL_PROGRAM                   (1UL << 1U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_ERASE              (1UL << 2U)
#define FLASH_CONTROLLER_CONTROL_START                     (1UL << 5U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION    (6U)
#define FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK        (127UL << FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION)
#define FLASH_CONTROLLER_STATUS_BUSY                       (1UL << 0U)
#define FLASH_CONTROLLER_STATUS_WRITE_BUFFER_NOT_EMPTY     (1UL << 1U)
#define FLASH_CONTROLLER_STATUS_QUEUE_WAIT                 (1UL << 2U)
#define FLASH_CONTROLLER_STATUS_END_OF_OPERATION           (1UL << 16U)
#define FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR     (1UL << 17U)
#define FLASH_CONTROLLER_STATUS_PROGRAMMING_SEQUENCE_ERROR (1UL << 18U)
#define FLASH_CONTROLLER_STATUS_STROBE_ERROR               (1UL << 19U)
#define FLASH_CONTROLLER_STATUS_INCONSISTENCY_ERROR        (1UL << 21U)
#define FLASH_CONTROLLER_STATUS_READ_PROTECTION_ERROR      (1UL << 23U)
#define FLASH_CONTROLLER_STATUS_READ_SECURE_ERROR          (1UL << 24U)
#define FLASH_CONTROLLER_STATUS_SINGLE_ECC_ERROR           (1UL << 25U)
#define FLASH_CONTROLLER_STATUS_DOUBLE_ECC_ERROR           (1UL << 26U)
#define FLASH_CONTROLLER_STATUS_ERROR_MASK                 (FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR | FLASH_CONTROLLER_STATUS_PROGRAMMING_SEQUENCE_ERROR | FLASH_CONTROLLER_STATUS_STROBE_ERROR | FLASH_CONTROLLER_STATUS_INCONSISTENCY_ERROR | FLASH_CONTROLLER_STATUS_READ_PROTECTION_ERROR | FLASH_CONTROLLER_STATUS_READ_SECURE_ERROR | FLASH_CONTROLLER_STATUS_SINGLE_ECC_ERROR | FLASH_CONTROLLER_STATUS_DOUBLE_ECC_ERROR)
#define FLASH_CONTROLLER_STATUS_CLEAR_MASK                 (FLASH_CONTROLLER_STATUS_END_OF_OPERATION | FLASH_CONTROLLER_STATUS_ERROR_MASK)
#define FLASH_CONTROLLER_OPERATION_TIMEOUT_CYCLES          (100000000UL)
#define FLASH_CONTROLLER_CACHE_LINE_BYTES                  (32UL)
#define FLASH_CONTROLLER_SCB_DCIMVAC_ADDRESS               ((uintptr_t)0xE000EF5CUL)

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool FlashController_IsInitialized(const FlashController_HandleTypeDef *Controller)
{
    return (Controller != NULL) && (Controller->FlashStartAddress == FLASH_CONTROLLER_FLASH_BASE_ADDRESS) && (Controller->FlashSizeBytes == FLASH_CONTROLLER_FLASH_SIZE_BYTES) && (Controller->SectorCount == FLASH_CONTROLLER_SECTOR_COUNT) && (Controller->ProgramUnitBytes == FLASH_CONTROLLER_PROGRAM_UNIT_BYTES);
}

static bool FlashController_IsRangeWithinBank(uintptr_t Address, size_t Size, uintptr_t BankBaseAddress)
{
    uintptr_t BankEndAddress = BankBaseAddress + FLASH_CONTROLLER_BANK_SIZE_BYTES;
    return (Size != 0U) && (Address >= BankBaseAddress) && (Address < BankEndAddress) && (Size <= (BankEndAddress - Address));
}

static bool FlashController_IsRangeValid(uintptr_t Address, size_t Size)
{
    return FlashController_IsRangeWithinBank(Address, Size, FLASH_CONTROLLER_BANK_1_BASE_ADDRESS) || FlashController_IsRangeWithinBank(Address, Size, FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
}

static bool FlashController_IsBank1Address(uintptr_t Address)
{
    bool LowerAddressBank = Address < FLASH_CONTROLLER_BANK_2_BASE_ADDRESS;
    bool BanksSwapped = (FLASH->OPTCR & FLASH_OPTCR_SWAP_BANK) != 0U;
    return LowerAddressBank != BanksSwapped;
}

static volatile uint32_t *FlashController_GetControlRegister(uintptr_t Address)
{
    return FlashController_IsBank1Address(Address) ? &FLASH->CR1 : &FLASH->CR2;
}

static volatile uint32_t *FlashController_GetStatusRegister(uintptr_t Address)
{
    return FlashController_IsBank1Address(Address) ? &FLASH->SR1 : &FLASH->SR2;
}

static volatile uint32_t *FlashController_GetClearControlRegister(uintptr_t Address)
{
    return FlashController_IsBank1Address(Address) ? &FLASH->CCR1 : &FLASH->CCR2;
}

static void FlashController_DataSynchronizationBarrier(void)
{
    __DSB();
}

static void FlashController_InstructionSynchronizationBarrier(void)
{
    __ISB();
}

static void FlashController_InvalidateDataCache(uintptr_t Address, size_t Size)
{
    volatile uint32_t *InvalidateByAddress = (volatile uint32_t *)FLASH_CONTROLLER_SCB_DCIMVAC_ADDRESS;
    uintptr_t CacheAddress = Address & ~(FLASH_CONTROLLER_CACHE_LINE_BYTES - 1UL);
    uintptr_t EndAddress = Address + Size;
    FlashController_DataSynchronizationBarrier();
    while(CacheAddress < EndAddress)
    {
        *InvalidateByAddress = (uint32_t)CacheAddress;
        CacheAddress += FLASH_CONTROLLER_CACHE_LINE_BYTES;
    }
    FlashController_DataSynchronizationBarrier();
    FlashController_InstructionSynchronizationBarrier();
}

static FlashController_ResultTypeDef FlashController_UnlockBank(uintptr_t Address)
{
    volatile uint32_t *ControlRegister = FlashController_GetControlRegister(Address);
    if((*ControlRegister & FLASH_CONTROLLER_CONTROL_LOCK) == 0U)
    {
        return FLASH_CONTROLLER_RESULT_OK;
    }
    if(FlashController_IsBank1Address(Address))
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
    return (*ControlRegister & FLASH_CONTROLLER_CONTROL_LOCK) == 0U ? FLASH_CONTROLLER_RESULT_OK : FLASH_CONTROLLER_RESULT_WRITE_PROTECTED;
}

static void FlashController_LockBank(uintptr_t Address)
{
    *FlashController_GetControlRegister(Address) |= FLASH_CONTROLLER_CONTROL_LOCK;
}

static FlashController_ResultTypeDef FlashController_GetOperationResult(uint32_t Status)
{
    if((Status & FLASH_CONTROLLER_STATUS_WRITE_PROTECTION_ERROR) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_WRITE_PROTECTED;
    }
    if((Status & FLASH_CONTROLLER_STATUS_ERROR_MASK) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_IO_ERROR;
    }
    return FLASH_CONTROLLER_RESULT_OK;
}

static FlashController_ResultTypeDef FlashController_WaitForOperation(uintptr_t Address)
{
    volatile uint32_t *StatusRegister = FlashController_GetStatusRegister(Address);
    uint32_t Timeout = FLASH_CONTROLLER_OPERATION_TIMEOUT_CYCLES;
    uint32_t Status;
    do
    {
        Status = *StatusRegister;
        Timeout--;
    } while(((Status & FLASH_CONTROLLER_STATUS_QUEUE_WAIT) != 0U) && (Timeout != 0U));
    if((Status & FLASH_CONTROLLER_STATUS_QUEUE_WAIT) != 0U)
    {
        return FLASH_CONTROLLER_RESULT_TIMEOUT;
    }
    *FlashController_GetClearControlRegister(Address) = Status & FLASH_CONTROLLER_STATUS_CLEAR_MASK;
    return FlashController_GetOperationResult(Status);
}

static void FlashController_ClearStatus(uintptr_t Address)
{
    *FlashController_GetClearControlRegister(Address) = FLASH_CONTROLLER_STATUS_CLEAR_MASK;
}

static FlashController_ResultTypeDef FlashController_ProgramFlashWord(uintptr_t Address, const uint8_t *Data)
{
    volatile uint32_t *ControlRegister = FlashController_GetControlRegister(Address);
    volatile uint32_t *Destination = (volatile uint32_t *)Address;
    uint32_t SourceWords[FLASH_NB_32BITWORD_IN_FLASHWORD];
    size_t Index;
    FlashController_ResultTypeDef Result;
    memcpy(SourceWords, Data, sizeof(SourceWords));
    FlashController_ClearStatus(Address);
    *ControlRegister |= FLASH_CONTROLLER_CONTROL_PROGRAM;
    FlashController_InstructionSynchronizationBarrier();
    FlashController_DataSynchronizationBarrier();
    for(Index = 0U; Index < FLASH_NB_32BITWORD_IN_FLASHWORD; Index++)
    {
        Destination[Index] = SourceWords[Index];
    }
    FlashController_InstructionSynchronizationBarrier();
    FlashController_DataSynchronizationBarrier();
    Result = FlashController_WaitForOperation(Address);
    *ControlRegister &= ~FLASH_CONTROLLER_CONTROL_PROGRAM;
    return Result;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

FlashController_ResultTypeDef FlashController_Init(FlashController_HandleTypeDef *Controller)
{
    if(Controller == NULL)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    Controller->FlashStartAddress = FLASH_CONTROLLER_FLASH_BASE_ADDRESS;
    Controller->FlashSizeBytes = FLASH_CONTROLLER_FLASH_SIZE_BYTES;
    Controller->SectorCount = FLASH_CONTROLLER_SECTOR_COUNT;
    Controller->ProgramUnitBytes = FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_ResultTypeDef FlashController_GetSectorInformation(const FlashController_HandleTypeDef *Controller, size_t Sector, FlashController_SectorInformationTypeDef *Information)
{
    uintptr_t BankBaseAddress;
    size_t SectorInBank;
    if((Controller == NULL) || (Information == NULL))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if(!FlashController_IsInitialized(Controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    if(Sector >= FLASH_CONTROLLER_SECTOR_COUNT)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    BankBaseAddress = Sector < FLASH_CONTROLLER_SECTORS_PER_BANK ? FLASH_CONTROLLER_BANK_1_BASE_ADDRESS : FLASH_CONTROLLER_BANK_2_BASE_ADDRESS;
    SectorInBank = Sector % FLASH_CONTROLLER_SECTORS_PER_BANK;
    Information->StartAddress = BankBaseAddress + (SectorInBank * FLASH_CONTROLLER_SECTOR_SIZE_BYTES);
    Information->SizeBytes = FLASH_CONTROLLER_SECTOR_SIZE_BYTES;
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_ResultTypeDef FlashController_Read(const FlashController_HandleTypeDef *Controller, uintptr_t Address, void *Data, size_t Size)
{
    if((Controller == NULL) || (Data == NULL) || !FlashController_IsRangeValid(Address, Size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if(!FlashController_IsInitialized(Controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    FlashController_InvalidateDataCache(Address, Size);
    memcpy(Data, (const void *)Address, Size);
    return FLASH_CONTROLLER_RESULT_OK;
}

FlashController_ResultTypeDef FlashController_Program(FlashController_HandleTypeDef *Controller, uintptr_t Address, const void *Data, size_t Size)
{
    const uint8_t *Source = Data;
    uintptr_t ProgramAddress = Address;
    size_t RemainingSize = Size;
    bool Bank1Used;
    bool Bank2Used;
    FlashController_ResultTypeDef Result = FLASH_CONTROLLER_RESULT_OK;
    if((Controller == NULL) || (Data == NULL) || !FlashController_IsRangeValid(Address, Size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if(!FlashController_IsInitialized(Controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    if(((Address % FLASH_CONTROLLER_PROGRAM_UNIT_BYTES) != 0U) || ((Size % FLASH_CONTROLLER_PROGRAM_UNIT_BYTES) != 0U))
    {
        return FLASH_CONTROLLER_RESULT_ALIGNMENT_ERROR;
    }
    Bank1Used = FlashController_IsBank1Address(Address);
    Bank2Used = !Bank1Used;
    Result = FlashController_WaitForOperation(Address);
    if((Result == FLASH_CONTROLLER_RESULT_OK) && Bank1Used)
    {
        Result = FlashController_UnlockBank(FLASH_CONTROLLER_BANK_1_BASE_ADDRESS);
    }
    if((Result == FLASH_CONTROLLER_RESULT_OK) && Bank2Used)
    {
        Result = FlashController_UnlockBank(FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
    }
    while((RemainingSize != 0U) && (Result == FLASH_CONTROLLER_RESULT_OK))
    {
        Result = FlashController_ProgramFlashWord(ProgramAddress, Source);
        ProgramAddress += FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
        Source += FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
        RemainingSize -= FLASH_CONTROLLER_PROGRAM_UNIT_BYTES;
    }
    if(Bank1Used)
    {
        FlashController_LockBank(FLASH_CONTROLLER_BANK_1_BASE_ADDRESS);
    }
    if(Bank2Used)
    {
        FlashController_LockBank(FLASH_CONTROLLER_BANK_2_BASE_ADDRESS);
    }
    FlashController_InvalidateDataCache(Address, Size);
    return Result;
}

FlashController_ResultTypeDef FlashController_EraseSector(FlashController_HandleTypeDef *Controller, size_t Sector)
{
    FlashController_SectorInformationTypeDef Information;
    volatile uint32_t *ControlRegister;
    uint32_t SectorInBank;
    FlashController_ResultTypeDef Result;
    if(Controller == NULL)
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    Result = FlashController_GetSectorInformation(Controller, Sector, &Information);
    if(Result != FLASH_CONTROLLER_RESULT_OK)
    {
        return Result;
    }
    SectorInBank = (uint32_t)(Sector % FLASH_CONTROLLER_SECTORS_PER_BANK);
    ControlRegister = FlashController_GetControlRegister(Information.StartAddress);
    Result = FlashController_WaitForOperation(Information.StartAddress);
    if(Result != FLASH_CONTROLLER_RESULT_OK)
    {
        return Result;
    }
    Result = FlashController_UnlockBank(Information.StartAddress);
    if(Result != FLASH_CONTROLLER_RESULT_OK)
    {
        return Result;
    }
    FlashController_ClearStatus(Information.StartAddress);
    *ControlRegister = (*ControlRegister & ~FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK) | (SectorInBank << FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_POSITION) | FLASH_CONTROLLER_CONTROL_SECTOR_ERASE;
    *ControlRegister |= FLASH_CONTROLLER_CONTROL_START;
    FlashController_DataSynchronizationBarrier();
    Result = FlashController_WaitForOperation(Information.StartAddress);
    *ControlRegister &= ~(FLASH_CONTROLLER_CONTROL_SECTOR_ERASE | FLASH_CONTROLLER_CONTROL_START | FLASH_CONTROLLER_CONTROL_SECTOR_NUMBER_MASK);
    FlashController_LockBank(Information.StartAddress);
    FlashController_InvalidateDataCache(Information.StartAddress, Information.SizeBytes);
    return Result;
}

FlashController_ResultTypeDef FlashController_Verify(const FlashController_HandleTypeDef *Controller, uintptr_t Address, const void *Data, size_t Size)
{
    if((Controller == NULL) || (Data == NULL) || !FlashController_IsRangeValid(Address, Size))
    {
        return FLASH_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }
    if(!FlashController_IsInitialized(Controller))
    {
        return FLASH_CONTROLLER_RESULT_NOT_INITIALIZED;
    }
    FlashController_InvalidateDataCache(Address, Size);
    return memcmp((const void *)Address, Data, Size) == 0 ? FLASH_CONTROLLER_RESULT_OK : FLASH_CONTROLLER_RESULT_VERIFY_ERROR;
}

bool FlashController_IsBusy(const FlashController_HandleTypeDef *Controller)
{
    uint32_t Status;
    if(!FlashController_IsInitialized(Controller))
    {
        return false;
    }
    Status = FLASH->SR1 | FLASH->SR2;
    return (Status & (FLASH_CONTROLLER_STATUS_BUSY | FLASH_CONTROLLER_STATUS_WRITE_BUFFER_NOT_EMPTY | FLASH_CONTROLLER_STATUS_QUEUE_WAIT)) != 0U;
}
