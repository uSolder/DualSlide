/**
 * @file render.c
 * @brief Windows implementation of the stateless 2D renderer.
 */

#include "render.h"

#include <string.h>

#define WINDOWS_RENDER_TRIG_SCALE    (16384)

static Render_RectTypeDef Windows_RenderClipRect = { 0, 0, RENDER_WIDTH, RENDER_HEIGHT };

static bool Windows_RenderIsValidTarget(const Render_TargetTypeDef *Target)
{
    return (Target != NULL) &&
           (Target->Pixels != NULL) &&
           (Target->Width == RENDER_WIDTH) &&
           (Target->Height == RENDER_HEIGHT) &&
           (Target->StridePixels >= RENDER_WIDTH);
}

static bool Windows_RenderClipRectToCurrent(const Render_RectTypeDef *Source, Render_RectTypeDef *Clipped)
{
    const int32_t SourceLeft = Source->X;
    const int32_t SourceTop = Source->Y;
    const int32_t SourceRight = SourceLeft + Source->Width;
    const int32_t SourceBottom = SourceTop + Source->Height;
    const int32_t ClipLeft = Windows_RenderClipRect.X;
    const int32_t ClipTop = Windows_RenderClipRect.Y;
    const int32_t ClipRight = ClipLeft + Windows_RenderClipRect.Width;
    const int32_t ClipBottom = ClipTop + Windows_RenderClipRect.Height;
    const int32_t Left = (SourceLeft > ClipLeft) ? SourceLeft : ClipLeft;
    const int32_t Top = (SourceTop > ClipTop) ? SourceTop : ClipTop;
    const int32_t Right = (SourceRight < ClipRight) ? SourceRight : ClipRight;
    const int32_t Bottom = (SourceBottom < ClipBottom) ? SourceBottom : ClipBottom;

    if((Source->Width == 0U) || (Source->Height == 0U) || (Left >= Right) || (Top >= Bottom))
    {
        return false;
    }

    Clipped->X = (int16_t)Left;
    Clipped->Y = (int16_t)Top;
    Clipped->Width = (uint16_t)(Right - Left);
    Clipped->Height = (uint16_t)(Bottom - Top);

    return true;
}

static int32_t Windows_RenderRoundDivide(int32_t Value, int32_t Divisor)
{
    return (Value >= 0) ? ((Value + (Divisor / 2)) / Divisor) : -(((-Value) + (Divisor / 2)) / Divisor);
}

static void Windows_RenderGetRotation(Render_AngleTypeDef Angle, int32_t *Sine, int32_t *Cosine)
{
    static const int16_t ArcTangentTenths[] = { 450, 266, 140, 71, 36, 18, 9, 4, 2, 1, 1 };
    int32_t X = 9949;
    int32_t Y = 0;
    int32_t RemainingAngle = Angle;

    while(RemainingAngle > 1800)
    {
        RemainingAngle -= 3600;
    }

    while(RemainingAngle < -1800)
    {
        RemainingAngle += 3600;
    }

    if(RemainingAngle > 900)
    {
        RemainingAngle -= 1800;
        X = -X;
    }
    else if(RemainingAngle < -900)
    {
        RemainingAngle += 1800;
        X = -X;
    }

    for(uint8_t Iteration = 0U; Iteration < (uint8_t)(sizeof(ArcTangentTenths) / sizeof(ArcTangentTenths[0])); Iteration++)
    {
        const int32_t PreviousX = X;
        const int32_t PreviousY = Y;

        if(RemainingAngle >= 0)
        {
            X = PreviousX - (PreviousY >> Iteration);
            Y = PreviousY + (PreviousX >> Iteration);
            RemainingAngle -= ArcTangentTenths[Iteration];
        }
        else
        {
            X = PreviousX + (PreviousY >> Iteration);
            Y = PreviousY - (PreviousX >> Iteration);
            RemainingAngle += ArcTangentTenths[Iteration];
        }
    }

    *Sine = Y;
    *Cosine = X;
}

void Render_SetClipRect(const Render_RectTypeDef *Rect)
{
    const int32_t RenderWidth = (int32_t)RENDER_WIDTH;
    const int32_t RenderHeight = (int32_t)RENDER_HEIGHT;
    const int32_t Left = (Rect != NULL) && (Rect->X > 0) ? Rect->X : 0;
    const int32_t Top = (Rect != NULL) && (Rect->Y > 0) ? Rect->Y : 0;
    const int32_t Right = (Rect != NULL) && (((int32_t)Rect->X + Rect->Width) < RenderWidth) ? ((int32_t)Rect->X + Rect->Width) : RenderWidth;
    const int32_t Bottom = (Rect != NULL) && (((int32_t)Rect->Y + Rect->Height) < RenderHeight) ? ((int32_t)Rect->Y + Rect->Height) : RenderHeight;

    if((Rect == NULL) || (Rect->Width == 0U) || (Rect->Height == 0U) || (Left >= Right) || (Top >= Bottom))
    {
        Windows_RenderClipRect.X = 0;
        Windows_RenderClipRect.Y = 0;
        Windows_RenderClipRect.Width = 0U;
        Windows_RenderClipRect.Height = 0U;
        return;
    }

    Windows_RenderClipRect.X = (int16_t)Left;
    Windows_RenderClipRect.Y = (int16_t)Top;
    Windows_RenderClipRect.Width = (uint16_t)(Right - Left);
    Windows_RenderClipRect.Height = (uint16_t)(Bottom - Top);
}

void Render_ResetClipRect(void)
{
    Windows_RenderClipRect.X = 0;
    Windows_RenderClipRect.Y = 0;
    Windows_RenderClipRect.Width = RENDER_WIDTH;
    Windows_RenderClipRect.Height = RENDER_HEIGHT;
}

void Render_Clear(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour)
{
    if(!Windows_RenderIsValidTarget(Target))
    {
        return;
    }

    for(uint16_t Y = 0U; Y < Target->Height; Y++)
    {
        memset(&Target->Pixels[Y * Target->StridePixels], Colour, Target->Width);
    }
}

void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour)
{
    Render_RectTypeDef ClippedRect;

    if(!Windows_RenderIsValidTarget(Target) || (Rect == NULL) || !Windows_RenderClipRectToCurrent(Rect, &ClippedRect))
    {
        return;
    }

    for(uint16_t Row = 0U; Row < ClippedRect.Height; Row++)
    {
        Render_ColourIndexTypeDef *Destination = &Target->Pixels[((uint32_t)(ClippedRect.Y + Row) * Target->StridePixels) + ClippedRect.X];

        memset(Destination, Colour, ClippedRect.Width);
    }
}

bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour)
{
    int32_t MinimumY;
    int32_t MaximumY;

    if(!Windows_RenderIsValidTarget(Target) ||
       (Points == NULL) ||
       (PointCount < 3U) ||
       (PointCount > RENDER_POLYGON_MAX_VERTEX_COUNT))
    {
        return false;
    }

    MinimumY = Points[0].Y;
    MaximumY = Points[0].Y;

    for(uint8_t PointIndex = 1U; PointIndex < PointCount; PointIndex++)
    {
        if(Points[PointIndex].Y < MinimumY)
        {
            MinimumY = Points[PointIndex].Y;
        }

        if(Points[PointIndex].Y > MaximumY)
        {
            MaximumY = Points[PointIndex].Y;
        }
    }

    if(MinimumY < Windows_RenderClipRect.Y)
    {
        MinimumY = Windows_RenderClipRect.Y;
    }

    if(MaximumY >= ((int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height))
    {
        MaximumY = (int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height - 1;
    }

    for(int32_t ScanY = MinimumY; ScanY <= MaximumY; ScanY++)
    {
        int32_t Intersections[RENDER_POLYGON_MAX_VERTEX_COUNT];
        uint8_t IntersectionCount = 0U;

        for(uint8_t PointIndex = 0U; PointIndex < PointCount; PointIndex++)
        {
            const Render_PointTypeDef *Start = &Points[PointIndex];
            const Render_PointTypeDef *End = &Points[(PointIndex + 1U) % PointCount];

            if(((Start->Y <= ScanY) && (End->Y > ScanY)) || ((End->Y <= ScanY) && (Start->Y > ScanY)))
            {
                Intersections[IntersectionCount++] = Start->X + ((ScanY - Start->Y) * (End->X - Start->X)) / (End->Y - Start->Y);
            }
        }

        for(uint8_t InsertionIndex = 1U; InsertionIndex < IntersectionCount; InsertionIndex++)
        {
            const int32_t Value = Intersections[InsertionIndex];
            uint8_t SortIndex = InsertionIndex;

            while((SortIndex > 0U) && (Intersections[SortIndex - 1U] > Value))
            {
                Intersections[SortIndex] = Intersections[SortIndex - 1U];
                SortIndex--;
            }

            Intersections[SortIndex] = Value;
        }

        for(uint8_t PairIndex = 0U; (PairIndex + 1U) < IntersectionCount; PairIndex += 2U)
        {
            const int32_t Left = Intersections[PairIndex];
            const int32_t Right = Intersections[PairIndex + 1U];

            if(Left < Right)
            {
                const Render_RectTypeDef Span = { (int16_t)Left, (int16_t)ScanY, (uint16_t)(Right - Left), 1U };

                Render_FillRect(Target, &Span, Colour);
            }
        }
    }

    return true;
}

void Render_DrawImage(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y)
{
    Render_ImageRegionTypeDef SourceRegion;

    if(Image == NULL)
    {
        return;
    }

    SourceRegion.X = 0U;
    SourceRegion.Y = 0U;
    SourceRegion.Width = Image->Width;
    SourceRegion.Height = Image->Height;
    Render_DrawImageRegion(Target, Image, &SourceRegion, X, Y);
}

void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle)
{
    int32_t Sine;
    int32_t Cosine;
    int32_t MinimumX;
    int32_t MaximumX;
    int32_t MinimumY;
    int32_t MaximumY;
    int32_t SourceCentreX;
    int32_t SourceCentreY;

    if(!Windows_RenderIsValidTarget(Target) ||
       (Image == NULL) ||
       (Image->Pixels == NULL) ||
       (Image->Width == 0U) ||
       (Image->Height == 0U) ||
       (Image->StridePixels < Image->Width))
    {
        return;
    }

    SourceCentreX = (int32_t)(Image->Width - 1U) / 2;
    SourceCentreY = (int32_t)(Image->Height - 1U) / 2;
    Windows_RenderGetRotation(Angle, &Sine, &Cosine);

    for(uint8_t CornerIndex = 0U; CornerIndex < 4U; CornerIndex++)
    {
        const int32_t SourceX = ((CornerIndex & 1U) != 0U) ? ((int32_t)Image->Width - 1 - SourceCentreX) : -SourceCentreX;
        const int32_t SourceY = ((CornerIndex & 2U) != 0U) ? ((int32_t)Image->Height - 1 - SourceCentreY) : -SourceCentreY;
        const int32_t RotatedX = CentreX + Windows_RenderRoundDivide((SourceX * Cosine) - (SourceY * Sine), WINDOWS_RENDER_TRIG_SCALE);
        const int32_t RotatedY = CentreY + Windows_RenderRoundDivide((SourceX * Sine) + (SourceY * Cosine), WINDOWS_RENDER_TRIG_SCALE);

        if(CornerIndex == 0U)
        {
            MinimumX = RotatedX;
            MaximumX = RotatedX;
            MinimumY = RotatedY;
            MaximumY = RotatedY;
        }
        else
        {
            MinimumX = (RotatedX < MinimumX) ? RotatedX : MinimumX;
            MaximumX = (RotatedX > MaximumX) ? RotatedX : MaximumX;
            MinimumY = (RotatedY < MinimumY) ? RotatedY : MinimumY;
            MaximumY = (RotatedY > MaximumY) ? RotatedY : MaximumY;
        }
    }

    MinimumX = (MinimumX < Windows_RenderClipRect.X) ? Windows_RenderClipRect.X : MinimumX;
    MaximumX = (MaximumX >= ((int32_t)Windows_RenderClipRect.X + Windows_RenderClipRect.Width)) ? ((int32_t)Windows_RenderClipRect.X + Windows_RenderClipRect.Width - 1) : MaximumX;
    MinimumY = (MinimumY < Windows_RenderClipRect.Y) ? Windows_RenderClipRect.Y : MinimumY;
    MaximumY = (MaximumY >= ((int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height)) ? ((int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height - 1) : MaximumY;

    for(int32_t DestinationY = MinimumY; DestinationY <= MaximumY; DestinationY++)
    {
        for(int32_t DestinationX = MinimumX; DestinationX <= MaximumX; DestinationX++)
        {
            const int32_t OffsetX = DestinationX - CentreX;
            const int32_t OffsetY = DestinationY - CentreY;
            const int32_t SourceX = Windows_RenderRoundDivide((OffsetX * Cosine) + (OffsetY * Sine), WINDOWS_RENDER_TRIG_SCALE) + SourceCentreX;
            const int32_t SourceY = Windows_RenderRoundDivide((-OffsetX * Sine) + (OffsetY * Cosine), WINDOWS_RENDER_TRIG_SCALE) + SourceCentreY;

            if((SourceX >= 0) && (SourceX < Image->Width) && (SourceY >= 0) && (SourceY < Image->Height))
            {
                const Render_ColourIndexTypeDef Colour = Image->Pixels[(SourceY * Image->StridePixels) + SourceX];

                if(!Image->HasTransparentColour || (Colour != Image->TransparentColour))
                {
                    Target->Pixels[(DestinationY * Target->StridePixels) + DestinationX] = Colour;
                }
            }
        }
    }
}

void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y)
{
    Render_RectTypeDef DestinationRect;
    Render_RectTypeDef ClippedRect;
    uint32_t SourceX;
    uint32_t SourceY;

    if(!Windows_RenderIsValidTarget(Target) ||
       (Image == NULL) ||
       (Image->Pixels == NULL) ||
       (SourceRegion == NULL) ||
       (SourceRegion->Width == 0U) ||
       (SourceRegion->Height == 0U) ||
       ((uint32_t)SourceRegion->X + SourceRegion->Width > Image->Width) ||
       ((uint32_t)SourceRegion->Y + SourceRegion->Height > Image->Height) ||
       (Image->StridePixels < Image->Width))
    {
        return;
    }

    DestinationRect.X = X;
    DestinationRect.Y = Y;
    DestinationRect.Width = SourceRegion->Width;
    DestinationRect.Height = SourceRegion->Height;

    if(!Windows_RenderClipRectToCurrent(&DestinationRect, &ClippedRect))
    {
        return;
    }

    SourceX = (uint32_t)SourceRegion->X + (uint32_t)(ClippedRect.X - DestinationRect.X);
    SourceY = (uint32_t)SourceRegion->Y + (uint32_t)(ClippedRect.Y - DestinationRect.Y);

    for(uint16_t Row = 0U; Row < ClippedRect.Height; Row++)
    {
        const Render_ColourIndexTypeDef *Source = &Image->Pixels[(SourceY + Row) * Image->StridePixels + SourceX];
        Render_ColourIndexTypeDef *Destination = &Target->Pixels[((uint32_t)(ClippedRect.Y + Row) * Target->StridePixels) + ClippedRect.X];

        if(!Image->HasTransparentColour)
        {
            memcpy(Destination, Source, ClippedRect.Width);
        }
        else
        {
            for(uint16_t Column = 0U; Column < ClippedRect.Width; Column++)
            {
                if(Source[Column] != Image->TransparentColour)
                {
                    Destination[Column] = Source[Column];
                }
            }
        }
    }
}

void Render_DrawText(Render_TargetTypeDef *Target, const Render_FontTypeDef *Font, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    int32_t PenX = X;

    if(!Windows_RenderIsValidTarget(Target) ||
       (Font == NULL) ||
       (Font->GlyphBits == NULL) ||
       (Text == NULL) ||
       (Font->GlyphWidth == 0U) ||
       (Font->GlyphHeight == 0U) ||
       (Font->RowStrideBytes == 0U) ||
       ((uint16_t)Font->RowStrideBytes * 8U < Font->GlyphWidth) ||
       (Font->GlyphStrideBytes < ((uint16_t)Font->RowStrideBytes * Font->GlyphHeight)))
    {
        return;
    }

    while(*Text != '\0')
    {
        const uint8_t Character = (uint8_t)*Text++;

        if((Character >= Font->FirstCharacter) && ((uint16_t)(Character - Font->FirstCharacter) < Font->GlyphCount))
        {
            const uint8_t *Glyph = &Font->GlyphBits[(uint32_t)(Character - Font->FirstCharacter) * Font->GlyphStrideBytes];

            for(uint8_t GlyphY = 0U; GlyphY < Font->GlyphHeight; GlyphY++)
            {
                const int32_t DestinationY = (int32_t)Y + GlyphY;
                const uint8_t *GlyphRow = &Glyph[(uint16_t)GlyphY * Font->RowStrideBytes];

                if((DestinationY < Windows_RenderClipRect.Y) || (DestinationY >= ((int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height)))
                {
                    continue;
                }

                for(uint8_t GlyphX = 0U; GlyphX < Font->GlyphWidth; GlyphX++)
                {
                    const int32_t DestinationX = PenX + GlyphX;

                    if((DestinationX >= Windows_RenderClipRect.X) &&
                       (DestinationX < ((int32_t)Windows_RenderClipRect.X + Windows_RenderClipRect.Width)) &&
                       ((GlyphRow[GlyphX >> 3U] & (uint8_t)(0x80U >> (GlyphX & 7U))) != 0U))
                    {
                        Target->Pixels[(DestinationY * Target->StridePixels) + DestinationX] = Colour;
                    }
                }
            }
        }

        PenX += Font->GlyphWidth;
    }
}