/**
 * @file solar_scout.h
 * @brief Public interface for the DualSlide Solar Scout mockup.
 */

#ifndef SOLAR_SCOUT_H
#define SOLAR_SCOUT_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

bool SolarScout_Init(void);
void SolarScout_Update(uint32_t DeltaTimeMilliseconds);
bool SolarScout_GetSplashScreenPalette(Display_ColourTypeDef *Palette);
bool SolarScout_DrawSplashScreen(Render_TargetTypeDef *Target);
void SolarScout_Render(void);
void SolarScout_Pause(void);
void SolarScout_Resume(void);
void SolarScout_Shutdown(void);

#endif /* SOLAR_SCOUT_H */