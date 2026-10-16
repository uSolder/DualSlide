/**
 * @file controls.c
 * @brief The two buttons and two sliders, read once per frame for every app.
 *
 * Each frame the raw inputs are read once and turned into the states apps
 * ask about: pressed and released this frame, held time, and slider
 * positions. A press is ignored until let go when it was already down as an
 * app started, or when it joins the other button in the system gesture.
 *
 * The buttons are the board's GPIO inputs and the sliders its potentiometer
 * ADC inputs. Slider readings are smoothed: slowly when they barely change,
 * which hides noise, and quickly when they move a lot, so the slider keeps up.
 */

#include "controls.h"

#include "adc.h"
#include "board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* How far a slider must move from where it rested to count as moved. */
#define CONTROLS_SLIDER_MOVE_THRESHOLD      (2000)

/* Slider smoothing, in fixed point with this many fractional bits. */
#define CONTROLS_FILTER_FRACTIONAL_BITS     (8U)
#define CONTROLS_FILTER_SLOW_SHIFT          (2U)
#define CONTROLS_FILTER_FAST_SHIFT          (1U)
#define CONTROLS_FILTER_FAST_THRESHOLD      (768U)

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool Down;
    bool Pressed;
    bool Released;
    bool Ignored;
    uint32_t HeldMilliseconds;
} Controls_ButtonStateTypeDef;

typedef struct
{
    int32_t Value;
    int32_t RestValue;
    bool RestValid;
    bool Moved;
    int32_t Filtered;                       /* Smoothed reading, fixed point */
    bool FilterValid;
} Controls_SliderStateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Controls_ButtonStateTypeDef Controls_Buttons[CONTROLS_BUTTON_COUNT];
static Controls_SliderStateTypeDef Controls_Sliders[CONTROLS_SLIDER_COUNT];

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool Controls_IsButtonValid(Controls_ButtonTypeDef Button)
{
    return (uint32_t)Button < (uint32_t)CONTROLS_BUTTON_COUNT;
}

static bool Controls_IsSliderValid(Controls_SliderTypeDef Slider)
{
    return (uint32_t)Slider < (uint32_t)CONTROLS_SLIDER_COUNT;
}

static bool Controls_ReadButton(Controls_ButtonTypeDef Button)
{
    return (Button == CONTROLS_PRIMARY) ? Board_IsPrimaryButtonDown() : Board_IsSecondaryButtonDown();
}

/* Smooths a raw reading into State, returning the smoothed value. */
static int32_t Controls_FilterSlider(Controls_SliderStateTypeDef *State, ADC_ValueTypeDef RawValue)
{
    const int32_t Target = (int32_t)RawValue << CONTROLS_FILTER_FRACTIONAL_BITS;
    int32_t Difference;
    uint8_t Shift;

    if(!State->FilterValid)
    {
        State->Filtered = Target;
        State->FilterValid = true;
    }
    else
    {
        Difference = Target - State->Filtered;
        Shift = (((Difference >= 0) ? Difference : -Difference) > ((int32_t)CONTROLS_FILTER_FAST_THRESHOLD << CONTROLS_FILTER_FRACTIONAL_BITS)) ?
                CONTROLS_FILTER_FAST_SHIFT : CONTROLS_FILTER_SLOW_SHIFT;
        State->Filtered += Difference / (int32_t)(1UL << Shift);
    }

    return (State->Filtered + (1L << (CONTROLS_FILTER_FRACTIONAL_BITS - 1U))) >> CONTROLS_FILTER_FRACTIONAL_BITS;
}

static void Controls_UpdateSlider(Controls_SliderTypeDef Slider)
{
    Controls_SliderStateTypeDef *State = &Controls_Sliders[Slider];
    const ADC_InputTypeDef *Input = (Slider == CONTROLS_LEFT_SLIDER) ? Board_GetPOTAInput() : Board_GetPOTBInput();
    ADC_ValueTypeDef Raw;
    int32_t Value;

    if((Input == NULL) || (ADC_Read(Input, &Raw) != ADC_RESULT_OK))
    {
        return;
    }

    Value = Controls_FilterSlider(State, Raw);

    State->Value = (Value < 0) ? 0 : ((Value > CONTROLS_SLIDER_RAW_MAXIMUM) ? CONTROLS_SLIDER_RAW_MAXIMUM : Value);

    if(!State->RestValid)
    {
        State->RestValue = State->Value;
        State->RestValid = true;
    }

    if(((State->Value - State->RestValue) >= CONTROLS_SLIDER_MOVE_THRESHOLD) || ((State->RestValue - State->Value) >= CONTROLS_SLIDER_MOVE_THRESHOLD))
    {
        State->Moved = true;
    }
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Controls_Init(void)
{
    for(uint32_t Button = 0U; Button < (uint32_t)CONTROLS_BUTTON_COUNT; Button++)
    {
        Controls_Buttons[Button] = (Controls_ButtonStateTypeDef){ 0 };
    }

    for(uint32_t Slider = 0U; Slider < (uint32_t)CONTROLS_SLIDER_COUNT; Slider++)
    {
        Controls_Sliders[Slider] = (Controls_SliderStateTypeDef){ 0 };
    }

    Controls_Update(0U);

    /* The press that switched the device on is not a game's. */
    Controls_IgnoreHeldButtons();
}

void Controls_Update(uint32_t DeltaTimeMilliseconds)
{
    bool AllDown = true;

    for(uint32_t Index = 0U; Index < (uint32_t)CONTROLS_BUTTON_COUNT; Index++)
    {
        Controls_ButtonStateTypeDef *State = &Controls_Buttons[Index];
        const bool Down = Controls_ReadButton((Controls_ButtonTypeDef)Index);

        State->Pressed = Down && !State->Down;
        State->Released = !Down && State->Down && !State->Ignored;

        if(State->Pressed)
        {
            State->Ignored = false;
            State->HeldMilliseconds = 0U;
        }
        else if(Down && (State->HeldMilliseconds < (UINT32_MAX - DeltaTimeMilliseconds)))
        {
            State->HeldMilliseconds += DeltaTimeMilliseconds;
        }

        if(!Down)
        {
            State->Ignored = false;
        }

        State->Down = Down;
        AllDown = AllDown && Down;
    }

    /* Both buttons together are the system gesture, not the game's. */
    if(AllDown)
    {
        Controls_IgnoreHeldButtons();
    }

    for(uint32_t Index = 0U; Index < (uint32_t)CONTROLS_SLIDER_COUNT; Index++)
    {
        Controls_UpdateSlider((Controls_SliderTypeDef)Index);
    }
}

void Controls_IgnoreHeldButtons(void)
{
    for(uint32_t Index = 0U; Index < (uint32_t)CONTROLS_BUTTON_COUNT; Index++)
    {
        if(Controls_Buttons[Index].Down)
        {
            Controls_Buttons[Index].Ignored = true;
            Controls_Buttons[Index].Pressed = false;
        }
    }
}

bool Controls_IsDown(Controls_ButtonTypeDef Button)
{
    return Controls_IsButtonValid(Button) && Controls_Buttons[Button].Down && !Controls_Buttons[Button].Ignored;
}

bool Controls_WasPressed(Controls_ButtonTypeDef Button)
{
    return Controls_IsButtonValid(Button) && Controls_Buttons[Button].Pressed && !Controls_Buttons[Button].Ignored;
}

bool Controls_WasReleased(Controls_ButtonTypeDef Button)
{
    return Controls_IsButtonValid(Button) && Controls_Buttons[Button].Released;
}

uint32_t Controls_HeldMilliseconds(Controls_ButtonTypeDef Button)
{
    return (Controls_IsDown(Button) || Controls_WasReleased(Button)) ? Controls_Buttons[Button].HeldMilliseconds : 0U;
}

float Controls_Slider(Controls_SliderTypeDef Slider)
{
    return (float)Controls_SliderRaw(Slider) / (float)CONTROLS_SLIDER_RAW_MAXIMUM;
}

int32_t Controls_SliderBetween(Controls_SliderTypeDef Slider, int32_t AtBottom, int32_t AtTop)
{
    const int64_t Span = (int64_t)AtTop - (int64_t)AtBottom;
    const int64_t Scaled = (Span * (int64_t)Controls_SliderRaw(Slider)) + ((Span >= 0) ? (CONTROLS_SLIDER_RAW_MAXIMUM / 2) : -(CONTROLS_SLIDER_RAW_MAXIMUM / 2));

    return (int32_t)((int64_t)AtBottom + (Scaled / CONTROLS_SLIDER_RAW_MAXIMUM));
}

int32_t Controls_SliderRaw(Controls_SliderTypeDef Slider)
{
    return Controls_IsSliderValid(Slider) ? Controls_Sliders[Slider].Value : 0;
}

bool Controls_SliderMoved(Controls_SliderTypeDef Slider)
{
    return Controls_IsSliderValid(Slider) && Controls_Sliders[Slider].Moved;
}

void Controls_ResetSliderMoved(Controls_SliderTypeDef Slider)
{
    if(Controls_IsSliderValid(Slider))
    {
        Controls_Sliders[Slider].RestValue = Controls_Sliders[Slider].Value;
        Controls_Sliders[Slider].RestValid = true;
        Controls_Sliders[Slider].Moved = false;
    }
}
