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

#ifndef TARGET_INTERFACE_DISPLAY_CONTROLLER_H
#define TARGET_INTERFACE_DISPLAY_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

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
typedef uint8_t DisplayController_Pin;

/**
 * @brief Value used when a display-controller signal is not connected.
 */
#define DISPLAY_CONTROLLER_PIN_UNUSED ((DisplayController_Pin)0xFFU)

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
} DisplayController_PixelFormat;

/**
 * @brief Active polarity of a display-control signal.
 */
typedef enum
{
    DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW = 0,
    DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH
} DisplayController_Polarity;

/**
 * @brief Edge on which the connected display samples pixel data.
 */
typedef enum
{
    DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE = 0,
    DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE
} DisplayController_PixelClockEdge;

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
} DisplayController_Result;

/**
 * @brief Physical display-controller output pins.
 *
 * Unused colour-bit pins must be set to DISPLAY_CONTROLLER_PIN_UNUSED.
 *
 * Colour pins are ordered from least significant bit to most significant bit.
 */
typedef struct
{
    DisplayController_Pin horizontal_sync_pin;
    DisplayController_Pin vertical_sync_pin;
    DisplayController_Pin data_enable_pin;
    DisplayController_Pin pixel_clock_pin;

    DisplayController_Pin red_pins[8];
    DisplayController_Pin green_pins[8];
    DisplayController_Pin blue_pins[8];
} DisplayController_PinConfiguration;

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
    uint16_t active_width;
    uint16_t active_height;

    uint16_t horizontal_sync_width;
    uint16_t horizontal_back_porch;
    uint16_t horizontal_front_porch;

    uint16_t vertical_sync_height;
    uint16_t vertical_back_porch;
    uint16_t vertical_front_porch;

    uint32_t refresh_rate_millihz;
} DisplayController_Timing;

/**
 * @brief Display signal-polarity configuration.
 */
typedef struct
{
    DisplayController_Polarity horizontal_sync;
    DisplayController_Polarity vertical_sync;
    DisplayController_Polarity data_enable;
    DisplayController_PixelClockEdge pixel_clock_edge;
} DisplayController_SignalConfiguration;

/**
 * @brief Background colour displayed outside the active layer.
 */
typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} DisplayController_Color;

/**
 * @brief Framebuffer layer configuration.
 *
 * The layer width and height must match the active width and height configured
 * in DisplayController_Timing.
 *
 * The stride is the number of bytes between the beginning of two consecutive
 * framebuffer rows. It may be greater than the visible row size when rows
 * contain padding.
 */
typedef struct
{
    void *framebuffer;

    uint16_t width;
    uint16_t height;
    size_t stride_bytes;

    DisplayController_PixelFormat pixel_format;
} DisplayController_LayerConfiguration;

/**
 * @brief Physical configuration of one raster display controller.
 *
 * The handle contains board-owned hardware configuration. Framebuffer storage
 * and layer configuration are supplied separately by the display subsystem.
 */
typedef struct
{
    DisplayController_PinConfiguration pins;
    DisplayController_Timing timing;
    DisplayController_SignalConfiguration signals;
    DisplayController_Color background_color;
} DisplayController_Handle;

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
 * @param controller Display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_Init(DisplayController_Handle *controller);

/**
 * @brief Configure the framebuffer layer scanned by the controller.
 *
 * The controller must already be initialized. The framebuffer must remain
 * valid and accessible to the display controller while the layer is active.
 *
 * This operation leaves the layer disabled until DisplayController_Enable()
 * is called.
 *
 * @param controller Initialized display-controller handle.
 * @param layer      Framebuffer layer configuration.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_ConfigureLayer(DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer);

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
 * @param controller  Initialized display-controller handle.
 * @param framebuffer New framebuffer address.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_SetFramebuffer(DisplayController_Handle *controller, void *framebuffer);

/**
 * @brief Configure the colour lookup table for an indexed framebuffer.
 *
 * Each palette entry is encoded as 0x00RRGGBB. This operation is valid only
 * when the configured layer uses
 * DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT.
 *
 * @param controller Initialized display-controller handle.
 * @param palette    Palette entries encoded as 0x00RRGGBB.
 * @param count      Number of palette entries, from 1 to 256.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_SetPalette(DisplayController_Handle *controller, const uint32_t *palette, size_t count);

/* -------------------------------------------------------------------------- */
/* Controller state                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enable the configured framebuffer layer and display output.
 *
 * A framebuffer layer must first be configured with
 * DisplayController_ConfigureLayer().
 *
 * @param controller Initialized display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_Enable(DisplayController_Handle *controller);

/**
 * @brief Disable the framebuffer layer and display output.
 *
 * @param controller Initialized display-controller handle.
 *
 * @return DISPLAY_CONTROLLER_RESULT_OK on success.
 */
DisplayController_Result DisplayController_Disable(DisplayController_Handle *controller);

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
uint32_t DisplayController_GetVerticalBlankCount(const DisplayController_Handle *Controller);

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
bool DisplayController_IsReloadPending(const DisplayController_Handle *Controller);

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
bool DisplayController_ConsumeReloadComplete(DisplayController_Handle *Controller);

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
void DisplayController_WaitForEvent(DisplayController_Handle *Controller);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_DISPLAY_CONTROLLER_H */