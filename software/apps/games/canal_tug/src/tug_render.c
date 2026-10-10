/**
 * @file tug_render.c
 * @brief Drawing Canal Tug: the scrolling countryside, the sites, the boats,
 *        and the screens over them.
 */

#include "tug_internal.h"

#include "app_manager.h"
#include "open_sans.h"
#include "open_sans_bold.h"
#include "controls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define GAUGE_TOP               (70)
#define GAUGE_HEIGHT            (340)
#define GAUGE_WIDTH             (11)
#define PANEL_RADIUS            (10U)
#define PI_F                    (3.14159265f)


/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Where the world's top-left corner falls on screen this frame. */
static int16_t Tug_OffsetX;
static int16_t Tug_OffsetY;
static uint32_t Tug_Clock;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

/* A steady pseudo-random number for a tile, so its decoration never flickers. */
static uint32_t Tug_TileHash(int16_t X, int16_t Y)
{
    uint32_t Hash = ((uint32_t)(uint16_t)X * 73856093U) ^ ((uint32_t)(uint16_t)Y * 19349663U);
    Hash ^= Hash >> 13;
    Hash *= 0x5BD1E995U;
    return Hash ^ (Hash >> 15);
}

static bool Tug_IsWaterAt(int16_t X, int16_t Y)
{
    return Tug_TileIsWater(Tug_TileAt(X, Y));
}

static bool Tug_IsLaneAt(int16_t X, int16_t Y)
{
    const char Tile = Tug_TileAt(X, Y);
    return Tile == TUG_TILE_LANE;
}

/* Drawing in world pixels. */
static void Tug_Box(Render_TargetTypeDef *Target, float X, float Y, int16_t Width, int16_t Height, uint8_t Colour)
{
    Render_Box(Target, (int16_t)(lrintf(X) + Tug_OffsetX), (int16_t)(lrintf(Y) + Tug_OffsetY), (uint16_t)Width, (uint16_t)Height, Colour);
}

static void Tug_Circle(Render_TargetTypeDef *Target, float X, float Y, uint16_t Radius, uint8_t Colour)
{
    Render_FillCircle(Target, (int16_t)(lrintf(X) + Tug_OffsetX), (int16_t)(lrintf(Y) + Tug_OffsetY), Radius, Colour);
}

static void Tug_Line(Render_TargetTypeDef *Target, float X1, float Y1, float X2, float Y2, uint16_t Thickness, uint8_t Colour)
{
    Render_DrawLine(Target, (int16_t)(lrintf(X1) + Tug_OffsetX), (int16_t)(lrintf(Y1) + Tug_OffsetY), (int16_t)(lrintf(X2) + Tug_OffsetX), (int16_t)(lrintf(Y2) + Tug_OffsetY), Thickness, Colour);
}

/* A filled shape around a body, its points given along the keel (Ahead) and across it (Right). */
static void Tug_DrawShape(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, const float (*Points)[2], uint8_t Count, uint8_t Colour)
{
    Render_PointTypeDef Screen[10];
    const float Sine = sinf(Body->Heading);
    const float Cosine = cosf(Body->Heading);
    for(uint8_t Index = 0U; (Index < Count) && (Index < 10U); Index++)
    {
        Screen[Index].X = (int16_t)(lrintf(Body->X + (Points[Index][0] * Sine) + (Points[Index][1] * Cosine)) + Tug_OffsetX);
        Screen[Index].Y = (int16_t)(lrintf(Body->Y - (Points[Index][0] * Cosine) + (Points[Index][1] * Sine)) + Tug_OffsetY);
    }
    (void)Render_DrawPolygon(Target, Screen, Count, Colour);
}

/* A rectangle on a body: from Back to Front along the keel, from Left to Right across it. */
static void Tug_DrawPlate(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, float Back, float Front, float Left, float Right, uint8_t Colour)
{
    const float Points[4][2] = { { Front, Left }, { Front, Right }, { Back, Right }, { Back, Left } };
    Tug_DrawShape(Target, Body, Points, 4U, Colour);
}

static void Tug_BodyCircle(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, float Ahead, float Right, uint16_t Radius, uint8_t Colour)
{
    float X;
    float Y;
    Tug_LocalPoint(Body, Ahead, Right, &X, &Y);
    Tug_Circle(Target, X, Y, Radius, Colour);
}

static void Tug_BodyLine(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, float Ahead1, float Right1, float Ahead2, float Right2, uint16_t Thickness, uint8_t Colour)
{
    float X1;
    float Y1;
    float X2;
    float Y2;
    Tug_LocalPoint(Body, Ahead1, Right1, &X1, &Y1);
    Tug_LocalPoint(Body, Ahead2, Right2, &X2, &Y2);
    Tug_Line(Target, X1, Y1, X2, Y2, Thickness, Colour);
}

static void Tug_DrawPanel(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height)
{
    Render_FillRoundRect(Target, (int16_t)(X + 4), (int16_t)(Y + 5), Width, Height, PANEL_RADIUS, TUG_COLOUR_SHADOW);
    Render_FillRoundRect(Target, X, Y, Width, Height, PANEL_RADIUS, TUG_COLOUR_PANEL_EDGE);
    Render_FillRoundRect(Target, (int16_t)(X + 2), (int16_t)(Y + 2), (uint16_t)(Width - 4U), (uint16_t)(Height - 4U), PANEL_RADIUS, TUG_COLOUR_PANEL);
}

/* -------------------------------------------------------------------------- */
/* The countryside                                                            */
/* -------------------------------------------------------------------------- */

static void Tug_DrawWaterTile(Render_TargetTypeDef *Target, char Tile, int16_t X, int16_t Y, int16_t Left, int16_t Top)
{
    const uint32_t Hash = Tug_TileHash(X, Y);
    const uint8_t Depth = Tug_WaterDepth(X, Y);

    if((Tile == TUG_TILE_SHALLOWS) || (Tile == TUG_TILE_BUOY))
    {
        Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_SHALLOWS);
        Render_Box(Target, (int16_t)(Left + (int16_t)(Hash % 16U)), (int16_t)(Top + (int16_t)((Hash >> 5) % 16U)), 3U, 2U, TUG_COLOUR_SHALLOWS_DARK);
        if((Hash % 3U) == 0U)
        {
            Render_Box(Target, (int16_t)(Left + (int16_t)((Hash >> 9) % 14U)), (int16_t)(Top + (int16_t)((Hash >> 13) % 16U)), 5U, 1U, TUG_COLOUR_FOAM_FADED);
        }
        return;
    }

    Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, (Depth >= 2U) ? TUG_COLOUR_WATER_DEEP : TUG_COLOUR_WATER);
    if(Depth == 0U)
    {
        /* The bank's shadow on the water beside it. */
        if(!Tug_IsWaterAt(X, (int16_t)(Y - 1)))
        {
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, 3U, TUG_COLOUR_WATER_EDGE);
        }
        if(!Tug_IsWaterAt((int16_t)(X - 1), Y))
        {
            Render_Box(Target, Left, Top, 3U, TUG_TILE_SIZE, TUG_COLOUR_WATER_EDGE);
        }
    }
    if((Hash % 7U) == 0U)
    {
        const int16_t Shift = (int16_t)lrintf(3.0f * sinf(((float)Tug_Clock * 0.0015f) + (float)(Hash % 628U) * 0.01f));
        Render_Box(Target, (int16_t)(Left + 4 + (int16_t)(Hash % 8U) + Shift), (int16_t)(Top + 5 + (int16_t)((Hash >> 8) % 10U)), 7U, 1U, TUG_COLOUR_WATER_GLINT);
    }
}

/* A muddy bank along any side of a land tile that meets the water. */
static void Tug_DrawBank(Render_TargetTypeDef *Target, int16_t X, int16_t Y, int16_t Left, int16_t Top)
{
    if(Tug_IsWaterAt(X, (int16_t)(Y + 1)))
    {
        Render_Box(Target, Left, (int16_t)(Top + TUG_TILE_SIZE - 3), TUG_TILE_SIZE, 3U, TUG_COLOUR_BANK);
    }
    if(Tug_IsWaterAt(X, (int16_t)(Y - 1)))
    {
        Render_Box(Target, Left, Top, TUG_TILE_SIZE, 3U, TUG_COLOUR_BANK);
    }
    if(Tug_IsWaterAt((int16_t)(X + 1), Y))
    {
        Render_Box(Target, (int16_t)(Left + TUG_TILE_SIZE - 3), Top, 3U, TUG_TILE_SIZE, TUG_COLOUR_BANK);
    }
    if(Tug_IsWaterAt((int16_t)(X - 1), Y))
    {
        Render_Box(Target, Left, Top, 3U, TUG_TILE_SIZE, TUG_COLOUR_BANK);
    }
}

/* A field's rows run one way or the other, the same across a patch of fields. */
static bool Tug_RowsAcross(int16_t X, int16_t Y)
{
    return (Tug_TileHash((int16_t)(X / 11), (int16_t)(Y / 9)) & 1U) != 0U;
}

static void Tug_DrawCottage(Render_TargetTypeDef *Target, int16_t X, int16_t Y, int16_t Left, int16_t Top, uint32_t Hash)
{
    static const uint8_t Roofs[3][2] =
    {
        { TUG_COLOUR_ROOF_RED, TUG_COLOUR_ROOF_RED_DARK }, { TUG_COLOUR_ROOF_SLATE, TUG_COLOUR_ROOF_SLATE_DARK }, { TUG_COLOUR_THATCH, TUG_COLOUR_THATCH_DARK }
    };
    const uint8_t Light = Roofs[Hash % 3U][0];
    const uint8_t Dark = Roofs[Hash % 3U][1];
    const bool RidgeAcross = Tug_IsLaneAt(X, (int16_t)(Y - 1)) || Tug_IsLaneAt(X, (int16_t)(Y + 1)) || ((Hash & 32U) != 0U);

    if((Hash % 5U) == 0U)
    {
        /* A cottage garden. */
        Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_MEADOW);
        Render_FillCircle(Target, (int16_t)(Left + 10), (int16_t)(Top + 10), 7U, TUG_COLOUR_TREE);
        Render_FillCircle(Target, (int16_t)(Left + 8), (int16_t)(Top + 8), 3U, TUG_COLOUR_TREE_LIGHT);
        Render_Box(Target, (int16_t)(Left + 2), (int16_t)(Top + 16), 3U, 3U, TUG_COLOUR_FLOWER);
        return;
    }
    Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, Dark);
    Render_Box(Target, (int16_t)(Left + 1), (int16_t)(Top + 1), TUG_TILE_SIZE - 2, TUG_TILE_SIZE - 2, Light);
    if(RidgeAcross)
    {
        Render_Box(Target, (int16_t)(Left + 1), (int16_t)(Top + 10), TUG_TILE_SIZE - 2, 9U, Dark);
    }
    else
    {
        Render_Box(Target, (int16_t)(Left + 10), (int16_t)(Top + 1), 9U, TUG_TILE_SIZE - 2, Dark);
    }
    if((Hash % 4U) == 1U)
    {
        Render_Box(Target, (int16_t)(Left + 3 + (int16_t)((Hash >> 6) % 10U)), (int16_t)(Top + 2), 4U, 4U, TUG_COLOUR_CHIMNEY);
    }
}

static void Tug_DrawLandTile(Render_TargetTypeDef *Target, char Tile, int16_t X, int16_t Y, int16_t Left, int16_t Top)
{
    const uint32_t Hash = Tug_TileHash(X, Y);
    switch(Tile)
    {
        case TUG_TILE_FIELD:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_FIELD);
            for(int16_t Row = 2; Row < TUG_TILE_SIZE; Row += 5)
            {
                if(Tug_RowsAcross(X, Y))
                {
                    Render_Box(Target, Left, (int16_t)(Top + Row), TUG_TILE_SIZE, 2U, TUG_COLOUR_FIELD_DARK);
                }
                else
                {
                    Render_Box(Target, (int16_t)(Left + Row), Top, 2U, TUG_TILE_SIZE, TUG_COLOUR_FIELD_DARK);
                }
            }
            break;

        case TUG_TILE_WHEAT:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_WHEAT);
            for(int16_t Row = 3; Row < TUG_TILE_SIZE; Row += 6)
            {
                if(Tug_RowsAcross(X, Y))
                {
                    Render_Box(Target, Left, (int16_t)(Top + Row), TUG_TILE_SIZE, 1U, TUG_COLOUR_WHEAT_DARK);
                }
                else
                {
                    Render_Box(Target, (int16_t)(Left + Row), Top, 1U, TUG_TILE_SIZE, TUG_COLOUR_WHEAT_DARK);
                }
            }
            break;

        case TUG_TILE_WOODS:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_TREE_DARK);
            Render_FillCircle(Target, (int16_t)(Left + 4 + (int16_t)(Hash % 12U)), (int16_t)(Top + 4 + (int16_t)((Hash >> 4) % 12U)), (uint16_t)(8U + (Hash % 4U)), TUG_COLOUR_TREE);
            Render_FillCircle(Target, (int16_t)(Left + 2 + (int16_t)(Hash % 12U)), (int16_t)(Top + 2 + (int16_t)((Hash >> 4) % 12U)), 3U, TUG_COLOUR_TREE_LIGHT);
            break;

        case TUG_TILE_HEDGE:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_MEADOW);
            Render_FillCircle(Target, (int16_t)(Left + 10), (int16_t)(Top + 10), 8U, TUG_COLOUR_HEDGE);
            Render_FillCircle(Target, (int16_t)(Left + 7), (int16_t)(Top + 7), 3U, TUG_COLOUR_HEDGE_LIGHT);
            if((Hash % 9U) == 0U)
            {
                Render_FillCircle(Target, (int16_t)(Left + 10), (int16_t)(Top + 10), 11U, TUG_COLOUR_TREE);
                Render_FillCircle(Target, (int16_t)(Left + 6), (int16_t)(Top + 6), 4U, TUG_COLOUR_TREE_LIGHT);
            }
            break;

        case TUG_TILE_TOWPATH:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_TOWPATH);
            Render_Box(Target, (int16_t)(Left + (int16_t)(Hash % 17U)), (int16_t)(Top + (int16_t)((Hash >> 5) % 17U)), 2U, 2U, TUG_COLOUR_TOWPATH_DARK);
            Render_Box(Target, (int16_t)(Left + (int16_t)((Hash >> 10) % 17U)), (int16_t)(Top + (int16_t)((Hash >> 15) % 17U)), 2U, 2U, TUG_COLOUR_TOWPATH_DARK);
            break;

        case TUG_TILE_LANE:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_LANE);
            if(!Tug_IsLaneAt(X, (int16_t)(Y - 1)) && !Tug_IsWaterAt(X, (int16_t)(Y - 1)))
            {
                Render_Box(Target, Left, Top, TUG_TILE_SIZE, 2U, TUG_COLOUR_VERGE);
            }
            if(!Tug_IsLaneAt(X, (int16_t)(Y + 1)) && !Tug_IsWaterAt(X, (int16_t)(Y + 1)))
            {
                Render_Box(Target, Left, (int16_t)(Top + TUG_TILE_SIZE - 2), TUG_TILE_SIZE, 2U, TUG_COLOUR_VERGE);
            }
            if(!Tug_IsLaneAt((int16_t)(X - 1), Y) && !Tug_IsWaterAt((int16_t)(X - 1), Y))
            {
                Render_Box(Target, Left, Top, 2U, TUG_TILE_SIZE, TUG_COLOUR_VERGE);
            }
            if(!Tug_IsLaneAt((int16_t)(X + 1), Y) && !Tug_IsWaterAt((int16_t)(X + 1), Y))
            {
                Render_Box(Target, (int16_t)(Left + TUG_TILE_SIZE - 2), Top, 2U, TUG_TILE_SIZE, TUG_COLOUR_VERGE);
            }
            break;

        case TUG_TILE_HOUSES:
            Tug_DrawCottage(Target, X, Y, Left, Top, Hash);
            break;

        case TUG_TILE_YARD:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_YARD);
            Render_Box(Target, (int16_t)(Left + (int16_t)(Hash % 17U)), (int16_t)(Top + (int16_t)((Hash >> 5) % 17U)), 3U, 3U, TUG_COLOUR_YARD_DARK);
            break;

        default:
            Render_Box(Target, Left, Top, TUG_TILE_SIZE, TUG_TILE_SIZE, TUG_COLOUR_MEADOW);
            Render_Box(Target, (int16_t)(Left + (int16_t)(Hash % 17U)), (int16_t)(Top + (int16_t)((Hash >> 5) % 17U)), 2U, 3U, TUG_COLOUR_MEADOW_DARK);
            Render_Box(Target, (int16_t)(Left + (int16_t)((Hash >> 10) % 17U)), (int16_t)(Top + (int16_t)((Hash >> 15) % 17U)), 2U, 2U, TUG_COLOUR_MEADOW_LIGHT);
            if((Hash % 23U) == 0U)
            {
                Render_Box(Target, (int16_t)(Left + 8), (int16_t)(Top + 8), 3U, 3U, ((Hash >> 3) & 1U) ? TUG_COLOUR_FLOWER : TUG_COLOUR_CAR_YELLOW);
            }
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* Sites                                                                      */
/* -------------------------------------------------------------------------- */

/* A building seen from above: a shadow, walls and a roof with a ridge. */
static void Tug_Building(Render_TargetTypeDef *Target, float X, float Y, int16_t Width, int16_t Height, uint8_t Roof, uint8_t Ridge)
{
    Tug_Box(Target, X + 4.0f, Y + 5.0f, Width, Height, TUG_COLOUR_SHADOW);
    Tug_Box(Target, X, Y, Width, Height, Ridge);
    Tug_Box(Target, X + 2.0f, Y + 2.0f, (int16_t)(Width - 4), (int16_t)(Height - 4), Roof);
    if(Width >= Height)
    {
        Tug_Box(Target, X + 2.0f, Y + (float)(Height / 2) - 1.0f, (int16_t)(Width - 4), 2, Ridge);
    }
    else
    {
        Tug_Box(Target, X + (float)(Width / 2) - 1.0f, Y + 2.0f, 2, (int16_t)(Height - 4), Ridge);
    }
}

static void Tug_ParkedCar(Render_TargetTypeDef *Target, float X, float Y, uint8_t Colour, bool Across)
{
    if(Across)
    {
        Tug_Box(Target, X + 1.0f, Y + 1.0f, 14, 8, TUG_COLOUR_SHADOW);
        Tug_Box(Target, X, Y, 14, 8, Colour);
        Tug_Box(Target, X + 9.0f, Y + 1.0f, 3, 6, TUG_COLOUR_GLASS);
    }
    else
    {
        Tug_Box(Target, X + 1.0f, Y + 1.0f, 8, 14, TUG_COLOUR_SHADOW);
        Tug_Box(Target, X, Y, 8, 14, Colour);
        Tug_Box(Target, X + 1.0f, Y + 2.0f, 6, 3, TUG_COLOUR_GLASS);
    }
}

static void Tug_Puff(Render_TargetTypeDef *Target, float X, float Y, uint32_t Phase)
{
    for(uint8_t Index = 0U; Index < 3U; Index++)
    {
        const uint32_t Age = (Phase + (Index * 700U)) % 2100U;
        Tug_Circle(Target, X + ((float)Age * 0.02f), Y - ((float)Age * 0.025f), (uint16_t)(4U + (Age / 300U)), TUG_COLOUR_SMOKE);
    }
}

static void Tug_DrawSite(Render_TargetTypeDef *Target, const Tug_DockTypeDef *Dock)
{
    static const uint8_t CarColours[5] = { TUG_COLOUR_CAR_RED, TUG_COLOUR_CAR_BLUE, TUG_COLOUR_CAR_YELLOW, TUG_COLOUR_CAR_WHITE, TUG_COLOUR_CAR_GREEN };
    const float X = (float)Dock->SiteX;
    const float Y = (float)Dock->SiteY;

    switch(Dock->Look)
    {
        case TUG_SITE_BOATYARD:
            Tug_Building(Target, X - 70.0f, Y - 50.0f, 80, 60, TUG_COLOUR_SHED, TUG_COLOUR_SHED_DARK);
            Tug_Box(Target, X + 20.0f, Y - 30.0f, 50, 70, TUG_COLOUR_WOOD);
            for(int16_t Plank = 0; Plank < 70; Plank += 7)
            {
                Tug_Box(Target, X + 20.0f, Y - 30.0f + (float)Plank, 50, 1, TUG_COLOUR_WOOD_DARK);
            }
            Tug_Box(Target, X + 32.0f, Y - 20.0f, 26, 50, TUG_COLOUR_TUG_RED);
            Tug_Box(Target, X + 36.0f, Y - 8.0f, 18, 18, TUG_COLOUR_TUG_CABIN);
            Tug_Line(Target, X - 60.0f, Y + 30.0f, X + 10.0f, Y + 30.0f, 3U, TUG_COLOUR_HAZARD);
            break;

        case TUG_SITE_LUMBER:
            Tug_Building(Target, X - 80.0f, Y - 50.0f, 60, 44, TUG_COLOUR_WOOD, TUG_COLOUR_WOOD_DARK);
            for(int16_t Row = 0; Row < 3; Row++)
            {
                for(int16_t Log = 0; Log < 6; Log++)
                {
                    const float LogY = Y - 40.0f + (float)(Row * 30) + (float)(Log * 4);
                    Tug_Box(Target, X - 5.0f, LogY, 80, 4, TUG_COLOUR_LOG);
                    Tug_Circle(Target, X - 5.0f, LogY + 2.0f, 2U, TUG_COLOUR_LOG_END);
                }
            }
            break;

        case TUG_SITE_QUARRY:
            Tug_Circle(Target, X, Y, 64U, TUG_COLOUR_STONE_DARK);
            Tug_Circle(Target, X, Y, 50U, TUG_COLOUR_STONE);
            Tug_Circle(Target, X, Y, 34U, TUG_COLOUR_STONE_DARK);
            Tug_Circle(Target, X, Y, 20U, TUG_COLOUR_SHALLOWS_DARK);
            Tug_Circle(Target, X + 70.0f, Y + 40.0f, 14U, TUG_COLOUR_GRAVEL);
            Tug_Circle(Target, X - 72.0f, Y + 38.0f, 12U, TUG_COLOUR_GRAVEL);
            break;

        case TUG_SITE_TOWN:
            Tug_Building(Target, X - 60.0f, Y - 40.0f, 60, 34, TUG_COLOUR_ROOF_RED, TUG_COLOUR_ROOF_RED_DARK);
            Tug_Building(Target, X + 10.0f, Y - 46.0f, 46, 46, TUG_COLOUR_STONE, TUG_COLOUR_STONE_DARK);
            Tug_Circle(Target, X + 33.0f, Y - 23.0f, 8U, TUG_COLOUR_WHITE);
            Tug_Line(Target, X + 33.0f, Y - 23.0f, X + 33.0f, Y - 29.0f, 2U, TUG_COLOUR_BLACK);
            Tug_Building(Target, X - 50.0f, Y + 10.0f, 34, 40, TUG_COLOUR_ROOF_SLATE, TUG_COLOUR_ROOF_SLATE_DARK);
            Tug_Building(Target, X + 0.0f, Y + 14.0f, 50, 30, TUG_COLOUR_THATCH, TUG_COLOUR_THATCH_DARK);
            break;

        case TUG_SITE_STEEL:
            Tug_Building(Target, X - 90.0f, Y - 40.0f, 110, 50, TUG_COLOUR_SHED_DARK, TUG_COLOUR_TUG_CHARCOAL);
            Tug_Building(Target, X + 30.0f, Y - 30.0f, 60, 70, TUG_COLOUR_RUST, TUG_COLOUR_BRICK);
            for(int16_t Stack = 0; Stack < 3; Stack++)
            {
                const float StackX = X - 70.0f + (float)(Stack * 34);
                Tug_Circle(Target, StackX, Y + 30.0f, 8U, TUG_COLOUR_BRICK);
                Tug_Circle(Target, StackX, Y + 30.0f, 4U, TUG_COLOUR_BLACK);
                Tug_Puff(Target, StackX, Y + 20.0f, Tug_Clock + ((uint32_t)Stack * 500U));
            }
            Tug_Box(Target, X + 40.0f, Y - 10.0f, 30, 6, TUG_COLOUR_TANK_ORANGE);
            break;

        case TUG_SITE_CAR_FACTORY:
            Tug_Building(Target, X - 90.0f, Y - 50.0f, 120, 60, TUG_COLOUR_CAR_WHITE, TUG_COLOUR_SHED);
            Tug_Box(Target, X - 88.0f, Y - 22.0f, 116, 4, TUG_COLOUR_CAR_BLUE);
            for(int16_t Car = 0; Car < 8; Car++)
            {
                Tug_ParkedCar(Target, X - 80.0f + (float)(Car * 16), Y + 20.0f, CarColours[Car % 5], false);
            }
            break;

        case TUG_SITE_BREWERY:
            Tug_Building(Target, X - 80.0f, Y - 50.0f, 80, 56, TUG_COLOUR_BRICK, TUG_COLOUR_ROOF_RED_DARK);
            for(int16_t Vat = 0; Vat < 3; Vat++)
            {
                Tug_Circle(Target, X + 22.0f + (float)(Vat * 26), Y - 24.0f, 12U, TUG_COLOUR_COPPER);
                Tug_Circle(Target, X + 18.0f + (float)(Vat * 26), Y - 28.0f, 4U, TUG_COLOUR_CAR_YELLOW);
            }
            for(int16_t Barrel = 0; Barrel < 6; Barrel++)
            {
                Tug_Circle(Target, X - 60.0f + (float)(Barrel * 14), Y + 26.0f, 6U, TUG_COLOUR_WOOD);
                Tug_Circle(Target, X - 60.0f + (float)(Barrel * 14), Y + 26.0f, 2U, TUG_COLOUR_WOOD_DARK);
            }
            break;

        case TUG_SITE_DEALER:
            Tug_Box(Target, X - 80.0f, Y - 50.0f, 70, 46, TUG_COLOUR_GLASS);
            Tug_Box(Target, X - 80.0f, Y - 50.0f, 70, 6, TUG_COLOUR_CAR_RED);
            for(int16_t Row = 0; Row < 3; Row++)
            {
                for(int16_t Car = 0; Car < 5; Car++)
                {
                    Tug_ParkedCar(Target, X + 0.0f + (float)(Car * 16), Y - 50.0f + (float)(Row * 24), CarColours[(Row + Car) % 5], false);
                }
            }
            for(int16_t Flag = 0; Flag < 4; Flag++)
            {
                Tug_Line(Target, X - 80.0f + (float)(Flag * 22), Y + 20.0f, X - 80.0f + (float)(Flag * 22), Y + 6.0f, 2U, TUG_COLOUR_WHITE);
                Tug_Box(Target, X - 79.0f + (float)(Flag * 22), Y + 6.0f, 8, 5, CarColours[Flag]);
            }
            break;

        case TUG_SITE_SPACE:
            Tug_Building(Target, X - 90.0f, Y - 60.0f, 60, 80, TUG_COLOUR_CAR_WHITE, TUG_COLOUR_SHED);
            Tug_Box(Target, X - 80.0f, Y - 50.0f, 40, 8, TUG_COLOUR_CAR_BLUE);
            Tug_Circle(Target, X + 40.0f, Y, 40U, TUG_COLOUR_YARD_DARK);
            Tug_Box(Target, X + 60.0f, Y - 40.0f, 10, 80, TUG_COLOUR_RUST);
            Tug_Circle(Target, X + 40.0f, Y, 12U, TUG_COLOUR_WHITE);
            Tug_Circle(Target, X + 40.0f, Y, 6U, TUG_COLOUR_TANK_ORANGE);
            Tug_Circle(Target, X + 28.0f, Y, 5U, TUG_COLOUR_WHITE);
            Tug_Circle(Target, X + 52.0f, Y, 5U, TUG_COLOUR_WHITE);
            break;

        case TUG_SITE_FARM:
            Tug_Building(Target, X - 70.0f, Y - 50.0f, 60, 40, TUG_COLOUR_CAR_RED, TUG_COLOUR_ROOF_RED_DARK);
            Tug_Circle(Target, X + 20.0f, Y - 40.0f, 13U, TUG_COLOUR_STEEL);
            Tug_Circle(Target, X + 20.0f, Y - 40.0f, 5U, TUG_COLOUR_SHED_DARK);
            Tug_Circle(Target, X + 50.0f, Y - 40.0f, 13U, TUG_COLOUR_STEEL);
            Tug_Circle(Target, X + 50.0f, Y - 40.0f, 5U, TUG_COLOUR_SHED_DARK);
            for(int16_t Bale = 0; Bale < 5; Bale++)
            {
                Tug_Circle(Target, X - 60.0f + (float)(Bale * 18), Y + 20.0f, 7U, TUG_COLOUR_HAY);
            }
            Tug_Box(Target, X + 30.0f, Y + 10.0f, 18, 12, TUG_COLOUR_CAR_GREEN);
            Tug_Circle(Target, X + 32.0f, Y + 22.0f, 4U, TUG_COLOUR_BLACK);
            break;

        case TUG_SITE_PCB:
            Tug_Building(Target, X - 80.0f, Y - 50.0f, 110, 70, TUG_COLOUR_PCB, TUG_COLOUR_TUG_CHARCOAL);
            for(int16_t Trace = 0; Trace < 6; Trace++)
            {
                Tug_Box(Target, X - 70.0f + (float)(Trace * 16), Y - 40.0f, 2, 50, TUG_COLOUR_PCB_GOLD);
                Tug_Box(Target, X - 72.0f + (float)(Trace * 16), Y + 6.0f, 6, 6, TUG_COLOUR_PCB_GOLD);
            }
            Tug_Box(Target, X - 40.0f, Y - 30.0f, 30, 30, TUG_COLOUR_BLACK);
            Tug_Box(Target, X + 40.0f, Y - 30.0f, 40, 60, TUG_COLOUR_YARD_DARK);
            break;

        case TUG_SITE_USOLDER:
            /* A tidy lawn and a path up to the door. */
            Tug_Box(Target, X - 96.0f, Y - 64.0f, 192, 128, TUG_COLOUR_MEADOW);
            Tug_Box(Target, X - 6.0f, Y + 22.0f, 12, 42, TUG_COLOUR_STONE);
            /* The main building: white with an orange band and a dark roof carrying the name and solar panels. */
            Tug_Box(Target, X - 86.0f + 5.0f, Y - 56.0f + 6.0f, 172, 78, TUG_COLOUR_SHADOW);
            Tug_Box(Target, X - 86.0f, Y - 56.0f, 172, 78, TUG_COLOUR_CAR_WHITE);
            Tug_Box(Target, X - 82.0f, Y - 52.0f, 164, 70, TUG_COLOUR_TUG_CHARCOAL);
            Tug_Box(Target, X - 86.0f, Y + 16.0f, 172, 6, TUG_COLOUR_TUG_ORANGE);
            for(int16_t Panel = 0; Panel < 5; Panel++)
            {
                Tug_Box(Target, X + 10.0f + (float)(Panel * 14), Y - 46.0f, 12, 22, TUG_COLOUR_TUG_NAVY);
                Tug_Box(Target, X + 10.0f + (float)(Panel * 14), Y - 36.0f, 12, 1, TUG_COLOUR_GLASS);
            }
            Render_FillRoundRect(Target, (int16_t)(lrintf(X) - 78 + Tug_OffsetX), (int16_t)(lrintf(Y) - 22 + Tug_OffsetY), 86U, 32U, 5U, TUG_COLOUR_TUG_ORANGE);
            Render_DrawTextAligned(Target, &OpenSansBold16, "uSOLDER", (int16_t)(lrintf(X) - 35 + Tug_OffsetX), (int16_t)(lrintf(Y) - 16 + Tug_OffsetY), RENDER_ALIGN_CENTRE, TUG_COLOUR_WHITE);
            Tug_Box(Target, X - 60.0f, Y - 46.0f, 30, 18, TUG_COLOUR_SHED_DARK);
            Tug_Circle(Target, X - 45.0f, Y - 37.0f, 5U, TUG_COLOUR_STEEL);
            /* The loading bay and the company vans. */
            Tug_Box(Target, X + 40.0f, Y + 22.0f, 46, 34, TUG_COLOUR_YARD);
            for(int16_t Van = 0; Van < 2; Van++)
            {
                const float VanX = X + 46.0f + (float)(Van * 20);
                Tug_Box(Target, VanX + 1.0f, Y + 27.0f, 14, 24, TUG_COLOUR_SHADOW);
                Tug_Box(Target, VanX, Y + 26.0f, 14, 24, TUG_COLOUR_CAR_WHITE);
                Tug_Box(Target, VanX, Y + 36.0f, 14, 3, TUG_COLOUR_TUG_ORANGE);
                Tug_Box(Target, VanX + 2.0f, Y + 27.0f, 10, 4, TUG_COLOUR_GLASS);
            }
            /* A few trees and a flowerbed. */
            for(int16_t Tree = 0; Tree < 3; Tree++)
            {
                Tug_Circle(Target, X - 78.0f + (float)(Tree * 22), Y + 46.0f, 8U, TUG_COLOUR_TREE);
                Tug_Circle(Target, X - 80.0f + (float)(Tree * 22), Y + 44.0f, 3U, TUG_COLOUR_TREE_LIGHT);
            }
            Tug_Box(Target, X - 26.0f, Y + 30.0f, 16, 6, TUG_COLOUR_FLOWER);
            Tug_Box(Target, X + 12.0f, Y + 30.0f, 16, 6, TUG_COLOUR_TUG_ORANGE);
            break;

        case TUG_SITE_MUSEUM:
            Tug_Building(Target, X - 70.0f, Y - 50.0f, 100, 56, TUG_COLOUR_STONE, TUG_COLOUR_STONE_DARK);
            for(int16_t Column = 0; Column < 7; Column++)
            {
                Tug_Box(Target, X - 64.0f + (float)(Column * 14), Y + 8.0f, 6, 14, TUG_COLOUR_WHITE);
            }
            Tug_Box(Target, X + 40.0f, Y - 30.0f, 40, 16, TUG_COLOUR_NARROWBOAT_A);
            Tug_Box(Target, X + 46.0f, Y - 27.0f, 24, 10, TUG_COLOUR_NARROWBOAT_D);
            break;

        case TUG_SITE_COLLIERY:
            Tug_Building(Target, X - 80.0f, Y - 40.0f, 60, 40, TUG_COLOUR_BRICK, TUG_COLOUR_ROOF_RED_DARK);
            Tug_Building(Target, X - 6.0f, Y - 46.0f, 34, 30, TUG_COLOUR_SHED_DARK, TUG_COLOUR_TUG_CHARCOAL);
            Tug_Line(Target, X + 20.0f, Y - 20.0f, X + 56.0f, Y + 8.0f, 4U, TUG_COLOUR_RUST);
            Tug_Circle(Target, X + 60.0f, Y + 20.0f, 24U, TUG_COLOUR_COAL);
            Tug_Circle(Target, X - 30.0f, Y + 30.0f, 18U, TUG_COLOUR_COAL);
            break;

        case TUG_SITE_POWER:
            for(int16_t Tower = 0; Tower < 2; Tower++)
            {
                const float TowerX = X - 50.0f + (float)(Tower * 70);
                Tug_Circle(Target, TowerX + 4.0f, Y - 15.0f, 32U, TUG_COLOUR_SHADOW);
                Tug_Circle(Target, TowerX, Y - 20.0f, 32U, TUG_COLOUR_STONE);
                Tug_Circle(Target, TowerX, Y - 20.0f, 22U, TUG_COLOUR_STONE_DARK);
                Tug_Puff(Target, TowerX, Y - 30.0f, Tug_Clock + ((uint32_t)Tower * 900U));
            }
            Tug_Building(Target, X - 70.0f, Y + 30.0f, 140, 30, TUG_COLOUR_BRICK, TUG_COLOUR_ROOF_RED_DARK);
            break;

        case TUG_SITE_CHEMICAL:
            for(int16_t Tank = 0; Tank < 4; Tank++)
            {
                Tug_Circle(Target, X - 60.0f + (float)(Tank * 36), Y - 30.0f, 15U, TUG_COLOUR_CAR_WHITE);
                Tug_Circle(Target, X - 60.0f + (float)(Tank * 36), Y - 30.0f, 4U, TUG_COLOUR_STEEL);
            }
            Tug_Line(Target, X - 75.0f, Y, X + 75.0f, Y, 3U, TUG_COLOUR_STEEL);
            Tug_Line(Target, X - 75.0f, Y + 8.0f, X + 75.0f, Y + 8.0f, 3U, TUG_COLOUR_TANK_ORANGE);
            Tug_Building(Target, X - 40.0f, Y + 20.0f, 80, 36, TUG_COLOUR_SHED, TUG_COLOUR_SHED_DARK);
            Tug_Circle(Target, X + 70.0f, Y + 40.0f, (uint16_t)(4U + ((Tug_Clock / 120U) % 3U)), TUG_COLOUR_TANK_ORANGE);
            break;

        case TUG_SITE_NUCLEAR:
            Tug_Box(Target, X - 90.0f, Y - 60.0f, 180, 2, TUG_COLOUR_HAZARD);
            Tug_Box(Target, X - 90.0f, Y + 58.0f, 180, 2, TUG_COLOUR_HAZARD);
            Tug_Circle(Target, X - 30.0f, Y - 10.0f, 30U, TUG_COLOUR_STONE);
            Tug_Circle(Target, X - 30.0f, Y - 10.0f, 22U, TUG_COLOUR_CAR_WHITE);
            Tug_Building(Target, X + 20.0f, Y - 40.0f, 60, 70, TUG_COLOUR_SHED, TUG_COLOUR_SHED_DARK);
            Tug_Circle(Target, X + 50.0f, Y - 5.0f, 14U, TUG_COLOUR_NUCLEAR);
            Tug_Circle(Target, X + 50.0f, Y - 5.0f, 4U, TUG_COLOUR_BLACK);
            break;

        case TUG_SITE_MARINA:
            Tug_Building(Target, X - 80.0f, Y - 50.0f, 60, 40, TUG_COLOUR_CAR_WHITE, TUG_COLOUR_ROOF_SLATE);
            for(int16_t Pontoon = 0; Pontoon < 4; Pontoon++)
            {
                Tug_Box(Target, X - 10.0f + (float)(Pontoon * 26), Y - 50.0f, 6, 90, TUG_COLOUR_WOOD);
                Tug_Box(Target, X - 2.0f + (float)(Pontoon * 26), Y - 40.0f + (float)((Pontoon % 2) * 30), 14, 28, TUG_COLOUR_YACHT);
            }
            break;

        case TUG_SITE_ROCKET:
            Tug_Building(Target, X - 90.0f, Y - 50.0f, 90, 70, TUG_COLOUR_SHED, TUG_COLOUR_SHED_DARK);
            Tug_Box(Target, X + 20.0f, Y - 40.0f, 60, 60, TUG_COLOUR_TUG_CHARCOAL);
            for(int16_t Bell = 0; Bell < 3; Bell++)
            {
                Tug_Circle(Target, X + 34.0f + (float)(Bell * 16), Y - 10.0f, 7U, TUG_COLOUR_STEEL);
                Tug_Circle(Target, X + 34.0f + (float)(Bell * 16), Y - 10.0f, 3U, TUG_COLOUR_BLACK);
            }
            break;

        case TUG_SITE_WINDFARM:
            for(int16_t Turbine = 0; Turbine < 3; Turbine++)
            {
                const float TurbineX = X - 60.0f + (float)(Turbine * 60);
                const float TurbineY = Y - 10.0f + (float)((Turbine % 2) * 30);
                const float Angle = ((float)Tug_Clock * 0.002f) + (float)Turbine;
                for(int16_t Blade = 0; Blade < 3; Blade++)
                {
                    const float BladeAngle = Angle + ((float)Blade * 2.0944f);
                    Tug_Line(Target, TurbineX, TurbineY, TurbineX + (28.0f * sinf(BladeAngle)), TurbineY - (28.0f * cosf(BladeAngle)), 3U, TUG_COLOUR_WHITE);
                }
                Tug_Circle(Target, TurbineX, TurbineY, 4U, TUG_COLOUR_STEEL);
            }
            break;

        case TUG_SITE_CASTLE:
        default:
            Tug_Box(Target, X - 70.0f, Y - 60.0f, 140, 110, TUG_COLOUR_STONE_DARK);
            Tug_Box(Target, X - 60.0f, Y - 50.0f, 120, 90, TUG_COLOUR_MEADOW);
            Tug_Building(Target, X - 25.0f, Y - 30.0f, 50, 50, TUG_COLOUR_STONE, TUG_COLOUR_STONE_DARK);
            Tug_Circle(Target, X - 70.0f, Y - 60.0f, 14U, TUG_COLOUR_STONE);
            Tug_Circle(Target, X + 70.0f, Y - 60.0f, 14U, TUG_COLOUR_STONE);
            Tug_Circle(Target, X - 70.0f, Y + 50.0f, 14U, TUG_COLOUR_STONE);
            Tug_Circle(Target, X + 70.0f, Y + 50.0f, 14U, TUG_COLOUR_STONE);
            Tug_Line(Target, X, Y - 5.0f, X, Y - 30.0f, 2U, TUG_COLOUR_BLACK);
            Tug_Box(Target, X + 1.0f, Y - 30.0f, 14, 9, TUG_COLOUR_CAR_RED);
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* Boats                                                                      */
/* -------------------------------------------------------------------------- */

static void Tug_DrawTugBoat(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, uint8_t Colour)
{
    const float L = Body->Length / 2.0f;
    const float W = Body->Width / 2.0f;
    const float Hull[10][2] =
    {
        { L, 0.0f }, { L - 4.0f, -W + 3.0f }, { L - 12.0f, -W }, { -L + 5.0f, -W }, { -L, -W + 5.0f },
        { -L, W - 5.0f }, { -L + 5.0f, W }, { L - 12.0f, W }, { L - 4.0f, W - 3.0f }, { L, 0.0f }
    };
    const float Deck[10][2] =
    {
        { L - 3.0f, 0.0f }, { L - 6.0f, -W + 5.0f }, { L - 13.0f, -W + 3.0f }, { -L + 6.0f, -W + 3.0f }, { -L + 3.0f, -W + 6.0f },
        { -L + 3.0f, W - 6.0f }, { -L + 6.0f, W - 3.0f }, { L - 13.0f, W - 3.0f }, { L - 6.0f, W - 5.0f }, { L - 3.0f, 0.0f }
    };
    Tug_BodyTypeDef Shadow = *Body;

    Shadow.X += 4.0f;
    Shadow.Y += 5.0f;
    Tug_DrawShape(Target, &Shadow, Hull, 9U, TUG_COLOUR_SHADOW);
    Tug_DrawShape(Target, Body, Hull, 9U, TUG_COLOUR_FENDER);
    Tug_DrawShape(Target, Body, Deck, 9U, Colour);

    /* Tyre fenders along the sides. */
    for(float Along = -L + 8.0f; Along < (L - 12.0f); Along += 10.0f)
    {
        Tug_BodyCircle(Target, Body, Along, -W, 2U, TUG_COLOUR_BLACK);
        Tug_BodyCircle(Target, Body, Along, W, 2U, TUG_COLOUR_BLACK);
    }
    Tug_DrawPlate(Target, Body, -L + 6.0f, L - 14.0f, -W + 5.0f, W - 5.0f, TUG_COLOUR_TUG_DECK);
    Tug_DrawPlate(Target, Body, -L * 0.15f, L - 14.0f, -W + 6.0f, W - 6.0f, TUG_COLOUR_TUG_CABIN);
    Tug_DrawPlate(Target, Body, L - 19.0f, L - 14.0f, -W + 7.0f, W - 7.0f, TUG_COLOUR_GLASS);
    Tug_DrawPlate(Target, Body, -L * 0.15f, -L * 0.15f + 3.0f, -W + 6.0f, W - 6.0f, Colour);
    Tug_BodyCircle(Target, Body, -L * 0.15f - 7.0f, 0.0f, 5U, TUG_COLOUR_BLACK);
    Tug_BodyCircle(Target, Body, -L * 0.15f - 7.0f, 0.0f, 3U, Colour);
    Tug_BodyCircle(Target, Body, -L + 9.0f, -W + 8.0f, 3U, TUG_COLOUR_TANK_ORANGE);
    Tug_BodyCircle(Target, Body, -L + 9.0f, -W + 8.0f, 1U, TUG_COLOUR_TUG_DECK);
    Tug_BodyCircle(Target, Body, -L + 3.0f, 0.0f, 3U, TUG_COLOUR_ROPE);
}

/* A narrowboat tied up on the bank: long, painted, with a roof and a chimney. */
static void Tug_DrawNarrowboat(Render_TargetTypeDef *Target, const Tug_MooredBoatTypeDef *Moored)
{
    static const uint8_t Paints[4] = { TUG_COLOUR_NARROWBOAT_A, TUG_COLOUR_NARROWBOAT_B, TUG_COLOUR_NARROWBOAT_C, TUG_COLOUR_NARROWBOAT_D };
    const float L = TUG_MOORED_LENGTH / 2.0f;
    const float W = TUG_MOORED_WIDTH / 2.0f;
    const float Hull[6][2] = { { L, 0.0f }, { L - 10.0f, -W }, { -L + 2.0f, -W }, { -L, -W + 3.0f }, { -L, W - 3.0f }, { L - 10.0f, W } };
    Tug_BodyTypeDef Body = { (float)Moored->X + 3.0f, (float)Moored->Y + 4.0f, 0.0f, 0.0f, (float)Moored->Heading / 1000.0f, 0.0f, TUG_MOORED_LENGTH, TUG_MOORED_WIDTH };

    Tug_DrawShape(Target, &Body, Hull, 6U, TUG_COLOUR_SHADOW);
    Body.X -= 3.0f;
    Body.Y -= 4.0f;
    Tug_DrawShape(Target, &Body, Hull, 6U, TUG_COLOUR_BLACK);
    Tug_DrawPlate(Target, &Body, -L + 6.0f, L - 12.0f, -W + 2.0f, W - 2.0f, Paints[Moored->Paint % 4U]);
    Tug_DrawPlate(Target, &Body, -L + 8.0f, L - 14.0f, -W + 5.0f, W - 5.0f, TUG_COLOUR_TUG_CABIN);
    Tug_BodyCircle(Target, &Body, L - 22.0f, 0.0f, 2U, TUG_COLOUR_BLACK);
    Tug_BodyCircle(Target, &Body, -L + 16.0f, -2.0f, 2U, TUG_COLOUR_FLOWER);
    Tug_BodyCircle(Target, &Body, -L + 20.0f, 2.0f, 2U, TUG_COLOUR_CAR_YELLOW);
}

/* A barge's hull: square-ended with the bow corners cut. */
static void Tug_DrawBargeHull(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, uint8_t Edge, uint8_t Deck)
{
    const float L = Body->Length / 2.0f;
    const float W = Body->Width / 2.0f;
    const float Hull[6][2] = { { L, -W + 6.0f }, { L, W - 6.0f }, { L - 8.0f, W }, { -L, W }, { -L, -W }, { L - 8.0f, -W } };
    const float Inner[6][2] = { { L - 3.0f, -W + 7.0f }, { L - 3.0f, W - 7.0f }, { L - 9.0f, W - 3.0f }, { -L + 3.0f, W - 3.0f }, { -L + 3.0f, -W + 3.0f }, { L - 9.0f, -W + 3.0f } };
    Tug_DrawShape(Target, Body, Hull, 6U, Edge);
    Tug_DrawShape(Target, Body, Inner, 6U, Deck);
    Tug_BodyCircle(Target, Body, L - 5.0f, -W + 9.0f, 2U, TUG_COLOUR_BLACK);
    Tug_BodyCircle(Target, Body, L - 5.0f, W - 9.0f, 2U, TUG_COLOUR_BLACK);
}

/* A pointed hull for yachts, barges of state and old ships. */
static void Tug_DrawShipHull(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, uint8_t Edge, uint8_t Deck)
{
    const float L = Body->Length / 2.0f;
    const float W = Body->Width / 2.0f;
    const float Hull[7][2] = { { L, 0.0f }, { L - 22.0f, -W }, { -L + 6.0f, -W }, { -L, -W + 6.0f }, { -L, W - 6.0f }, { -L + 6.0f, W }, { L - 22.0f, W } };
    const float Inner[7][2] = { { L - 4.0f, 0.0f }, { L - 23.0f, -W + 3.0f }, { -L + 6.0f, -W + 3.0f }, { -L + 3.0f, -W + 6.0f }, { -L + 3.0f, W - 6.0f }, { -L + 6.0f, W - 3.0f }, { L - 23.0f, W - 3.0f } };
    Tug_DrawShape(Target, Body, Hull, 7U, Edge);
    Tug_DrawShape(Target, Body, Inner, 7U, Deck);
}

static void Tug_DrawCargoBody(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, Tug_LookTypeDef Look, bool Flash)
{
    static const uint8_t CarColours[5] = { TUG_COLOUR_CAR_RED, TUG_COLOUR_CAR_BLUE, TUG_COLOUR_CAR_YELLOW, TUG_COLOUR_CAR_WHITE, TUG_COLOUR_CAR_GREEN };
    const float L = Body->Length / 2.0f;
    const float W = Body->Width / 2.0f;
    const uint8_t Edge = Flash ? TUG_COLOUR_WHITE : TUG_COLOUR_BARGE_DARK;
    Tug_BodyTypeDef Shadow = *Body;

    Shadow.X += 4.0f;
    Shadow.Y += 5.0f;
    Tug_DrawPlate(Target, &Shadow, -L, L, -W, W, TUG_COLOUR_SHADOW);

    switch(Look)
    {
        case TUG_LOOK_LOGS:
            for(int8_t Log = 0; Log < 5; Log++)
            {
                const float Across = -W + 2.0f + ((float)Log * ((Body->Width - 4.0f) / 5.0f));
                const float Width = ((Body->Width - 4.0f) / 5.0f) - 1.0f;
                Tug_DrawPlate(Target, Body, -L + (float)((Log % 2) * 4), L - (float)(((Log + 1) % 2) * 4), Across, Across + Width, Flash ? TUG_COLOUR_WHITE : TUG_COLOUR_LOG);
                Tug_BodyCircle(Target, Body, L - (float)(((Log + 1) % 2) * 4), Across + (Width / 2.0f), 3U, TUG_COLOUR_LOG_END);
            }
            Tug_DrawPlate(Target, Body, L / 2.0f - 2.0f, L / 2.0f + 2.0f, -W, W, TUG_COLOUR_ROPE);
            Tug_DrawPlate(Target, Body, -L / 2.0f - 2.0f, -L / 2.0f + 2.0f, -W, W, TUG_COLOUR_ROPE);
            break;

        case TUG_LOOK_GRAVEL:
        case TUG_LOOK_COAL:
        {
            const uint8_t Pile = (Look == TUG_LOOK_GRAVEL) ? TUG_COLOUR_GRAVEL : TUG_COLOUR_COAL;
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, Body, -L + 8.0f, L - 12.0f, -W + 6.0f, W - 6.0f, Pile);
            Tug_DrawPlate(Target, Body, -L + 18.0f, L - 22.0f, -W + 11.0f, W - 11.0f, (Look == TUG_LOOK_GRAVEL) ? TUG_COLOUR_STONE : TUG_COLOUR_TUG_CHARCOAL);
            for(int8_t Lump = 0; Lump < 6; Lump++)
            {
                Tug_BodyCircle(Target, Body, -L + 14.0f + (float)(Lump * 12), (float)((Lump % 3) - 1) * 8.0f, 2U, (Look == TUG_LOOK_GRAVEL) ? TUG_COLOUR_STONE_DARK : TUG_COLOUR_STEEL);
            }
            break;
        }

        case TUG_LOOK_HAY:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 10.0f; Along < (L - 12.0f); Along += 16.0f)
            {
                Tug_BodyCircle(Target, Body, Along, -W / 2.0f, 7U, TUG_COLOUR_HAY);
                Tug_BodyCircle(Target, Body, Along, W / 2.0f, 7U, TUG_COLOUR_HAY);
                Tug_BodyCircle(Target, Body, Along, -W / 2.0f, 3U, TUG_COLOUR_WHEAT_DARK);
                Tug_BodyCircle(Target, Body, Along, W / 2.0f, 3U, TUG_COLOUR_WHEAT_DARK);
            }
            break;

        case TUG_LOOK_SCRAP:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, Body, -L + 8.0f, L - 14.0f, -W + 6.0f, W - 6.0f, TUG_COLOUR_RUST);
            Tug_DrawPlate(Target, Body, -L + 14.0f, -L + 30.0f, -W + 8.0f, -2.0f, TUG_COLOUR_STEEL);
            Tug_DrawPlate(Target, Body, 0.0f, 18.0f, 2.0f, W - 8.0f, TUG_COLOUR_CAR_BLUE);
            Tug_BodyCircle(Target, Body, -10.0f, 6.0f, 5U, TUG_COLOUR_BLACK);
            break;

        case TUG_LOOK_COMPONENTS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 6.0f; (Along + 14.0f) < (L - 10.0f); Along += 16.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 14.0f, -W + 5.0f, -1.0f, TUG_COLOUR_PCB);
                Tug_DrawPlate(Target, Body, Along, Along + 14.0f, 1.0f, W - 5.0f, TUG_COLOUR_PCB);
                Tug_DrawPlate(Target, Body, Along + 4.0f, Along + 10.0f, -W + 8.0f, -4.0f, TUG_COLOUR_BLACK);
                Tug_DrawPlate(Target, Body, Along + 4.0f, Along + 10.0f, 4.0f, W - 8.0f, TUG_COLOUR_PCB_GOLD);
            }
            break;

        case TUG_LOOK_PRODUCE:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 6.0f; (Along + 12.0f) < (L - 10.0f); Along += 14.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 12.0f, -W + 4.0f, -1.0f, TUG_COLOUR_WOOD);
                Tug_DrawPlate(Target, Body, Along, Along + 12.0f, 1.0f, W - 4.0f, TUG_COLOUR_WOOD);
                Tug_BodyCircle(Target, Body, Along + 6.0f, -W / 2.0f, 3U, TUG_COLOUR_CAR_RED);
                Tug_BodyCircle(Target, Body, Along + 6.0f, W / 2.0f, 3U, TUG_COLOUR_CAR_GREEN);
            }
            break;

        case TUG_LOOK_BARRELS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 9.0f; Along < (L - 12.0f); Along += 12.0f)
            {
                for(int8_t Row = -1; Row <= 1; Row++)
                {
                    Tug_BodyCircle(Target, Body, Along, (float)Row * 11.0f, 5U, TUG_COLOUR_WOOD);
                    Tug_BodyCircle(Target, Body, Along, (float)Row * 11.0f, 2U, TUG_COLOUR_WOOD_DARK);
                }
            }
            break;

        case TUG_LOOK_STEEL:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(int8_t Beam = 0; Beam < 4; Beam++)
            {
                const float Across = -W + 6.0f + ((float)Beam * ((Body->Width - 12.0f) / 4.0f));
                Tug_DrawPlate(Target, Body, -L + 6.0f, L - 10.0f, Across, Across + 5.0f, TUG_COLOUR_STEEL);
                Tug_DrawPlate(Target, Body, -L + 6.0f, L - 10.0f, Across + 2.0f, Across + 3.0f, TUG_COLOUR_TUG_CHARCOAL);
            }
            break;

        case TUG_LOOK_CARS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(int8_t Car = 0; Car < 10; Car++)
            {
                const float Along = -L + 8.0f + (float)((Car / 2) * 20);
                const float Across = ((Car % 2) == 0) ? (-W + 5.0f) : 2.0f;
                if((Along + 16.0f) < (L - 8.0f))
                {
                    Tug_DrawPlate(Target, Body, Along, Along + 16.0f, Across, Across + 13.0f, CarColours[Car % 5]);
                    Tug_DrawPlate(Target, Body, Along + 10.0f, Along + 13.0f, Across + 2.0f, Across + 11.0f, TUG_COLOUR_GLASS);
                }
            }
            break;

        case TUG_LOOK_CHEMICALS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE_DARK);
            for(float Along = -L + 16.0f; Along < (L - 14.0f); Along += 26.0f)
            {
                Tug_BodyCircle(Target, Body, Along, 0.0f, (uint16_t)(W - 5.0f), TUG_COLOUR_CAR_WHITE);
                Tug_BodyCircle(Target, Body, Along, 0.0f, 5U, TUG_COLOUR_TANK_ORANGE);
            }
            Tug_DrawPlate(Target, Body, -L + 4.0f, L - 8.0f, -1.0f, 1.0f, TUG_COLOUR_STEEL);
            break;

        case TUG_LOOK_YACHT:
            Tug_DrawShipHull(Target, Body, Flash ? TUG_COLOUR_DANGER : TUG_COLOUR_YACHT_TRIM, TUG_COLOUR_YACHT);
            Tug_DrawPlate(Target, Body, -L + 12.0f, L - 30.0f, -W + 6.0f, W - 6.0f, TUG_COLOUR_WOOD);
            Tug_DrawPlate(Target, Body, -L + 18.0f, L - 36.0f, -W + 8.0f, W - 8.0f, TUG_COLOUR_YACHT);
            Tug_DrawPlate(Target, Body, L - 42.0f, L - 36.0f, -W + 8.0f, W - 8.0f, TUG_COLOUR_TUG_CHARCOAL);
            Tug_BodyCircle(Target, Body, -L + 9.0f, 0.0f, 5U, TUG_COLOUR_GLASS);
            break;

        case TUG_LOOK_DINOSAUR:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            Tug_BodyLine(Target, Body, -L + 10.0f, 0.0f, L - 26.0f, 0.0f, 3U, TUG_COLOUR_BONE);
            for(float Along = -L + 20.0f; Along < (L - 30.0f); Along += 8.0f)
            {
                Tug_BodyLine(Target, Body, Along, -W + 8.0f, Along, W - 8.0f, 2U, TUG_COLOUR_BONE);
            }
            Tug_BodyCircle(Target, Body, L - 22.0f, 0.0f, 7U, TUG_COLOUR_BONE);
            Tug_BodyCircle(Target, Body, L - 20.0f, -3.0f, 2U, TUG_COLOUR_BLACK);
            break;

        case TUG_LOOK_BLADE:
            Tug_DrawPlate(Target, Body, -L, L, -3.0f, 3.0f, TUG_COLOUR_BARGE_DARK);
            {
                const float Blade[6][2] = { { L, -2.0f }, { L, 2.0f }, { -L * 0.6f, W }, { -L, W - 4.0f }, { -L, -W + 4.0f }, { -L * 0.6f, -W } };
                Tug_DrawShape(Target, Body, Blade, 6U, Flash ? TUG_COLOUR_DANGER : TUG_COLOUR_CAR_WHITE);
            }
            Tug_DrawPlate(Target, Body, -L, -L + 10.0f, -W + 4.0f, W - 4.0f, TUG_COLOUR_STEEL);
            Tug_DrawPlate(Target, Body, L - 20.0f, L - 12.0f, -3.0f, 3.0f, TUG_COLOUR_CAR_RED);
            break;

        case TUG_LOOK_TRANSFORMER:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, Body, -L + 14.0f, L - 18.0f, -W + 8.0f, W - 8.0f, TUG_COLOUR_TUG_GREEN);
            for(float Along = -L + 18.0f; Along < (L - 22.0f); Along += 6.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 2.0f, -W + 8.0f, W - 8.0f, TUG_COLOUR_TREE_DARK);
            }
            Tug_BodyCircle(Target, Body, 0.0f, -8.0f, 4U, TUG_COLOUR_CAR_WHITE);
            Tug_BodyCircle(Target, Body, 0.0f, 8.0f, 4U, TUG_COLOUR_CAR_WHITE);
            break;

        case TUG_LOOK_STEAMSHIP:
            Tug_DrawShipHull(Target, Body, Edge, TUG_COLOUR_TUG_CHARCOAL);
            Tug_DrawPlate(Target, Body, -L + 10.0f, L - 26.0f, -W + 6.0f, W - 6.0f, TUG_COLOUR_WOOD);
            Tug_DrawPlate(Target, Body, -L + 22.0f, L - 44.0f, -W + 8.0f, W - 8.0f, TUG_COLOUR_YACHT);
            Tug_BodyCircle(Target, Body, -4.0f, 0.0f, 8U, TUG_COLOUR_CAR_RED);
            Tug_BodyCircle(Target, Body, -4.0f, 0.0f, 4U, TUG_COLOUR_BLACK);
            Tug_BodyCircle(Target, Body, -L + 14.0f, -W + 10.0f, 4U, TUG_COLOUR_WHITE);
            break;

        case TUG_LOOK_ROYAL:
            Tug_DrawShipHull(Target, Body, Flash ? TUG_COLOUR_WHITE : TUG_COLOUR_GOLD, TUG_COLOUR_ROYAL);
            Tug_DrawPlate(Target, Body, -L + 16.0f, L - 34.0f, -W + 7.0f, W - 7.0f, TUG_COLOUR_GOLD);
            Tug_DrawPlate(Target, Body, -L + 20.0f, L - 38.0f, -W + 9.0f, W - 9.0f, TUG_COLOUR_CAR_RED);
            Tug_BodyCircle(Target, Body, L - 28.0f, 0.0f, 5U, TUG_COLOUR_GOLD);
            for(float Along = -L + 14.0f; Along < (L - 30.0f); Along += 14.0f)
            {
                Tug_BodyLine(Target, Body, Along, -W, Along - 6.0f, -W - 6.0f, 2U, TUG_COLOUR_WOOD);
                Tug_BodyLine(Target, Body, Along, W, Along - 6.0f, W + 6.0f, 2U, TUG_COLOUR_WOOD);
            }
            break;

        case TUG_LOOK_NUCLEAR:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE_DARK);
            for(float Along = -L + 18.0f; Along < (L - 14.0f); Along += 30.0f)
            {
                Tug_BodyCircle(Target, Body, Along, 0.0f, (uint16_t)(W - 6.0f), TUG_COLOUR_STEEL);
                Tug_BodyCircle(Target, Body, Along, 0.0f, 10U, TUG_COLOUR_NUCLEAR);
                Tug_BodyCircle(Target, Body, Along, 0.0f, 3U, TUG_COLOUR_BLACK);
            }
            Tug_DrawPlate(Target, Body, -L + 3.0f, L - 9.0f, -W + 3.0f, -W + 6.0f, TUG_COLOUR_HAZARD);
            Tug_DrawPlate(Target, Body, -L + 3.0f, L - 9.0f, W - 6.0f, W - 3.0f, TUG_COLOUR_HAZARD);
            break;

        case TUG_LOOK_ROCKET_ENGINE:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            {
                const float Bell[6][2] = { { L - 16.0f, -8.0f }, { L - 16.0f, 8.0f }, { -L + 20.0f, W - 6.0f }, { -L + 10.0f, W - 6.0f }, { -L + 10.0f, -W + 6.0f }, { -L + 20.0f, -W + 6.0f } };
                Tug_DrawShape(Target, Body, Bell, 6U, Flash ? TUG_COLOUR_WHITE : TUG_COLOUR_STEEL);
            }
            Tug_DrawPlate(Target, Body, L - 34.0f, L - 12.0f, -12.0f, 12.0f, TUG_COLOUR_TUG_CHARCOAL);
            Tug_BodyCircle(Target, Body, -L + 12.0f, 0.0f, (uint16_t)(W - 8.0f), TUG_COLOUR_BLACK);
            Tug_BodyLine(Target, Body, L - 30.0f, -14.0f, -L + 24.0f, -W + 8.0f, 2U, TUG_COLOUR_COPPER);
            Tug_BodyLine(Target, Body, L - 30.0f, 14.0f, -L + 24.0f, W - 8.0f, 2U, TUG_COLOUR_COPPER);
            break;

        case TUG_LOOK_GRANITE:
        case TUG_LOOK_MARBLE:
        {
            const uint8_t Stone = (Look == TUG_LOOK_GRANITE) ? TUG_COLOUR_STONE_DARK : TUG_COLOUR_WHITE;
            const uint8_t Vein = (Look == TUG_LOOK_GRANITE) ? TUG_COLOUR_TUG_CHARCOAL : TUG_COLOUR_SMOKE;
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 6.0f; (Along + 16.0f) < (L - 10.0f); Along += 19.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 16.0f, -W + 5.0f, -1.0f, Stone);
                Tug_DrawPlate(Target, Body, Along + 2.0f, Along + 18.0f, 1.0f, W - 5.0f, Stone);
                Tug_BodyLine(Target, Body, Along + 3.0f, -W + 8.0f, Along + 12.0f, -4.0f, 1U, Vein);
                Tug_BodyLine(Target, Body, Along + 6.0f, 4.0f, Along + 14.0f, W - 8.0f, 1U, Vein);
            }
            break;
        }

        case TUG_LOOK_PLANKS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, Body, -L + 6.0f, L - 10.0f, -W + 5.0f, W - 5.0f, TUG_COLOUR_LOG_END);
            for(float Across = -W + 8.0f; Across < (W - 6.0f); Across += 5.0f)
            {
                Tug_DrawPlate(Target, Body, -L + 6.0f, L - 10.0f, Across, Across + 1.0f, TUG_COLOUR_WOOD);
            }
            Tug_DrawPlate(Target, Body, -L / 3.0f, -L / 3.0f + 3.0f, -W + 4.0f, W - 4.0f, TUG_COLOUR_ROPE);
            Tug_DrawPlate(Target, Body, L / 3.0f, L / 3.0f + 3.0f, -W + 4.0f, W - 4.0f, TUG_COLOUR_ROPE);
            break;

        case TUG_LOOK_MILK:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 10.0f; Along < (L - 12.0f); Along += 11.0f)
            {
                for(int8_t Row = -1; Row <= 1; Row++)
                {
                    Tug_BodyCircle(Target, Body, Along, (float)Row * 10.0f, 5U, TUG_COLOUR_STEEL);
                    Tug_BodyCircle(Target, Body, Along, (float)Row * 10.0f, 3U, TUG_COLOUR_WHITE);
                }
            }
            break;

        case TUG_LOOK_PIPES:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(int8_t Pipe = 0; Pipe < 4; Pipe++)
            {
                const float Across = -W + 7.0f + ((float)Pipe * ((Body->Width - 14.0f) / 4.0f));
                Tug_DrawPlate(Target, Body, -L + 4.0f, L - 8.0f, Across, Across + 6.0f, TUG_COLOUR_STEEL);
                Tug_BodyCircle(Target, Body, L - 8.0f, Across + 3.0f, 3U, TUG_COLOUR_TUG_CHARCOAL);
            }
            break;

        case TUG_LOOK_SOLAR:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 6.0f; (Along + 18.0f) < (L - 8.0f); Along += 21.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 18.0f, -W + 5.0f, W - 5.0f, TUG_COLOUR_TUG_NAVY);
                Tug_DrawPlate(Target, Body, Along + 8.0f, Along + 9.0f, -W + 5.0f, W - 5.0f, TUG_COLOUR_GLASS);
                Tug_DrawPlate(Target, Body, Along, Along + 18.0f, -1.0f, 0.0f, TUG_COLOUR_GLASS);
            }
            break;

        case TUG_LOOK_ENGINES:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 8.0f; (Along + 22.0f) < (L - 8.0f); Along += 28.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 22.0f, -W + 6.0f, W - 6.0f, TUG_COLOUR_WOOD);
                Tug_DrawPlate(Target, Body, Along + 3.0f, Along + 19.0f, -W + 9.0f, W - 9.0f, TUG_COLOUR_CAR_BLUE);
                Tug_BodyCircle(Target, Body, Along + 11.0f, 0.0f, 4U, TUG_COLOUR_STEEL);
            }
            break;

        case TUG_LOOK_FIREWORKS:
            Tug_DrawBargeHull(Target, Body, Edge, TUG_COLOUR_BARGE);
            for(float Along = -L + 6.0f; (Along + 14.0f) < (L - 10.0f); Along += 16.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 14.0f, -W + 5.0f, -1.0f, TUG_COLOUR_CAR_RED);
                Tug_DrawPlate(Target, Body, Along, Along + 14.0f, 1.0f, W - 5.0f, TUG_COLOUR_CAR_RED);
                Tug_BodyCircle(Target, Body, Along + 7.0f, -W / 2.0f, 2U, TUG_COLOUR_CAR_YELLOW);
                Tug_BodyCircle(Target, Body, Along + 7.0f, W / 2.0f, 2U, TUG_COLOUR_CAR_YELLOW);
            }
            Tug_DrawPlate(Target, Body, -L + 3.0f, L - 9.0f, -W + 3.0f, -W + 5.0f, TUG_COLOUR_HAZARD);
            Tug_DrawPlate(Target, Body, -L + 3.0f, L - 9.0f, W - 5.0f, W - 3.0f, TUG_COLOUR_HAZARD);
            break;

        case TUG_LOOK_FUEL_TANK:
        default:
            Tug_DrawPlate(Target, Body, -L, L, -W + 10.0f, W - 10.0f, TUG_COLOUR_BARGE_DARK);
            {
                const float Tank[8][2] = { { L, 0.0f }, { L - 18.0f, -W + 2.0f }, { L - 30.0f, -W }, { -L, -W }, { -L, W }, { L - 30.0f, W }, { L - 18.0f, W - 2.0f }, { L, 0.0f } };
                Tug_DrawShape(Target, Body, Tank, 7U, Flash ? TUG_COLOUR_WHITE : TUG_COLOUR_TANK_ORANGE);
            }
            for(float Along = -L + 20.0f; Along < (L - 30.0f); Along += 30.0f)
            {
                Tug_DrawPlate(Target, Body, Along, Along + 2.0f, -W, W, TUG_COLOUR_RUST);
            }
            Tug_DrawPlate(Target, Body, -L, L - 30.0f, -2.0f, 2.0f, TUG_COLOUR_WHEAT_DARK);
            break;
    }
}

/*
 * Cargo thrusters: a pod at the middle of each of the cargo's four sides.
 * Swinging the bow right, the bow pod blows foam out to the left and the
 * stern pod out to the right, and the other way about.
 */
static void Tug_DrawCargoThrusters(Render_TargetTypeDef *Target, const Tug_BodyTypeDef *Body, int8_t Thruster, uint32_t Clock)
{
    const float L = Body->Length / 2.0f;
    const float W = Body->Width / 2.0f;
    const float Pods[4][2] = { { L - 2.0f, 0.0f }, { -L + 2.0f, 0.0f }, { 0.0f, -W }, { 0.0f, W } };
    for(uint8_t Index = 0U; Index < 4U; Index++)
    {
        Tug_BodyCircle(Target, Body, Pods[Index][0], Pods[Index][1], 5U, TUG_COLOUR_TUG_CHARCOAL);
        Tug_BodyCircle(Target, Body, Pods[Index][0], Pods[Index][1], 2U, TUG_COLOUR_TUG_ORANGE);
    }
    if(Thruster != 0)
    {
        const float Side = (float)(-Thruster);
        const uint16_t Puff = (uint16_t)(3U + ((Clock / 60U) % 3U));
        Tug_BodyCircle(Target, Body, L - 2.0f, Side * 10.0f, Puff, TUG_COLOUR_FOAM);
        Tug_BodyCircle(Target, Body, L - 2.0f, Side * (W + 6.0f), (uint16_t)(Puff - 1U), TUG_COLOUR_FOAM);
        Tug_BodyCircle(Target, Body, -L + 2.0f, -Side * 10.0f, Puff, TUG_COLOUR_FOAM);
        Tug_BodyCircle(Target, Body, -L + 2.0f, -Side * (W + 6.0f), (uint16_t)(Puff - 1U), TUG_COLOUR_FOAM);
    }
}

static void Tug_DrawCargo(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    const Tug_TowTypeDef *Cargo = &World->Cargo;
    if(!Cargo->Present)
    {
        return;
    }
    if(Cargo->SinkMilliseconds > 0U)
    {
        /* Wrecked: it settles into the water and bubbles away. */
        if(Cargo->SinkMilliseconds < 2200U)
        {
            const Tug_BodyTypeDef *Body = &Cargo->Body;
            const float Shrink = 1.0f - ((float)Cargo->SinkMilliseconds / 2600.0f);
            Tug_DrawPlate(Target, Body, -Body->Length * Shrink / 2.0f, Body->Length * Shrink / 2.0f, -Body->Width * Shrink / 2.0f, Body->Width * Shrink / 2.0f, TUG_COLOUR_SHADOW);
            for(uint8_t Bubble = 0U; Bubble < 5U; Bubble++)
            {
                const float Along = ((float)Bubble - 2.0f) * Body->Length / 6.0f;
                Tug_BodyCircle(Target, Body, Along, (float)(((Cargo->SinkMilliseconds / 90U) + Bubble) % 7U) - 3.0f,
                               (uint16_t)(3U + ((Cargo->SinkMilliseconds / 150U) + Bubble) % 4U), TUG_COLOUR_FOAM);
            }
        }
        return;
    }
    Tug_DrawCargoBody(Target, &Cargo->Body, Tug_CargoTypes[Cargo->Type].Look, Cargo->FlashMilliseconds > 0U);
    if(Cargo->Thrusters)
    {
        Tug_DrawCargoThrusters(Target, &Cargo->Body, Cargo->Thruster, World->Milliseconds);
    }
}

/* -------------------------------------------------------------------------- */
/* Traffic and docks                                                          */
/* -------------------------------------------------------------------------- */

/* Where a person on a lane is, in world pixels; Lane is pixels from the lane's left or top edge. */
static void Tug_WalkerPosition(const Tug_WorldTypeDef *World, const Tug_WalkerTypeDef *Walker, float Lane, float *X, float *Y)
{
    const Tug_SegmentTypeDef *Segment = &World->Segments[Walker->Segment];
    if(Segment->Across)
    {
        *X = (float)(Segment->X * TUG_TILE_SIZE) + Walker->Position;
        *Y = (float)(Segment->Y * TUG_TILE_SIZE) + Lane;
    }
    else
    {
        *X = (float)(Segment->X * TUG_TILE_SIZE) + Lane;
        *Y = (float)(Segment->Y * TUG_TILE_SIZE) + Walker->Position;
    }
}

static bool Tug_OnScreen(float X, float Y, float Margin)
{
    const float ScreenX = X + (float)Tug_OffsetX;
    const float ScreenY = Y + (float)Tug_OffsetY;
    return (ScreenX > -Margin) && (ScreenY > -Margin) && (ScreenX < (float)RENDER_WIDTH + Margin) && (ScreenY < (float)RENDER_HEIGHT + Margin);
}

/* People strolling along the lanes. */
/* -------------------------------------------------------------------------- */
/* Weather                                                                    */
/* -------------------------------------------------------------------------- */

/* Rain falling across the screen, slanting with the wind, and rings where it lands on the water. */
/* Rain falling across the screen, slanting with the wind, and rings where it lands on the water. Whole-number maths throughout, as it runs for every drop every frame. */
static void Tug_DrawRain(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World, uint8_t Drops)
{
    const uint32_t Clock = World->Milliseconds % 600000U;
    const int32_t SlantPerMille = (int32_t)lrintf(World->WindX * 1000.0f / 120.0f);
    const int32_t CameraX = (int32_t)lrintf(World->CameraX);
    const int32_t CameraY = (int32_t)lrintf(World->CameraY);

    for(uint8_t Index = 0U; Index < Drops; Index++)
    {
        const uint32_t Hash = Tug_TileHash((int16_t)Index, 911);
        const uint32_t Speed = 520U + ((Hash >> 8) % 260U);
        const int32_t Y = (int32_t)((((Hash >> 4) % 500U) + ((Clock * Speed) / 1000U)) % 500U) - 20;
        const int32_t X = (((((int32_t)(Hash % 840U) + ((Y * SlantPerMille) / 1000)) % 840) + 840) % 840) - 20;
        Render_DrawLine(Target, (int16_t)X, (int16_t)Y, (int16_t)(X + ((12 * SlantPerMille) / 1000)), (int16_t)(Y + 12), 1U, TUG_COLOUR_FOAM_FADED);
    }

    /* Each ring grows for half a second, then starts again somewhere else. */
    for(uint8_t Index = 0U; Index < (uint8_t)(Drops / 4U); Index++)
    {
        const uint32_t Round = (World->Milliseconds + (uint32_t)Index * 137U) / 500U;
        const uint32_t Hash = Tug_TileHash((int16_t)Index, (int16_t)(Round & 0x7FFFU));
        const int32_t X = CameraX + (int32_t)(Hash % RENDER_WIDTH);
        const int32_t Y = CameraY + (int32_t)((Hash >> 12) % RENDER_HEIGHT);
        const uint32_t Age = (World->Milliseconds + (uint32_t)Index * 137U) % 500U;
        if((X >= 0) && (Y >= 0) && Tug_IsWaterAt((int16_t)(X / TUG_TILE_SIZE), (int16_t)(Y / TUG_TILE_SIZE)))
        {
            Render_DrawCircle(Target, (int16_t)(X + Tug_OffsetX), (int16_t)(Y + Tug_OffsetY), (uint16_t)(2U + (Age / 100U)), 1U, TUG_COLOUR_WATER_GLINT);
        }
    }
}

/* Wind: streaks running across the water the way it blows, two for each patch of the map on screen. */
#define WIND_PATCH              (100)
#define WIND_STREAKS_PER_PATCH  (2)

static void Tug_DrawWindStreaks(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    const float Strength = sqrtf((World->WindX * World->WindX) + (World->WindY * World->WindY));

    /* The wind's direction in thousandths, so each streak needs only whole-number maths. */
    const int32_t DirectionX = (int32_t)lrintf(World->WindX * 1000.0f / Strength);
    const int32_t DirectionY = (int32_t)lrintf(World->WindY * 1000.0f / Strength);
    const int32_t LengthX = (DirectionX * 26) / 1000;
    const int32_t LengthY = (DirectionY * 26) / 1000;

    /* Each streak is two thin lines side by side, which look like one 2 pixel line but draw much faster. */
    const int32_t BesideX = (abs(DirectionX) >= abs(DirectionY)) ? 0 : 1;
    const int32_t BesideY = 1 - BesideX;
    const int16_t FirstX = (int16_t)(floorf(World->CameraX / (float)WIND_PATCH) - 1.0f);
    const int16_t FirstY = (int16_t)(floorf(World->CameraY / (float)WIND_PATCH) - 1.0f);
    const int16_t LastX = (int16_t)(FirstX + ((int16_t)RENDER_WIDTH / WIND_PATCH) + 2);
    const int16_t LastY = (int16_t)(FirstY + ((int16_t)RENDER_HEIGHT / WIND_PATCH) + 2);
    const uint32_t Drift = World->Milliseconds / 20U;

    for(int16_t PatchY = FirstY; PatchY <= LastY; PatchY++)
    {
        for(int16_t PatchX = FirstX; PatchX <= LastX; PatchX++)
        {
            for(int16_t Streak = 0; Streak < WIND_STREAKS_PER_PATCH; Streak++)
            {
                const uint32_t Hash = Tug_TileHash(PatchX, (int16_t)(PatchY + 3000 + (Streak * 1000)));
                const int32_t Travel = (int32_t)((Drift + (Hash % (uint32_t)WIND_PATCH)) % (uint32_t)WIND_PATCH);
                const int32_t X = ((int32_t)PatchX * WIND_PATCH) + (int32_t)((Hash >> 8) % (uint32_t)WIND_PATCH) + ((DirectionX * Travel) / 1000);
                const int32_t Y = ((int32_t)PatchY * WIND_PATCH) + (int32_t)((Hash >> 16) % (uint32_t)WIND_PATCH) + ((DirectionY * Travel) / 1000);
                const int32_t ScreenX = X + Tug_OffsetX;
                const int32_t ScreenY = Y + Tug_OffsetY;

                /*
                 * Each streak shows only in the middle of its run, so it fades in
                 * and out rather than jumping; streaks off the screen are skipped
                 * before the map is read.
                 */
                if((Travel > 10) && (Travel < 90) && (ScreenX > -30) && (ScreenX < ((int32_t)RENDER_WIDTH + 30)) && (ScreenY > -30) && (ScreenY < ((int32_t)RENDER_HEIGHT + 30)) &&
                   (X >= 0) && (Y >= 0) && Tug_IsWaterAt((int16_t)(X / TUG_TILE_SIZE), (int16_t)(Y / TUG_TILE_SIZE)))
                {
                    Render_DrawLine(Target, (int16_t)ScreenX, (int16_t)ScreenY, (int16_t)(ScreenX + LengthX), (int16_t)(ScreenY + LengthY), 1U, TUG_COLOUR_WATER_GLINT);
                    Render_DrawLine(Target, (int16_t)(ScreenX + BesideX), (int16_t)(ScreenY + BesideY), (int16_t)(ScreenX + LengthX + BesideX), (int16_t)(ScreenY + LengthY + BesideY), 1U, TUG_COLOUR_WATER_GLINT);
                }
            }
        }
    }
}

/* Rain over everything, drawn last. */
static void Tug_DrawWeather(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    if(World->Weather == TUG_WEATHER_RAIN)
    {
        Tug_DrawRain(Target, World, 60U);
    }
    else if(World->Weather == TUG_WEATHER_STORM)
    {
        Tug_DrawRain(Target, World, 110U);
    }
}

/* What the weather is called on the job board. */
static const char *Tug_WeatherName(Tug_WeatherTypeDef Weather)
{
    static const char *const Names[TUG_WEATHER_COUNT] = { "CLEAR", "RAIN", "WINDY", "STORM" };
    return Names[((uint8_t)Weather < (uint8_t)TUG_WEATHER_COUNT) ? (uint8_t)Weather : 0U];
}

static void Tug_DrawTraffic(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    for(uint8_t Index = 0U; Index < TUG_MAX_PEOPLE; Index++)
    {
        const Tug_WalkerTypeDef *Person = &World->People[Index];
        float X;
        float Y;
        if(Person->Segment >= World->SegmentCount)
        {
            continue;
        }
        Tug_WalkerPosition(World, Person, (Person->Direction > 0) ? 18.0f : 2.0f, &X, &Y);
        if(Tug_OnScreen(X, Y, 4.0f))
        {
            Tug_Circle(Target, X, Y, 2U, Person->Colour);
        }
    }
}

/* Each site: its buildings, a name board, and at its dock a ring, bright when it's where to go. */
static void Tug_DrawDocks(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World, bool Labels)
{
    for(uint8_t Dock = 0U; Dock < TUG_DOCK_COUNT; Dock++)
    {
        const Tug_DockTypeDef *Site = &Tug_Docks[Dock];
        const bool IsTarget = World->TargetDock == (int8_t)Dock;
        float X;
        float Y;

        if(!Labels)
        {
            if(Tug_OnScreen((float)Site->SiteX, (float)Site->SiteY, 160.0f))
            {
                Tug_DrawSite(Target, Site);
            }
            continue;
        }

        Tug_DockPosition(Dock, &X, &Y);
        if(Tug_OnScreen(X, Y, 80.0f))
        {
            if(IsTarget)
            {
                Render_DrawCircle(Target, (int16_t)(lrintf(X) + Tug_OffsetX), (int16_t)(lrintf(Y) + Tug_OffsetY), (uint16_t)(56U + ((World->Milliseconds / 80U) % 6U)), 3U,
                                  ((World->Milliseconds / 300U) & 1U) ? TUG_COLOUR_TARGET : TUG_COLOUR_WHITE);
            }
            else
            {
                Render_DrawCircle(Target, (int16_t)(lrintf(X) + Tug_OffsetX), (int16_t)(lrintf(Y) + Tug_OffsetY), 46U, 1U, TUG_COLOUR_WATER_GLINT);
            }
        }
        if(Tug_OnScreen((float)Site->SiteX, (float)Site->SiteY - 80.0f, 120.0f))
        {
            const uint16_t Width = (uint16_t)(Render_TextWidth(&OpenSansBold12, Site->Name) + 14U);
            const int16_t LabelX = (int16_t)(Site->SiteX + Tug_OffsetX);
            const int16_t LabelY = (int16_t)(Site->SiteY - 84 + Tug_OffsetY);
            Render_FillRoundRect(Target, (int16_t)(LabelX - (int16_t)(Width / 2U) + 2), (int16_t)(LabelY + 3), Width, 19U, 4U, TUG_COLOUR_SHADOW);
            Render_FillRoundRect(Target, (int16_t)(LabelX - (int16_t)(Width / 2U)), LabelY, Width, 19U, 4U, IsTarget ? TUG_COLOUR_TARGET : TUG_COLOUR_PANEL);
            Render_DrawTextAligned(Target, &OpenSansBold12, Site->Name, LabelX, (int16_t)(LabelY + 2), RENDER_ALIGN_CENTRE, IsTarget ? TUG_COLOUR_BLACK : TUG_COLOUR_TEXT);
        }
    }
}

void Tug_DrawWorld(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    const int16_t CameraX = (int16_t)lrintf(World->CameraX);
    const int16_t CameraY = (int16_t)lrintf(World->CameraY);
    const int16_t FirstX = (int16_t)((CameraX >= 0) ? (CameraX / TUG_TILE_SIZE) : -1);
    const int16_t FirstY = (int16_t)((CameraY >= 0) ? (CameraY / TUG_TILE_SIZE) : -2);
    const int16_t LastX = (int16_t)(FirstX + ((int16_t)RENDER_WIDTH / TUG_TILE_SIZE) + 1);
    const int16_t LastY = (int16_t)(FirstY + ((int16_t)RENDER_HEIGHT / TUG_TILE_SIZE) + 2);

    Tug_OffsetX = (int16_t)-CameraX;
    Tug_OffsetY = (int16_t)-CameraY;
    Tug_Clock = World->Milliseconds;

    /* The ground and the water. */
    for(int16_t Y = FirstY; Y <= LastY; Y++)
    {
        for(int16_t X = FirstX; X <= LastX; X++)
        {
            const char Tile = Tug_TileAt(X, Y);
            const int16_t Left = (int16_t)((X * TUG_TILE_SIZE) + Tug_OffsetX);
            const int16_t Top = (int16_t)((Y * TUG_TILE_SIZE) + Tug_OffsetY);
            if(Tug_TileIsWater(Tile))
            {
                Tug_DrawWaterTile(Target, Tile, X, Y, Left, Top);
            }
            else
            {
                Tug_DrawLandTile(Target, Tile, X, Y, Left, Top);
                Tug_DrawBank(Target, X, Y, Left, Top);
            }
        }
    }
    Tug_DrawDocks(Target, World, false);

    for(uint8_t Index = 0U; Index < TUG_MAX_WAKE; Index++)
    {
        const Tug_WakeTypeDef *Foam = &World->Wake[Index];
        if(Foam->Active)
        {
            const uint16_t Radius = (uint16_t)(3U + ((Foam->AgeMilliseconds * 7U) / TUG_WAKE_LIFE_MS));
            const uint8_t Colour = (Foam->AgeMilliseconds < (TUG_WAKE_LIFE_MS / 3U)) ? TUG_COLOUR_FOAM :
                                   ((Foam->AgeMilliseconds < ((TUG_WAKE_LIFE_MS * 2U) / 3U)) ? TUG_COLOUR_FOAM_FADED : TUG_COLOUR_WATER_GLINT);
            Render_DrawCircle(Target, (int16_t)(lrintf(Foam->X) + Tug_OffsetX), (int16_t)(lrintf(Foam->Y) + Tug_OffsetY), Radius, 2U, Colour);
        }
    }

    /* Wind streaks run across the water, under the boats. */
    if((World->Weather == TUG_WEATHER_WIND) || (World->Weather == TUG_WEATHER_STORM))
    {
        Tug_DrawWindStreaks(Target, World);
    }

    /* Buoys bob in the shallows. */
    for(int16_t Y = FirstY; Y <= LastY; Y++)
    {
        for(int16_t X = FirstX; X <= LastX; X++)
        {
            if(Tug_TileAt(X, Y) == TUG_TILE_BUOY)
            {
                const uint32_t Hash = Tug_TileHash(X, Y);
                const float CentreX = (float)((X * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2));
                const float CentreY = (float)((Y * TUG_TILE_SIZE) + (TUG_TILE_SIZE / 2)) + (1.5f * sinf(((float)Tug_Clock * 0.004f) + (float)(Hash % 628U) * 0.01f));
                Tug_Circle(Target, CentreX + 2.0f, CentreY + 3.0f, 7U, TUG_COLOUR_SHADOW);
                Tug_Circle(Target, CentreX, CentreY, 7U, TUG_COLOUR_BUOY);
                Tug_Circle(Target, CentreX, CentreY, 3U, TUG_COLOUR_WHITE);
            }
        }
    }

    for(uint8_t Index = 0U; Index < TUG_MOORED_BOAT_COUNT; Index++)
    {
        if(Tug_OnScreen((float)Tug_MooredBoats[Index].X, (float)Tug_MooredBoats[Index].Y, 60.0f))
        {
            Tug_DrawNarrowboat(Target, &Tug_MooredBoats[Index]);
        }
    }

    Tug_DrawDocks(Target, World, true);
    Tug_DrawCargo(Target, World);

    /* The tow rope and, while hooking on, a marker at the cargo's bow. */
    if(World->Cargo.Present && (World->Cargo.SinkMilliseconds == 0U))
    {
        float SternX;
        float SternY;
        float BowX;
        float BowY;
        Tug_LocalPoint(&World->Tug.Body, -World->Tug.Body.Length / 2.0f + 3.0f, 0.0f, &SternX, &SternY);
        Tug_LocalPoint(&World->Cargo.Body, World->Cargo.Body.Length / 2.0f, 0.0f, &BowX, &BowY);
        if(World->Cargo.Hooked)
        {
            Tug_Line(Target, SternX, SternY, BowX, BowY, 3U, TUG_COLOUR_ROPE);
        }
        else if(World->TargetIsCargo)
        {
            (void)Tug_HookEnd(World, &BowX, &BowY);
            Render_DrawCircle(Target, (int16_t)(lrintf(BowX) + Tug_OffsetX), (int16_t)(lrintf(BowY) + Tug_OffsetY),
                              (uint16_t)(12U + ((World->Milliseconds / 90U) % 6U)), 3U, ((World->Milliseconds / 300U) & 1U) ? TUG_COLOUR_TARGET : TUG_COLOUR_WHITE);
        }
    }

    Tug_DrawTugBoat(Target, &World->Tug.Body, Tug_Models[World->Tug.Model].Colour);

    Tug_DrawTraffic(Target, World);
    Tug_DrawWeather(Target, World);
}

/* -------------------------------------------------------------------------- */
/* Status bar, gauges and guidance                                            */
/* -------------------------------------------------------------------------- */

static void Tug_DrawGauge(Render_TargetTypeDef *Target, int16_t X, float Engine, float Throttle)
{
    const int16_t Middle = (int16_t)(GAUGE_TOP + (GAUGE_HEIGHT / 2));
    const int16_t Fill = (int16_t)lrintf(Engine * (float)((GAUGE_HEIGHT / 2) - 3));
    const int16_t Tick = (int16_t)lrintf(Throttle * (float)((GAUGE_HEIGHT / 2) - 3));

    Render_FillRoundRect(Target, (int16_t)(X - 2), (int16_t)(GAUGE_TOP - 2), (uint16_t)(GAUGE_WIDTH + 4), (uint16_t)(GAUGE_HEIGHT + 4), 6U, TUG_COLOUR_PANEL_EDGE);
    Render_FillRoundRect(Target, X, GAUGE_TOP, GAUGE_WIDTH, GAUGE_HEIGHT, 5U, TUG_COLOUR_PANEL);
    if(Fill > 0)
    {
        Render_Box(Target, (int16_t)(X + 2), (int16_t)(Middle - Fill), (uint16_t)(GAUGE_WIDTH - 4), (uint16_t)Fill, TUG_COLOUR_MONEY);
    }
    else if(Fill < 0)
    {
        Render_Box(Target, (int16_t)(X + 2), Middle, (uint16_t)(GAUGE_WIDTH - 4), (uint16_t)(-Fill), TUG_COLOUR_DANGER);
    }
    Render_Box(Target, X, (int16_t)(Middle - 1), GAUGE_WIDTH, 2U, TUG_COLOUR_TEXT_MUTED);
    Render_Box(Target, (int16_t)(X - 2), (int16_t)(Middle - Tick - 1), (uint16_t)(GAUGE_WIDTH + 4), 3U, TUG_COLOUR_WHITE);
}

/* Where the player should be heading now, if anywhere. */
static bool Tug_TargetPosition(const Tug_WorldTypeDef *World, float *X, float *Y)
{
    if(World->TargetIsCargo && World->Cargo.Present)
    {
        (void)Tug_HookEnd(World, X, Y);
        return true;
    }
    if(World->TargetDock >= 0)
    {
        Tug_DockPosition((uint8_t)World->TargetDock, X, Y);
        return true;
    }
    return false;
}

/* An arrow at the edge of the screen pointing the way to an off-screen target, with how far it is. */
static void Tug_DrawPointer(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World)
{
    float TargetX;
    float TargetY;
    float ScreenX;
    float ScreenY;
    float Angle;
    float Distance;
    float EdgeX;
    float EdgeY;
    char Text[16];

    if(!Tug_TargetPosition(World, &TargetX, &TargetY))
    {
        return;
    }
    ScreenX = TargetX - World->CameraX;
    ScreenY = TargetY - World->CameraY;
    if((ScreenX > 30.0f) && (ScreenX < (float)RENDER_WIDTH - 30.0f) && (ScreenY > (float)TUG_HUD_HEIGHT + 30.0f) && (ScreenY < (float)RENDER_HEIGHT - 30.0f))
    {
        return;
    }

    Angle = atan2f(TargetX - World->Tug.Body.X, -(TargetY - World->Tug.Body.Y));
    Distance = sqrtf(((TargetX - World->Tug.Body.X) * (TargetX - World->Tug.Body.X)) + ((TargetY - World->Tug.Body.Y) * (TargetY - World->Tug.Body.Y)));
    EdgeX = fminf(fmaxf(ScreenX, 46.0f), (float)RENDER_WIDTH - 46.0f);
    EdgeY = fminf(fmaxf(ScreenY, (float)TUG_HUD_HEIGHT + 36.0f), (float)RENDER_HEIGHT - 36.0f);
    {
        const float Sine = sinf(Angle);
        const float Cosine = cosf(Angle);
        const Render_PointTypeDef Arrow[3] =
        {
            { (int16_t)lrintf(EdgeX + (16.0f * Sine)), (int16_t)lrintf(EdgeY - (16.0f * Cosine)) },
            { (int16_t)lrintf(EdgeX - (10.0f * Sine) + (10.0f * Cosine)), (int16_t)lrintf(EdgeY + (10.0f * Cosine) + (10.0f * Sine)) },
            { (int16_t)lrintf(EdgeX - (10.0f * Sine) - (10.0f * Cosine)), (int16_t)lrintf(EdgeY + (10.0f * Cosine) - (10.0f * Sine)) }
        };
        Render_FillCircle(Target, (int16_t)lrintf(EdgeX), (int16_t)lrintf(EdgeY), 20U, TUG_COLOUR_PANEL);
        (void)Render_DrawPolygon(Target, Arrow, 3U, TUG_COLOUR_TARGET);
    }
    (void)Render_FormatText(Text, sizeof(Text), "%u M", (unsigned int)(lrintf(Distance * 0.25f / 10.0f) * 10));
    Render_DrawTextAligned(Target, &OpenSansBold12, Text, (int16_t)lrintf(EdgeX), (int16_t)lrintf(EdgeY + 22.0f), RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
}

/* The status bar: the bank balance, what to do next, and how the cargo is faring. */
static void Tug_DrawStatusBar(Render_TargetTypeDef *Target)
{
    const Tug_TowTypeDef *Cargo = &Tug_Game.World.Cargo;
    char Text[64];

    Render_Box(Target, 0, 0, RENDER_WIDTH, TUG_HUD_HEIGHT, TUG_COLOUR_PANEL);
    Render_Box(Target, 0, TUG_HUD_HEIGHT, RENDER_WIDTH, 2U, TUG_COLOUR_PANEL_EDGE);
    Tug_FormatMoney(Text, sizeof(Text), Tug_Game.Career.Money);
    Render_DrawText(Target, &OpenSansBold16, Text, 12, 4, TUG_COLOUR_MONEY);
    if(Tug_Game.HasJob && Cargo->Present)
    {
        const float Condition = fmaxf(Cargo->Condition, 0.0f);
        Render_DrawText(Target, &OpenSans12, "CARGO", 682, 7, TUG_COLOUR_TEXT_MUTED);
        Render_Box(Target, 724, 9, 66U, 10U, TUG_COLOUR_PANEL_EDGE);
        Render_Box(Target, 725, 10, (uint16_t)lrintf(64.0f * Condition / 100.0f), 8U,
                   (Condition > 60.0f) ? TUG_COLOUR_MONEY : ((Condition > 30.0f) ? TUG_COLOUR_TARGET : TUG_COLOUR_DANGER));
    }
    /* The on-time bonus, beside the cargo bar: waiting to start until the cargo's hooked on, then counting down; gone once it runs out. */
    if((Tug_Game.Phase == TUG_PHASE_TO_CARGO) || (Tug_Game.BonusMilliseconds > 0U))
    {
        const uint32_t Seconds = (Tug_Game.Phase == TUG_PHASE_TO_CARGO) ? Tug_Game.Job.BonusSeconds : ((Tug_Game.BonusMilliseconds + 999U) / 1000U);
        (void)Render_FormatText(Text, sizeof(Text), "BONUS %u:%02u", (unsigned int)(Seconds / 60U), (unsigned int)(Seconds % 60U));
        Render_DrawTextAligned(Target, &OpenSansBold12, Text, 668, 7, RENDER_ALIGN_RIGHT, (Tug_Game.Phase == TUG_PHASE_TO_CARGO) ? TUG_COLOUR_TEXT_MUTED : TUG_COLOUR_TARGET);
    }
    if(Tug_Game.Phase == TUG_PHASE_TO_CARGO)
    {
        (void)Render_FormatText(Text, sizeof(Text), "COLLECT %s AT %s", Tug_CargoTypes[Tug_Game.Job.Cargo].Name, Tug_Docks[Tug_Game.Job.From].Name);
    }
    else
    {
        (void)Render_FormatText(Text, sizeof(Text), "TOW %s TO %s", Tug_CargoTypes[Tug_Game.Job.Cargo].Name, Tug_Docks[Tug_Game.Job.To].Name);
    }
    Render_DrawTextAligned(Target, (Render_TextWidth(&OpenSans16, Text) > 440U) ? &OpenSans12 : &OpenSans16, Text, 375, 5, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
}

static void Tug_DrawBanner(Render_TargetTypeDef *Target, const char *Text, uint8_t Colour)
{
    const uint16_t Width = (uint16_t)(Render_TextWidth(&OpenSans16, Text) + 32U);
    const int16_t Left = (int16_t)(((int16_t)RENDER_WIDTH - (int16_t)Width) / 2);
    Tug_DrawPanel(Target, Left, 40, Width, 32U);
    Render_DrawTextAligned(Target, &OpenSans16, Text, (int16_t)(RENDER_WIDTH / 2U), 46, RENDER_ALIGN_CENTRE, Colour);
}

static void Tug_DrawGuidance(Render_TargetTypeDef *Target)
{
    const Tug_WorldTypeDef *World = &Tug_Game.World;
    char Text[64];

    if(Tug_Game.Phase == TUG_PHASE_TO_CARGO)
    {
        const float DeltaX = World->Tug.Body.X - World->Cargo.Body.X;
        const float DeltaY = World->Tug.Body.Y - World->Cargo.Body.Y;
        if((Tug_Game.HintMilliseconds > 0U) || (((DeltaX * DeltaX) + (DeltaY * DeltaY)) < (220.0f * 220.0f)))
        {
            Tug_DrawBanner(Target, "BACK YOUR STERN UP TO THE MARKED END TO HOOK ON", TUG_COLOUR_TEXT);
        }
    }
    else if((Tug_Game.Phase == TUG_PHASE_TOWING) && ((Tug_Game.HintMilliseconds > 0U) || (World->DeliverProgress > 0.0f)))
    {
        if(World->DeliverProgress > 0.0f)
        {
            Tug_DrawBanner(Target, "HOLD IT STILL...", TUG_COLOUR_TARGET);
            Render_Box(Target, 330, 66, (uint16_t)lrintf(140.0f * fminf(World->DeliverProgress, 1.0f)), 3U, TUG_COLOUR_TARGET);
        }
        else
        {
            (void)Render_FormatText(Text, sizeof(Text), "TOW IT INTO THE RING AT %s AND HOLD STILL", Tug_Docks[Tug_Game.Job.To].Name);
            Tug_DrawBanner(Target, Text, TUG_COLOUR_TEXT);
        }
    }
    if((Tug_Game.Career.Deliveries == 0U) && (Tug_Game.HintMilliseconds > 0U))
    {
        Tug_DrawPanel(Target, 170, 420, 460U, 42U);
        Render_DrawTextAligned(Target, &OpenSans12, "EACH SLIDER DRIVES ONE ENGINE: UP AHEAD, DOWN ASTERN", (int16_t)(RENDER_WIDTH / 2U), 426, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
        Render_DrawTextAligned(Target, &OpenSans12, "KEEP OFF THE BANKS AND OUT OF THE SHALLOWS: THEY DAMAGE THE CARGO", (int16_t)(RENDER_WIDTH / 2U), 442, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
    }
}

/* -------------------------------------------------------------------------- */
/* Screens                                                                    */
/* -------------------------------------------------------------------------- */

static void Tug_DrawTitle(Render_TargetTypeDef *Target)
{
    const Tug_CareerTypeDef *Career = &Tug_Game.Career;
    char Money[24];

    Tug_DrawPanel(Target, 220, 120, 360U, 230U);
    Render_DrawTextAligned(Target, &OpenSansBold28, "CANAL TUG", (int16_t)(RENDER_WIDTH / 2U), 140, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
    Render_DrawTextAligned(Target, &OpenSans16, "HAULAGE ON THE COUNTRY CANALS", (int16_t)(RENDER_WIDTH / 2U), 182, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
    Tug_FormatMoney(Money, sizeof(Money), Career->Money);
    Render_DrawTextAligned(Target, &OpenSansBold20, Money, (int16_t)(RENDER_WIDTH / 2U), 220, RENDER_ALIGN_CENTRE, TUG_COLOUR_MONEY);
    Render_DrawTextf(Target, &OpenSans12, 300, 252, TUG_COLOUR_TEXT_MUTED, "TUG: %s", Tug_Models[Career->Model].Name);
    Render_DrawTextf(Target, &OpenSans12, 300, 270, TUG_COLOUR_TEXT_MUTED, "DELIVERIES: %u", (unsigned int)Career->Deliveries);
    if(TUG_TEST_MODE != 0)
    {
        Render_DrawTextAligned(Target, &OpenSansBold12, "TEST MODE: ALL UNLOCKED, NOT SAVED", (int16_t)(RENDER_WIDTH / 2U), 328, RENDER_ALIGN_CENTRE, TUG_COLOUR_DANGER);
    }
    if(((Tug_Game.ScreenMilliseconds / 500U) & 1U) == 0U)
    {
        Render_DrawTextAligned(Target, &OpenSans16, "BUMPER: GO TO WORK", (int16_t)(RENDER_WIDTH / 2U), 308, RENDER_ALIGN_CENTRE, TUG_COLOUR_TARGET);
    }
}

/* A small picture of a cargo or a tug for a list, drawn on the screen itself. */
static void Tug_DrawCargoIcon(Render_TargetTypeDef *Target, uint8_t Type, int16_t X, int16_t Y)
{
    const Tug_CargoTypeDef *Kind = &Tug_CargoTypes[Type];
    const float Scale = fminf(0.55f, 76.0f / Kind->Length);
    const Tug_BodyTypeDef Body = { (float)X, (float)Y, 0.0f, 0.0f, PI_F / 2.0f, 0.0f, Kind->Length * Scale, Kind->Width * Scale };
    const int16_t SavedX = Tug_OffsetX;
    const int16_t SavedY = Tug_OffsetY;
    Tug_OffsetX = 0;
    Tug_OffsetY = 0;
    Tug_DrawCargoBody(Target, &Body, Kind->Look, false);
    Tug_OffsetX = SavedX;
    Tug_OffsetY = SavedY;
}

static void Tug_DrawTugIcon(Render_TargetTypeDef *Target, uint8_t Model, int16_t X, int16_t Y)
{
    const Tug_ModelTypeDef *Kind = &Tug_Models[Model];
    const Tug_BodyTypeDef Body = { (float)X, (float)Y, 0.0f, 0.0f, PI_F / 2.0f, 0.0f, Kind->Length * 0.8f, Kind->Width * 0.8f };
    const int16_t SavedX = Tug_OffsetX;
    const int16_t SavedY = Tug_OffsetY;
    Tug_OffsetX = 0;
    Tug_OffsetY = 0;
    Tug_DrawTugBoat(Target, &Body, Kind->Colour);
    Tug_OffsetX = SavedX;
    Tug_OffsetY = SavedY;
}

static void Tug_DrawListRow(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, bool Selected)
{
    if(Selected)
    {
        Render_FillRoundRect(Target, X, Y, Width, Height, 6U, TUG_COLOUR_PANEL_LIGHT);
        Render_Box(Target, X, (int16_t)(Y + 6), 4U, (uint16_t)(Height - 12U), TUG_COLOUR_TARGET);
    }
}

static void Tug_DrawBoard(Render_TargetTypeDef *Target)
{
    const int16_t Top = 40;
    const int16_t RowHeight = 82;
    const int16_t ListLeft = 26;
    const uint16_t ListWidth = 748U;
    char Text[64];
    char Deposit[24];
    char Pay[24];
    char Price[24];

    Tug_DrawPanel(Target, 12, Top, 776U, 430U);
    Render_DrawText(Target, &OpenSansBold20, "JOB BOARD", ListLeft + 8, (int16_t)(Top + 12), TUG_COLOUR_TEXT);
    Tug_FormatMoney(Text, sizeof(Text), Tug_Game.Career.Money);
    Render_DrawTextAligned(Target, &OpenSansBold20, Text, (int16_t)(ListLeft + (int16_t)ListWidth - 8), (int16_t)(Top + 12), RENDER_ALIGN_RIGHT, TUG_COLOUR_MONEY);

    for(uint8_t Index = 0U; Index <= Tug_Game.OfferCount; Index++)
    {
        const int16_t Y = (int16_t)(Top + 46 + (Index * RowHeight));
        Tug_DrawListRow(Target, ListLeft, Y, ListWidth, (uint16_t)(RowHeight - 6), Index == Tug_Game.Selection);
        if(Index < Tug_Game.OfferCount)
        {
            const Tug_OfferTypeDef *Offer = &Tug_Game.Offers[Index];
            const Tug_CargoTypeDef *Kind = &Tug_CargoTypes[Offer->Cargo];
            const uint32_t Metres = (uint32_t)Tug_RouteTiles[Offer->From][Offer->To] * TUG_METRES_PER_TILE;

            Tug_DrawCargoIcon(Target, Offer->Cargo, (int16_t)(ListLeft + 52), (int16_t)(Y + 38));
            Render_DrawText(Target, (Render_TextWidth(&OpenSansBold20, Kind->Name) > 250U) ? &OpenSansBold16 : &OpenSansBold20, Kind->Name, (int16_t)(ListLeft + 102), (int16_t)(Y + 8), TUG_COLOUR_TEXT);
            (void)Render_FormatText(Text, sizeof(Text), "%s TO %s", Tug_Docks[Offer->From].Name, Tug_Docks[Offer->To].Name);
            Render_DrawText(Target, &OpenSans12, Text, (int16_t)(ListLeft + 102), (int16_t)(Y + 36), TUG_COLOUR_TEXT_MUTED);
            (void)Render_FormatText(Text, sizeof(Text), "ROUTE %u.%u KM   %s", (unsigned int)(Metres / 1000U), (unsigned int)((Metres % 1000U) / 100U), Tug_WeatherName(Offer->Weather));
            Render_DrawText(Target, &OpenSansBold12, Text, (int16_t)(ListLeft + 102), (int16_t)(Y + 54), TUG_COLOUR_TEXT);
            if(Kind->Fragility >= 2.4f)
            {
                Render_DrawText(Target, &OpenSansBold12, "FRAGILE", (int16_t)(ListLeft + 108 + (int16_t)Render_TextWidth(&OpenSansBold12, Text) + 12), (int16_t)(Y + 54), TUG_COLOUR_DANGER);
            }
            Tug_FormatMoney(Pay, sizeof(Pay), (int32_t)Offer->Pay);
            if(Offer->Deposit == 0U)
            {
                Render_DrawTextAligned(Target, &OpenSansBold16, "NO DEPOSIT", (int16_t)(ListLeft + (int16_t)ListWidth - 12), (int16_t)(Y + 8), RENDER_ALIGN_RIGHT, TUG_COLOUR_TEXT_MUTED);
            }
            else
            {
                Tug_FormatMoney(Deposit, sizeof(Deposit), (int32_t)Offer->Deposit);
                (void)Render_FormatText(Text, sizeof(Text), "DEPOSIT %s", Deposit);
                Render_DrawTextAligned(Target, &OpenSansBold16, Text, (int16_t)(ListLeft + (int16_t)ListWidth - 12), (int16_t)(Y + 8), RENDER_ALIGN_RIGHT, TUG_COLOUR_DANGER);
            }
            (void)Render_FormatText(Text, sizeof(Text), "PAYS UP TO %s", Pay);
            Render_DrawTextAligned(Target, &OpenSansBold16, Text, (int16_t)(ListLeft + (int16_t)ListWidth - 12), (int16_t)(Y + 30), RENDER_ALIGN_RIGHT, TUG_COLOUR_MONEY);
            (void)Render_FormatText(Text, sizeof(Text), "+20%% IF TOWED IN %u:%02u", (unsigned int)(Offer->BonusSeconds / 60U), (unsigned int)(Offer->BonusSeconds % 60U));
            Render_DrawTextAligned(Target, &OpenSansBold12, Text, (int16_t)(ListLeft + (int16_t)ListWidth - 12), (int16_t)(Y + 54), RENDER_ALIGN_RIGHT, TUG_COLOUR_TARGET);
        }
        else
        {
            uint8_t Next = TUG_MODEL_COUNT;
            for(uint8_t Model = 0U; Model < TUG_MODEL_COUNT; Model++)
            {
                if((Next == TUG_MODEL_COUNT) && ((Tug_Game.Career.OwnedModels & (1U << Model)) == 0U))
                {
                    Next = Model;
                }
            }
            Tug_DrawTugIcon(Target, (Next < TUG_MODEL_COUNT) ? Next : Tug_Game.Career.Model, (int16_t)(ListLeft + 52), (int16_t)(Y + 38));
            Render_DrawText(Target, &OpenSansBold20, "VISIT THE BOATYARD", (int16_t)(ListLeft + 102), (int16_t)(Y + 8), TUG_COLOUR_TEXT);
            if(Next < TUG_MODEL_COUNT)
            {
                Tug_FormatMoney(Price, sizeof(Price), (int32_t)Tug_Models[Next].Price);
                (void)Render_FormatText(Text, sizeof(Text), "TUGS AND UPGRADES - NEXT TUG: %s, %s", Tug_Models[Next].Name, Price);
            }
            else
            {
                (void)Render_FormatText(Text, sizeof(Text), "CHANGE TUGS OR BUY UPGRADES");
            }
            Render_DrawText(Target, &OpenSans12, Text, (int16_t)(ListLeft + 102), (int16_t)(Y + 36), TUG_COLOUR_TEXT_MUTED);
        }
    }

    Render_DrawTextAligned(Target, &OpenSans12, "RIGHT SLIDER: CHOOSE      BUMPER: ACCEPT", (int16_t)(ListLeft + (int16_t)(ListWidth / 2U)), (int16_t)(Top + 410), RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
}

/* Where each tug is moored in the marina: five along the top of the pontoon, four below. */
static void Tug_MarinaBerth(uint8_t Model, float *X, float *Y, float *Heading)
{
    if(Model < 5U)
    {
        *X = 120.0f + ((float)Model * 140.0f);
        *Y = 178.0f;
        *Heading = PI_F;
    }
    else
    {
        *X = 190.0f + ((float)(Model - 5U) * 140.0f);
        *Y = 318.0f;
        *Heading = 0.0f;
    }
}

static void Tug_DrawHoldBar(Render_TargetTypeDef *Target, int16_t X, int16_t Y, Controls_ButtonTypeDef Button, bool Used, uint8_t Colour)
{
    const uint32_t Held = (Controls_IsDown(Button) && !Used) ? Controls_HeldMilliseconds(Button) : 0U;
    Render_Box(Target, X, Y, 120U, 4U, TUG_COLOUR_PANEL_EDGE);
    if(Held > 100U)
    {
        Render_Box(Target, X, Y, (uint16_t)((Held >= 700U) ? 120U : ((Held * 120U) / 700U)), 4U, Colour);
    }
}

/*
 * The boatyard's marina: every tug in the game moored along a pontoon,
 * the picked one ringed, and its details below.
 */
/* The boatyard's bank and sheds along the top, the page's title and the money to spend. */
static void Tug_DrawYardTop(Render_TargetTypeDef *Target, const char *Title)
{
    char Text[24];
    Render_Box(Target, 0, 0, RENDER_WIDTH, TUG_HUD_HEIGHT, TUG_COLOUR_MEADOW);
    Render_Box(Target, 0, TUG_HUD_HEIGHT, RENDER_WIDTH, 56U, TUG_COLOUR_MEADOW);
    Render_Box(Target, 0, (int16_t)(TUG_HUD_HEIGHT + 44), RENDER_WIDTH, 14U, TUG_COLOUR_TOWPATH);
    Render_Box(Target, 0, (int16_t)(TUG_HUD_HEIGHT + 56), RENDER_WIDTH, 4U, TUG_COLOUR_BANK);
    Tug_Building(Target, 40.0f, 30.0f, 120, 36, TUG_COLOUR_SHED, TUG_COLOUR_SHED_DARK);
    Tug_Building(Target, 600.0f, 30.0f, 150, 36, TUG_COLOUR_ROOF_RED, TUG_COLOUR_ROOF_RED_DARK);
    Render_FillRoundRect(Target, 280, 34, 240U, 26U, 6U, TUG_COLOUR_PANEL);
    Render_DrawTextAligned(Target, &OpenSansBold16, Title, 400, 38, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
    Tug_FormatMoney(Text, sizeof(Text), Tug_Game.Career.Money);
    Render_FillRoundRect(Target, 330, 4, 140U, 24U, 6U, TUG_COLOUR_PANEL);
    Render_DrawTextAligned(Target, &OpenSansBold16, Text, 400, 7, RENDER_ALIGN_CENTRE, TUG_COLOUR_MONEY);
}

/* A picture of each upgrade, centred on X, Y, small enough for its stand. */
static void Tug_DrawUpgradeIcon(Render_TargetTypeDef *Target, uint8_t Bit, int16_t X, int16_t Y, uint32_t Clock)
{
    const Tug_BodyTypeDef Barge = { (float)X, (float)Y, 0.0f, 0.0f, PI_F / 2.0f, 0.0f, 72.0f, 30.0f };

    switch(Bit)
    {
        case TUG_UPGRADE_SOFT_FENDERS:
            Tug_DrawBargeHull(Target, &Barge, TUG_COLOUR_BARGE_DARK, TUG_COLOUR_BARGE);
            for(float Along = -26.0f; Along < 26.0f; Along += 12.0f)
            {
                Tug_BodyCircle(Target, &Barge, Along, -15.0f, 5U, TUG_COLOUR_FENDER);
                Tug_BodyCircle(Target, &Barge, Along, 15.0f, 5U, TUG_COLOUR_FENDER);
                Tug_BodyCircle(Target, &Barge, Along, -15.0f, 2U, TUG_COLOUR_BLACK);
                Tug_BodyCircle(Target, &Barge, Along, 15.0f, 2U, TUG_COLOUR_BLACK);
            }
            break;

        case TUG_UPGRADE_WEATHER_COVERS:
            Tug_DrawBargeHull(Target, &Barge, TUG_COLOUR_BARGE_DARK, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, &Barge, -32.0f, 26.0f, -12.0f, 12.0f, TUG_COLOUR_TUG_GREEN);
            for(float Along = -22.0f; Along < 26.0f; Along += 14.0f)
            {
                Tug_DrawPlate(Target, &Barge, Along, Along + 2.0f, -13.0f, 13.0f, TUG_COLOUR_ROPE);
            }
            break;

        case TUG_UPGRADE_CARGO_THRUSTERS:
        {
            const Tug_BodyTypeDef Crate = { (float)X, (float)Y, 0.0f, 0.0f, PI_F / 2.0f, 0.0f, 60.0f, 30.0f };
            Tug_DrawPlate(Target, &Crate, -24.0f, 24.0f, -11.0f, 11.0f, TUG_COLOUR_BARGE);
            Tug_DrawCargoThrusters(Target, &Crate, (int8_t)(((Clock / 700U) % 3U) - 1U), Clock);
            break;
        }

        case TUG_UPGRADE_KEEL_GUARDS:
            Tug_DrawBargeHull(Target, &Barge, TUG_COLOUR_BARGE_DARK, TUG_COLOUR_BARGE);
            Tug_DrawPlate(Target, &Barge, -36.0f, 28.0f, -15.0f, -11.0f, TUG_COLOUR_STEEL);
            Tug_DrawPlate(Target, &Barge, -36.0f, 28.0f, 11.0f, 15.0f, TUG_COLOUR_STEEL);
            for(float Along = -30.0f; Along < 28.0f; Along += 10.0f)
            {
                Tug_BodyCircle(Target, &Barge, Along, -13.0f, 1U, TUG_COLOUR_TUG_CHARCOAL);
                Tug_BodyCircle(Target, &Barge, Along, 13.0f, 1U, TUG_COLOUR_TUG_CHARCOAL);
            }
            break;

        case TUG_UPGRADE_TURBO:
            Render_FillRoundRect(Target, (int16_t)(X - 30), (int16_t)(Y - 16), 60U, 32U, 4U, TUG_COLOUR_TUG_CHARCOAL);
            Render_Box(Target, (int16_t)(X - 24), (int16_t)(Y - 12), 48U, 6U, TUG_COLOUR_STEEL);
            for(int16_t Pipe = -18; Pipe <= 18; Pipe += 18)
            {
                Render_FillCircle(Target, (int16_t)(X + Pipe), (int16_t)(Y + 6), 4U, TUG_COLOUR_TUG_ORANGE);
            }
            Render_FillCircle(Target, (int16_t)(X + 30), (int16_t)(Y - 18 - (int16_t)((Clock / 120U) % 8U)), 4U, TUG_COLOUR_SMOKE);
            break;

        case TUG_UPGRADE_TRADE_LICENCE:
        default:
            Render_FillRoundRect(Target, (int16_t)(X - 28), (int16_t)(Y - 22), 56U, 44U, 3U, TUG_COLOUR_WHITE);
            for(int16_t Line = 0; Line < 4; Line++)
            {
                Render_Box(Target, (int16_t)(X - 20), (int16_t)(Y - 14 + (Line * 7)), (uint16_t)((Line == 3) ? 18U : 40U), 2U, TUG_COLOUR_STONE_DARK);
            }
            Render_Box(Target, (int16_t)(X + 10), (int16_t)(Y + 10), 6U, 14U, TUG_COLOUR_CAR_RED);
            Render_FillCircle(Target, (int16_t)(X + 13), (int16_t)(Y + 10), 7U, TUG_COLOUR_GOLD);
            break;
    }
}

/* Where each upgrade's stand is in the yard: four along the back, the rest centred in front. */
static void Tug_UpgradeStand(uint8_t Index, int16_t *X, int16_t *Y)
{
    if(Index < 4U)
    {
        *X = (int16_t)(130 + (Index * 180));
        *Y = 178;
    }
    else
    {
        *X = (int16_t)(400 - (((int16_t)TUG_UPGRADE_COUNT - 5) * 90) + ((int16_t)(Index - 4U) * 180));
        *Y = 310;
    }
}

/* The boatyard's second page: every upgrade on a stand in the yard, with its price or whether it's bought. */
static void Tug_DrawUpgradeYard(Render_TargetTypeDef *Target)
{
    const uint8_t Picked = (uint8_t)(Tug_Game.Selection - TUG_MODEL_COUNT);
    const Tug_UpgradeTypeDef *Chosen = &Tug_Upgrades[Picked];
    const bool ChosenOwned = (Tug_Game.Career.Upgrades & Chosen->Bit) != 0U;
    const bool CanBuy = (int32_t)Chosen->Price <= Tug_Game.Career.Money;
    const uint32_t Clock = Tug_Game.World.Milliseconds;
    char Text[48];
    char Price[24];

    Tug_OffsetX = 0;
    Tug_OffsetY = 0;
    Tug_Clock = Clock;

    /* The paved yard, then the bank and sheds along the top. */
    Render_Box(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT, TUG_COLOUR_YARD);
    for(int16_t Y = 120; Y < 380; Y += 40)
    {
        Render_Box(Target, 0, Y, RENDER_WIDTH, 1U, TUG_COLOUR_YARD_DARK);
    }
    for(int16_t X = 0; X < 800; X += 80)
    {
        Render_Box(Target, X, 86, 1U, 296U, TUG_COLOUR_YARD_DARK);
    }
    Tug_DrawYardTop(Target, "BOATYARD: UPGRADES");

    for(uint8_t Index = 0U; Index < TUG_UPGRADE_COUNT; Index++)
    {
        const Tug_UpgradeTypeDef *Upgrade = &Tug_Upgrades[Index];
        const bool Owned = (Tug_Game.Career.Upgrades & Upgrade->Bit) != 0U;
        int16_t X;
        int16_t Y;
        Tug_UpgradeStand(Index, &X, &Y);

        if(Index == Picked)
        {
            Render_DrawRect(Target, (int16_t)(X - 70), (int16_t)(Y - 48), 140U, 96U, 3U, ((Clock / 300U) & 1U) ? TUG_COLOUR_TARGET : TUG_COLOUR_WHITE);
        }

        /* A wooden stand with a pool of water, so the parts are shown afloat. */
        Render_FillRoundRect(Target, (int16_t)(X - 62), (int16_t)(Y - 40), 124U, 80U, 6U, TUG_COLOUR_WOOD_DARK);
        Render_FillRoundRect(Target, (int16_t)(X - 58), (int16_t)(Y - 36), 116U, 72U, 4U, TUG_COLOUR_WATER);
        Tug_DrawUpgradeIcon(Target, Upgrade->Bit, X, Y, Clock);

        Render_DrawTextAligned(Target, &OpenSansBold12, Upgrade->Name, X, (int16_t)(Y - 60), RENDER_ALIGN_CENTRE, TUG_COLOUR_BLACK);
        if(Owned)
        {
            (void)Render_FormatText(Text, sizeof(Text), "OWNED");
        }
        else
        {
            Tug_FormatMoney(Text, sizeof(Text), (int32_t)Upgrade->Price);
        }
        {
            const uint16_t Width = (uint16_t)(Render_TextWidth(&OpenSansBold12, Text) + 12U);
            Render_FillRoundRect(Target, (int16_t)(X - (int16_t)(Width / 2U)), (int16_t)(Y + 46), Width, 18U, 4U, TUG_COLOUR_PANEL);
            Render_DrawTextAligned(Target, &OpenSansBold12, Text, X, (int16_t)(Y + 48), RENDER_ALIGN_CENTRE,
                                   Owned ? TUG_COLOUR_TEXT : (((int32_t)Upgrade->Price <= Tug_Game.Career.Money) ? TUG_COLOUR_MONEY : TUG_COLOUR_DANGER));
        }
    }

    /* The picked upgrade's details and what the buttons do. */
    Tug_DrawPanel(Target, 20, 382, 760U, 92U);
    Render_DrawText(Target, &OpenSansBold20, Chosen->Name, 40, 390, TUG_COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans12, Chosen->What, 40, 418, TUG_COLOUR_TEXT_MUTED);
    Render_DrawText(Target, &OpenSans12, Chosen->Does, 40, 436, TUG_COLOUR_TEXT);
    Tug_FormatMoney(Price, sizeof(Price), (int32_t)Chosen->Price);
    (void)Render_FormatText(Text, sizeof(Text), ChosenOwned ? "OWNED" : (CanBuy ? "%s - HOLD PRIMARY TO BUY" : "%s - NOT ENOUGH MONEY"), Price);
    Render_DrawTextAligned(Target, &OpenSansBold16, Text, 760, 392, RENDER_ALIGN_RIGHT, (ChosenOwned || CanBuy) ? TUG_COLOUR_MONEY : TUG_COLOUR_DANGER);
    Render_DrawTextAligned(Target, &OpenSans12, "PRIMARY: NEXT   SECONDARY: BACK   HOLD SECONDARY: LEAVE", 760, 420, RENDER_ALIGN_RIGHT, TUG_COLOUR_TEXT_MUTED);
    Tug_DrawHoldBar(Target, 500, 446, CONTROLS_PRIMARY, Tug_Game.PrimaryHoldDone, TUG_COLOUR_MONEY);
    Tug_DrawHoldBar(Target, 640, 446, CONTROLS_SECONDARY, Tug_Game.SecondaryHoldDone, TUG_COLOUR_DANGER);
    Render_DrawText(Target, &OpenSans12, "BUY", 500, 452, TUG_COLOUR_TEXT_MUTED);
    Render_DrawText(Target, &OpenSans12, "LEAVE", 640, 452, TUG_COLOUR_TEXT_MUTED);
}

/* The boatyard's first page: every tug at its berth on the pontoon. */
static void Tug_DrawShop(Render_TargetTypeDef *Target)
{
    const uint8_t Picked = Tug_Game.Selection;
    const Tug_ModelTypeDef *Model = &Tug_Models[(Picked < TUG_MODEL_COUNT) ? Picked : 0U];
    const bool Owned = (Picked < TUG_MODEL_COUNT) && ((Tug_Game.Career.OwnedModels & (1U << Picked)) != 0U);
    const bool Affordable = (int32_t)Model->Price <= Tug_Game.Career.Money;
    const uint32_t Clock = Tug_Game.World.Milliseconds;
    char Text[48];

    if(Picked >= TUG_MODEL_COUNT)
    {
        Tug_DrawUpgradeYard(Target);
        return;
    }

    Tug_OffsetX = 0;
    Tug_OffsetY = 0;
    Tug_Clock = Clock;

    /* Water, with a bank and the boatyard's sheds along the top, and the money to spend. */
    Render_Box(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT, TUG_COLOUR_WATER);
    for(int16_t Y = 100; Y < 480; Y += 20)
    {
        for(int16_t X = 0; X < 800; X += 20)
        {
            const uint32_t Hash = Tug_TileHash(X, Y);
            if((Hash % 7U) == 0U)
            {
                const int16_t Shift = (int16_t)lrintf(3.0f * sinf(((float)Clock * 0.0015f) + (float)(Hash % 628U) * 0.01f));
                Render_Box(Target, (int16_t)(X + 4 + Shift), (int16_t)(Y + 8), 7U, 1U, TUG_COLOUR_WATER_GLINT);
            }
        }
    }
    Tug_DrawYardTop(Target, "BOATYARD: TUGS");

    /* The pontoon: a walkway along the middle with fingers between the berths. */
    Render_Box(Target, 30, 248, 740U, 16U, TUG_COLOUR_WOOD);
    for(int16_t X = 30; X < 770; X += 8)
    {
        Render_Box(Target, X, 248, 1U, 16U, TUG_COLOUR_WOOD_DARK);
    }
    for(uint8_t Finger = 0U; Finger < 6U; Finger++)
    {
        Render_Box(Target, (int16_t)(46 + (Finger * 140)), 120, 8U, 128U, TUG_COLOUR_WOOD);
    }
    for(uint8_t Finger = 0U; Finger < 5U; Finger++)
    {
        Render_Box(Target, (int16_t)(116 + (Finger * 140)), 264, 8U, 108U, TUG_COLOUR_WOOD);
    }

    /* Every tug at its berth, with its price or whether it's yours. */
    for(uint8_t Index = 0U; Index < TUG_MODEL_COUNT; Index++)
    {
        const Tug_ModelTypeDef *Kind = &Tug_Models[Index];
        const bool Mine = (Tug_Game.Career.OwnedModels & (1U << Index)) != 0U;
        float X;
        float Y;
        float Heading;
        Tug_BodyTypeDef Body;
        Tug_MarinaBerth(Index, &X, &Y, &Heading);
        Body.X = X;
        Body.Y = Y;
        Body.VelocityX = 0.0f;
        Body.VelocityY = 0.0f;
        Body.Heading = Heading;
        Body.Spin = 0.0f;
        Body.Length = Kind->Length;
        Body.Width = Kind->Width;
        if(Index == Picked)
        {
            Render_DrawCircle(Target, (int16_t)X, (int16_t)Y, (uint16_t)(58U + ((Clock / 90U) % 5U)), 3U, ((Clock / 300U) & 1U) ? TUG_COLOUR_TARGET : TUG_COLOUR_WHITE);
        }
        Tug_DrawTugBoat(Target, &Body, Kind->Colour);
        if(Index == Tug_Game.Career.Model)
        {
            (void)Render_FormatText(Text, sizeof(Text), "IN USE");
        }
        else if(Mine)
        {
            (void)Render_FormatText(Text, sizeof(Text), "OWNED");
        }
        else
        {
            Tug_FormatMoney(Text, sizeof(Text), (int32_t)Kind->Price);
        }
        {
            const uint16_t Width = (uint16_t)(Render_TextWidth(&OpenSansBold12, Text) + 12U);
            const int16_t TagY = (Index < 5U) ? 112 : 352;
            Render_FillRoundRect(Target, (int16_t)(lrintf(X) - (int16_t)(Width / 2U)), TagY, Width, 18U, 4U, TUG_COLOUR_PANEL);
            Render_DrawTextAligned(Target, &OpenSansBold12, Text, (int16_t)lrintf(X), (int16_t)(TagY + 2), RENDER_ALIGN_CENTRE,
                                   (Index == Tug_Game.Career.Model) ? TUG_COLOUR_TARGET : (Mine ? TUG_COLOUR_TEXT : (((int32_t)Kind->Price <= Tug_Game.Career.Money) ? TUG_COLOUR_MONEY : TUG_COLOUR_DANGER)));
        }
    }

    /* The picked item's details and what the buttons do. */
    Tug_DrawPanel(Target, 20, 382, 760U, 92U);
    Render_DrawText(Target, &OpenSansBold20, Model->Name, 40, 390, TUG_COLOUR_TEXT);
    (void)Render_FormatText(Text, sizeof(Text), "TOWS CLASS %u CARGO", (unsigned int)Model->Class);
    Render_DrawText(Target, &OpenSans12, Text, 40, 418, TUG_COLOUR_TEXT_MUTED);
    Render_DrawText(Target, &OpenSans12, "POWER", 40, 438, TUG_COLOUR_TEXT_MUTED);
    Render_Box(Target, 90, 442, 120U, 6U, TUG_COLOUR_PANEL_EDGE);
    Render_Box(Target, 90, 442, (uint16_t)lrintf(120.0f * Model->Thrust * sqrtf(Model->Mass) / (104.0f * 2.65f)), 6U, TUG_COLOUR_TARGET);
    if(Picked == Tug_Game.Career.Model)
    {
        (void)Render_FormatText(Text, sizeof(Text), "IN USE");
    }
    else if(Owned)
    {
        (void)Render_FormatText(Text, sizeof(Text), "OWNED - HOLD PRIMARY TO USE");
    }
    else
    {
        char Price[24];
        Tug_FormatMoney(Price, sizeof(Price), (int32_t)Model->Price);
        (void)Render_FormatText(Text, sizeof(Text), Affordable ? "%s - HOLD PRIMARY TO BUY" : "%s - NOT ENOUGH MONEY", Price);
    }
    Render_DrawTextAligned(Target, &OpenSansBold16, Text, 760, 392, RENDER_ALIGN_RIGHT,
                           (Picked == Tug_Game.Career.Model) ? TUG_COLOUR_TARGET : ((Owned || Affordable) ? TUG_COLOUR_MONEY : TUG_COLOUR_DANGER));
    Render_DrawTextAligned(Target, &OpenSans12, "PRIMARY: NEXT   SECONDARY: BACK   HOLD SECONDARY: LEAVE", 760, 420, RENDER_ALIGN_RIGHT, TUG_COLOUR_TEXT_MUTED);
    Tug_DrawHoldBar(Target, 500, 446, CONTROLS_PRIMARY, Tug_Game.PrimaryHoldDone, TUG_COLOUR_MONEY);
    Tug_DrawHoldBar(Target, 640, 446, CONTROLS_SECONDARY, Tug_Game.SecondaryHoldDone, TUG_COLOUR_DANGER);
    Render_DrawText(Target, &OpenSans12, "BUY / USE", 500, 452, TUG_COLOUR_TEXT_MUTED);
    Render_DrawText(Target, &OpenSans12, "LEAVE", 640, 452, TUG_COLOUR_TEXT_MUTED);
}

static void Tug_DrawResult(Render_TargetTypeDef *Target)
{
    const int16_t Centre = (int16_t)(RENDER_WIDTH / 2U);
    char Amount[24];
    char Other[24];
    char Text[64];

    Tug_DrawPanel(Target, 210, 124, 380U, 244U);
    if(Tug_Game.Delivered)
    {
        Render_DrawTextAligned(Target, &OpenSansBold28, "DELIVERED!", Centre, 138, RENDER_ALIGN_CENTRE, TUG_COLOUR_MONEY);
        Tug_FormatMoney(Amount, sizeof(Amount), Tug_Game.ResultAmount);
        (void)Render_FormatText(Text, sizeof(Text), "+%s", Amount);
        Render_DrawTextAligned(Target, &OpenSansBold28, Text, Centre, 180, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
        Tug_FormatMoney(Amount, sizeof(Amount), Tug_Game.ResultPay);
        if(Tug_Game.Job.Deposit > 0U)
        {
            Tug_FormatMoney(Other, sizeof(Other), Tug_Game.ResultDeposit);
            (void)Render_FormatText(Text, sizeof(Text), "PAY %s   DEPOSIT BACK %s", Amount, Other);
        }
        else
        {
            (void)Render_FormatText(Text, sizeof(Text), "PAY %s", Amount);
        }
        Render_DrawTextAligned(Target, &OpenSans12, Text, Centre, 218, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
        if(Tug_Game.ResultBonus > 0)
        {
            Tug_FormatMoney(Amount, sizeof(Amount), Tug_Game.ResultBonus);
            (void)Render_FormatText(Text, sizeof(Text), "ON TIME BONUS +%s", Amount);
            Render_DrawTextAligned(Target, &OpenSansBold12, Text, Centre, 236, RENDER_ALIGN_CENTRE, TUG_COLOUR_TARGET);
        }
        (void)Render_FormatText(Text, sizeof(Text), "CARGO CONDITION %u%%", (unsigned int)lrintf(fmaxf(Tug_Game.World.Cargo.Condition, 0.0f)));
        Render_DrawTextAligned(Target, &OpenSans16, Text, Centre, 258, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
        if(Tug_Game.ResultFavour >= 0)
        {
            (void)Render_FormatText(Text, sizeof(Text), "%s THINKS HIGHLY OF YOU", Tug_Docks[Tug_Game.ResultFavour].Name);
            Render_DrawTextAligned(Target, &OpenSans12, Text, Centre, 306, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
        }
    }
    else
    {
        Render_DrawTextAligned(Target, &OpenSansBold28, "CARGO WRECKED!", Centre, 146, RENDER_ALIGN_CENTRE, TUG_COLOUR_DANGER);
        Render_DrawTextAligned(Target, &OpenSansBold20, (Tug_Game.Job.Deposit > 0U) ? "DEPOSIT LOST" : "NO PAYMENT", Centre, 200, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
        Render_DrawTextAligned(Target, &OpenSans16, "HARD KNOCKS BREAK CARGO", Centre, 244, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT_MUTED);
    }
    Tug_FormatMoney(Amount, sizeof(Amount), Tug_Game.Career.Money);
    (void)Render_FormatText(Text, sizeof(Text), "BALANCE %s", Amount);
    Render_DrawTextAligned(Target, &OpenSans16, Text, Centre, 280, RENDER_ALIGN_CENTRE, TUG_COLOUR_MONEY);
    if(Tug_Game.ScreenMilliseconds >= 800U)
    {
        Render_DrawTextAligned(Target, &OpenSans16, "BUMPER: JOB BOARD", Centre, 334, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
    }
}

void Tug_DrawGame(Render_TargetTypeDef *Target)
{
    const Tug_WorldTypeDef *World = &Tug_Game.World;

    if(Tug_Game.Screen == TUG_SCREEN_SHOP)
    {
        Tug_DrawShop(Target);
        return;
    }
    Tug_DrawWorld(Target, World);
    if(Tug_Game.Screen == TUG_SCREEN_TITLE)
    {
        Tug_DrawTitle(Target);
        return;
    }

    if(Tug_Game.Screen == TUG_SCREEN_DRIVING)
    {
        Tug_DrawStatusBar(Target);
        Tug_DrawGauge(Target, 6, World->Tug.LeftEngine, Tug_Game.LeftThrottle);
        Tug_DrawGauge(Target, (int16_t)(RENDER_WIDTH - 6U - GAUGE_WIDTH), World->Tug.RightEngine, Tug_Game.RightThrottle);
        Tug_DrawPointer(Target, World);
        Tug_DrawGuidance(Target);
    }
    else if(Tug_Game.Screen == TUG_SCREEN_BOARD)
    {
        Tug_DrawBoard(Target);
    }
    else if(Tug_Game.Screen == TUG_SCREEN_RESULT)
    {
        Tug_DrawResult(Target);
    }
}

void Tug_DrawSplash(Render_TargetTypeDef *Target, Tug_WorldTypeDef *World)
{
    const Render_RectTypeDef Area = { APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y, APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT };

    Render_SetClipRect(&Area);
    Tug_DrawWorld(Target, World);
    Tug_DrawPanel(Target, 280, 76, 240U, 54U);
    Render_DrawTextAligned(Target, &OpenSansBold28, "CANAL TUG", (int16_t)(RENDER_WIDTH / 2U), 86, RENDER_ALIGN_CENTRE, TUG_COLOUR_TEXT);
    Render_ResetClipRect();
}
