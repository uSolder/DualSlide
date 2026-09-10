/**
 * @file tanks_render.c
 * @brief Fixed full-arena camera and low-cost vector artwork for TANKS.
 */

#include "tanks_internal.h"

#include "open_sans.h"

#include <stddef.h>

static void Tanks_FillRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint8_t Colour)
{
    const Render_RectTypeDef Rectangle = { X, Y, Width, Height };
    Render_FillRect(Target, &Rectangle, Colour);
}

static int16_t Tanks_TextWidth(const Font *FontData, const char *Text)
{
    int16_t Width = 0;
    if((FontData == NULL) || (Text == NULL)) return 0;
    while(*Text != '\0')
    {
        const FontGlyph *Glyph = Font_GetGlyph(FontData, (uint8_t)*Text);
        if(Glyph != NULL) Width = (int16_t)(Width + Glyph->advance);
        Text++;
    }
    return Width;
}

static void Tanks_DrawCenteredText(Render_TargetTypeDef *Target, const Font *FontData, const char *Text, int16_t CentreX, int16_t Y, uint8_t Colour)
{
    Render_DrawText(Target, FontData, Text, (int16_t)(CentreX - (Tanks_TextWidth(FontData, Text) / 2)), Y, Colour);
}

static int16_t Tanks_ScaleVisual(int16_t Value)
{
    return Value;
}

static void Tanks_DrawDisc(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, int16_t Radius, uint8_t Colour)
{
    Render_PointTypeDef Points[24];
    for(uint8_t Index = 0U; Index < 24U; Index++)
    {
        const int16_t Angle = (int16_t)(((int32_t)Index * TANKS_ANGLE_FULL) / 24);
        Points[Index].X = (int16_t)(CentreX + ((Tanks_Sine(Angle) * Radius) / TANKS_TRIG_ONE));
        Points[Index].Y = (int16_t)(CentreY - ((Tanks_Cosine(Angle) * Radius) / TANKS_TRIG_ONE));
    }
    (void)Render_DrawPolygon(Target, Points, 24U, Colour);
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

static void Tanks_DrawThickLine(Render_TargetTypeDef *Target, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, int16_t HalfWidth, uint8_t Colour)
{
    const int32_t DeltaX = X2 - X1;
    const int32_t DeltaY = Y2 - Y1;
    const uint32_t Length = Tanks_IntegerSquareRoot((uint64_t)(DeltaX * DeltaX) + (uint64_t)(DeltaY * DeltaY));
    Render_PointTypeDef Points[4];
    int16_t OffsetX;
    int16_t OffsetY;
    if(Length == 0U) { Tanks_DrawDisc(Target, X1, Y1, HalfWidth, Colour); return; }
    OffsetX = (int16_t)((-DeltaY * HalfWidth) / (int32_t)Length);
    OffsetY = (int16_t)((DeltaX * HalfWidth) / (int32_t)Length);
    Points[0].X = (int16_t)(X1 + OffsetX); Points[0].Y = (int16_t)(Y1 + OffsetY);
    Points[1].X = (int16_t)(X2 + OffsetX); Points[1].Y = (int16_t)(Y2 + OffsetY);
    Points[2].X = (int16_t)(X2 - OffsetX); Points[2].Y = (int16_t)(Y2 - OffsetY);
    Points[3].X = (int16_t)(X1 - OffsetX); Points[3].Y = (int16_t)(Y1 - OffsetY);
    (void)Render_DrawPolygon(Target, Points, 4U, Colour);
}


void Tanks_WorldToScreen(Tanks_VectorTypeDef World, int16_t *ScreenX, int16_t *ScreenY)
{
    *ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + (World.X >> TANKS_FP_SHIFT));
    *ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + (World.Y >> TANKS_FP_SHIFT));
}

static void Tanks_DrawOutsidePattern(Render_TargetTypeDef *Target)
{
    Render_Clear(Target, TANKS_COLOUR_FLOOR_DARK);
}

static uint8_t Tanks_RenderTileAt(int16_t X, int16_t Y)
{
    if((X < 0) || (Y < 0) || (X >= TANKS_MAP_WIDTH) || (Y >= TANKS_MAP_HEIGHT)) return TANKS_TILE_WALL;
    return Tanks_Game.Tiles[Y][X];
}

static void Tanks_DrawArenaFloor(Render_TargetTypeDef *Target)
{
    Tanks_FillRect(Target, TANKS_ARENA_SCREEN_X, TANKS_ARENA_SCREEN_Y, TANKS_WORLD_WIDTH, TANKS_WORLD_HEIGHT, TANKS_COLOUR_FLOOR);
    for(uint8_t Row = 1U; Row < 5U; Row++)
    {
        const int16_t Y = (int16_t)((TANKS_WORLD_HEIGHT * Row) / 5U);
        Tanks_FillRect(Target, 0, Y, TANKS_WORLD_WIDTH, 1U, TANKS_COLOUR_GROUT);
        for(uint8_t Join = 0U; Join < 5U; Join++)
        {
            const int16_t X = (int16_t)(((Join * 173U) + (Row * 79U)) % TANKS_WORLD_WIDTH);
            Tanks_FillRect(Target, X, (int16_t)(Y - (TANKS_WORLD_HEIGHT / 5)), 1U, (uint16_t)(TANKS_WORLD_HEIGHT / 5), TANKS_COLOUR_FLOOR_DARK);
        }
    }
    Tanks_FillRect(Target, 0, TANKS_WORLD_HEIGHT, RENDER_WIDTH, (uint16_t)(RENDER_HEIGHT - TANKS_WORLD_HEIGHT), TANKS_COLOUR_FLOOR_DARK);
}

static void Tanks_DrawPitRun(Render_TargetTypeDef *Target, uint8_t StartX, uint8_t Y, uint8_t Length)
{
    const int16_t ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + ((int32_t)StartX * TANKS_TILE_SIZE));
    const int16_t ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + ((int32_t)Y * TANKS_TILE_SIZE));
    const uint16_t Width = (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE);
    Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 4), Width, TANKS_TILE_SIZE, TANKS_COLOUR_SHADOW);
    Tanks_FillRect(Target, ScreenX, ScreenY, Width, TANKS_TILE_SIZE, TANKS_COLOUR_PIT_EDGE);
    if((Width > 6U) && (TANKS_TILE_SIZE > 6))
    {
        Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 3), (uint16_t)(Width - 6U), (uint16_t)(TANKS_TILE_SIZE - 6), TANKS_COLOUR_PIT);
        Tanks_FillRect(Target, (int16_t)(ScreenX + 4), (int16_t)(ScreenY + 4), (uint16_t)(Width - 8U), 2U, TANKS_COLOUR_SMOKE_DARK);
    }
}

static void Tanks_DrawWallSegment(Render_TargetTypeDef *Target, uint8_t StartX, uint8_t StartY, uint8_t Length, bool Vertical)
{
    const int16_t ScreenX = (int16_t)(TANKS_ARENA_SCREEN_X + ((int32_t)StartX * TANKS_TILE_SIZE));
    const int16_t ScreenY = (int16_t)(TANKS_ARENA_SCREEN_Y + ((int32_t)StartY * TANKS_TILE_SIZE));
    const uint16_t Width = Vertical ? TANKS_TILE_SIZE : (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE);
    const uint16_t Height = Vertical ? (uint16_t)((uint16_t)Length * TANKS_TILE_SIZE) : TANKS_TILE_SIZE;
    Tanks_FillRect(Target, (int16_t)(ScreenX + 5), (int16_t)(ScreenY + 6), Width, Height, TANKS_COLOUR_SHADOW);
    Tanks_FillRect(Target, ScreenX, ScreenY, Width, Height, TANKS_COLOUR_WALL_SHADOW);
    Tanks_FillRect(Target, (int16_t)(ScreenX + 2), (int16_t)(ScreenY + 2), (uint16_t)(Width - 4U), (uint16_t)(Height - 4U), TANKS_COLOUR_WALL);
    Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 3), (uint16_t)(Width - 6U), 6U, TANKS_COLOUR_WALL_LIGHT);
    Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + 9), 4U, (uint16_t)(Height - 13U), TANKS_COLOUR_WALL_LIGHT);
    Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(ScreenY + Height - 7U), (uint16_t)(Width - 6U), 4U, TANKS_COLOUR_WALL_SHADOW);
    Tanks_FillRect(Target, (int16_t)(ScreenX + Width - 7U), (int16_t)(ScreenY + 9), 4U, (uint16_t)(Height - 13U), TANKS_COLOUR_WALL_SHADOW);
    for(uint8_t Block = 1U; Block < Length; Block++)
    {
        if(Vertical)
        {
            const int16_t SeamY = (int16_t)(ScreenY + ((int32_t)Block * TANKS_TILE_SIZE));
            Tanks_FillRect(Target, (int16_t)(ScreenX + 2), (int16_t)(SeamY - 1), (uint16_t)(Width - 4U), 2U, TANKS_COLOUR_WALL_SHADOW);
            Tanks_FillRect(Target, (int16_t)(ScreenX + 3), (int16_t)(SeamY + 1), (uint16_t)(Width - 6U), 1U, TANKS_COLOUR_WALL_LIGHT);
        }
        else
        {
            const int16_t SeamX = (int16_t)(ScreenX + ((int32_t)Block * TANKS_TILE_SIZE));
            Tanks_FillRect(Target, (int16_t)(SeamX - 1), (int16_t)(ScreenY + 2), 2U, (uint16_t)(Height - 4U), TANKS_COLOUR_WALL_SHADOW);
            Tanks_FillRect(Target, (int16_t)(SeamX + 1), (int16_t)(ScreenY + 3), 1U, (uint16_t)(Height - 6U), TANKS_COLOUR_WALL_LIGHT);
        }
    }
}

static void Tanks_DrawArenaGeometry(Render_TargetTypeDef *Target)
{
    bool WallDrawn[TANKS_MAP_HEIGHT][TANKS_MAP_WIDTH];
    Tanks_DrawArenaFloor(Target);
    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
        for(uint8_t X = 0U; X < TANKS_MAP_WIDTH; X++) WallDrawn[Y][X] = false;

    for(uint8_t Y = 0U; Y < TANKS_MAP_HEIGHT; Y++)
    {
        uint8_t X = 0U;
        while(X < TANKS_MAP_WIDTH)
        {
            uint8_t End = X;
            if(Tanks_Game.Tiles[Y][X] != TANKS_TILE_PIT) { X++; continue; }
            while((End + 1U < TANKS_MAP_WIDTH) && (Tanks_Game.Tiles[Y][End + 1U] == TANKS_TILE_PIT)) End++;
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
            if(WallDrawn[Y][X] || (Tanks_Game.Tiles[Y][X] != TANKS_TILE_WALL)) continue;
            while((X + HorizontalLength < TANKS_MAP_WIDTH) && !WallDrawn[Y][X + HorizontalLength] &&
                  (Tanks_Game.Tiles[Y][X + HorizontalLength] == TANKS_TILE_WALL)) HorizontalLength++;
            while((Y + VerticalLength < TANKS_MAP_HEIGHT) && !WallDrawn[Y + VerticalLength][X] &&
                  (Tanks_Game.Tiles[Y + VerticalLength][X] == TANKS_TILE_WALL)) VerticalLength++;
            Vertical = VerticalLength > HorizontalLength;
            Length = Vertical ? VerticalLength : HorizontalLength;
            for(uint8_t Index = 0U; Index < Length; Index++)
                WallDrawn[Y + (Vertical ? Index : 0U)][X + (Vertical ? 0U : Index)] = true;
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
    if((X < -50) || (Y < -50) || (X > (int16_t)RENDER_WIDTH + 50) || (Y > (int16_t)RENDER_HEIGHT + 50)) return;
    Tanks_DrawRotatedRect(Target, X, Y, 12, 1, Angle, TANKS_COLOUR_FLOOR_DARK);
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
    if(Player) { *HalfWidth = 15; *HalfHeight = 21; }
    else if((Tank->Type == TANKS_ENEMY_MINELAYER) || Tanks_RenderEnemyUsesRocket((Tanks_EnemyTypeDef)Tank->Type)) { *HalfWidth = 14; *HalfHeight = 20; }
    else if(Tanks_RenderEnemyTargetsPlayer((Tanks_EnemyTypeDef)Tank->Type)) { *HalfWidth = 13; *HalfHeight = 19; }
    else { *HalfWidth = 12; *HalfHeight = 18; }
}

static void Tanks_DrawTankBody(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, int16_t HullAngle, int16_t TurretAngle,
                               int16_t HalfWidth, int16_t HalfHeight, uint8_t Body, uint8_t Light, uint8_t Dark, bool Boss)
{
    Render_PointTypeDef Hull[6];
    Render_PointTypeDef LeftCentre;
    Render_PointTypeDef RightCentre;
    Render_PointTypeDef BarrelBase;
    Render_PointTypeDef BarrelTip;
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
    Tanks_DrawRotatedRect(Target, CentreX, (int16_t)(CentreY - 5), (int16_t)(HalfWidth - 4), 3, HullAngle, Light);
    Tanks_DrawRotatedRect(Target, CentreX, (int16_t)(CentreY + HalfHeight - 5), (int16_t)(HalfWidth - 5), 2, HullAngle, Dark);

    Tanks_DrawDisc(Target, CentreX, CentreY, Boss ? 8 : 7, Dark);
    Tanks_DrawDisc(Target, CentreX, (int16_t)(CentreY - 1), Boss ? 6 : 5, Light);
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, -4, HullAngle, &BarrelBase);
    Tanks_RotateLocalPoint(CentreX, CentreY, 0, (int16_t)-BarrelLength, HullAngle, &BarrelTip);
    Tanks_DrawThickLine(Target, BarrelBase.X, BarrelBase.Y, BarrelTip.X, BarrelTip.Y, Boss ? 4 : 3, Dark);
    Tanks_DrawThickLine(Target, BarrelBase.X, BarrelBase.Y, BarrelTip.X, BarrelTip.Y, Boss ? 2 : 2, Body);
    Tanks_DrawDisc(Target, BarrelTip.X, BarrelTip.Y, Boss ? 4 : 3, Dark);
    Tanks_DrawDisc(Target, BarrelTip.X, BarrelTip.Y, 1, TANKS_COLOUR_FIRE_LIGHT);
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
            case TANKS_ENEMY_RICOCHET: Body = TANKS_COLOUR_SUCCESS; Light = TANKS_COLOUR_WHITE; Dark = TANKS_COLOUR_TRACK; break;
            case TANKS_ENEMY_MINELAYER: Body = TANKS_COLOUR_MINE; Light = TANKS_COLOUR_WARNING; Dark = TANKS_COLOUR_TRACK; break;
            case TANKS_ENEMY_ROCKET: Body = TANKS_COLOUR_DANGER; Light = TANKS_COLOUR_FIRE_LIGHT; Dark = TANKS_COLOUR_ENEMY_DARK; break;
            case TANKS_ENEMY_HUNTER_DIRECT: Body = TANKS_COLOUR_ENEMY_LIGHT; Light = TANKS_COLOUR_WHITE; Dark = TANKS_COLOUR_ENEMY_DARK; break;
            case TANKS_ENEMY_HUNTER_RICOCHET: Body = TANKS_COLOUR_PLAYER_LASER; Light = TANKS_COLOUR_SUCCESS; Dark = TANKS_COLOUR_TRACK; break;
            case TANKS_ENEMY_HUNTER_ROCKET: Body = TANKS_COLOUR_BLACK; Light = TANKS_COLOUR_DANGER; Dark = TANKS_COLOUR_SMOKE_DARK; break;
            case TANKS_ENEMY_DUMB:
            default: Body = TANKS_COLOUR_ENEMY; Light = TANKS_COLOUR_ENEMY_LIGHT; Dark = TANKS_COLOUR_ENEMY_DARK; break;
        }
        if(Tank->FlashMilliseconds > 0U) { Body = TANKS_COLOUR_WHITE; Light = TANKS_COLOUR_WHITE; }
    }
    Tanks_DrawTankBody(Target, CentreX, CentreY, HullAngle, TurretAngle, HalfWidth, HalfHeight, Body, Light, Dark,
                       !Player && (Type == TANKS_ENEMY_HUNTER_ROCKET));
    if(!Player && (Type == TANKS_ENEMY_MINELAYER))
    {
        for(int16_t Offset = -7; Offset <= 7; Offset += 7)
        {
            Render_PointTypeDef Rack;
            Tanks_RotateLocalPoint(CentreX, CentreY, Offset, (int16_t)(HalfHeight - 4), HullAngle, &Rack);
            Tanks_DrawDisc(Target, Rack.X, Rack.Y, 3, TANKS_COLOUR_TRACK);
            Tanks_DrawDisc(Target, Rack.X, Rack.Y, 1, TANKS_COLOUR_DANGER);
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
        Tanks_DrawDisc(Target, LeftPod.X, LeftPod.Y, 2, TANKS_COLOUR_FIRE_LIGHT);
        Tanks_DrawDisc(Target, RightPod.X, RightPod.Y, 2, TANKS_COLOUR_FIRE_LIGHT);
    }
    if(!Player && Tanks_RenderEnemyTargetsPlayer(Type))
    {
        Tanks_DrawDisc(Target, CentreX, CentreY, 3, TANKS_COLOUR_DANGER);
        Tanks_DrawDisc(Target, CentreX, CentreY, 1, TANKS_COLOUR_WHITE);
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
    if((X < -70) || (Y < -70) || (X > (int16_t)RENDER_WIDTH + 70) || (Y > (int16_t)RENDER_HEIGHT + 70)) return;
    Tanks_DrawDisc(Target, X, Y, 18, TANKS_COLOUR_SCORCH);
    Tanks_DrawDisc(Target, (int16_t)(X - 10), (int16_t)(Y + 6), 8, TANKS_COLOUR_SMOKE_DARK);
    Tanks_DrawDisc(Target, (int16_t)(X + 10), (int16_t)(Y - 5), 7, TANKS_COLOUR_SMOKE_DARK);
    Tanks_TankDimensions(&Tank, PlayerWreck, &HalfWidth, &HalfHeight);
    Tanks_DrawTankBody(Target, X, Y,
                       Wreck->Heading,
                       Tanks_NormalizeAngle((int32_t)Wreck->TurretHeading + 180),
                       HalfWidth, HalfHeight, TANKS_COLOUR_WRECK, TANKS_COLOUR_SMOKE_DARK, TANKS_COLOUR_BLACK,
                       (Tank.Type == TANKS_ENEMY_HUNTER_ROCKET) || (Tank.Type == TANKS_ENEMY_HUNTER_RICOCHET));
    Tanks_DrawDisc(Target, (int16_t)(X + 3), (int16_t)(Y - 2), 4, TANKS_COLOUR_BLACK);
}

static void Tanks_DrawMine(Render_TargetTypeDef *Target, const Tanks_MineTypeDef *Mine)
{
    int16_t X;
    int16_t Y;
    const bool Bright = (((Tanks_Game.RunMilliseconds / 140U) + (Mine->Owner * 3U)) & 1U) != 0U;
    Tanks_WorldToScreen(Mine->Position, &X, &Y);
    Tanks_DrawDisc(Target, (int16_t)(X + 2), (int16_t)(Y + 2), 8, TANKS_COLOUR_SHADOW);
    Tanks_DrawDisc(Target, X, Y, 7, Bright ? TANKS_COLOUR_WARNING : TANKS_COLOUR_MINE);
    Tanks_DrawDisc(Target, X, Y, 3, Mine->ArmMilliseconds == 0U ? (Bright ? TANKS_COLOUR_DANGER : TANKS_COLOUR_WHITE) : TANKS_COLOUR_TRACK);
    for(int16_t Angle = 0; Angle < TANKS_ANGLE_FULL; Angle += TANKS_ANGLE_QUARTER)
    {
        Render_PointTypeDef Point;
        Tanks_RotateLocalPoint(X, Y, 0, -10, Angle, &Point);
        Tanks_DrawDisc(Target, Point.X, Point.Y, 2, TANKS_COLOUR_TRACK);
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
    if(!Tanks_Game.Player.Active) return;
    Position = Tanks_RenderPointAhead(Tanks_Game.Player.Position, Tanks_Game.Player.Heading, 48);
    for(uint16_t Step = 0U; Step < 230U; Step++)
    {
        const Tanks_VectorTypeDef Candidate = Tanks_RenderPointAhead(Position, Tanks_Game.Player.Heading, 4);
        if(Tanks_RenderTileAtWorld(Candidate) == TANKS_TILE_WALL)
        {
            Tanks_WorldToScreen(Candidate, &DotX, &DotY);
            Tanks_DrawDisc(Target, DotX, DotY, 4, TANKS_COLOUR_SHADOW);
            Tanks_DrawDisc(Target, DotX, DotY, 3, TANKS_COLOUR_DANGER);
            Tanks_DrawDisc(Target, DotX, DotY, 1, TANKS_COLOUR_WHITE);
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
    for(uint8_t Index = 0U; Index < TANKS_MAX_TRACK_MARKS; Index++) if(Tanks_Game.TrackMarks[Index].Active) Tanks_DrawTrackMark(Target, &Tanks_Game.TrackMarks[Index]);
    for(uint8_t Index = 0U; Index < TANKS_MAX_WRECKS; Index++) if(Tanks_Game.Wrecks[Index].Active) Tanks_DrawWreck(Target, &Tanks_Game.Wrecks[Index]);
    for(uint8_t Index = 0U; Index < TANKS_MAX_MINES; Index++) if(Tanks_Game.Mines[Index].Active) Tanks_DrawMine(Target, &Tanks_Game.Mines[Index]);
    for(uint8_t Index = 0U; Index < TANKS_MAX_ENEMIES; Index++)
    {
        const Tanks_TankTypeDef *Enemy = &Tanks_Game.Enemies[Index];
        int16_t X;
        int16_t Y;
        if(!Enemy->Active) continue;
        Tanks_WorldToScreen(Enemy->Position, &X, &Y);
        if((X < -80) || (Y < -80) || (X > (int16_t)RENDER_WIDTH + 80) || (Y > (int16_t)RENDER_HEIGHT + 80)) continue;
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
        const uint8_t Colour = Bullet->Owner == 0U ? TANKS_COLOUR_BULLET : TANKS_COLOUR_FIRE_LIGHT;
        if(!Bullet->Active) continue;
        Tanks_WorldToScreen(Bullet->Position, &X, &Y);
        Trail.X = Bullet->Position.X - (Bullet->Velocity.X / 28);
        Trail.Y = Bullet->Position.Y - (Bullet->Velocity.Y / 28);
        Tanks_WorldToScreen(Trail, &TrailX, &TrailY);
        if(Bullet->Type == TANKS_PROJECTILE_ROCKET)
        {
            Tanks_DrawThickLine(Target, TrailX, TrailY, X, Y, 2, TANKS_COLOUR_SMOKE);
            Tanks_DrawRotatedRect(Target, X, Y, 4, 8, Bullet->Heading, TANKS_COLOUR_DANGER);
            Tanks_DrawDisc(Target, X, Y, 3, TANKS_COLOUR_FIRE_LIGHT);
            Tanks_DrawDisc(Target, X, Y, 1, TANKS_COLOUR_WHITE);
        }
        else
        {
            Tanks_DrawThickLine(Target, TrailX, TrailY, X, Y, 1, Colour);
            Tanks_DrawDisc(Target, X, Y, 3, Colour);
            Tanks_DrawDisc(Target, X, Y, 1, TANKS_COLOUR_WHITE);
        }
    }
    for(uint8_t Index = 0U; Index < TANKS_MAX_PARTICLES; Index++)
    {
        const Tanks_ParticleTypeDef *Particle = &Tanks_Game.Particles[Index];
        int16_t X;
        int16_t Y;
        if(!Particle->Active) continue;
        Tanks_WorldToScreen(Particle->Position, &X, &Y);
        Tanks_DrawDisc(Target, X, Y, Tanks_ScaleVisual(Particle->Size), Particle->Colour);
    }
}

static const char *Tanks_RoundName(uint16_t Wave)
{
    static const char *const Names[10] =
    {
        "TRAINING ROOMS", "TWIN WINGS", "FOUR CHAMBERS", "NESTED HALLS", "SPLIT HOUSE",
        "FIVE ROOMS", "CENTRAL COURT", "OFFSET SUITES", "ROCKET LAB", "FINAL COMPLEX"
    };
    return Names[(Wave - 1U) % 10U];
}

static void Tanks_DrawHud(Render_TargetTypeDef *Target)
{
    Tanks_FillRect(Target, 684, 8, 108U, 32U, TANKS_COLOUR_PANEL);
    for(uint8_t Index = 0U; Index < 3U; Index++)
        Tanks_DrawDisc(Target, (int16_t)(700 + Index * 18), 24, 6, Index < Tanks_Game.Lives ? TANKS_COLOUR_PLAYER_LIGHT : TANKS_COLOUR_SMOKE_DARK);
    Tanks_DrawDisc(Target, 770, 24, 7, Tanks_Game.Player.ReloadMilliseconds == 0U ? TANKS_COLOUR_SUCCESS : TANKS_COLOUR_WARNING);
    Tanks_DrawDisc(Target, 770, 24, 3, TANKS_COLOUR_WHITE);
}

static void Tanks_DrawRoundOverlay(Render_TargetTypeDef *Target)
{
    char Wave[8];
    char Enemies[8];
    Tanks_FormatUnsigned(Wave, sizeof(Wave), Tanks_Game.Wave);
    Tanks_FormatUnsigned(Enemies, sizeof(Enemies), Tanks_CountActiveEnemies());
    Tanks_FillRect(Target, 215, 147, 370U, 176U, TANKS_COLOUR_PANEL);
    Tanks_FillRect(Target, 215, 147, 370U, 5U, TANKS_COLOUR_PLAYER_LIGHT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "ROUND", 400, 168, TANKS_COLOUR_MUTED);
    Tanks_DrawCenteredText(Target, &OpenSans36, Wave, 400, 192, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans36, Tanks_RoundName(Tanks_Game.Wave), 400, 235, TANKS_COLOUR_PLAYER_LIGHT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "ENEMIES", 348, 282, TANKS_COLOUR_MUTED);
    Tanks_DrawCenteredText(Target, &OpenSans20, Enemies, 468, 282, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans20, Tanks_Game.Wave == 1U ? "ONE BASIC TANK" : "NEW ENEMY TYPES APPEAR EACH ROUND", 400, 306, TANKS_COLOUR_WARNING);
}

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

    if(Tanks_Game.Screen == TANKS_SCREEN_WAVE_INTRO) Tanks_DrawRoundOverlay(Target);
    else if(Tanks_Game.RoundComplete)
    {
        Tanks_FillRect(Target, 252, 174, 296U, 116U, TANKS_COLOUR_PANEL);
        Tanks_FillRect(Target, 252, 174, 296U, 6U, TANKS_COLOUR_SUCCESS);
        Tanks_DrawCenteredText(Target, &OpenSans36, "ARENA CLEAR", 400, 196, TANKS_COLOUR_SUCCESS);
        Tanks_DrawCenteredText(Target, &OpenSans20, "NEW FIELD INCOMING", 400, 252, TANKS_COLOUR_MUTED);
    }
    else if(Tanks_Game.MessageMilliseconds > 0U)
    {
        const int16_t Width = (int16_t)(Tanks_TextWidth(&OpenSans20, Tanks_Game.Message) + 38);
        Tanks_FillRect(Target, (int16_t)(400 - Width / 2), 44, (uint16_t)Width, 34U, TANKS_COLOUR_PANEL);
        Tanks_DrawCenteredText(Target, &OpenSans20, Tanks_Game.Message, 400, 51, TANKS_COLOUR_TEXT);
    }
}

void Tanks_DrawTitle(Render_TargetTypeDef *Target)
{
    Tanks_TankTypeDef Preview = Tanks_Game.Player;
    Tanks_DrawOutsidePattern(Target);
    Tanks_FillRect(Target, 102, 42, 596U, 396U, TANKS_COLOUR_PANEL);
    Tanks_FillRect(Target, 102, 42, 596U, 6U, TANKS_COLOUR_PLAYER_LIGHT);
    Tanks_DrawCenteredText(Target, &OpenSans36, "TANKS", 400, 68, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "ROOM RICOCHET", 400, 114, TANKS_COLOUR_MUTED);
    Preview.Active = true;
    Preview.Type = 0U;
    Preview.FlashMilliseconds = 0U;
    Preview.Heading = 900;
    Preview.TurretHeading = Preview.Heading;
    Tanks_DrawDisc(Target, 535, 212, 5, TANKS_COLOUR_SHADOW);
    Tanks_DrawDisc(Target, 535, 212, 3, TANKS_COLOUR_DANGER);
    Tanks_DrawTank(Target, &Preview, 400, 212, 900, 900, true);
    Tanks_DrawCenteredText(Target, &OpenSans20, "DRIVE WITH BOTH TRACK SLIDERS", 400, 286, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "RED DOT SHOWS THE DIRECT BARREL AIM", 400, 318, TANKS_COLOUR_WARNING);
    Tanks_DrawCenteredText(Target, &OpenSans20, "QUICK TAP PRIMARY: FIRE ONE SHOT", 400, 350, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "DIRECT, RICOCHET, MINE AND ROCKET TANKS", 400, 380, TANKS_COLOUR_SUCCESS);
    if(((Tanks_Game.ScreenMilliseconds / 450U) & 1U) == 0U) Tanks_DrawCenteredText(Target, &OpenSans20, "PRESS PRIMARY TO START", 400, 410, TANKS_COLOUR_TEXT);
}

void Tanks_DrawEndScreen(Render_TargetTypeDef *Target, bool Victory)
{
    char Wave[8];
    Tanks_DrawGame(Target);
    Tanks_FillRect(Target, 172, 100, 456U, 278U, TANKS_COLOUR_PANEL);
    Tanks_FillRect(Target, 172, 100, 456U, 6U, Victory ? TANKS_COLOUR_SUCCESS : TANKS_COLOUR_DANGER);
    Tanks_DrawCenteredText(Target, &OpenSans36, Victory ? "CAMPAIGN COMPLETE" : "TANK DESTROYED", 400, 132, Victory ? TANKS_COLOUR_SUCCESS : TANKS_COLOUR_DANGER);
    Tanks_FormatUnsigned(Wave, sizeof(Wave), Tanks_Game.Wave);
    Tanks_DrawCenteredText(Target, &OpenSans20, Victory ? "ALL TEN ROUNDS CLEARED" : "REACHED ROUND", 400, 218, TANKS_COLOUR_MUTED);
    if(!Victory) Tanks_DrawCenteredText(Target, &OpenSans36, Wave, 400, 246, TANKS_COLOUR_TEXT);
    Tanks_DrawCenteredText(Target, &OpenSans20, "PRIMARY: NEW CAMPAIGN", 400, 316, TANKS_COLOUR_WARNING);
    Tanks_DrawCenteredText(Target, &OpenSans20, "SECONDARY: TITLE", 400, 348, TANKS_COLOUR_MUTED);
}

void Tanks_DrawSplashArtwork(Render_TargetTypeDef *Target, uint32_t ElapsedMilliseconds)
{
    const Render_RectTypeDef Bounds = { TANKS_SPLASH_X, TANKS_SPLASH_Y, TANKS_SPLASH_WIDTH, TANKS_SPLASH_HEIGHT };
    Tanks_TankTypeDef Preview = Tanks_Game.Player;
    const int16_t PatrolX = (int16_t)(TANKS_SPLASH_X + 100 + ((ElapsedMilliseconds / 5U) % 480U));
    Render_FillRect(Target, &Bounds, TANKS_COLOUR_OUTSIDE_DARK);
    for(int16_t Offset = -300; Offset <= 300; Offset += 80)
        Tanks_DrawRotatedRect(Target, 400, (int16_t)(240 + Offset), 380, 18, 450, ((Offset / 80) & 1) != 0 ? TANKS_COLOUR_OUTSIDE : TANKS_COLOUR_OUTSIDE_LIGHT);
    Tanks_FillRect(Target, 90, 92, 620U, 292U, TANKS_COLOUR_FLOOR);
    Tanks_FillRect(Target, 90, 92, 620U, 5U, TANKS_COLOUR_PLAYER_LIGHT);
    Tanks_DrawCenteredText(Target, &OpenSans36, "TANKS", 400, 106, TANKS_COLOUR_PANEL);
    Tanks_DrawCenteredText(Target, &OpenSans20, "RICOCHET TACTICS", 400, 154, TANKS_COLOUR_PLAYER_DARK);
    Preview.Active = true;
    Preview.Type = 0U;
    Preview.FlashMilliseconds = 0U;
    Tanks_DrawDisc(Target, (int16_t)(PatrolX + 145), 318, 4, TANKS_COLOUR_DANGER);
    Tanks_DrawTank(Target, &Preview, PatrolX, 318, 900, 900, true);
    Tanks_DrawThickLine(Target, 118, 350, 682, 350, 2, TANKS_COLOUR_GROUT);
}
