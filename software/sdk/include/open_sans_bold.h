/**
 * @file open_sans_bold.h
 * @brief Open Sans Bold, as built into the DualSlide firmware.
 *
 * Use the fonts exactly as in the firmware: &OpenSansBold20 is a
 * const Font *. See open_sans.h.
 */

#ifndef OPEN_SANS_BOLD_H
#define OPEN_SANS_BOLD_H

#include "dualslide_sdk.h"
#include "font.h"

#define OpenSansBold12                      (*DualSlide_API->Fonts->OpenSansBold12)
#define OpenSansBold16                      (*DualSlide_API->Fonts->OpenSansBold16)
#define OpenSansBold20                      (*DualSlide_API->Fonts->OpenSansBold20)
#define OpenSansBold28                      (*DualSlide_API->Fonts->OpenSansBold28)
#define OpenSansBold36                      (*DualSlide_API->Fonts->OpenSansBold36)

#endif /* OPEN_SANS_BOLD_H */
