/**
 * @file tanks.h
 * @brief Public application interface for TANKS on DualSlide.
 */

#ifndef TANKS_H
#define TANKS_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Tanks_Init(void);
void Tanks_Update(uint32_t DeltaTimeMilliseconds);
bool Tanks_GetSplashScreenPalette(Display_ColourTypeDef *Palette);
bool Tanks_DrawSplashScreen(Render_TargetTypeDef *Target);
void Tanks_Render(void);
void Tanks_Pause(void);
void Tanks_Resume(void);
void Tanks_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* TANKS_H */
