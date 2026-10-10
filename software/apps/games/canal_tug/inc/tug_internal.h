/**
 * @file tug_internal.h
 * @brief Types, tuning and shared functions inside Canal Tug.
 *
 * Canal Tug: you run a tugboat on the canals of the English countryside.
 * Each slider is one of the tug's two engines: centre is idle, up is ahead,
 * down is astern. Pick a job from the board, back up to the cargo to hook it
 * on, tow it along the canals to the site that wants it and hold it still in
 * the ring there to deliver it.
 *
 * Every job pays by how well the cargo arrives. Free jobs cost nothing;
 * paid jobs cost money up front and pay back more, but nothing at all if the
 * cargo is wrecked. Earnings buy bigger tugs at the boatyard, which can tow
 * heavier cargo and have bow thrusters on the bumpers.
 */

#ifndef TUG_INTERNAL_H
#define TUG_INTERNAL_H

#include "render.h"
#include "tug_sites.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/*
 * The countryside is a grid of small tiles, many screens across; the view
 * scrolls. Its size and the number of sites come from tug_sites.h, which is
 * made with the map by tools/canal_tug_map/make_map.py.
 */
#define TUG_TILE_SIZE           (20)
#define TUG_WORLD_WIDTH         (TUG_MAP_WIDTH * TUG_TILE_SIZE)
#define TUG_WORLD_HEIGHT        (TUG_MAP_HEIGHT * TUG_TILE_SIZE)

#define TUG_SHIPYARD_DOCK       (TUG_AT_BOATYARD)
#define TUG_MODEL_COUNT         (9U)

/*
 * Upgrades the boatyard sells, each bought once and kept for good: a bit
 * each in the career's Upgrades. Their names, prices and descriptions are
 * in Tug_Upgrades (tug_catalogue.c).
 */
#define TUG_UPGRADE_CARGO_THRUSTERS (0x01U)
#define TUG_UPGRADE_SOFT_FENDERS    (0x02U)
#define TUG_UPGRADE_KEEL_GUARDS     (0x04U)
#define TUG_UPGRADE_WEATHER_COVERS  (0x08U)
#define TUG_UPGRADE_TURBO           (0x20U)
#define TUG_UPGRADE_TRADE_LICENCE   (0x40U)
#define TUG_UPGRADE_COUNT           (6U)

/* The boatyard's two pages: every tug, then every upgrade. */
#define TUG_SHOP_ITEM_COUNT         (TUG_MODEL_COUNT + TUG_UPGRADE_COUNT)

/*
 * Test mode: 1 starts every game with every tug and upgrade owned and no
 * deposits on jobs, and never saves, so the real career is left as it was.
 * Set it back to 0 for normal play.
 */
#define TUG_TEST_MODE               (1)
#define TUG_TEST_MONEY              (1000000)
#define TUG_CARGO_TYPE_COUNT    (28U)
#define TUG_OFFER_COUNT         (3U)
#define TUG_MAX_PEOPLE          (30U)
#define TUG_MAX_SEGMENTS        (96U)
#define TUG_MAX_WAKE            (64U)
#define TUG_WAKE_LIFE_MS        (1600U)

/* Moored narrowboats, as obstacles. */
#define TUG_MOORED_LENGTH       (84.0f)
#define TUG_MOORED_WIDTH        (18.0f)

/* The status bar across the top of the screen. */
#define TUG_HUD_HEIGHT          (26)

/* Route lengths on the job board count each tile as this many metres. */
#define TUG_METRES_PER_TILE     (5U)

/* Which button is the left bumper and which is the right. Swap them if they're the wrong way round. */
#define TUG_LEFT_BUMPER         (CONTROLS_SECONDARY)
#define TUG_RIGHT_BUMPER        (CONTROLS_PRIMARY)

/*
 * Map tiles as the game sees them. tug_city.c stores only water ('~'),
 * shallows, buoys, lanes, houses, yards and open country ('G'); the rest is
 * worked out as each row is read. Water is '0' (beside the bank) to '3'
 * (well out), by depth.
 */
#define TUG_TILE_STORED_WATER   ('~')
#define TUG_TILE_WATER_FIRST    ('0')
#define TUG_TILE_WATER_LAST     ('3')
#define TUG_TILE_SHALLOWS       ('s')
#define TUG_TILE_BUOY           ('o')
#define TUG_TILE_TOWPATH        ('p')
#define TUG_TILE_LANE           ('r')
#define TUG_TILE_HOUSES         ('H')
#define TUG_TILE_MEADOW         ('G')
#define TUG_TILE_FIELD          ('F')
#define TUG_TILE_WHEAT          ('W')
#define TUG_TILE_WOODS          ('T')
#define TUG_TILE_HEDGE          ('h')
#define TUG_TILE_YARD           ('Q')

/* -------------------------------------------------------------------------- */
/* Palette                                                                    */
/* -------------------------------------------------------------------------- */

typedef enum
{
    TUG_COLOUR_BLACK = 0,
    TUG_COLOUR_WHITE,
    TUG_COLOUR_WATER_DEEP,
    TUG_COLOUR_WATER,
    TUG_COLOUR_WATER_EDGE,
    TUG_COLOUR_WATER_GLINT,
    TUG_COLOUR_SHALLOWS,
    TUG_COLOUR_SHALLOWS_DARK,
    TUG_COLOUR_FOAM,
    TUG_COLOUR_FOAM_FADED,
    TUG_COLOUR_SHADOW,
    TUG_COLOUR_BANK,
    TUG_COLOUR_TOWPATH,
    TUG_COLOUR_TOWPATH_DARK,
    TUG_COLOUR_LANE,
    TUG_COLOUR_VERGE,
    TUG_COLOUR_MEADOW,
    TUG_COLOUR_MEADOW_DARK,
    TUG_COLOUR_MEADOW_LIGHT,
    TUG_COLOUR_FIELD,
    TUG_COLOUR_FIELD_DARK,
    TUG_COLOUR_WHEAT,
    TUG_COLOUR_WHEAT_DARK,
    TUG_COLOUR_HEDGE,
    TUG_COLOUR_HEDGE_LIGHT,
    TUG_COLOUR_TREE,
    TUG_COLOUR_TREE_LIGHT,
    TUG_COLOUR_TREE_DARK,
    TUG_COLOUR_FLOWER,
    TUG_COLOUR_ROOF_RED,
    TUG_COLOUR_ROOF_RED_DARK,
    TUG_COLOUR_ROOF_SLATE,
    TUG_COLOUR_ROOF_SLATE_DARK,
    TUG_COLOUR_THATCH,
    TUG_COLOUR_THATCH_DARK,
    TUG_COLOUR_STONE,
    TUG_COLOUR_STONE_DARK,
    TUG_COLOUR_CHIMNEY,
    TUG_COLOUR_YARD,
    TUG_COLOUR_YARD_DARK,
    TUG_COLOUR_SHED,
    TUG_COLOUR_SHED_DARK,
    TUG_COLOUR_HAZARD,
    TUG_COLOUR_WOOD,
    TUG_COLOUR_WOOD_DARK,
    TUG_COLOUR_LOG,
    TUG_COLOUR_LOG_END,
    TUG_COLOUR_BRICK,
    TUG_COLOUR_COPPER,
    TUG_COLOUR_SMOKE,
    TUG_COLOUR_CAR_RED,
    TUG_COLOUR_CAR_BLUE,
    TUG_COLOUR_CAR_YELLOW,
    TUG_COLOUR_CAR_WHITE,
    TUG_COLOUR_CAR_GREEN,
    TUG_COLOUR_GLASS,
    TUG_COLOUR_PCB,
    TUG_COLOUR_PCB_GOLD,
    TUG_COLOUR_TUG_RED,
    TUG_COLOUR_TUG_YELLOW,
    TUG_COLOUR_TUG_GREEN,
    TUG_COLOUR_TUG_BROWN,
    TUG_COLOUR_TUG_BLUE,
    TUG_COLOUR_TUG_ORANGE,
    TUG_COLOUR_TUG_CHARCOAL,
    TUG_COLOUR_TUG_NAVY,
    TUG_COLOUR_TUG_TEAL,
    TUG_COLOUR_TUG_DECK,
    TUG_COLOUR_TUG_CABIN,
    TUG_COLOUR_FENDER,
    TUG_COLOUR_ROPE,
    TUG_COLOUR_BARGE,
    TUG_COLOUR_BARGE_DARK,
    TUG_COLOUR_GRAVEL,
    TUG_COLOUR_COAL,
    TUG_COLOUR_HAY,
    TUG_COLOUR_STEEL,
    TUG_COLOUR_RUST,
    TUG_COLOUR_CONTAINER_A,
    TUG_COLOUR_CONTAINER_B,
    TUG_COLOUR_CONTAINER_C,
    TUG_COLOUR_YACHT,
    TUG_COLOUR_YACHT_TRIM,
    TUG_COLOUR_ROYAL,
    TUG_COLOUR_GOLD,
    TUG_COLOUR_NUCLEAR,
    TUG_COLOUR_TANK_ORANGE,
    TUG_COLOUR_BONE,
    TUG_COLOUR_NARROWBOAT_A,
    TUG_COLOUR_NARROWBOAT_B,
    TUG_COLOUR_NARROWBOAT_C,
    TUG_COLOUR_NARROWBOAT_D,
    TUG_COLOUR_BUOY,
    TUG_COLOUR_PANEL,
    TUG_COLOUR_PANEL_EDGE,
    TUG_COLOUR_PANEL_LIGHT,
    TUG_COLOUR_TEXT,
    TUG_COLOUR_TEXT_MUTED,
    TUG_COLOUR_TARGET,
    TUG_COLOUR_MONEY,
    TUG_COLOUR_DANGER,
    TUG_COLOUR_COUNT
} Tug_ColourTypeDef;

/* -------------------------------------------------------------------------- */
/* Catalogues                                                                 */
/* -------------------------------------------------------------------------- */

/* How a site's buildings are drawn. */
typedef enum
{
    TUG_SITE_BOATYARD = 0,
    TUG_SITE_LUMBER,
    TUG_SITE_QUARRY,
    TUG_SITE_TOWN,
    TUG_SITE_STEEL,
    TUG_SITE_CAR_FACTORY,
    TUG_SITE_BREWERY,
    TUG_SITE_DEALER,
    TUG_SITE_SPACE,
    TUG_SITE_FARM,
    TUG_SITE_PCB,
    TUG_SITE_USOLDER,
    TUG_SITE_MUSEUM,
    TUG_SITE_COLLIERY,
    TUG_SITE_POWER,
    TUG_SITE_CHEMICAL,
    TUG_SITE_NUCLEAR,
    TUG_SITE_MARINA,
    TUG_SITE_ROCKET,
    TUG_SITE_WINDFARM,
    TUG_SITE_CASTLE
} Tug_SiteLookTypeDef;

/* A site on the bank: its dock (a water tile) and where its buildings stand. */
typedef struct
{
    const char *Name;
    uint16_t X;                 /* the dock's water tile */
    uint16_t Y;
    int16_t Heading;            /* the way the waterway runs there, thousandths of a radian */
    int16_t SiteX;              /* pixels */
    int16_t SiteY;
    Tug_SiteLookTypeDef Look;
} Tug_DockTypeDef;

typedef struct
{
    int16_t X;                  /* pixels */
    int16_t Y;
    int16_t Heading;            /* thousandths of a radian */
    uint8_t Paint;
} Tug_MooredBoatTypeDef;

/* A tug the boatyard sells. */
typedef struct
{
    const char *Name;
    uint32_t Price;
    uint8_t Class;              /* tows cargo of this class and below */
    float Thrust;               /* each engine at full ahead, pixels per second squared */
    float Mass;                 /* against the cargo's, for how hard it is to tow */
    float Length;
    float Width;
    uint8_t Colour;
} Tug_ModelTypeDef;

typedef struct
{
    const char *Name;
    uint32_t Price;
    uint8_t Bit;                /* TUG_UPGRADE_... */
    const char *What;           /* what it is */
    const char *Does;           /* what it does */
} Tug_UpgradeTypeDef;

typedef enum
{
    TUG_LOOK_GRAVEL = 0,
    TUG_LOOK_LOGS,
    TUG_LOOK_HAY,
    TUG_LOOK_SCRAP,
    TUG_LOOK_COMPONENTS,
    TUG_LOOK_PRODUCE,
    TUG_LOOK_BARRELS,
    TUG_LOOK_COAL,
    TUG_LOOK_STEEL,
    TUG_LOOK_CARS,
    TUG_LOOK_CHEMICALS,
    TUG_LOOK_YACHT,
    TUG_LOOK_DINOSAUR,
    TUG_LOOK_BLADE,
    TUG_LOOK_TRANSFORMER,
    TUG_LOOK_STEAMSHIP,
    TUG_LOOK_ROYAL,
    TUG_LOOK_NUCLEAR,
    TUG_LOOK_ROCKET_ENGINE,
    TUG_LOOK_FUEL_TANK,
    TUG_LOOK_GRANITE,
    TUG_LOOK_MARBLE,
    TUG_LOOK_PLANKS,
    TUG_LOOK_MILK,
    TUG_LOOK_PIPES,
    TUG_LOOK_SOLAR,
    TUG_LOOK_ENGINES,
    TUG_LOOK_FIREWORKS
} Tug_LookTypeDef;

/*
 * Something to tow. It's picked up at one of two sites and taken to one of
 * two others, so the routes vary. Deposit is a damage deposit put down up
 * front (0 for free jobs): it comes back on delivery, less whatever share
 * of the cargo was damaged on the way, and is lost if the cargo is wrecked.
 * Pay is what the cargo is worth on delivery, deposit included, on a route
 * of average length.
 */
typedef struct
{
    const char *Name;
    uint8_t Class;
    uint32_t Deposit;
    uint32_t Pay;
    float Fragility;            /* how much a knock or a scrape hurts it */
    float Length;
    float Width;
    float Mass;
    Tug_LookTypeDef Look;
    uint8_t From[2];            /* site indexes in Tug_Docks (TUG_AT_...) */
    uint8_t To[2];
} Tug_CargoTypeDef;

extern const uint8_t Tug_MapRuns[];
extern const uint32_t Tug_MapRowStart[TUG_MAP_HEIGHT + 1U];
extern const Tug_DockTypeDef Tug_Docks[TUG_DOCK_COUNT];
extern const Tug_MooredBoatTypeDef Tug_MooredBoats[TUG_MOORED_BOAT_COUNT];
extern const Tug_ModelTypeDef Tug_Models[TUG_MODEL_COUNT];
extern const Tug_UpgradeTypeDef Tug_Upgrades[TUG_UPGRADE_COUNT];
extern const Tug_CargoTypeDef Tug_CargoTypes[TUG_CARGO_TYPE_COUNT];
extern const int16_t Tug_SplashRoute[][2];
extern const uint16_t Tug_RouteTiles[TUG_DOCK_COUNT][TUG_DOCK_COUNT];
extern const uint8_t Tug_SplashRouteCount;

/* -------------------------------------------------------------------------- */
/* World                                                                      */
/* -------------------------------------------------------------------------- */

/* Each job's weather. Wind pushes the boats about; rain and storms darken the day. */
typedef enum
{
    TUG_WEATHER_CLEAR = 0,
    TUG_WEATHER_RAIN,
    TUG_WEATHER_WIND,
    TUG_WEATHER_STORM,
    TUG_WEATHER_COUNT
} Tug_WeatherTypeDef;

/* Anything afloat: its centre, speed, heading (0 up the screen, clockwise) and size. */
typedef struct
{
    float X;
    float Y;
    float VelocityX;
    float VelocityY;
    float Heading;
    float Spin;
    float Length;
    float Width;
} Tug_BodyTypeDef;

typedef struct
{
    Tug_BodyTypeDef Body;
    float LeftEngine;           /* -1 full astern to 1 full ahead */
    float RightEngine;
    uint8_t Model;
    float ExtraThrust;          /* from upgrades: this share more push from the engines */
} Tug_TugTypeDef;

typedef struct
{
    Tug_BodyTypeDef Body;
    uint8_t Type;
    float Condition;            /* 100 perfect, 0 wrecked */
    bool Present;
    bool Hooked;
    bool Thrusters;             /* cargo thrusters fitted */
    int8_t Thruster;            /* -1 swinging its bow to the left, 1 to the right, 0 off */
    bool Covered;               /* weather covers fitted: the wind gets less grip */
    uint16_t FlashMilliseconds;
    uint16_t SinkMilliseconds;  /* counting up once wrecked */
} Tug_TowTypeDef;

/* A straight run of lane that cars and people travel along. */
typedef struct
{
    int16_t X;                  /* first tile */
    int16_t Y;
    uint8_t Length;             /* in tiles */
    bool Across;
} Tug_SegmentTypeDef;

typedef struct
{
    float Position;             /* pixels along the segment */
    float Speed;
    uint8_t Segment;
    int8_t Direction;
    uint8_t Colour;
    uint16_t PauseMilliseconds;
} Tug_WalkerTypeDef;

typedef struct
{
    float X;
    float Y;
    uint16_t AgeMilliseconds;
    bool Active;
} Tug_WakeTypeDef;

/* What happened to the boats in one step, for knocks and wear. */
typedef struct
{
    float TugKnock;             /* how hard the tug hit something, pixels per second */
    float CargoKnock;           /* how hard the cargo hit something */
    float CargoScrape;          /* pixels the cargo slid along something */
    float CargoGrounding;       /* pixels the cargo was dragged through shallows */
} Tug_StepResultTypeDef;

/* Everything that moves; the game has one, and the launcher preview another. */
typedef struct
{
    Tug_TugTypeDef Tug;
    Tug_TowTypeDef Cargo;
    Tug_SegmentTypeDef Segments[TUG_MAX_SEGMENTS];
    uint8_t SegmentCount;
    Tug_WalkerTypeDef People[TUG_MAX_PEOPLE];
    Tug_WakeTypeDef Wake[TUG_MAX_WAKE];
    uint8_t NextWake;
    uint16_t WakeTimerMilliseconds;
    float CameraX;              /* world position of the screen's top-left corner */
    float CameraY;
    uint32_t Milliseconds;
    int8_t TargetDock;          /* the site to head for, or -1 */
    bool TargetIsCargo;         /* head for the cargo's bow instead */
    float DeliverProgress;      /* 0 to 1 while holding the cargo still at its site */
    Tug_WeatherTypeDef Weather;
    float WindX;                /* the wind's push before gusts, pixels per second per second */
    float WindY;
} Tug_WorldTypeDef;

/* -------------------------------------------------------------------------- */
/* Career                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum
{
    TUG_SCREEN_TITLE = 0,
    TUG_SCREEN_BOARD,
    TUG_SCREEN_SHOP,
    TUG_SCREEN_DRIVING,
    TUG_SCREEN_RESULT
} Tug_ScreenTypeDef;

typedef enum
{
    TUG_PHASE_TO_CARGO = 0,     /* heading for the cargo to hook on */
    TUG_PHASE_TOWING            /* cargo hooked, heading for its site */
} Tug_PhaseTypeDef;

typedef struct
{
    uint8_t Cargo;
    uint8_t From;
    uint8_t To;
    uint32_t Deposit;           /* put down when the job is taken, back on delivery */
    uint32_t Pay;               /* earned on delivery, on top of the deposit */
    uint16_t BonusSeconds;      /* delivered this soon after hooking on earns the on-time bonus */
    Tug_WeatherTypeDef Weather;
} Tug_OfferTypeDef;

/* Kept between games with save.h. */
typedef struct
{
    int32_t Money;
    uint16_t OwnedModels;       /* bit per tug model */
    uint8_t Model;              /* the tug in use */
    uint8_t Upgrades;           /* TUG_UPGRADE_... bits */
    uint16_t Deliveries;
    uint16_t Wrecks;
    uint32_t Earned;
} Tug_CareerTypeDef;

typedef struct
{
    Tug_ScreenTypeDef Screen;
    Tug_PhaseTypeDef Phase;
    Tug_WorldTypeDef World;
    Tug_CareerTypeDef Career;
    Tug_OfferTypeDef Offers[TUG_OFFER_COUNT];
    uint8_t OfferCount;
    Tug_OfferTypeDef Job;
    bool HasJob;
    uint8_t Selection;
    bool Delivered;             /* the last job's result */
    int32_t ResultAmount;       /* all of it: */
    int32_t ResultDeposit;      /* the deposit back, */
    int32_t ResultPay;          /* the pay */
    int32_t ResultBonus;        /* and the on-time bonus */
    int8_t ResultFavour;        /* a site that thinks better of us after this job, or -1 */
    uint32_t BonusMilliseconds; /* time left for the on-time bonus, 0 once it's gone */
    uint8_t Reputation[TUG_DOCK_COUNT]; /* how well each site thinks of us, kept between games */
    float LeftThrottle;         /* the sliders, -1 to 1 with a dead zone around the centre */
    float RightThrottle;
    bool PrimaryHoldDone;       /* in the marina: this press has already been used as a hold */
    bool SecondaryHoldDone;
    bool Initialized;
    bool Paused;
    uint16_t ScreenMilliseconds;
    uint16_t KnockMilliseconds;
    uint16_t HintMilliseconds;
} Tug_GameTypeDef;

extern Tug_GameTypeDef Tug_Game;

/* -------------------------------------------------------------------------- */
/* Shared functions                                                           */
/* -------------------------------------------------------------------------- */

/* tug_world.c */
char Tug_TileAt(int16_t TileX, int16_t TileY);
bool Tug_TileIsWater(char Tile);
uint8_t Tug_WaterDepth(int16_t TileX, int16_t TileY);
void Tug_DockPosition(uint8_t Dock, float *X, float *Y);
void Tug_PrepareWorld(Tug_WorldTypeDef *World);
void Tug_PlaceTugAtShipyard(Tug_WorldTypeDef *World);
void Tug_PlaceCargo(Tug_WorldTypeDef *World, uint8_t Type, uint8_t Dock);
void Tug_UpdateCity(Tug_WorldTypeDef *World, uint32_t DeltaMilliseconds);
void Tug_StepBoats(Tug_WorldTypeDef *World, float DeltaSeconds, float LeftThrottle, float RightThrottle, Tug_StepResultTypeDef *Result);
void Tug_FollowCamera(Tug_WorldTypeDef *World, float DeltaSeconds, bool Snap);
void Tug_LocalPoint(const Tug_BodyTypeDef *Body, float Ahead, float Right, float *X, float *Y);
float Tug_HookEnd(const Tug_WorldTypeDef *World, float *X, float *Y);
float Tug_Speed(const Tug_BodyTypeDef *Body);

/* tug_career.c */
void Tug_LoadCareer(void);
void Tug_StartTitle(void);
void Tug_UpdateGame(uint32_t DeltaMilliseconds);
void Tug_FormatMoney(char *Buffer, uint32_t Size, int32_t Amount);

/* tug_render.c */
void Tug_DrawWorld(Render_TargetTypeDef *Target, const Tug_WorldTypeDef *World);
void Tug_DrawGame(Render_TargetTypeDef *Target);
void Tug_DrawSplash(Render_TargetTypeDef *Target, Tug_WorldTypeDef *World);

/* canal_tug.c */
void Tug_SetWeatherColours(Tug_WeatherTypeDef Weather);

#endif /* TUG_INTERNAL_H */
