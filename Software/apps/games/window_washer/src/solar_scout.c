/**
 * @file solar_scout.c
 * @brief Visual and control mockup for an arcade solar-system discovery game.
 */

#include "solar_scout.h"

#include "app_manager.h"
#include "input.h"
#include "open_sans.h"

#include <stddef.h>

#define INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)
#define SOLAR_SPLASH_X (60)
#define SOLAR_SPLASH_Y (60)
#define SOLAR_SPLASH_WIDTH (680U)
#define SOLAR_SPLASH_HEIGHT (360U)
#define SOLAR_FIXED_SCALE (256)
#define SOLAR_SHIP_SIZE (12U)

enum
{
    COLOUR_SPACE = 0U, COLOUR_STAR = 1U, COLOUR_STAR_DIM = 2U, COLOUR_SUN_GLOW = 3U,
    COLOUR_SUN = 4U, COLOUR_ORBIT = 5U, COLOUR_EARTH = 6U, COLOUR_MOON = 7U,
    COLOUR_MARS = 8U, COLOUR_JUPITER = 9U, COLOUR_SHIP = 10U, COLOUR_THRUST = 11U,
    COLOUR_TEXT = 12U, COLOUR_MUTED = 13U, COLOUR_PANEL = 14U, COLOUR_PANEL_EDGE = 15U,
    COLOUR_DISCOVERY = 16U, COLOUR_FUEL = 17U, COLOUR_TRAJECTORY = 18U, COLOUR_SHADOW = 19U
};

typedef enum { SOLAR_SCREEN_MENU, SOLAR_SCREEN_FLIGHT, SOLAR_SCREEN_DISCOVERY } Solar_ScreenTypeDef;
typedef struct { int32_t X; int32_t Y; int32_t VelocityX; int32_t VelocityY; int16_t Heading; uint16_t Fuel; uint8_t Discoveries; Solar_ScreenTypeDef Screen; bool PrimaryDown; bool SecondaryDown; uint32_t ElapsedMilliseconds; uint32_t DiscoveryMilliseconds; } Solar_GameTypeDef;

static const Display_ColourTypeDef SolarScout_Palette[] =
{
    0x00000000U, 0x00EAF7FFU, 0x00425A72U, 0x004D3A13U, 0x00FFC94AU,
    0x00233A55U, 0x003B9BDBU, 0x00D6E5EFU, 0x00D96C4FU, 0x00D8A55CU,
    0x00F4FAFFU, 0x006EE7D8U, 0x00F7FFF7U, 0x0095AABD, 0x00132135U,
    0x003A6C97U, 0x006EE7D8U, 0x00DFF05AU, 0x006C63FFU, 0x00101422U
};

static Solar_GameTypeDef Solar_Game;
static uint32_t Solar_PendingDeltaTimeMilliseconds;
static bool Solar_Initialized;
static bool Solar_Paused;
static uint32_t Solar_SplashElapsedMilliseconds;

static void Solar_Rect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    const Render_RectTypeDef Rect = { X, Y, Width, Height };
    Render_FillRect(Target, &Rect, Colour);
}

static void Solar_Disc(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Diameter, Render_ColourIndexTypeDef Colour)
{
    const uint16_t Corner = (Diameter + 3U) / 4U;
    Solar_Rect(Target, X + (int16_t)Corner, Y, Diameter - (2U * Corner), Corner, Colour);
    Solar_Rect(Target, X, Y + (int16_t)Corner, Diameter, Diameter - (2U * Corner), Colour);
    Solar_Rect(Target, X + (int16_t)Corner, Y + (int16_t)Diameter - (int16_t)Corner, Diameter - (2U * Corner), Corner, Colour);
}

static int16_t Solar_TextWidth(const Font *FontData, const char *Text)
{
    int16_t Width = 0;
    while(*Text != '\0') { const FontGlyph *Glyph = Font_GetGlyph(FontData, (uint8_t)*Text); if(Glyph != NULL) Width = (int16_t)(Width + Glyph->advance); Text++; }
    return Width;
}

static void Solar_CenteredText(Render_TargetTypeDef *Target, const Font *FontData, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    Render_DrawText(Target, FontData, Text, (int16_t)(X - (Solar_TextWidth(FontData, Text) / 2)), Y, Colour);
}

static void Solar_Stars(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint32_t Phase)
{
    uint32_t Seed = 0x5A17D00DU;
    const int16_t LimitX = (int16_t)(X + Width);
    const int16_t LimitY = (int16_t)(Y + Height);
    for(uint8_t Index = 0U; Index < 72U; Index++)
    {
        Seed = (Seed * 1664525U) + 1013904223U;
        const int16_t StarX = (int16_t)(X + (Seed % Width));
        Seed = (Seed * 1664525U) + 1013904223U;
        const int16_t StarY = (int16_t)(Y + (Seed % Height));
        if((StarX < LimitX) && (StarY < LimitY)) Solar_Rect(Target, StarX, StarY, (Seed & 7U) == (Phase & 7U) ? 2U : 1U, 1U, (Seed & 3U) == 0U ? COLOUR_STAR_DIM : COLOUR_STAR);
    }
}

static void Solar_Orbit(Render_TargetTypeDef *Target, int16_t CenterX, int16_t CenterY, int16_t RadiusX, int16_t RadiusY)
{
    for(int16_t X = (int16_t)(CenterX - RadiusX); X <= (int16_t)(CenterX + RadiusX); X += 4)
    {
        const int16_t Offset = (int16_t)(X - CenterX);
        const int32_t Inside = (int32_t)RadiusX * RadiusX - (int32_t)Offset * Offset;
        if(Inside >= 0)
        {
            const int16_t YOffset = (int16_t)((RadiusY * (int32_t)RadiusY * Inside) / ((int32_t)RadiusX * RadiusX));
            int16_t Root = 0;
            while((Root + 1) * (Root + 1) <= YOffset) Root++;
            Solar_Rect(Target, X, (int16_t)(CenterY - Root), 1U, 1U, COLOUR_ORBIT);
            Solar_Rect(Target, X, (int16_t)(CenterY + Root), 1U, 1U, COLOUR_ORBIT);
        }
    }
}

static void Solar_DrawShip(Render_TargetTypeDef *Target, int16_t X, int16_t Y, int16_t Heading, bool Thrusting)
{
    const bool FacesRight = (Heading >= -90) && (Heading <= 90);
    Solar_Rect(Target, X - 5, Y - 3, 10U, 7U, COLOUR_SHIP);
    Solar_Rect(Target, FacesRight ? X + 5 : X - 7, Y - 1, 2U, 3U, COLOUR_SHIP);
    Solar_Rect(Target, X - 2, Y - 5, 4U, 2U, COLOUR_SHIP);
    if(Thrusting) Solar_Rect(Target, FacesRight ? X - 9 : X + 7, Y - 1, 4U, 3U, COLOUR_THRUST);
}

static void Solar_DrawMap(Render_TargetTypeDef *Target, uint32_t Phase, bool ShowShip)
{
    const int16_t CenterX = 400;
    const int16_t CenterY = 252;
    const int16_t EarthX = (int16_t)(CenterX + 107 + ((Phase / 120U) % 18U));
    const int16_t EarthY = (int16_t)(CenterY - 20 + ((Phase / 240U) % 9U));
    const int16_t MarsX = (int16_t)(CenterX - 166 + ((Phase / 180U) % 32U));
    const int16_t MarsY = (int16_t)(CenterY + 48 - ((Phase / 300U) % 13U));
    Render_Clear(Target, COLOUR_SPACE);
    Solar_Stars(Target, 0, 0, RENDER_WIDTH, RENDER_HEIGHT, Phase / 80U);
    Solar_Orbit(Target, CenterX, CenterY, 112, 48);
    Solar_Orbit(Target, CenterX, CenterY, 180, 78);
    Solar_Orbit(Target, CenterX, CenterY, 266, 116);
    Solar_Disc(Target, CenterX - 28, CenterY - 28, 56U, COLOUR_SUN_GLOW);
    Solar_Disc(Target, CenterX - 19, CenterY - 19, 38U, COLOUR_SUN);
    Solar_Disc(Target, EarthX - 11, EarthY - 11, 22U, COLOUR_EARTH);
    Solar_Disc(Target, EarthX + 20, EarthY - 2, 6U, COLOUR_MOON);
    Solar_Disc(Target, MarsX - 8, MarsY - 8, 16U, COLOUR_MARS);
    Solar_Disc(Target, 588, 161, 32U, COLOUR_JUPITER);
    Solar_Rect(Target, 582, 177, 44U, 3U, COLOUR_MARS);
    Render_DrawText(Target, &OpenSans20, "EARTH", (int16_t)(EarthX - 29), (int16_t)(EarthY + 24), COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, "MARS", (int16_t)(MarsX - 23), (int16_t)(MarsY + 22), COLOUR_MUTED);
    Render_DrawText(Target, &OpenSans20, "JUPITER", 572, 204, COLOUR_MUTED);
    if(ShowShip)
    {
        const int16_t ShipX = (int16_t)(Solar_Game.X / SOLAR_FIXED_SCALE);
        const int16_t ShipY = (int16_t)(Solar_Game.Y / SOLAR_FIXED_SCALE);
        Solar_Rect(Target, (int16_t)(ShipX - 65), ShipY, 52U, 1U, COLOUR_TRAJECTORY);
        Solar_DrawShip(Target, ShipX, ShipY, Solar_Game.Heading, Solar_Game.Fuel > 0U);
    }
}

static void Solar_DrawHud(Render_TargetTypeDef *Target)
{
    Solar_Rect(Target, 20, 18, 160U, 52U, COLOUR_PANEL);
    Solar_Rect(Target, 20, 18, 160U, 2U, COLOUR_PANEL_EDGE);
    Render_DrawText(Target, &OpenSans20, "SOLAR SCOUT", 32, 25, COLOUR_TEXT);
    Render_DrawText(Target, &OpenSans20, "DISCOVERIES", 32, 49, COLOUR_MUTED);
    Render_DrawText(Target, &OpenSans20, "2 / 9", 140, 49, COLOUR_DISCOVERY);
    Solar_Rect(Target, 620, 18, 160U, 52U, COLOUR_PANEL);
    Solar_Rect(Target, 620, 18, 160U, 2U, COLOUR_PANEL_EDGE);
    Render_DrawText(Target, &OpenSans20, "FUEL", 636, 25, COLOUR_MUTED);
    Solar_Rect(Target, 636, 51, 120U, 8U, COLOUR_SHADOW);
    Solar_Rect(Target, 636, 51, Solar_Game.Fuel > 120U ? 120U : Solar_Game.Fuel, 8U, COLOUR_FUEL);
}

static void Solar_DrawMenu(Render_TargetTypeDef *Target)
{
    Solar_DrawMap(Target, Solar_Game.ElapsedMilliseconds, false);
    Solar_Rect(Target, 160, 88, 480U, 258U, COLOUR_SHADOW);
    Solar_Rect(Target, 162, 86, 476U, 258U, COLOUR_SPACE);
    Solar_Rect(Target, 162, 86, 476U, 3U, COLOUR_PANEL_EDGE);
    Solar_CenteredText(Target, &OpenSans36, "SOLAR SCOUT", 400, 118, COLOUR_TEXT);
    Solar_CenteredText(Target, &OpenSans20, "FIND THE WORLDS", 400, 162, COLOUR_DISCOVERY);
    Solar_CenteredText(Target, &OpenSans20, "EXPLORE THE SOLAR SYSTEM", 400, 205, COLOUR_TEXT);
    Solar_CenteredText(Target, &OpenSans20, "USE BOTH SLIDERS TO STEER AND THRUST", 400, 234, COLOUR_MUTED);
    Solar_CenteredText(Target, &OpenSans20, "PRIMARY: LAUNCH", 400, 290, COLOUR_THRUST);
}

static void Solar_DrawDiscovery(Render_TargetTypeDef *Target)
{
    Solar_DrawMap(Target, Solar_Game.ElapsedMilliseconds, false);
    Solar_Rect(Target, 196, 104, 408U, 236U, COLOUR_SHADOW);
    Solar_Rect(Target, 198, 102, 404U, 236U, COLOUR_SPACE);
    Solar_Rect(Target, 198, 102, 404U, 3U, COLOUR_DISCOVERY);
    Solar_CenteredText(Target, &OpenSans36, "MARS FOUND", 400, 135, COLOUR_DISCOVERY);
    Solar_CenteredText(Target, &OpenSans20, "THE RED PLANET", 400, 181, COLOUR_TEXT);
    Solar_CenteredText(Target, &OpenSans20, "YOU ARE NOW 2 WORLDS FROM HOME", 400, 218, COLOUR_MUTED);
    Solar_CenteredText(Target, &OpenSans20, "PRIMARY: CONTINUE", 400, 281, COLOUR_THRUST);
}

static void Solar_UpdateFlight(uint32_t DeltaTimeMilliseconds)
{
    int32_t Left = 32768;
    int32_t Right = 32768;
    (void)Input_Get_Value(INPUT_LEFT_SLIDER_NUMBER, &Left);
    (void)Input_Get_Value(INPUT_RIGHT_SLIDER_NUMBER, &Right);
    Solar_Game.Heading += (int16_t)((Right - Left) / 1800);
    if(Solar_Game.Heading > 180) Solar_Game.Heading -= 360;
    if(Solar_Game.Heading < -180) Solar_Game.Heading += 360;
    Solar_Game.VelocityX += (int32_t)(((Left + Right - 65535) * (int32_t)DeltaTimeMilliseconds) / 260000);
    Solar_Game.VelocityY += (int32_t)(((Right - Left) * (int32_t)DeltaTimeMilliseconds) / 520000);
    Solar_Game.X += Solar_Game.VelocityX * (int32_t)DeltaTimeMilliseconds / 1000;
    Solar_Game.Y += Solar_Game.VelocityY * (int32_t)DeltaTimeMilliseconds / 1000;
    if(Solar_Game.X < 24 * SOLAR_FIXED_SCALE) Solar_Game.X = 24 * SOLAR_FIXED_SCALE;
    if(Solar_Game.X > 776 * SOLAR_FIXED_SCALE) Solar_Game.X = 776 * SOLAR_FIXED_SCALE;
    if(Solar_Game.Y < 88 * SOLAR_FIXED_SCALE) Solar_Game.Y = 88 * SOLAR_FIXED_SCALE;
    if(Solar_Game.Y > 442 * SOLAR_FIXED_SCALE) Solar_Game.Y = 442 * SOLAR_FIXED_SCALE;
    if(Solar_Game.Fuel > 0U) Solar_Game.Fuel--;
    if((Solar_Game.ElapsedMilliseconds > 8500U) && (Solar_Game.Discoveries == 2U)) { Solar_Game.Discoveries = 3U; Solar_Game.Screen = SOLAR_SCREEN_DISCOVERY; Solar_Game.DiscoveryMilliseconds = 0U; }
}

bool SolarScout_Init(void)
{
    Solar_Game.X = 515 * SOLAR_FIXED_SCALE;
    Solar_Game.Y = 270 * SOLAR_FIXED_SCALE;
    Solar_Game.VelocityX = 0;
    Solar_Game.VelocityY = 0;
    Solar_Game.Heading = 0;
    Solar_Game.Fuel = 120U;
    Solar_Game.Discoveries = 2U;
    Solar_Game.Screen = SOLAR_SCREEN_MENU;
    Solar_Game.PrimaryDown = false;
    Solar_Game.SecondaryDown = false;
    Solar_Game.ElapsedMilliseconds = 0U;
    Solar_PendingDeltaTimeMilliseconds = 0U;
    Solar_Paused = false;
    Solar_Initialized = true;
    return true;
}

void SolarScout_Update(uint32_t DeltaTimeMilliseconds)
{
    int32_t Value = 0;
    bool Primary = false;
    if(!Solar_Initialized || Solar_Paused) return;
    if(Input_Get_Value(INPUT_PRIMARY_BUTTON_NUMBER, &Value)) Primary = Value != 0;
    if(Primary && !Solar_Game.PrimaryDown)
    {
        if(Solar_Game.Screen == SOLAR_SCREEN_MENU) Solar_Game.Screen = SOLAR_SCREEN_FLIGHT;
        else if(Solar_Game.Screen == SOLAR_SCREEN_DISCOVERY) Solar_Game.Screen = SOLAR_SCREEN_FLIGHT;
    }
    Solar_Game.PrimaryDown = Primary;
    Solar_PendingDeltaTimeMilliseconds += DeltaTimeMilliseconds;
}

bool SolarScout_GetSplashScreenPalette(Display_ColourTypeDef *Palette)
{
    if(Palette == NULL) return false;
    for(uint16_t Index = 0U; Index < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; Index++) Palette[Index] = 0U;
    for(uint16_t Index = 0U; Index < (uint16_t)(sizeof(SolarScout_Palette) / sizeof(SolarScout_Palette[0])); Index++) Palette[Index] = SolarScout_Palette[Index];
    return true;
}

bool SolarScout_DrawSplashScreen(Render_TargetTypeDef *Target)
{
    const Render_RectTypeDef Bounds = { APP_MANAGER_SPLASH_SCREEN_X, APP_MANAGER_SPLASH_SCREEN_Y, APP_MANAGER_SPLASH_SCREEN_WIDTH, APP_MANAGER_SPLASH_SCREEN_HEIGHT };
    if((Target == NULL) || (Target->Pixels == NULL)) return false;
    Solar_SplashElapsedMilliseconds += 33U;
    Render_FillRect(Target, &Bounds, COLOUR_SPACE);
    Solar_Stars(Target, SOLAR_SPLASH_X, SOLAR_SPLASH_Y, SOLAR_SPLASH_WIDTH, SOLAR_SPLASH_HEIGHT, Solar_SplashElapsedMilliseconds / 80U);
    Solar_Disc(Target, 361, 188, 78U, COLOUR_SUN_GLOW);
    Solar_Disc(Target, 374, 201, 52U, COLOUR_SUN);
    Solar_Disc(Target, 512, 252, 25U, COLOUR_EARTH);
    Solar_DrawShip(Target, 473, 242, 0, true);
    Solar_CenteredText(Target, &OpenSans36, "SOLAR SCOUT", 400, 89, COLOUR_TEXT);
    Solar_CenteredText(Target, &OpenSans20, "FIND THE WORLDS", 400, 365, COLOUR_DISCOVERY);
    return true;
}

void SolarScout_Render(void)
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;
    uint32_t DeltaTimeMilliseconds;
    if(!Solar_Initialized || Solar_Paused) return;
    Frame = Display_AcquireFrame();
    if(Frame == NULL) return;
    Target.Pixels = Frame->Pixels; Target.Width = Frame->Width; Target.Height = Frame->Height; Target.StridePixels = Frame->StridePixels;
    DeltaTimeMilliseconds = Solar_PendingDeltaTimeMilliseconds;
    if(DeltaTimeMilliseconds > 50U) DeltaTimeMilliseconds = 50U;
    Solar_PendingDeltaTimeMilliseconds = 0U;
    Solar_Game.ElapsedMilliseconds += DeltaTimeMilliseconds;
    if(Solar_Game.Screen == SOLAR_SCREEN_FLIGHT) Solar_UpdateFlight(DeltaTimeMilliseconds);
    if(Solar_Game.Screen == SOLAR_SCREEN_MENU) Solar_DrawMenu(&Target);
    else if(Solar_Game.Screen == SOLAR_SCREEN_DISCOVERY) Solar_DrawDiscovery(&Target);
    else { Solar_DrawMap(&Target, Solar_Game.ElapsedMilliseconds, true); Solar_DrawHud(&Target); }
    (void)Display_PresentFrame(Frame);
}

void SolarScout_Pause(void) { Solar_Paused = true; }
void SolarScout_Resume(void) { Solar_Paused = false; }
void SolarScout_Shutdown(void) { Solar_Initialized = false; Solar_Paused = false; Solar_PendingDeltaTimeMilliseconds = 0U; }