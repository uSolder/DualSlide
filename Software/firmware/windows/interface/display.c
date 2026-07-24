/**
 * @file display.c
 * @brief SDL3 implementation of the portable CLUT8 display contract.
 */

#include "display.h"

#include <SDL3/SDL.h>

#define WINDOWS_DISPLAY_WIDTH    (800U)
#define WINDOWS_DISPLAY_HEIGHT   (480U)

typedef struct
{
    SDL_Window *Window;
    SDL_Renderer *Renderer;
    SDL_Texture *Texture;
    Display_FrameTypeDef Frame;
    Display_ColourTypeDef Palette[DISPLAY_PALETTE_SIZE];
    uint32_t ConvertedPalette[DISPLAY_PALETTE_SIZE];
    uint32_t PresentedPixels[WINDOWS_DISPLAY_WIDTH * WINDOWS_DISPLAY_HEIGHT];
    Display_PixelTypeDef FramePixels[WINDOWS_DISPLAY_WIDTH * WINDOWS_DISPLAY_HEIGHT];
    bool Initialised;
    bool FrameAcquired;
} Windows_DisplayStateTypeDef;

static Windows_DisplayStateTypeDef Windows_DisplayState;

static uint32_t Windows_DisplayConvertColour(Display_ColourTypeDef Colour)
{
    const uint32_t red = (Colour >> 16U) & 0xFFU;
    const uint32_t green = (Colour >> 8U) & 0xFFU;
    const uint32_t blue = Colour & 0xFFU;

    return (red << 24U) | (green << 16U) | (blue << 8U) | 0xFFU;
}

bool Display_Init(void)
{
    Windows_DisplayStateTypeDef *State = &Windows_DisplayState;

    if(State->Initialised)
    {
        return true;
    }

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        return false;
    }

    State->Window = SDL_CreateWindow("DualSlide", (int)WINDOWS_DISPLAY_WIDTH, (int)WINDOWS_DISPLAY_HEIGHT, 0U);

    if(State->Window == NULL)
    {
        return false;
    }

    State->Renderer = SDL_CreateRenderer(State->Window, NULL);

    if(State->Renderer == NULL)
    {
        SDL_DestroyWindow(State->Window);
        State->Window = NULL;
        return false;
    }

    /* VSync is preferred but not required for the simulator to function. */
    SDL_SetRenderVSync(State->Renderer, 1);

    State->Texture = SDL_CreateTexture(State->Renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, (int)WINDOWS_DISPLAY_WIDTH, (int)WINDOWS_DISPLAY_HEIGHT);

    if(State->Texture == NULL)
    {
        SDL_DestroyRenderer(State->Renderer);
        SDL_DestroyWindow(State->Window);
        State->Renderer = NULL;
        State->Window = NULL;
        return false;
    }

    State->Frame.Pixels = State->FramePixels;
    State->Frame.Width = WINDOWS_DISPLAY_WIDTH;
    State->Frame.Height = WINDOWS_DISPLAY_HEIGHT;
    State->Frame.StridePixels = WINDOWS_DISPLAY_WIDTH;
    State->Frame.PixelFormat = DISPLAY_PIXEL_FORMAT_CLUT8;
    State->Initialised = true;

    return true;
}

bool Display_SetPalette(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount)
{
    Windows_DisplayStateTypeDef *State = &Windows_DisplayState;

    if(!State->Initialised ||
       (Colours == NULL) ||
       (EntryCount == 0U) ||
       ((uint32_t)FirstEntry + EntryCount > DISPLAY_PALETTE_SIZE))
    {
        return false;
    }

    for(uint16_t EntryOffset = 0U; EntryOffset < EntryCount; EntryOffset++)
    {
        const uint16_t EntryIndex = FirstEntry + EntryOffset;

        State->Palette[EntryIndex] = Colours[EntryOffset];
        State->ConvertedPalette[EntryIndex] = Windows_DisplayConvertColour(Colours[EntryOffset]);
    }

    return true;
}

Display_FrameTypeDef *Display_AcquireFrame(void)
{
    Windows_DisplayStateTypeDef *State = &Windows_DisplayState;

    if(!State->Initialised || State->FrameAcquired)
    {
        return NULL;
    }

    State->FrameAcquired = true;

    return &State->Frame;
}

bool Display_PresentFrame(Display_FrameTypeDef *Frame)
{
    Windows_DisplayStateTypeDef *State = &Windows_DisplayState;

    if(!State->Initialised ||
       !State->FrameAcquired ||
       (Frame != &State->Frame))
    {
        return false;
    }

    for(uint32_t Y = 0U; Y < WINDOWS_DISPLAY_HEIGHT; Y++)
    {
        const Display_PixelTypeDef *SourceRow = &State->FramePixels[Y * WINDOWS_DISPLAY_WIDTH];
        uint32_t *DestinationRow = &State->PresentedPixels[Y * WINDOWS_DISPLAY_WIDTH];

        for(uint32_t X = 0U; X < WINDOWS_DISPLAY_WIDTH; X++)
        {
            DestinationRow[X] = State->ConvertedPalette[SourceRow[X]];
        }
    }

    State->FrameAcquired = false;

    return SDL_UpdateTexture(State->Texture, NULL, State->PresentedPixels, (int)(WINDOWS_DISPLAY_WIDTH * sizeof(State->PresentedPixels[0]))) &&
           SDL_RenderClear(State->Renderer) &&
           SDL_RenderTexture(State->Renderer, State->Texture, NULL, NULL) &&
           SDL_RenderPresent(State->Renderer);
}