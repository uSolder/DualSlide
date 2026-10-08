/**
 * @file display_controller.h
 * @brief Hardware-independent raster display-controller interface contract.
 *
 * This interface represents an MCU display controller that continuously reads
 * pixel data from a framebuffer and outputs a parallel raster display signal.
 *
 * The target-specific implementation translates the generic pin, timing,
 * framebuffer, pixel-format, and signal-polarity configuration into the
 * appropriate peripheral and GPIO registers.
 */

#ifndef TARGET_API_DISPLAY_CONTROLLER_H
#define TARGET_API_DISPLAY_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Target identifiers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Target-defined GPIO pin identifier.
 *
 * Pin constants are provided by the selected target definitions file.
 */
typedef uint8_t DisplayController_PinTypeDef;

/**
 * @brief Value used when a display-controller signal is not connected.
 */
#define DISPLAY_CONTROLLER_PIN_UNUSED ((DisplayController_PinTypeDef)0xFFU)

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Pixel formats supported by a display controller.
 *
 * A target may support only a subset of these formats.
 */
typedef enum
{
    DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT = 0,
    DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB565,
    DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB888,
    DISPLAY_CONTROLLER_PIXEL_FORMAT_ARGB8888
} DisplayController_PixelFormatTypeDef;

/**
 * @brief Active polarity of a display-control signal.
 */
typedef enum
{
    DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW = 0,
    DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH
} DisplayController_PolarityTypeDef;

/**
 * @brief Edge on which the connected display samples pixel data.
 */
typedef enum
{
    DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE = 0,
    DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE
} DisplayController_PixelClockEdgeTypeDef;

/**
 * @brief Result returned by a display-controller operation.
 */
typedef enum
{
    DISPLAY_CONTROLLER_RESULT_OK = 0,
    DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT,
    DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED,
    DISPLAY_CONTROLLER_RESULT_UNSUPPORTED,
    DISPLAY_CONTROLLER_RESULT_BUSY,
    DISPLAY_CONTROLLER_RESULT_TIMEOUT,
    DISPLAY_CONTROLLER_RESULT_IO_ERROR
} DisplayController_ResultTypeDef;

/**
 * @brief Physical display-controller output pins.
 *
 * Unused colour-bit pins must be set to DISPLAY_CONTROLLER_PIN_UNUSED.
 *
 * Colour pins are ordered from least significant bit to most significant bit.
 */
typedef struct
{
    DisplayController_PinTypeDef HorizontalSyncPin;
    DisplayController_PinTypeDef VerticalSyncPin;
    DisplayController_PinTypeDef DataEnablePin;
    DisplayController_PinTypeDef PixelClockPin;

    DisplayController_PinTypeDef RedPins[8];
    DisplayController_PinTypeDef GreenPins[8];
    DisplayController_PinTypeDef BluePins[8];
} DisplayController_PinConfigurationTypeDef;

/**
 * @brief Horizontal and vertical raster timing configuration.
 *
 * Horizontal values are specified in pixel-clock periods. Vertical values are
 * specified in complete display lines.
 *
 * The requested refresh rate is specified in millihertz. For example, 60000
 * represents 60 Hz. The target implementation calculates the required pixel
 * clock from the complete raster timing and validates it against the clock
 * supplied to the display peripheral.
 */
typedef struct
{
    uint16_t ActiveWidth;
    uint16_t ActiveHeight;

    uint16_t HorizontalSyncWidth;
    uint16_t HorizontalBackPorch;
    uint16_t HorizontalFrontPorch;

    uint16_t VerticalSyncHeight;
    uint16_t VerticalBackPorch;
    uint16_t VerticalFrontPorch;

    uint32_t RefreshRateMilliHz;
} DisplayController_TimingTypeDef;

/**
 * @brief Display signal-polarity configuration.
 */
typedef struct
{
    DisplayController_PolarityTypeDef HorizontalSync;
    DisplayController_PolarityTypeDef VerticalSync;
    DisplayController_PolarityTypeDef DataEnable;
    DisplayController_PixelClockEdgeTypeDef PixelClockEdge;
} DisplayController_SignalConfigurationTypeDef;

/**
 * @brief Background colour displayed outside the active layer.
 */
typedef struct
{
    uint8_t Red;
    uint8_t Green;
    uint8_t Blue;
} DisplayController_ColourTypeDef;

/**
 * @brief Framebuffer layer configuration.
 *
 * The layer width and height must match the active width and height configured
 * in DisplayController_TimingTypeDef.
 *
 * The stride is the number of bytes between the beginning of two consecutive
 * framebuffer rows. It may be greater than the visible row size when rows
 * contain padding.
 */
typedef struct
{
    void *Framebuffer;

    uint16_t Width;
    uint16_t Height;
    size_t StrideBytes;

    DisplayController_PixelFormatTypeDef PixelFormat;
} DisplayController_LayerConfigurationTypeDef;

/**
 * @brief Physical configuration of one raster display controller.
 *
 * The handle contains board-owned hardware configuration. Framebuffer storage
 * and layer configuration are supplied separately by the display subsystem.
 */
typedef struct
{
    DisplayController_PinConfigurationTypeDef Pins;
    DisplayController_TimingTypeDef Timing;
    DisplayController_SignalConfigurationTypeDef Signals;
    DisplayController_ColourTypeDef BackgroundColour;
} DisplayController_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a raster display controller.
 *
 * Configures the target GPIO pins, display peripheral, raster timings, output
 * signal polarities, and background colour.
 *
 * Initialization leaves the framebuffer layer and display output disabled.
 *
 * @param Controller Display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_Init(DisplayController_HandleTypeDef *Controller);

/**
 * @brief Configure the framebuffer layer scanned by the controller.
 *
 * The controller must already be initialized. The framebuffer must remain
 * valid and accessible to the display controller while the layer is active.
 *
 * This operation leaves the layer disabled until DisplayController_Enable()
 * is called.
 *
 * @param Controller Initialized display-controller handle.
 * @param Layer      Framebuffer layer configuration.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_ConfigureLayer(DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer);

/* -------------------------------------------------------------------------- */
/* Framebuffer control                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Change the framebuffer scanned by the display controller.
 *
 * The new framebuffer uses the width, height, stride, and pixel format supplied
 * to DisplayController_ConfigureLayer().
 *
 * Where supported, an active controller applies the address during vertical
 * blanking.
 *
 * @param Controller  Initialized display-controller handle.
 * @param Framebuffer New framebuffer address.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_SetFramebuffer(DisplayController_HandleTypeDef *Controller, void *Framebuffer);

/**
 * @brief Configure the colour lookup table for an indexed framebuffer.
 *
 * Each palette entry is encoded as 0x00RRGGBB. This operation is valid only
 * when the configured layer uses
 * DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT.
 *
 * @param Controller Initialized display-controller handle.
 * @param Palette    Palette entries encoded as 0x00RRGGBB.
 * @param Count      Number of palette entries, from 1 to 256.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_SetPalette(DisplayController_HandleTypeDef *Controller, const uint32_t *Palette, size_t Count);

/* -------------------------------------------------------------------------- */
/* Controller state                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enable the configured framebuffer layer and display output.
 *
 * A framebuffer layer must first be configured with
 * DisplayController_ConfigureLayer().
 *
 * @param Controller Initialized display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_Enable(DisplayController_HandleTypeDef *Controller);

/**
 * @brief Disable the framebuffer layer and display output.
 *
 * @param Controller Initialized display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_ResultTypeDef DisplayController_Disable(DisplayController_HandleTypeDef *Controller);

/**
 * @brief Return the number of vertical-blank periods observed by the controller.
 *
 * The count is incremented by DisplayController_IRQHandler() whenever the LTDC
 * line interrupt marks the beginning of a new vertical-blank interval.
 *
 * @param Controller Display-controller handle.
 *
 * @return Number of vertical-blank periods observed since the controller was
 *         enabled, or zero if the handle is invalid or not initialized.
 */
uint32_t DisplayController_GetVerticalBlankCount(const DisplayController_HandleTypeDef *Controller);

/**
 * @brief Determine whether a vertical-blank framebuffer reload is pending.
 *
 * A reload becomes pending when DisplayController_SetFramebuffer() requests a
 * shadow-register reload during vertical blanking. It remains pending until the
 * LTDC reload-complete interrupt is handled.
 *
 * @param Controller Display-controller handle.
 *
 * @return true if a reload is pending; otherwise false.
 */
bool DisplayController_IsReloadPending(const DisplayController_HandleTypeDef *Controller);

/**
 * @brief Read and clear the framebuffer-reload completion event.
 *
 * Returns whether the LTDC has completed a previously requested vertical-blank
 * shadow-register reload. When true is returned, the stored completion event is
 * cleared so it is consumed only once.
 *
 * @param Controller Display-controller handle.
 *
 * @return true if a reload-complete event was pending; otherwise false.
 */
bool DisplayController_ConsumeReloadComplete(DisplayController_HandleTypeDef *Controller);

/**
 * @brief Handle LTDC frame and framebuffer-reload interrupts.
 *
 * Processes the LTDC line interrupt used to mark the beginning of vertical
 * blanking and the reload interrupt used to confirm completion of a pending
 * shadow-register reload.
 *
 * This function is intended to be called directly by LTDC_IRQHandler() in the
 * target interrupt-vector file.
 */
void DisplayController_IRQHandler(void);

/**
 * @brief Wait until the next display-controller event occurs.
 */
void DisplayController_WaitForEvent(DisplayController_HandleTypeDef *Controller);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_DISPLAY_CONTROLLER_H */
