/**
 * @file render.c
 * @brief Rotating CLUT8 software renderer for the embedded display target.
 *
 * Drawing coordinates use the shared 800x480 landscape render space. Every
 * logical pixel is written counter-clockwise into the physical 480x800 CLUT8
 * framebuffer so the game appears upright in the opposite landscape
 * orientation:
 *
 *     physical_x = (RENDER_HEIGHT - 1) - logical_y
 *     physical_y = logical_x
 *
 * The target therefore describes the physical framebuffer and must be
 * 480 pixels wide by 800 pixels high.
 *
 * Rectangle fills, polygon spans, image blits, and rotated sprites calculate
 * rotated addresses directly. Per-pixel clipping and coordinate conversion
 * are avoided after a primitive has been clipped.
 */

#include "render.h"
#include "font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Physical framebuffer geometry                                              */
/* -------------------------------------------------------------------------- */

#define RENDER_PHYSICAL_WIDTH              (RENDER_HEIGHT)
#define RENDER_PHYSICAL_HEIGHT             (RENDER_WIDTH)

#define RENDER_PHYSICAL_X(LogicalY) \
    ((RENDER_HEIGHT - 1U) - (uint32_t)(LogicalY))

#define RENDER_PHYSICAL_Y(LogicalX) \
    ((uint32_t)(LogicalX))

/*
 * Fixed-point trigonometric scale used by rotated sprite drawing.
 */
#define RENDER_TRIG_SHIFT                  (15)
#define RENDER_TRIG_ONE                    (1L << RENDER_TRIG_SHIFT)

/* -------------------------------------------------------------------------- */
/* Private state                                                              */
/* -------------------------------------------------------------------------- */

static Render_RectTypeDef Render_ClipRect = {
    .X = 0, .Y = 0, .Width = RENDER_WIDTH, .Height = RENDER_HEIGHT
};

/* -------------------------------------------------------------------------- */
/* Trigonometric lookup                                                       */
/* -------------------------------------------------------------------------- */

/*
 * Sine values for 0 through 90 degrees in Q15 format.
 *
 * Tenths-of-a-degree angles are linearly interpolated between adjacent
 * entries. This avoids floating-point operations and libm dependencies.
 */
static const int16_t Render_SineQuarterWave[91] = {
         0,   572,  1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126, 5690,  6252,  6813,  7371,  7927,  8481,  9032,  9580, 10126, 10668, 11207, 11743, 12275, 12803, 13328, 13848, 14365, 14876, 15384, 15886, 16384, 16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622, 21063, 21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730, 25101, 25465, 25821, 26169, 26509, 26841, 27165, 27481, 27788, 28087, 28378, 28660, 28934, 29198, 29454, 29701, 29939, 30168, 30388, 30598, 30799, 30991, 31173, 31346, 31510, 31664, 31808, 31943, 32068, 32183, 32288, 32384, 32469, 32545, 32610, 32666, 32712, 32747, 32767, 32767, 32767
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Render_IsTargetValid(const Render_TargetTypeDef *Target);
static bool Render_IsImageValid(const Render_ImageTypeDef *Image);
static bool Render_IsLogicalPixelVisible(int32_t X, int32_t Y);
static inline void Render_WriteLogicalPixel(Render_TargetTypeDef *Target, int32_t X, int32_t Y, Render_ColourIndexTypeDef Colour);
static int32_t Render_MaxInt32(int32_t A, int32_t B);
static int32_t Render_MinInt32(int32_t A, int32_t B);
static int32_t Render_NormalizeAngleTenths(int32_t Angle);
static int32_t Render_SineQ15(Render_AngleTypeDef Angle);
static int32_t Render_CosineQ15(Render_AngleTypeDef Angle);

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static bool Render_IsTargetValid(const Render_TargetTypeDef *Target)
{
    if ((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    if ((Target->Width != RENDER_PHYSICAL_WIDTH) || (Target->Height != RENDER_PHYSICAL_HEIGHT))
    {
        return false;
    }

    if (Target->StridePixels < RENDER_PHYSICAL_WIDTH)
    {
        return false;
    }

    return true;
}

static bool Render_IsImageValid(const Render_ImageTypeDef *Image)
{
    if ((Image == NULL) || (Image->Pixels == NULL))
    {
        return false;
    }

    if ((Image->Width == 0U) || (Image->Height == 0U))
    {
        return false;
    }

    if (Image->StridePixels < Image->Width)
    {
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Geometry helpers                                                           */
/* -------------------------------------------------------------------------- */

static int32_t Render_MaxInt32(int32_t A, int32_t B)
{
    return (A > B) ? A : B;
}

static int32_t Render_MinInt32(int32_t A, int32_t B)
{
    return (A < B) ? A : B;
}

static bool Render_IsLogicalPixelVisible(int32_t X, int32_t Y)
{
    int32_t clip_right;
    int32_t clip_bottom;

    if ((X < 0) || (Y < 0) || (X >= (int32_t)RENDER_WIDTH) || (Y >= (int32_t)RENDER_HEIGHT))
    {
        return false;
    }

    clip_right = (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width;

    clip_bottom = (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height;

    return (X >= (int32_t)Render_ClipRect.X) && (Y >= (int32_t)Render_ClipRect.Y) && (X < clip_right) && (Y < clip_bottom);
}

static inline void Render_WriteLogicalPixel(Render_TargetTypeDef *Target, int32_t X, int32_t Y, Render_ColourIndexTypeDef Colour)
{
    uint32_t physical_x;
    uint32_t physical_y;
    uint32_t physical_index;

    if (!Render_IsLogicalPixelVisible(X, Y))
    {
        return;
    }

    physical_x = RENDER_PHYSICAL_X(Y);
    physical_y = RENDER_PHYSICAL_Y(X);

    physical_index = (physical_y * Target->StridePixels) + physical_x;

    Target->Pixels[physical_index] = Colour;
}

/* -------------------------------------------------------------------------- */
/* Fixed-point trigonometry                                                    */
/* -------------------------------------------------------------------------- */

static int32_t Render_NormalizeAngleTenths(int32_t Angle)
{
    Angle %= 3600;

    if (Angle < 0)
    {
        Angle += 3600;
    }

    return Angle;
}

static int32_t Render_SineQ15(Render_AngleTypeDef Angle)
{
    int32_t normalized;
    int32_t quadrant;
    int32_t within_quadrant;
    int32_t degree;
    int32_t fraction;
    int32_t low;
    int32_t high;
    int32_t value;

    normalized = Render_NormalizeAngleTenths((int32_t)Angle);
    quadrant = normalized / 900;
    within_quadrant = normalized % 900;

    if ((quadrant == 1) || (quadrant == 3))
    {
        within_quadrant = 900 - within_quadrant;
    }

    degree = within_quadrant / 10;
    fraction = within_quadrant % 10;

    if (degree >= 90)
    {
        value = Render_SineQuarterWave[90];
    }
    else
    {
        low = Render_SineQuarterWave[degree];
        high = Render_SineQuarterWave[degree + 1];

        value = low + (((high - low) * fraction) / 10);
    }

    if (quadrant >= 2)
    {
        value = -value;
    }

    return value;
}

static int32_t Render_CosineQ15(Render_AngleTypeDef Angle)
{
    return Render_SineQ15((Render_AngleTypeDef)((int32_t)Angle + 900));
}

/* -------------------------------------------------------------------------- */
/* Clipping                                                                   */
/* -------------------------------------------------------------------------- */

void Render_SetClipRect(const Render_RectTypeDef *Rect)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    if (Rect == NULL)
    {
        Render_ResetClipRect();
        return;
    }

    left = Render_MaxInt32((int32_t)Rect->X, 0);
    top = Render_MaxInt32((int32_t)Rect->Y, 0);

    right = Render_MinInt32((int32_t)Rect->X + (int32_t)Rect->Width, (int32_t)RENDER_WIDTH);

    bottom = Render_MinInt32((int32_t)Rect->Y + (int32_t)Rect->Height, (int32_t)RENDER_HEIGHT);

    if ((right <= left) || (bottom <= top))
    {
        Render_ClipRect.X = 0;
        Render_ClipRect.Y = 0;
        Render_ClipRect.Width = 0U;
        Render_ClipRect.Height = 0U;
        return;
    }

    Render_ClipRect.X = (int16_t)left;
    Render_ClipRect.Y = (int16_t)top;
    Render_ClipRect.Width = (uint16_t)(right - left);
    Render_ClipRect.Height = (uint16_t)(bottom - top);
}

void Render_ResetClipRect(void)
{
    Render_ClipRect.X = 0;
    Render_ClipRect.Y = 0;
    Render_ClipRect.Width = RENDER_WIDTH;
    Render_ClipRect.Height = RENDER_HEIGHT;
}

/* -------------------------------------------------------------------------- */
/* Primitive drawing                                                          */
/* -------------------------------------------------------------------------- */

void Render_Clear(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour)
{
    uint32_t physical_y;

    if (!Render_IsTargetValid(Target))
    {
        return;
    }

    if (Target->StridePixels == RENDER_PHYSICAL_WIDTH)
    {
        memset(Target->Pixels, Colour, (size_t)RENDER_PHYSICAL_WIDTH * (size_t)RENDER_PHYSICAL_HEIGHT);

        return;
    }

    for (physical_y = 0U; physical_y < RENDER_PHYSICAL_HEIGHT; physical_y++)
    {
        memset(&Target->Pixels[physical_y * Target->StridePixels], Colour, RENDER_PHYSICAL_WIDTH);
    }
}

void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t logical_x;
    uint32_t physical_y;
    uint32_t fill_length;
    Render_ColourIndexTypeDef *destination;

    if (!Render_IsTargetValid(Target) || (Rect == NULL))
    {
        return;
    }

    left = Render_MaxInt32((int32_t)Rect->X, (int32_t)Render_ClipRect.X);

    top = Render_MaxInt32((int32_t)Rect->Y, (int32_t)Render_ClipRect.Y);

    right = Render_MinInt32((int32_t)Rect->X + (int32_t)Rect->Width, (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width);

    bottom = Render_MinInt32((int32_t)Rect->Y + (int32_t)Rect->Height, (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height);

    left = Render_MaxInt32(left, 0);
    top = Render_MaxInt32(top, 0);
    right = Render_MinInt32(right, (int32_t)RENDER_WIDTH);
    bottom = Render_MinInt32(bottom, (int32_t)RENDER_HEIGHT);

    if ((right <= left) || (bottom <= top))
    {
        return;
    }

    /*
     * A logical horizontal rectangle becomes a set of contiguous physical
     * scanline spans after the fixed clockwise framebuffer rotation.
     */
    physical_y = (uint32_t)left;

    fill_length = (uint32_t)(bottom - top);

    destination = &Target->Pixels[(physical_y * Target->StridePixels) + RENDER_PHYSICAL_X(bottom - 1)];

    for (logical_x = left; logical_x < right; logical_x++)
    {
        memset(destination, Colour, fill_length);
        destination += Target->StridePixels;
    }
}
bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour)
{
    int32_t intersections[RENDER_POLYGON_MAX_VERTEX_COUNT];
    int32_t minimum_y;
    int32_t maximum_y;
    int32_t scan_y;
    int32_t x_start;
    int32_t x_end;
    int32_t temporary;
    int32_t x;
    uint8_t index;
    uint8_t next;
    uint8_t intersection_count;
    uint8_t sort_index;

    if (!Render_IsTargetValid(Target) || (Points == NULL))
    {
        return false;
    }

    if ((PointCount < 3U) || (PointCount > RENDER_POLYGON_MAX_VERTEX_COUNT))
    {
        return false;
    }

    minimum_y = Points[0].Y;
    maximum_y = Points[0].Y;

    for (index = 1U; index < PointCount; index++)
    {
        minimum_y = Render_MinInt32(minimum_y, Points[index].Y);
        maximum_y = Render_MaxInt32(maximum_y, Points[index].Y);
    }

    minimum_y = Render_MaxInt32(minimum_y, Render_ClipRect.Y);

    maximum_y = Render_MinInt32(maximum_y, (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height - 1);

    minimum_y = Render_MaxInt32(minimum_y, 0);
    maximum_y = Render_MinInt32(maximum_y, (int32_t)RENDER_HEIGHT - 1);

    for (scan_y = minimum_y; scan_y <= maximum_y; scan_y++)
    {
        intersection_count = 0U;

        for (index = 0U; index < PointCount; index++)
        {
            next = (uint8_t)((index + 1U) % PointCount);

            if (((Points[index].Y <= scan_y) && (Points[next].Y > scan_y)) || ((Points[next].Y <= scan_y) && (Points[index].Y > scan_y)))
            {
                intersections[intersection_count] = (int32_t)Points[index].X + (((scan_y - (int32_t)Points[index].Y) * ((int32_t)Points[next].X - (int32_t)Points[index].X)) / ((int32_t)Points[next].Y - (int32_t)Points[index].Y));

                intersection_count++;
            }
        }

        for (index = 1U; index < intersection_count; index++)
        {
            temporary = intersections[index];
            sort_index = index;

            while ((sort_index > 0U) && (intersections[sort_index - 1U] > temporary))
            {
                intersections[sort_index] = intersections[sort_index - 1U];

                sort_index--;
            }

            intersections[sort_index] = temporary;
        }

        for (index = 0U; (uint8_t)(index + 1U) < intersection_count; index = (uint8_t)(index + 2U))
        {
            x_start = intersections[index];
            x_end = intersections[index + 1U];

            x_start = Render_MaxInt32(x_start, Render_ClipRect.X);

            x_end = Render_MinInt32(x_end, (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width - 1);

            x_start = Render_MaxInt32(x_start, 0);
            x_end = Render_MinInt32(x_end, (int32_t)RENDER_WIDTH - 1);

            if (x_start <= x_end)
            {
                Render_ColourIndexTypeDef *destination = &Target->Pixels[((uint32_t)x_start * Target->StridePixels) + RENDER_PHYSICAL_X(scan_y)];

                for (x = x_start; x <= x_end; x++)
                {
                    *destination = Colour;
                    destination += Target->StridePixels;
                }
            }
        }
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Image drawing                                                              */
/* -------------------------------------------------------------------------- */

void Render_DrawImage(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y)
{
    Render_ImageRegionTypeDef region;

    if (!Render_IsImageValid(Image))
    {
        return;
    }

    region.X = 0U;
    region.Y = 0U;
    region.Width = Image->Width;
    region.Height = Image->Height;

    Render_DrawImageRegion(Target, Image, &region, X, Y);
}

void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y)
{
    int32_t destination_left;
    int32_t destination_top;
    int32_t destination_right;
    int32_t destination_bottom;
    uint32_t source_start_x;
    uint32_t source_start_y;
    uint32_t copy_width;
    uint32_t copy_height;
    uint32_t row;
    uint32_t column;
    const Render_ColourIndexTypeDef *source;
    Render_ColourIndexTypeDef *destination;
    Render_ColourIndexTypeDef colour;

    if (!Render_IsTargetValid(Target) || !Render_IsImageValid(Image) || (SourceRegion == NULL))
    {
        return;
    }

    if (((uint32_t)SourceRegion->X + (uint32_t)SourceRegion->Width > (uint32_t)Image->Width) || ((uint32_t)SourceRegion->Y + (uint32_t)SourceRegion->Height > (uint32_t)Image->Height))
    {
        return;
    }

    destination_left = Render_MaxInt32((int32_t)X, Render_MaxInt32((int32_t)Render_ClipRect.X, 0));

    destination_top = Render_MaxInt32((int32_t)Y, Render_MaxInt32((int32_t)Render_ClipRect.Y, 0));

    destination_right = Render_MinInt32((int32_t)X + (int32_t)SourceRegion->Width, Render_MinInt32((int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width, (int32_t)RENDER_WIDTH));

    destination_bottom = Render_MinInt32((int32_t)Y + (int32_t)SourceRegion->Height, Render_MinInt32((int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height, (int32_t)RENDER_HEIGHT));

    if ((destination_right <= destination_left) || (destination_bottom <= destination_top))
    {
        return;
    }

    source_start_x = (uint32_t)SourceRegion->X + (uint32_t)(destination_left - (int32_t)X);

    source_start_y = (uint32_t)SourceRegion->Y + (uint32_t)(destination_top - (int32_t)Y);

    copy_width = (uint32_t)(destination_right - destination_left);

    copy_height = (uint32_t)(destination_bottom - destination_top);

    for (row = 0U; row < copy_height; row++)
    {
        source = &Image->Pixels[((source_start_y + row) * Image->StridePixels) + source_start_x];

        destination = &Target->Pixels[((uint32_t)destination_left * Target->StridePixels) + RENDER_PHYSICAL_X((uint32_t)destination_top + row)];

        if (!Image->HasTransparentColour)
        {
            for (column = 0U; column < copy_width; column++)
            {
                *destination = source[column];
                destination += Target->StridePixels;
            }
        }
        else
        {
            for (column = 0U; column < copy_width; column++)
            {
                colour = source[column];

                if (colour != Image->TransparentColour)
                {
                    *destination = colour;
                }

                destination += Target->StridePixels;
            }
        }
    }
}
void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle)
{
    int32_t sine;
    int32_t cosine;
    int32_t half_width_q15;
    int32_t half_height_q15;
    int32_t radius;
    int32_t destination_left;
    int32_t destination_top;
    int32_t destination_right;
    int32_t destination_bottom;
    int32_t destination_x;
    int32_t destination_y;
    int32_t relative_x_q15;
    int32_t relative_y_q15;
    int32_t source_x_q15;
    int32_t source_y_q15;
    int32_t source_x;
    int32_t source_y;
    uint32_t source_index;
    Render_ColourIndexTypeDef *destination;
    Render_ColourIndexTypeDef colour;

    if (!Render_IsTargetValid(Target) || !Render_IsImageValid(Image))
    {
        return;
    }

    sine = Render_SineQ15(Angle);
    cosine = Render_CosineQ15(Angle);

    half_width_q15 = (int32_t)Image->Width << (RENDER_TRIG_SHIFT - 1);

    half_height_q15 = (int32_t)Image->Height << (RENDER_TRIG_SHIFT - 1);

    radius = ((int32_t)Image->Width + (int32_t)Image->Height + 1) / 2;

    destination_left = Render_MaxInt32((int32_t)CentreX - radius, Render_MaxInt32((int32_t)Render_ClipRect.X, 0));

    destination_top = Render_MaxInt32((int32_t)CentreY - radius, Render_MaxInt32((int32_t)Render_ClipRect.Y, 0));

    destination_right = Render_MinInt32((int32_t)CentreX + radius, Render_MinInt32((int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width - 1, (int32_t)RENDER_WIDTH - 1));

    destination_bottom = Render_MinInt32((int32_t)CentreY + radius, Render_MinInt32((int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height - 1, (int32_t)RENDER_HEIGHT - 1));

    if ((destination_right < destination_left) || (destination_bottom < destination_top))
    {
        return;
    }

    relative_x_q15 = ((destination_left - (int32_t)CentreX) << RENDER_TRIG_SHIFT) + (RENDER_TRIG_ONE / 2);

    for (destination_y = destination_top; destination_y <= destination_bottom; destination_y++)
    {
        relative_y_q15 = ((destination_y - (int32_t)CentreY) << RENDER_TRIG_SHIFT) + (RENDER_TRIG_ONE / 2);

        /*
         * Perform the expensive transform once at the beginning of each row.
         * Moving one logical pixel right then advances source X by cosine and
         * source Y by negative sine.
         */
        source_x_q15 = (int32_t)(((((int64_t)cosine * relative_x_q15) + ((int64_t)sine * relative_y_q15)) >> RENDER_TRIG_SHIFT) + half_width_q15);

        source_y_q15 = (int32_t)((((-(int64_t)sine * relative_x_q15) + ((int64_t)cosine * relative_y_q15)) >> RENDER_TRIG_SHIFT) + half_height_q15);

        destination = &Target->Pixels[((uint32_t)destination_left * Target->StridePixels) + RENDER_PHYSICAL_X(destination_y)];

        for (destination_x = destination_left; destination_x <= destination_right; destination_x++)
        {
            source_x = source_x_q15 >> RENDER_TRIG_SHIFT;

            source_y = source_y_q15 >> RENDER_TRIG_SHIFT;

            if ((source_x >= 0) && (source_y >= 0) && (source_x < (int32_t)Image->Width) && (source_y < (int32_t)Image->Height))
            {
                source_index = ((uint32_t)source_y * Image->StridePixels) + (uint32_t)source_x;

                colour = Image->Pixels[source_index];

                if (!Image->HasTransparentColour || (colour != Image->TransparentColour))
                {
                    *destination = colour;
                }
            }

            source_x_q15 += cosine;
            source_y_q15 -= sine;
            destination += Target->StridePixels;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Text drawing                                                               */
/* -------------------------------------------------------------------------- */

void Render_DrawText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    const FontGlyph *glyph;
    const uint8_t *glyph_bitmap;
    uint32_t codepoint;
    uint32_t bit_index;
    uint32_t byte_index;
    uint16_t row;
    uint16_t column;
    uint8_t bit_mask;
    int32_t cursor_x;
    int32_t baseline_y;
    int32_t destination_x;
    int32_t destination_y;

    if (!Render_IsTargetValid(Target) || (FontAsset == NULL) || (FontAsset->bitmap == NULL) || (FontAsset->glyphs == NULL) || (FontAsset->glyphCount == 0U) || (Text == NULL))
    {
        return;
    }

    cursor_x = (int32_t)X;
    baseline_y = (int32_t)Y + (int32_t)FontAsset->ascent;

    while (*Text != '\0')
    {
        codepoint = (uint8_t)*Text;
        Text++;

        if (codepoint == (uint32_t)'\n')
        {
            cursor_x = (int32_t)X;
            baseline_y += (int32_t)FontAsset->lineHeight;
            continue;
        }

        if (codepoint == (uint32_t)'\r')
        {
            continue;
        }

        glyph = Font_GetGlyph(FontAsset, codepoint);

        if (glyph == NULL)
        {
            codepoint = (uint32_t)'?';

            glyph = Font_GetGlyph(FontAsset, codepoint);

            if (glyph == NULL)
            {
                continue;
            }
        }

        glyph_bitmap = &FontAsset->bitmap[glyph->bitmapOffset];

        for (row = 0U; row < glyph->height; row++)
        {
            for (column = 0U; column < glyph->width; column++)
            {
                bit_index = ((uint32_t)row * (uint32_t)glyph->width) + (uint32_t)column;

                byte_index = bit_index >> 3U;
                bit_mask = (uint8_t)(0x80U >> (bit_index & 7U));

                if ((glyph_bitmap[byte_index] & bit_mask) == 0U)
                {
                    continue;
                }

                destination_x = cursor_x + (int32_t)glyph->offsetX + (int32_t)column;

                destination_y = baseline_y + (int32_t)glyph->offsetY + (int32_t)row;

                if (Render_IsLogicalPixelVisible(destination_x, destination_y))
                {
                    Target->Pixels[((uint32_t)destination_x * Target->StridePixels) + RENDER_PHYSICAL_X(destination_y)] = Colour;
                }
            }
        }

        cursor_x += (int32_t)glyph->advance;
    }
}