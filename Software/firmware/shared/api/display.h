/**
 * @file display.h
 * @brief Platform-neutral display output contract.
 *
 * Application and renderer code use this interface to obtain a writable frame
 * buffer and present the completed frame.  The platform implementation owns
 * buffer allocation, display timing, and any window or hardware details.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

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
 * The unused most-significant byte is zero.  Backends may convert this value
 * to their native palette-register representation internally.
 */
typedef uint32_t Display_ColourTypeDef;

/** Number of entries in the CLUT8 palette. */
#define DISPLAY_PALETTE_SIZE (256U)

/**
 * @brief A writable frame supplied by the display backend.
 *
 * Pixels are stored row-major as 8-bit palette indices.  StridePixels may be
 * larger than Width to accommodate backend-specific alignment or padding.
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
 * @brief Initialise the platform display backend.
 *
 * @return true when the display is ready to acquire frames; otherwise false.
 */
bool Display_Init(void);

/**
 * @brief Replace a contiguous range of CLUT8 palette entries.
 *
 * The backend applies the new palette before it presents a subsequent frame.
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
 * The returned frame remains owned by the display backend.  The caller may
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
 * @return true when the backend accepted the frame; otherwise false.
 */
bool Display_PresentFrame(Display_FrameTypeDef *Frame);

/**
 * @brief Wait until the display can begin rendering a new frame.
 */
void Display_WaitForFrame(void);

#endif /* DISPLAY_H */