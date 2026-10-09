/**
 * @file render.h
 * @brief 2D drawing for the 800 x 480 screen.
 *
 * QUICK START
 *
 *   Your app's Render function is handed a Target to draw on. Draw the whole
 *   screen every frame, back to front:
 *
 *       Render_Clear(Target, RENDER_BLACK);
 *       Render_Box(Target, 100, 100, 200, 50, RENDER_BLUE);          // filled rectangle
 *       Render_FillCircle(Target, 400, 240, 30, RENDER_YELLOW);      // ball
 *       Render_DrawLine(Target, 0, 479, 799, 0, 3, RENDER_RED);      // 3 pixels thick
 *       Render_DrawTextAligned(Target, &OpenSansBold36, "GAME OVER", 400, 200, RENDER_ALIGN_CENTRE, RENDER_WHITE);
 *       Render_DrawTextf(Target, &OpenSans20, 10, 10, RENDER_WHITE, "SCORE %d", Score);
 *
 *   X runs left to right (0 to 799), Y top to bottom (0 to 479). Text is
 *   placed by its top edge. Anything off screen is simply not drawn.
 *
 * COLOURS
 *
 *   Colours are palette numbers. An app without a palette of its own gets the
 *   standard palette, whose colours are named below (RENDER_RED and so on).
 *   An app can instead give its own list of up to 128 colours.
 *
 * Render functions only write into the target supplied by their caller. They
 * never allocate, acquire, retain, or present a framebuffer.
 */

#ifndef RENDER_H
#define RENDER_H

#include "font.h"

#include <stdbool.h>
#include <stdint.h>

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
 * @brief Where text sits relative to the X given to Render_DrawTextAligned().
 */
typedef enum
{
    RENDER_ALIGN_LEFT = 0, /**< X is the text's left edge. */
    RENDER_ALIGN_CENTRE,   /**< X is the text's centre. */
    RENDER_ALIGN_RIGHT     /**< X is the text's right edge. */
} Render_AlignTypeDef;

/* -------------------------------------------------------------------------- */
/* The standard palette                                                       */
/* -------------------------------------------------------------------------- */

/*
 * Colours of the standard palette, used by apps that don't supply their own.
 * In an app with its own palette, these numbers mean that app's colours.
 */
#define RENDER_BLACK                         ((Render_ColourIndexTypeDef)0U)
#define RENDER_WHITE                         ((Render_ColourIndexTypeDef)1U)
#define RENDER_GREY                          ((Render_ColourIndexTypeDef)2U)
#define RENDER_DARK_GREY                     ((Render_ColourIndexTypeDef)3U)
#define RENDER_LIGHT_GREY                    ((Render_ColourIndexTypeDef)4U)
#define RENDER_RED                           ((Render_ColourIndexTypeDef)5U)
#define RENDER_DARK_RED                      ((Render_ColourIndexTypeDef)6U)
#define RENDER_ORANGE                        ((Render_ColourIndexTypeDef)7U)
#define RENDER_YELLOW                        ((Render_ColourIndexTypeDef)8U)
#define RENDER_GREEN                         ((Render_ColourIndexTypeDef)9U)
#define RENDER_DARK_GREEN                    ((Render_ColourIndexTypeDef)10U)
#define RENDER_CYAN                          ((Render_ColourIndexTypeDef)11U)
#define RENDER_BLUE                          ((Render_ColourIndexTypeDef)12U)
#define RENDER_DARK_BLUE                     ((Render_ColourIndexTypeDef)13U)
#define RENDER_PURPLE                        ((Render_ColourIndexTypeDef)14U)
#define RENDER_PINK                          ((Render_ColourIndexTypeDef)15U)
#define RENDER_BROWN                         ((Render_ColourIndexTypeDef)16U)
#define RENDER_SKY                           ((Render_ColourIndexTypeDef)17U)

/** Number of colours in the standard palette. */
#define RENDER_STANDARD_COLOUR_COUNT         (18U)

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

/* -------------------------------------------------------------------------- */
/* Shapes and text helpers                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fill a rectangle given by its top-left corner and size.
 */
void Render_Box(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour);

/**
 * @brief Draw a rectangle's outline, Thickness pixels wide, inside its edges.
 */
void Render_DrawRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Thickness, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a rectangle with rounded corners of the given Radius.
 */
void Render_FillRoundRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Radius, Render_ColourIndexTypeDef Colour);

/**
 * @brief Fill a circle given by its centre and radius.
 */
void Render_FillCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, Render_ColourIndexTypeDef Colour);

/**
 * @brief Draw a ring: a circle's outline, Thickness pixels wide, inside its radius.
 */
void Render_DrawCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, uint16_t Thickness, Render_ColourIndexTypeDef Colour);

/**
 * @brief Draw a straight line from (X1, Y1) to (X2, Y2), Thickness pixels wide.
 */
void Render_DrawLine(Render_TargetTypeDef *Target, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint16_t Thickness, Render_ColourIndexTypeDef Colour);

/**
 * @brief Width in pixels of Text in a font; for several lines, the widest line.
 */
uint16_t Render_TextWidth(const Font *FontAsset, const char *Text);

/**
 * @brief Draw text with its left edge, centre or right edge at X.
 */
void Render_DrawTextAligned(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_AlignTypeDef Align, Render_ColourIndexTypeDef Colour);

/**
 * @brief Format text into Buffer, like a small snprintf().
 *
 * Supports %d %i %u %x %c %s and %%, with an optional width (%3d), zero
 * padding (%03d), left alignment (%-8s), or a width taken from the arguments
 * (%0*d). The text is always terminated and cut to fit.
 *
 * @return Buffer, so the result can be passed straight to a draw call.
 */
char *Render_FormatText(char *Buffer, uint32_t Size, const char *Format, ...);

/**
 * @brief Draw formatted text with its top-left corner at X, Y.
 *
 * Formats like Render_FormatText(), up to 127 characters:
 * Render_DrawTextf(Target, &OpenSans20, 10, 10, RENDER_WHITE, "SCORE %d", Score).
 */
void Render_DrawTextf(Render_TargetTypeDef *Target, const Font *FontAsset, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour, const char *Format, ...);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_H */
