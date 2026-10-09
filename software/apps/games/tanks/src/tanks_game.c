/**
 * @file tanks_game.c
 * @brief Designed tactical arenas, differential-drive physics, combat and enemy AI.
 */

#include "tanks_internal.h"
#include "tanks_audio.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define PLAYER_RADIUS         (18)
#define ENEMY_RADIUS          (16)
#define BULLET_RADIUS         (4)
#define PLAYER_SPEED          (132)
#define PLAYER_TURN_RATE      (1800)
#define PLAYER_RELOAD_MS      (620U)
#define ROUND_CLEAR_MS        (2200U)
#define PLAYER_OWNER          (0U)

/* The demo battle: the autopilot keeps about this far from its target, and a new battle starts this long after one ends. */
#define DEMO_FIGHT_RANGE      (170)
#define DEMO_RESTART_MS       (1800U)
#define RICOCHET_MAX_BOUNCES  (2U)
#define RICOCHET_PLAYER_SPEED (400)
#define RICOCHET_ENEMY_SPEED  (275)
/* Blast radii: every tank whose hull reaches into the circle is destroyed, whoever set it off. */
#define ROCKET_BLAST_RADIUS   (54U)
#define MINE_BLAST_RADIUS     (58U)

/* The player can have this many mines out at once. */
#define PLAYER_MAX_MINES      (2U)

/* Tanks steer around any of the player's mines within this distance of where they're heading. */
#define MINE_AVOID_DISTANCE   (34U)

/*
 * The mine-layer races between the spots on the level's mine map at
 * MINELAYER_SPEED, planting a mine when it is within MINELAYER_PLANT_PIXELS
 * of one. Facing more than MINELAYER_PIVOT_ANGLE away from where it's going,
 * it turns on the spot first rather than driving round in circles. A spot
 * with a mine within MINE_SPOT_PIXELS of it is already mined. Mines laid on
 * the map stay until something sets them off.
 */
#define MINELAYER_SPEED       (900)
#define MINELAYER_PIVOT_ANGLE (300)
#define MINELAYER_PLANT_PIXELS (14U)
#define MINE_SPOT_PIXELS      (20U)
#define MINE_LIFE_FOREVER     (UINT16_MAX)
#define NO_MINE_SPOT          (UINT8_MAX)

/* A path that goes round the player keeps its waypoints this far from the player's tank. */
#define PATH_PLAYER_CLEARANCE (40U)
#define CAMPAIGN_WAVES        (10U)
#define ENDLESS_HARDEST_FROM  (7U)
#define BASIC_FIRE_CONE       (80)
#define HUNTER_SPEED_PERCENT  (120)
#define HUNTER_CLOSE_RANGE    (170)
#define HUNTER_ROCKET_RANGE   (260)

/* Testing only: 1 makes the player's tank indestructible. Set to 0 for release. */
#define PLAYER_INVINCIBLE     (0)

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Tanks_SpawnEnemy(uint8_t Slot, uint8_t Ordinal, uint8_t Total);
static void Tanks_DestroyEnemy(Tanks_TankTypeDef *Enemy, bool LeaveWreck);
static Tanks_VectorTypeDef Tanks_EnemyRandomOpenPoint(const Tanks_TankTypeDef *Enemy, int16_t MaximumDistance);
static bool Tanks_DropMine(const Tanks_TankTypeDef *Tank, uint8_t Owner);
static bool Tanks_SteerTank(Tanks_TankTypeDef *Tank, int16_t DesiredHeading, int16_t Speed, bool MayFire);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const uint8_t Tanks_SpawnTiles[16][2] =
{
    { 6U, 4U }, { 25U, 4U }, { 6U, 14U }, { 25U, 14U },
    { 16U, 4U }, { 4U, 9U }, { 28U, 9U }, { 11U, 14U },
    { 21U, 14U }, { 11U, 4U }, { 21U, 4U }, { 16U, 15U },
    { 8U, 9U }, { 24U, 9U }, { 12U, 7U }, { 20U, 12U }
};

/* -------------------------------------------------------------------------- */
/* Game logic                                                                 */
/* -------------------------------------------------------------------------- */

static bool Tanks_TileBlocksTank(uint8_t Tile, bool AvoidPits)
{
    return (Tile == TANKS_TILE_WALL) || (Tile == TANKS_TILE_HQ) || (AvoidPits && (Tile == TANKS_TILE_PIT));
}

static bool Tanks_TileBlocksBullet(uint8_t Tile)
{
    return (Tile == TANKS_TILE_WALL) || (Tile == TANKS_TILE_HQ);
}

static uint8_t Tanks_TileAtPosition(Tanks_VectorTypeDef Position)
{
    const int32_t X = Position.X >> TANKS_FP_SHIFT;
    const int32_t Y = Position.Y >> TANKS_FP_SHIFT;
    const int32_t TileX = X / TANKS_TILE_SIZE;
    const int32_t TileY = Y / TANKS_TILE_SIZE;
    if((TileX < 0) || (TileY < 0) || (TileX >= TANKS_MAP_WIDTH) || (TileY >= TANKS_MAP_HEIGHT))
    {
        return TANKS_TILE_WALL;
    }
    return Tanks_Game.Tiles[TileY][TileX];
}

static bool Tanks_PositionBlocked(Tanks_VectorTypeDef Position, int16_t Radius, bool AvoidPits)
{
    const int32_t X = Position.X >> TANKS_FP_SHIFT;
    const int32_t Y = Position.Y >> TANKS_FP_SHIFT;
    const int32_t MinimumTileX = (X - Radius) / TANKS_TILE_SIZE;
    const int32_t MaximumTileX = (X + Radius) / TANKS_TILE_SIZE;
    const int32_t MinimumTileY = (Y - Radius) / TANKS_TILE_SIZE;
    const int32_t MaximumTileY = (Y + Radius) / TANKS_TILE_SIZE;
    for(int32_t TileY = MinimumTileY; TileY <= MaximumTileY; TileY++)
    {
        for(int32_t TileX = MinimumTileX; TileX <= MaximumTileX; TileX++)
        {
            int32_t NearestX;
            int32_t NearestY;
            int32_t DeltaX;
            int32_t DeltaY;
            if((TileX < 0) || (TileY < 0) || (TileX >= TANKS_MAP_WIDTH) || (TileY >= TANKS_MAP_HEIGHT))
            {
                return true;
            }
            if(!Tanks_TileBlocksTank(Tanks_Game.Tiles[TileY][TileX], AvoidPits))
            {
                continue;
            }
            NearestX = Tanks_Clamp32(X, TileX * TANKS_TILE_SIZE, ((TileX + 1) * TANKS_TILE_SIZE) - 1);
            NearestY = Tanks_Clamp32(Y, TileY * TANKS_TILE_SIZE, ((TileY + 1) * TANKS_TILE_SIZE) - 1);
            DeltaX = X - NearestX;
            DeltaY = Y - NearestY;
            if(((DeltaX * DeltaX) + (DeltaY * DeltaY)) < (Radius * Radius))
            {
                return true;
            }
        }
    }
    return false;
}

static bool Tanks_TanksOverlap(Tanks_VectorTypeDef Position, int16_t Radius, const Tanks_TankTypeDef *Ignored)
{
    if((&Tanks_Game.Player != Ignored) && Tanks_Game.Player.Active &&
       (Tanks_DistancePixels(Position, Tanks_Game.Player.Position) < (uint32_t)(Radius + PLAYER_RADIUS)))
    {
        return true;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        const Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
        if((Enemy != Ignored) && Enemy->Active &&
           (Tanks_DistancePixels(Position, Enemy->Position) < (uint32_t)(Radius + ENEMY_RADIUS)))
        {
            return true;
        }
    }
    return false;
}

static void Tanks_ClearRoundEntities(void)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        Tanks_Game.Enemies[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
    {
        Tanks_Game.Bullets[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_PARTICLES; Index++)
    {
        Tanks_Game.Particles[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        Tanks_Game.Mines[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_TRACK_MARKS; Index++)
    {
        Tanks_Game.TrackMarks[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_WRECKS; Index++)
    {
        Tanks_Game.Wrecks[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_BLASTS; Index++)
    {
        Tanks_Game.Blasts[Index].Active = false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_FLOATING_TEXT; Index++)
    {
        Tanks_Game.FloatingText[Index].Active = false;
    }
}

static bool Tanks_IsReservedArenaTile(uint8_t X, uint8_t Y)
{
    if((X >= 14U) && (X <= 18U) && (Y >= 8U) && (Y <= 12U))
    {
        return true;
    }
    return false;
}

static void Tanks_PlaceArenaTile(uint8_t X, uint8_t Y, Tanks_TileTypeDef Tile)
{
    if((X == 0U) || (Y == 0U) || (X >= TANKS_MAP_WIDTH - 1U) || (Y >= TANKS_MAP_HEIGHT - 1U))
    {
        return;
    }
    if(Tanks_IsReservedArenaTile(X, Y))
    {
        return;
    }
    Tanks_Game.Tiles[Y][X] = (uint8_t)Tile;
}

static void Tanks_PlaceWallLine(uint8_t X, uint8_t Y, uint8_t Length, bool Vertical)
{
    for(uint8_t Index = 0U; Index < Length; Index++)
    {
        Tanks_PlaceArenaTile((uint8_t)(X + (Vertical ? 0U : Index)), (uint8_t)(Y + (Vertical ? Index : 0U)), TANKS_TILE_WALL);
    }
}

static void Tanks_ClearArenaTile(uint8_t X, uint8_t Y)
{
    if((X == 0U) || (Y == 0U) || (X >= TANKS_MAP_WIDTH - 1U) || (Y >= TANKS_MAP_HEIGHT - 1U))
    {
        return;
    }
    Tanks_Game.Tiles[Y][X] = TANKS_TILE_FLOOR;
}

static void Tanks_ClearArenaPad(uint8_t CentreX, uint8_t CentreY)
{
    for(int8_t OffsetY = -1; OffsetY <= 1; OffsetY++)
    {
        for(int8_t OffsetX = -1; OffsetX <= 1; OffsetX++)
        {
            Tanks_ClearArenaTile((uint8_t)((int16_t)CentreX + OffsetX), (uint8_t)((int16_t)CentreY + OffsetY));
        }
    }
}

static void Tanks_PlaceRoom(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
    Tanks_PlaceWallLine(X, Y, Width, false);
    Tanks_PlaceWallLine(X, (uint8_t)(Y + Height - 1U), Width, false);
    Tanks_PlaceWallLine(X, Y, Height, true);
    Tanks_PlaceWallLine((uint8_t)(X + Width - 1U), Y, Height, true);
}

static void Tanks_OpenHorizontalDoor(uint8_t X, uint8_t Y, uint8_t Width)
{
    for(uint8_t Index = 0U; Index < Width; Index++)
    {
        Tanks_ClearArenaTile((uint8_t)(X + Index), Y);
    }
}

static void Tanks_OpenVerticalDoor(uint8_t X, uint8_t Y, uint8_t Height)
{
    for(uint8_t Index = 0U; Index < Height; Index++)
    {
        Tanks_ClearArenaTile(X, (uint8_t)(Y + Index));
    }
}

/*
 * Add a spot to the level's mine map, in tiles. A mine-layer drives to every
 * spot on the map and plants one mine on each: put them in doorways and
 * other places the player has to pass.
 */
static void Tanks_AddMineSpot(uint8_t X, uint8_t Y)
{
    if(Tanks_Game.MineSpotCount < TANKS_MAX_MINE_SPOTS)
    {
        Tanks_Game.MineSpots[Tanks_Game.MineSpotCount][0] = X;
        Tanks_Game.MineSpots[Tanks_Game.MineSpotCount][1] = Y;
        Tanks_Game.MineSpotCount++;
    }
}

static void Tanks_BuildArena(uint16_t Wave)
{
    const uint8_t Layout = (uint8_t)((Wave - 1U) % 10U);
    Tanks_Game.MineSpotCount = 0U;
    switch(Layout)
    {
        case 0U: /* TRAINING ROOMS */
            Tanks_PlaceRoom(2U, 2U, 12U, 7U);
            Tanks_OpenHorizontalDoor(7U, 8U, 3U);
            Tanks_PlaceRoom(18U, 2U, 12U, 7U);
            Tanks_OpenHorizontalDoor(22U, 8U, 3U);
            Tanks_PlaceRoom(2U, 11U, 10U, 6U);
            Tanks_OpenHorizontalDoor(6U, 11U, 3U);
            Tanks_PlaceRoom(20U, 11U, 10U, 6U);
            Tanks_OpenHorizontalDoor(24U, 11U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(8U, 8U);
            Tanks_AddMineSpot(23U, 8U);
            Tanks_AddMineSpot(7U, 11U);
            Tanks_AddMineSpot(25U, 11U);
            break;
        case 1U: /* TWIN WINGS */
            Tanks_PlaceRoom(2U, 2U, 11U, 15U);
            Tanks_OpenVerticalDoor(12U, 5U, 3U);
            Tanks_OpenVerticalDoor(12U, 12U, 3U);
            Tanks_PlaceRoom(19U, 2U, 11U, 15U);
            Tanks_OpenVerticalDoor(19U, 5U, 3U);
            Tanks_OpenVerticalDoor(19U, 12U, 3U);
            Tanks_PlaceRoom(13U, 2U, 6U, 7U);
            Tanks_OpenHorizontalDoor(15U, 8U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(12U, 6U);
            Tanks_AddMineSpot(12U, 13U);
            Tanks_AddMineSpot(19U, 6U);
            Tanks_AddMineSpot(19U, 13U);
            Tanks_AddMineSpot(16U, 8U);
            break;
        case 2U: /* FOUR CHAMBERS */
            Tanks_PlaceRoom(2U, 2U, 11U, 7U);
            Tanks_OpenHorizontalDoor(7U, 8U, 3U);
            Tanks_OpenVerticalDoor(12U, 4U, 3U);
            Tanks_PlaceRoom(19U, 2U, 11U, 7U);
            Tanks_OpenHorizontalDoor(22U, 8U, 3U);
            Tanks_OpenVerticalDoor(19U, 4U, 3U);
            Tanks_PlaceRoom(2U, 11U, 11U, 6U);
            Tanks_OpenHorizontalDoor(6U, 11U, 3U);
            Tanks_OpenVerticalDoor(12U, 13U, 3U);
            Tanks_PlaceRoom(19U, 11U, 11U, 6U);
            Tanks_OpenHorizontalDoor(23U, 11U, 3U);
            Tanks_OpenVerticalDoor(19U, 13U, 3U);
            Tanks_PlaceWallLine(15U, 2U, 5U, true);
            Tanks_PlaceWallLine(16U, 14U, 3U, true);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(8U, 8U);
            Tanks_AddMineSpot(12U, 5U);
            Tanks_AddMineSpot(23U, 8U);
            Tanks_AddMineSpot(19U, 5U);
            Tanks_AddMineSpot(7U, 11U);
            Tanks_AddMineSpot(12U, 14U);
            Tanks_AddMineSpot(24U, 11U);
            Tanks_AddMineSpot(19U, 14U);
            break;
        case 3U: /* NESTED HALLS */
            Tanks_PlaceRoom(3U, 2U, 26U, 8U);
            Tanks_OpenHorizontalDoor(8U, 9U, 3U);
            Tanks_OpenHorizontalDoor(21U, 9U, 3U);
            Tanks_PlaceWallLine(16U, 2U, 6U, true);
            Tanks_OpenVerticalDoor(16U, 5U, 3U);
            Tanks_PlaceRoom(3U, 11U, 11U, 6U);
            Tanks_OpenHorizontalDoor(7U, 11U, 3U);
            Tanks_PlaceRoom(18U, 11U, 11U, 6U);
            Tanks_OpenHorizontalDoor(22U, 11U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(9U, 9U);
            Tanks_AddMineSpot(22U, 9U);
            Tanks_AddMineSpot(16U, 6U);
            Tanks_AddMineSpot(8U, 11U);
            Tanks_AddMineSpot(23U, 11U);
            break;
        case 4U: /* SPLIT HOUSE */
            Tanks_PlaceRoom(2U, 3U, 13U, 13U);
            Tanks_OpenVerticalDoor(14U, 6U, 3U);
            Tanks_OpenVerticalDoor(14U, 12U, 3U);
            Tanks_PlaceRoom(17U, 3U, 13U, 13U);
            Tanks_OpenVerticalDoor(17U, 6U, 3U);
            Tanks_OpenVerticalDoor(17U, 12U, 3U);
            Tanks_PlaceWallLine(7U, 9U, 5U, false);
            Tanks_PlaceWallLine(21U, 9U, 5U, false);
            Tanks_OpenHorizontalDoor(9U, 9U, 3U);
            Tanks_OpenHorizontalDoor(23U, 9U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(14U, 7U);
            Tanks_AddMineSpot(14U, 13U);
            Tanks_AddMineSpot(17U, 7U);
            Tanks_AddMineSpot(17U, 13U);
            Tanks_AddMineSpot(10U, 9U);
            Tanks_AddMineSpot(24U, 9U);
            break;
        case 5U: /* FIVE ROOMS */
            Tanks_PlaceRoom(2U, 2U, 9U, 7U);
            Tanks_OpenHorizontalDoor(5U, 8U, 3U);
            Tanks_PlaceRoom(12U, 2U, 8U, 7U);
            Tanks_OpenHorizontalDoor(15U, 8U, 3U);
            Tanks_PlaceRoom(21U, 2U, 9U, 7U);
            Tanks_OpenHorizontalDoor(24U, 8U, 3U);
            Tanks_PlaceRoom(3U, 11U, 12U, 6U);
            Tanks_OpenHorizontalDoor(8U, 11U, 3U);
            Tanks_PlaceRoom(17U, 11U, 12U, 6U);
            Tanks_OpenHorizontalDoor(22U, 11U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(6U, 8U);
            Tanks_AddMineSpot(16U, 8U);
            Tanks_AddMineSpot(25U, 8U);
            Tanks_AddMineSpot(9U, 11U);
            Tanks_AddMineSpot(23U, 11U);
            break;
        case 6U: /* CENTRAL COURT */
            Tanks_PlaceRoom(5U, 2U, 22U, 6U);
            Tanks_OpenHorizontalDoor(8U, 7U, 3U);
            Tanks_OpenHorizontalDoor(15U, 7U, 3U);
            Tanks_OpenHorizontalDoor(22U, 7U, 3U);
            Tanks_PlaceRoom(5U, 12U, 22U, 5U);
            Tanks_OpenHorizontalDoor(8U, 12U, 3U);
            Tanks_OpenHorizontalDoor(15U, 12U, 3U);
            Tanks_OpenHorizontalDoor(22U, 12U, 3U);
            Tanks_PlaceRoom(2U, 6U, 8U, 7U);
            Tanks_OpenVerticalDoor(9U, 8U, 3U);
            Tanks_PlaceRoom(22U, 6U, 8U, 7U);
            Tanks_OpenVerticalDoor(22U, 8U, 3U);
            /*
             * Mine map: the middle of every doorway a tank fits through. The
             * side rooms wall over the outer doors of the top and bottom halls.
             */
            Tanks_AddMineSpot(16U, 7U);
            Tanks_AddMineSpot(16U, 12U);
            Tanks_AddMineSpot(9U, 9U);
            Tanks_AddMineSpot(22U, 9U);
            break;
        case 7U: /* OFFSET SUITES */
            Tanks_PlaceRoom(2U, 2U, 14U, 8U);
            Tanks_OpenHorizontalDoor(7U, 9U, 3U);
            Tanks_OpenVerticalDoor(15U, 5U, 3U);
            Tanks_PlaceRoom(16U, 2U, 14U, 8U);
            Tanks_OpenHorizontalDoor(22U, 9U, 3U);
            Tanks_OpenVerticalDoor(16U, 5U, 3U);
            Tanks_PlaceRoom(2U, 10U, 11U, 7U);
            Tanks_OpenHorizontalDoor(6U, 10U, 3U);
            Tanks_OpenVerticalDoor(12U, 13U, 3U);
            Tanks_PlaceRoom(19U, 10U, 11U, 7U);
            Tanks_OpenHorizontalDoor(23U, 10U, 3U);
            Tanks_OpenVerticalDoor(19U, 13U, 3U);
            /* Mine map: the middle of every doorway (the two suites share one, through both walls). */
            Tanks_AddMineSpot(8U, 9U);
            Tanks_AddMineSpot(15U, 6U);
            Tanks_AddMineSpot(23U, 9U);
            Tanks_AddMineSpot(7U, 10U);
            Tanks_AddMineSpot(12U, 14U);
            Tanks_AddMineSpot(24U, 10U);
            Tanks_AddMineSpot(19U, 14U);
            break;
        case 8U: /* ROCKET LAB */
            Tanks_PlaceRoom(2U, 2U, 28U, 6U);
            Tanks_OpenHorizontalDoor(6U, 7U, 3U);
            Tanks_OpenHorizontalDoor(15U, 7U, 3U);
            Tanks_OpenHorizontalDoor(24U, 7U, 3U);
            Tanks_PlaceWallLine(11U, 2U, 5U, true);
            Tanks_OpenVerticalDoor(11U, 4U, 3U);
            Tanks_PlaceWallLine(21U, 2U, 5U, true);
            Tanks_OpenVerticalDoor(21U, 4U, 3U);
            Tanks_PlaceRoom(2U, 11U, 13U, 6U);
            Tanks_OpenHorizontalDoor(7U, 11U, 3U);
            Tanks_PlaceRoom(17U, 11U, 13U, 6U);
            Tanks_OpenHorizontalDoor(22U, 11U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(7U, 7U);
            Tanks_AddMineSpot(16U, 7U);
            Tanks_AddMineSpot(25U, 7U);
            Tanks_AddMineSpot(11U, 5U);
            Tanks_AddMineSpot(21U, 5U);
            Tanks_AddMineSpot(8U, 11U);
            Tanks_AddMineSpot(23U, 11U);
            break;
        default: /* FINAL COMPLEX */
            Tanks_PlaceRoom(2U, 2U, 10U, 7U);
            Tanks_OpenHorizontalDoor(6U, 8U, 3U);
            Tanks_PlaceRoom(12U, 2U, 8U, 7U);
            Tanks_OpenHorizontalDoor(15U, 8U, 3U);
            Tanks_PlaceRoom(20U, 2U, 10U, 7U);
            Tanks_OpenHorizontalDoor(24U, 8U, 3U);
            Tanks_PlaceRoom(2U, 11U, 10U, 6U);
            Tanks_OpenHorizontalDoor(6U, 11U, 3U);
            Tanks_PlaceRoom(20U, 11U, 10U, 6U);
            Tanks_OpenHorizontalDoor(24U, 11U, 3U);
            Tanks_PlaceRoom(12U, 12U, 8U, 5U);
            Tanks_OpenHorizontalDoor(15U, 12U, 3U);
            Tanks_OpenVerticalDoor(12U, 13U, 3U);
            Tanks_OpenVerticalDoor(19U, 13U, 3U);
            /* Mine map: the middle of every doorway. */
            Tanks_AddMineSpot(7U, 8U);
            Tanks_AddMineSpot(16U, 8U);
            Tanks_AddMineSpot(25U, 8U);
            Tanks_AddMineSpot(7U, 11U);
            Tanks_AddMineSpot(25U, 11U);
            Tanks_AddMineSpot(16U, 12U);
            Tanks_AddMineSpot(12U, 14U);
            Tanks_AddMineSpot(19U, 14U);
            break;
    }

    /* Keep the player start and all possible enemy deployment pads clear. */
    for(uint8_t Y = 8U; Y <= 12U; Y++)
    {
        for(uint8_t X = 14U; X <= 18U; X++)
        {
            Tanks_ClearArenaTile(X, Y);
        }
    }
    for(uint8_t Spawn = 0U; Spawn < 5U; Spawn++)
    {
        const uint8_t X = Tanks_SpawnTiles[Spawn][0];
        const uint8_t Y = Tanks_SpawnTiles[Spawn][1];
        Tanks_ClearArenaPad(X, Y);
    }
}

void Tanks_ResetBattlefield(void)
{
    const uint16_t Wave = Tanks_Game.ArenaRound == 0U ? 1U : Tanks_Game.ArenaRound;
    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        for(uint8_t X = 0U; X < TANKS_MAP_WIDTH; X++)
        {
            const bool Border = (X == 0U) || (Y == 0U) || (X == TANKS_MAP_WIDTH - 1U) || (Y == TANKS_MAP_HEIGHT - 1U);
            Tanks_Game.Tiles[Y][X] = Border ? TANKS_TILE_WALL : TANKS_TILE_FLOOR;
            Tanks_Game.TileDamage[Y][X] = 0U;
        }
    }

    Tanks_BuildArena(Wave);
    Tanks_ClearRoundEntities();
}

static void Tanks_ResetPlayer(void)
{
    Tanks_TankTypeDef *Player = &Tanks_Game.Player;
    Player->Position.X = TANKS_FP(16 * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    Player->Position.Y = TANKS_FP(10 * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    Player->HomePosition = Player->Position;
    Player->MoveTarget = Player->Position;
    Player->Heading = 0;
    Player->TurretHeading = 0;
    Player->LinearVelocity = 0;
    Player->AngularVelocity = 0;
    Player->LeftTrack = 0;
    Player->RightTrack = 0;
    Player->MaximumHull = 1;
    Player->Hull = 1;
    Player->ReloadMilliseconds = 0U;
    Player->AimRefreshMilliseconds = 0U;
    Player->InvulnerableMilliseconds = 2200U;
    Player->AiThinkMilliseconds = 0U;
    Player->StuckMilliseconds = 0U;
    Player->AiTurnBias = 0;
    Player->Mines = 0U;
    Player->Type = 0U;
    Player->Group = 0U;
    Player->AiMode = TANKS_AI_GUARD;
    Player->FlashMilliseconds = 0U;
    Player->AimValid = false;
    Player->Awake = true;
    Player->Active = true;
}

void Tanks_ShowMessage(const char *Text, uint16_t Milliseconds)
{
    Tanks_CopyText(Tanks_Game.Message, sizeof(Tanks_Game.Message), Text);
    Tanks_Game.MessageMilliseconds = Milliseconds;
}

void Tanks_StartNewGame(void)
{
    Tanks_Game.Score = 0U;
    Tanks_Game.RunMilliseconds = 0U;
    Tanks_Game.Wave = 0U;
    Tanks_Game.Lives = 3U;
    Tanks_Game.HqMaximumHull = 0U;
    Tanks_Game.HqHull = 0U;
    Tanks_Game.NextSpawnPoint = 0U;
    Tanks_Game.RoundComplete = false;
    Tanks_Game.RoundClearMilliseconds = 0U;
    Tanks_StartWave(1U);
}

void Tanks_StartWave(uint16_t Wave)
{
    static const uint8_t EnemyCounts[CAMPAIGN_WAVES] = { 1U, 2U, 2U, 3U, 3U, 4U, 4U, 5U, 5U, 5U };
    uint8_t EnemyCount;
    Tanks_Game.Wave = Wave;
    /*
     * Rounds 1-10 are the campaign. Beyond it the game is endless: a random
     * arena (never the same one twice running) holding the enemies of a
     * random round from 7 to 10, so it never gets harder than those.
     */
    if(Wave <= CAMPAIGN_WAVES)
    {
        Tanks_Game.ArenaRound = (uint8_t)Wave;
        Tanks_Game.EnemyRound = (uint8_t)Wave;
    }
    else
    {
        const uint8_t PreviousArena = Tanks_Game.ArenaRound;
        do
        {
            Tanks_Game.ArenaRound = (uint8_t)(1U + (Tanks_Random() % CAMPAIGN_WAVES));
        } while(Tanks_Game.ArenaRound == PreviousArena);
        Tanks_Game.EnemyRound = (uint8_t)(ENDLESS_HARDEST_FROM + (Tanks_Random() % (CAMPAIGN_WAVES - ENDLESS_HARDEST_FROM + 1U)));
    }
    EnemyCount = EnemyCounts[Tanks_Game.EnemyRound - 1U];
    Tanks_Game.HqMaximumHull = 0U;
    Tanks_Game.HqHull = 0U;
    Tanks_Game.WaveSpawnRemaining = 0U;
    Tanks_Game.SpawnTimerMilliseconds = 0U;
    Tanks_Game.NextSpawnPoint = 0U;
    Tanks_Game.GroupCount = 0U;
    Tanks_Game.CurrentGroup = 0U;
    Tanks_Game.GroupTransitionMilliseconds = 0U;
    Tanks_Game.RoundComplete = false;
    Tanks_Game.RoundClearMilliseconds = 0U;
    Tanks_ResetBattlefield();
    Tanks_ResetPlayer();
    for(uint8_t Ordinal = 0U; Ordinal < EnemyCount; Ordinal++)
    {
        (void)Tanks_SpawnEnemy(Ordinal, Ordinal, EnemyCount);
    }
    Tanks_Game.Screen = TANKS_SCREEN_WAVE_INTRO;
    Tanks_Game.ScreenMilliseconds = 0U;
    TanksAudio_PlayJingle(TANKS_AUDIO_JINGLE_WAVE_INTRO);
}

uint8_t Tanks_CountActiveEnemies(void)
{
    uint8_t Count = 0U;
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        if(Tanks_Game.Enemies[Index].Active)
        {
            Count++;
        }
    }
    return Count;
}

uint8_t Tanks_CountAwakeEnemies(void)
{
    uint8_t Count = 0U;
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        if(Tanks_Game.Enemies[Index].Active && Tanks_Game.Enemies[Index].Awake)
        {
            Count++;
        }
    }
    return Count;
}

static Tanks_VectorTypeDef Tanks_SpawnPosition(uint8_t Slot)
{
    Tanks_VectorTypeDef Position;
    const uint8_t SafeSlot = (uint8_t)(Slot % 16U);
    Position.X = TANKS_FP((int32_t)Tanks_SpawnTiles[SafeSlot][0] * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    Position.Y = TANKS_FP((int32_t)Tanks_SpawnTiles[SafeSlot][1] * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    return Position;
}

static Tanks_EnemyTypeDef Tanks_ChooseEnemyType(uint8_t Ordinal, uint8_t Total)
{
    (void)Total;
    switch(Tanks_Game.EnemyRound)
    {
        case 1U:
            return TANKS_ENEMY_DUMB;
        case 2U:
        {
            static const Tanks_EnemyTypeDef Types[2] = { TANKS_ENEMY_DUMB, TANKS_ENEMY_RICOCHET };
            return Types[Ordinal % 2U];
        }
        case 3U:
        {
            static const Tanks_EnemyTypeDef Types[2] = { TANKS_ENEMY_DUMB, TANKS_ENEMY_MINELAYER };
            return Types[Ordinal % 2U];
        }
        case 4U:
        {
            static const Tanks_EnemyTypeDef Types[3] = { TANKS_ENEMY_DUMB, TANKS_ENEMY_ROCKET, TANKS_ENEMY_RICOCHET };
            return Types[Ordinal % 3U];
        }
        case 5U:
        {
            static const Tanks_EnemyTypeDef Types[3] = { TANKS_ENEMY_DUMB, TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_MINELAYER };
            return Types[Ordinal % 3U];
        }
        case 6U:
        {
            static const Tanks_EnemyTypeDef Types[4] = { TANKS_ENEMY_RICOCHET, TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_HUNTER_RICOCHET, TANKS_ENEMY_MINELAYER };
            return Types[Ordinal % 4U];
        }
        case 7U:
        {
            static const Tanks_EnemyTypeDef Types[4] = { TANKS_ENEMY_ROCKET, TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_HUNTER_ROCKET, TANKS_ENEMY_MINELAYER };
            return Types[Ordinal % 4U];
        }
        case 8U:
        {
            static const Tanks_EnemyTypeDef Types[5] = { TANKS_ENEMY_DUMB, TANKS_ENEMY_RICOCHET, TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_HUNTER_RICOCHET, TANKS_ENEMY_ROCKET };
            return Types[Ordinal % 5U];
        }
        case 9U:
        {
            static const Tanks_EnemyTypeDef Types[5] = { TANKS_ENEMY_MINELAYER, TANKS_ENEMY_ROCKET, TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_HUNTER_RICOCHET, TANKS_ENEMY_HUNTER_ROCKET };
            return Types[Ordinal % 5U];
        }
        default:
        {
            static const Tanks_EnemyTypeDef Types[5] = { TANKS_ENEMY_RICOCHET, TANKS_ENEMY_MINELAYER, TANKS_ENEMY_ROCKET, TANKS_ENEMY_HUNTER_RICOCHET, TANKS_ENEMY_HUNTER_ROCKET };
            return Types[Ordinal % 5U];
        }
    }
}

static bool Tanks_EnemyTargetsPlayer(Tanks_EnemyTypeDef Type)
{
    return (Type == TANKS_ENEMY_HUNTER_DIRECT) || (Type == TANKS_ENEMY_HUNTER_RICOCHET) || (Type == TANKS_ENEMY_HUNTER_ROCKET);
}

static bool Tanks_EnemyUsesRicochet(Tanks_EnemyTypeDef Type)
{
    return (Type == TANKS_ENEMY_RICOCHET) || (Type == TANKS_ENEMY_HUNTER_RICOCHET);
}

static bool Tanks_EnemyUsesRocket(Tanks_EnemyTypeDef Type)
{
    return (Type == TANKS_ENEMY_ROCKET) || (Type == TANKS_ENEMY_HUNTER_ROCKET);
}

static uint8_t Tanks_InitialEnemyMode(Tanks_EnemyTypeDef Type, uint8_t Ordinal)
{
    (void)Ordinal;
    if(Tanks_EnemyTargetsPlayer(Type))
    {
        return TANKS_AI_CHASE;
    }
    return TANKS_AI_PATROL;
}

static bool Tanks_SpawnEnemy(uint8_t Slot, uint8_t Ordinal, uint8_t Total)
{
    Tanks_VectorTypeDef Position;
    Tanks_EnemyTypeDef Type;
    Tanks_TankTypeDef *Enemy = NULL;
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        if(!Tanks_Game.Enemies[Index].Active)
        {
            Enemy = &Tanks_Game.Enemies[Index];
            break;
        }
    }
    if(Enemy == NULL)
    {
        return false;
    }
    Position = Tanks_SpawnPosition(Slot);
    if(Tanks_TanksOverlap(Position, ENEMY_RADIUS, NULL))
    {
        return false;
    }
    Type = Tanks_ChooseEnemyType(Ordinal, Total);
    Enemy->Position = Position;
    Enemy->HomePosition = Position;
    Enemy->MoveTarget = Position;
    Enemy->Heading = (int16_t)((Ordinal * 613U + (Tanks_Random() % 500U)) % TANKS_ANGLE_FULL);
    Enemy->TurretHeading = Enemy->Heading;
    Enemy->LinearVelocity = 0;
    Enemy->AngularVelocity = 0;
    Enemy->LeftTrack = 0;
    Enemy->RightTrack = 0;
    Enemy->Type = (uint8_t)Type;
    Enemy->Group = 0U;
    Enemy->AiMode = Tanks_InitialEnemyMode(Type, Ordinal);
    Enemy->MaximumHull = 1;
    Enemy->Hull = 1;
    Enemy->ReloadMilliseconds = Type == TANKS_ENEMY_MINELAYER ? 450U : (uint16_t)(900U + (Tanks_Random() % 900U));
    Enemy->AimRefreshMilliseconds = 0U;
    Enemy->InvulnerableMilliseconds = 550U;
    Enemy->AiThinkMilliseconds = (uint16_t)(900U + (Tanks_Random() % 2300U));
    Enemy->StuckMilliseconds = 0U;
    Enemy->AiTurnBias = (Tanks_Random() & 1U) != 0U ? 500 : -500;
    Enemy->Mines = 0U;
    Enemy->MineSpot = NO_MINE_SPOT;
    Enemy->FlashMilliseconds = 0U;
    Enemy->AimValid = false;
    Enemy->Awake = true;
    Enemy->Active = true;
    return true;
}

static Tanks_ParticleTypeDef *Tanks_AllocateParticle(void)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_PARTICLES; Index++)
    {
        if(!Tanks_Game.Particles[Index].Active)
        {
            return &Tanks_Game.Particles[Index];
        }
    }
    return &Tanks_Game.Particles[Tanks_Random() % TANKS_MAX_PARTICLES];
}

void Tanks_SpawnExplosion(Tanks_VectorTypeDef Position, uint8_t Strength)
{
    const uint8_t Count = (uint8_t)Tanks_Clamp32(8 + Strength, 8, 30);
    TanksAudio_PlayExplosion(Position, Strength);
    Tanks_Game.CameraKickMilliseconds = (uint16_t)Tanks_Clamp32(70 + ((int32_t)Strength * 8), 70, 260);
    for(uint8_t Index = 0U; Index < Count; Index++)
    {
        Tanks_ParticleTypeDef *Particle = Tanks_AllocateParticle();
        const int16_t Angle = (int16_t)(Tanks_Random() % TANKS_ANGLE_FULL);
        const int32_t Speed = 25 + (int32_t)(Tanks_Random() % (uint32_t)(65 + Strength * 3U));
        Particle->Position = Position;
        Particle->Velocity.X = (Tanks_Sine(Angle) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Particle->Velocity.Y = (-Tanks_Cosine(Angle) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Particle->MaximumLifeMilliseconds = (uint16_t)(300U + (Tanks_Random() % 750U));
        Particle->LifeMilliseconds = Particle->MaximumLifeMilliseconds;
        Particle->Size = (uint8_t)(2U + (Tanks_Random() % 7U));
        Particle->Colour = (Index % 3U) == 0U ? TANKS_COLOUR_FIRE_LIGHT : ((Index % 3U) == 1U ? TANKS_COLOUR_FIRE : TANKS_COLOUR_SMOKE);
        Particle->Active = true;
    }
}

void Tanks_AwardScore(Tanks_VectorTypeDef Position, uint16_t BaseScore)
{
    (void)Position;
    (void)BaseScore;
}

static void Tanks_AddWreck(const Tanks_TankTypeDef *Tank)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_WRECKS; Index++)
    {
        Tanks_WreckTypeDef *Wreck = &Tanks_Game.Wrecks[Index];
        if(Wreck->Active)
        {
            continue;
        }
        Wreck->Position = Tank->Position;
        Wreck->Heading = Tank->Heading;
        Wreck->TurretHeading = Tank->TurretHeading;
        Wreck->Type = Tank == &Tanks_Game.Player ? 0xFFU : Tank->Type;
        Wreck->Active = true;
        return;
    }
}

static void Tanks_DestroyEnemy(Tanks_TankTypeDef *Enemy, bool LeaveWreck)
{
    const Tanks_VectorTypeDef Position = Enemy->Position;
    if(LeaveWreck)
    {
        Tanks_AddWreck(Enemy);
    }
    Enemy->Active = false;
    Tanks_SpawnExplosion(Position, 11U);
}

static void Tanks_DestroyPlayer(bool LeaveWreck, const char *Message)
{
    Tanks_TankTypeDef *Player = &Tanks_Game.Player;
#if PLAYER_INVINCIBLE
    (void)LeaveWreck;
    (void)Message;
    return;
#endif
    if(!Player->Active)
    {
        return;
    }
    if(LeaveWreck)
    {
        Tanks_AddWreck(Player);
    }
    Tanks_SpawnExplosion(Player->Position, 19U);
    Player->Active = false;
    if(Tanks_Game.Demo)
    {
        return;
    }
    if(Tanks_Game.Lives > 0U)
    {
        Tanks_Game.Lives--;
    }
    if(Tanks_Game.Lives == 0U)
    {
        Tanks_Game.Screen = TANKS_SCREEN_GAME_OVER;
        Tanks_Game.ScreenMilliseconds = 0U;
        TanksAudio_PlayJingle(TANKS_AUDIO_JINGLE_GAME_OVER);
        Tanks_CheckRecord();
    }
    else
    {
        TanksAudio_PlayJingle(TANKS_AUDIO_JINGLE_TANK_LOST);
        Tanks_ResetPlayer();
        if(Message != NULL)
        {
            Tanks_ShowMessage(Message, 1500U);
        }
    }
}

/*
 * An explosion at Position. Every tank whose hull reaches within Radius is
 * destroyed, the player included (unless just respawned and protected), and
 * any mine caught in the blast goes off too, so mines can chain.
 */
static void Tanks_Detonate(Tanks_VectorTypeDef Position, uint32_t Radius, uint8_t Strength)
{
    Tanks_BlastTypeDef *Ring = &Tanks_Game.Blasts[0];

    Tanks_SpawnExplosion(Position, Strength);

    for(uint8_t Index = 0U; Index < TANKS_MAX_BLASTS; Index++)
    {
        if(!Tanks_Game.Blasts[Index].Active || (Tanks_Game.Blasts[Index].LifeMilliseconds < Ring->LifeMilliseconds))
        {
            Ring = &Tanks_Game.Blasts[Index];
        }

        if(!Ring->Active)
        {
            break;
        }
    }
    Ring->Position = Position;
    Ring->Radius = (uint16_t)Radius;
    Ring->LifeMilliseconds = TANKS_BLAST_RING_MS;
    Ring->Active = true;

    if(Tanks_Game.Player.Active && (Tanks_DistancePixels(Position, Tanks_Game.Player.Position) <= Radius + (PLAYER_RADIUS / 2U)))
    {
        Tanks_DamagePlayer(1U);
    }

    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];

        if(Enemy->Active && (Tanks_DistancePixels(Position, Enemy->Position) <= Radius + (ENEMY_RADIUS / 2U)))
        {
            Tanks_DestroyEnemy(Enemy, true);
        }
    }

    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[Index];

        if(Mine->Active && (Tanks_DistancePixels(Position, Mine->Position) <= Radius))
        {
            Mine->Active = false;
            Tanks_Detonate(Mine->Position, MINE_BLAST_RADIUS, 14U);
        }
    }
}

void Tanks_DamagePlayer(uint8_t Damage)
{
    (void)Damage;
    if(!Tanks_Game.Player.Active || (Tanks_Game.Player.InvulnerableMilliseconds > 0U))
    {
        return;
    }
    Tanks_DestroyPlayer(true, NULL);
}

void Tanks_DamageHq(uint8_t Damage)
{
    (void)Damage;
}

static Tanks_BulletTypeDef *Tanks_AllocateBullet(void)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
    {
        if(!Tanks_Game.Bullets[Index].Active)
        {
            return &Tanks_Game.Bullets[Index];
        }
    }
    return NULL;
}

static bool Tanks_Fire(Tanks_TankTypeDef *Tank, uint8_t Owner)
{
    Tanks_BulletTypeDef *Bullet;
    const int16_t Heading = Tank->Heading;
    int32_t Speed = RICOCHET_PLAYER_SPEED;
    int16_t MuzzleDistance = Owner == PLAYER_OWNER ? 47 : 41;
    uint8_t ProjectileType = TANKS_PROJECTILE_RICOCHET;
    if(Owner != PLAYER_OWNER)
    {
        const Tanks_EnemyTypeDef Type = (Tanks_EnemyTypeDef)Tank->Type;
        if(Tanks_EnemyUsesRocket(Type))
        {
            Speed = 225;
            ProjectileType = TANKS_PROJECTILE_ROCKET;
            MuzzleDistance = 44;
        }
        else if(Tanks_EnemyUsesRicochet(Type))
        {
            Speed = RICOCHET_ENEMY_SPEED + 20;
        }
        else
        {
            Speed = RICOCHET_ENEMY_SPEED;
        }
    }
    if(!Tank->Active || (Tank->ReloadMilliseconds > 0U))
    {
        return false;
    }
    Bullet = Tanks_AllocateBullet();
    if(Bullet == NULL)
    {
        return false;
    }
    Bullet->Position.X = Tank->Position.X + ((Tanks_Sine(Heading) * MuzzleDistance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    Bullet->Position.Y = Tank->Position.Y - ((Tanks_Cosine(Heading) * MuzzleDistance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    Bullet->Velocity.X = (Tanks_Sine(Heading) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
    Bullet->Velocity.Y = (-Tanks_Cosine(Heading) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
    Bullet->Heading = Heading;
    Bullet->LifeMilliseconds = ProjectileType == TANKS_PROJECTILE_ROCKET ? 4200U : 4800U;
    Bullet->Damage = 1U;
    Bullet->Owner = Owner;
    Bullet->Bounces = 0U;
    Bullet->Type = ProjectileType;
    Bullet->Active = true;
    Tank->TurretHeading = Tank->Heading;
    Tank->LinearVelocity = (int16_t)Tanks_Clamp32((int32_t)Tank->LinearVelocity - 18, -150, 150);
    if(Owner == PLAYER_OWNER)
    {
        Tank->ReloadMilliseconds = PLAYER_RELOAD_MS;
    }
    else if(Tanks_EnemyUsesRocket((Tanks_EnemyTypeDef)Tank->Type))
    {
        Tank->ReloadMilliseconds = 3300U;
    }
    else if(Tanks_EnemyUsesRicochet((Tanks_EnemyTypeDef)Tank->Type))
    {
        Tank->ReloadMilliseconds = 1950U;
    }
    else
    {
        Tank->ReloadMilliseconds = Tanks_Game.EnemyRound == 1U ? 3000U : 1650U;
    }

    for(uint8_t Index = 0U; Index < 3U; Index++)
    {
        Tanks_ParticleTypeDef *Particle = Tanks_AllocateParticle();
        Particle->Position = Bullet->Position;
        Particle->Velocity.X = -Bullet->Velocity.X / 10;
        Particle->Velocity.Y = -Bullet->Velocity.Y / 10;
        Particle->LifeMilliseconds = 120U;
        Particle->MaximumLifeMilliseconds = 120U;
        Particle->Size = (uint8_t)(3U + Index);
        Particle->Colour = TANKS_COLOUR_FIRE_LIGHT;
        Particle->Active = true;
    }
    Tanks_Game.CameraKickMilliseconds = Owner == PLAYER_OWNER ? 45U : 28U;
    TanksAudio_PlayFire(Bullet->Position, Owner == PLAYER_OWNER, ProjectileType == TANKS_PROJECTILE_ROCKET);
    return true;
}

static bool Tanks_MoveTank(Tanks_TankTypeDef *Tank, int16_t Radius, uint32_t DeltaMilliseconds)
{
    const int32_t AverageTrack = ((int32_t)Tank->LeftTrack + Tank->RightTrack) / 2;
    int32_t MaximumSpeed = PLAYER_SPEED;
    if(Tank != &Tanks_Game.Player)
    {
        if(Tank->Type == TANKS_ENEMY_MINELAYER)
        {
            MaximumSpeed = 204;
        }
        else if(Tanks_EnemyUsesRocket((Tanks_EnemyTypeDef)Tank->Type))
        {
            MaximumSpeed = 62;
        }
        else
        {
            MaximumSpeed = 76;
        }
        if(Tanks_EnemyTargetsPlayer((Tanks_EnemyTypeDef)Tank->Type))
        {
            MaximumSpeed = (MaximumSpeed * HUNTER_SPEED_PERCENT) / 100;
        }
    }
    const int32_t SpeedTarget = (AverageTrack * MaximumSpeed) / 1000;
    int32_t TurnTarget = (((int32_t)Tank->LeftTrack - Tank->RightTrack) * PLAYER_TURN_RATE) / 2000;
    Tanks_VectorTypeDef Candidate;
    bool Moved = false;
    const bool AvoidPits = Tank != &Tanks_Game.Player;
    if((Tank != &Tanks_Game.Player) && !Tanks_EnemyTargetsPlayer((Tanks_EnemyTypeDef)Tank->Type))
    {
        TurnTarget = (TurnTarget * 74) / 100;
    }
    if((Tank != &Tanks_Game.Player) && (Tank->Type == TANKS_ENEMY_MINELAYER))
    {
        /* The mine-layer turns three times as fast as other basic tanks, to match its speed. */
        TurnTarget *= 3;
    }
    Tank->LinearVelocity = (int16_t)(Tank->LinearVelocity + (((SpeedTarget - Tank->LinearVelocity) * 11 * (int32_t)DeltaMilliseconds) / 1000));
    Tank->AngularVelocity = (int16_t)(Tank->AngularVelocity + (((TurnTarget - Tank->AngularVelocity) * 11 * (int32_t)DeltaMilliseconds) / 1000));
    Tank->Heading = Tanks_NormalizeAngle((int32_t)Tank->Heading + (((int32_t)Tank->AngularVelocity * (int32_t)DeltaMilliseconds) / 1000));

    Candidate = Tank->Position;
    Candidate.X += (Tanks_Sine(Tank->Heading) * Tank->LinearVelocity * (int32_t)DeltaMilliseconds * TANKS_FP_ONE) / (TANKS_TRIG_ONE * 1000);
    if(!Tanks_PositionBlocked(Candidate, Radius, AvoidPits) && !Tanks_TanksOverlap(Candidate, Radius, Tank))
    {
        Tank->Position.X = Candidate.X;
        Moved = true;
    }
    Candidate = Tank->Position;
    Candidate.Y -= (Tanks_Cosine(Tank->Heading) * Tank->LinearVelocity * (int32_t)DeltaMilliseconds * TANKS_FP_ONE) / (TANKS_TRIG_ONE * 1000);
    if(!Tanks_PositionBlocked(Candidate, Radius, AvoidPits) && !Tanks_TanksOverlap(Candidate, Radius, Tank))
    {
        Tank->Position.Y = Candidate.Y;
        Moved = true;
    }

    if(!Moved && ((Tank->LinearVelocity > 8) || (Tank->LinearVelocity < -8)))
    {
        Tank->LinearVelocity /= 3;
        Tank->StuckMilliseconds = (uint16_t)Tanks_Clamp32(Tank->StuckMilliseconds + DeltaMilliseconds, 0, 4000);
    }
    else if(Moved)
    {
        Tank->StuckMilliseconds = 0U;
    }

    if((Tank == &Tanks_Game.Player) && (Tanks_TileAtPosition(Tank->Position) == TANKS_TILE_PIT))
    {
        Tanks_DestroyPlayer(false, "WATCH THE PITS");
        return false;
    }
    return true;
}

static bool Tanks_LineOfSight(Tanks_VectorTypeDef Start, Tanks_VectorTypeDef End)
{
    const int32_t DeltaX = End.X - Start.X;
    const int32_t DeltaY = End.Y - Start.Y;
    const uint32_t Distance = Tanks_DistancePixels(Start, End);
    const uint16_t Steps = (uint16_t)Tanks_Clamp32((int32_t)(Distance / 6U), 2, 160);
    for(uint16_t Step = 1U; Step < Steps; Step++)
    {
        Tanks_VectorTypeDef Position;
        uint8_t Tile;
        Position.X = Start.X + ((DeltaX * Step) / Steps);
        Position.Y = Start.Y + ((DeltaY * Step) / Steps);
        Tile = Tanks_TileAtPosition(Position);
        if(Tile == TANKS_TILE_WALL)
        {
            return false;
        }
    }
    return true;
}

/*
 * The demo's autopilot, for the launcher preview: the player's tank hunts the
 * enemy with the same steering the enemies use. In sight, it turns to face
 * the enemy and holds a fighting distance; out of sight, it roams. Returns
 * whether it is lined up to fire.
 */
static bool Tanks_DemoPilot(Tanks_TankTypeDef *Player, uint32_t DeltaMilliseconds)
{
    const Tanks_TankTypeDef *Target = NULL;
    int16_t Heading;
    int16_t Speed;
    int16_t Error;
    bool Visible;

    for(uint8_t Index = 0U; (Index < TANKS_MAX_ENEMIES) && (Target == NULL); Index++)
    {
        Target = Tanks_Game.Enemies[Index].Active ? &Tanks_Game.Enemies[Index] : NULL;
    }
    if(Target == NULL)
    {
        Player->LeftTrack = 0;
        Player->RightTrack = 0;
        return false;
    }

    Visible = Tanks_LineOfSight(Player->Position, Target->Position);
    if(Visible)
    {
        Heading = Tanks_AngleTo(Player->Position, Target->Position);
        Speed = (int16_t)Tanks_Clamp32(((int32_t)Tanks_DistancePixels(Player->Position, Target->Position) - DEMO_FIGHT_RANGE) * 4, -300, 600);
    }
    else
    {
        if((Player->AiThinkMilliseconds <= DeltaMilliseconds) || (Tanks_DistancePixels(Player->Position, Player->MoveTarget) < 42U))
        {
            Player->HomePosition = Target->Position;
            Player->MoveTarget = Tanks_EnemyRandomOpenPoint(Player, 220);
            Player->AiThinkMilliseconds = 2500U;
        }
        else
        {
            Player->AiThinkMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        Heading = Tanks_AngleTo(Player->Position, Player->MoveTarget);
        Speed = 600;
    }

    Error = Tanks_NormalizeAngle((int32_t)Player->Heading - Heading);
    return Tanks_SteerTank(Player, Heading, Speed, Visible) && (Error < 28) && (Error > -28);
}

static uint8_t Tanks_CountPlayerMines(void)
{
    uint8_t Count = 0U;

    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        Count += (Tanks_Game.Mines[Index].Active && (Tanks_Game.Mines[Index].Owner == PLAYER_OWNER)) ? 1U : 0U;
    }

    return Count;
}

static void Tanks_UpdatePlayer(uint32_t DeltaMilliseconds)
{
    Tanks_TankTypeDef *Player = &Tanks_Game.Player;
    bool Fire;
    if(!Player->Active)
    {
        return;
    }
    if(Tanks_Game.Demo)
    {
        Fire = Tanks_DemoPilot(Player, DeltaMilliseconds);
    }
    else
    {
        Player->LeftTrack = Tanks_Game.Input.LeftTrack;
        Player->RightTrack = Tanks_Game.Input.RightTrack;
        Fire = Tanks_Game.Input.Primary.Pressed;
    }
    if(!Tanks_MoveTank(Player, PLAYER_RADIUS, DeltaMilliseconds))
    {
        return;
    }
    Player->TurretHeading = Player->Heading;

    /* A short tap fires exactly one shot immediately. Holding the button never charges or repeats. */
    if(Fire)
    {
        (void)Tanks_Fire(Player, PLAYER_OWNER);
    }

    /* Secondary lays a mine behind the tank, a few at a time. */
    if(!Tanks_Game.Demo && Tanks_Game.Input.Secondary.Pressed && (Tanks_CountPlayerMines() < PLAYER_MAX_MINES))
    {
        (void)Tanks_DropMine(Player, PLAYER_OWNER);
    }

    if((Player->LinearVelocity > 18) || (Player->LinearVelocity < -18))
    {
        if(Tanks_Game.TrackMarkTimerMilliseconds <= DeltaMilliseconds)
        {
            for(uint8_t Index = 0U; Index < TANKS_MAX_TRACK_MARKS; Index++)
            {
                Tanks_TrackMarkTypeDef *Mark = &Tanks_Game.TrackMarks[Index];
                if(Mark->Active)
                {
                    continue;
                }
                Mark->Position = Player->Position;
                Mark->Heading = Player->Heading;
                Mark->LifeMilliseconds = 9000U;
                Mark->Active = true;
                break;
            }
            Tanks_Game.TrackMarkTimerMilliseconds = 75U;
        }
        else
        {
            Tanks_Game.TrackMarkTimerMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
    }
}

static Tanks_VectorTypeDef Tanks_PointAhead(Tanks_VectorTypeDef Position, int16_t Heading, int16_t Distance)
{
    Tanks_VectorTypeDef Result;
    Result.X = Position.X + ((Tanks_Sine(Heading) * Distance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    Result.Y = Position.Y - ((Tanks_Cosine(Heading) * Distance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    return Result;
}

static bool Tanks_EnemyProbeBlocked(const Tanks_TankTypeDef *Enemy, int16_t Heading, int16_t Distance)
{
    const Tanks_VectorTypeDef Probe = Tanks_PointAhead(Enemy->Position, Heading, Distance);

    /* The player's mines count as obstacles: tanks steer around them rather than drive over them. */
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        const Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[Index];
        if(Mine->Active && (Mine->Owner == PLAYER_OWNER) && (Tanks_DistancePixels(Probe, Mine->Position) <= MINE_AVOID_DISTANCE))
        {
            return true;
        }
    }

    return Tanks_PositionBlocked(Probe, ENEMY_RADIUS, true);
}

static Tanks_VectorTypeDef Tanks_EnemyRandomOpenPoint(const Tanks_TankTypeDef *Enemy, int16_t MaximumDistance)
{
    for(uint8_t Attempt = 0U; Attempt < 10U; Attempt++)
    {
        const int16_t Angle = (int16_t)(Tanks_Random() % TANKS_ANGLE_FULL);
        const int16_t Distance = (int16_t)(70 + (Tanks_Random() % (uint32_t)(MaximumDistance - 69)));
        Tanks_VectorTypeDef Candidate = Tanks_PointAhead(Enemy->HomePosition, Angle, Distance);
        const int32_t PixelX = Candidate.X >> TANKS_FP_SHIFT;
        const int32_t PixelY = Candidate.Y >> TANKS_FP_SHIFT;
        if((PixelX < 45) || (PixelY < 45) || (PixelX > TANKS_WORLD_WIDTH - 45) || (PixelY > TANKS_WORLD_HEIGHT - 45))
        {
            continue;
        }
        if(!Tanks_PositionBlocked(Candidate, ENEMY_RADIUS, true))
        {
            return Candidate;
        }
    }
    return Enemy->HomePosition;
}

static void Tanks_SelectEnemyMode(Tanks_TankTypeDef *Enemy, uint8_t EnemyIndex, uint32_t PlayerDistance)
{
    (void)EnemyIndex;
    (void)PlayerDistance;
    if(Tanks_EnemyTargetsPlayer((Tanks_EnemyTypeDef)Enemy->Type))
    {
        Enemy->AiMode = TANKS_AI_CHASE;
        Enemy->MoveTarget = Tanks_Game.Player.Position;
        Enemy->AiThinkMilliseconds = (uint16_t)(420U + (Tanks_Random() % 360U));
    }
    else
    {
        Enemy->AiMode = TANKS_AI_PATROL;
        Enemy->MoveTarget = Tanks_EnemyRandomOpenPoint(Enemy, 260);
        Enemy->AiThinkMilliseconds = (uint16_t)(1000U + (Tanks_Random() % 1900U));
    }
}

static bool Tanks_PathNodePassable(int16_t NodeX, int16_t NodeY)
{
    Tanks_VectorTypeDef Centre;
    if((NodeX <= 0) || (NodeY <= 0) || (NodeX >= TANKS_MAP_WIDTH - 2) || (NodeY >= TANKS_MAP_HEIGHT - 2))
    {
        return false;
    }
    for(int16_t OffsetY = 0; OffsetY < 2; OffsetY++)
    {
        for(int16_t OffsetX = 0; OffsetX < 2; OffsetX++)
        {
            if(Tanks_TileBlocksTank(Tanks_Game.Tiles[NodeY + OffsetY][NodeX + OffsetX], true))
            {
                return false;
            }
        }
    }
    Centre.X = TANKS_FP((int32_t)(NodeX + 1) * TANKS_TILE_SIZE);
    Centre.Y = TANKS_FP((int32_t)(NodeY + 1) * TANKS_TILE_SIZE);
    return !Tanks_PositionBlocked(Centre, ENEMY_RADIUS, true);
}

static uint16_t Tanks_ClosestPathNode(Tanks_VectorTypeDef Position)
{
    const int16_t TileX = (int16_t)((Position.X >> TANKS_FP_SHIFT) / TANKS_TILE_SIZE);
    const int16_t TileY = (int16_t)((Position.Y >> TANKS_FP_SHIFT) / TANKS_TILE_SIZE);
    uint16_t Best = UINT16_MAX;
    uint32_t BestDistance = UINT32_MAX;
    for(int16_t NodeY = TileY - 1; NodeY <= TileY; NodeY++)
    {
        for(int16_t NodeX = TileX - 1; NodeX <= TileX; NodeX++)
        {
            Tanks_VectorTypeDef Centre;
            uint32_t Distance;
            if(!Tanks_PathNodePassable(NodeX, NodeY))
            {
                continue;
            }
            Centre.X = TANKS_FP((int32_t)(NodeX + 1) * TANKS_TILE_SIZE);
            Centre.Y = TANKS_FP((int32_t)(NodeY + 1) * TANKS_TILE_SIZE);
            Distance = Tanks_DistancePixels(Position, Centre);
            if(Distance < BestDistance)
            {
                BestDistance = Distance;
                Best = (uint16_t)(NodeY * (TANKS_MAP_WIDTH - 1) + NodeX);
            }
        }
    }
    return Best;
}

/* The next waypoint on the shortest path from the enemy to Target, going round the player's tank if AroundPlayer. */
static Tanks_VectorTypeDef Tanks_PathWaypointTo(const Tanks_TankTypeDef *Enemy, Tanks_VectorTypeDef Target, bool AroundPlayer)
{
    enum
    {
        PATH_WIDTH = TANKS_MAP_WIDTH - 1,
        PATH_HEIGHT = TANKS_MAP_HEIGHT - 1,
        PATH_COUNT = PATH_WIDTH * PATH_HEIGHT
    };
    static int16_t Parent[PATH_COUNT];
    static uint16_t Queue[PATH_COUNT];
    static const int8_t Delta[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    const uint16_t Start = Tanks_ClosestPathNode(Enemy->Position);
    const uint16_t Goal = Tanks_ClosestPathNode(Target);
    uint16_t Read = 0U;
    uint16_t Write = 0U;
    Tanks_VectorTypeDef Result = Target;
    if((Start == UINT16_MAX) || (Goal == UINT16_MAX))
    {
        return Result;
    }
    for(uint16_t Index = 0U; Index < PATH_COUNT; Index++)
    {
        Parent[Index] = -1;
    }
    Parent[Start] = (int16_t)Start;
    Queue[Write++] = Start;
    while(Read < Write)
    {
        const uint16_t Current = Queue[Read++];
        const int16_t X = (int16_t)(Current % PATH_WIDTH);
        const int16_t Y = (int16_t)(Current / PATH_WIDTH);
        if(Current == Goal)
        {
            break;
        }
        for(uint8_t Direction = 0U; Direction < 4U; Direction++)
        {
            const int16_t NextX = (int16_t)(X + Delta[Direction][0]);
            const int16_t NextY = (int16_t)(Y + Delta[Direction][1]);
            uint16_t Next;
            if((NextX < 0) || (NextY < 0) || (NextX >= PATH_WIDTH) || (NextY >= PATH_HEIGHT))
            {
                continue;
            }
            Next = (uint16_t)(NextY * PATH_WIDTH + NextX);
            if(Parent[Next] >= 0)
            {
                continue;
            }
            if(!Tanks_PathNodePassable(NextX, NextY))
            {
                continue;
            }
            if(AroundPlayer && Tanks_Game.Player.Active)
            {
                Tanks_VectorTypeDef Centre;
                Centre.X = TANKS_FP((int32_t)(NextX + 1) * TANKS_TILE_SIZE);
                Centre.Y = TANKS_FP((int32_t)(NextY + 1) * TANKS_TILE_SIZE);
                if(Tanks_DistancePixels(Centre, Tanks_Game.Player.Position) < PATH_PLAYER_CLEARANCE)
                {
                    continue;
                }
            }
            Parent[Next] = (int16_t)Current;
            Queue[Write++] = Next;
        }
    }
    if(Parent[Goal] < 0)
    {
        return Result;
    }
    {
        uint16_t Cursor = Goal;
        while((uint16_t)Parent[Cursor] != Start && (uint16_t)Parent[Cursor] != Cursor)
        {
            Cursor = (uint16_t)Parent[Cursor];
        }
        Result.X = TANKS_FP((int32_t)((Cursor % PATH_WIDTH) + 1) * TANKS_TILE_SIZE);
        Result.Y = TANKS_FP((int32_t)((Cursor / PATH_WIDTH) + 1) * TANKS_TILE_SIZE);
    }
    return Result;
}

static bool Tanks_RicochetRayHitsPlayer(Tanks_VectorTypeDef Start, int16_t Heading)
{
    Tanks_VectorTypeDef Position = Tanks_PointAhead(Start, Heading, 41);
    int32_t StepX = (Tanks_Sine(Heading) * 6 * TANKS_FP_ONE) / TANKS_TRIG_ONE;
    int32_t StepY = (-Tanks_Cosine(Heading) * 6 * TANKS_FP_ONE) / TANKS_TRIG_ONE;
    uint8_t Bounces = 0U;
    for(uint16_t Step = 0U; Step < 190U; Step++)
    {
        Tanks_VectorTypeDef Candidate = Position;
        bool Bounced = false;
        Candidate.X += StepX;
        if(Tanks_TileBlocksBullet(Tanks_TileAtPosition(Candidate)))
        {
            StepX = -StepX;
            Bounced = true;
        }
        else
        {
            Position.X = Candidate.X;
        }
        Candidate = Position;
        Candidate.Y += StepY;
        if(Tanks_TileBlocksBullet(Tanks_TileAtPosition(Candidate)))
        {
            StepY = -StepY;
            Bounced = true;
        }
        else
        {
            Position.Y = Candidate.Y;
        }
        if(Bounced)
        {
            Bounces++;
            if(Bounces > RICOCHET_MAX_BOUNCES)
            {
                return false;
            }
        }
        if(Tanks_DistancePixels(Position, Tanks_Game.Player.Position) <= PLAYER_RADIUS + BULLET_RADIUS)
        {
            return true;
        }
    }
    return false;
}

static bool Tanks_FindRicochetHeading(const Tanks_TankTypeDef *Enemy, int16_t *Heading)
{
    const int16_t Direct = Tanks_AngleTo(Enemy->Position, Tanks_Game.Player.Position);
    for(int16_t Offset = 0; Offset <= TANKS_ANGLE_HALF; Offset += 20)
    {
        const int16_t CandidateA = Tanks_NormalizeAngle((int32_t)Direct + Offset);
        if(Tanks_RicochetRayHitsPlayer(Enemy->Position, CandidateA))
        {
            *Heading = CandidateA;
            return true;
        }
        if(Offset != 0)
        {
            const int16_t CandidateB = Tanks_NormalizeAngle((int32_t)Direct - Offset);
            if(Tanks_RicochetRayHitsPlayer(Enemy->Position, CandidateB))
            {
                *Heading = CandidateB;
                return true;
            }
        }
    }
    return false;
}

/* Heading that leads the player: where they will be when a shot at this speed arrives. */
static int16_t Tanks_LeadHeading(const Tanks_TankTypeDef *Enemy, int32_t BulletSpeed)
{
    const Tanks_TankTypeDef *Player = &Tanks_Game.Player;
    const int32_t FlightMilliseconds = (int32_t)((Tanks_DistancePixels(Enemy->Position, Player->Position) * 1000U) / (uint32_t)BulletSpeed);
    const int32_t Travel = Tanks_Clamp32(((int32_t)Player->LinearVelocity * FlightMilliseconds) / 1000, -200, 200);
    return Tanks_AngleTo(Enemy->Position, Tanks_PointAhead(Player->Position, Player->Heading, (int16_t)Travel));
}

/* Put a mine down at Position; Owner is 0 for the player, or the enemy's index + 1. */
static bool Tanks_PlaceMine(Tanks_VectorTypeDef Position, uint8_t Owner, uint16_t LifeMilliseconds)
{
    if(Tanks_TileAtPosition(Position) != TANKS_TILE_FLOOR)
    {
        return false;
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[Index];
        if(Mine->Active)
        {
            continue;
        }
        Mine->Position = Position;
        Mine->ArmMilliseconds = 700U;
        Mine->LifeMilliseconds = LifeMilliseconds;
        Mine->Owner = Owner;
        Mine->OwnerClear = false;
        Mine->Active = true;
        TanksAudio_PlayMineDropped(Position);
        return true;
    }
    return false;
}

/* Lay a mine just behind a tank. */
static bool Tanks_DropMine(const Tanks_TankTypeDef *Tank, uint8_t Owner)
{
    Tanks_VectorTypeDef Position;
    Position.X = Tank->Position.X - ((Tanks_Sine(Tank->Heading) * 24 * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    Position.Y = Tank->Position.Y + ((Tanks_Cosine(Tank->Heading) * 24 * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    return Tanks_PlaceMine(Position, Owner, 9000U);
}

/* Set a tank's tracks to turn towards DesiredHeading and drive at Speed. */
static void Tanks_DriveTank(Tanks_TankTypeDef *Tank, int16_t DesiredHeading, int16_t Speed)
{
    const int16_t Error = Tanks_NormalizeAngle((int32_t)DesiredHeading - Tank->Heading);
    if(Error > 620)
    {
        Tank->LeftTrack = 690;
        Tank->RightTrack = -690;
    }
    else if(Error < -620)
    {
        Tank->LeftTrack = -690;
        Tank->RightTrack = 690;
    }
    else
    {
        Tank->LeftTrack = (int16_t)Tanks_Clamp32(Speed + (Error * 2), -900, 900);
        Tank->RightTrack = (int16_t)Tanks_Clamp32(Speed - (Error * 2), -900, 900);
    }
}

/*
 * Set a tank's tracks to head for DesiredHeading at Speed, turning aside from
 * walls and pits ahead and backing out when stuck. Returns whether it may
 * still fire: a tank that has to steer around something stops shooting.
 */
static bool Tanks_SteerTank(Tanks_TankTypeDef *Tank, int16_t DesiredHeading, int16_t Speed, bool MayFire)
{
    if((Speed > 0) && Tanks_EnemyProbeBlocked(Tank, DesiredHeading, 55))
    {
        const int16_t LeftHeading = Tanks_NormalizeAngle((int32_t)DesiredHeading - 560);
        const int16_t RightHeading = Tanks_NormalizeAngle((int32_t)DesiredHeading + 560);
        const bool LeftBlocked = Tanks_EnemyProbeBlocked(Tank, LeftHeading, 66);
        const bool RightBlocked = Tanks_EnemyProbeBlocked(Tank, RightHeading, 66);
        if(LeftBlocked && !RightBlocked)
        {
            DesiredHeading = RightHeading;
        }
        else if(RightBlocked && !LeftBlocked)
        {
            DesiredHeading = LeftHeading;
        }
        else
        {
            DesiredHeading = Tanks_NormalizeAngle((int32_t)Tank->Heading + Tank->AiTurnBias);
        }
        if(MayFire)
        {
            MayFire = false;
            Speed = 0;
        }
    }
    if(Tank->StuckMilliseconds > 300U)
    {
        DesiredHeading = Tanks_NormalizeAngle((int32_t)Tank->Heading + Tank->AiTurnBias);
        Speed = -420;
        Tank->AiThinkMilliseconds = 0U;
        if(Tank->StuckMilliseconds > 900U)
        {
            Tank->AiTurnBias = (int16_t)-Tank->AiTurnBias;
        }
    }

    Tanks_DriveTank(Tank, DesiredHeading, Speed);
    return MayFire;
}

static Tanks_VectorTypeDef Tanks_MineSpotPosition(uint8_t Spot)
{
    Tanks_VectorTypeDef Position;
    Position.X = TANKS_FP((int32_t)Tanks_Game.MineSpots[Spot][0] * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    Position.Y = TANKS_FP((int32_t)Tanks_Game.MineSpots[Spot][1] * TANKS_TILE_SIZE + (TANKS_TILE_SIZE / 2));
    return Position;
}

static bool Tanks_MineSpotMined(uint8_t Spot)
{
    const Tanks_VectorTypeDef Position = Tanks_MineSpotPosition(Spot);
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        if(Tanks_Game.Mines[Index].Active && (Tanks_DistancePixels(Position, Tanks_Game.Mines[Index].Position) <= MINE_SPOT_PIXELS))
        {
            return true;
        }
    }
    return false;
}

/*
 * The next spot to mine: of the spots with no mine yet that no other
 * mine-layer is heading for, the one furthest from the player.
 */
static uint8_t Tanks_NextMineSpot(const Tanks_TankTypeDef *Enemy)
{
    uint8_t Best = NO_MINE_SPOT;
    uint32_t BestDistance = 0U;
    for(uint8_t Spot = 0U; Spot < Tanks_Game.MineSpotCount; Spot++)
    {
        bool Claimed = false;
        uint32_t Distance;
        if(Tanks_MineSpotMined(Spot))
        {
            continue;
        }
        for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
        {
            const Tanks_TankTypeDef *Other = &Tanks_Game.Enemies[Index];
            if((Other != Enemy) && Other->Active && (Other->Type == TANKS_ENEMY_MINELAYER) && (Other->MineSpot == Spot))
            {
                Claimed = true;
            }
        }
        if(Claimed)
        {
            continue;
        }
        Distance = Tanks_DistancePixels(Tanks_Game.Player.Position, Tanks_MineSpotPosition(Spot));
        if((Best == NO_MINE_SPOT) || (Distance > BestDistance))
        {
            BestDistance = Distance;
            Best = Spot;
        }
    }
    return Best;
}

/*
 * The mine-layer never fights or flees the player. It races to the spot on
 * the level's mine map furthest from the player that has no mine, plants
 * one mine there, and moves on to the next, until every spot is mined. If a mine on
 * the map goes off, it comes back to replace it. With nothing left to mine it
 * patrols. Returns the speed and sets the heading, and whether it is making
 * a straight run at a spot in plain sight (and needn't look out for walls).
 */
static int16_t Tanks_PlanMinelayer(Tanks_TankTypeDef *Enemy, uint8_t EnemyIndex, int16_t *DesiredHeading, bool *Direct)
{
    Tanks_VectorTypeDef Spot;
    uint32_t Distance;
    int16_t TurnNeeded;

    if((Enemy->MineSpot >=Tanks_Game.MineSpotCount) || Tanks_MineSpotMined(Enemy->MineSpot))
    {
        Enemy->MineSpot = Tanks_NextMineSpot(Enemy);
    }

    if(Enemy->MineSpot == NO_MINE_SPOT)
    {
        if((Enemy->AiThinkMilliseconds == 0U) || (Tanks_DistancePixels(Enemy->Position, Enemy->MoveTarget) < 42U))
        {
            Enemy->MoveTarget = Tanks_EnemyRandomOpenPoint(Enemy, 260);
            Enemy->AiThinkMilliseconds = 4000U;
        }
        *DesiredHeading = Tanks_AngleTo(Enemy->Position, Enemy->MoveTarget);
        return 430;
    }

    Spot = Tanks_MineSpotPosition(Enemy->MineSpot);
    Distance = Tanks_DistancePixels(Enemy->Position, Spot);
    if(Distance <= MINELAYER_PLANT_PIXELS)
    {
        if(Tanks_PlaceMine(Spot, (uint8_t)(EnemyIndex + 1U), MINE_LIFE_FOREVER))
        {
            Enemy->Mines++;
        }
        Enemy->MineSpot = Tanks_NextMineSpot(Enemy);
        *DesiredHeading = Enemy->Heading;
        return MINELAYER_SPEED;
    }

    /* Far off, follow the shortest path; with the spot in plain sight, drive straight onto it. */
    if((Distance < 90U) && Tanks_LineOfSight(Enemy->Position, Spot))
    {
        Enemy->MoveTarget = Spot;
        *Direct = true;
    }
    else
    {
        Enemy->MoveTarget = Tanks_PathWaypointTo(Enemy, Spot, true);
    }
    *DesiredHeading = Tanks_AngleTo(Enemy->Position, Enemy->MoveTarget);

    TurnNeeded = Tanks_NormalizeAngle((int32_t)*DesiredHeading - Enemy->Heading);
    if((TurnNeeded > MINELAYER_PIVOT_ANGLE) || (TurnNeeded < -MINELAYER_PIVOT_ANGLE))
    {
        return 0;
    }

    /* Ease off on the last stretch so it stops on the spot rather than overshooting. */
    return (int16_t)Tanks_Clamp32((int32_t)Distance * 12, 300, MINELAYER_SPEED);
}

static void Tanks_UpdateEnemy(Tanks_TankTypeDef *Enemy, uint8_t EnemyIndex, uint32_t DeltaMilliseconds)
{
    const Tanks_EnemyTypeDef Type = (Tanks_EnemyTypeDef)Enemy->Type;
    const bool Hunter = Tanks_EnemyTargetsPlayer(Type);
    const bool Ricochet = Tanks_EnemyUsesRicochet(Type);
    const bool Rocket = Tanks_EnemyUsesRocket(Type);
    const bool PlayerVisible = Tanks_Game.Player.Active && Tanks_LineOfSight(Enemy->Position, Tanks_Game.Player.Position);
    const uint32_t PlayerDistance = Tanks_Game.Player.Active ? Tanks_DistancePixels(Enemy->Position, Tanks_Game.Player.Position) : UINT32_MAX;
    int16_t DesiredHeading = Enemy->Heading;
    int16_t AimHeading = Tanks_AngleTo(Enemy->Position, Tanks_Game.Player.Position);
    int16_t AimError;
    int16_t Speed = 450;
    bool MayFire = false;
    bool Direct = false;

    if(!Enemy->Awake || !Tanks_Game.Player.Active)
    {
        Enemy->LeftTrack = 0;
        Enemy->RightTrack = 0;
        Enemy->LinearVelocity = 0;
        Enemy->AngularVelocity = 0;
        return;
    }

    if(Enemy->AiThinkMilliseconds > DeltaMilliseconds)
    {
        Enemy->AiThinkMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else if(Type != TANKS_ENEMY_MINELAYER)
    {
        Tanks_SelectEnemyMode(Enemy, EnemyIndex, PlayerDistance);
    }

    if(Hunter)
    {
        /*
         * Hunters chase. In sight, they drive at the player and fire on the
         * move, aiming where the player will be, easing off only at close
         * range. Out of sight, they follow the shortest path to the player.
         */
        if(PlayerVisible)
        {
            const int32_t BulletSpeed = Rocket ? 225 : (Ricochet ? (RICOCHET_ENEMY_SPEED + 20) : RICOCHET_ENEMY_SPEED);
            AimHeading = Tanks_LeadHeading(Enemy, BulletSpeed);
            DesiredHeading = AimHeading;
            Speed = (int16_t)Tanks_Clamp32(((int32_t)PlayerDistance - (Rocket ? HUNTER_ROCKET_RANGE : HUNTER_CLOSE_RANGE)) * 4, 0, 650);
            MayFire = true;
        }
        else
        {
            Enemy->MoveTarget = Tanks_PathWaypointTo(Enemy, Tanks_Game.Player.Position, false);
            DesiredHeading = Tanks_AngleTo(Enemy->Position, Enemy->MoveTarget);
            Speed = 650;

            /* A ricochet hunter banks a shot off the walls when it has one. */
            if(Ricochet && (Enemy->ReloadMilliseconds == 0U))
            {
                if(Enemy->AimRefreshMilliseconds == 0U)
                {
                    Enemy->AimValid = Tanks_FindRicochetHeading(Enemy, &Enemy->TurretHeading);
                    Enemy->AimRefreshMilliseconds = 350U;
                }
                if(Enemy->AimValid)
                {
                    AimHeading = Enemy->TurretHeading;
                    DesiredHeading = AimHeading;
                    Speed = 0;
                    MayFire = true;
                }
            }
        }
    }
    else if(Type == TANKS_ENEMY_MINELAYER)
    {
        Speed = Tanks_PlanMinelayer(Enemy, EnemyIndex, &DesiredHeading, &Direct);
        AimHeading = Enemy->Heading;
    }
    else
    {
        /*
         * Basic tanks never track the player: they patrol, and fire straight
         * ahead only when the player crosses their line of fire (or, for the
         * ricochet tank, a bounce along its heading would hit the player).
         */
        DesiredHeading = Tanks_AngleTo(Enemy->Position, Enemy->MoveTarget);
        Speed = 430;
        if(Tanks_DistancePixels(Enemy->Position, Enemy->MoveTarget) < 42U)
        {
            Enemy->AiThinkMilliseconds = 0U;
        }
        AimHeading = Enemy->Heading;
        if(Enemy->ReloadMilliseconds == 0U)
        {
            if(Ricochet)
            {
                if(Enemy->AimRefreshMilliseconds == 0U)
                {
                    Enemy->AimValid = Tanks_RicochetRayHitsPlayer(Enemy->Position, Enemy->Heading);
                    Enemy->AimRefreshMilliseconds = 200U;
                }
                MayFire = Enemy->AimValid;
            }
            else
            {
                int16_t Offset = Tanks_NormalizeAngle((int32_t)Tanks_AngleTo(Enemy->Position, Tanks_Game.Player.Position) - Enemy->Heading);
                if(Offset < 0)
                {
                    Offset = (int16_t)-Offset;
                }
                MayFire = PlayerVisible && (Offset < BASIC_FIRE_CONE);
            }
        }
    }

    if(Tanks_Game.EnemyRound == 1U && Speed > 300)
    {
        Speed = 300;
    }

    if(Direct)
    {
        Tanks_DriveTank(Enemy, DesiredHeading, Speed);
    }
    else
    {
        MayFire = Tanks_SteerTank(Enemy, DesiredHeading, Speed, MayFire);
    }
    (void)Tanks_MoveTank(Enemy, ENEMY_RADIUS, DeltaMilliseconds);

    AimError = Tanks_NormalizeAngle((int32_t)Enemy->Heading - AimHeading);
    if(AimError < 0)
    {
        AimError = (int16_t)-AimError;
    }
    if((Enemy->ReloadMilliseconds == 0U) && (AimError < 28) && MayFire)
    {
        (void)Tanks_Fire(Enemy, (uint8_t)(EnemyIndex + 1U));
        Enemy->AimValid = false;
        Enemy->AimRefreshMilliseconds = 0U;
    }
}

static bool Tanks_BulletHitsTank(Tanks_BulletTypeDef *Bullet)
{
    if(Bullet->Owner == PLAYER_OWNER)
    {
        for(uint8_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
        {
            Tanks_BulletTypeDef *Other = &Tanks_Game.Bullets[Index];
            if((Other == Bullet) || !Other->Active || (Other->Type != TANKS_PROJECTILE_ROCKET) || (Other->Owner == PLAYER_OWNER))
            {
                continue;
            }
            if(Tanks_DistancePixels(Bullet->Position, Other->Position) > 11U)
            {
                continue;
            }
            Other->Active = false;
            Tanks_SpawnExplosion(Other->Position, 7U);
            return true;
        }
        for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
        {
            Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[Index];
            if(!Mine->Active || (Tanks_DistancePixels(Bullet->Position, Mine->Position) > 11U))
            {
                continue;
            }
            Mine->Active = false;
            Tanks_Detonate(Mine->Position, MINE_BLAST_RADIUS, 14U);
            return true;
        }
        for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
        {
            Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
            if(!Enemy->Active || !Enemy->Awake || (Enemy->InvulnerableMilliseconds > 0U))
            {
                continue;
            }
            if(Tanks_DistancePixels(Bullet->Position, Enemy->Position) > ENEMY_RADIUS + BULLET_RADIUS)
            {
                continue;
            }
            Enemy->Hull = (int16_t)(Enemy->Hull - Bullet->Damage);
            Enemy->FlashMilliseconds = 120U;
            Enemy->InvulnerableMilliseconds = 85U;
            if(Enemy->Hull <= 0)
            {
                Tanks_DestroyEnemy(Enemy, true);
            }
            return true;
        }
        if((Bullet->Bounces > 0U) && Tanks_Game.Player.Active && (Tanks_Game.Player.InvulnerableMilliseconds == 0U) &&
           (Tanks_DistancePixels(Bullet->Position, Tanks_Game.Player.Position) <= PLAYER_RADIUS + BULLET_RADIUS))
        {
            Tanks_DamagePlayer(1U);
            return true;
        }
        return false;
    }

    if(Tanks_Game.Player.Active && (Tanks_Game.Player.InvulnerableMilliseconds == 0U) &&
       (Tanks_DistancePixels(Bullet->Position, Tanks_Game.Player.Position) <= PLAYER_RADIUS + BULLET_RADIUS))
    {
        Tanks_DamagePlayer(Bullet->Damage);
        return true;
    }

    if(Bullet->Bounces > 0U)
    {
        for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
        {
            Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
            if(!Enemy->Active || !Enemy->Awake || (Bullet->Owner == (uint8_t)(Index + 1U)) || (Enemy->InvulnerableMilliseconds > 0U))
            {
                continue;
            }
            if(Tanks_DistancePixels(Bullet->Position, Enemy->Position) > ENEMY_RADIUS + BULLET_RADIUS)
            {
                continue;
            }
            Tanks_DestroyEnemy(Enemy, true);
            return true;
        }
    }
    return false;
}

static void Tanks_SpawnRicochetSpark(Tanks_VectorTypeDef Position)
{
    for(uint8_t Index = 0U; Index < 5U; Index++)
    {
        Tanks_ParticleTypeDef *Particle = Tanks_AllocateParticle();
        const int16_t Angle = (int16_t)(Tanks_Random() % TANKS_ANGLE_FULL);
        const int32_t Speed = 30 + (int32_t)(Tanks_Random() % 55U);
        Particle->Position = Position;
        Particle->Velocity.X = (Tanks_Sine(Angle) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Particle->Velocity.Y = (-Tanks_Cosine(Angle) * Speed * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Particle->LifeMilliseconds = 160U;
        Particle->MaximumLifeMilliseconds = 160U;
        Particle->Size = (uint8_t)(2U + (Index & 1U));
        Particle->Colour = Index < 3U ? TANKS_COLOUR_FIRE_LIGHT : TANKS_COLOUR_WHITE;
        Particle->Active = true;
    }
}

static bool Tanks_BulletAxisBlocked(Tanks_BulletTypeDef *Bullet, Tanks_VectorTypeDef Candidate, int32_t *TileX, int32_t *TileY)
{
    const uint8_t Tile = Tanks_TileAtPosition(Candidate);
    *TileX = (Candidate.X >> TANKS_FP_SHIFT) / TANKS_TILE_SIZE;
    *TileY = (Candidate.Y >> TANKS_FP_SHIFT) / TANKS_TILE_SIZE;
    if(Tile == TANKS_TILE_HQ)
    {
        if(Bullet->Owner != PLAYER_OWNER)
        {
            Tanks_DamageHq(Bullet->Damage);
            Bullet->Active = false;
        }
        return true;
    }
    return Tanks_TileBlocksBullet(Tile);
}

static void Tanks_ExplodeRocket(Tanks_BulletTypeDef *Bullet)
{
    const Tanks_VectorTypeDef Position = Bullet->Position;
    if(!Bullet->Active)
    {
        return;
    }
    Bullet->Active = false;
    Tanks_Detonate(Position, ROCKET_BLAST_RADIUS, 16U);
}

static void Tanks_UpdateBullet(Tanks_BulletTypeDef *Bullet, uint32_t DeltaMilliseconds)
{
    Tanks_VectorTypeDef Candidate;
    int32_t TileX = 0;
    int32_t TileY = 0;
    bool Bounced = false;

    if(Bullet->Type == TANKS_PROJECTILE_ROCKET)
    {
        /* Rockets fly straight, never ricochet, and explode on impact, destroying every tank in the blast. */
        Bullet->Velocity.X = (Tanks_Sine(Bullet->Heading) * 225 * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Bullet->Velocity.Y = (-Tanks_Cosine(Bullet->Heading) * 225 * TANKS_FP_ONE) / TANKS_TRIG_ONE;
        Candidate = Bullet->Position;
        Candidate.X += (Bullet->Velocity.X * (int32_t)DeltaMilliseconds) / 1000;
        Candidate.Y += (Bullet->Velocity.Y * (int32_t)DeltaMilliseconds) / 1000;
        if(Tanks_TileBlocksBullet(Tanks_TileAtPosition(Candidate)))
        {
            Tanks_ExplodeRocket(Bullet);
            return;
        }
        Bullet->Position = Candidate;
        if((Tanks_Random() & 3U) == 0U)
        {
            Tanks_ParticleTypeDef *Particle = Tanks_AllocateParticle();
            Particle->Position = Bullet->Position;
            Particle->Velocity.X = -Bullet->Velocity.X / 12;
            Particle->Velocity.Y = -Bullet->Velocity.Y / 12;
            Particle->LifeMilliseconds = 260U;
            Particle->MaximumLifeMilliseconds = 260U;
            Particle->Size = 3U;
            Particle->Colour = TANKS_COLOUR_SMOKE;
            Particle->Active = true;
        }
        /* A rocket goes off on touching any tank but the one that fired it. */
        if(Tanks_Game.Player.Active &&
           (Tanks_DistancePixels(Bullet->Position, Tanks_Game.Player.Position) <= PLAYER_RADIUS + BULLET_RADIUS))
        {
            Tanks_ExplodeRocket(Bullet);
            return;
        }
        for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
        {
            const Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];

            if(Enemy->Active && (Bullet->Owner != (uint8_t)(Index + 1U)) &&
               (Tanks_DistancePixels(Bullet->Position, Enemy->Position) <= ENEMY_RADIUS + BULLET_RADIUS))
            {
                Tanks_ExplodeRocket(Bullet);
                return;
            }
        }
        if(Bullet->LifeMilliseconds > DeltaMilliseconds)
        {
            Bullet->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Tanks_ExplodeRocket(Bullet);
        }
        return;
    }

    Candidate = Bullet->Position;
    Candidate.X += (Bullet->Velocity.X * (int32_t)DeltaMilliseconds) / 1000;
    if(Tanks_BulletAxisBlocked(Bullet, Candidate, &TileX, &TileY))
    {
        if(!Bullet->Active)
        {
            return;
        }
        Bullet->Velocity.X = -Bullet->Velocity.X;
        Bounced = true;
    }
    else
    {
        Bullet->Position.X = Candidate.X;
    }

    Candidate = Bullet->Position;
    Candidate.Y += (Bullet->Velocity.Y * (int32_t)DeltaMilliseconds) / 1000;
    if(Tanks_BulletAxisBlocked(Bullet, Candidate, &TileX, &TileY))
    {
        if(!Bullet->Active)
        {
            return;
        }
        Bullet->Velocity.Y = -Bullet->Velocity.Y;
        Bounced = true;
    }
    else
    {
        Bullet->Position.Y = Candidate.Y;
    }

    if(Bounced)
    {
        Bullet->Bounces++;
        Tanks_SpawnRicochetSpark(Bullet->Position);
        if(Bullet->Bounces > RICOCHET_MAX_BOUNCES)
        {
            Bullet->Active = false;
            TanksAudio_PlayBulletSpent(Bullet->Position);
        }
        else
        {
            TanksAudio_PlayRicochet(Bullet->Position);
        }
    }
    if(Bullet->Active && Tanks_BulletHitsTank(Bullet))
    {
        Bullet->Active = false;
    }
    if(Bullet->LifeMilliseconds > DeltaMilliseconds)
    {
        Bullet->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Bullet->Active = false;
    }
}

/* An armed mine goes off when any tank drives onto it. */
/*
 * Whether a tank, numbered Owner, is on the mine. Enemies know where their
 * side's mines are and never set them off. The player can drive away from a
 * mine just laid: it only counts once the player has been clear of it.
 */
static bool Tanks_MineTouchedBy(Tanks_MineTypeDef *Mine, const Tanks_TankTypeDef *Tank, uint8_t Owner, uint32_t Reach)
{
    const bool Touching = Tank->Active && (Tanks_DistancePixels(Mine->Position, Tank->Position) <= Reach);

    if((Owner != PLAYER_OWNER) && (Mine->Owner != PLAYER_OWNER))
    {
        return false;
    }

    if((Owner == Mine->Owner) && !Mine->OwnerClear)
    {
        Mine->OwnerClear = !Touching;
        return false;
    }

    return Touching;
}

static bool Tanks_MineTriggered(Tanks_MineTypeDef *Mine)
{
    if((Tanks_Game.Player.InvulnerableMilliseconds == 0U) && Tanks_MineTouchedBy(Mine, &Tanks_Game.Player, PLAYER_OWNER, 31U))
    {
        return true;
    }

    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        if(Tanks_MineTouchedBy(Mine, &Tanks_Game.Enemies[Index], (uint8_t)(Index + 1U), 29U))
        {
            return true;
        }
    }

    return false;
}

static void Tanks_UpdateMines(uint32_t DeltaMilliseconds)
{
    for(uint8_t MineIndex = 0U; MineIndex < TANKS_MAX_MINES; MineIndex++)
    {
        Tanks_MineTypeDef *Mine = &Tanks_Game.Mines[MineIndex];
        if(!Mine->Active)
        {
            continue;
        }
        if(Mine->LifeMilliseconds == MINE_LIFE_FOREVER)
        {
        }
        else if(Mine->LifeMilliseconds > DeltaMilliseconds)
        {
            Mine->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Mine->Active = false;
            TanksAudio_PlayMineFizzle(Mine->Position);
            continue;
        }
        if(Mine->ArmMilliseconds > DeltaMilliseconds)
        {
            Mine->ArmMilliseconds -= (uint16_t)DeltaMilliseconds;
            continue;
        }
        if(Mine->ArmMilliseconds != 0U)
        {
            TanksAudio_PlayMineArmed(Mine->Position);
        }
        Mine->ArmMilliseconds = 0U;
        if(Tanks_MineTriggered(Mine))
        {
            Mine->Active = false;
            Tanks_Detonate(Mine->Position, MINE_BLAST_RADIUS, 14U);
        }
    }
}

static void Tanks_UpdateEffects(uint32_t DeltaMilliseconds)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_BLASTS; Index++)
    {
        Tanks_BlastTypeDef *Blast = &Tanks_Game.Blasts[Index];

        if(Blast->Active)
        {
            Blast->LifeMilliseconds = (Blast->LifeMilliseconds > DeltaMilliseconds) ? (uint16_t)(Blast->LifeMilliseconds - DeltaMilliseconds) : 0U;
            Blast->Active = Blast->LifeMilliseconds > 0U;
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_PARTICLES; Index++)
    {
        Tanks_ParticleTypeDef *Particle = &Tanks_Game.Particles[Index];
        if(!Particle->Active)
        {
            continue;
        }
        Particle->Position.X += (Particle->Velocity.X * (int32_t)DeltaMilliseconds) / 1000;
        Particle->Position.Y += (Particle->Velocity.Y * (int32_t)DeltaMilliseconds) / 1000;
        Particle->Velocity.X = (Particle->Velocity.X * 94) / 100;
        Particle->Velocity.Y = (Particle->Velocity.Y * 94) / 100;
        if(Particle->LifeMilliseconds > DeltaMilliseconds)
        {
            Particle->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Particle->Active = false;
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_TRACK_MARKS; Index++)
    {
        Tanks_TrackMarkTypeDef *Mark = &Tanks_Game.TrackMarks[Index];
        if(!Mark->Active)
        {
            continue;
        }
        if(Mark->LifeMilliseconds > DeltaMilliseconds)
        {
            Mark->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Mark->Active = false;
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_FLOATING_TEXT; Index++)
    {
        Tanks_FloatingTextTypeDef *Floating = &Tanks_Game.FloatingText[Index];
        if(!Floating->Active)
        {
            continue;
        }
        Floating->Position.Y -= TANKS_FP((int32_t)DeltaMilliseconds) / 35;
        if(Floating->LifeMilliseconds > DeltaMilliseconds)
        {
            Floating->LifeMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Floating->Active = false;
        }
    }
}

static void Tanks_UpdateTankTimers(Tanks_TankTypeDef *Tank, uint32_t DeltaMilliseconds)
{
    if(Tank->ReloadMilliseconds > DeltaMilliseconds)
    {
        Tank->ReloadMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Tank->ReloadMilliseconds = 0U;
    }
    if(Tank->AimRefreshMilliseconds > DeltaMilliseconds)
    {
        Tank->AimRefreshMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Tank->AimRefreshMilliseconds = 0U;
    }
    if(Tank->InvulnerableMilliseconds > DeltaMilliseconds)
    {
        Tank->InvulnerableMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Tank->InvulnerableMilliseconds = 0U;
    }
    if(Tank->FlashMilliseconds > DeltaMilliseconds)
    {
        Tank->FlashMilliseconds -= (uint8_t)DeltaMilliseconds;
    }
    else
    {
        Tank->FlashMilliseconds = 0U;
    }
}

void Tanks_Simulate(uint32_t DeltaMilliseconds)
{
    Tanks_Game.RunMilliseconds += DeltaMilliseconds;
    Tanks_UpdateTankTimers(&Tanks_Game.Player, DeltaMilliseconds);
    Tanks_UpdatePlayer(DeltaMilliseconds);
    if(Tanks_Game.Screen != TANKS_SCREEN_PLAYING)
    {
        return;
    }

    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
        if(!Enemy->Active)
        {
            continue;
        }
        Tanks_UpdateTankTimers(Enemy, DeltaMilliseconds);
        Tanks_UpdateEnemy(Enemy, Index, DeltaMilliseconds);
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
    {
        if(Tanks_Game.Bullets[Index].Active)
        {
            Tanks_UpdateBullet(&Tanks_Game.Bullets[Index], DeltaMilliseconds);
        }
    }
    Tanks_UpdateMines(DeltaMilliseconds);
    Tanks_UpdateEffects(DeltaMilliseconds);
    if(Tanks_Game.MessageMilliseconds > DeltaMilliseconds)
    {
        Tanks_Game.MessageMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Tanks_Game.MessageMilliseconds = 0U;
    }
    if(Tanks_Game.CameraKickMilliseconds > DeltaMilliseconds)
    {
        Tanks_Game.CameraKickMilliseconds -= (uint16_t)DeltaMilliseconds;
    }
    else
    {
        Tanks_Game.CameraKickMilliseconds = 0U;
    }
    if((Tanks_Game.Screen != TANKS_SCREEN_PLAYING) || Tanks_Game.Demo)
    {
        return;
    }

    if(!Tanks_Game.RoundComplete && (Tanks_CountActiveEnemies() == 0U))
    {
        Tanks_Game.RoundComplete = true;
        Tanks_Game.RoundClearMilliseconds = ROUND_CLEAR_MS;
        TanksAudio_PlayJingle(Tanks_Game.Wave == CAMPAIGN_WAVES ? TANKS_AUDIO_JINGLE_VICTORY : TANKS_AUDIO_JINGLE_ARENA_CLEAR);
    }

    if(Tanks_Game.RoundComplete)
    {
        if(Tanks_Game.RoundClearMilliseconds > DeltaMilliseconds)
        {
            Tanks_Game.RoundClearMilliseconds -= (uint16_t)DeltaMilliseconds;
        }
        else
        {
            Tanks_StartWave((uint16_t)(Tanks_Game.Wave + 1U));
        }
    }
}

/*
 * The launcher preview's demo battle: one arena after another, the player's
 * tank on autopilot against a single hunter on the enemy AI.
 */
void Tanks_StartDemo(void)
{
    static const Tanks_EnemyTypeDef Hunters[3] = { TANKS_ENEMY_HUNTER_DIRECT, TANKS_ENEMY_HUNTER_RICOCHET, TANKS_ENEMY_HUNTER_ROCKET };
    /* Cover in the open field around the fort: X, Y, length (horizontal). */
    static const uint8_t Cover[4][3] = { { 7U, 5U, 3U }, { 22U, 5U, 3U }, { 7U, 13U, 3U }, { 22U, 13U, 3U } };
    const bool Swap = (Tanks_Game.DemoRound & 1U) != 0U;
    Tanks_TankTypeDef *Enemy = NULL;

    Tanks_Game.Demo = true;
    Tanks_Game.DemoRound = (uint8_t)((Tanks_Game.DemoRound % CAMPAIGN_WAVES) + 1U);
    Tanks_Game.DemoRestartMilliseconds = 0U;
    Tanks_Game.Lives = 1U;
    Tanks_StartWave(1U);
    Tanks_Game.EnemyRound = CAMPAIGN_WAVES;
    Tanks_Game.Screen = TANKS_SCREEN_PLAYING;
    Tanks_Game.MineSpotCount = 0U;

    /* An open field walled at its edges, with the fort in the middle and a little cover. */
    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        for(uint8_t X = 0U; X < TANKS_MAP_WIDTH; X++)
        {
            const bool Border = (X == 0U) || (Y == 0U) || (X == TANKS_MAP_WIDTH - 1U) || (Y == TANKS_MAP_HEIGHT - 1U);
            const bool Fort = (X >= TANKS_DEMO_FORT_X) && (X < TANKS_DEMO_FORT_X + TANKS_DEMO_FORT_WIDTH) &&
                              (Y >= TANKS_DEMO_FORT_Y) && (Y < TANKS_DEMO_FORT_Y + TANKS_DEMO_FORT_HEIGHT);
            Tanks_Game.Tiles[Y][X] = (Border || Fort) ? TANKS_TILE_WALL : TANKS_TILE_FLOOR;
            Tanks_Game.TileDamage[Y][X] = 0U;
        }
    }
    for(uint8_t Block = 0U; Block < 4U; Block++)
    {
        for(uint8_t Index = 0U; Index < Cover[Block][2]; Index++)
        {
            Tanks_Game.Tiles[Cover[Block][1]][Cover[Block][0] + Index] = TANKS_TILE_WALL;
        }
    }

    /* The two tanks start in opposite corners, swapping sides each battle. */
    Tanks_Game.Player.Position.X = TANKS_FP(((Swap ? 27 : 4) * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2));
    Tanks_Game.Player.Position.Y = TANKS_FP(((Swap ? 3 : 15) * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2));
    Tanks_Game.Player.Heading = Swap ? 1800 : 0;
    Tanks_Game.Player.HomePosition = Tanks_Game.Player.Position;
    Tanks_Game.Player.MoveTarget = Tanks_Game.Player.Position;

    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        if(Tanks_Game.Enemies[Index].Active && (Enemy == NULL))
        {
            Enemy = &Tanks_Game.Enemies[Index];
        }
        else
        {
            Tanks_Game.Enemies[Index].Active = false;
        }
    }
    if(Enemy != NULL)
    {
        Enemy->Type = (uint8_t)Hunters[Tanks_Game.DemoRound % 3U];
        Enemy->Position.X = TANKS_FP(((Swap ? 4 : 27) * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2));
        Enemy->Position.Y = TANKS_FP(((Swap ? 15 : 3) * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2));
        Enemy->Heading = Swap ? 0 : 1800;
        Enemy->HomePosition = Enemy->Position;
        Enemy->MoveTarget = Enemy->Position;
    }
}

void Tanks_UpdateDemo(uint32_t DeltaMilliseconds)
{
    Tanks_Simulate(DeltaMilliseconds);

    if(!Tanks_Game.Player.Active || (Tanks_CountActiveEnemies() == 0U))
    {
        Tanks_Game.DemoRestartMilliseconds = (uint16_t)(Tanks_Game.DemoRestartMilliseconds + DeltaMilliseconds);

        if(Tanks_Game.DemoRestartMilliseconds >= DEMO_RESTART_MS)
        {
            Tanks_StartDemo();
        }
    }
}
