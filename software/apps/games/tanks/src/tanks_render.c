/**
 * @file tanks_render.c
 * @brief Fixed full-arena camera and low-cost vector artwork for DualTrack.
 *
 * The look is a proving ground at night: dark concrete under a faint
 * tactical grid, steel barriers with amber hazard dashes, and the player's
 * two treads leaving trails that fade behind it.
 */

#include "tanks_internal.h"

#include "open_sans.h"

#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Spacing of the tactical grid on the arena floor, and of the barriers' hazard dashes. */
#define TANKS_GRID_SIZE       (40)
#define TANKS_HAZARD_SPACING  (16)
#define TANKS_HAZARD_LENGTH   (8U)

/* A tread mark is fresh for this long, then fades. */
#define TANKS_TRAIL_FRESH_MS  (4500U)

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* Joins two strings into Buffer, truncating to fit. */
static void Tanks_JoinText(char *Buffer, uint8_t Size, const char *First, const char *Second)
{
    uint8_t Length = 0U;
    while((First != NULL) && (*First != '\0') && (Length + 1U < Size))
    {
        Buffer[Length++] = *First++;
    }
    while((Second != NULL) && (*Second != '\0') && (Length + 1U < Size))
    {
        Buffer[Length++] = *Second++;
    }
    Buffer[Length] = '\0';
}

static int16_t Tanks_ScaleVisual(int16_t Value)
{
    return Value;
}

static void Tanks_RotateLocalPoint(int16_t CentreX, int16_t CentreY, int16_t LocalX, int16_t LocalY, int16_t Angle, Render_PointTypeDef *Point)
{
    const int32_t Sine = Tanks_Sine(Angle);
    const int32_t Cosine = Tanks_Cosine(Angle);
    Point->X = (int16_t)(CentreX + (((Cosine * LocalX) - (Sine * LocalY)) / TANKS_TRIG_ONE));
    Point->Y = (int16_t)(CentreY + (((Sine * LocalX) + (Cosine * LocalY)) / TANKS_TRIG_ONE));
}

static void Tanks_DrawRotatedRect(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, int16_t HalfWidth, int16_t HalfHeight, int16_t Angle, uint8_t Colour)
{
    Render_PointTypeDef Points[4];
    Tanks_RotateLocalPoint(CentreX, CentreY, -HalfWidth, -HalfHeight, Angle, &Points[0]);
    Tanks_RotateLocalPoint(CentreX, CentreY, HalfWidth, -HalfHeight, Angle, &Points[1]);
    Tanks_RotateLocalPoint(CentreX, CentreY, HalfWidth, HalfHeight, Angle, &Points[2]);
    Tanks_RotateLocalPoint(CentreX, CentreY, -HalfWidth, HalfHeight, Angle, &Points[3]);
    (void)Render_DrawPolygon(Target, Points, 4U, Colour);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Tanks_WorldToScreen(Tanks_VectorTypeDef World, int16_t *ScreenX, int16_t *ScreenY)
{
    *ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + (World.X >> TANKS_FP_SHIFT));
    *ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + (World.Y >> TANKS_FP_SHIFT));
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Tanks_DrawOutsidePattern(Render_TargetTypeDef *Target)
{
    Render_Clear(Target, TANKS_COLOUR_OUTSIDE);
}

static uint8_t Tanks_RenderTileAt(int16_t X, int16_t Y)
{
    if((X < 0) || (Y < 0) || (X >= TANKS_MAP_WIDTH) || (Y >= TANKS_MAP_HEIGHT))
    {
        return TANKS_TILE_WALL;
    }
    return Tanks_Game.Tiles[Y][X];
}

/* Dark concrete under a faint tactical grid, with brighter marks where the lines cross. */
static void Tanks_DrawArenaFloor(Render_TargetTypeDef *Target)
{
    Render_Box(Target, TANKS_ARENA_SCREEN_X, TANKS_ARENA_SCREEN_Y, TANKS_WORLD_WIDTH, TANKS_WORLD_HEIGHT, TANKS_COLOUR_FLOOR);
    for(int16_t X = TANKS_GRID_SIZE; X < (int16_t)TANKS_WORLD_WIDTH; X += TANKS_GRID_SIZE)
    {
        Render_Box(Target, (int16_t)(TANKS_ARENA_SCREEN_X + X), TANKS_ARENA_SCREEN_Y, 1U, TANKS_WORLD_HEIGHT, TANKS_COLOUR_GROUT);
    }
    for(int16_t Y = TANKS_GRID_SIZE; Y < (int16_t)TANKS_WORLD_HEIGHT; Y += TANKS_GRID_SIZE)
    {
        Render_Box(Target, TANKS_ARENA_SCREEN_X, (int16_t)(TANKS_ARENA_SCREEN_Y + Y), TANKS_WORLD_WIDTH, 1U, TANKS_COLOUR_GROUT);
        for(int16_t X = TANKS_GRID_SIZE; X < (int16_t)TANKS_WORLD_WIDTH; X += TANKS_GRID_SIZE)
        {
            Render_Box(Target, (int16_t)(TANKS_ARENA_SCREEN_X + X - 3), (int16_t)(TANKS_ARENA_SCREEN_Y + Y), 7U, 1U, TANKS_COLOUR_FLOOR_LIGHT);
            Render_Box(Target, (int16_t)(TANKS_ARENA_SCREEN_X + X), (int16_t)(TANKS_ARENA_SCREEN_Y + Y - 3), 1U, 7U, TANKS_COLOUR_FLOOR_LIGHT);
        }
    }
    Render_Box(Target, 0, TANKS_WORLD_HEIGHT, RENDER_WIDTH, (uint16_t)(RENDER_HEIGHT - TANKS_WORLD_HEIGHT), TANKS_COLOUR_OUTSIDE);
}

static void Tanks_DrawPitRun(Render_TargetTypeDef *Target, uint8_t StartX, uint8_t Y, uint8_t Length)
{
    const int16_t ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + ((int32_t)StartX * TANKS_TILE_SIZE));
    const int16_t ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + ((int32_t)Y * TANKS_TILE_SIZE));
    const uint16_t Width = (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE);
    Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 4), Width, TANKS_TILE_SIZE, TANKS_COLOUR_SHADOW);
    Render_Box(Target, ScreenX, ScreenY, Width, TANKS_TILE_SIZE, TANKS_COLOUR_PIT_EDGE);
    if((Width > 6U) && (TANKS_TILE_SIZE > 6))
    {
        Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 3), (uint16_t)(Width - 6U), (uint16_t)(TANKS_TILE_SIZE - 6), TANKS_COLOUR_PIT);
        Render_Box(Target, (int16_t)(ScreenX + 4), (int16_t)(ScreenY + 4), (uint16_t)(Width - 8U), 2U, TANKS_COLOUR_SMOKE_DARK);
    }
}

static void Tanks_DrawWallSegment(Render_TargetTypeDef *Target, uint8_t StartX, uint8_t StartY, uint8_t Length, bool Vertical)
{
    const int16_t ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + ((int32_t)StartX * TANKS_TILE_SIZE));
    const int16_t ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + ((int32_t)StartY * TANKS_TILE_SIZE));
    const uint16_t Width = Vertical ? TANKS_TILE_SIZE : (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE);
    const uint16_t Height = Vertical ? (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE) : TANKS_TILE_SIZE;
    Render_Box(Target, (int16_t)(ScreenX + 5), (int16_t)(ScreenY + 6), Width, Height, TANKS_COLOUR_SHADOW);
    Render_Box(Target, ScreenX, ScreenY, Width, Height, TANKS_COLOUR_WALL_SHADOW);
    Render_Box(Target, (int16_t)(ScreenX + 2), (int16_t)(ScreenY + 2), (uint16_t)(Width - 4U), (uint16_t)(Height - 4U), TANKS_COLOUR_WALL);
    Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 3), (uint16_t)(Width - 6U), 6U, TANKS_COLOUR_WALL_LIGHT);
    Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 9), 4U, (uint16_t)(Height - 13U), TANKS_COLOUR_WALL_LIGHT);
    Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + Height - 7U), (uint16_t)(Width - 6U), 4U, TANKS_COLOUR_WALL_SHADOW);
    Render_Box(Target, (int16_t)(ScreenX + Width - 7U), (int16_t)(ScreenY + 9), 4U, (uint16_t)(Height - 13U), TANKS_COLOUR_WALL_SHADOW);
    /* Amber hazard dashes along the barrier's top edge. */
    if(Vertical)
    {
        for(int16_t Offset = 9; Offset + (int16_t)TANKS_HAZARD_LENGTH < (int16_t)Height - 6; Offset += TANKS_HAZARD_SPACING)
        {
            Render_Box(Target, (int16_t)(ScreenX + (int16_t)(Width / 2U) - 1), (int16_t)(ScreenY + Offset), 3U, TANKS_HAZARD_LENGTH, TANKS_COLOUR_HAZARD);
        }
    }
    else
    {
        for(int16_t Offset = 9; Offset + (int16_t)TANKS_HAZARD_LENGTH < (int16_t)Width - 6; Offset += TANKS_HAZARD_SPACING)
        {
            Render_Box(Target, (int16_t)(ScreenX + Offset), (int16_t)(ScreenY + (int16_t)(Height / 2U) - 1), TANKS_HAZARD_LENGTH, 3U, TANKS_COLOUR_HAZARD);
        }
    }
    for(uint8_t Block = 1U; Block < Length; Block++)
    {
        if(Vertical)
        {
            const int16_t SeamY = (int16_t)(ScreenY + ((int32_t)Block * TANKS_TILE_SIZE));
            Render_Box(Target, (int16_t)(ScreenX + 2), (int16_t)(SeamY - 1), (uint16_t)(Width - 4U), 2U, TANKS_COLOUR_WALL_SHADOW);
            Render_Box(Target, (int16_t)(ScreenX + 3), (int16_t)(SeamY + 1), (uint16_t)(Width - 6U), 1U, TANKS_COLOUR_WALL_LIGHT);
        }
        else
        {
            const int16_t SeamX = (int16_t)(ScreenX + ((int32_t)Block * TANKS_TILE_SIZE));
            Render_Box(Target, (int16_t)(SeamX - 1), (int16_t)(ScreenY + 2), 2U, (uint16_t)(Height - 4U), TANKS_COLOUR_WALL_SHADOW);
            Render_Box(Target, (int16_t)(SeamX + 1), (int16_t)(ScreenY + 3), 1U, (uint16_t)(Height - 6U), TANKS_COLOUR_WALL_LIGHT);
        }
    }
}

static void Tanks_DrawArenaGeometry(Render_TargetTypeDef *Target)
{
    bool WallDrawn[TANKS_MAP_HEIGHT][TANKS_MAP_WIDTH];
    Tanks_DrawArenaFloor(Target);
    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        for(uint8_t X = 0U; X < TANKS_MAP_WIDTH; X++)
        {
            WallDrawn[Y][X] = false;
        }
    }

    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        uint8_t X = 0U;
        while(X < TANKS_MAP_WIDTH)
        {
            uint8_t End = X;
            if(Tanks_Game.Tiles[Y][X] != TANKS_TILE_PIT)
            {
                X++;
                continue;
            }
            while((End + 1U < TANKS_MAP_WIDTH) && (Tanks_Game.Tiles[Y][End + 1U] == TANKS_TILE_PIT))
            {
                End++;
            }
            Tanks_DrawPitRun(Target, X, Y, (uint8_t)(End - X + 1U));
            X = (uint8_t)(End + 1U);
        }
    }

    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        for(uint8_t X = 0U; X < TANKS_MAP_WIDTH; X++)
        {
            uint8_t HorizontalLength = 0U;
            uint8_t VerticalLength = 0U;
            bool Vertical;
            uint8_t Length;
            if(WallDrawn[Y][X] || (Tanks_Game.Tiles[Y][X] != TANKS_TILE_WALL))
            {
                continue;
            }
            while((X + HorizontalLength < TANKS_MAP_WIDTH) && !WallDrawn[Y][X + HorizontalLength] &&
                  (Tanks_Game.Tiles[Y][X + HorizontalLength] == TANKS_TILE_WALL))
            {
                HorizontalLength++;
            }
            while((Y + VerticalLength < TANKS_MAP_HEIGHT) && !WallDrawn[Y + VerticalLength][X] &&
                  (Tanks_Game.Tiles[Y + VerticalLength][X] == TANKS_TILE_WALL))
            {
                VerticalLength++;
            }
            Vertical = VerticalLength > HorizontalLength;
            Length = Vertical ? VerticalLength : HorizontalLength;
            for(uint8_t Index = 0U; Index < Length; Index++)
            {
                WallDrawn[Y + (Vertical ? Index : 0U)][X + (Vertical ? 0U : Index)] = true;
            }
            Tanks_DrawWallSegment(Target, X, Y, Length, Vertical);
        }
    }
}

static void Tanks_DrawTrackMark(Render_TargetTypeDef *Target, const Tanks_TrackMarkTypeDef *Mark)
{
    int16_t X;
    int16_t Y;
    const int16_t Angle = Mark->Heading;
    Tanks_WorldToScreen(Mark->Position, &X, &Y);
    if((X < -50) || (Y < -50) || (X > (int16_t)RENDER_WIDTH + 50) || (Y > (int16_t)RENDER_HEIGHT + 50))
    {
        return;
    }
    /* One link of each tread, either side of the hull: two trails that fade with age. */
    {
        const uint8_t Colour = (Mark->LifeMilliseconds > TANKS_TRAIL_FRESH_MS) ? TANKS_COLOUR_TRAIL : TANKS_COLOUR_TRAIL_OLD;
        Render_PointTypeDef Left;
        Render_PointTypeDef Right;
        Tanks_RotateLocalPoint(X, Y, -18, 0, Angle, &Left);
        Tanks_RotateLocalPoint(X, Y, 18, 0, Angle, &Right);
        Tanks_DrawRotatedRect(Target, Left.X, Left.Y, 3, 2, Angle, Colour);
        Tanks_DrawRotatedRect(Target, Right.X, Right.Y, 3, 2, Angle, Colour);
    }
}

static bool Tanks_RenderEnemyUsesRocket(Tanks_EnemyTypeDef Type)
{
    return (Type == TANKS_ENEMY_ROCKET) || (Type == TANKS_ENEMY_HUNTER_ROCKET);
}

static bool Tanks_RenderEnemyTargetsPlayer(Tanks_EnemyTypeDef Type)
{
    return (Type == TANKS_ENEMY_HUNTER_DIRECT) || (Type == TANKS_ENEMY_HUNTER_RICOCHET) || (Type == TANKS_ENEMY_HUNTER_ROCKET);
}

static void Tanks_TankDimensions(const Tanks_TankTypeDef *Tank, bool Player, int16_t *HalfWidth, int16_t *HalfHeight)
{
    if(Player)
    {
        *HalfWidth = 15;
        *HalfHeight = 21;
    }
    else if((Tank->Type == TANKS_ENEMY_MINELAYER) || Tanks_RenderEnemyUsesRocket((Tanks_EnemyTypeDef)Tank->Type))
    {
        *HalfWidth = 14;
        *HalfHeight = 20;
    }
    else if(Tanks_RenderEnemyTargetsPlayer((Tanks_EnemyTypeDef)Tank->Type))
    {
        *HalfWidth = 13;
        *HalfHeight = 19;
    }
    else
    {
        *HalfWidth = 12;
        *HalfHeight = 18;
    }
}

/* Gun is false for an unarmed tank (the mine-layer), drawn without a barrel. */
static void Tanks_DrawTankBody(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, int16_t HullAngle, int16_t TurretAngle,
                               int16_t HalfWidth, int16_t HalfHeight, uint8_t Body, uint8_t Light, uint8_t Dark, bool Boss, bool Gun)
{
    Render_PointTypeDef Hull[6];
    Render_PointTypeDef LeftCentre;
    Render_PointTypeDef RightCentre;
    Render_PointTypeDef BarrelBase;
    Render_PointTypeDef BarrelTip;
    Render_PointTypeDef Stripe;
    const int16_t TrackWidth = Boss ? 4 : 3;
    const int16_t BarrelLength = (int16_t)(HalfHeight + 17);
    (void)TurretAngle;
    Tanks_DrawRotatedRect(Target, (int16_t)(CentreX + 2), (int16_t)(CentreY + 3), (int16_t)(HalfWidth + 4), (int16_t)(HalfHeight + 1), HullAngle, TANKS_COLOUR_SHADOW);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(-HalfWidth - 3), 0, HullAngle, &LeftCentre);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(HalfWidth + 3), 0, HullAngle, &RightCentre);
    Tanks_DrawRotatedRect(Target, LeftCentre.X, LeftCentre.Y, TrackWidth, HalfHeight, HullAngle, TANKS_COLOUR_TRACK);
    Tanks_DrawRotatedRect(Target, RightCentre.X, RightCentre.Y, TrackWidth, HalfHeight, HullAngle, TANKS_COLOUR_TRACK);
    Tanks_DrawRotatedRect(Target, LeftCentre.X, LeftCentre.Y, 1, (int16_t)(HalfHeight - 2), HullAngle, TANKS_COLOUR_TRACK_LIGHT);
    Tanks_DrawRotatedRect(Target, RightCentre.X, RightCentre.Y, 1, (int16_t)(HalfHeight - 2), HullAngle, TANKS_COLOUR_TRACK_LIGHT);

    Tanks_RotateLocalPoint(CentreX, CentreY, -HalfWidth, (int16_t)-HalfHeight, HullAngle, &Hull[0]);
    Tanks_RotateLocalPoint(CentreX, CentreY, HalfWidth, (int16_t)-HalfHeight, HullAngle, &Hull[1]);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(HalfWidth + 2), (int16_t)(HalfHeight - 5), HullAngle, &Hull[2]);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(HalfWidth - 4), HalfHeight, HullAngle, &Hull[3]);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(-HalfWidth + 4), HalfHeight, HullAngle, &Hull[4]);
    Tanks_RotateLocalPoint(CentreX, CentreY, (int16_t)(-HalfWidth - 2), (int16_t)(HalfHeight - 5), HullAngle, &Hull[5]);
    (void)Render_DrawPolygon(Target, Hull, 6U, Body);
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, -5, HullAngle, &Stripe);
    Tanks_DrawRotatedRect(Target, Stripe.X, Stripe.Y, (int16_t)(HalfWidth - 4), 3, HullAngle, Light);
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, (int16_t)(HalfHeight - 5), HullAngle, &Stripe);
    Tanks_DrawRotatedRect(Target, Stripe.X, Stripe.Y, (int16_t)(HalfWidth - 5), 2, HullAngle, Dark);

    Render_FillCircle(Target, CentreX, CentreY, (uint16_t)(Boss ? 8 : 7), Dark);
    Render_FillCircle(Target, CentreX, (int16_t)(CentreY - 1), (uint16_t)(Boss ? 6 : 5), Light);
    if(!Gun)
    {
        return;
    }
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, -4, HullAngle, &BarrelBase);
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, (int16_t)-BarrelLength, HullAngle, &BarrelTip);
    Render_DrawLine(Target, BarrelBase.X, BarrelBase.Y, BarrelTip.X, BarrelTip.Y, (uint16_t)(2 * (Boss ? 4 : 3)), Dark);
    Render_DrawLine(Target, BarrelBase.X, BarrelBase.Y, BarrelTip.X, BarrelTip.Y, (uint16_t)(2 * (Boss ? 2 : 2)), Body);
    Render_FillCircle(Target, BarrelTip.X, BarrelTip.Y, (uint16_t)(Boss ? 4 : 3), Dark);
    Render_FillCircle(Target, BarrelTip.X, BarrelTip.Y, (uint16_t)(1), TANKS_COLOUR_FIRE_LIGHT);
}

static void Tanks_DrawTank(Render_TargetTypeDef *Target, const Tanks_TankTypeDef *Tank, int16_t CentreX, int16_t CentreY, int16_t HullAngle, int16_t TurretAngle, bool Player)
{
    int16_t HalfWidth;
    int16_t HalfHeight;
    uint8_t Body;
    uint8_t Light;
    uint8_t Dark;
    const Tanks_EnemyTypeDef Type = (Tanks_EnemyTypeDef)Tank->Type;
    Tanks_TankDimensions(Tank, Player, &HalfWidth, &HalfHeight);
    if(Player)
    {
        Body = Tank->FlashMilliseconds > 0U ? TANKS_COLOUR_WHITE : TANKS_COLOUR_PLAYER;
        Light = Tank->FlashMilliseconds > 0U ? TANKS_COLOUR_WHITE : TANKS_COLOUR_PLAYER_LIGHT;
        Dark = TANKS_COLOUR_PLAYER_DARK;
    }
    else
    {
        switch(Type)
        {
            case TANKS_ENEMY_RICOCHET:
                Body = TANKS_COLOUR_PLAYER_LASER;
                Light = TANKS_COLOUR_WHITE;
                Dark = TANKS_COLOUR_TRACK;
                break;
            case TANKS_ENEMY_MINELAYER:
                Body = TANKS_COLOUR_DANGER;
                Light = TANKS_COLOUR_MINE;
                Dark = TANKS_COLOUR_TRACK;
                break;
            case TANKS_ENEMY_ROCKET:
                Body = TANKS_COLOUR_MUTED;
                Light = TANKS_COLOUR_WHITE;
                Dark = TANKS_COLOUR_SMOKE_DARK;
                break;
            case TANKS_ENEMY_HUNTER_DIRECT:
                Body = TANKS_COLOUR_ENEMY_LIGHT;
                Light = TANKS_COLOUR_WHITE;
                Dark = TANKS_COLOUR_ENEMY_DARK;
                break;
            case TANKS_ENEMY_HUNTER_RICOCHET:
                Body = TANKS_COLOUR_HQ_LIGHT;
                Light = TANKS_COLOUR_WHITE;
                Dark = TANKS_COLOUR_HQ_DARK;
                break;
            case TANKS_ENEMY_HUNTER_ROCKET:
                Body = TANKS_COLOUR_FIRE;
                Light = TANKS_COLOUR_FIRE_LIGHT;
                Dark = TANKS_COLOUR_BLACK;
                break;
            case TANKS_ENEMY_DUMB:
            default:
                Body = TANKS_COLOUR_ENEMY;
                Light = TANKS_COLOUR_ENEMY_LIGHT;
                Dark = TANKS_COLOUR_ENEMY_DARK;
                break;
        }
        if(Tank->FlashMilliseconds > 0U)
        {
            Body = TANKS_COLOUR_WHITE;
            Light = TANKS_COLOUR_WHITE;
        }
    }
    Tanks_DrawTankBody(Target, CentreX, CentreY, HullAngle, TurretAngle, HalfWidth, HalfHeight, Body, Light, Dark,
                       !Player && (Type == TANKS_ENEMY_HUNTER_ROCKET), Player || (Type != TANKS_ENEMY_MINELAYER));
    if(!Player && (Type == TANKS_ENEMY_MINELAYER))
    {
        for(int16_t Offset = -7; Offset <= 7; Offset += 7)
        {
            Render_PointTypeDef Rack;
            Tanks_RotateLocalPoint(CentreX, CentreY, Offset, (int16_t)(HalfHeight - 4), HullAngle, &Rack);
            Render_FillCircle(Target, Rack.X, Rack.Y, (uint16_t)(3), TANKS_COLOUR_TRACK);
            Render_FillCircle(Target, Rack.X, Rack.Y, (uint16_t)(1), TANKS_COLOUR_DANGER);
        }
    }
    else if(!Player && Tanks_RenderEnemyUsesRocket(Type))
    {
        Render_PointTypeDef LeftPod;
        Render_PointTypeDef RightPod;
        Tanks_RotateLocalPoint(CentreX, CentreY, -8, -3, HullAngle, &LeftPod);
        Tanks_RotateLocalPoint(CentreX, CentreY, 8, -3, HullAngle, &RightPod);
        Tanks_DrawRotatedRect(Target, LeftPod.X, LeftPod.Y, 3, 8, HullAngle, TANKS_COLOUR_TRACK);
        Tanks_DrawRotatedRect(Target, RightPod.X, RightPod.Y, 3, 8, HullAngle, TANKS_COLOUR_TRACK);
        Render_FillCircle(Target, LeftPod.X, LeftPod.Y, (uint16_t)(2), TANKS_COLOUR_FIRE_LIGHT);
        Render_FillCircle(Target, RightPod.X, RightPod.Y, (uint16_t)(2), TANKS_COLOUR_FIRE_LIGHT);
    }
    if(!Player && Tanks_RenderEnemyTargetsPlayer(Type))
    {
        Render_FillCircle(Target, CentreX, CentreY, (uint16_t)(3), TANKS_COLOUR_DANGER);
        Render_FillCircle(Target, CentreX, CentreY, (uint16_t)(1), TANKS_COLOUR_WHITE);
    }
}

static void Tanks_DrawWreck(Render_TargetTypeDef *Target, const Tanks_WreckTypeDef *Wreck)
{
    Tanks_TankTypeDef Tank;
    int16_t X;
    int16_t Y;
    int16_t HalfWidth;
    int16_t HalfHeight;
    const bool PlayerWreck = Wreck->Type == 0xFFU;
    Tank.Type = PlayerWreck ? 0U : Wreck->Type;
    Tanks_WorldToScreen(Wreck->Position, &X, &Y);
    if((X < -70) || (Y < -70) || (X > (int16_t)RENDER_WIDTH + 70) || (Y > (int16_t)RENDER_HEIGHT + 70))
    {
        return;
    }
    Render_FillCircle(Target, X, Y, (uint16_t)(18), TANKS_COLOUR_SCORCH);
    Render_FillCircle(Target, (int16_t)(X - 10), (int16_t)(Y + 6), (uint16_t)(8), TANKS_COLOUR_SMOKE_DARK);
    Render_FillCircle(Target, (int16_t)(X + 10), (int16_t)(Y - 5), (uint16_t)(7), TANKS_COLOUR_SMOKE_DARK);
    Tanks_TankDimensions(&Tank, PlayerWreck, &HalfWidth, &HalfHeight);
    Tanks_DrawTankBody(Target, X, Y,
                       Wreck->Heading,
                       Tanks_NormalizeAngle((int32_t)Wreck->TurretHeading + 180),
                       HalfWidth, HalfHeight, TANKS_COLOUR_WRECK, TANKS_COLOUR_SMOKE_DARK, TANKS_COLOUR_BLACK,
                       (Tank.Type == TANKS_ENEMY_HUNTER_ROCKET) || (Tank.Type == TANKS_ENEMY_HUNTER_RICOCHET),
                       PlayerWreck || (Tank.Type != TANKS_ENEMY_MINELAYER));
    Render_FillCircle(Target, (int16_t)(X + 3), (int16_t)(Y - 2), (uint16_t)(4), TANKS_COLOUR_BLACK);
}

static void Tanks_DrawMine(Render_TargetTypeDef *Target, const Tanks_MineTypeDef *Mine)
{
    int16_t X;
    int16_t Y;
    const bool Bright = (((Tanks_Game.RunMilliseconds / 140U) + (Mine->Owner * 3U)) & 1U) != 0U;
    Tanks_WorldToScreen(Mine->Position, &X, &Y);
    Render_FillCircle(Target, (int16_t)(X + 2), (int16_t)(Y + 2), (uint16_t)(8), TANKS_COLOUR_SHADOW);
    Render_FillCircle(Target, X, Y, (uint16_t)(7), Bright ? TANKS_COLOUR_WARNING : TANKS_COLOUR_MINE);
    Render_FillCircle(Target, X, Y, (uint16_t)(3), Mine->ArmMilliseconds == 0U ? (Bright ? TANKS_COLOUR_DANGER : TANKS_COLOUR_WHITE) : TANKS_COLOUR_TRACK);
    for(int16_t Angle = 0; Angle < TANKS_ANGLE_FULL; Angle += TANKS_ANGLE_QUARTER)
    {
        Render_PointTypeDef Point;
        Tanks_RotateLocalPoint(X, Y, 0, -10, Angle, &Point);
        Render_FillCircle(Target, Point.X, Point.Y, (uint16_t)(2), TANKS_COLOUR_TRACK);
    }
}

static Tanks_VectorTypeDef Tanks_RenderPointAhead(Tanks_VectorTypeDef Position, int16_t Heading, int16_t Distance)
{
    Tanks_VectorTypeDef Result;
    Result.X = Position.X + ((Tanks_Sine(Heading) * Distance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    Result.Y = Position.Y - ((Tanks_Cosine(Heading) * Distance * TANKS_FP_ONE) / TANKS_TRIG_ONE);
    return Result;
}

static uint8_t Tanks_RenderTileAtWorld(Tanks_VectorTypeDef Position)
{
    const int32_t PixelX = Position.X >> TANKS_FP_SHIFT;
    const int32_t PixelY = Position.Y >> TANKS_FP_SHIFT;
    return Tanks_RenderTileAt((int16_t)(PixelX / TANKS_TILE_SIZE), (int16_t)(PixelY / TANKS_TILE_SIZE));
}

static void Tanks_DrawPlayerLaserDot(Render_TargetTypeDef *Target)
{
    Tanks_VectorTypeDef Position;
    int16_t DotX;
    int16_t DotY;
    if(!Tanks_Game.Player.Active)
    {
        return;
    }
    Position = Tanks_RenderPointAhead(Tanks_Game.Player.Position, Tanks_Game.Player.Heading, 48);
    for(uint16_t Step = 0U; Step < 230U; Step++)
    {
        const Tanks_VectorTypeDef Candidate = Tanks_RenderPointAhead(Position, Tanks_Game.Player.Heading, 4);
        if(Tanks_RenderTileAtWorld(Candidate) == TANKS_TILE_WALL)
        {
            Tanks_WorldToScreen(Candidate, &DotX, &DotY);
            Render_FillCircle(Target, DotX, DotY, (uint16_t)(4), TANKS_COLOUR_SHADOW);
            Render_FillCircle(Target, DotX, DotY, (uint16_t)(3), TANKS_COLOUR_DANGER);
            Render_FillCircle(Target, DotX, DotY, (uint16_t)(1), TANKS_COLOUR_WHITE);
            return;
        }
        Position = Candidate;
    }
}

static void Tanks_DrawAimLasers(Render_TargetTypeDef *Target)
{
    Tanks_DrawPlayerLaserDot(Target);
}

static void Tanks_DrawWorldEntities(Render_TargetTypeDef *Target)
{
    for(uint8_t Index = 0U; Index < TANKS_MAX_TRACK_MARKS; Index++)
    {
        if(Tanks_Game.TrackMarks[Index].Active)
        {
            Tanks_DrawTrackMark(Target, &Tanks_Game.TrackMarks[Index]);
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_WRECKS; Index++)
    {
        if(Tanks_Game.Wrecks[Index].Active)
        {
            Tanks_DrawWreck(Target, &Tanks_Game.Wrecks[Index]);
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++)
    {
        if(Tanks_Game.Mines[Index].Active)
        {
            Tanks_DrawMine(Target, &Tanks_Game.Mines[Index]);
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        const Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
        int16_t X;
        int16_t Y;
        if(!Enemy->Active)
        {
            continue;
        }
        Tanks_WorldToScreen(Enemy->Position, &X, &Y);
        if((X < -80) || (Y < -80) || (X > (int16_t)RENDER_WIDTH + 80) || (Y > (int16_t)RENDER_HEIGHT + 80))
        {
            continue;
        }
        Tanks_DrawTank(Target, Enemy, X, Y,
                       Enemy->Heading,
                       Enemy->TurretHeading, false);
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_BULLETS; Index++)
    {
        const Tanks_BulletTypeDef *Bullet = &Tanks_Game.Bullets[Index];
        Tanks_VectorTypeDef Trail;
        int16_t X;
        int16_t Y;
        int16_t TrailX;
        int16_t TrailY;
        const uint8_t Colour = Bullet->Owner == 0U ? TANKS_COLOUR_WHITE : TANKS_COLOUR_DANGER;
        if(!Bullet->Active)
        {
            continue;
        }
        Tanks_WorldToScreen(Bullet->Position, &X, &Y);
        Trail.X = Bullet->Position.X - (Bullet->Velocity.X / 16);
        Trail.Y = Bullet->Position.Y - (Bullet->Velocity.Y / 16);
        Tanks_WorldToScreen(Trail, &TrailX, &TrailY);
        if(Bullet->Type == TANKS_PROJECTILE_ROCKET)
        {
            Render_DrawLine(Target, TrailX, TrailY, X, Y, (uint16_t)(2 * (2)), TANKS_COLOUR_SMOKE);
            Tanks_DrawRotatedRect(Target, X, Y, 4, 8, Bullet->Heading, TANKS_COLOUR_DANGER);
            Render_FillCircle(Target, X, Y, (uint16_t)(3), TANKS_COLOUR_FIRE_LIGHT);
            Render_FillCircle(Target, X, Y, (uint16_t)(1), TANKS_COLOUR_WHITE);
        }
        else
        {
            /* A dark outline keeps the trail and shell crisp against the grid. */
            Render_DrawLine(Target, TrailX, TrailY, X, Y, (uint16_t)(2 * (2)), TANKS_COLOUR_SHADOW);
            Render_DrawLine(Target, TrailX, TrailY, X, Y, (uint16_t)(2 * (1)), Colour);
            Render_FillCircle(Target, X, Y, (uint16_t)(5), TANKS_COLOUR_SHADOW);
            Render_FillCircle(Target, X, Y, (uint16_t)(3), Colour);
            Render_FillCircle(Target, X, Y, (uint16_t)(1), TANKS_COLOUR_WHITE);
        }
    }
    /* Shockwaves: a ring racing out to the blast radius, thinning as it goes. */
    for(uint8_t Index = 0U; Index < TANKS_MAX_BLASTS; Index++)
    {
        const Tanks_BlastTypeDef *Blast = &Tanks_Game.Blasts[Index];
        const uint32_t Elapsed = TANKS_BLAST_RING_MS - Blast->LifeMilliseconds;
        int16_t X;
        int16_t Y;
        if(!Blast->Active)
        {
            continue;
        }
        Tanks_WorldToScreen(Blast->Position, &X, &Y);
        Render_DrawCircle(Target, X, Y, (uint16_t)(8U + (((uint32_t)Blast->Radius - 8U) * Elapsed) / TANKS_BLAST_RING_MS),
                          (uint16_t)(1U + ((3U * Blast->LifeMilliseconds) / TANKS_BLAST_RING_MS)), (Elapsed < (TANKS_BLAST_RING_MS / 2U)) ? TANKS_COLOUR_FIRE_LIGHT : TANKS_COLOUR_SMOKE);
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_PARTICLES; Index++)
    {
        const Tanks_ParticleTypeDef *Particle = &Tanks_Game.Particles[Index];
        int16_t X;
        int16_t Y;
        if(!Particle->Active)
        {
            continue;
        }
        Tanks_WorldToScreen(Particle->Position, &X, &Y);
        Render_FillCircle(Target, X, Y, (uint16_t)(Tanks_ScaleVisual(Particle->Size)), Particle->Colour);
    }
}

static const char *Tanks_RoundName(uint16_t Wave)
{
    static const char *const Names[10] = {
        "SHAKEDOWN YARD", "TWIN BAYS", "FOUR LOCKS", "NESTED DEPOT", "SPLIT HANGAR",
        "FIVE CELLS", "CENTRAL PAD", "OFFSET BUNKERS", "LAUNCH RANGE", "CONTROL BLOCK"
    };
    return Names[(Wave - 1U) % 10U];
}

static void Tanks_DrawHud(Render_TargetTypeDef *Target)
{
    Render_Box(Target, 684, 8, 108U, 32U, TANKS_COLOUR_PANEL);
    for(uint8_t Index = 0U; Index < 3U; Index++)
    {
        Render_FillCircle(Target, (int16_t)(700 + Index * 18), 24, (uint16_t)(6), Index < Tanks_Game.Lives ? TANKS_COLOUR_PLAYER_LIGHT : TANKS_COLOUR_SMOKE_DARK);
    }
    Render_FillCircle(Target, 770, 24, (uint16_t)(7), Tanks_Game.Player.ReloadMilliseconds == 0U ? TANKS_COLOUR_SUCCESS : TANKS_COLOUR_WARNING);
    Render_FillCircle(Target, 770, 24, (uint16_t)(3), TANKS_COLOUR_WHITE);
}

/* Sector start: a slim banner with the sector, its name, and one pip per enemy. */
static void Tanks_DrawRoundOverlay(Render_TargetTypeDef *Target)
{
    const uint8_t EnemyCount = Tanks_CountActiveEnemies();
    const int16_t PipStart = (int16_t)(400 - (((int16_t)EnemyCount - 1) * 9));
    Render_Box(Target, 0, 168, RENDER_WIDTH, 112U, TANKS_COLOUR_PANEL);
    Render_Box(Target, 0, 168, RENDER_WIDTH, 2U, TANKS_COLOUR_PLAYER);
    Render_Box(Target, 0, 278, RENDER_WIDTH, 2U, TANKS_COLOUR_PLAYER);
    Render_DrawTextf(Target, &OpenSans16, 140, 186, TANKS_COLOUR_PLAYER_LIGHT, "SECTOR %02u", (unsigned int)Tanks_Game.Wave);
    Render_DrawTextAligned(Target, &OpenSans36, Tanks_RoundName(Tanks_Game.ArenaRound), 400, 200, RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
    for(uint8_t Index = 0U; Index < EnemyCount; Index++)
    {
        Tanks_DrawRotatedRect(Target, (int16_t)(PipStart + ((int16_t)Index * 18)), 258, 4, 4, 450, TANKS_COLOUR_ENEMY);
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Tanks_DrawGame(Render_TargetTypeDef *Target)
{
    int16_t PlayerX;
    int16_t PlayerY;
    Tanks_DrawOutsidePattern(Target);
    Tanks_DrawArenaGeometry(Target);
    Tanks_DrawAimLasers(Target);
    Tanks_DrawWorldEntities(Target);
    if(Tanks_Game.Player.Active)
    {
        Tanks_WorldToScreen(Tanks_Game.Player.Position, &PlayerX, &PlayerY);
        Tanks_DrawTank(Target, &Tanks_Game.Player, PlayerX, PlayerY, Tanks_Game.Player.Heading,
                       Tanks_Game.Player.TurretHeading, true);
    }
    Tanks_DrawHud(Target);

    if(Tanks_Game.Screen == TANKS_SCREEN_WAVE_INTRO)
    {
        Tanks_DrawRoundOverlay(Target);
    }
    else if(Tanks_Game.RoundComplete)
    {
        Render_Box(Target, 0, 196, RENDER_WIDTH, 72U, TANKS_COLOUR_PANEL);
        Render_Box(Target, 0, 196, RENDER_WIDTH, 2U, TANKS_COLOUR_SUCCESS);
        Render_Box(Target, 0, 266, RENDER_WIDTH, 2U, TANKS_COLOUR_SUCCESS);
        Render_DrawTextAligned(Target, &OpenSans36, "SECTOR SECURED", 400, 210, RENDER_ALIGN_CENTRE, TANKS_COLOUR_SUCCESS);
    }
    else if(Tanks_Game.MessageMilliseconds > 0U)
    {
        const int16_t Width = (int16_t)((int16_t)Render_TextWidth(&OpenSans20, Tanks_Game.Message) + 38);
        Render_Box(Target, (int16_t)(400 - Width / 2), 44, (uint16_t)Width, 34U, TANKS_COLOUR_PANEL);
        Render_DrawTextAligned(Target, &OpenSans20, Tanks_Game.Message, 400, 51, RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
    }
}

/*
 * Service medal: a teal, white and amber ribbon above a medal stamped with the
 * best round. Rounds 1-3 earn bronze, 4-6 silver and 7 onwards gold; with no
 * record yet the medal is plain grey and blank.
 */
static void Tanks_DrawMedal(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, int16_t Radius, uint16_t Wave, const Font *NumberFont, int16_t NumberRise)
{
    const int16_t RibbonWidth = (int16_t)((Radius * 9) / 8);
    const int16_t RibbonHeight = Radius;
    const int16_t RibbonX = (int16_t)(CentreX - (RibbonWidth / 2));
    const int16_t RibbonY = (int16_t)(CentreY - Radius - RibbonHeight + 6);
    const int16_t Stripe = (int16_t)(RibbonWidth / 3);
    uint8_t Rim = TANKS_COLOUR_SMOKE_DARK;
    uint8_t Face = TANKS_COLOUR_SMOKE;
    uint8_t Shine = TANKS_COLOUR_SMOKE;
    char Number[8];

    if(Wave >= 7U)
    {
        Rim = TANKS_COLOUR_MINE;
        Face = TANKS_COLOUR_FIRE_LIGHT;
        Shine = TANKS_COLOUR_BULLET;
    }
    else if(Wave >= 4U)
    {
        Rim = TANKS_COLOUR_SMOKE;
        Face = TANKS_COLOUR_MUTED;
        Shine = TANKS_COLOUR_WHITE;
    }
    else if(Wave >= 1U)
    {
        Rim = TANKS_COLOUR_WALL_SHADOW;
        Face = TANKS_COLOUR_WALL;
        Shine = TANKS_COLOUR_WALL_LIGHT;
    }

    Render_Box(Target, (int16_t)(RibbonX + 2), (int16_t)(RibbonY + 3), (uint16_t)RibbonWidth, (uint16_t)RibbonHeight, TANKS_COLOUR_SHADOW);
    Render_Box(Target, RibbonX, RibbonY, (uint16_t)Stripe, (uint16_t)RibbonHeight, TANKS_COLOUR_PLAYER);
    Render_Box(Target, (int16_t)(RibbonX + Stripe), RibbonY, (uint16_t)Stripe, (uint16_t)RibbonHeight, TANKS_COLOUR_WHITE);
    Render_Box(Target, (int16_t)(RibbonX + (2 * Stripe)), RibbonY, (uint16_t)(RibbonWidth - (2 * Stripe)), (uint16_t)RibbonHeight, TANKS_COLOUR_WARNING);

    Render_FillCircle(Target, (int16_t)(CentreX + 2), (int16_t)(CentreY + 3), (uint16_t)(Radius), TANKS_COLOUR_SHADOW);
    Render_FillCircle(Target, CentreX, CentreY, (uint16_t)(Radius), Rim);
    Render_FillCircle(Target, CentreX, CentreY, (uint16_t)((int16_t)(Radius - 3)), Face);
    Render_FillCircle(Target, (int16_t)(CentreX - (Radius / 3)), (int16_t)(CentreY - (Radius / 3)), (uint16_t)((int16_t)(Radius / 5)), Shine);

    if(Wave > 0U)
    {
        Tanks_FormatUnsigned(Number, sizeof(Number), Wave);
        Render_DrawTextAligned(Target, NumberFont, Number, CentreX, (int16_t)(CentreY - NumberRise), RENDER_ALIGN_CENTRE, TANKS_COLOUR_PANEL);
    }
}

void Tanks_DrawTitle(Render_TargetTypeDef *Target)
{
    Tanks_TankTypeDef Preview = Tanks_Game.Player;
    Tanks_DrawOutsidePattern(Target);
    Tanks_DrawArenaFloor(Target);
    Render_Box(Target, 200, 100, 400U, 270U, TANKS_COLOUR_PANEL);
    Render_Box(Target, 200, 100, 400U, 2U, TANKS_COLOUR_PLAYER);
    Render_DrawTextAligned(Target, &OpenSans36, "DUALTRACK", 400, 116, RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
    Render_DrawTextAligned(Target, &OpenSans16, "TWO SLIDERS. TWO TREADS.", 400, 162, RENDER_ALIGN_CENTRE, TANKS_COLOUR_PLAYER_LIGHT);
    Preview.Active = true;
    Preview.Type = 0U;
    Preview.FlashMilliseconds = 0U;
    Preview.Heading = 900;
    Preview.TurretHeading = Preview.Heading;
    Tanks_DrawTank(Target, &Preview, 320, 240, 900, 900, true);

    /* The record: best round on the medal, the holder's callsign beneath. */
    Tanks_DrawMedal(Target, 480, 240, 28, Tanks_Game.Record.BestWave, &OpenSans28, 20);
    Render_DrawTextAligned(Target, &OpenSans20, Tanks_Game.Record.BestCallsign, 480, 276, RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
    if(((Tanks_Game.ScreenMilliseconds / 450U) & 1U) == 0U)
    {
        Render_DrawTextAligned(Target, &OpenSans20, "PRESS PRIMARY TO START", 400, 316, RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
    }
}

/*
 * New record: the medal earned and three callsign boxes. Set letters are
 * white, the letter being chosen is yellow in a blinking frame, and letters
 * still to come show a dash.
 */
static void Tanks_DrawRecordEntry(Render_TargetTypeDef *Target)
{
    const Tanks_RecordTypeDef *Record = &Tanks_Game.Record;
    const uint16_t Wave = (uint16_t)Tanks_Clamp32(Tanks_Game.Wave, 0, 99);
    const bool FrameLit = ((Tanks_Game.ScreenMilliseconds / 400U) & 1U) == 0U;
    Render_Box(Target, 220, 110, 360U, 230U, TANKS_COLOUR_PANEL);
    Render_Box(Target, 220, 110, 360U, 2U, TANKS_COLOUR_WARNING);
    Render_DrawTextAligned(Target, &OpenSans28, "NEW RECORD", 400, 120, RENDER_ALIGN_CENTRE, TANKS_COLOUR_WARNING);
    Tanks_DrawMedal(Target, 400, 196, 22, Wave, &OpenSans20, 15);
    for(uint8_t Index = 0U; Index < TANKS_CALLSIGN_LENGTH; Index++)
    {
        const int16_t BoxX = (int16_t)(322 + ((int16_t)Index * 56));
        const bool Current = Index == Record->Index;
        char Letter[2];
        uint8_t Colour;
        if(Current && FrameLit)
        {
            Render_Box(Target, (int16_t)(BoxX - 3), 229, 50U, 56U, TANKS_COLOUR_WARNING);
        }
        Render_Box(Target, BoxX, 232, 44U, 50U, TANKS_COLOUR_SHADOW);
        if(Current)
        {
            Letter[0] = (char)('A' + Record->Letter);
            Colour = TANKS_COLOUR_WARNING;
        }
        else
        {
            Letter[0] = Record->Callsign[Index];
            Colour = Index < Record->Index ? TANKS_COLOUR_TEXT : TANKS_COLOUR_MUTED;
        }
        Letter[1] = '\0';
        Render_DrawTextAligned(Target, &OpenSans36, Letter, (int16_t)(BoxX + 22), 231, RENDER_ALIGN_CENTRE, Colour);
    }
    Render_DrawTextAligned(Target, &OpenSans16, "SLIDE: LETTER   PRIMARY: OK", 400, 300, RENDER_ALIGN_CENTRE, TANKS_COLOUR_MUTED);
}

void Tanks_DrawEndScreen(Render_TargetTypeDef *Target)
{
    char Number[8];
    char Round[16];
    Tanks_DrawGame(Target);
    if(Tanks_Game.Record.Entering)
    {
        Tanks_DrawRecordEntry(Target);
        return;
    }
    Render_Box(Target, 220, 130, 360U, 210U, TANKS_COLOUR_PANEL);
    Render_Box(Target, 220, 130, 360U, 2U, TANKS_COLOUR_DANGER);
    Render_DrawTextAligned(Target, &OpenSans36, "SIGNAL LOST", 400, 152, RENDER_ALIGN_CENTRE, TANKS_COLOUR_DANGER);
    Tanks_FormatUnsigned(Number, sizeof(Number), Tanks_Game.Wave);
    Tanks_JoinText(Round, sizeof(Round), "SECTOR ", Number);
    Render_DrawTextAligned(Target, &OpenSans20, Round, 400, 210, RENDER_ALIGN_CENTRE, TANKS_COLOUR_MUTED);
    Render_DrawTextAligned(Target, &OpenSans20, "PRIMARY: PLAY AGAIN", 400, 262, RENDER_ALIGN_CENTRE, TANKS_COLOUR_WARNING);
    Render_DrawTextAligned(Target, &OpenSans20, "SECONDARY: MENU", 400, 294, RENDER_ALIGN_CENTRE, TANKS_COLOUR_MUTED);
}

/* The launcher preview: the demo battle around the fort, with the title inside it. */
void Tanks_DrawSplashArtwork(Render_TargetTypeDef *Target)
{
    int16_t PlayerX;
    int16_t PlayerY;
    Tanks_DrawArenaGeometry(Target);
    Tanks_DrawWorldEntities(Target);
    if(Tanks_Game.Player.Active)
    {
        Tanks_WorldToScreen(Tanks_Game.Player.Position, &PlayerX, &PlayerY);
        Tanks_DrawTank(Target, &Tanks_Game.Player, PlayerX, PlayerY, Tanks_Game.Player.Heading, Tanks_Game.Player.TurretHeading, true);
    }

    /* The title, inside the fort's walls. */
    Render_Box(Target, (int16_t)((TANKS_DEMO_FORT_X + 1U) * TANKS_TILE_SIZE), (int16_t)((TANKS_DEMO_FORT_Y + 1U) * TANKS_TILE_SIZE),
               (uint16_t)((TANKS_DEMO_FORT_WIDTH - 2U) * TANKS_TILE_SIZE), (uint16_t)((TANKS_DEMO_FORT_HEIGHT - 2U) * TANKS_TILE_SIZE), TANKS_COLOUR_PANEL);
    Render_DrawTextAligned(Target, &OpenSans36, "DUALTRACK", (int16_t)((TANKS_DEMO_FORT_X + (TANKS_DEMO_FORT_WIDTH / 2U)) * TANKS_TILE_SIZE),
                           (int16_t)(((TANKS_DEMO_FORT_Y + (TANKS_DEMO_FORT_HEIGHT / 2U)) * TANKS_TILE_SIZE) - 25), RENDER_ALIGN_CENTRE, TANKS_COLOUR_TEXT);
}
