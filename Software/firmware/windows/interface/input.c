/**
 * @file input.c
 * @brief Windows SDL3 implementation of the generic input-control contract.
 */

#include "input.h"

#include <SDL3/SDL.h>

#define WINDOWS_INPUT_LEFT_SLIDER_NUMBER      ((Input_NumberTypeDef)1U)
#define WINDOWS_INPUT_RIGHT_SLIDER_NUMBER     ((Input_NumberTypeDef)2U)
#define WINDOWS_INPUT_PRIMARY_BUTTON_NUMBER   ((Input_NumberTypeDef)3U)
#define WINDOWS_INPUT_SECONDARY_BUTTON_NUMBER ((Input_NumberTypeDef)4U)
#define WINDOWS_INPUT_BATTERY_NUMBER          ((Input_NumberTypeDef)5U)

#define WINDOWS_INPUT_SLIDER_MINIMUM          (0)
#define WINDOWS_INPUT_SLIDER_MAXIMUM          (65535)
#define WINDOWS_INPUT_SLIDER_CENTRE           ((WINDOWS_INPUT_SLIDER_MAXIMUM + 1) / 2)
#define WINDOWS_INPUT_SLIDER_PER_MOUSE_PIXEL  (128)

typedef struct
{
    SDL_MouseID LeftMouse;
    SDL_MouseID RightMouse;
    int32_t LeftSliderValue;
    int32_t RightSliderValue;
    bool Initialised;
    bool RelativeMouseModeEnabled;
} Windows_InputStateTypeDef;

static Windows_InputStateTypeDef Windows_InputState;

static const Input_InfoTypeDef Windows_InputInfo[] =
{
    {
        .Number = WINDOWS_INPUT_LEFT_SLIDER_NUMBER,
        .Minimum = WINDOWS_INPUT_SLIDER_MINIMUM,
        .Maximum = WINDOWS_INPUT_SLIDER_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = WINDOWS_INPUT_RIGHT_SLIDER_NUMBER,
        .Minimum = WINDOWS_INPUT_SLIDER_MINIMUM,
        .Maximum = WINDOWS_INPUT_SLIDER_MAXIMUM,
        .Type = INPUT_TYPE_ANALOG
    },
    {
        .Number = WINDOWS_INPUT_PRIMARY_BUTTON_NUMBER,
        .Minimum = 0,
        .Maximum = 1,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = WINDOWS_INPUT_SECONDARY_BUTTON_NUMBER,
        .Minimum = 0,
        .Maximum = 1,
        .Type = INPUT_TYPE_DIGITAL
    },
    {
        .Number = WINDOWS_INPUT_BATTERY_NUMBER,
        .Minimum = 0,
        .Maximum = 100,
        .Type = INPUT_TYPE_ANALOG
    }
};

static int32_t Windows_InputClamp(int32_t Value, int32_t Minimum, int32_t Maximum)
{
    if(Value < Minimum)
    {
        return Minimum;
    }

    if(Value > Maximum)
    {
        return Maximum;
    }

    return Value;
}

static void Windows_InputProcessMouseMotion(const SDL_MouseMotionEvent *Motion)
{
    Windows_InputStateTypeDef *State = &Windows_InputState;
    const int32_t Delta = (int32_t)(-Motion->yrel * WINDOWS_INPUT_SLIDER_PER_MOUSE_PIXEL);

    if(Motion->which == 0U)
    {
        return;
    }

    if((State->LeftMouse == 0U) || (State->LeftMouse == Motion->which))
    {
        State->LeftMouse = Motion->which;
        State->LeftSliderValue = Windows_InputClamp(
            State->LeftSliderValue + Delta,
            WINDOWS_INPUT_SLIDER_MINIMUM,
            WINDOWS_INPUT_SLIDER_MAXIMUM);
    }
    else if((State->RightMouse == 0U) || (State->RightMouse == Motion->which))
    {
        State->RightMouse = Motion->which;
        State->RightSliderValue = Windows_InputClamp(
            State->RightSliderValue + Delta,
            WINDOWS_INPUT_SLIDER_MINIMUM,
            WINDOWS_INPUT_SLIDER_MAXIMUM);
    }
}

static void Windows_InputProcessMouseRemoval(SDL_MouseID Mouse)
{
    Windows_InputStateTypeDef *State = &Windows_InputState;

    if(Mouse == State->LeftMouse)
    {
        State->LeftMouse = 0U;
        State->LeftSliderValue = WINDOWS_INPUT_SLIDER_CENTRE;
    }

    if(Mouse == State->RightMouse)
    {
        State->RightMouse = 0U;
        State->RightSliderValue = WINDOWS_INPUT_SLIDER_CENTRE;
    }
}

static void Windows_InputUpdate(void)
{
    SDL_Event Event;

    if(!Windows_InputState.RelativeMouseModeEnabled)
    {
        SDL_Window *Window = SDL_GetMouseFocus();

        if(Window != NULL)
        {
            Windows_InputState.RelativeMouseModeEnabled =
                SDL_SetWindowRelativeMouseMode(Window, true);
        }
    }

    while(SDL_PollEvent(&Event))
    {
        if(Event.type == SDL_EVENT_MOUSE_MOTION)
        {
            Windows_InputProcessMouseMotion(&Event.motion);
        }
        else if(Event.type == SDL_EVENT_MOUSE_REMOVED)
        {
            Windows_InputProcessMouseRemoval(Event.mdevice.which);
        }
    }
}

bool Input_Init(void)
{
    Windows_InputStateTypeDef *State = &Windows_InputState;

    if(State->Initialised)
    {
        return true;
    }

    if(!SDL_Init(SDL_INIT_EVENTS))
    {
        return false;
    }

    State->LeftSliderValue = WINDOWS_INPUT_SLIDER_CENTRE;
    State->RightSliderValue = WINDOWS_INPUT_SLIDER_CENTRE;
    State->Initialised = true;

    return true;
}

uint8_t Input_Get_Count(void)
{
    return (uint8_t)(sizeof(Windows_InputInfo) / sizeof(Windows_InputInfo[0]));
}

bool Input_Get_Info(uint8_t Index, Input_InfoTypeDef *Info)
{
    if((Info == NULL) || (Index >= Input_Get_Count()))
    {
        return false;
    }

    *Info = Windows_InputInfo[Index];

    return true;
}

bool Input_Get_Value(Input_NumberTypeDef Number, int32_t *Value)
{
    const bool *KeyboardState;

    if(!Windows_InputState.Initialised || (Value == NULL))
    {
        return false;
    }

    Windows_InputUpdate();
    KeyboardState = SDL_GetKeyboardState(NULL);

    switch(Number)
    {
        case WINDOWS_INPUT_LEFT_SLIDER_NUMBER:
            *Value = Windows_InputState.LeftSliderValue;
            return true;

        case WINDOWS_INPUT_RIGHT_SLIDER_NUMBER:
            *Value = Windows_InputState.RightSliderValue;
            return true;

        case WINDOWS_INPUT_PRIMARY_BUTTON_NUMBER:
            *Value = KeyboardState[SDL_SCANCODE_SPACE] ? 1 : 0;
            return true;

        case WINDOWS_INPUT_SECONDARY_BUTTON_NUMBER:
            *Value = KeyboardState[SDL_SCANCODE_ESCAPE] ? 1 : 0;
            return true;

        case WINDOWS_INPUT_BATTERY_NUMBER:
            *Value = 100;
            return true;

        default:
            return false;
    }
}