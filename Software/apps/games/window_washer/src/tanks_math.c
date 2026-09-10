/**
 * @file tanks_math.c
 * @brief Deterministic fixed-point mathematics for TANKS.
 */

#include "tanks_internal.h"

#include <stddef.h>

static const int16_t Tanks_QuarterSine[91] =
{
    0,18,36,54,71,89,107,125,143,160,178,195,212,230,247,264,281,
    297,314,330,347,363,379,395,410,426,441,456,471,486,500,515,
    529,543,557,571,584,597,610,623,636,648,660,672,684,695,706,
    717,728,738,748,758,768,777,786,795,804,812,820,828,836,843,
    850,857,863,870,876,882,887,892,897,902,906,910,914,917,921,
    924,926,929,931,933,935,936,937,938,939,940,941,941,941
};

int32_t Tanks_Clamp32(int32_t Value, int32_t Minimum, int32_t Maximum)
{
    if(Value < Minimum) return Minimum;
    if(Value > Maximum) return Maximum;
    return Value;
}

int16_t Tanks_NormalizeAngle(int32_t Angle)
{
    while(Angle >= TANKS_ANGLE_HALF) Angle -= TANKS_ANGLE_FULL;
    while(Angle < -TANKS_ANGLE_HALF) Angle += TANKS_ANGLE_FULL;
    return (int16_t)Angle;
}

int16_t Tanks_Sine(int16_t Angle)
{
    int32_t A = Angle;
    int32_t Sign = 1;
    int32_t Index;
    int32_t Fraction;
    int32_t Low;
    int32_t High;
    A %= TANKS_ANGLE_FULL;
    if(A < 0) A += TANKS_ANGLE_FULL;
    if(A >= TANKS_ANGLE_HALF) { A -= TANKS_ANGLE_HALF; Sign = -1; }
    if(A > TANKS_ANGLE_QUARTER) A = TANKS_ANGLE_HALF - A;
    Index = A / 10;
    Fraction = A % 10;
    Low = Tanks_QuarterSine[Index];
    High = Index < 90 ? Tanks_QuarterSine[Index + 1] : Low;
    return (int16_t)(Sign * (Low + (((High - Low) * Fraction) / 10)));
}

int16_t Tanks_Cosine(int16_t Angle)
{
    return Tanks_Sine((int16_t)(Angle + TANKS_ANGLE_QUARTER));
}

uint32_t Tanks_IntegerSquareRoot(uint64_t Value)
{
    uint64_t Result = 0U;
    uint64_t Bit = (uint64_t)1U << 62U;
    while(Bit > Value) Bit >>= 2U;
    while(Bit != 0U)
    {
        if(Value >= Result + Bit)
        {
            Value -= Result + Bit;
            Result = (Result >> 1U) + Bit;
        }
        else Result >>= 1U;
        Bit >>= 2U;
    }
    return (uint32_t)Result;
}

uint32_t Tanks_DistancePixels(Tanks_VectorTypeDef A, Tanks_VectorTypeDef B)
{
    const int64_t DeltaX = (A.X - B.X) >> TANKS_FP_SHIFT;
    const int64_t DeltaY = (A.Y - B.Y) >> TANKS_FP_SHIFT;
    return Tanks_IntegerSquareRoot((uint64_t)(DeltaX * DeltaX) + (uint64_t)(DeltaY * DeltaY));
}

int16_t Tanks_AngleTo(Tanks_VectorTypeDef From, Tanks_VectorTypeDef To)
{
    const int32_t DeltaX = (To.X - From.X) >> TANKS_FP_SHIFT;
    const int32_t DeltaY = (To.Y - From.Y) >> TANKS_FP_SHIFT;
    const int32_t AbsoluteX = DeltaX < 0 ? -DeltaX : DeltaX;
    const int32_t AbsoluteY = DeltaY < 0 ? -DeltaY : DeltaY;
    int32_t Angle;
    if((AbsoluteX == 0) && (AbsoluteY == 0)) return 0;
    if(AbsoluteY >= AbsoluteX) Angle = (AbsoluteY == 0) ? 0 : ((AbsoluteX * 450) / AbsoluteY);
    else Angle = 900 - ((AbsoluteY * 450) / AbsoluteX);
    if((DeltaX >= 0) && (DeltaY < 0)) return (int16_t)Angle;
    if((DeltaX >= 0) && (DeltaY >= 0)) return (int16_t)(1800 - Angle);
    if((DeltaX < 0) && (DeltaY >= 0)) return (int16_t)(-1800 + Angle);
    return (int16_t)-Angle;
}

int16_t Tanks_TurnToward(int16_t Current, int16_t Target, int16_t MaximumStep)
{
    const int16_t Error = Tanks_NormalizeAngle((int32_t)Target - Current);
    if(Error > MaximumStep) return Tanks_NormalizeAngle((int32_t)Current + MaximumStep);
    if(Error < -MaximumStep) return Tanks_NormalizeAngle((int32_t)Current - MaximumStep);
    return Target;
}

uint32_t Tanks_Random(void)
{
    Tanks_Game.RandomState = (Tanks_Game.RandomState * 1664525U) + 1013904223U;
    return Tanks_Game.RandomState;
}

void Tanks_FormatUnsigned(char *Buffer, uint8_t Size, uint32_t Value)
{
    char Reverse[11];
    uint8_t Count = 0U;
    uint8_t Index = 0U;
    if((Buffer == NULL) || (Size == 0U)) return;
    do { Reverse[Count++] = (char)('0' + (Value % 10U)); Value /= 10U; }
    while((Value != 0U) && (Count < sizeof(Reverse)));
    while((Count > 0U) && (Index + 1U < Size)) Buffer[Index++] = Reverse[--Count];
    Buffer[Index] = '\0';
}

void Tanks_CopyText(char *Destination, uint8_t Capacity, const char *Source)
{
    uint8_t Index = 0U;
    if((Destination == NULL) || (Capacity == 0U)) return;
    if(Source != NULL)
    {
        while((Source[Index] != '\0') && (Index + 1U < Capacity))
        {
            Destination[Index] = Source[Index];
            Index++;
        }
    }
    Destination[Index] = '\0';
}
