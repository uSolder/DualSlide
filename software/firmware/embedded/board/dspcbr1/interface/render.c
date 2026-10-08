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
    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    if((Target->Width != RENDER_PHYSICAL_WIDTH) || (Target->Height != RENDER_PHYSICAL_HEIGHT))
    {
        return false;
    }

    if(Target->StridePixels < RENDER_PHYSICAL_WIDTH)
    {
        return false;
    }

    return true;
}

static bool Render_IsImageValid(const Render_ImageTypeDef *Image)
{
    if((Image == NULL) || (Image->Pixels == NULL))
    {
        return false;
    }

    if((Image->Width == 0U) || (Image->Height == 0U))
    {
        return false;
    }

    if(Image->StridePixels < Image->Width)
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
    int32_t ClipRight;
    int32_t ClipBottom;

    if((X < 0) || (Y < 0) || (X >= (int32_t)RENDER_WIDTH) || (Y >= (int32_t)RENDER_HEIGHT))
    {
        return false;
    }

    ClipRight = (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width;

    ClipBottom = (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height;

    return (X >= (int32_t)Render_ClipRect.X) && (Y >= (int32_t)Render_ClipRect.Y) && (X < ClipRight) && (Y < ClipBottom);
}

static inline void Render_WriteLogicalPixel(Render_TargetTypeDef *Target, int32_t X, int32_t Y, Render_ColourIndexTypeDef Colour)
{
    uint32_t PhysicalX;
    uint32_t PhysicalY;
    uint32_t PhysicalIndex;

    if(!Render_IsLogicalPixelVisible(X, Y))
    {
        return;
    }

    PhysicalX = RENDER_PHYSICAL_X(Y);
    PhysicalY = RENDER_PHYSICAL_Y(X);

    PhysicalIndex = (PhysicalY * Target->StridePixels) + PhysicalX;

    Target->Pixels[PhysicalIndex] = Colour;
}

/* -------------------------------------------------------------------------- */
/* Fixed-point trigonometry                                                    */
/* -------------------------------------------------------------------------- */

static int32_t Render_NormalizeAngleTenths(int32_t Angle)
{
    Angle %= 3600;

    if(Angle < 0)
    {
        Angle += 3600;
    }

    return Angle;
}

static int32_t Render_SineQ15(Render_AngleTypeDef Angle)
{
    int32_t Normalized;
    int32_t Quadrant;
    int32_t WithinQuadrant;
    int32_t Degree;
    int32_t Fraction;
    int32_t Low;
    int32_t High;
    int32_t Value;

    Normalized = Render_NormalizeAngleTenths((int32_t)Angle);
    Quadrant = Normalized / 900;
    WithinQuadrant = Normalized % 900;

    if((Quadrant == 1) || (Quadrant == 3))
    {
        WithinQuadrant = 900 - WithinQuadrant;
    }

    Degree = WithinQuadrant / 10;
    Fraction = WithinQuadrant % 10;

    if(Degree >= 90)
    {
        Value = Render_SineQuarterWave[90];
    }
    else
    {
        Low = Render_SineQuarterWave[Degree];
        High = Render_SineQuarterWave[Degree + 1];

        Value = Low + (((High - Low) * Fraction) / 10);
    }

    if(Quadrant >= 2)
    {
        Value = -Value;
    }

    return Value;
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
    int32_t Left;
    int32_t Top;
    int32_t Right;
    int32_t Bottom;

    if(Rect == NULL)
    {
        Render_ResetClipRect();
        return;
    }

    Left = Render_MaxInt32((int32_t)Rect->X, 0);
    Top = Render_MaxInt32((int32_t)Rect->Y, 0);

    Right = Render_MinInt32((int32_t)Rect->X + (int32_t)Rect->Width, (int32_t)RENDER_WIDTH);

    Bottom = Render_MinInt32((int32_t)Rect->Y + (int32_t)Rect->Height, (int32_t)RENDER_HEIGHT);

    if((Right <= Left) || (Bottom <= Top))
    {
        Render_ClipRect.X = 0;
        Render_ClipRect.Y = 0;
        Render_ClipRect.Width = 0U;
        Render_ClipRect.Height = 0U;
        return;
    }

    Render_ClipRect.X = (int16_t)Left;
    Render_ClipRect.Y = (int16_t)Top;
    Render_ClipRect.Width = (uint16_t)(Right - Left);
    Render_ClipRect.Height = (uint16_t)(Bottom - Top);
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
    uint32_t PhysicalY;

    if(!Render_IsTargetValid(Target))
    {
        return;
    }

    if(Target->StridePixels == RENDER_PHYSICAL_WIDTH)
    {
        memset(Target->Pixels, Colour, (size_t)RENDER_PHYSICAL_WIDTH * (size_t)RENDER_PHYSICAL_HEIGHT);

        return;
    }

    for(PhysicalY = 0U; PhysicalY < RENDER_PHYSICAL_HEIGHT; PhysicalY++)
    {
        memset(&Target->Pixels[PhysicalY * Target->StridePixels], Colour, RENDER_PHYSICAL_WIDTH);
    }
}

void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour)
{
    int32_t Left;
    int32_t Top;
    int32_t Right;
    int32_t Bottom;
    int32_t LogicalX;
    uint32_t PhysicalY;
    uint32_t FillLength;
    Render_ColourIndexTypeDef *Destination;

    if(!Render_IsTargetValid(Target) || (Rect == NULL))
    {
        return;
    }

    Left = Render_MaxInt32((int32_t)Rect->X, (int32_t)Render_ClipRect.X);

    Top = Render_MaxInt32((int32_t)Rect->Y, (int32_t)Render_ClipRect.Y);

    Right = Render_MinInt32((int32_t)Rect->X + (int32_t)Rect->Width, (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width);

    Bottom = Render_MinInt32((int32_t)Rect->Y + (int32_t)Rect->Height, (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height);

    Left = Render_MaxInt32(Left, 0);
    Top = Render_MaxInt32(Top, 0);
    Right = Render_MinInt32(Right, (int32_t)RENDER_WIDTH);
    Bottom = Render_MinInt32(Bottom, (int32_t)RENDER_HEIGHT);

    if((Right <= Left) || (Bottom <= Top))
    {
        return;
    }

    /*
     * A logical horizontal rectangle becomes a set of contiguous physical
     * scanline spans after the fixed clockwise framebuffer rotation.
     */
    PhysicalY = (uint32_t)Left;

    FillLength = (uint32_t)(Bottom - Top);

    Destination = &Target->Pixels[(PhysicalY * Target->StridePixels) + RENDER_PHYSICAL_X(Bottom - 1)];

    for(LogicalX = Left; LogicalX < Right; LogicalX++)
    {
        memset(Destination, Colour, FillLength);
        Destination += Target->StridePixels;
    }
}
bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour)
{
    int32_t Intersections[RENDER_POLYGON_MAX_VERTEX_COUNT];
    int32_t MinimumY;
    int32_t MaximumY;
    int32_t ScanY;
    int32_t XStart;
    int32_t XEnd;
    int32_t Temporary;
    int32_t X;
    uint8_t Index;
    uint8_t Next;
    uint8_t IntersectionCount;
    uint8_t SortIndex;

    if(!Render_IsTargetValid(Target) || (Points == NULL))
    {
        return false;
    }

    if((PointCount < 3U) || (PointCount > RENDER_POLYGON_MAX_VERTEX_COUNT))
    {
        return false;
    }

    MinimumY = Points[0].Y;
    MaximumY = Points[0].Y;

    for(Index = 1U; Index < PointCount; Index++)
    {
        MinimumY = Render_MinInt32(MinimumY, Points[Index].Y);
        MaximumY = Render_MaxInt32(MaximumY, Points[Index].Y);
    }

    MinimumY = Render_MaxInt32(MinimumY, Render_ClipRect.Y);

    MaximumY = Render_MinInt32(MaximumY, (int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height - 1);

    MinimumY = Render_MaxInt32(MinimumY, 0);
    MaximumY = Render_MinInt32(MaximumY, (int32_t)RENDER_HEIGHT - 1);

    for(ScanY = MinimumY; ScanY <= MaximumY; ScanY++)
    {
        IntersectionCount = 0U;

        for(Index = 0U; Index < PointCount; Index++)
        {
            Next = (uint8_t)((Index + 1U) % PointCount);

            if(((Points[Index].Y <= ScanY) && (Points[Next].Y > ScanY)) || ((Points[Next].Y <= ScanY) && (Points[Index].Y > ScanY)))
            {
                Intersections[IntersectionCount] = (int32_t)Points[Index].X + (((ScanY - (int32_t)Points[Index].Y) * ((int32_t)Points[Next].X - (int32_t)Points[Index].X)) / ((int32_t)Points[Next].Y - (int32_t)Points[Index].Y));

                IntersectionCount++;
            }
        }

        for(Index = 1U; Index < IntersectionCount; Index++)
        {
            Temporary = Intersections[Index];
            SortIndex = Index;

            while((SortIndex > 0U) && (Intersections[SortIndex - 1U] > Temporary))
            {
                Intersections[SortIndex] = Intersections[SortIndex - 1U];

                SortIndex--;
            }

            Intersections[SortIndex] = Temporary;
        }

        for(Index = 0U; (uint8_t)(Index + 1U) < IntersectionCount; Index = (uint8_t)(Index + 2U))
        {
            XStart = Intersections[Index];
            XEnd = Intersections[Index + 1U];

            XStart = Render_MaxInt32(XStart, Render_ClipRect.X);

            XEnd = Render_MinInt32(XEnd, (int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width - 1);

            XStart = Render_MaxInt32(XStart, 0);
            XEnd = Render_MinInt32(XEnd, (int32_t)RENDER_WIDTH - 1);

            if(XStart <= XEnd)
            {
                Render_ColourIndexTypeDef *Destination = &Target->Pixels[((uint32_t)XStart * Target->StridePixels) + RENDER_PHYSICAL_X(ScanY)];

                for(X = XStart; X <= XEnd; X++)
                {
                    *Destination = Colour;
                    Destination += Target->StridePixels;
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
    Render_ImageRegionTypeDef Region;

    if(!Render_IsImageValid(Image))
    {
        return;
    }

    Region.X = 0U;
    Region.Y = 0U;
    Region.Width = Image->Width;
    Region.Height = Image->Height;

    Render_DrawImageRegion(Target, Image, &Region, X, Y);
}

void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y)
{
    int32_t DestinationLeft;
    int32_t DestinationTop;
    int32_t DestinationRight;
    int32_t DestinationBottom;
    uint32_t SourceStartX;
    uint32_t SourceStartY;
    uint32_t CopyWidth;
    uint32_t CopyHeight;
    uint32_t Row;
    uint32_t Column;
    const Render_ColourIndexTypeDef *Source;
    Render_ColourIndexTypeDef *Destination;
    Render_ColourIndexTypeDef Colour;

    if(!Render_IsTargetValid(Target) || !Render_IsImageValid(Image) || (SourceRegion == NULL))
    {
        return;
    }

    if(((uint32_t)SourceRegion->X + (uint32_t)SourceRegion->Width > (uint32_t)Image->Width) || ((uint32_t)SourceRegion->Y + (uint32_t)SourceRegion->Height > (uint32_t)Image->Height))
    {
        return;
    }

    DestinationLeft = Render_MaxInt32((int32_t)X, Render_MaxInt32((int32_t)Render_ClipRect.X, 0));

    DestinationTop = Render_MaxInt32((int32_t)Y, Render_MaxInt32((int32_t)Render_ClipRect.Y, 0));

    DestinationRight = Render_MinInt32((int32_t)X + (int32_t)SourceRegion->Width, Render_MinInt32((int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width, (int32_t)RENDER_WIDTH));

    DestinationBottom = Render_MinInt32((int32_t)Y + (int32_t)SourceRegion->Height, Render_MinInt32((int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height, (int32_t)RENDER_HEIGHT));

    if((DestinationRight <= DestinationLeft) || (DestinationBottom <= DestinationTop))
    {
        return;
    }

    SourceStartX = (uint32_t)SourceRegion->X + (uint32_t)(DestinationLeft - (int32_t)X);

    SourceStartY = (uint32_t)SourceRegion->Y + (uint32_t)(DestinationTop - (int32_t)Y);

    CopyWidth = (uint32_t)(DestinationRight - DestinationLeft);

    CopyHeight = (uint32_t)(DestinationBottom - DestinationTop);

    for(Row = 0U; Row < CopyHeight; Row++)
    {
        Source = &Image->Pixels[((SourceStartY + Row) * Image->StridePixels) + SourceStartX];

        Destination = &Target->Pixels[((uint32_t)DestinationLeft * Target->StridePixels) + RENDER_PHYSICAL_X((uint32_t)DestinationTop + Row)];

        if(!Image->HasTransparentColour)
        {
            for(Column = 0U; Column < CopyWidth; Column++)
            {
                *Destination = Source[Column];
                Destination += Target->StridePixels;
            }
        }
        else
        {
            for(Column = 0U; Column < CopyWidth; Column++)
            {
                Colour = Source[Column];

                if(Colour != Image->TransparentColour)
                {
                    *Destination = Colour;
                }

                Destination += Target->StridePixels;
            }
        }
    }
}
void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle)
{
    int32_t Sine;
    int32_t Cosine;
    int32_t HalfWidthQ15;
    int32_t HalfHeightQ15;
    int32_t Radius;
    int32_t DestinationLeft;
    int32_t DestinationTop;
    int32_t DestinationRight;
    int32_t DestinationBottom;
    int32_t DestinationX;
    int32_t DestinationY;
    int32_t RelativeXQ15;
    int32_t RelativeYQ15;
    int32_t SourceXQ15;
    int32_t SourceYQ15;
    int32_t SourceX;
    int32_t SourceY;
    uint32_t SourceIndex;
    Render_ColourIndexTypeDef *Destination;
    Render_ColourIndexTypeDef Colour;

    if(!Render_IsTargetValid(Target) || !Render_IsImageValid(Image))
    {
        return;
    }

    Sine = Render_SineQ15(Angle);
    Cosine = Render_CosineQ15(Angle);

    HalfWidthQ15 = (int32_t)Image->Width << (RENDER_TRIG_SHIFT - 1);

    HalfHeightQ15 = (int32_t)Image->Height << (RENDER_TRIG_SHIFT - 1);

    Radius = ((int32_t)Image->Width + (int32_t)Image->Height + 1) / 2;

    DestinationLeft = Render_MaxInt32((int32_t)CentreX - Radius, Render_MaxInt32((int32_t)Render_ClipRect.X, 0));

    DestinationTop = Render_MaxInt32((int32_t)CentreY - Radius, Render_MaxInt32((int32_t)Render_ClipRect.Y, 0));

    DestinationRight = Render_MinInt32((int32_t)CentreX + Radius, Render_MinInt32((int32_t)Render_ClipRect.X + (int32_t)Render_ClipRect.Width - 1, (int32_t)RENDER_WIDTH - 1));

    DestinationBottom = Render_MinInt32((int32_t)CentreY + Radius, Render_MinInt32((int32_t)Render_ClipRect.Y + (int32_t)Render_ClipRect.Height - 1, (int32_t)RENDER_HEIGHT - 1));

    if((DestinationRight < DestinationLeft) || (DestinationBottom < DestinationTop))
    {
        return;
    }

    RelativeXQ15 = ((DestinationLeft - (int32_t)CentreX) << RENDER_TRIG_SHIFT) + (RENDER_TRIG_ONE / 2);

    for(DestinationY = DestinationTop; DestinationY <= DestinationBottom; DestinationY++)
    {
        RelativeYQ15 = ((DestinationY - (int32_t)CentreY) << RENDER_TRIG_SHIFT) + (RENDER_TRIG_ONE / 2);

        /*
         * Perform the expensive transform once at the beginning of each row.
         * Moving one logical pixel right then advances source X by cosine and
         * source Y by negative sine.
         */
        SourceXQ15 = (int32_t)(((((int64_t)Cosine * RelativeXQ15) + ((int64_t)Sine * RelativeYQ15)) >> RENDER_TRIG_SHIFT) + HalfWidthQ15);

        SourceYQ15 = (int32_t)((((-(int64_t)Sine * RelativeXQ15) + ((int64_t)Cosine * RelativeYQ15)) >> RENDER_TRIG_SHIFT) + HalfHeightQ15);

        Destination = &Target->Pixels[((uint32_t)DestinationLeft * Target->StridePixels) + RENDER_PHYSICAL_X(DestinationY)];

        for(DestinationX = DestinationLeft; DestinationX <= DestinationRight; DestinationX++)
        {
            SourceX = SourceXQ15 >> RENDER_TRIG_SHIFT;

            SourceY = SourceYQ15 >> RENDER_TRIG_SHIFT;

            if((SourceX >= 0) && (SourceY >= 0) && (SourceX < (int32_t)Image->Width) && (SourceY < (int32_t)Image->Height))
            {
                SourceIndex = ((uint32_t)SourceY * Image->StridePixels) + (uint32_t)SourceX;

                Colour = Image->Pixels[SourceIndex];

                if(!Image->HasTransparentColour || (Colour != Image->TransparentColour))
                {
                    *Destination = Colour;
                }
            }

            SourceXQ15 += Cosine;
            SourceYQ15 -= Sine;
            Destination += Target->StridePixels;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Text drawing                                                               */
/* -------------------------------------------------------------------------- */

void Render_DrawText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    const FontGlyph *Glyph;
    const uint8_t *GlyphBitmap;
    uint32_t Codepoint;
    uint32_t BitIndex;
    uint32_t ByteIndex;
    uint16_t Row;
    uint16_t Column;
    uint8_t BitMask;
    int32_t CursorX;
    int32_t BaselineY;
    int32_t DestinationX;
    int32_t DestinationY;

    if(!Render_IsTargetValid(Target) || (FontAsset == NULL) || (FontAsset->bitmap == NULL) || (FontAsset->glyphs == NULL) || (FontAsset->glyphCount == 0U) || (Text == NULL))
    {
        return;
    }

    CursorX = (int32_t)X;
    BaselineY = (int32_t)Y + (int32_t)FontAsset->ascent;

    while(*Text != '\0')
    {
        Codepoint = (uint8_t)*Text;
        Text++;

        if(Codepoint == (uint32_t)'\n')
        {
            CursorX = (int32_t)X;
            BaselineY += (int32_t)FontAsset->lineHeight;
            continue;
        }

        if(Codepoint == (uint32_t)'\r')
        {
            continue;
        }

        Glyph = Font_GetGlyph(FontAsset, Codepoint);

        if(Glyph == NULL)
        {
            Codepoint = (uint32_t)'?';

            Glyph = Font_GetGlyph(FontAsset, Codepoint);

            if(Glyph == NULL)
            {
                continue;
            }
        }

        GlyphBitmap = &FontAsset->bitmap[Glyph->bitmapOffset];

        for(Row = 0U; Row < Glyph->height; Row++)
        {
            for(Column = 0U; Column < Glyph->width; Column++)
            {
                BitIndex = ((uint32_t)Row * (uint32_t)Glyph->width) + (uint32_t)Column;

                ByteIndex = BitIndex >> 3U;
                BitMask = (uint8_t)(0x80U >> (BitIndex & 7U));

                if((GlyphBitmap[ByteIndex] & BitMask) == 0U)
                {
                    continue;
                }

                DestinationX = CursorX + (int32_t)Glyph->offsetX + (int32_t)Column;

                DestinationY = BaselineY + (int32_t)Glyph->offsetY + (int32_t)Row;

                if(Render_IsLogicalPixelVisible(DestinationX, DestinationY))
                {
                    Target->Pixels[((uint32_t)DestinationX * Target->StridePixels) + RENDER_PHYSICAL_X(DestinationY)] = Colour;
                }
            }
        }

        CursorX += (int32_t)Glyph->advance;
    }
}
