/**
 * @file game_loader.c
 * @brief Loads a game file from the game disk into game memory and starts it.
 *
 * The file is read straight from the disk in pieces, so loading needs no
 * buffer the size of the game:
 *
 *   1. the first GAME_FILE_SUMMARY_BYTES: header, chunk table and INFO
 *   2. the whole file, to check its CRC
 *   3. the load image, into game memory, then the zeroed memory is cleared
 *   4. the relocations, a piece at a time, each adding the load address to
 *      one word of the image
 */

#include "game_loader.h"

#include "game_api.h"
#include "game_memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Bytes read at a time while checking the CRC. */
#define GAME_LOADER_CHUNK_SIZE              (2048U)

/* Relocations read at a time. */
#define GAME_LOADER_RELOCATION_BATCH        (128U)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint8_t GameLoader_Buffer[GAME_LOADER_CHUNK_SIZE] __attribute__((aligned(4)));

_Static_assert(GAME_LOADER_CHUNK_SIZE >= GAME_FILE_SUMMARY_BYTES, "The buffer must hold a summary");
_Static_assert((GAME_LOADER_RELOCATION_BATCH * sizeof(uint32_t)) <= GAME_LOADER_CHUNK_SIZE, "Relocations must fit the buffer");

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* Reads the start of the file into the buffer; returns how many bytes it holds. */
static uint32_t GameLoader_ReadStart(const FAT_FileTypeDef *File)
{
    uint32_t Size = (File->Size < GAME_FILE_SUMMARY_BYTES) ? File->Size : GAME_FILE_SUMMARY_BYTES;

    return FAT_Read(File, 0U, GameLoader_Buffer, Size) ? Size : 0U;
}

static GameFile_ResultTypeDef GameLoader_CheckCrc(const FAT_FileTypeDef *File, const GameFile_HeaderTypeDef *Header)
{
    uint32_t Offset = offsetof(GameFile_HeaderTypeDef, FileCRC) + sizeof(Header->FileCRC);
    uint32_t State = GAME_FILE_CRC_START;

    if(Header->FileSize != File->Size)
    {
        return GAME_FILE_DAMAGED;
    }

    while(Offset < File->Size)
    {
        uint32_t Count = File->Size - Offset;

        Count = (Count > GAME_LOADER_CHUNK_SIZE) ? GAME_LOADER_CHUNK_SIZE : Count;

        if(!FAT_Read(File, Offset, GameLoader_Buffer, Count))
        {
            return GAME_FILE_DAMAGED;
        }

        State = GameFile_UpdateCrc(State, GameLoader_Buffer, Count);
        Offset += Count;
    }

    return (~State == Header->FileCRC) ? GAME_FILE_OK : GAME_FILE_DAMAGED;
}

static bool GameLoader_Relocate(const FAT_FileTypeDef *File, const GameFile_ChunkTypeDef *Chunk, uint8_t *Image, uint32_t ImageSize)
{
    const uint32_t Base = (uint32_t)(uintptr_t)Image;
    const uint32_t Count = Chunk->Size / sizeof(uint32_t);
    uint32_t Done = 0U;

    while(Done < Count)
    {
        uint32_t Batch = Count - Done;
        uint32_t Index;

        Batch = (Batch > GAME_LOADER_RELOCATION_BATCH) ? GAME_LOADER_RELOCATION_BATCH : Batch;

        if(!FAT_Read(File, Chunk->Offset + (Done * sizeof(uint32_t)), GameLoader_Buffer, Batch * sizeof(uint32_t)))
        {
            return false;
        }

        for(Index = 0U; Index < Batch; Index++)
        {
            uint32_t Entry;
            uint32_t Offset;
            uint32_t Word;

            memcpy(&Entry, &GameLoader_Buffer[Index * sizeof(uint32_t)], sizeof(Entry));
            Offset = Entry & GAME_FILE_RELOCATION_OFFSET_MASK;

            if(((Entry >> GAME_FILE_RELOCATION_TYPE_SHIFT) != GAME_FILE_RELOCATION_ABSOLUTE_32) ||
               (Offset > (ImageSize - sizeof(Word))))
            {
                return false;
            }

            /* Words need not be aligned, so go through memcpy. */
            memcpy(&Word, &Image[Offset], sizeof(Word));
            Word += Base;
            memcpy(&Image[Offset], &Word, sizeof(Word));
        }

        Done += Batch;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

uint32_t GameLoader_GetAvailableMemory(void)
{
    return GameMemory_GetSize();
}

GameFile_ResultTypeDef GameLoader_ReadSummary(const FAT_FileTypeDef *File, GameFile_SummaryTypeDef *Summary)
{
    const uint32_t Size = GameLoader_ReadStart(File);
    GameFile_ResultTypeDef Result;

    if(Size == 0U)
    {
        memset(Summary, 0, sizeof(*Summary));
        return GAME_FILE_DAMAGED;
    }

    Result = GameFile_ReadSummary(GameLoader_Buffer, Size, Summary);

    if(Result != GAME_FILE_OK)
    {
        return Result;
    }

    return GameFile_CheckCompatibility(Summary, GameLoader_GetAvailableMemory());
}

GameFile_ResultTypeDef GameLoader_Load(const FAT_FileTypeDef *File, const AppManager_AppTypeDef **Application)
{
    uint8_t *Memory = GameMemory_GetAddress();
    const uint32_t MemorySize = GameMemory_GetSize();
    GameFile_SummaryTypeDef Summary;
    GameFile_ChunkTypeDef CodeChunk;
    GameFile_ChunkTypeDef RelocationChunk;
    GameFile_CodeTypeDef Code;
    GameAPI_EntryTypeDef Entry;
    const AppManager_AppTypeDef *Loaded;
    GameFile_ResultTypeDef Result;
    uint32_t Size;

    *Application = NULL;

    Result = GameLoader_ReadSummary(File, &Summary);

    if(Result != GAME_FILE_OK)
    {
        return Result;
    }

    /* The summary read left the start of the file in the buffer. */
    Size = (File->Size < GAME_FILE_SUMMARY_BYTES) ? File->Size : GAME_FILE_SUMMARY_BYTES;

    if(!GameFile_FindChunk(GameLoader_Buffer, Size, &Summary.Header, GAME_FILE_CHUNK_CODE, &CodeChunk) ||
       !GameFile_FindChunk(GameLoader_Buffer, Size, &Summary.Header, GAME_FILE_CHUNK_RELOCATIONS, &RelocationChunk) ||
       (CodeChunk.Size < sizeof(Code)) ||
       !FAT_Read(File, CodeChunk.Offset, &Code, sizeof(Code)))
    {
        return GAME_FILE_DAMAGED;
    }

    if((Code.ImageSize != (CodeChunk.Size - sizeof(Code))) || (Code.ImageSize < sizeof(uint32_t)) ||
       (Code.EntryOffset >= Code.ImageSize) || ((Code.EntryOffset & 1U) == 0U) ||
       (Code.Alignment == 0U) || ((Code.Alignment & (Code.Alignment - 1U)) != 0U))
    {
        return GAME_FILE_DAMAGED;
    }

    if((Code.Alignment > GAME_MEMORY_ALIGNMENT) || (Code.ZeroSize > MemorySize) || (Code.ImageSize > (MemorySize - Code.ZeroSize)))
    {
        return GAME_FILE_TOO_LARGE;
    }

    Result = GameLoader_CheckCrc(File, &Summary.Header);

    if(Result != GAME_FILE_OK)
    {
        return Result;
    }

    if(!FAT_Read(File, CodeChunk.Offset + sizeof(Code), Memory, Code.ImageSize))
    {
        return GAME_FILE_DAMAGED;
    }

    memset(&Memory[Code.ImageSize], 0, Code.ZeroSize);

    if(!GameLoader_Relocate(File, &RelocationChunk, Memory, Code.ImageSize))
    {
        /* Leave nothing that looks runnable behind. */
        memset(Memory, 0, Code.ImageSize);
        return GAME_FILE_DAMAGED;
    }

    GameMemory_PrepareCode(Memory, Code.ImageSize + Code.ZeroSize);

    /* The entry offset carries the Thumb bit, so this is a callable address. */
    Entry = (GameAPI_EntryTypeDef)(uintptr_t)((uintptr_t)Memory + Code.EntryOffset);
    GameAPI_SetGame(Summary.Info.Identifier);
    Loaded = Entry(&GameAPI_Firmware);

    if((Loaded == NULL) ||
       ((const uint8_t *)Loaded < Memory) || ((const uint8_t *)(Loaded + 1) > &Memory[Code.ImageSize + Code.ZeroSize]) ||
       (Loaded->Init == NULL) || (Loaded->Render == NULL) ||
       ((Loaded->Palette != NULL) && (Loaded->PaletteCount > APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT)))
    {
        GameAPI_SetGame(NULL);
        return GAME_FILE_REFUSED;
    }

    *Application = Loaded;
    return GAME_FILE_OK;
}
