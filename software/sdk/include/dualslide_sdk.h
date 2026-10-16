/**
 * @file dualslide_sdk.h
 * @brief Internals of the DualSlide game SDK.
 *
 * Games do not include this. They use the ordinary DualSlide headers
 * (render.h, controls.h, sound.h and so on); the SDK implements those
 * functions by calling the firmware through the game API (game_api.h).
 */

#ifndef DUALSLIDE_SDK_H
#define DUALSLIDE_SDK_H

#include "game_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The firmware's API, set by the entry point before anything else runs. */
extern const GameAPI_TypeDef *DualSlide_API;

#ifdef __cplusplus
}
#endif

#endif /* DUALSLIDE_SDK_H */
