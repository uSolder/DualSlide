/**
 * @file open_sans.h
 * @brief Open Sans, as built into the DualSlide firmware.
 *
 * Use the fonts exactly as in the firmware: &OpenSans20 is a const Font *.
 * In a game file each name refers to the firmware's copy, so the fonts cost
 * the game nothing. They are not constants, so they cannot appear in static
 * initialisers.
 */

#ifndef OPEN_SANS_H
#define OPEN_SANS_H

#include "dualslide_sdk.h"
#include "font.h"

#define OpenSans12                          (*DualSlide_API->Fonts->OpenSans12)
#define OpenSans16                          (*DualSlide_API->Fonts->OpenSans16)
#define OpenSans20                          (*DualSlide_API->Fonts->OpenSans20)
#define OpenSans28                          (*DualSlide_API->Fonts->OpenSans28)
#define OpenSans36                          (*DualSlide_API->Fonts->OpenSans36)

#endif /* OPEN_SANS_H */
