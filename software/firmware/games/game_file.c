/**
 * @file game_file.c
 * @brief Reads DualSlide game files and decides whether they can run.
 */

#include "game_file.h"

#include "firmware_version.h"
#include "game_api.h"
#include "render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Code in game files must match the CPU and calling convention of the firmware. */
#if defined(__arm__) && defined(__ARM_ARCH_7EM__) && defined(__ARM_PCS_VFP)
#define GAME_FILE_FIRMWARE_ARCHITECTURE     GAME_FILE_ARCHITECTURE_CORTEX_M7_HARD_FLOAT
#else
#define GAME_FILE_FIRMWARE_ARCHITECTURE     (0UL) /* Runs no game files */
#endif

#define GAME_FILE_BYTES_PER_KILOBYTE        (1024U)

static const uint8_t GameFile_Signature[GAME_FILE_SIGNATURE_SIZE] = GAME_FILE_SIGNATURE;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static uint32_t GameFile_CalculateCrc(const uint8_t *Data, uint32_t Size)
{
    return ~GameFile_UpdateCrc(GAME_FILE_CRC_START, Data, Size);
}

/* True when Offset + Size bytes fit within Limit, without overflowing. */
static bool GameFile_Fits(uint32_t Offset, uint32_t Size, uint32_t Limit)
{
    return (Offset <= Limit) && (Size <= (Limit - Offset));
}

/* Negative, zero or positive as Version is older than, equal to or newer than Major.Minor.Patch. */
static int GameFile_CompareVersion(const GameFile_VersionTypeDef *Version, uint16_t Major, uint16_t Minor, uint16_t Patch)
{
    if(Version->Major != Major)
    {
        return (Version->Major < Major) ? -1 : 1;
    }

    if(Version->Minor != Minor)
    {
        return (Version->Minor < Minor) ? -1 : 1;
    }

    if(Version->Patch != Patch)
    {
        return (Version->Patch < Patch) ? -1 : 1;
    }

    return 0;
}

static void GameFile_TerminateText(char *Text, size_t Size)
{
    Text[Size - 1U] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

GameFile_ResultTypeDef GameFile_ReadSummary(const void *Data, uint32_t Size, GameFile_SummaryTypeDef *Summary)
{
    const uint8_t *Bytes = Data;
    GameFile_HeaderTypeDef *Header = &Summary->Header;
    GameFile_ResultTypeDef Result = GAME_FILE_OK;
    bool HasInfo = false;
    bool HasCode = false;
    bool HasRelocations = false;
    uint32_t Index;

    memset(Summary, 0, sizeof(*Summary));

    if((Size < GAME_FILE_SIGNATURE_SIZE) || (memcmp(Bytes, GameFile_Signature, GAME_FILE_SIGNATURE_SIZE) != 0))
    {
        return GAME_FILE_NOT_A_GAME;
    }

    /* Every format keeps the start of the header, up to GameVersion, in place. */
    if(Size < (offsetof(GameFile_HeaderTypeDef, GameVersion) + sizeof(GameFile_VersionTypeDef)))
    {
        return GAME_FILE_DAMAGED;
    }

    memcpy(Header, Bytes, (Size < sizeof(*Header)) ? Size : sizeof(*Header));

    if(Header->FormatMajor > GAME_FILE_FORMAT_MAJOR)
    {
        return GAME_FILE_NEEDS_NEWER_FIRMWARE;
    }

    if((Header->FormatMajor != GAME_FILE_FORMAT_MAJOR) || (Size < sizeof(*Header)) ||
       (Header->HeaderSize < sizeof(*Header)) || !GameFile_Fits(0U, Header->HeaderSize, Header->FileSize) ||
       (Header->ChunkTableOffset > Size) ||
       (Header->ChunkCount > ((Size - Header->ChunkTableOffset) / sizeof(GameFile_ChunkTypeDef))))
    {
        return GAME_FILE_DAMAGED;
    }

    for(Index = 0U; Index < Header->ChunkCount; Index++)
    {
        GameFile_ChunkTypeDef Chunk;

        memcpy(&Chunk, &Bytes[Header->ChunkTableOffset + (Index * sizeof(Chunk))], sizeof(Chunk));

        if(!GameFile_Fits(Chunk.Offset, Chunk.Size, Header->FileSize))
        {
            return GAME_FILE_DAMAGED;
        }

        switch(Chunk.Identifier)
        {
            case GAME_FILE_CHUNK_INFO:
                if((Chunk.Size < sizeof(Summary->Info)) || !GameFile_Fits(Chunk.Offset, sizeof(Summary->Info), Size))
                {
                    return GAME_FILE_DAMAGED;
                }

                memcpy(&Summary->Info, &Bytes[Chunk.Offset], sizeof(Summary->Info));
                GameFile_TerminateText(Summary->Info.Identifier, sizeof(Summary->Info.Identifier));
                GameFile_TerminateText(Summary->Info.Name, sizeof(Summary->Info.Name));
                GameFile_TerminateText(Summary->Info.Author, sizeof(Summary->Info.Author));
                GameFile_TerminateText(Summary->Info.Description, sizeof(Summary->Info.Description));
                HasInfo = true;
                break;

            case GAME_FILE_CHUNK_CODE:
                HasCode = Chunk.Size >= sizeof(GameFile_CodeTypeDef);
                break;

            case GAME_FILE_CHUNK_RELOCATIONS:
                HasRelocations = (Chunk.Size % sizeof(uint32_t)) == 0U;
                break;

            default:
                /* Keep reading, so the player still sees the game's name. */
                if((Chunk.Flags & GAME_FILE_CHUNK_REQUIRED) != 0U)
                {
                    Result = GAME_FILE_NEEDS_NEWER_FIRMWARE;
                }
                break;
        }
    }

    if(!HasInfo || !HasCode || !HasRelocations)
    {
        return GAME_FILE_DAMAGED;
    }

    return Result;
}

GameFile_ResultTypeDef GameFile_CheckCompatibility(const GameFile_SummaryTypeDef *Summary, uint32_t AvailableMemory)
{
    const GameFile_HeaderTypeDef *Header = &Summary->Header;

    if(Header->Architecture != GAME_FILE_FIRMWARE_ARCHITECTURE)
    {
        return GAME_FILE_WRONG_HARDWARE;
    }

    if(Header->ApiMajor < GAME_API_VERSION_MAJOR)
    {
        return GAME_FILE_NEEDS_NEWER_GAME;
    }

    if((Header->ApiMajor > GAME_API_VERSION_MAJOR) || (Header->ApiMinor > GAME_API_VERSION_MINOR))
    {
        return GAME_FILE_NEEDS_NEWER_FIRMWARE;
    }

    if(Header->MemorySize > AvailableMemory)
    {
        return GAME_FILE_TOO_LARGE;
    }

    return GAME_FILE_OK;
}

GameFile_ResultTypeDef GameFile_Verify(const void *Data, uint32_t Size)
{
    const uint8_t *Bytes = Data;
    GameFile_HeaderTypeDef Header;
    const uint32_t CheckedFrom = offsetof(GameFile_HeaderTypeDef, FileCRC) + sizeof(Header.FileCRC);

    if(Size < sizeof(Header))
    {
        return GAME_FILE_DAMAGED;
    }

    memcpy(&Header, Bytes, sizeof(Header));

    if((Header.FileSize != Size) || (GameFile_CalculateCrc(&Bytes[CheckedFrom], Size - CheckedFrom) != Header.FileCRC))
    {
        return GAME_FILE_DAMAGED;
    }

    return GAME_FILE_OK;
}

uint32_t GameFile_UpdateCrc(uint32_t State, const void *Data, uint32_t Size)
{
    const uint8_t *Bytes = Data;
    uint32_t Index;
    uint8_t Bit;

    for(Index = 0U; Index < Size; Index++)
    {
        State ^= Bytes[Index];

        for(Bit = 0U; Bit < 8U; Bit++)
        {
            State = (State >> 1U) ^ ((State & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }

    return State;
}

bool GameFile_FindChunk(const void *Data, uint32_t Size, const GameFile_HeaderTypeDef *Header, uint32_t Identifier, GameFile_ChunkTypeDef *Chunk)
{
    const uint8_t *Bytes = Data;
    uint32_t Index;

    if((Header->ChunkTableOffset > Size) ||
       (Header->ChunkCount > ((Size - Header->ChunkTableOffset) / sizeof(GameFile_ChunkTypeDef))))
    {
        return false;
    }

    for(Index = 0U; Index < Header->ChunkCount; Index++)
    {
        memcpy(Chunk, &Bytes[Header->ChunkTableOffset + (Index * sizeof(*Chunk))], sizeof(*Chunk));

        if(Chunk->Identifier == Identifier)
        {
            return true;
        }
    }

    return false;
}

void GameFile_DescribeProblem(GameFile_ResultTypeDef Result, const GameFile_SummaryTypeDef *Summary, uint32_t AvailableMemory,
                              char Title[GAME_FILE_MESSAGE_SIZE], char Detail[GAME_FILE_MESSAGE_SIZE])
{
    const char *Name = ((Summary != NULL) && (Summary->Info.Name[0] != '\0')) ? Summary->Info.Name : "This game";

    Title[0] = '\0';
    Detail[0] = '\0';

    switch(Result)
    {
        case GAME_FILE_OK:
            break;

        case GAME_FILE_NOT_A_GAME:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Not a game");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "This file isn't a DualSlide game.");
            break;

        case GAME_FILE_DAMAGED:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Damaged game file");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "Copy the game file onto DualSlide again.");
            break;

        case GAME_FILE_NEEDS_NEWER_FIRMWARE:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Update needed");

            /* Quote the version only when it really is newer than this one. */
            if((Summary != NULL) &&
               (GameFile_CompareVersion(&Summary->Header.MinimumFirmware, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH) > 0))
            {
                (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s needs DualSlide firmware %u.%u.%u or newer. This DualSlide has %u.%u.%u.",
                                        Name,
                                        (unsigned int)Summary->Header.MinimumFirmware.Major,
                                        (unsigned int)Summary->Header.MinimumFirmware.Minor,
                                        (unsigned int)Summary->Header.MinimumFirmware.Patch,
                                        FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
            }
            else
            {
                (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s needs newer DualSlide firmware.", Name);
            }
            break;

        case GAME_FILE_NEEDS_NEWER_GAME:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Game update needed");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s was made for older DualSlide firmware. Get its latest version.", Name);
            break;

        case GAME_FILE_WRONG_HARDWARE:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Not for this DualSlide");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s was made for different DualSlide hardware.", Name);
            break;

        case GAME_FILE_TOO_LARGE:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Too large");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s needs %u KB of memory. This DualSlide has %u KB for games.",
                                    Name,
                                    (unsigned int)(((Summary != NULL) ? Summary->Header.MemorySize : 0U) + GAME_FILE_BYTES_PER_KILOBYTE - 1U) / GAME_FILE_BYTES_PER_KILOBYTE,
                                    (unsigned int)(AvailableMemory / GAME_FILE_BYTES_PER_KILOBYTE));
            break;

        case GAME_FILE_REFUSED:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Can't start game");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s didn't start on this DualSlide. Get its latest version.", Name);
            break;

        default:
            (void)Render_FormatText(Title, GAME_FILE_MESSAGE_SIZE, "Can't open game");
            (void)Render_FormatText(Detail, GAME_FILE_MESSAGE_SIZE, "%s can't run on this DualSlide.", Name);
            break;
    }
}
