/**
 * @file tanks_record.c
 * @brief Best-round record and the callsign of the player who set it.
 *
 * The record is the highest round reached, kept in persistent storage with a
 * three-letter callsign. When a game ends beyond the record, the player picks
 * each letter with the right slider, sets it with primary, and steps back
 * with secondary. Letters set on button release, so the release that sets
 * the last letter does not also start a new game.
 */

#include "tanks_internal.h"

#include "controls.h"
#include "save.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define TANKS_RECORD_SAVE_NAME        "TKRW"
#define TANKS_RECORD_VERSION          (1U)
#define TANKS_RECORD_MAXIMUM_WAVE     (99U)
#define TANKS_CALLSIGN_LETTERS        (26U)
#define TANKS_CALLSIGN_HYSTERESIS     (600)
#define TANKS_CALLSIGN_EMPTY          ('-')

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    uint32_t Version;
    uint32_t BestWave;
    char Callsign[TANKS_CALLSIGN_LENGTH + 1U];
} Tanks_RecordSaveTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const char Tanks_NoCallsign[TANKS_CALLSIGN_LENGTH + 1U] = "---";

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Tanks_CopyCallsign(char *Destination, const char *Source)
{
    for(uint8_t Index = 0U; Index < TANKS_CALLSIGN_LENGTH; Index++)
    {
        Destination[Index] = Source[Index];
    }
    Destination[TANKS_CALLSIGN_LENGTH] = '\0';
}

static bool Tanks_CallsignIsValid(const char *Callsign)
{
    for(uint8_t Index = 0U; Index < TANKS_CALLSIGN_LENGTH; Index++)
    {
        if(((Callsign[Index] < 'A') || (Callsign[Index] > 'Z')) && (Callsign[Index] != TANKS_CALLSIGN_EMPTY))
        {
            return false;
        }
    }
    return true;
}

static void Tanks_SaveRecord(void)
{
    Tanks_RecordSaveTypeDef Save =
    {
        .Version = TANKS_RECORD_VERSION,
        .BestWave = Tanks_Game.Record.BestWave
    };
    Tanks_CopyCallsign(Save.Callsign, Tanks_Game.Record.BestCallsign);
    (void)Save_Store(TANKS_RECORD_SAVE_NAME, &Save, sizeof(Save));
}

/*
 * Slider position to a letter, A at the top of its travel and Z at the bottom.
 * The current letter holds until the slider is clearly past its edges.
 */
static uint8_t Tanks_SliderToLetter(int32_t Value, uint8_t Current)
{
    const int32_t Span = (CONTROLS_SLIDER_RAW_MAXIMUM + 1) / (int32_t)TANKS_CALLSIGN_LETTERS;
    const int32_t Clamped = Tanks_Clamp32(Value, 0, CONTROLS_SLIDER_RAW_MAXIMUM);
    int32_t Letter = Clamped / Span;
    if(Letter >= (int32_t)TANKS_CALLSIGN_LETTERS)
    {
        Letter = (int32_t)TANKS_CALLSIGN_LETTERS - 1;
    }
    if((Letter != (int32_t)Current) &&
       (Clamped > (((int32_t)Current * Span) - TANKS_CALLSIGN_HYSTERESIS)) &&
       (Clamped < ((((int32_t)Current + 1) * Span) + TANKS_CALLSIGN_HYSTERESIS)))
    {
        return Current;
    }
    return (uint8_t)Letter;
}

/* The record is the round reached, capped to what the medal can show. */
static uint16_t Tanks_RecordWave(void)
{
    return (uint16_t)Tanks_Clamp32(Tanks_Game.Wave, 0, TANKS_RECORD_MAXIMUM_WAVE);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Tanks_LoadRecord(void)
{
    Tanks_RecordTypeDef *Record = &Tanks_Game.Record;
    Tanks_RecordSaveTypeDef Save;
    Record->BestWave = 0U;
    Record->Entering = false;
    Tanks_CopyCallsign(Record->BestCallsign, Tanks_NoCallsign);
    if(Save_Load(TANKS_RECORD_SAVE_NAME, &Save, sizeof(Save)) &&
       (Save.Version == TANKS_RECORD_VERSION) && (Save.BestWave <= TANKS_RECORD_MAXIMUM_WAVE) && Tanks_CallsignIsValid(Save.Callsign))
    {
        Record->BestWave = (uint16_t)Save.BestWave;
        Tanks_CopyCallsign(Record->BestCallsign, Save.Callsign);
    }
}

void Tanks_CheckRecord(void)
{
    Tanks_RecordTypeDef *Record = &Tanks_Game.Record;
    if(Tanks_RecordWave() <= Record->BestWave)
    {
        return;
    }
    Record->Entering = true;
    Record->Index = 0U;
    Record->Letter = 0U;
    Tanks_CopyCallsign(Record->Callsign, Tanks_NoCallsign);
    Record->Letter = Tanks_SliderToLetter(Controls_SliderRaw(CONTROLS_RIGHT_SLIDER), 0U);

    /* A button held when the game ended (still firing, say) must not set a letter as it is let go. */
    Controls_IgnoreHeldButtons();
}

void Tanks_UpdateRecordEntry(void)
{
    Tanks_RecordTypeDef *Record = &Tanks_Game.Record;
    const bool Primary = Controls_WasReleased(CONTROLS_PRIMARY);
    const bool Secondary = Controls_WasReleased(CONTROLS_SECONDARY);
    Record->Letter = Tanks_SliderToLetter(Controls_SliderRaw(CONTROLS_RIGHT_SLIDER), Record->Letter);
    if(Primary)
    {
        Record->Callsign[Record->Index] = (char)('A' + Record->Letter);
        Record->Index++;
        if(Record->Index >= TANKS_CALLSIGN_LENGTH)
        {
            Record->BestWave = Tanks_RecordWave();
            Tanks_CopyCallsign(Record->BestCallsign, Record->Callsign);
            Record->Entering = false;
            Tanks_SaveRecord();
        }
    }
    else if(Secondary && (Record->Index > 0U))
    {
        Record->Index--;
        Record->Callsign[Record->Index] = TANKS_CALLSIGN_EMPTY;
    }
}

void Tanks_SaveUnclaimedRecord(void)
{
    Tanks_RecordTypeDef *Record = &Tanks_Game.Record;
    if((Tanks_Game.Screen == TANKS_SCREEN_TITLE) || (Tanks_RecordWave() <= Record->BestWave))
    {
        return;
    }

    /* Leaving mid-campaign or mid-entry keeps the round reached, with any letters set so far. */
    Record->BestWave = Tanks_RecordWave();
    Tanks_CopyCallsign(Record->BestCallsign, Record->Entering ? Record->Callsign : Tanks_NoCallsign);
    Record->Entering = false;
    Tanks_SaveRecord();
}
