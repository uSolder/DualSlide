/**
 * @file game_file.h
 * @brief DualSlide game files (.dsg): layout, compatibility and messages.
 *
 * A game file holds one game, built separately from the firmware. Players
 * install it by copying it onto the DualSlide drive.
 *
 * LAYOUT
 *
 *   All numbers are little-endian. A file is a fixed header, a chunk table,
 *   then the chunks themselves:
 *
 *   GameFile_HeaderTypeDef   signature, sizes, CRC, and everything needed to
 *                            decide whether this DualSlide can run the game
 *   GameFile_ChunkTypeDef[]  ChunkCount entries at ChunkTableOffset
 *   chunks                   at the offsets the table gives
 *
 *   The build tool places the INFO chunk directly after the chunk table, so
 *   the first GAME_FILE_SUMMARY_BYTES of a file describe the game fully.
 *
 * CHUNKS
 *
 *   INFO  Required. GameFile_InfoTypeDef: identifier, name, author and
 *         description. The launcher shows only the name; the rest is there
 *         for future use, such as an information page.
 *   CODE  Required. GameFile_CodeTypeDef followed by the load image (code,
 *         read-only data and initialised data), linked at address zero.
 *   RELO  Required. Relocations to apply once the image is placed in memory:
 *         an array of uint32_t entries, each GAME_FILE_RELOCATION_* in the top
 *         four bits and a byte offset into the image in the rest.
 *
 *   Readers skip chunks they do not recognise, unless GAME_FILE_CHUNK_REQUIRED
 *   is set, in which case the game needs a newer DualSlide. New kinds of
 *   chunk can therefore be added without breaking older firmware.
 *
 * VERSIONS
 *
 *   FormatMajor/FormatMinor  layout of the file itself. A reader rejects a
 *                            different major and ignores a newer minor.
 *   ApiMajor/ApiMinor        game API the game was built for (game_api.h).
 *   MinimumFirmware          first firmware that can run everything in the
 *                            file, quoted to the player when an update is
 *                            needed.
 *   GameVersion              the game's own version, shown to the player.
 *
 *   Every format version keeps the header up to and including GameVersion
 *   where it is now, so any firmware can name the update a newer file needs.
 */

#ifndef GAME_FILE_H
#define GAME_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Format definitions                                                         */
/* -------------------------------------------------------------------------- */

/** File name extension, as FAT stores it. */
#define GAME_FILE_EXTENSION                 "DSG"

/** Signature: catches files that are not game files, and text-mode copies. */
#define GAME_FILE_SIGNATURE                 { 0x89U, 'D', 'S', 'G', '\r', '\n', 0x1AU, '\n' }
#define GAME_FILE_SIGNATURE_SIZE            (8U)

/** Version of the layout described here. */
#define GAME_FILE_FORMAT_MAJOR              (1U)
#define GAME_FILE_FORMAT_MINOR              (0U)

/** CPU and calling convention of the code: ARMv7E-M, Thumb, FPv5-D16, hard float. */
#define GAME_FILE_ARCHITECTURE_CORTEX_M7_HARD_FLOAT (0x4648374DUL) /* "M7HF" */

/** Chunk identifiers. */
#define GAME_FILE_CHUNK_INFO                (0x4F464E49UL) /* "INFO" */
#define GAME_FILE_CHUNK_CODE                (0x45444F43UL) /* "CODE" */
#define GAME_FILE_CHUNK_RELOCATIONS         (0x4F4C4552UL) /* "RELO" */

/** Chunk flag: a reader that does not understand this chunk must not run the game. */
#define GAME_FILE_CHUNK_REQUIRED            (0x00000001UL)

/** Relocation types, in the top four bits of a relocation entry. */
#define GAME_FILE_RELOCATION_TYPE_SHIFT     (28U)
#define GAME_FILE_RELOCATION_OFFSET_MASK    (0x0FFFFFFFUL)
#define GAME_FILE_RELOCATION_ABSOLUTE_32    (0U)  /* Add the load address to a 32-bit word */

/** Bytes from the start of a file that GameFile_ReadSummary() needs. */
#define GAME_FILE_SUMMARY_BYTES             (1024U)

/** Longest texts in the INFO chunk, including the terminating zero. */
#define GAME_FILE_IDENTIFIER_SIZE           (48U)
#define GAME_FILE_NAME_SIZE                 (32U)
#define GAME_FILE_AUTHOR_SIZE               (32U)
#define GAME_FILE_DESCRIPTION_SIZE          (128U)

/** Longest message GameFile_DescribeProblem() writes, including the terminating zero. */
#define GAME_FILE_MESSAGE_SIZE              (128U)

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief A MAJOR.MINOR.PATCH version.
 */
typedef struct
{
    uint16_t Major;
    uint16_t Minor;
    uint16_t Patch;
    uint16_t Reserved;
} GameFile_VersionTypeDef;

/**
 * @brief Fixed header at the start of every game file.
 */
typedef struct
{
    uint8_t Signature[GAME_FILE_SIGNATURE_SIZE];
    uint16_t FormatMajor;
    uint16_t FormatMinor;
    uint32_t HeaderSize;                    /* Bytes; later minor versions may add members */
    uint32_t FileSize;                      /* Bytes, the whole file */
    uint32_t FileCRC;                       /* CRC-32 of every byte after this member */
    uint32_t Architecture;                  /* GAME_FILE_ARCHITECTURE_* */
    uint16_t ApiMajor;
    uint16_t ApiMinor;
    GameFile_VersionTypeDef MinimumFirmware;
    GameFile_VersionTypeDef GameVersion;
    uint32_t ChunkTableOffset;
    uint32_t ChunkCount;
    uint32_t MemorySize;                    /* Bytes the game occupies once loaded: image and zeroed memory */
    uint32_t Reserved[3];
} GameFile_HeaderTypeDef;

/**
 * @brief One entry of the chunk table.
 */
typedef struct
{
    uint32_t Identifier;                    /* GAME_FILE_CHUNK_* */
    uint32_t Flags;                         /* GAME_FILE_CHUNK_REQUIRED */
    uint32_t Offset;                        /* Bytes from the start of the file */
    uint32_t Size;                          /* Bytes */
} GameFile_ChunkTypeDef;

/**
 * @brief The INFO chunk. Texts are UTF-8 and zero-terminated.
 */
typedef struct
{
    char Identifier[GAME_FILE_IDENTIFIER_SIZE];   /* Unique and permanent, such as "com.example.canal-tug" */
    char Name[GAME_FILE_NAME_SIZE];               /* Shown in the launcher */
    char Author[GAME_FILE_AUTHOR_SIZE];
    char Description[GAME_FILE_DESCRIPTION_SIZE];
} GameFile_InfoTypeDef;

/**
 * @brief Start of the CODE chunk; the load image follows it.
 */
typedef struct
{
    uint32_t ImageSize;                     /* Bytes of load image after this structure */
    uint32_t ZeroSize;                      /* Bytes of zeroed memory (.bss) after the image */
    uint32_t EntryOffset;                   /* GameAPI_EntryTypeDef, as an offset into the image (Thumb bit set) */
    uint32_t Alignment;                     /* Bytes the image's load address must be a multiple of */
} GameFile_CodeTypeDef;

/**
 * @brief Outcome of reading or checking a game file, from the player's view.
 */
typedef enum
{
    GAME_FILE_OK = 0,
    GAME_FILE_NOT_A_GAME,                   /* No game file signature */
    GAME_FILE_DAMAGED,                      /* Truncated, inconsistent, or the CRC fails */
    GAME_FILE_NEEDS_NEWER_FIRMWARE,         /* Newer file format, API or required chunk */
    GAME_FILE_NEEDS_NEWER_GAME,             /* Built for an older API major version */
    GAME_FILE_WRONG_HARDWARE,               /* Code for a different CPU */
    GAME_FILE_TOO_LARGE                     /* Needs more memory than this DualSlide has */
} GameFile_ResultTypeDef;

/**
 * @brief What the launcher needs to list a game and decide whether it can run.
 */
typedef struct
{
    GameFile_HeaderTypeDef Header;
    GameFile_InfoTypeDef Info;
} GameFile_SummaryTypeDef;

_Static_assert(sizeof(GameFile_VersionTypeDef) == 8U, "Game file layout changed");
_Static_assert(sizeof(GameFile_HeaderTypeDef) == 72U, "Game file layout changed");
_Static_assert(offsetof(GameFile_HeaderTypeDef, FileCRC) == 20U, "Game file layout changed");
_Static_assert(offsetof(GameFile_HeaderTypeDef, GameVersion) == 40U, "Game file layout changed");
_Static_assert(sizeof(GameFile_ChunkTypeDef) == 16U, "Game file layout changed");
_Static_assert(sizeof(GameFile_InfoTypeDef) == 240U, "Game file layout changed");
_Static_assert(sizeof(GameFile_CodeTypeDef) == 16U, "Game file layout changed");

/* -------------------------------------------------------------------------- */
/* Functions                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read a game's header, chunk table and INFO chunk.
 *
 * Only the start of the file is needed, so the launcher can list games
 * without reading them whole. The CRC is not checked here.
 *
 * @param Data    The first bytes of the file.
 * @param Size    Number of bytes in Data; GAME_FILE_SUMMARY_BYTES is enough.
 * @param Summary Receives what was read. Valid unless GAME_FILE_NOT_A_GAME or
 *                GAME_FILE_DAMAGED is returned, so the launcher can show the
 *                game's name beside any other problem.
 *
 * @return GAME_FILE_OK, GAME_FILE_NOT_A_GAME, GAME_FILE_DAMAGED, or
 *         GAME_FILE_NEEDS_NEWER_FIRMWARE for a newer layout or required chunk.
 *         Call GameFile_CheckCompatibility() next.
 */
GameFile_ResultTypeDef GameFile_ReadSummary(const void *Data, uint32_t Size, GameFile_SummaryTypeDef *Summary);

/**
 * @brief Decide whether this firmware can run the game.
 *
 * @param Summary         From GameFile_ReadSummary().
 * @param AvailableMemory Bytes available for a loaded game.
 *
 * @return GAME_FILE_OK or the compatibility problem.
 */
GameFile_ResultTypeDef GameFile_CheckCompatibility(const GameFile_SummaryTypeDef *Summary, uint32_t AvailableMemory);

/**
 * @brief Check a whole game file's size and CRC.
 *
 * @return GAME_FILE_OK or GAME_FILE_DAMAGED.
 */
GameFile_ResultTypeDef GameFile_Verify(const void *Data, uint32_t Size);

/**
 * @brief Explain a problem to the player.
 *
 * @param Result          The problem.
 * @param Summary         The game, or NULL when nothing could be read.
 * @param AvailableMemory Bytes available for a loaded game.
 * @param Title           Receives a short heading, such as "Update needed".
 * @param Detail          Receives a sentence or two saying what to do.
 */
void GameFile_DescribeProblem(GameFile_ResultTypeDef Result, const GameFile_SummaryTypeDef *Summary, uint32_t AvailableMemory,
                              char Title[GAME_FILE_MESSAGE_SIZE], char Detail[GAME_FILE_MESSAGE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GAME_FILE_H */
