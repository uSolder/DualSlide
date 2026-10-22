/**
 * @file display.h
 * @brief The screen: double-buffered frames of 8-bit palette colours.
 *
 * Application and renderer code obtain a writable frame buffer here and
 * present the completed frame. display.c owns the buffers, the display timing
 * and the LCD controller.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Supported pixel encodings for a display frame. */
typedef enum
{
    DISPLAY_PIXEL_FORMAT_CLUT8 = 0
} Display_PixelFormatTypeDef;

/** One 8-bit index into the active display colour lookup table. */
typedef uint8_t Display_PixelTypeDef;

/**
 * @brief An RGB888 palette colour, stored as 0x00RRGGBB.
 *
 * The unused most-significant byte is zero.  The display may convert this value
 * to their native palette-register representation internally.
 */
typedef uint32_t Display_ColourTypeDef;

/** Number of entries in the CLUT8 palette. */
#define DISPLAY_PALETTE_SIZE (256U)

/**
 * @brief A writable frame supplied by the display.
 *
 * Pixels are stored row-major as 8-bit palette indices, in the panel's own
 * portrait orientation: Width is 480 and Height 800.  StridePixels may be
 * larger than Width to accommodate display-specific alignment or padding.
 * render.h turns this into the 800 by 480 landscape space games draw in; see
 * Render_PixelAddress() there.
 */
typedef struct
{
    Display_PixelTypeDef *Pixels;
    uint16_t Width;
    uint16_t Height;
    uint32_t StridePixels;
    Display_PixelFormatTypeDef PixelFormat;
} Display_FrameTypeDef;

/**
 * @brief Initialise the display.
 *
 * @return true when the display is ready to acquire frames; otherwise false.
 */
bool Display_Init(void);

/**
 * @brief Replace a contiguous range of CLUT8 palette entries.
 *
 * The display applies the new palette before it presents a subsequent frame.
 * Callers must keep @p Colours valid only for the duration of this call.
 *
 * @param FirstEntry First palette index to replace.
 * @param Colours RGB888 colours in 0x00RRGGBB form.
 * @param EntryCount Number of palette entries to replace.
 * @return true when the palette range was accepted; otherwise false.
 */
bool Display_SetPalette(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount);

/**
 * @brief Acquire the next writable frame.
 *
 * The returned frame remains owned by the display.  The caller may
 * write its pixels until it passes the same pointer to Display_PresentFrame().
 * Returns NULL when a frame is temporarily unavailable.
 */
Display_FrameTypeDef *Display_AcquireFrame(void);

/**
 * @brief Queue a completed frame for presentation.
 *
 * After this call, the caller must no longer access @p Frame unless it obtains
 * that frame again through Display_AcquireFrame().
 *
 * @param Frame A frame returned by Display_AcquireFrame().
 * @return true when the display accepted the frame; otherwise false.
 */
bool Display_PresentFrame(Display_FrameTypeDef *Frame);

/**
 * @brief Wait until the display can begin rendering a new frame.
 */
void Display_WaitForFrame(void);

/**
 * @brief Set the screen brightness.
 *
 * The display maps the percentage onto a perceptual curve, so equal steps
 * look like equal changes in brightness.
 *
 * @param Percent 0 (darkest) to 100 (full brightness); larger values are clamped.
 * @return true when the brightness was applied; otherwise false.
 */
bool Display_SetBrightness(uint8_t Percent);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
