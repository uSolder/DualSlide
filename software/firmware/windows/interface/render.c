/**
 * @file render.c
 * @brief Windows implementation of the stateless 2D renderer.
 */

#include "render.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define WINDOWS_RENDER_TRIG_SCALE    (16384)

/* Longest line Render_DrawTextAligned() and Render_DrawTextf() draw. */
#define RENDER_TEXT_BUFFER_SIZE      (128U)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Render_RectTypeDef Windows_RenderClipRect = { 0, 0, RENDER_WIDTH, RENDER_HEIGHT };

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

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

void Render_DrawText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    int32_t CursorX = X;
    int32_t BaselineY;

    if(!Windows_RenderIsValidTarget(Target) ||
       (FontAsset == NULL) ||
       (FontAsset->bitmap == NULL) ||
       (FontAsset->glyphs == NULL) ||
       (FontAsset->glyphCount == 0U) ||
       (Text == NULL))
    {
        return;
    }

    BaselineY = (int32_t)Y + (int32_t)FontAsset->ascent;

    while(*Text != '\0')
    {
        uint32_t Codepoint = (uint8_t)*Text++;
        const FontGlyph *Glyph;
        const uint8_t *GlyphBitmap;

        if(Codepoint == (uint32_t)'\n')
        {
            CursorX = X;
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
            Glyph = Font_GetGlyph(FontAsset, (uint32_t)'?');

            if(Glyph == NULL)
            {
                continue;
            }
        }

        GlyphBitmap = &FontAsset->bitmap[Glyph->bitmapOffset];

        for(uint16_t Row = 0U; Row < Glyph->height; Row++)
        {
            const int32_t DestinationY = BaselineY + Glyph->offsetY + Row;

            if((DestinationY < Windows_RenderClipRect.Y) || (DestinationY >= ((int32_t)Windows_RenderClipRect.Y + Windows_RenderClipRect.Height)))
            {
                continue;
            }

            for(uint16_t Column = 0U; Column < Glyph->width; Column++)
            {
                const uint32_t BitIndex = ((uint32_t)Row * Glyph->width) + Column;
                const int32_t DestinationX = CursorX + Glyph->offsetX + Column;

                if((DestinationX >= Windows_RenderClipRect.X) &&
                   (DestinationX < ((int32_t)Windows_RenderClipRect.X + Windows_RenderClipRect.Width)) &&
                   ((GlyphBitmap[BitIndex >> 3U] & (uint8_t)(0x80U >> (BitIndex & 7U))) != 0U))
                {
                    Target->Pixels[(DestinationY * Target->StridePixels) + DestinationX] = Colour;
                }
            }
        }

        CursorX += Glyph->advance;
    }
}

/* -------------------------------------------------------------------------- */
/* Shapes and text helpers                                                    */
/* -------------------------------------------------------------------------- */

/*
 * These draw through Render_FillRect(), Render_DrawPolygon() and
 * Render_DrawText(), so they honour the clip rectangle. Round shapes are one
 * horizontal span per row.
 */

/* One horizontal span from Left to Right inclusive. */
static void Render_Span(Render_TargetTypeDef *Target, int32_t Left, int32_t Right, int32_t Y, Render_ColourIndexTypeDef Colour)
{
    Render_RectTypeDef Rect;

    if((Right < Left) || (Y < INT16_MIN) || (Y > INT16_MAX))
    {
        return;
    }

    Left = (Left < INT16_MIN) ? INT16_MIN : Left;
    Right = (Right > INT16_MAX) ? INT16_MAX : Right;
    Rect.X = (int16_t)Left;
    Rect.Y = (int16_t)Y;
    Rect.Width = (uint16_t)((Right - Left) + 1);
    Rect.Height = 1U;
    Render_FillRect(Target, &Rect, Colour);
}

/* Half the width of a circle of Radius at DeltaY rows from its centre. */
static int32_t Render_CircleHalfWidth(float Radius, int32_t DeltaY)
{
    const float Square = ((Radius + 0.5f) * (Radius + 0.5f)) - ((float)DeltaY * (float)DeltaY);

    return (Square > 0.0f) ? (int32_t)sqrtf(Square) : -1;
}

/* The glyph drawn for a character, falling back to '?' as Render_DrawText() does. */
static const FontGlyph *Render_GlyphFor(const Font *FontAsset, char Character)
{
    const FontGlyph *Glyph = Font_GetGlyph(FontAsset, (uint8_t)Character);

    return (Glyph != NULL) ? Glyph : Font_GetGlyph(FontAsset, (uint32_t)'?');
}

/* Width of the text up to the end of its first line. */
static uint32_t Render_LineWidth(const Font *FontAsset, const char *Text)
{
    uint32_t Width = 0U;

    while((*Text != '\0') && (*Text != '\n'))
    {
        if(*Text != '\r')
        {
            const FontGlyph *Glyph = Render_GlyphFor(FontAsset, *Text);

            Width += (Glyph != NULL) ? Glyph->advance : 0U;
        }

        Text++;
    }

    return Width;
}

static void Render_AppendCharacter(char *Buffer, uint32_t Size, uint32_t *Length, char Character)
{
    if((*Length + 1U) < Size)
    {
        Buffer[*Length] = Character;
        (*Length)++;
    }
}

static void Render_AppendNumber(char *Buffer, uint32_t Size, uint32_t *Length, uint32_t Value, uint32_t Base, bool Negative, uint32_t Width, char Pad)
{
    char Digits[12];
    uint32_t Count = 0U;

    do
    {
        const uint32_t Digit = Value % Base;

        Digits[Count++] = (char)((Digit < 10U) ? ('0' + Digit) : ('a' + (Digit - 10U)));
        Value /= Base;
    }
    while((Value != 0U) && (Count < sizeof(Digits)));

    Width = (Width > Count + (Negative ? 1U : 0U)) ? (Width - Count - (Negative ? 1U : 0U)) : 0U;

    /* Zero padding goes after the sign, space padding before it. */
    if(Negative && (Pad == '0'))
    {
        Render_AppendCharacter(Buffer, Size, Length, '-');
    }

    while(Width-- > 0U)
    {
        Render_AppendCharacter(Buffer, Size, Length, Pad);
    }

    if(Negative && (Pad != '0'))
    {
        Render_AppendCharacter(Buffer, Size, Length, '-');
    }

    while(Count > 0U)
    {
        Render_AppendCharacter(Buffer, Size, Length, Digits[--Count]);
    }
}

static char *Render_FormatList(char *Buffer, uint32_t Size, const char *Format, va_list Arguments)
{
    uint32_t Length = 0U;

    if((Buffer == NULL) || (Size == 0U))
    {
        return Buffer;
    }

    while((Format != NULL) && (*Format != '\0'))
    {
        char Pad = ' ';
        uint32_t Width = 0U;
        uint32_t RightWidth = 0U;
        uint32_t Start;
        bool Left = false;
        bool Long = false;

        if(*Format != '%')
        {
            Render_AppendCharacter(Buffer, Size, &Length, *Format++);
            continue;
        }

        Format++;

        if(*Format == '-')
        {
            Left = true;
            Format++;
        }

        if(*Format == '0')
        {
            Pad = Left ? ' ' : '0';
            Format++;
        }

        if(*Format == '*')
        {
            const int Given = va_arg(Arguments, int);

            Width = (Given > 0) ? (uint32_t)Given : 0U;
            Format++;
        }

        while((*Format >= '0') && (*Format <= '9'))
        {
            Width = (Width * 10U) + (uint32_t)(*Format++ - '0');
        }

        if(*Format == 'l')
        {
            Long = true;
            Format++;
        }

        /* Left-justified: write the value unpadded, then pad on its right. */
        if(Left)
        {
            RightWidth = Width;
            Width = 0U;
        }

        Start = Length;

        switch(*Format)
        {
            case 'd':
            case 'i':
            {
                const long Value = Long ? va_arg(Arguments, long) : (long)va_arg(Arguments, int);
                const uint32_t Magnitude = (Value < 0) ? (uint32_t)(-(Value + 1)) + 1U : (uint32_t)Value;

                Render_AppendNumber(Buffer, Size, &Length, Magnitude, 10U, Value < 0, Width, Pad);
                break;
            }

            case 'u':
            case 'x':
            {
                const uint32_t Value = Long ? (uint32_t)va_arg(Arguments, unsigned long) : va_arg(Arguments, unsigned int);

                Render_AppendNumber(Buffer, Size, &Length, Value, (*Format == 'x') ? 16U : 10U, false, Width, Pad);
                break;
            }

            case 'c':
                Render_AppendCharacter(Buffer, Size, &Length, (char)va_arg(Arguments, int));
                break;

            case 's':
            {
                const char *Text = va_arg(Arguments, const char *);
                uint32_t TextLength = 0U;

                Text = (Text != NULL) ? Text : "";

                while(Text[TextLength] != '\0')
                {
                    TextLength++;
                }

                while(Width > TextLength)
                {
                    Render_AppendCharacter(Buffer, Size, &Length, ' ');
                    Width--;
                }

                while(*Text != '\0')
                {
                    Render_AppendCharacter(Buffer, Size, &Length, *Text++);
                }
                break;
            }

            case '%':
                Render_AppendCharacter(Buffer, Size, &Length, '%');
                break;

            case '\0':
                Format--;
                break;

            default:
                Render_AppendCharacter(Buffer, Size, &Length, '%');
                Render_AppendCharacter(Buffer, Size, &Length, *Format);
                break;
        }

        while(((Length - Start) < RightWidth) && ((Length + 1U) < Size))
        {
            Render_AppendCharacter(Buffer, Size, &Length, ' ');
        }

        Format++;
    }

    Buffer[Length] = '\0';

    return Buffer;
}

void Render_Box(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    const Render_RectTypeDef Rect = { X, Y, Width, Height };

    Render_FillRect(Target, &Rect, Colour);
}

void Render_DrawRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    if((Thickness == 0U) || (Width == 0U) || (Height == 0U))
    {
        return;
    }

    /* A border as thick as the box is half of it fills the box. */
    if(((uint32_t)Thickness * 2U >= Width) || ((uint32_t)Thickness * 2U >= Height))
    {
        Render_Box(Target, X, Y, Width, Height, Colour);
        return;
    }

    Render_Box(Target, X, Y, Width, Thickness, Colour);
    Render_Box(Target, X, (int16_t)(Y + (int16_t)Height - (int16_t)Thickness), Width, Thickness, Colour);
    Render_Box(Target, X, (int16_t)(Y + (int16_t)Thickness), Thickness, (uint16_t)(Height - (2U * Thickness)), Colour);
    Render_Box(Target, (int16_t)(X + (int16_t)Width - (int16_t)Thickness), (int16_t)(Y + (int16_t)Thickness), Thickness, (uint16_t)(Height - (2U * Thickness)), Colour);
}

void Render_FillRoundRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Radius, Render_ColourIndexTypeDef Colour)
{
    const uint16_t Smaller = (Width < Height) ? Width : Height;

    Radius = (Radius > (Smaller / 2U)) ? (uint16_t)(Smaller / 2U) : Radius;

    for(int32_t Row = 0; Row < (int32_t)Height; Row++)
    {
        int32_t Inset = 0;
        int32_t FromEdge = (Row < (int32_t)Radius) ? Row : (((int32_t)Height - 1 - Row) < (int32_t)Radius ? ((int32_t)Height - 1 - Row) : -1);

        if(FromEdge >= 0)
        {
            const float DeltaY = (float)Radius - (float)FromEdge - 0.5f;

            Inset = (int32_t)((float)Radius - sqrtf(((float)Radius * (float)Radius) - (DeltaY * DeltaY)) + 0.5f);
        }

        Render_Span(Target, (int32_t)X + Inset, (int32_t)X + (int32_t)Width - 1 - Inset, (int32_t)Y + Row, Colour);
    }
}

void Render_FillCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, Render_ColourIndexTypeDef Colour)
{
    for(int32_t DeltaY = -(int32_t)Radius; DeltaY <= (int32_t)Radius; DeltaY++)
    {
        const int32_t Half = Render_CircleHalfWidth((float)Radius, DeltaY);

        Render_Span(Target, (int32_t)CentreX - Half, (int32_t)CentreX + Half, (int32_t)CentreY + DeltaY, Colour);
    }
}

void Render_DrawCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    const int32_t Inner = (int32_t)Radius - (int32_t)Thickness;

    if(Thickness == 0U)
    {
        return;
    }

    if(Inner < 0)
    {
        Render_FillCircle(Target, CentreX, CentreY, Radius, Colour);
        return;
    }

    for(int32_t DeltaY = -(int32_t)Radius; DeltaY <= (int32_t)Radius; DeltaY++)
    {
        const int32_t Outer = Render_CircleHalfWidth((float)Radius, DeltaY);
        const int32_t Hole = Render_CircleHalfWidth((float)Inner, DeltaY);

        if(Hole < 0)
        {
            Render_Span(Target, (int32_t)CentreX - Outer, (int32_t)CentreX + Outer, (int32_t)CentreY + DeltaY, Colour);
        }
        else
        {
            Render_Span(Target, (int32_t)CentreX - Outer, (int32_t)CentreX - Hole - 1, (int32_t)CentreY + DeltaY, Colour);
            Render_Span(Target, (int32_t)CentreX + Hole + 1, (int32_t)CentreX + Outer, (int32_t)CentreY + DeltaY, Colour);
        }
    }
}

void Render_DrawLine(Render_TargetTypeDef *Target, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    const int32_t DeltaX = (int32_t)X2 - (int32_t)X1;
    const int32_t DeltaY = (int32_t)Y2 - (int32_t)Y1;

    /* Thin lines: one pixel per step (Bresenham). */
    if(Thickness <= 1U)
    {
        const int32_t StepX = (DeltaX < 0) ? -1 : 1;
        const int32_t StepY = (DeltaY < 0) ? -1 : 1;
        const int32_t Width = (DeltaX < 0) ? -DeltaX : DeltaX;
        const int32_t Height = (DeltaY < 0) ? DeltaY : -DeltaY;
        int32_t Error = Width + Height;
        int32_t X = X1;
        int32_t Y = Y1;

        for(;;)
        {
            Render_Span(Target, X, X, Y, Colour);

            if((X == X2) && (Y == Y2))
            {
                break;
            }

            {
                const int32_t Doubled = 2 * Error;

                if(Doubled >= Height)
                {
                    Error += Height;
                    X += StepX;
                }

                if(Doubled <= Width)
                {
                    Error += Width;
                    Y += StepY;
                }
            }
        }

        return;
    }

    /* Thick lines: a rectangle along the line. */
    {
        const float Length = sqrtf((float)((DeltaX * DeltaX) + (DeltaY * DeltaY)));
        const float Half = (float)Thickness * 0.5f;
        Render_PointTypeDef Points[4];
        int16_t OffsetX;
        int16_t OffsetY;

        if(Length < 0.5f)
        {
            Render_FillCircle(Target, X1, Y1, (uint16_t)(Thickness / 2U), Colour);
            return;
        }

        OffsetX = (int16_t)lroundf((-(float)DeltaY * Half) / Length);
        OffsetY = (int16_t)lroundf(((float)DeltaX * Half) / Length);
        Points[0] = (Render_PointTypeDef){ (int16_t)(X1 + OffsetX), (int16_t)(Y1 + OffsetY) };
        Points[1] = (Render_PointTypeDef){ (int16_t)(X2 + OffsetX), (int16_t)(Y2 + OffsetY) };
        Points[2] = (Render_PointTypeDef){ (int16_t)(X2 - OffsetX), (int16_t)(Y2 - OffsetY) };
        Points[3] = (Render_PointTypeDef){ (int16_t)(X1 - OffsetX), (int16_t)(Y1 - OffsetY) };
        (void)Render_DrawPolygon(Target, Points, 4U, Colour);
    }
}

uint16_t Render_TextWidth(const Font *FontAsset, const char *Text)
{
    uint32_t Widest = 0U;

    if((FontAsset == NULL) || (FontAsset->glyphs == NULL) || (Text == NULL))
    {
        return 0U;
    }

    for(;;)
    {
        const uint32_t Width = Render_LineWidth(FontAsset, Text);

        Widest = (Width > Widest) ? Width : Widest;

        while((*Text != '\0') && (*Text != '\n'))
        {
            Text++;
        }

        if(*Text == '\0')
        {
            break;
        }

        Text++;
    }

    return (Widest > UINT16_MAX) ? UINT16_MAX : (uint16_t)Widest;
}

void Render_DrawTextAligned(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_AlignTypeDef Align, Render_ColourIndexTypeDef Colour)
{
    char Line[RENDER_TEXT_BUFFER_SIZE];
    int32_t LineY = Y;

    if((FontAsset == NULL) || (FontAsset->glyphs == NULL) || (Text == NULL))
    {
        return;
    }

    /* Each line is aligned on its own. */
    for(;;)
    {
        const int32_t Width = (int32_t)Render_LineWidth(FontAsset, Text);
        int32_t LineX = X;
        uint32_t Length = 0U;

        while((*Text != '\0') && (*Text != '\n'))
        {
            if(Length < (RENDER_TEXT_BUFFER_SIZE - 1U))
            {
                Line[Length++] = *Text;
            }

            Text++;
        }

        Line[Length] = '\0';

        if(Align == RENDER_ALIGN_CENTRE)
        {
            LineX -= Width / 2;
        }
        else if(Align == RENDER_ALIGN_RIGHT)
        {
            LineX -= Width;
        }

        Render_DrawText(Target, FontAsset, Line, (int16_t)LineX, (int16_t)LineY, Colour);

        if(*Text == '\0')
        {
            break;
        }

        Text++;
        LineY += FontAsset->lineHeight;
    }
}

char *Render_FormatText(char *Buffer, uint32_t Size, const char *Format, ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    (void)Render_FormatList(Buffer, Size, Format, Arguments);
    va_end(Arguments);

    return Buffer;
}

void Render_DrawTextf(Render_TargetTypeDef *Target, const Font *FontAsset, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour, const char *Format, ...)
{
    char Text[RENDER_TEXT_BUFFER_SIZE];
    va_list Arguments;

    va_start(Arguments, Format);
    (void)Render_FormatList(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    Render_DrawText(Target, FontAsset, Text, X, Y, Colour);
}
