/**
 * @file canal_tug.c
 * @brief Canal Tug: the app, its colours and the launcher preview.
 *
 * The game flow is in tug_career.c, the boats and the village traffic in
 * tug_world.c, the drawing in tug_render.c, the map and sites in tug_city.c,
 * the tugs and cargo in tug_catalogue.c and the sounds in tug_audio.c.
 */

#include "canal_tug.h"

#include "tug_audio.h"
#include "tug_internal.h"

#include "app_manager.h"
#include "display.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* The launcher preview moves on this much each time it is drawn. */
#define SPLASH_FRAME_MS         (16U)

/* The preview's tug cruises along Tug_SplashRoute at this speed, in pixels per second. */
#define SPLASH_SPEED            (45.0f)

/* The preview's tow: a tug of this model with this cargo, on a rope this long. */
#define SPLASH_MODEL            (2U)
#define SPLASH_CARGO            (9U)
#define SPLASH_ROPE             (56.0f)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Colours, in the order of Tug_ColourTypeDef. */
static const Display_ColourTypeDef CanalTug_Palette[TUG_COLOUR_COUNT] =
{
    0x00000000U, /* BLACK           */
    0x00FFFFFFU, /* WHITE           */
    0x002A5B6EU, /* WATER_DEEP      */
    0x00356F84U, /* WATER           */
    0x00244D5EU, /* WATER_EDGE      */
    0x006FA3B4U, /* WATER_GLINT     */
    0x007CC6DBU, /* SHALLOWS        */
    0x00B9D6B0U, /* SHALLOWS_DARK   */
    0x00EAF5F6U, /* FOAM            */
    0x00A9D3DEU, /* FOAM_FADED      */
    0x00283028U, /* SHADOW          */
    0x006B5A3CU, /* BANK            */
    0x00C8B38AU, /* TOWPATH         */
    0x00A08A63U, /* TOWPATH_DARK    */
    0x00676D70U, /* LANE            */
    0x0094A36AU, /* VERGE           */
    0x0079AC4EU, /* MEADOW          */
    0x00649440U, /* MEADOW_DARK     */
    0x0094C266U, /* MEADOW_LIGHT    */
    0x00916A45U, /* FIELD           */
    0x00745234U, /* FIELD_DARK      */
    0x00DDBE62U, /* WHEAT           */
    0x00BC9C44U, /* WHEAT_DARK      */
    0x003E6E2DU, /* HEDGE           */
    0x00578C3DU, /* HEDGE_LIGHT     */
    0x002F6630U, /* TREE            */
    0x00508E45U, /* TREE_LIGHT      */
    0x00244D24U, /* TREE_DARK       */
    0x00E8739AU, /* FLOWER          */
    0x00B5534AU, /* ROOF_RED        */
    0x00863A33U, /* ROOF_RED_DARK   */
    0x00687988U, /* ROOF_SLATE      */
    0x00495663U, /* ROOF_SLATE_DARK */
    0x00C9A45CU, /* THATCH          */
    0x009C7C3FU, /* THATCH_DARK     */
    0x00B7AE9CU, /* STONE           */
    0x00857D6DU, /* STONE_DARK      */
    0x00433A35U, /* CHIMNEY         */
    0x00A9A294U, /* YARD            */
    0x0089836FU, /* YARD_DARK       */
    0x00A3A8ACU, /* SHED            */
    0x00737A80U, /* SHED_DARK       */
    0x00F2B630U, /* HAZARD          */
    0x00A57A55U, /* WOOD            */
    0x006F4F35U, /* WOOD_DARK       */
    0x008C6239U, /* LOG             */
    0x00D9B37CU, /* LOG_END         */
    0x00A64B34U, /* BRICK           */
    0x00C27A3EU, /* COPPER          */
    0x00C9CED1U, /* SMOKE           */
    0x00D8433CU, /* CAR_RED         */
    0x003F73C9U, /* CAR_BLUE        */
    0x00F1C232U, /* CAR_YELLOW      */
    0x00ECEFF1U, /* CAR_WHITE       */
    0x0043A363U, /* CAR_GREEN       */
    0x009ED6E6U, /* GLASS           */
    0x00237A3BU, /* PCB             */
    0x00E0B54AU, /* PCB_GOLD        */
    0x00D2402FU, /* TUG_RED         */
    0x00E8B92EU, /* TUG_YELLOW      */
    0x00338A55U, /* TUG_GREEN       */
    0x008A5A34U, /* TUG_BROWN       */
    0x002F6DB5U, /* TUG_BLUE        */
    0x00E57A24U, /* TUG_ORANGE      */
    0x003A3F45U, /* TUG_CHARCOAL    */
    0x00223A6EU, /* TUG_NAVY        */
    0x001E9A96U, /* TUG_TEAL        */
    0x00C9B48FU, /* TUG_DECK        */
    0x00F4F1E8U, /* TUG_CABIN       */
    0x00262626U, /* FENDER          */
    0x00E8D6A6U, /* ROPE            */
    0x00715E4AU, /* BARGE           */
    0x00463A2EU, /* BARGE_DARK      */
    0x008F8E89U, /* GRAVEL          */
    0x00303235U, /* COAL            */
    0x00E3C463U, /* HAY             */
    0x00A7AFB6U, /* STEEL           */
    0x00A0522DU, /* RUST            */
    0x00C9483BU, /* CONTAINER_A     */
    0x002F7FB8U, /* CONTAINER_B     */
    0x00E5A12CU, /* CONTAINER_C     */
    0x00FBFBF7U, /* YACHT           */
    0x00213A5CU, /* YACHT_TRIM      */
    0x005B2A86U, /* ROYAL           */
    0x00E6C34BU, /* GOLD            */
    0x00F5D426U, /* NUCLEAR         */
    0x00E07B2EU, /* TANK_ORANGE     */
    0x00EFE6CFU, /* BONE            */
    0x001F5E3AU, /* NARROWBOAT_A    */
    0x008E2232U, /* NARROWBOAT_B    */
    0x001D4E89U, /* NARROWBOAT_C    */
    0x00C88A1EU, /* NARROWBOAT_D    */
    0x00E8541EU, /* BUOY            */
    0x00102336U, /* PANEL           */
    0x00284A6BU, /* PANEL_EDGE      */
    0x001C3954U, /* PANEL_LIGHT     */
    0x00FFFFFFU, /* TEXT            */
    0x0099B3C9U, /* TEXT_MUTED      */
    0x00FFD34EU, /* TARGET          */
    0x0063C77AU, /* MONEY           */
    0x00F05A50U  /* DANGER          */
};

/* The launcher preview's own world, kept apart from the game's. */
static Tug_WorldTypeDef CanalTug_SplashWorld;
static bool CanalTug_SplashReady = false;
static float CanalTug_SplashTravelled = 0.0f;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* A point Distance pixels along the preview's route, and the way the route runs there. */
static void CanalTug_RoutePoint(float Distance, float *X, float *Y, float *Heading)
{
    for(uint8_t Index = 1U; Index < Tug_SplashRouteCount; Index++)
    {
        const float StartX = (float)Tug_SplashRoute[Index - 1U][0];
        const float StartY = (float)Tug_SplashRoute[Index - 1U][1];
        const float DeltaX = (float)Tug_SplashRoute[Index][0] - StartX;
        const float DeltaY = (float)Tug_SplashRoute[Index][1] - StartY;
        const float Length = sqrtf((DeltaX * DeltaX) + (DeltaY * DeltaY));
        if((Distance <= Length) || (Index == (Tug_SplashRouteCount - 1U)))
        {
            const float Share = (Length > 0.0f) ? fminf(Distance / Length, 1.0f) : 0.0f;
            *X = StartX + (DeltaX * Share);
            *Y = StartY + (DeltaY * Share);
            *Heading = atan2f(DeltaX, -DeltaY);
            return;
        }
        Distance -= Length;
    }
}

static float CanalTug_RouteLength(void)
{
    float Total = 0.0f;
    for(uint8_t Index = 1U; Index < Tug_SplashRouteCount; Index++)
    {
        const float DeltaX = (float)(Tug_SplashRoute[Index][0] - Tug_SplashRoute[Index - 1U][0]);
        const float DeltaY = (float)(Tug_SplashRoute[Index][1] - Tug_SplashRoute[Index - 1U][1]);
        Total += sqrtf((DeltaX * DeltaX) + (DeltaY * DeltaY));
    }
    return Total;
}

/* The preview: a tug towing new cars along the main canal, past the villages and fields. */
static void CanalTug_UpdateSplash(Tug_WorldTypeDef *World, uint32_t DeltaMilliseconds)
{
    Tug_TugTypeDef *Tug = &World->Tug;
    Tug_TowTypeDef *Cargo = &World->Cargo;
    const float Gap = (Tug->Body.Length / 2.0f) + SPLASH_ROPE + (Cargo->Body.Length / 2.0f);
    const float Length = CanalTug_RouteLength();

    CanalTug_SplashTravelled += SPLASH_SPEED * (float)DeltaMilliseconds / 1000.0f;
    if(CanalTug_SplashTravelled > Length)
    {
        CanalTug_SplashTravelled = Gap;
    }
    CanalTug_RoutePoint(CanalTug_SplashTravelled, &Tug->Body.X, &Tug->Body.Y, &Tug->Body.Heading);
    CanalTug_RoutePoint(fmaxf(CanalTug_SplashTravelled - Gap, 0.0f), &Cargo->Body.X, &Cargo->Body.Y, &Cargo->Body.Heading);
    Tug->LeftEngine = 0.6f;
    Tug->RightEngine = 0.6f;
    World->CameraX = Tug->Body.X - 470.0f;
    World->CameraY = Tug->Body.Y - 250.0f;
    Tug_UpdateCity(World, DeltaMilliseconds);
}

/* -------------------------------------------------------------------------- */
/* Weather                                                                    */
/* -------------------------------------------------------------------------- */

/* One colour channel moved Share of the way towards Towards, after scaling by Light. */
static uint32_t CanalTug_Channel(uint32_t Colour, uint8_t Shift, float Light, uint32_t Towards, float Share)
{
    const float Value = (float)((Colour >> Shift) & 0xFFU) * Light;
    const float Mixed = Value + (((float)((Towards >> Shift) & 0xFFU) - Value) * Share);
    return ((uint32_t)lrintf(fminf(fmaxf(Mixed, 0.0f), 255.0f))) << Shift;
}

/*
 * The world's colours for the weather: darker and bluer in rain, darker
 * still in a storm. The panels and their text keep their own colours.
 */
void Tug_SetWeatherColours(Tug_WeatherTypeDef Weather)
{
    Display_ColourTypeDef Colours[TUG_COLOUR_COUNT];
    float Light = 1.0f;
    uint32_t Towards = 0U;
    float Share = 0.0f;

    if(Weather == TUG_WEATHER_RAIN)
    {
        Light = 0.84f;
        Towards = 0x00465A6EU;
        Share = 0.12f;
    }
    else if(Weather == TUG_WEATHER_STORM)
    {
        Light = 0.68f;
        Towards = 0x00323C50U;
        Share = 0.16f;
    }
    for(uint16_t Index = 0U; Index < (uint16_t)TUG_COLOUR_COUNT; Index++)
    {
        const uint32_t Colour = CanalTug_Palette[Index];
        Colours[Index] = (Index >= (uint16_t)TUG_COLOUR_PANEL) ? Colour :
                         (CanalTug_Channel(Colour, 16U, Light, Towards, Share) | CanalTug_Channel(Colour, 8U, Light, Towards, Share) | CanalTug_Channel(Colour, 0U, Light, Towards, Share));
    }
    (void)Display_SetPalette(APP_MANAGER_SPLASH_PALETTE_START_INDEX, Colours, (uint16_t)TUG_COLOUR_COUNT);
}

/* -------------------------------------------------------------------------- */
/* Application functions                                                      */
/* -------------------------------------------------------------------------- */

static bool CanalTug_Init(void)
{
    Tug_LoadCareer();
    Tug_Game.Paused = false;
    Tug_StartTitle();
    Tug_Game.Initialized = true;
    return true;
}

static void CanalTug_Update(uint32_t DeltaTimeMilliseconds)
{
    if(!Tug_Game.Initialized || Tug_Game.Paused)
    {
        return;
    }
    Tug_UpdateGame((DeltaTimeMilliseconds > 50U) ? 50U : DeltaTimeMilliseconds);
}

static void CanalTug_Render(Render_TargetTypeDef *Target)
{
    if(Tug_Game.Initialized)
    {
        Tug_DrawGame(Target);
    }
}

static bool CanalTug_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    if(!CanalTug_SplashReady)
    {
        Tug_PrepareWorld(&CanalTug_SplashWorld);
        CanalTug_SplashWorld.Tug.Model = SPLASH_MODEL;
        Tug_PlaceTugAtShipyard(&CanalTug_SplashWorld);
        Tug_PlaceCargo(&CanalTug_SplashWorld, SPLASH_CARGO, TUG_SHIPYARD_DOCK);
        CanalTug_SplashWorld.Cargo.Hooked = true;
        CanalTug_SplashTravelled = (CanalTug_SplashWorld.Tug.Body.Length / 2.0f) + SPLASH_ROPE + (CanalTug_SplashWorld.Cargo.Body.Length / 2.0f);
        CanalTug_SplashReady = true;
    }
    CanalTug_UpdateSplash(&CanalTug_SplashWorld, SPLASH_FRAME_MS);
    Tug_DrawSplash(Target, &CanalTug_SplashWorld);
    return true;
}

static void CanalTug_Pause(void)
{
    Tug_Game.Paused = true;
    TugAudio_StopEngines();
}

static void CanalTug_Resume(void)
{
    Tug_Game.Paused = false;
    Tug_SetWeatherColours(Tug_Game.World.Weather);
    if(Tug_Game.Screen == TUG_SCREEN_DRIVING)
    {
        TugAudio_StartEngines();
    }
}

static void CanalTug_Shutdown(void)
{
    TugAudio_StopEngines();
    Tug_Game.Initialized = false;
    Tug_Game.Paused = false;
}

/* -------------------------------------------------------------------------- */
/* Application                                                                */
/* -------------------------------------------------------------------------- */

const AppManager_AppTypeDef CanalTug_App =
{
    .Init = CanalTug_Init,
    .Update = CanalTug_Update,
    .Render = CanalTug_Render,
    .DrawSplashScreen = CanalTug_DrawSplashScreen,
    .Pause = CanalTug_Pause,
    .Resume = CanalTug_Resume,
    .Shutdown = CanalTug_Shutdown,
    APP_PALETTE(CanalTug_Palette)
};
