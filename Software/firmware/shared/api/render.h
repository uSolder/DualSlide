/**
 * @file render.h
 * @brief Stateless 2D drawing contract for generic CLUT8 pixel targets.
 *
 * Render functions only write into the target supplied by their caller.  They
 * never allocate, acquire, retain, or present a framebuffer.
 */

#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RENDER_WIDTH    (800U)
#define RENDER_HEIGHT   (480U)
#define RENDER_POLYGON_MAX_VERTEX_COUNT    (32U)

/** One CLUT8 palette index. */
typedef uint8_t Render_ColourIndexTypeDef;

/**
 * @brief A signed clockwise rotation in tenths of a degree.
 *
 * Zero is upright.  Positive angles rotate clockwise in the render target's
 * coordinate system, where Y increases downward.
 */
typedef int16_t Render_AngleTypeDef;

#define RENDER_ANGLE_TENTHS_PER_DEGREE    (10)

/**
 * @brief A writable row-major CLUT8 render target.
 *
 * Render functions require an 800 by 480 target.  StridePixels may be larger
 * than Width when the caller's backing buffer has row padding.
 */
typedef struct
{
    Render_ColourIndexTypeDef *Pixels;
    uint16_t Width;
    uint16_t Height;
    uint32_t StridePixels;
} Render_TargetTypeDef;

/** A logical rectangle in the 800 by 480 render space. */
typedef struct
{
    int16_t X;
    int16_t Y;
    uint16_t Width;
    uint16_t Height;
} Render_RectTypeDef;

/** One logical vertex in the 800 by 480 render space. */
typedef struct
{
    int16_t X;
    int16_t Y;
} Render_PointTypeDef;

/** A rectangular region within a source image. */
typedef struct
{
    uint16_t X;
    uint16_t Y;
    uint16_t Width;
    uint16_t Height;
} Render_ImageRegionTypeDef;

/**
 * @brief An upright, row-major CLUT8 image.
 *
 * Pixels are palette indices.  When HasTransparentColour is true, pixels
 * equal to TransparentColour are not written to the target frame.
 */
typedef struct
{
    const Render_ColourIndexTypeDef *Pixels;
    uint16_t Width;
    uint16_t Height;
    uint16_t StridePixels;
    bool HasTransparentColour;
    Render_ColourIndexTypeDef TransparentColour;
} Render_ImageTypeDef;

/**
 * @brief A fixed-cell, one-bit source font.
 *
 * GlyphBits contains GlyphCount consecutive glyphs.  Each glyph occupies
 * GlyphStrideBytes bytes.  Rows are RowStrideBytes bytes apart and bits are
 * read most-significant-bit first, so the leftmost pixel is bit 7 of the
 * first byte.  Character codes start at FirstCharacter.
 */
typedef struct
{
    const uint8_t *GlyphBits;
    uint16_t GlyphCount;
    uint16_t GlyphStrideBytes;
    uint8_t FirstCharacter;
    uint8_t GlyphWidth;
    uint8_t GlyphHeight;
    uint8_t RowStrideBytes;
} Render_FontTypeDef;

/**
 * @brief Restrict subsequent drawing commands to a logical rectangular area.
 */
void Render_SetClipRect(const Render_RectTypeDef *Rect);

/**
 * @brief Restore clipping to the complete logical render area.
 */
void Render_ResetClipRect(void);

/**
 * @brief Fill the entire provided target with one palette index.
 *
 * @p Target must be a writable 800 by 480 CLUT8 render target.
 */
void Render_Clear(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a logical rectangle in @p Target with one palette index.
 *
 * Pixels outside the current clipping region are not written.
 */
void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a simple closed polygon with one palette index.
 *
 * @p Points must contain three to RENDER_POLYGON_MAX_VERTEX_COUNT vertices in
 * perimeter order.  Concave polygons are supported; self-intersecting
 * polygons are invalid.  Pixels outside the current clipping region are not
 * written.
 *
 * @return true when the polygon arguments were accepted; otherwise false.
 */
bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour);

/**
 * @brief Draw a complete image in @p Target with its top-left corner at @p X, @p Y.
 */
void Render_DrawImage(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y);

/**
 * @brief Draw an image rotated around its geometric centre.
 *
 * The source remains upright and row-major in memory.  @p CentreX and
 * @p CentreY select the image centre in the render target.  Pixels use
 * nearest-neighbour sampling; transparent source pixels remain unwritten.
 *
 * This operation is intended for small dynamic sprites such as vehicles.  It
 * costs substantially more than an unrotated image blit and is unsuitable for
 * full-screen backgrounds.
 */
void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle);

/**
 * @brief Draw a rectangular region from an image in @p Target at @p X, @p Y.
 */
void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y);

/**
 * @brief Draw a byte string in @p Target using a fixed-cell shared font asset.
 *
 * Bytes outside the font's declared character range are skipped.
 */
void Render_DrawText(Render_TargetTypeDef *Target, const Render_FontTypeDef *Font, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_H */