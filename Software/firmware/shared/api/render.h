/**
 * @file render.h
 * @brief Stateless 2D drawing contract for generic CLUT8 pixel targets.
 *
 * Render functions only write into the target supplied by their caller. They
 * never allocate, acquire, retain, or present a framebuffer.
 */

#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RENDER_WIDTH                         (800U)
#define RENDER_HEIGHT                        (480U)
#define RENDER_POLYGON_MAX_VERTEX_COUNT      (32U)
#define RENDER_ANGLE_TENTHS_PER_DEGREE       (10)

/**
 * @brief One CLUT8 palette index.
 */
typedef uint8_t Render_ColourIndexTypeDef;

/**
 * @brief A signed clockwise rotation in tenths of a degree.
 *
 * Zero is upright. Positive angles rotate clockwise in the render target's
 * coordinate system, where Y increases downward.
 */
typedef int16_t Render_AngleTypeDef;

/**
 * @brief A writable row-major CLUT8 render target.
 *
 * Render functions require an 800 by 480 logical target. StridePixels may be
 * larger than Width when the caller's backing buffer has row padding.
 */
typedef struct
{
    Render_ColourIndexTypeDef *Pixels;
    uint16_t Width;
    uint16_t Height;
    uint32_t StridePixels;
} Render_TargetTypeDef;

/**
 * @brief A logical rectangle in the 800 by 480 render space.
 */
typedef struct
{
    int16_t X;
    int16_t Y;
    uint16_t Width;
    uint16_t Height;
} Render_RectTypeDef;

/**
 * @brief One logical vertex in the 800 by 480 render space.
 */
typedef struct
{
    int16_t X;
    int16_t Y;
} Render_PointTypeDef;

/**
 * @brief A rectangular region within a source image.
 */
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
 * Pixels are palette indices. When HasTransparentColour is true, pixels equal
 * to TransparentColour are not written to the target frame.
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
 * @brief Restrict subsequent drawing commands to a logical rectangular area.
 *
 * Passing NULL restores clipping to the complete render area.
 */
void Render_SetClipRect(const Render_RectTypeDef *Rect);

/**
 * @brief Restore clipping to the complete logical render area.
 */
void Render_ResetClipRect(void);

/**
 * @brief Fill the entire provided target with one palette index.
 *
 * @param Target Writable CLUT8 render target.
 * @param Colour Palette index written to every pixel.
 */
void Render_Clear(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a logical rectangle with one palette index.
 *
 * Pixels outside the current clipping region are not written.
 *
 * @param Target Writable CLUT8 render target.
 * @param Rect Logical rectangle to fill.
 * @param Colour Palette index written to the rectangle.
 */
void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a simple closed polygon with one palette index.
 *
 * Points must contain between three and
 * RENDER_POLYGON_MAX_VERTEX_COUNT vertices in perimeter order. Concave
 * polygons are supported. Self-intersecting polygons are invalid.
 *
 * @param Target Writable CLUT8 render target.
 * @param Points Polygon vertices in perimeter order.
 * @param PointCount Number of supplied vertices.
 * @param Colour Palette index written inside the polygon.
 *
 * @return true when the polygon arguments were accepted; otherwise false.
 */
bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour);

/**
 * @brief Draw a complete image with its top-left corner at X, Y.
 *
 * @param Target Writable CLUT8 render target.
 * @param Image Source CLUT8 image.
 * @param X Logical destination X coordinate.
 * @param Y Logical destination Y coordinate.
 */
void Render_DrawImage(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y);

/**
 * @brief Draw a rectangular region from an image at X, Y.
 *
 * @param Target Writable CLUT8 render target.
 * @param Image Source CLUT8 image.
 * @param SourceRegion Region within the source image.
 * @param X Logical destination X coordinate.
 * @param Y Logical destination Y coordinate.
 */
void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y);

/**
 * @brief Draw an image rotated around its geometric centre.
 *
 * The source remains upright and row-major in memory. CentreX and CentreY
 * select the image centre in the render target. Pixels use nearest-neighbour
 * sampling, and transparent source pixels remain unwritten.
 *
 * This operation is intended for small dynamic sprites. It costs
 * substantially more than an unrotated image blit and is unsuitable for
 * full-screen backgrounds.
 *
 * @param Target Writable CLUT8 render target.
 * @param Image Source CLUT8 image.
 * @param CentreX Logical X coordinate of the image centre.
 * @param CentreY Logical Y coordinate of the image centre.
 * @param Angle Clockwise rotation in tenths of a degree.
 */
void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle);

/**
 * @brief Draw a string using a packed proportional 1-bit font.
 *
 * X and Y identify the top-left corner of the first text line. Glyph metrics
 * are interpreted relative to the font baseline. Newline characters move the
 * cursor to the beginning of the next line. Carriage returns are ignored.
 *
 * Unsupported characters are replaced with '?' when that glyph exists in the
 * font.
 *
 * @param Target Writable CLUT8 render target.
 * @param FontAsset Packed bitmap-font asset.
 * @param Text Null-terminated byte string.
 * @param X Logical X coordinate of the first line.
 * @param Y Logical Y coordinate of the first line.
 * @param Colour Palette index used for set glyph pixels.
 */
void Render_DrawText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_H */