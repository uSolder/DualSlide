/**
 * @file pong.h
 * @brief Public interface for the DualSlide Pong game.
 */

#ifndef PONG_H
#define PONG_H

#include "display.h"
#include "render.h"

#include <stdbool.h>
#include <stdint.h>

bool Pong_Init(void);
void Pong_Update(uint32_t DeltaTimeMilliseconds);
bool Pong_GetSplashScreenPalette(Display_ColourTypeDef *Palette);
bool Pong_DrawSplashScreen(Render_TargetTypeDef *Target);
void Pong_Render(void);
void Pong_Pause(void);
void Pong_Resume(void);
void Pong_Shutdown(void);

#endif /* PONG_H */