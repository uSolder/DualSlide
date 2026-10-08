/**
 * @file display.c
 * @brief DSPCBR1 double-buffered CLUT8 display implementation.
 *
 * Frames use the LCD controller's native 480x800 layout. The renderer supplies
 * already-rotated pixel data, so this backend performs no rotation or copy.
 *
 * Frame acquisition is paced by the LTDC vertical-blank interrupt exposed
 * through the display-controller driver. At most one frame is acquired during
 * each display refresh period.
 */

#include "display.h"

#include "board.h"
#include "display_controller.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Display configuration                                                      */
/* -------------------------------------------------------------------------- */

#define DISPLAY_WIDTH                       480U
#define DISPLAY_HEIGHT                      800U
#define DISPLAY_FRAMEBUFFER_COUNT           2U
#define DISPLAY_FRAMEBUFFER_PIXEL_COUNT     (DISPLAY_WIDTH * DISPLAY_HEIGHT)
#define DISPLAY_FRAMEBUFFER_SIZE_BYTES      \
    (DISPLAY_FRAMEBUFFER_PIXEL_COUNT * sizeof(Display_PixelTypeDef))

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Runtime state owned by the embedded display backend.
 */
typedef struct
{
    DisplayController_HandleTypeDef *Controller;

    Display_FrameTypeDef Frames[DISPLAY_FRAMEBUFFER_COUNT];
    Display_ColourTypeDef Palette[DISPLAY_PALETTE_SIZE];

    uint32_t AcquiredVerticalBlankCount;

    uint8_t VisibleIndex;
    uint8_t WritableIndex;
    uint8_t PendingIndex;

    bool Initialized;
    bool FrameAcquired;
    bool SwapPending;
    bool PaletteDirty;
} Display_StateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/*
 * Each framebuffer is placed in a separate physical AXI SRAM bank by the
 * linker script. Target_Init() configures AXI SRAM2 and AXI SRAM3 as
 * non-cacheable memory, so explicit framebuffer cache maintenance is neither
 * required nor permitted here.
 */
static Display_PixelTypeDef Display_Framebuffer0[DISPLAY_FRAMEBUFFER_PIXEL_COUNT] __attribute__((section(".ltdc_framebuffer0"), aligned(128)));

static Display_PixelTypeDef Display_Framebuffer1[DISPLAY_FRAMEBUFFER_PIXEL_COUNT] __attribute__((section(".ltdc_framebuffer1"), aligned(128)));

static Display_PixelTypeDef * const Display_FramebufferPixels[DISPLAY_FRAMEBUFFER_COUNT] =
{
    Display_Framebuffer0,
    Display_Framebuffer1
};

_Static_assert(DISPLAY_FRAMEBUFFER_SIZE_BYTES <= (384U * 1024U), "Framebuffer does not fit in one 384 KB AXI SRAM bank.");

static Display_StateTypeDef Display_State;

static const DisplayController_LayerConfigurationTypeDef Display_Layer =
{
    .Framebuffer = Display_Framebuffer0,
    .Width = DISPLAY_WIDTH,
    .Height = DISPLAY_HEIGHT,
    .StrideBytes = DISPLAY_WIDTH * sizeof(Display_PixelTypeDef),
    .PixelFormat = DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Display_InitializeDefaultPalette(void);
static bool Display_ApplyPalette(void);
static bool Display_CompletePendingSwap(void);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize all palette entries to a grayscale ramp.
 */
static void Display_InitializeDefaultPalette(void)
{
    uint32_t Index;
    uint32_t Component;

    for(Index = 0U; Index < DISPLAY_PALETTE_SIZE; Index++)
    {
        Component = Index & 0xFFU;

        Display_State.Palette[Index] = (Component << 16U) | (Component << 8U) | Component;
    }

    Display_State.PaletteDirty = true;
}

/**
 * @brief Apply pending palette changes to the active LTDC layer.
 *
 * @return true if no update was required or the palette update was accepted;
 *         otherwise false.
 */
static bool Display_ApplyPalette(void)
{
    DisplayController_ResultTypeDef Result;

    if(!Display_State.PaletteDirty)
    {
        return true;
    }

    Result = DisplayController_SetPalette(Display_State.Controller, Display_State.Palette, DISPLAY_PALETTE_SIZE);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.PaletteDirty = false;

    return true;
}

/**
 * @brief Finish a framebuffer swap after the LTDC reload interrupt completes.
 *
 * The old visible framebuffer is not returned to the renderer until the
 * display-controller driver reports that the vertical-blank reload completed.
 *
 * @return true if no swap is pending or the pending swap completed; otherwise
 *         false.
 */
static bool Display_CompletePendingSwap(void)
{
    if(!Display_State.SwapPending)
    {
        return true;
    }

    if(DisplayController_IsReloadPending(Display_State.Controller))
    {
        return false;
    }

    if(!DisplayController_ConsumeReloadComplete(Display_State.Controller))
    {
        return false;
    }

    Display_State.VisibleIndex = Display_State.PendingIndex;
    Display_State.WritableIndex = (Display_State.VisibleIndex == 0U) ? 1U : 0U;

    Display_State.SwapPending = false;

    return true;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Display_Init(void)
{
    DisplayController_ResultTypeDef Result;
    uint32_t Index;

    if(Display_State.Initialized)
    {
        return true;
    }

    Display_State.Controller = Board_GetDisplayController();

    if(Display_State.Controller == NULL)
    {
        return false;
    }

    memset(Display_Framebuffer0, 0, sizeof(Display_Framebuffer0));
    memset(Display_Framebuffer1, 0, sizeof(Display_Framebuffer1));
    memset(&Display_State.Frames, 0, sizeof(Display_State.Frames));

    for(Index = 0U; Index < DISPLAY_FRAMEBUFFER_COUNT; Index++)
    {
        Display_State.Frames[Index].Pixels = Display_FramebufferPixels[Index];
        Display_State.Frames[Index].Width = DISPLAY_WIDTH;
        Display_State.Frames[Index].Height = DISPLAY_HEIGHT;
        Display_State.Frames[Index].StridePixels = DISPLAY_WIDTH;
        Display_State.Frames[Index].PixelFormat = DISPLAY_PIXEL_FORMAT_CLUT8;
    }

    Display_State.AcquiredVerticalBlankCount = UINT32_MAX;
    Display_State.VisibleIndex = 0U;
    Display_State.WritableIndex = 1U;
    Display_State.PendingIndex = 0U;
    Display_State.FrameAcquired = false;
    Display_State.SwapPending = false;
    Display_State.PaletteDirty = false;
    Display_State.Initialized = false;

    Display_InitializeDefaultPalette();

    Result = DisplayController_ConfigureLayer(Display_State.Controller, &Display_Layer);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    if(!Display_ApplyPalette())
    {
        return false;
    }

    Result = DisplayController_Enable(Display_State.Controller);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.Initialized = true;

    return true;
}

bool Display_SetPalette(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount)
{
    uint32_t FinalEntry;

    if(!Display_State.Initialized)
    {
        return false;
    }

    if((Colours == NULL) || (EntryCount == 0U))
    {
        return false;
    }

    FinalEntry = (uint32_t)FirstEntry + (uint32_t)EntryCount;

    if(FinalEntry > DISPLAY_PALETTE_SIZE)
    {
        return false;
    }

    memcpy(&Display_State.Palette[FirstEntry], Colours, (size_t)EntryCount * sizeof(Display_ColourTypeDef));

    Display_State.PaletteDirty = true;

    return true;
}

Display_FrameTypeDef *Display_AcquireFrame(void)
{
    uint32_t VerticalBlankCount;

    if(!Display_State.Initialized)
    {
        return NULL;
    }

    if(Display_State.FrameAcquired)
    {
        return NULL;
    }

    /*
     * The previous front buffer remains owned by LTDC until the reload-complete
     * interrupt confirms that the queued framebuffer-address change occurred.
     */
    if(!Display_CompletePendingSwap())
    {
        return NULL;
    }

    VerticalBlankCount = DisplayController_GetVerticalBlankCount(Display_State.Controller);

    /*
     * Permit at most one acquisition per vertical-blank period. UINT32_MAX is
     * used during initialization so the first frame can be acquired immediately.
     */
    if(VerticalBlankCount == Display_State.AcquiredVerticalBlankCount)
    {
        return NULL;
    }

    Display_State.AcquiredVerticalBlankCount = VerticalBlankCount;
    Display_State.FrameAcquired = true;

    return &Display_State.Frames[Display_State.WritableIndex];
}

bool Display_PresentFrame(Display_FrameTypeDef *Frame)
{
    DisplayController_ResultTypeDef Result;

    if(!Display_State.Initialized)
    {
        return false;
    }

    if(!Display_State.FrameAcquired)
    {
        return false;
    }

    if(Frame != &Display_State.Frames[Display_State.WritableIndex])
    {
        return false;
    }

    if(Display_State.SwapPending)
    {
        return false;
    }

    if(!Display_ApplyPalette())
    {
        return false;
    }

    Result = DisplayController_SetFramebuffer(Display_State.Controller, Frame->Pixels);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.PendingIndex = Display_State.WritableIndex;
    Display_State.FrameAcquired = false;
    Display_State.SwapPending = true;

    return true;
}

void Display_WaitForFrame(void)
{
    uint32_t VerticalBlankCount;

    if(!Display_State.Initialized)
    {
        return;
    }

    for(;;)
    {
        if(!Display_CompletePendingSwap())
        {
            DisplayController_WaitForEvent(Display_State.Controller);
            continue;
        }

        VerticalBlankCount = DisplayController_GetVerticalBlankCount(Display_State.Controller);

        if(VerticalBlankCount != Display_State.AcquiredVerticalBlankCount)
        {
            return;
        }

        DisplayController_WaitForEvent(Display_State.Controller);
    }
}
