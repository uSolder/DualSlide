/**
 * @file game_library.c
 * @brief The games installed on the game disk, for the app manager.
 */

#include "game_library.h"

#include "app_manager.h"
#include "game_loader.h"
#include "open_sans.h"
#include "open_sans_bold.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define GAME_LIBRARY_CARD_MARGIN            (40)
#define GAME_LIBRARY_CARD_TEXT_WIDTH        (APP_MANAGER_SPLASH_SCREEN_WIDTH - (2U * GAME_LIBRARY_CARD_MARGIN))
#define GAME_LIBRARY_CARD_LINE_HEIGHT       (28)
#define GAME_LIBRARY_CARD_MAX_LINES         (4U)
#define GAME_LIBRARY_LINE_SIZE              (GAME_FILE_MESSAGE_SIZE)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static GameLibrary_GameTypeDef GameLibrary_Games[GAME_LIBRARY_MAX_GAMES];
static uint16_t GameLibrary_Count;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool GameLibrary_IsGameFileName(const char *Name)
{
    const char *Dot = strrchr(Name, '.');

    return (Dot != NULL) && (strcmp(Dot + 1, GAME_FILE_EXTENSION) == 0);
}

static char GameLibrary_Lower(char Character)
{
    return ((Character >= 'A') && (Character <= 'Z')) ? (char)(Character - 'A' + 'a') : Character;
}

/* Case-insensitive order of two names. */
static int GameLibrary_CompareNames(const char *First, const char *Second)
{
    while((*First != '\0') && (GameLibrary_Lower(*First) == GameLibrary_Lower(*Second)))
    {
        First++;
        Second++;
    }

    return (int)(unsigned char)GameLibrary_Lower(*First) - (int)(unsigned char)GameLibrary_Lower(*Second);
}

/* Negative, zero or positive as First's game version is older, equal or newer. */
static int GameLibrary_CompareVersions(const GameLibrary_GameTypeDef *First, const GameLibrary_GameTypeDef *Second)
{
    const GameFile_VersionTypeDef *A = &First->Summary.Header.GameVersion;
    const GameFile_VersionTypeDef *B = &Second->Summary.Header.GameVersion;

    if(A->Major != B->Major)
    {
        return (A->Major < B->Major) ? -1 : 1;
    }

    if(A->Minor != B->Minor)
    {
        return (A->Minor < B->Minor) ? -1 : 1;
    }

    return (A->Patch < B->Patch) ? -1 : ((A->Patch > B->Patch) ? 1 : 0);
}

/* Whether Candidate should replace Existing, a copy of the same game. */
static bool GameLibrary_IsBetter(const GameLibrary_GameTypeDef *Candidate, const GameLibrary_GameTypeDef *Existing)
{
    if((Candidate->Result == GAME_FILE_OK) != (Existing->Result == GAME_FILE_OK))
    {
        return Candidate->Result == GAME_FILE_OK;
    }

    return GameLibrary_CompareVersions(Candidate, Existing) > 0;
}

/* Adds a game, or replaces an older copy of it. */
static void GameLibrary_Add(const GameLibrary_GameTypeDef *Game)
{
    uint16_t Index;

    if(Game->Summary.Info.Identifier[0] != '\0')
    {
        for(Index = 0U; Index < GameLibrary_Count; Index++)
        {
            if(strcmp(GameLibrary_Games[Index].Summary.Info.Identifier, Game->Summary.Info.Identifier) == 0)
            {
                if(GameLibrary_IsBetter(Game, &GameLibrary_Games[Index]))
                {
                    GameLibrary_Games[Index] = *Game;
                }

                return;
            }
        }
    }

    if(GameLibrary_Count < GAME_LIBRARY_MAX_GAMES)
    {
        GameLibrary_Games[GameLibrary_Count++] = *Game;
    }
}

static void GameLibrary_Sort(void)
{
    uint16_t Index;

    /* Insertion sort: the list is short. */
    for(Index = 1U; Index < GameLibrary_Count; Index++)
    {
        GameLibrary_GameTypeDef Game = GameLibrary_Games[Index];
        uint16_t Position = Index;

        while((Position > 0U) && (GameLibrary_CompareNames(GameLibrary_Games[Position - 1U].Summary.Info.Name, Game.Summary.Info.Name) > 0))
        {
            GameLibrary_Games[Position] = GameLibrary_Games[Position - 1U];
            Position--;
        }

        GameLibrary_Games[Position] = Game;
    }
}

/*
 * Splits Text into lines no wider than the card, breaking at spaces.
 * Returns the number of lines written.
 */
static uint32_t GameLibrary_WrapText(const Font *FontAsset, const char *Text, char Lines[][GAME_LIBRARY_LINE_SIZE], uint32_t MaximumLines)
{
    uint32_t LineCount = 0U;

    while((*Text != '\0') && (LineCount < MaximumLines))
    {
        char *Line = Lines[LineCount];
        uint32_t Length = 0U;
        uint32_t LastBreak = 0U;

        while(Text[Length] != '\0')
        {
            Line[Length] = Text[Length];
            Line[Length + 1U] = '\0';

            if((Length + 2U) >= GAME_LIBRARY_LINE_SIZE)
            {
                break;
            }

            if(Render_TextWidth(FontAsset, Line) > GAME_LIBRARY_CARD_TEXT_WIDTH)
            {
                break;
            }

            if(Text[Length] == ' ')
            {
                LastBreak = Length;
            }

            Length++;
        }

        if((Text[Length] != '\0') && (LastBreak != 0U))
        {
            Length = LastBreak;
        }

        Line[Length] = '\0';
        Text += Length;

        while(*Text == ' ')
        {
            Text++;
        }

        LineCount++;
    }

    return LineCount;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

uint16_t GameLibrary_Scan(void)
{
    GameLibrary_GameTypeDef Game;
    uint32_t Position = 0U;

    GameLibrary_Count = 0U;

    if(!FAT_Mount())
    {
        return 0U;
    }

    while(FAT_NextFile(&Position, &Game.File))
    {
        /* Hidden files include the "._" copies macOS leaves beside each file. */
        if(((Game.File.Attributes & (FAT_READER_ATTRIBUTE_HIDDEN | FAT_READER_ATTRIBUTE_SYSTEM)) != 0U) ||
           !GameLibrary_IsGameFileName(Game.File.Name))
        {
            continue;
        }

        Game.Result = GameLoader_ReadSummary(&Game.File, &Game.Summary);

        if(Game.Result == GAME_FILE_NOT_A_GAME)
        {
            continue;
        }

        /* A file too damaged to name is shown by its file name. */
        if(Game.Summary.Info.Name[0] == '\0')
        {
            (void)strncpy(Game.Summary.Info.Name, Game.File.Name, sizeof(Game.Summary.Info.Name) - 1U);
        }

        GameLibrary_Add(&Game);
    }

    GameLibrary_Sort();

    return GameLibrary_Count;
}

uint16_t GameLibrary_GetCount(void)
{
    return GameLibrary_Count;
}

GameLibrary_GameTypeDef *GameLibrary_GetGame(uint16_t Index)
{
    return (Index < GameLibrary_Count) ? &GameLibrary_Games[Index] : NULL;
}

void GameLibrary_DrawProblem(const GameLibrary_GameTypeDef *Game, Render_TargetTypeDef *Target)
{
    const int16_t CentreX = (int16_t)(APP_MANAGER_SPLASH_SCREEN_X + (APP_MANAGER_SPLASH_SCREEN_WIDTH / 2U));
    char Title[GAME_FILE_MESSAGE_SIZE];
    char Detail[GAME_FILE_MESSAGE_SIZE];
    char Lines[GAME_LIBRARY_CARD_MAX_LINES][GAME_LIBRARY_LINE_SIZE];
    char NameLine[GAME_LIBRARY_LINE_SIZE];
    const GameFile_VersionTypeDef *Version = &Game->Summary.Header.GameVersion;
    uint32_t LineCount;
    uint32_t Index;
    int16_t Y;

    GameFile_DescribeProblem(Game->Result, &Game->Summary, GameLoader_GetAvailableMemory(), Title, Detail);

    Render_Box(Target, (int16_t)APP_MANAGER_SPLASH_SCREEN_X, (int16_t)APP_MANAGER_SPLASH_SCREEN_Y,
               APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT, RENDER_DARK_GREY);
    Render_Box(Target, (int16_t)APP_MANAGER_SPLASH_SCREEN_X, (int16_t)APP_MANAGER_SPLASH_SCREEN_Y,
               APP_MANAGER_SPLASH_SCREEN_WIDTH, 8U, RENDER_ORANGE);

    /* The game, so the player knows which file this is about. */
    if((Version->Major | Version->Minor | Version->Patch) != 0U)
    {
        (void)Render_FormatText(NameLine, sizeof(NameLine), "%s  %u.%u.%u", Game->Summary.Info.Name,
                                (unsigned int)Version->Major, (unsigned int)Version->Minor, (unsigned int)Version->Patch);
    }
    else
    {
        (void)Render_FormatText(NameLine, sizeof(NameLine), "%s", Game->Summary.Info.Name);
    }

    Render_DrawTextAligned(Target, &OpenSans20, NameLine, CentreX, (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 70), RENDER_ALIGN_CENTRE, RENDER_LIGHT_GREY);
    Render_DrawTextAligned(Target, &OpenSansBold36, Title, CentreX, (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 120), RENDER_ALIGN_CENTRE, RENDER_WHITE);

    LineCount = GameLibrary_WrapText(&OpenSans20, Detail, Lines, GAME_LIBRARY_CARD_MAX_LINES);
    Y = (int16_t)(APP_MANAGER_SPLASH_SCREEN_Y + 190);

    for(Index = 0U; Index < LineCount; Index++)
    {
        Render_DrawTextAligned(Target, &OpenSans20, Lines[Index], CentreX, Y, RENDER_ALIGN_CENTRE, RENDER_WHITE);
        Y = (int16_t)(Y + GAME_LIBRARY_CARD_LINE_HEIGHT);
    }
}
