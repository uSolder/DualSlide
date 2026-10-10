/**
 * @file tug_world.c
 * @brief Canal Tug's moving parts: the tug, the cargo on its tow rope, the
 *        traffic in the villages and the camera.
 */

#include "tug_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* How quickly the engines answer the throttles: full range in about a third of a second. */
#define ENGINE_RESPONSE         (6.0f)
#define ENGINE_ASTERN           (0.65f)

/* Turning from the two engines pulling differently (raised with TUG_DRAG_SPIN, so the turning speed stays the same). */
#define ENGINE_TURN             (0.107f)

/*
 * Steering follows a curve: STEER_LINEAR_SHARE of it is straight in
 * proportion to how differently the engines pull, and the rest grows with
 * the square of it. Small differences steer gently, so holding a course is
 * easy, while a full difference (one engine full ahead, the other full
 * astern) still turns as hard as ever.
 */
#define STEER_LINEAR_SHARE      (0.5f)

/* Cargo thrusters swing the cargo round this hard (radians per second per second, divided by the square root of its mass). */
#define CARGO_THRUSTER_TURN     (4.0f)

/*
 * Finished map rows kept at hand, each in the slot its row number picks:
 * enough for every row on screen at once, so scrolling rarely builds any.
 * Building a row reads the stored rows up to DEPTH_REACH above and below,
 * which are kept in a smaller cache of their own.
 */
#define ROW_CACHE_SIZE          (32U)
#define STORED_CACHE_SIZE       (8U)
#define DEPTH_REACH             (3)

/* The patchwork of fields: patches about this many tiles across, jittered by up to this much. */
#define FIELD_WIDTH             (26)
#define FIELD_HEIGHT            (22)
#define FIELD_JITTER_X          (7)
#define FIELD_JITTER_Y          (6)
#define HEDGE_THICKNESS         (1.1f)

/*
 * Water drag on the tug and on cargo: little along the keel, a lot sideways,
 * and a lot against spinning, since a hull shaped to go forwards resists
 * turning: a turn stops almost as soon as the steering does.
 */
#define TUG_DRAG_AHEAD          (0.8f)
#define TUG_DRAG_SIDEWAYS       (6.0f)
#define TUG_DRAG_SPIN           (6.0f)
#define CARGO_DRAG_AHEAD        (0.55f)
#define CARGO_DRAG_SIDEWAYS     (3.0f)
#define CARGO_DRAG_SPIN         (6.0f)

/* In the shallows the drag grows by up to this many times, the more of the hull is aground. */
#define SHALLOWS_DRAG           (4.0f)

/*
 * Unhooked cargo slower than this (pixels per second, and radians per second
 * of spin) has come to rest: it is stopped dead and left alone until the tug
 * hooks it or bumps it.
 */
#define CARGO_REST_SPEED        (1.0f)
#define CARGO_REST_SPIN         (0.01f)

/* The wind pushes this much less along a keel than across it, and the low tug catches this share of what cargo does. */
#define WIND_ALONG_SHARE        (0.25f)
#define WIND_TUG_SHARE          (0.5f)

/* Weather covers leave the wind this share of its grip on the cargo. */
#define WIND_COVERED_SHARE      (0.5f)

/* The tow rope, from the tug's stern to the cargo's bow. */
#define ROPE_LENGTH             (56.0f)

/*
 * The rope gives a little: each step it takes up only this share of its
 * stretch, and of the speed pulling the boats apart, so it tightens over a
 * moment instead of snapping and yanking the cargo round in tight turns.
 */
#define ROPE_TAKE_UP            (0.15f)

/*
 * The rope is tied to the tug's stern, so a pull from one side turns the
 * tug as well as dragging it: the stern swings towards the pull and the bow
 * away, until the tug points straight along the rope. Then it drifts across,
 * still in line, until tug, rope and cargo are straight. 1 is a plain
 * plank's worth of turning for its length; less is gentler.
 */
#define TUG_ROPE_TURN           (0.5f)

/* The same for the cargo, whose bow the rope is tied to: a long cargo turns slowly. */
#define CARGO_ROPE_TURN         (2.0f)

/* Knocks bounce back this much of the speed lost. */
#define KNOCK_BOUNCE            (0.3f)

/* The tug's fenders soften the knocks between it and its cargo to this share. */
#define FENDER_SOFTENING        (0.5f)

/* A buoy's radius, as an obstacle. */
#define BUOY_RADIUS             (7.0f)

/* The camera looks ahead of the tug and catches up this quickly. */
#define CAMERA_LEAD             (0.6f)
#define CAMERA_CATCH_UP         (3.0f)

/* Foam behind the tug while its engines work. */
#define WAKE_INTERVAL_MS        (40U)

#define PI_F                    (3.14159265f)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static uint32_t Tug_RandomState = 0x6A09E667U;

/* Finished rows, and stored rows decoded from their runs; the row number plus one in each slot, 0 when empty. */
static char Tug_RowCache[ROW_CACHE_SIZE][TUG_MAP_WIDTH];
static int16_t Tug_RowCacheRow[ROW_CACHE_SIZE];
static char Tug_StoredCache[STORED_CACHE_SIZE][TUG_MAP_WIDTH];
static int16_t Tug_StoredCacheRow[STORED_CACHE_SIZE];

/* What lies off the edges of the map: open country. */
static char Tug_OpenCountry[TUG_MAP_WIDTH];
static bool Tug_OpenCountryReady = false;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static uint32_t Tug_Random(void)
{
    Tug_RandomState = (Tug_RandomState * 1664525U) + 1013904223U;
    return Tug_RandomState >> 8;
}

static float Tug_Clamp(float Value, float Minimum, float Maximum)
{
    return (Value < Minimum) ? Minimum : ((Value > Maximum) ? Maximum : Value);
}

static float Tug_WrapAngle(float Angle)
{
    while(Angle > PI_F)
    {
        Angle -= 2.0f * PI_F;
    }
    while(Angle < -PI_F)
    {
        Angle += 2.0f * PI_F;
    }
    return Angle;
}

/* A stored row of the map, decoded from its runs; off the map, open country. */
static const char *Tug_StoredRow(int16_t TileY)
{
    uint16_t Slot;
    char *Row;
    uint32_t Run;
    uint16_t X = 0U;

    if((TileY < 0) || (TileY >= TUG_MAP_HEIGHT))
    {
        if(!Tug_OpenCountryReady)
        {
            memset(Tug_OpenCountry, TUG_TILE_MEADOW, sizeof(Tug_OpenCountry));
            Tug_OpenCountryReady = true;
        }
        return Tug_OpenCountry;
    }
    Slot = (uint16_t)((uint16_t)TileY % STORED_CACHE_SIZE);
    Row = Tug_StoredCache[Slot];
    if(Tug_StoredCacheRow[Slot] == (int16_t)(TileY + 1))
    {
        return Row;
    }
    Run = Tug_MapRowStart[TileY];
    while((Run < Tug_MapRowStart[TileY + 1]) && (X < TUG_MAP_WIDTH))
    {
        uint16_t Count = Tug_MapRuns[Run];
        if((X + Count) > TUG_MAP_WIDTH)
        {
            Count = (uint16_t)(TUG_MAP_WIDTH - X);
        }
        memset(&Row[X], (int)Tug_MapRuns[Run + 1U], Count);
        X = (uint16_t)(X + Count);
        Run += 2U;
    }
    Tug_StoredCacheRow[Slot] = (int16_t)(TileY + 1);
    return Row;
}

static bool Tug_StoredIsWater(const char *Row, int16_t TileX)
{
    if((TileX < 0) || (TileX >= TUG_MAP_WIDTH))
    {
        return false;
    }
    return (Row[TileX] == TUG_TILE_STORED_WATER) || (Row[TileX] == TUG_TILE_SHALLOWS) || (Row[TileX] == TUG_TILE_BUOY);
}

static uint32_t Tug_FieldHash(int32_t A, int32_t B)
{
    uint32_t Hash = ((uint32_t)A * 0x9E3779B1U) ^ ((uint32_t)B * 0x85EBCA77U) ^ 0x27D4EB2FU;
    Hash ^= Hash >> 15;
    Hash *= 0x2C1B3C6DU;
    Hash ^= Hash >> 12;
    return Hash;
}

/*
 * The country between the waterways: a patchwork of fields, each meadow,
 * ploughed, wheat or wood, with hedgerows along their borders. The same
 * place always gives the same field.
 */
static char Tug_FieldAt(int16_t TileX, int16_t TileY)
{
    static const char Crops[10] = { 'G', 'G', 'F', 'F', 'W', 'W', 'T', 'G', 'F', 'T' };
    const int32_t CellX = TileX / FIELD_WIDTH;
    const int32_t CellY = TileY / FIELD_HEIGHT;
    float Best = 1.0e9f;
    float Second = 1.0e9f;
    char Crop = TUG_TILE_MEADOW;

    for(int32_t OffsetY = -1; OffsetY <= 1; OffsetY++)
    {
        for(int32_t OffsetX = -1; OffsetX <= 1; OffsetX++)
        {
            const uint32_t Hash = Tug_FieldHash(CellX + OffsetX, CellY + OffsetY);
            const float CentreX = (float)(((CellX + OffsetX) * FIELD_WIDTH) + (int32_t)(Hash % (2U * FIELD_JITTER_X + 1U)) - FIELD_JITTER_X);
            const float CentreY = (float)(((CellY + OffsetY) * FIELD_HEIGHT) + (int32_t)((Hash >> 8) % (2U * FIELD_JITTER_Y + 1U)) - FIELD_JITTER_Y);
            const float DeltaX = (float)TileX - CentreX;
            const float DeltaY = ((float)TileY - CentreY) * 1.2f;
            const float Distance = sqrtf((DeltaX * DeltaX) + (DeltaY * DeltaY));
            if(Distance < Best)
            {
                Second = Best;
                Best = Distance;
                Crop = Crops[(Hash >> 16) % 10U];
            }
            else if(Distance < Second)
            {
                Second = Distance;
            }
        }
    }
    return ((Second - Best) < HEDGE_THICKNESS) ? TUG_TILE_HEDGE : Crop;
}

/*
 * Finish a row from the stored map: water gets its depth (how far the bank
 * is, up to DEPTH_REACH tiles), open country beside the water becomes
 * towpath, and the rest of the open country becomes fields.
 */
static void Tug_BuildRow(int16_t TileY, char *Row)
{
    const char *Stored[(2 * DEPTH_REACH) + 1];
    for(int16_t Offset = -DEPTH_REACH; Offset <= DEPTH_REACH; Offset++)
    {
        Stored[Offset + DEPTH_REACH] = Tug_StoredRow((int16_t)(TileY + Offset));
    }

    for(int16_t X = 0; X < TUG_MAP_WIDTH; X++)
    {
        const char Tile = Stored[DEPTH_REACH][X];
        if(Tile == TUG_TILE_STORED_WATER)
        {
            uint8_t Depth = (uint8_t)DEPTH_REACH;
            for(int16_t Reach = 1; (Reach <= DEPTH_REACH) && (Depth == (uint8_t)DEPTH_REACH); Reach++)
            {
                for(int16_t OffsetY = -Reach; (OffsetY <= Reach) && (Depth == (uint8_t)DEPTH_REACH); OffsetY++)
                {
                    for(int16_t OffsetX = -Reach; OffsetX <= Reach; OffsetX++)
                    {
                        if(!Tug_StoredIsWater(Stored[DEPTH_REACH + OffsetY], (int16_t)(X + OffsetX)))
                        {
                            Depth = (uint8_t)(Reach - 1);
                            break;
                        }
                    }
                }
            }
            Row[X] = (char)(TUG_TILE_WATER_FIRST + Depth);
        }
        else if(Tile == TUG_TILE_MEADOW)
        {
            bool Bank = false;
            for(int16_t OffsetY = -1; (OffsetY <= 1) && !Bank; OffsetY++)
            {
                for(int16_t OffsetX = -1; OffsetX <= 1; OffsetX++)
                {
                    if(Tug_StoredIsWater(Stored[DEPTH_REACH + OffsetY], (int16_t)(X + OffsetX)))
                    {
                        Bank = true;
                        break;
                    }
                }
            }
            Row[X] = Bank ? TUG_TILE_TOWPATH : Tug_FieldAt(X, TileY);
        }
        else
        {
            Row[X] = Tile;
        }
    }
}

char Tug_TileAt(int16_t TileX, int16_t TileY)
{
    uint16_t Slot;

    if((TileX < 0) || (TileY < 0) || (TileX >= TUG_MAP_WIDTH) || (TileY >= TUG_MAP_HEIGHT))
    {
        return TUG_TILE_MEADOW;
    }
    Slot = (uint16_t)((uint16_t)TileY % ROW_CACHE_SIZE);
    if(Tug_RowCacheRow[Slot] != (int16_t)(TileY + 1))
    {
        Tug_BuildRow(TileY, Tug_RowCache[Slot]);
        Tug_RowCacheRow[Slot] = (int16_t)(TileY + 1);
    }
    return Tug_RowCache[Slot][TileX];
}

bool Tug_TileIsWater(char Tile)
{
    return ((Tile >= TUG_TILE_WATER_FIRST) && (Tile <= TUG_TILE_WATER_LAST)) || (Tile == TUG_TILE_SHALLOWS) || (Tile == TUG_TILE_BUOY);
}

/* How far a water tile is from the bank: 0 beside it, up to 3. */
uint8_t Tug_WaterDepth(int16_t TileX, int16_t TileY)
{
    const char Tile = Tug_TileAt(TileX, TileY);
    return ((Tile >= TUG_TILE_WATER_FIRST) && (Tile <= TUG_TILE_WATER_LAST)) ? (uint8_t)(Tile - TUG_TILE_WATER_FIRST) : 0U;
}

void Tug_DockPosition(uint8_t Dock, float *X, float *Y)
{
    *X = (float)((Tug_Docks[Dock].X * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2));
    *Y = (float)((Tug_Docks[Dock].Y * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2));
}

/* A point on a body, Ahead along its keel and Right across it. */
void Tug_LocalPoint(const Tug_BodyTypeDef *Body, float Ahead, float Right, float *X, float *Y)
{
    const float Sine = sinf(Body->Heading);
    const float Cosine = cosf(Body->Heading);
    *X = Body->X + (Ahead * Sine) + (Right * Cosine);
    *Y = Body->Y - (Ahead * Cosine) + (Right * Sine);
}

/*
 * Where the tow rope goes on the cargo: either end will do until it's
 * hooked, so this is whichever end is nearer the tug's stern. Returns +1
 * for the bow and -1 for the stern, and sets that end's position.
 */
float Tug_HookEnd(const Tug_WorldTypeDef *World, float *X, float *Y)
{
    const Tug_BodyTypeDef *Cargo = &World->Cargo.Body;
    float SternX;
    float SternY;
    float BowX;
    float BowY;
    float AftX;
    float AftY;

    Tug_LocalPoint(&World->Tug.Body, -World->Tug.Body.Length / 2.0f, 0.0f, &SternX, &SternY);
    Tug_LocalPoint(Cargo, Cargo->Length / 2.0f, 0.0f, &BowX, &BowY);
    Tug_LocalPoint(Cargo, -Cargo->Length / 2.0f, 0.0f, &AftX, &AftY);
    if(World->Cargo.Hooked ||
       ((((SternX - BowX) * (SternX - BowX)) + ((SternY - BowY) * (SternY - BowY))) <= (((SternX - AftX) * (SternX - AftX)) + ((SternY - AftY) * (SternY - AftY)))))
    {
        *X = BowX;
        *Y = BowY;
        return 1.0f;
    }
    *X = AftX;
    *Y = AftY;
    return -1.0f;
}

float Tug_Speed(const Tug_BodyTypeDef *Body)
{
    return sqrtf((Body->VelocityX * Body->VelocityX) + (Body->VelocityY * Body->VelocityY));
}

/* -------------------------------------------------------------------------- */
/* Boats                                                                      */
/* -------------------------------------------------------------------------- */

/* Whether a point lies within Margin pixels of a body's outline. */
static bool Tug_PointInBody(const Tug_BodyTypeDef *Body, float X, float Y, float Margin)
{
    const float DeltaX = X - Body->X;
    const float DeltaY = Y - Body->Y;
    const float Sine = sinf(Body->Heading);
    const float Cosine = cosf(Body->Heading);
    const float Along = (DeltaX * Sine) - (DeltaY * Cosine);
    const float Across = (DeltaX * Cosine) + (DeltaY * Sine);
    return (fabsf(Along) < ((Body->Length / 2.0f) + Margin)) && (fabsf(Across) < ((Body->Width / 2.0f) + Margin));
}

/* Whether a body placed at X, Y, Heading would overlap Other. */
static bool Tug_BodiesOverlap(const Tug_BodyTypeDef *Body, float X, float Y, float Heading, const Tug_BodyTypeDef *Other, float Margin)
{
    const float HalfLength = Body->Length / 2.0f;
    const float HalfWidth = Body->Width / 2.0f;
    const float Points[9][2] =
    {
        { HalfLength, 0.0f }, { HalfLength, -HalfWidth }, { HalfLength, HalfWidth },
        { 0.0f, -HalfWidth }, { 0.0f, HalfWidth }, { -HalfLength, -HalfWidth },
        { -HalfLength, HalfWidth }, { -HalfLength, 0.0f }, { 0.0f, 0.0f }
    };
    const float OtherHalfLength = Other->Length / 2.0f;
    const float OtherHalfWidth = Other->Width / 2.0f;
    const float Corners[6][2] =
    {
        { OtherHalfLength, -OtherHalfWidth }, { OtherHalfLength, OtherHalfWidth }, { -OtherHalfLength, -OtherHalfWidth },
        { -OtherHalfLength, OtherHalfWidth }, { OtherHalfLength, 0.0f }, { -OtherHalfLength, 0.0f }
    };
    const Tug_BodyTypeDef Placed = { X, Y, 0.0f, 0.0f, Heading, 0.0f, Body->Length, Body->Width };
    const float Sine = sinf(Heading);
    const float Cosine = cosf(Heading);

    for(uint8_t Index = 0U; Index < 9U; Index++)
    {
        if(Tug_PointInBody(Other, X + (Points[Index][0] * Sine) + (Points[Index][1] * Cosine), Y - (Points[Index][0] * Cosine) + (Points[Index][1] * Sine), Margin))
        {
            return true;
        }
    }
    for(uint8_t Index = 0U; Index < 6U; Index++)
    {
        float CornerX;
        float CornerY;
        Tug_LocalPoint(Other, Corners[Index][0], Corners[Index][1], &CornerX, &CornerY);
        if(Tug_PointInBody(&Placed, CornerX, CornerY, Margin))
        {
            return true;
        }
    }
    return false;
}

/* Whether a point is on the bank, on a buoy or on a moored narrowboat. */
static bool Tug_PointBlocked(float X, float Y)
{
    const int16_t TileX = (int16_t)floorf(X / (float)TUG_TILE_SIZE);
    const int16_t TileY = (int16_t)floorf(Y / (float)TUG_TILE_SIZE);
    const char Tile = Tug_TileAt(TileX, TileY);

    if(!Tug_TileIsWater(Tile))
    {
        return true;
    }
    if(Tile == TUG_TILE_BUOY)
    {
        const float CentreX = (float)((TileX * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2));
        const float CentreY = (float)((TileY * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2));
        if((((X - CentreX) * (X - CentreX)) + ((Y - CentreY) * (Y - CentreY))) < (BUOY_RADIUS * BUOY_RADIUS))
        {
            return true;
        }
    }
    for(uint8_t Index = 0U; Index < TUG_MOORED_BOAT_COUNT; Index++)
    {
        const Tug_MooredBoatTypeDef *Moored = &Tug_MooredBoats[Index];
        if((fabsf(X - (float)Moored->X) < 50.0f) && (fabsf(Y - (float)Moored->Y) < 50.0f))
        {
            const Tug_BodyTypeDef Boat = { (float)Moored->X, (float)Moored->Y, 0.0f, 0.0f, (float)Moored->Heading / 1000.0f, 0.0f, TUG_MOORED_LENGTH, TUG_MOORED_WIDTH };
            if(Tug_PointInBody(&Boat, X, Y, 0.0f))
            {
                return true;
            }
        }
    }
    return false;
}

/* The outline points tested for knocks and grounding, along the keel and across it. */
static void Tug_HullPoint(const Tug_BodyTypeDef *Body, uint8_t Index, float *Ahead, float *Right)
{
    static const float Shape[11][2] =
    {
        { 1.0f, 0.0f }, { 0.85f, -1.0f }, { 0.85f, 1.0f }, { 0.5f, -1.0f }, { 0.5f, 1.0f }, { 0.0f, -1.0f },
        { 0.0f, 1.0f }, { -0.5f, -1.0f }, { -0.5f, 1.0f }, { -1.0f, -1.0f }, { -1.0f, 1.0f }
    };
    *Ahead = Shape[Index][0] * Body->Length / 2.0f;
    *Right = Shape[Index][1] * Body->Width / 2.0f;
}

/* Whether a body placed at X, Y, Heading would touch anything but open water. */
static bool Tug_BodyBlocked(const Tug_BodyTypeDef *Body, float X, float Y, float Heading)
{
    const float Sine = sinf(Heading);
    const float Cosine = cosf(Heading);

    for(uint8_t Index = 0U; Index < 11U; Index++)
    {
        float Ahead;
        float Right;
        Tug_HullPoint(Body, Index, &Ahead, &Right);
        if(Tug_PointBlocked(X + (Ahead * Sine) + (Right * Cosine), Y - (Ahead * Cosine) + (Right * Sine)))
        {
            return true;
        }
    }
    return false;
}

/* How much of a body's hull is over the shallows, 0 to 1. */
static float Tug_ShallowShare(const Tug_BodyTypeDef *Body)
{
    uint8_t Aground = 0U;
    for(uint8_t Index = 0U; Index < 11U; Index++)
    {
        float Ahead;
        float Right;
        float X;
        float Y;
        char Tile;
        Tug_HullPoint(Body, Index, &Ahead, &Right);
        Tug_LocalPoint(Body, Ahead, Right, &X, &Y);
        Tile = Tug_TileAt((int16_t)floorf(X / (float)TUG_TILE_SIZE), (int16_t)floorf(Y / (float)TUG_TILE_SIZE));
        Aground = (uint8_t)(Aground + (((Tile == TUG_TILE_SHALLOWS) || (Tile == TUG_TILE_BUOY)) ? 1U : 0U));
    }
    return (float)Aground / 11.0f;
}

/* How hard the wind is blowing just now: it gusts between about a quarter and all of its strength. */
static float Tug_Gust(const Tug_WorldTypeDef *World)
{
    const float Time = (float)(World->Milliseconds % 600000U) / 1000.0f;
    return 0.7f + (0.3f * sinf(Time * 1.1f)) + (0.15f * sinf(Time * 3.7f));
}

/* The wind's push on a body, along its keel and across it; heavier boats are pushed less. */
static void Tug_WindPush(const Tug_WorldTypeDef *World, const Tug_BodyTypeDef *Body, float Mass, float Share, float *Ahead, float *Sideways)
{
    const float Strength = Share * Tug_Gust(World) / sqrtf(Mass);
    const float Sine = sinf(Body->Heading);
    const float Cosine = cosf(Body->Heading);
    *Ahead = ((World->WindX * Sine) - (World->WindY * Cosine)) * WIND_ALONG_SHARE * Strength;
    *Sideways = ((World->WindX * Cosine) + (World->WindY * Sine)) * Strength;
}

/* Push a body along and sideways, spin it, and let the water slow it. */
static void Tug_DriveBody(Tug_BodyTypeDef *Body, float Ahead, float Sideways, float Spin, float DragAhead, float DragSideways, float DragSpin, float DeltaSeconds)
{
    const float Sine = sinf(Body->Heading);
    const float Cosine = cosf(Body->Heading);
    float Forward = (Body->VelocityX * Sine) - (Body->VelocityY * Cosine);
    float Across = (Body->VelocityX * Cosine) + (Body->VelocityY * Sine);

    Forward = (Forward + (Ahead * DeltaSeconds)) / (1.0f + (DragAhead * DeltaSeconds));
    Across = (Across + (Sideways * DeltaSeconds)) / (1.0f + (DragSideways * DeltaSeconds));
    Body->Spin = (Body->Spin + (Spin * DeltaSeconds)) / (1.0f + (DragSpin * DeltaSeconds));
    Body->VelocityX = (Forward * Sine) + (Across * Cosine);
    Body->VelocityY = (Across * Sine) - (Forward * Cosine);
}

/*
 * Move a body on by its speed. Whatever part of the move hits something is
 * dropped and bounced back, so boats scrape along the bank rather than
 * stick. Returns how hard it hit (pixels per second), or 0.
 */
static float Tug_MoveBody(Tug_BodyTypeDef *Body, float DeltaSeconds)
{
    const float NextX = Body->X + (Body->VelocityX * DeltaSeconds);
    const float NextY = Body->Y + (Body->VelocityY * DeltaSeconds);
    const float NextHeading = Tug_WrapAngle(Body->Heading + (Body->Spin * DeltaSeconds));
    float Impact;

    if(!Tug_BodyBlocked(Body, NextX, NextY, NextHeading))
    {
        Body->X = NextX;
        Body->Y = NextY;
        Body->Heading = NextHeading;
        return 0.0f;
    }
    if(!Tug_BodyBlocked(Body, NextX, Body->Y, NextHeading))
    {
        Impact = fabsf(Body->VelocityY);
        Body->X = NextX;
        Body->Heading = NextHeading;
        Body->VelocityY *= -KNOCK_BOUNCE;
        return fmaxf(Impact, 1.0f);
    }
    if(!Tug_BodyBlocked(Body, Body->X, NextY, NextHeading))
    {
        Impact = fabsf(Body->VelocityX);
        Body->Y = NextY;
        Body->Heading = NextHeading;
        Body->VelocityX *= -KNOCK_BOUNCE;
        return fmaxf(Impact, 1.0f);
    }
    if(!Tug_BodyBlocked(Body, NextX, NextY, Body->Heading))
    {
        Impact = fabsf(Body->Spin) * Body->Length * 0.4f;
        Body->X = NextX;
        Body->Y = NextY;
        Body->Spin *= -KNOCK_BOUNCE;
        return fmaxf(Impact, 1.0f);
    }
    Impact = Tug_Speed(Body);
    if(!Tug_BodyBlocked(Body, Body->X, Body->Y, NextHeading))
    {
        Body->Heading = NextHeading;
    }
    else
    {
        Body->Spin *= -KNOCK_BOUNCE;
    }
    Body->VelocityX *= -KNOCK_BOUNCE;
    Body->VelocityY *= -KNOCK_BOUNCE;
    return fmaxf(Impact, 1.0f);
}

/* Shift a body by DeltaX, DeltaY if it fits there. */
static void Tug_NudgeBody(Tug_BodyTypeDef *Body, float DeltaX, float DeltaY)
{
    if(!Tug_BodyBlocked(Body, Body->X + DeltaX, Body->Y + DeltaY, Body->Heading))
    {
        Body->X += DeltaX;
        Body->Y += DeltaY;
    }
}

/*
 * The tow rope: once it's stretched to its length, it pulls the tug and the
 * cargo together, the lighter one moving more. Towed cargo swings round to
 * trail behind the rope.
 */
static void Tug_PullRope(Tug_WorldTypeDef *World)
{
    Tug_BodyTypeDef *Tug = &World->Tug.Body;
    Tug_BodyTypeDef *Cargo = &World->Cargo.Body;
    const float TugMass = Tug_Models[World->Tug.Model].Mass;
    const float CargoMass = Tug_CargoTypes[World->Cargo.Type].Mass;
    const float TugShare = CargoMass / (TugMass + CargoMass);
    const float CargoShare = TugMass / (TugMass + CargoMass);
    float SternX;
    float SternY;
    float BowX;
    float BowY;
    float DeltaX;
    float DeltaY;
    float Distance;

    Tug_LocalPoint(Tug, -Tug->Length / 2.0f, 0.0f, &SternX, &SternY);
    Tug_LocalPoint(Cargo, Cargo->Length / 2.0f, 0.0f, &BowX, &BowY);
    DeltaX = SternX - BowX;
    DeltaY = SternY - BowY;
    Distance = sqrtf((DeltaX * DeltaX) + (DeltaY * DeltaY));

    if(Distance > ROPE_LENGTH)
    {
        const float NormalX = DeltaX / Distance;
        const float NormalY = DeltaY / Distance;
        const float Stretch = (Distance - ROPE_LENGTH) * ROPE_TAKE_UP;
        const float Separating = (((Tug->VelocityX - Cargo->VelocityX) * NormalX) + ((Tug->VelocityY - Cargo->VelocityY) * NormalY)) * ROPE_TAKE_UP;

        /*
         * How much a pull towards the cargo turns the tug about its middle:
         * the stern's offset from the middle crossed with the pull, over the
         * tug's resistance to turning (a plank's, length squared over 12).
         * Positive turns it clockwise.
         */
        const float LeverX = SternX - Tug->X;
        const float LeverY = SternY - Tug->Y;
        const float TurnPerPull = TUG_ROPE_TURN * 12.0f * ((LeverX * -NormalY) - (LeverY * -NormalX)) / (Tug->Length * Tug->Length);

        /* The same for the cargo, pulled at its bow towards the tug. */
        const float BowLeverX = BowX - Cargo->X;
        const float BowLeverY = BowY - Cargo->Y;
        const float CargoTurnPerPull = CARGO_ROPE_TURN * 12.0f * ((BowLeverX * NormalY) - (BowLeverY * NormalX)) / (Cargo->Length * Cargo->Length);

        Tug_NudgeBody(Tug, -NormalX * Stretch * TugShare, -NormalY * Stretch * TugShare);
        if(!Tug_BodyBlocked(Tug, Tug->X, Tug->Y, Tug_WrapAngle(Tug->Heading + (TurnPerPull * Stretch * TugShare))))
        {
            Tug->Heading = Tug_WrapAngle(Tug->Heading + (TurnPerPull * Stretch * TugShare));
        }
        Tug_NudgeBody(Cargo, NormalX * Stretch * CargoShare, NormalY * Stretch * CargoShare);
        if(!Tug_BodyBlocked(Cargo, Cargo->X, Cargo->Y, Tug_WrapAngle(Cargo->Heading + (CargoTurnPerPull * Stretch * CargoShare))))
        {
            Cargo->Heading = Tug_WrapAngle(Cargo->Heading + (CargoTurnPerPull * Stretch * CargoShare));
        }
        if(Separating > 0.0f)
        {
            Cargo->Spin += CargoTurnPerPull * Separating * CargoShare;
            Tug->Spin += TurnPerPull * Separating * TugShare;
            Tug->VelocityX -= NormalX * Separating * TugShare;
            Tug->VelocityY -= NormalY * Separating * TugShare;
            Cargo->VelocityX += NormalX * Separating * CargoShare;
            Cargo->VelocityY += NormalY * Separating * CargoShare;
        }
    }
}

/*
 * When the tug and the cargo touch they shove each other apart, the lighter
 * one moving more, and their speeds towards each other even out, as a tug
 * pushing a barge would. They never block each other outright, so a tow
 * can't jam. Returns how hard they met (pixels per second), or 0.
 */
static float Tug_PushBoats(Tug_WorldTypeDef *World)
{
    Tug_BodyTypeDef *Tug = &World->Tug.Body;
    Tug_BodyTypeDef *Cargo = &World->Cargo.Body;
    const float TugMass = Tug_Models[World->Tug.Model].Mass;
    const float CargoMass = Tug_CargoTypes[World->Cargo.Type].Mass;
    const float TugShare = CargoMass / (TugMass + CargoMass);
    const float CargoShare = TugMass / (TugMass + CargoMass);
    const float DeltaX = Tug->X - Cargo->X;
    const float DeltaY = Tug->Y - Cargo->Y;
    const float Sine = sinf(Cargo->Heading);
    const float Cosine = cosf(Cargo->Heading);
    const float Along = (DeltaX * Sine) - (DeltaY * Cosine);
    const float Across = (DeltaX * Cosine) + (DeltaY * Sine);
    const float AlongGap = ((Cargo->Length + Tug->Length) / 2.0f) - fabsf(Along);
    const float AcrossGap = ((Cargo->Width + Tug->Width) / 2.0f) - fabsf(Across);
    float NormalX;
    float NormalY;
    float Depth;
    float Closing;

    if(!Tug_BodiesOverlap(Tug, Tug->X, Tug->Y, Tug->Heading, Cargo, 0.0f))
    {
        return 0.0f;
    }

    /* Push out along whichever of the cargo's sides is overlapped least: its ends or its flanks. */
    if((AlongGap / Cargo->Length) < (AcrossGap / Cargo->Width))
    {
        NormalX = (Along >= 0.0f) ? Sine : -Sine;
        NormalY = (Along >= 0.0f) ? -Cosine : Cosine;
        Depth = AlongGap;
    }
    else
    {
        NormalX = (Across >= 0.0f) ? Cosine : -Cosine;
        NormalY = (Across >= 0.0f) ? Sine : -Sine;
        Depth = AcrossGap;
    }
    Depth = fminf(fmaxf(Depth, 0.0f), 4.0f);

    Tug_NudgeBody(Tug, NormalX * Depth * TugShare, NormalY * Depth * TugShare);
    Tug_NudgeBody(Cargo, -NormalX * Depth * CargoShare, -NormalY * Depth * CargoShare);

    Closing = ((Cargo->VelocityX - Tug->VelocityX) * NormalX) + ((Cargo->VelocityY - Tug->VelocityY) * NormalY);
    if(Closing > 0.0f)
    {
        Tug->VelocityX += NormalX * Closing * TugShare;
        Tug->VelocityY += NormalY * Closing * TugShare;
        Cargo->VelocityX -= NormalX * Closing * CargoShare;
        Cargo->VelocityY -= NormalY * Closing * CargoShare;
        return Closing;
    }
    return 0.0f;
}

void Tug_StepBoats(Tug_WorldTypeDef *World, float DeltaSeconds, float LeftThrottle, float RightThrottle, Tug_StepResultTypeDef *Result)
{
    Tug_TugTypeDef *Tug = &World->Tug;
    const Tug_ModelTypeDef *Model = &Tug_Models[Tug->Model];
    const float Response = ENGINE_RESPONSE * DeltaSeconds;
    const float Thrust = Model->Thrust * (1.0f + Tug->ExtraThrust);
    const float TugAground = 1.0f + (SHALLOWS_DRAG * Tug_ShallowShare(&Tug->Body));
    float LeftPush;
    float RightPush;
    float Difference;
    float WindAhead;
    float WindSideways;

    memset(Result, 0, sizeof(*Result));
    Tug->LeftEngine += Tug_Clamp(LeftThrottle - Tug->LeftEngine, -Response, Response);
    Tug->RightEngine += Tug_Clamp(RightThrottle - Tug->RightEngine, -Response, Response);
    LeftPush = Tug->LeftEngine * Thrust * ((Tug->LeftEngine < 0.0f) ? ENGINE_ASTERN : 1.0f);
    RightPush = Tug->RightEngine * Thrust * ((Tug->RightEngine < 0.0f) ? ENGINE_ASTERN : 1.0f);

    /* The left engine pushing harder turns the tug right (clockwise), and the other way about. */
    Tug_WindPush(World, &Tug->Body, Model->Mass, WIND_TUG_SHARE, &WindAhead, &WindSideways);
    /* How differently the engines pull, as a share of the most they can (both full, opposite ways), then curved. */
    Difference = (LeftPush - RightPush) / (2.0f * Thrust);
    Difference *= STEER_LINEAR_SHARE + ((1.0f - STEER_LINEAR_SHARE) * fabsf(Difference));
    Tug_DriveBody(&Tug->Body, LeftPush + RightPush + WindAhead, WindSideways,
                  Difference * 2.0f * Thrust * ENGINE_TURN,
                  TUG_DRAG_AHEAD * TugAground, TUG_DRAG_SIDEWAYS * TugAground, TUG_DRAG_SPIN * TugAground, DeltaSeconds);
    Result->TugKnock = Tug_MoveBody(&Tug->Body, DeltaSeconds);

    if(World->Cargo.Present)
    {
        Tug_BodyTypeDef *Cargo = &World->Cargo.Body;
        const bool Resting = !World->Cargo.Hooked && (Tug_Speed(Cargo) < CARGO_REST_SPEED) && (fabsf(Cargo->Spin) < CARGO_REST_SPIN);

        /*
         * Cargo waiting at its dock, often far off screen, is left alone:
         * moving it would only read map rows far from the screen and push
         * the rows being drawn out of the row cache.
         */
        if(Resting)
        {
            Cargo->VelocityX = 0.0f;
            Cargo->VelocityY = 0.0f;
            Cargo->Spin = 0.0f;
        }
        else
        {
            const float Share = Tug_ShallowShare(Cargo);
            const float Aground = 1.0f + (SHALLOWS_DRAG * Share);
            const float Swing = (World->Cargo.Thrusters && World->Cargo.Hooked) ? ((float)World->Cargo.Thruster * CARGO_THRUSTER_TURN / sqrtf(Tug_CargoTypes[World->Cargo.Type].Mass)) : 0.0f;
            float CargoAhead = 0.0f;
            float CargoSideways = 0.0f;

            /* Cargo tied up at its dock is sheltered; once it's on the rope the wind gets at it. */
            if(World->Cargo.Hooked)
            {
                Tug_WindPush(World, Cargo, Tug_CargoTypes[World->Cargo.Type].Mass, World->Cargo.Covered ? WIND_COVERED_SHARE : 1.0f, &CargoAhead, &CargoSideways);
            }
            Tug_DriveBody(Cargo, CargoAhead, CargoSideways, Swing, CARGO_DRAG_AHEAD * Aground, CARGO_DRAG_SIDEWAYS * Aground, CARGO_DRAG_SPIN * Aground, DeltaSeconds);
            Result->CargoKnock = Tug_MoveBody(Cargo, DeltaSeconds);
            if(Result->CargoKnock > 0.0f)
            {
                Result->CargoScrape = Tug_Speed(Cargo) * DeltaSeconds;
            }
            Result->CargoGrounding = Tug_Speed(Cargo) * DeltaSeconds * Share;
        }
        if(World->Cargo.Hooked)
        {
            Tug_PullRope(World);
        }
        if(World->Cargo.SinkMilliseconds == 0U)
        {
            const float Shove = Tug_PushBoats(World);
            Result->TugKnock = fmaxf(Result->TugKnock, Shove);
            Result->CargoKnock = fmaxf(Result->CargoKnock, Shove * FENDER_SOFTENING);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Setting up                                                                 */
/* -------------------------------------------------------------------------- */

static void Tug_AddSegment(Tug_WorldTypeDef *World, int16_t X, int16_t Y, int16_t Length, bool Across)
{
    if((Length >= 5) && (World->SegmentCount < TUG_MAX_SEGMENTS))
    {
        Tug_SegmentTypeDef *Segment = &World->Segments[World->SegmentCount++];
        Segment->X = X;
        Segment->Y = Y;
        Segment->Length = (uint8_t)((Length > 255) ? 255 : Length);
        Segment->Across = Across;
    }
}

/* Find the straight runs of lane, then put people walking on them. */
static void Tug_FindLanes(Tug_WorldTypeDef *World)
{
    static const uint8_t ShirtColours[] = { TUG_COLOUR_CAR_RED, TUG_COLOUR_CAR_BLUE, TUG_COLOUR_CAR_YELLOW, TUG_COLOUR_WHITE, TUG_COLOUR_CAR_GREEN, TUG_COLOUR_TUG_NAVY };

    /*
     * One pass down the stored map, a row at a time (each row is decoded
     * only once): runs across are found within each row, and runs down are
     * followed column by column from row to row.
     */
    static int16_t ColumnStart[TUG_MAP_WIDTH];

    World->SegmentCount = 0U;
    for(int16_t X = 0; X < TUG_MAP_WIDTH; X++)
    {
        ColumnStart[X] = -1;
    }
    for(int16_t Y = 0; Y <= TUG_MAP_HEIGHT; Y++)
    {
        int16_t Start = -1;
        for(int16_t X = 0; X <= TUG_MAP_WIDTH; X++)
        {
            const bool Road = (X < TUG_MAP_WIDTH) && (Y < TUG_MAP_HEIGHT) && (Tug_StoredRow(Y)[X] == TUG_TILE_LANE);
            if(Road && (Start < 0))
            {
                Start = X;
            }
            else if(!Road && (Start >= 0))
            {
                Tug_AddSegment(World, Start, Y, (int16_t)(X - Start), true);
                Start = -1;
            }
            if(X < TUG_MAP_WIDTH)
            {
                if(Road && (ColumnStart[X] < 0))
                {
                    ColumnStart[X] = Y;
                }
                else if(!Road && (ColumnStart[X] >= 0))
                {
                    Tug_AddSegment(World, X, ColumnStart[X], (int16_t)(Y - ColumnStart[X]), false);
                    ColumnStart[X] = -1;
                }
            }
        }
    }
    if(World->SegmentCount == 0U)
    {
        return;
    }

    for(uint8_t Index = 0U; Index < TUG_MAX_PEOPLE; Index++)
    {
        Tug_WalkerTypeDef *Person = &World->People[Index];
        Person->Segment = (uint8_t)(Tug_Random() % World->SegmentCount);
        Person->Position = (float)(Tug_Random() % ((uint32_t)World->Segments[Person->Segment].Length * TUG_TILE_SIZE));
        Person->Direction = (Tug_Random() & 1U) ? 1 : -1;
        Person->Speed = 9.0f + (float)(Tug_Random() % 9U);
        Person->Colour = ShirtColours[Tug_Random() % sizeof(ShirtColours)];
        Person->PauseMilliseconds = 0U;
    }
}

void Tug_PrepareWorld(Tug_WorldTypeDef *World)
{
    memset(World, 0, sizeof(*World));
    World->TargetDock = -1;
    Tug_FindLanes(World);
}

void Tug_PlaceTugAtShipyard(Tug_WorldTypeDef *World)
{
    Tug_TugTypeDef *Tug = &World->Tug;
    const Tug_ModelTypeDef *Model = &Tug_Models[Tug->Model];
    memset(&Tug->Body, 0, sizeof(Tug->Body));
    Tug_DockPosition(TUG_SHIPYARD_DOCK, &Tug->Body.X, &Tug->Body.Y);
    Tug->Body.Heading = (float)Tug_Docks[TUG_SHIPYARD_DOCK].Heading / 1000.0f;
    Tug->Body.Length = Model->Length;
    Tug->Body.Width = Model->Width;
    Tug->LeftEngine = 0.0f;
    Tug->RightEngine = 0.0f;
}

/* Lay cargo at a site's dock, pointing along the canal. */
void Tug_PlaceCargo(Tug_WorldTypeDef *World, uint8_t Type, uint8_t Dock)
{
    Tug_TowTypeDef *Cargo = &World->Cargo;
    const Tug_CargoTypeDef *Kind = &Tug_CargoTypes[Type];

    memset(Cargo, 0, sizeof(*Cargo));
    Tug_DockPosition(Dock, &Cargo->Body.X, &Cargo->Body.Y);
    Cargo->Body.Heading = (float)Tug_Docks[Dock].Heading / 1000.0f;
    Cargo->Body.Length = Kind->Length;
    Cargo->Body.Width = Kind->Width;
    Cargo->Type = Type;
    Cargo->Condition = 100.0f;
    Cargo->Present = true;

    /*
     * It lies along the bank, the way the waterway runs. On a tight bend a
     * long cargo may touch the bank: ease it out towards the middle until it fits.
     */
    for(uint8_t Attempt = 0U; (Attempt < 12U) && Tug_BodyBlocked(&Cargo->Body, Cargo->Body.X, Cargo->Body.Y, Cargo->Body.Heading); Attempt++)
    {
        float BestX = Cargo->Body.X;
        float BestY = Cargo->Body.Y;
        uint8_t BestDepth = 0U;
        for(int16_t OffsetY = -1; OffsetY <= 1; OffsetY++)
        {
            for(int16_t OffsetX = -1; OffsetX <= 1; OffsetX++)
            {
                const float X = Cargo->Body.X + (float)(OffsetX * 4);
                const float Y = Cargo->Body.Y + (float)(OffsetY * 4);
                const uint8_t Depth = Tug_WaterDepth((int16_t)(X / (float)TUG_TILE_SIZE), (int16_t)(Y / (float)TUG_TILE_SIZE));
                if(Depth > BestDepth)
                {
                    BestDepth = Depth;
                    BestX = X;
                    BestY = Y;
                }
            }
        }
        Cargo->Body.X = BestX;
        Cargo->Body.Y = BestY;
    }
}

/* -------------------------------------------------------------------------- */
/* Village life                                                               */
/* -------------------------------------------------------------------------- */

static void Tug_MoveWalker(const Tug_WorldTypeDef *World, Tug_WalkerTypeDef *Walker, uint32_t DeltaMilliseconds, uint16_t PauseMilliseconds)
{
    const float End = (float)((uint32_t)World->Segments[Walker->Segment].Length * TUG_TILE_SIZE);

    if(Walker->PauseMilliseconds > DeltaMilliseconds)
    {
        Walker->PauseMilliseconds = (uint16_t)(Walker->PauseMilliseconds - DeltaMilliseconds);
        return;
    }
    Walker->PauseMilliseconds = 0U;
    Walker->Position += (float)Walker->Direction * Walker->Speed * (float)DeltaMilliseconds / 1000.0f;
    if((Walker->Position < 0.0f) || (Walker->Position > End))
    {
        /* At the end of the lane, stop for a moment and turn round. */
        Walker->Position = Tug_Clamp(Walker->Position, 0.0f, End);
        Walker->Direction = (int8_t)-Walker->Direction;
        Walker->PauseMilliseconds = PauseMilliseconds;
    }
}

void Tug_UpdateCity(Tug_WorldTypeDef *World, uint32_t DeltaMilliseconds)
{
    const Tug_TugTypeDef *Tug = &World->Tug;
    const float Load = fmaxf(fabsf(Tug->LeftEngine), fabsf(Tug->RightEngine));

    World->Milliseconds += DeltaMilliseconds;
    if(World->SegmentCount > 0U)
    {
        for(uint8_t Index = 0U; Index < TUG_MAX_PEOPLE; Index++)
        {
            Tug_MoveWalker(World, &World->People[Index], DeltaMilliseconds, (uint16_t)(1000U + (Tug_Random() % 3000U)));
        }
    }

    for(uint8_t Index = 0U; Index < TUG_MAX_WAKE; Index++)
    {
        Tug_WakeTypeDef *Foam = &World->Wake[Index];
        if(Foam->Active)
        {
            Foam->AgeMilliseconds = (uint16_t)(Foam->AgeMilliseconds + DeltaMilliseconds);
            Foam->Active = Foam->AgeMilliseconds < TUG_WAKE_LIFE_MS;
        }
    }
    World->WakeTimerMilliseconds = (uint16_t)(World->WakeTimerMilliseconds + DeltaMilliseconds);
    if((Load > 0.08f) && (World->WakeTimerMilliseconds >= WAKE_INTERVAL_MS))
    {
        Tug_WakeTypeDef *Foam = &World->Wake[World->NextWake];
        const float Across = (float)((int32_t)(Tug_Random() % 11U) - 5);
        const float Behind = ((Tug->LeftEngine + Tug->RightEngine) < 0.0f) ? (Tug->Body.Length / 2.0f) : (-Tug->Body.Length / 2.0f - 3.0f);
        Tug_LocalPoint(&Tug->Body, Behind, Across, &Foam->X, &Foam->Y);
        Foam->AgeMilliseconds = 0U;
        Foam->Active = true;
        World->NextWake = (uint8_t)((World->NextWake + 1U) % TUG_MAX_WAKE);
        World->WakeTimerMilliseconds = 0U;
    }
    else if(World->WakeTimerMilliseconds > WAKE_INTERVAL_MS)
    {
        World->WakeTimerMilliseconds = WAKE_INTERVAL_MS;
    }
}

/* -------------------------------------------------------------------------- */
/* Camera                                                                     */
/* -------------------------------------------------------------------------- */

void Tug_FollowCamera(Tug_WorldTypeDef *World, float DeltaSeconds, bool Snap)
{
    const Tug_BodyTypeDef *Tug = &World->Tug.Body;
    float TargetX = Tug->X + (Tug->VelocityX * CAMERA_LEAD) - ((float)RENDER_WIDTH / 2.0f);
    float TargetY = Tug->Y + (Tug->VelocityY * CAMERA_LEAD) - ((float)(RENDER_HEIGHT + TUG_HUD_HEIGHT) / 2.0f);
    const float Blend = Snap ? 1.0f : Tug_Clamp(DeltaSeconds * CAMERA_CATCH_UP, 0.0f, 1.0f);

    TargetX = Tug_Clamp(TargetX, 0.0f, (float)(TUG_WORLD_WIDTH - (int32_t)RENDER_WIDTH));
    TargetY = Tug_Clamp(TargetY, (float)-TUG_HUD_HEIGHT, (float)(TUG_WORLD_HEIGHT - (int32_t)RENDER_HEIGHT));
    World->CameraX += (TargetX - World->CameraX) * Blend;
    World->CameraY += (TargetY - World->CameraY) * Blend;
}
