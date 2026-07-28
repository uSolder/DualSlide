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
    DisplayController_Handle *controller;

    Display_FrameTypeDef frames[DISPLAY_FRAMEBUFFER_COUNT];
    Display_ColourTypeDef palette[DISPLAY_PALETTE_SIZE];

    uint32_t acquired_vertical_blank_count;

    uint8_t visible_index;
    uint8_t writable_index;
    uint8_t pending_index;

    bool initialized;
    bool frame_acquired;
    bool swap_pending;
    bool palette_dirty;
} Display_StateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/*
 * Both buffers must be placed in SRAM accessible by LTDC.
 *
 * The buffers must not be placed in DTCM. Their 32-byte alignment also permits
 * complete Cortex-M7 data-cache clean operations.
 */
static Display_PixelTypeDef Display_Framebuffers
    [DISPLAY_FRAMEBUFFER_COUNT]
    [DISPLAY_FRAMEBUFFER_PIXEL_COUNT]
    __attribute__((section(".ltdc_framebuffer"), aligned(32)));

static Display_StateTypeDef Display_State;

static const DisplayController_LayerConfiguration Display_Layer =
{
    .framebuffer = Display_Framebuffers[0],
    .width = DISPLAY_WIDTH,
    .height = DISPLAY_HEIGHT,
    .stride_bytes = DISPLAY_WIDTH * sizeof(Display_PixelTypeDef),
    .pixel_format = DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT
};

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static void Display_InitializeDefaultPalette(void);
static bool Display_ApplyPalette(void);
static bool Display_CompletePendingSwap(void);
static void Display_CleanFramebufferCache(const Display_FrameTypeDef *Frame);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize all palette entries to a grayscale ramp.
 */
static void Display_InitializeDefaultPalette(void)
{
    uint32_t index;
    uint32_t component;

    for(index = 0U; index < DISPLAY_PALETTE_SIZE; index++)
    {
        component = index & 0xFFU;

        Display_State.palette[index] =
            (component << 16U) |
            (component << 8U) |
            component;
    }

    Display_State.palette_dirty = true;
}

/**
 * @brief Apply pending palette changes to the active LTDC layer.
 *
 * @return true if no update was required or the palette update was accepted;
 *         otherwise false.
 */
static bool Display_ApplyPalette(void)
{
    DisplayController_Result result;

    if(!Display_State.palette_dirty)
    {
        return true;
    }

    result = DisplayController_SetPalette(
        Display_State.controller,
        Display_State.palette,
        DISPLAY_PALETTE_SIZE);

    if(result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.palette_dirty = false;

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
    if(!Display_State.swap_pending)
    {
        return true;
    }

    if(DisplayController_IsReloadPending(Display_State.controller))
    {
        return false;
    }

    if(!DisplayController_ConsumeReloadComplete(Display_State.controller))
    {
        return false;
    }

    Display_State.visible_index = Display_State.pending_index;
    Display_State.writable_index =
        (Display_State.visible_index == 0U) ? 1U : 0U;

    Display_State.swap_pending = false;

    return true;
}

/**
 * @brief Make CPU framebuffer writes visible to LTDC.
 *
 * @param Frame Framebuffer description whose pixel memory must be cleaned.
 */
static void Display_CleanFramebufferCache(const Display_FrameTypeDef *Frame)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr(
        (uint32_t *)(void *)Frame->Pixels,
        (int32_t)DISPLAY_FRAMEBUFFER_SIZE_BYTES);
#else
    (void)Frame;
#endif
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Display_Init(void)
{
    DisplayController_Result result;
    uint32_t index;

    if(Display_State.initialized)
    {
        return true;
    }

    Display_State.controller = Board_GetDisplayController();

    if(Display_State.controller == NULL)
    {
        return false;
    }

    memset(Display_Framebuffers, 0, sizeof(Display_Framebuffers));
    memset(&Display_State.frames, 0, sizeof(Display_State.frames));

    for(index = 0U; index < DISPLAY_FRAMEBUFFER_COUNT; index++)
    {
        Display_State.frames[index].Pixels = Display_Framebuffers[index];
        Display_State.frames[index].Width = DISPLAY_WIDTH;
        Display_State.frames[index].Height = DISPLAY_HEIGHT;
        Display_State.frames[index].StridePixels = DISPLAY_WIDTH;
        Display_State.frames[index].PixelFormat = DISPLAY_PIXEL_FORMAT_CLUT8;
    }

    Display_State.acquired_vertical_blank_count = UINT32_MAX;
    Display_State.visible_index = 0U;
    Display_State.writable_index = 1U;
    Display_State.pending_index = 0U;
    Display_State.frame_acquired = false;
    Display_State.swap_pending = false;
    Display_State.palette_dirty = false;
    Display_State.initialized = false;

    Display_InitializeDefaultPalette();

    Display_CleanFramebufferCache(&Display_State.frames[0]);
    Display_CleanFramebufferCache(&Display_State.frames[1]);

    result = DisplayController_ConfigureLayer(Display_State.controller, &Display_Layer);

    if(result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    if(!Display_ApplyPalette())
    {
        return false;
    }

    result = DisplayController_Enable(Display_State.controller);

    if(result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.initialized = true;

    return true;
}

bool Display_SetPalette(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount)
{
    uint32_t final_entry;

    if(!Display_State.initialized)
    {
        return false;
    }

    if((Colours == NULL) || (EntryCount == 0U))
    {
        return false;
    }

    final_entry = (uint32_t)FirstEntry + (uint32_t)EntryCount;

    if(final_entry > DISPLAY_PALETTE_SIZE)
    {
        return false;
    }

    memcpy(
        &Display_State.palette[FirstEntry],
        Colours,
        (size_t)EntryCount * sizeof(Display_ColourTypeDef));

    Display_State.palette_dirty = true;

    return true;
}

Display_FrameTypeDef *Display_AcquireFrame(void)
{
    uint32_t vertical_blank_count;

    if(!Display_State.initialized)
    {
        return NULL;
    }

    if(Display_State.frame_acquired)
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

    vertical_blank_count =
        DisplayController_GetVerticalBlankCount(Display_State.controller);

    /*
     * Permit at most one acquisition per vertical-blank period. UINT32_MAX is
     * used during initialization so the first frame can be acquired immediately.
     */
    if(vertical_blank_count == Display_State.acquired_vertical_blank_count)
    {
        return NULL;
    }

    Display_State.acquired_vertical_blank_count = vertical_blank_count;
    Display_State.frame_acquired = true;

    return &Display_State.frames[Display_State.writable_index];
}

bool Display_PresentFrame(Display_FrameTypeDef *Frame)
{
    DisplayController_Result result;

    if(!Display_State.initialized)
    {
        return false;
    }

    if(!Display_State.frame_acquired)
    {
        return false;
    }

    if(Frame != &Display_State.frames[Display_State.writable_index])
    {
        return false;
    }

    if(Display_State.swap_pending)
    {
        return false;
    }

    if(!Display_ApplyPalette())
    {
        return false;
    }

    Display_CleanFramebufferCache(Frame);

    result = DisplayController_SetFramebuffer(Display_State.controller, Frame->Pixels);

    if(result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return false;
    }

    Display_State.pending_index = Display_State.writable_index;
    Display_State.frame_acquired = false;
    Display_State.swap_pending = true;

    return true;
}

void Display_WaitForFrame(void)
{
    uint32_t vertical_blank_count;

    if(!Display_State.initialized)
    {
        return;
    }

    for(;;)
    {
        if(!Display_CompletePendingSwap())
        {
            DisplayController_WaitForEvent(Display_State.controller);
            continue;
        }

        vertical_blank_count =
            DisplayController_GetVerticalBlankCount(Display_State.controller);

        if(vertical_blank_count != Display_State.acquired_vertical_blank_count)
        {
            return;
        }

        DisplayController_WaitForEvent(Display_State.controller);
    }
}