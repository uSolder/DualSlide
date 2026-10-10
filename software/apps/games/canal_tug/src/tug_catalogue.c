/**
 * @file tug_catalogue.c
 * @brief The tugs the boatyard sells and the cargo there is to tow.
 *
 * Change prices, pay, routes and handling here. Each cargo is picked up at
 * either of two sites and delivered to either of two others; the sites are
 * named TUG_AT_... in tug_sites.h. A job is only offered when
 * the tug in use is of its class or higher and there's enough money to put
 * down its damage deposit. Free jobs (deposit 0) are always on offer. Pay is
 * what the cargo is worth on delivery, deposit included, on a route of
 * average length: longer routes pay more.
 */

#include "tug_internal.h"

/* -------------------------------------------------------------------------- */
/* Tugs                                                                       */
/* -------------------------------------------------------------------------- */

const Tug_ModelTypeDef Tug_Models[TUG_MODEL_COUNT] =
{
    /* Name            Price     Class  Thrust   Mass   Length  Width  Colour */
    { "MINNOW",            0U,    1U,      70.0f,  1.0f,  54.0f, 24.0f, TUG_COLOUR_TUG_RED      },
    { "PUFFIN",         1200U,    1U,       74.0f,  1.2f,  56.0f, 24.0f, TUG_COLOUR_TUG_YELLOW   },
    { "DOCKHAND",       3500U,    2U,       78.0f,  1.8f,  62.0f, 26.0f, TUG_COLOUR_TUG_GREEN    },
    { "OTTER",          8000U,    2U,       82.0f,  2.2f,  64.0f, 26.0f, TUG_COLOUR_TUG_BROWN    },
    { "CANAL KING",    15000U,    3U,      86.0f,  3.0f,  70.0f, 28.0f, TUG_COLOUR_TUG_BLUE     },
    { "BULLDOG",       30000U,    3U,      90.0f,  3.6f,  72.0f, 30.0f, TUG_COLOUR_TUG_ORANGE   },
    { "IRONSIDE",      60000U,    4U,      94.0f,  4.6f,  78.0f, 30.0f, TUG_COLOUR_TUG_CHARCOAL },
    { "LEVIATHAN",    120000U,    5U,      98.0f,  6.0f,  84.0f, 32.0f, TUG_COLOUR_TUG_NAVY     },
    { "ARGONAUT",     250000U,    5U,      104.0f,  7.0f,  90.0f, 34.0f, TUG_COLOUR_TUG_TEAL     }
};

/* -------------------------------------------------------------------------- */
/* Upgrades                                                                   */
/* -------------------------------------------------------------------------- */

/* In the order the boatyard shows them, cheapest first. What each one does is worked out in tug_career.c and tug_world.c. */
const Tug_UpgradeTypeDef Tug_Upgrades[TUG_UPGRADE_COUNT] =
{
    /* Name               Price    Bit                           What it is                                        What it does */
    { "SOFT FENDERS",     1500U,  TUG_UPGRADE_SOFT_FENDERS,    "RUBBER FENDERS ROUND EVERY CARGO YOU TOW",        "KNOCKS DO 40% LESS DAMAGE TO THE CARGO" },
    { "WEATHER COVERS",   2000U,  TUG_UPGRADE_WEATHER_COVERS,  "SHEETS LASHED OVER EVERY CARGO YOU TOW",          "THE WIND PUSHES THE CARGO HALF AS HARD" },
    { "CARGO THRUSTERS",  2500U,  TUG_UPGRADE_CARGO_THRUSTERS, "FITTED TO ALL FOUR SIDES OF ANY CARGO YOU TOW",   "THE BUMPERS SWING THE CARGO ROUND, LEFT OR RIGHT" },
    { "KEEL GUARDS",      3000U,  TUG_UPGRADE_KEEL_GUARDS,     "STEEL STRIPS ALONG THE HULL OF EVERY CARGO",      "SCRAPES AND SHALLOWS DO HALF THE DAMAGE" },
    { "TURBO ENGINE",     8000U,  TUG_UPGRADE_TURBO,           "A TURBOCHARGER FOR EVERY TUG YOU OWN",            "12% MORE PUSH FROM THE ENGINES" },
    { "TRADE LICENCE",   20000U,  TUG_UPGRADE_TRADE_LICENCE,   "MEMBERSHIP OF THE CANAL CARRIERS GUILD",          "EVERY JOB PAYS 10% MORE" }
};

/* -------------------------------------------------------------------------- */
/* Cargo                                                                      */
/* -------------------------------------------------------------------------- */

const Tug_CargoTypeDef Tug_CargoTypes[TUG_CARGO_TYPE_COUNT] =
{
    /* Name                      Class  Deposit    Pay       Fragility  Length   Width  Mass   Look                         From (either)                                   To (either) */
    { "GRAVEL",                   1U,        0U,      180U,  0.8f,       96.0f, 40.0f, 1.4f, TUG_LOOK_GRAVEL, { TUG_AT_GREYSTONE_QUARRY, TUG_AT_FLINT_HILL_QUARRY }, { TUG_AT_MIDDLETON, TUG_AT_BROOKFORD } },
    { "TIMBER LOGS",              1U,        0U,      200U,  0.6f,      120.0f, 34.0f, 1.2f, TUG_LOOK_LOGS, { TUG_AT_ASHWOOD_LUMBER, TUG_AT_OAKLEY_LUMBER }, { TUG_AT_BOATYARD, TUG_AT_YACHT_BUILDERS } },
    { "HAY BALES",                1U,        0U,      190U,  1.0f,       92.0f, 40.0f, 1.1f, TUG_LOOK_HAY, { TUG_AT_MEADOW_FARM, TUG_AT_WILLOW_FARM }, { TUG_AT_CASTLE, TUG_AT_PALACE } },
    { "MILK CHURNS",              1U,        0U,      170U,  1.4f,       88.0f, 36.0f, 1.1f, TUG_LOOK_MILK, { TUG_AT_MEADOW_FARM, TUG_AT_WILLOW_FARM }, { TUG_AT_MIDDLETON, TUG_AT_BROOKFORD } },
    { "SCRAP METAL",              1U,        0U,      210U,  0.5f,       96.0f, 40.0f, 1.5f, TUG_LOOK_SCRAP, { TUG_AT_WESTGATE_CARS, TUG_AT_EASTGATE_CARS }, { TUG_AT_FORGEMOOR_STEEL, TUG_AT_BLACKHEATH_STEEL } },
    { "SAWN PLANKS",              1U,      200U,      380U,  0.9f,      110.0f, 36.0f, 1.2f, TUG_LOOK_PLANKS, { TUG_AT_ASHWOOD_LUMBER, TUG_AT_OAKLEY_LUMBER }, { TUG_AT_MIDDLETON, TUG_AT_BROOKFORD } },
    { "DUALSLIDE COMPONENTS",     1U,      300U,      520U,  2.0f,       84.0f, 36.0f, 1.0f, TUG_LOOK_COMPONENTS, { TUG_AT_PCB_FACTORY, TUG_AT_PCB_WORKS }, { TUG_AT_USOLDER, TUG_AT_USOLDER_WORKSHOP } },
    { "FRESH PRODUCE",            1U,      350U,      600U,  1.8f,       92.0f, 40.0f, 1.2f, TUG_LOOK_PRODUCE, { TUG_AT_MEADOW_FARM, TUG_AT_WILLOW_FARM }, { TUG_AT_MIDDLETON, TUG_AT_BROOKFORD } },
    { "BEER BARRELS",             1U,      450U,      760U,  1.6f,       96.0f, 40.0f, 1.4f, TUG_LOOK_BARRELS, { TUG_AT_HOPSWELL_BREWERY, TUG_AT_BARLEYCOMBE_BREWERY }, { TUG_AT_LAKESIDE_MARINA, TUG_AT_RIVERSIDE_MARINA } },
    { "GRANITE",                  2U,      700U,     1150U,  0.6f,      100.0f, 42.0f, 2.8f, TUG_LOOK_GRANITE, { TUG_AT_GREYSTONE_QUARRY, TUG_AT_FLINT_HILL_QUARRY }, { TUG_AT_MIDDLETON, TUG_AT_BROOKFORD } },
    { "COAL",                     2U,      900U,     1500U,  0.7f,      110.0f, 42.0f, 2.4f, TUG_LOOK_COAL, { TUG_AT_DEEPDALE_COLLIERY, TUG_AT_COLDHAM_COLLIERY }, { TUG_AT_NORTH_POWER_STATION, TUG_AT_SOUTH_POWER_STATION } },
    { "STEEL BEAMS",              2U,     1400U,     2300U,  0.9f,      130.0f, 40.0f, 2.8f, TUG_LOOK_STEEL, { TUG_AT_FORGEMOOR_STEEL, TUG_AT_BLACKHEATH_STEEL }, { TUG_AT_MOTORWORKS_NORTH, TUG_AT_MOTORWORKS_SOUTH } },
    { "STEEL PIPES",              2U,     1100U,     1800U,  0.8f,      130.0f, 38.0f, 2.6f, TUG_LOOK_PIPES, { TUG_AT_FORGEMOOR_STEEL, TUG_AT_BLACKHEATH_STEEL }, { TUG_AT_CHEMICAL_WORKS, TUG_AT_REFINERY } },
    { "MARBLE BLOCKS",            2U,     1200U,     2000U,  1.4f,      100.0f, 40.0f, 2.6f, TUG_LOOK_MARBLE, { TUG_AT_GREYSTONE_QUARRY, TUG_AT_FLINT_HILL_QUARRY }, { TUG_AT_CASTLE, TUG_AT_PALACE } },
    { "MARINE ENGINES",           2U,     1600U,     2600U,  1.8f,       96.0f, 40.0f, 2.2f, TUG_LOOK_ENGINES, { TUG_AT_MOTORWORKS_NORTH, TUG_AT_MOTORWORKS_SOUTH }, { TUG_AT_BOATYARD, TUG_AT_YACHT_BUILDERS } },
    { "SOLAR PANELS",             2U,     1800U,     3000U,  2.4f,      110.0f, 40.0f, 1.8f, TUG_LOOK_SOLAR, { TUG_AT_PCB_FACTORY, TUG_AT_PCB_WORKS }, { TUG_AT_HILLTOP_WIND_FARM, TUG_AT_MOORSIDE_WIND_FARM } },
    { "FIREWORKS",                2U,     2200U,     3700U,  3.0f,       92.0f, 40.0f, 1.6f, TUG_LOOK_FIREWORKS, { TUG_AT_CHEMICAL_WORKS, TUG_AT_REFINERY }, { TUG_AT_CASTLE, TUG_AT_PALACE } },
    { "NEW CARS",                 2U,     2600U,     4300U,  2.0f,      120.0f, 42.0f, 2.2f, TUG_LOOK_CARS, { TUG_AT_MOTORWORKS_NORTH, TUG_AT_MOTORWORKS_SOUTH }, { TUG_AT_WESTGATE_CARS, TUG_AT_EASTGATE_CARS } },
    { "CHEMICALS",                3U,     4500U,     7400U,  2.4f,      124.0f, 44.0f, 3.4f, TUG_LOOK_CHEMICALS, { TUG_AT_CHEMICAL_WORKS, TUG_AT_REFINERY }, { TUG_AT_PCB_FACTORY, TUG_AT_PCB_WORKS } },
    { "LUXURY YACHT",             3U,     7000U,    11500U,  2.8f,      116.0f, 40.0f, 3.0f, TUG_LOOK_YACHT, { TUG_AT_BOATYARD, TUG_AT_YACHT_BUILDERS }, { TUG_AT_LAKESIDE_MARINA, TUG_AT_RIVERSIDE_MARINA } },
    { "DINOSAUR SKELETON",        3U,     9000U,    15500U,  3.0f,      110.0f, 42.0f, 2.6f, TUG_LOOK_DINOSAUR, { TUG_AT_GREYSTONE_QUARRY, TUG_AT_FLINT_HILL_QUARRY }, { TUG_AT_CANAL_MUSEUM, TUG_AT_SCIENCE_MUSEUM } },
    { "WIND TURBINE BLADE",       3U,    12000U,    20000U,  2.0f,      200.0f, 30.0f, 3.2f, TUG_LOOK_BLADE, { TUG_AT_FORGEMOOR_STEEL, TUG_AT_BLACKHEATH_STEEL }, { TUG_AT_HILLTOP_WIND_FARM, TUG_AT_MOORSIDE_WIND_FARM } },
    { "TRANSFORMER",              4U,    20000U,    33000U,  1.8f,      120.0f, 48.0f, 5.5f, TUG_LOOK_TRANSFORMER, { TUG_AT_FORGEMOOR_STEEL, TUG_AT_BLACKHEATH_STEEL }, { TUG_AT_NORTH_POWER_STATION, TUG_AT_SOUTH_POWER_STATION } },
    { "MUSEUM STEAMSHIP",         4U,    26000U,    43000U,  2.4f,      150.0f, 44.0f, 5.0f, TUG_LOOK_STEAMSHIP, { TUG_AT_BOATYARD, TUG_AT_YACHT_BUILDERS }, { TUG_AT_CANAL_MUSEUM, TUG_AT_SCIENCE_MUSEUM } },
    { "ROYAL BARGE",              4U,    40000U,    68000U,  3.2f,      140.0f, 40.0f, 4.0f, TUG_LOOK_ROYAL, { TUG_AT_LAKESIDE_MARINA, TUG_AT_RIVERSIDE_MARINA }, { TUG_AT_CASTLE, TUG_AT_PALACE } },
    { "NUCLEAR WASTE",            4U,    55000U,   100000U,  3.2f,      110.0f, 46.0f, 6.0f, TUG_LOOK_NUCLEAR, { TUG_AT_NORTH_POWER_STATION, TUG_AT_SOUTH_POWER_STATION }, { TUG_AT_DEEP_STORE, TUG_AT_NUCLEAR_STORE } },
    { "SATURN V ROCKET ENGINE",   5U,    90000U,   160000U,  2.8f,      130.0f, 50.0f, 7.0f, TUG_LOOK_ROCKET_ENGINE, { TUG_AT_ROCKET_WORKS, TUG_AT_ENGINE_TEST_SITE }, { TUG_AT_SPACE_CENTRE, TUG_AT_LAUNCH_SITE } },
    { "SHUTTLE FUEL TANK",        5U,   140000U,   250000U,  3.0f,      210.0f, 50.0f, 7.5f, TUG_LOOK_FUEL_TANK, { TUG_AT_CHEMICAL_WORKS, TUG_AT_REFINERY }, { TUG_AT_SPACE_CENTRE, TUG_AT_LAUNCH_SITE } }
};
