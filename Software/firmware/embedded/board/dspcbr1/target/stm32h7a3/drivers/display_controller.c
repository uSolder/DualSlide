/**
 * @file display_controller.c
 * @brief STM32H7A3 raster display-controller implementation.
 *
 * This driver implements the hardware-independent display-controller contract
 * using the STM32H7A3 LTDC peripheral and LTDC layer 1.
 */

#include "display_controller.h"

#include "STM32H7A3_Defs.h"
#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define DISPLAY_CONTROLLER_REGISTRY_SIZE               1U
#define DISPLAY_CONTROLLER_MAX_PALETTE_ENTRIES         256U
#define DISPLAY_CONTROLLER_PIXEL_CLOCK_TOLERANCE_PCT   3U
#define DISPLAY_CONTROLLER_INTERRUPT_PRIORITY          5U
#define DISPLAY_CONTROLLER_ERROR_INTERRUPT_PRIORITY    5U

#define DISPLAY_CONTROLLER_GPIO_MODE_ALTERNATE         2U
#define DISPLAY_CONTROLLER_GPIO_SPEED_VERY_HIGH        3U

#define DISPLAY_CONTROLLER_BLEND_FACTOR_1_CA           0x06U
#define DISPLAY_CONTROLLER_BLEND_FACTOR_2_CA           0x07U

#define DISPLAY_CONTROLLER_CLUT_INDEX_POSITION         24U
#define DISPLAY_CONTROLLER_CLUT_RED_POSITION           16U
#define DISPLAY_CONTROLLER_CLUT_GREEN_POSITION         8U
#define DISPLAY_CONTROLLER_CLUT_BLUE_POSITION          0U

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Runtime state associated with one initialized display controller.
 */
typedef struct
{
    DisplayController_Handle *handle;
    DisplayController_LayerConfiguration layer;
    volatile uint32_t vertical_blank_count;
    volatile bool reload_pending;
    volatile bool reload_complete;

    bool initialized;
    bool layer_configured;
    bool enabled;
} DisplayController_State;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static DisplayController_State DisplayController_Registry[DISPLAY_CONTROLLER_REGISTRY_SIZE];

/*
 * Debugger-visible LTDC fault counters.
 *
 * Add these symbols to the debugger watch window. A non-zero or increasing
 * value indicates that LTDC was unable to fetch framebuffer data in time or
 * encountered an AXI transfer error.
 */
volatile uint32_t DisplayController_FifoUnderrunCount;
volatile uint32_t DisplayController_TransferErrorCount;

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static DisplayController_State *DisplayController_FindState(const DisplayController_Handle *controller);
static DisplayController_State *DisplayController_AllocateState(DisplayController_Handle *controller);
static DisplayController_Result DisplayController_ValidateConfiguration(const DisplayController_Handle *controller);
static DisplayController_Result DisplayController_ValidateLayer(const DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer);
static DisplayController_Result DisplayController_ValidatePins(const DisplayController_PinConfiguration *pins);
static DisplayController_Result DisplayController_ValidateRequiredPin(DisplayController_Pin pin);
static DisplayController_Result DisplayController_ValidateColorPins(const DisplayController_Pin pins[8]);
static GPIO_TypeDef *DisplayController_GetGPIOPort(DisplayController_Pin pin);
static uint32_t DisplayController_GetGPIOPinNumber(DisplayController_Pin pin);
static DisplayController_Result DisplayController_GetAlternateFunction(DisplayController_Pin pin, uint32_t *alternate_function);
static DisplayController_Result DisplayController_ConfigurePin(DisplayController_Pin pin);
static DisplayController_Result DisplayController_ConfigurePins(const DisplayController_PinConfiguration *pins);
static DisplayController_Result DisplayController_ConfigureGlobalRegisters(const DisplayController_Handle *controller);
static DisplayController_Result DisplayController_ConfigureLayerRegisters(const DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer);
static uint32_t DisplayController_CalculatePixelClock(const DisplayController_Timing *timing);
static uint32_t DisplayController_GetPixelFormatEncoding(DisplayController_PixelFormat pixel_format);
static uint32_t DisplayController_GetBytesPerPixel(DisplayController_PixelFormat pixel_format);
static DisplayController_Result DisplayController_ReloadImmediate(void);
static DisplayController_Result DisplayController_ReloadVerticalBlanking(DisplayController_State *state);
static void DisplayController_WaitForVerticalBlank(const DisplayController_State *state);
static void DisplayController_ConfigureInterrupts(const DisplayController_Handle *controller);
static void DisplayController_DisableInterrupts(void);

/* -------------------------------------------------------------------------- */
/* State registry                                                             */
/* -------------------------------------------------------------------------- */

static DisplayController_State *DisplayController_FindState(const DisplayController_Handle *controller)
{
    size_t index;

    if (controller == NULL)
    {
        return NULL;
    }

    for (index = 0U; index < DISPLAY_CONTROLLER_REGISTRY_SIZE; index++)
    {
        if (DisplayController_Registry[index].initialized && (DisplayController_Registry[index].handle == controller))
        {
            return &DisplayController_Registry[index];
        }
    }

    return NULL;
}

static DisplayController_State *DisplayController_AllocateState(DisplayController_Handle *controller)
{
    size_t index;

    for (index = 0U; index < DISPLAY_CONTROLLER_REGISTRY_SIZE; index++)
    {
        if (!DisplayController_Registry[index].initialized)
        {
            DisplayController_Registry[index].handle = controller;
            DisplayController_Registry[index].vertical_blank_count = 0U;
            DisplayController_Registry[index].reload_pending = false;
            DisplayController_Registry[index].reload_complete = false;
            DisplayController_Registry[index].layer_configured = false;
            DisplayController_Registry[index].enabled = false;

            return &DisplayController_Registry[index];
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
/* GPIO helpers                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *DisplayController_GetGPIOPort(DisplayController_Pin pin)
{
    uint32_t port_index;

    if (pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return NULL;
    }

    port_index = ((uint32_t)pin >> 4U) & 0x0FU;

    switch (port_index)
    {
        case 0U: return GPIOA;

        case 1U: return GPIOB;

        case 2U: return GPIOC;

        case 3U: return GPIOD;

        case 4U: return GPIOE;

        case 5U: return GPIOF;

        case 6U: return GPIOG;

        case 7U: return GPIOH;

        case 8U: return GPIOI;

        case 9U: return GPIOJ;

        case 10U: return GPIOK;

        default: return NULL;
    }
}

static uint32_t DisplayController_GetGPIOPinNumber(DisplayController_Pin pin)
{
    return (uint32_t)pin & 0x0FU;
}

/* -------------------------------------------------------------------------- */
/* Alternate-function mapping                                                 */
/* -------------------------------------------------------------------------- */

static DisplayController_Result DisplayController_GetAlternateFunction(DisplayController_Pin pin, uint32_t *alternate_function)
{
    if (alternate_function == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    switch (pin)
    {
        case PC0: case PC1: case PA5: case PA6: case PA7: case PC4: case PC5: case PB10: case PB14: case PB15: case PC6: case PC7: case PC10: case PC11: case PD2: case PB8: case PB9:
            *alternate_function = 14U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PA8:
            *alternate_function = 13U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PB5:
            *alternate_function = 11U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PC9:
            *alternate_function = 10U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PB0: case PB1:
            *alternate_function = 9U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        default: return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static DisplayController_Result DisplayController_ValidateRequiredPin(DisplayController_Pin pin)
{
    uint32_t alternate_function;

    if (pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if (DisplayController_GetGPIOPort(pin) == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    return DisplayController_GetAlternateFunction(pin, &alternate_function);
}

static DisplayController_Result DisplayController_ValidateColorPins(const DisplayController_Pin pins[8])
{
    size_t index;
    uint32_t alternate_function;

    for (index = 0U; index < 8U; index++)
    {
        if (pins[index] == DISPLAY_CONTROLLER_PIN_UNUSED)
        {
            continue;
        }

        if (DisplayController_GetGPIOPort(pins[index]) == NULL)
        {
            return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
        }

        if (DisplayController_GetAlternateFunction(pins[index], &alternate_function) != DISPLAY_CONTROLLER_RESULT_OK)
        {
            return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
        }
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_Result DisplayController_ValidatePins(const DisplayController_PinConfiguration *pins)
{
    DisplayController_Result result;

    if (pins == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    result = DisplayController_ValidateRequiredPin(pins->horizontal_sync_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ValidateRequiredPin(pins->vertical_sync_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ValidateRequiredPin(pins->data_enable_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ValidateRequiredPin(pins->pixel_clock_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ValidateColorPins(pins->red_pins);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ValidateColorPins(pins->green_pins);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    return DisplayController_ValidateColorPins(pins->blue_pins);
}

static DisplayController_Result DisplayController_ValidateConfiguration(const DisplayController_Handle *controller)
{
    uint32_t requested_pixel_clock_hz;
    uint32_t actual_pixel_clock_hz;
    uint32_t difference_hz;
    uint32_t tolerance_hz;

    if (controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->timing.active_width == 0U) || (controller->timing.active_height == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->timing.horizontal_sync_width == 0U) || (controller->timing.vertical_sync_height == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if (controller->timing.refresh_rate_millihz == 0U)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->signals.horizontal_sync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (controller->signals.horizontal_sync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->signals.vertical_sync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (controller->signals.vertical_sync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->signals.data_enable != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (controller->signals.data_enable != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((controller->signals.pixel_clock_edge != DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE) && (controller->signals.pixel_clock_edge != DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    requested_pixel_clock_hz = DisplayController_CalculatePixelClock(&controller->timing);
    actual_pixel_clock_hz = RCC_GetKernelFrequency(LTDC);

    if ((requested_pixel_clock_hz == 0U) || (actual_pixel_clock_hz == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    difference_hz = (requested_pixel_clock_hz > actual_pixel_clock_hz)
        ? requested_pixel_clock_hz - actual_pixel_clock_hz
        : actual_pixel_clock_hz - requested_pixel_clock_hz;

    tolerance_hz = (requested_pixel_clock_hz / 100U) * DISPLAY_CONTROLLER_PIXEL_CLOCK_TOLERANCE_PCT;

    if (difference_hz > tolerance_hz)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    return DisplayController_ValidatePins(&controller->pins);
}

static DisplayController_Result DisplayController_ValidateLayer(const DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer)
{
    uint32_t bytes_per_pixel;
    size_t minimum_stride;

    if ((controller == NULL) || (layer == NULL) || (layer->framebuffer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((layer->width == 0U) || (layer->height == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if ((layer->width != controller->timing.active_width) || (layer->height != controller->timing.active_height))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    bytes_per_pixel = DisplayController_GetBytesPerPixel(layer->pixel_format);

    if (bytes_per_pixel == 0U)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    minimum_stride = (size_t)layer->width * (size_t)bytes_per_pixel;

    if (layer->stride_bytes < minimum_stride)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Pin configuration                                                          */
/* -------------------------------------------------------------------------- */

static DisplayController_Result DisplayController_ConfigurePin(DisplayController_Pin pin)
{
    GPIO_TypeDef *gpio;
    uint32_t pin_number;
    uint32_t alternate_function;
    uint32_t afr_index;
    uint32_t afr_position;
    DisplayController_Result result;

    if (pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    gpio = DisplayController_GetGPIOPort(pin);

    if (gpio == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    result = DisplayController_GetAlternateFunction(pin, &alternate_function);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    if (RCC_EnablePeripheralClock(gpio) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    pin_number = DisplayController_GetGPIOPinNumber(pin);
    afr_index = pin_number / 8U;
    afr_position = (pin_number % 8U) * 4U;

    gpio->MODER &= ~(0x3UL << (pin_number * 2U));
    gpio->MODER |= DISPLAY_CONTROLLER_GPIO_MODE_ALTERNATE << (pin_number * 2U);

    gpio->OTYPER &= ~(1UL << pin_number);

    gpio->OSPEEDR &= ~(0x3UL << (pin_number * 2U));
    gpio->OSPEEDR |= DISPLAY_CONTROLLER_GPIO_SPEED_VERY_HIGH << (pin_number * 2U);

    gpio->PUPDR &= ~(0x3UL << (pin_number * 2U));

    gpio->AFR[afr_index] &= ~(0xFUL << afr_position);
    gpio->AFR[afr_index] |= alternate_function << afr_position;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_Result DisplayController_ConfigurePins(const DisplayController_PinConfiguration *pins)
{
    const DisplayController_Pin *color_groups[3];
    DisplayController_Result result;
    size_t group_index;
    size_t pin_index;

    result = DisplayController_ConfigurePin(pins->horizontal_sync_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ConfigurePin(pins->vertical_sync_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ConfigurePin(pins->data_enable_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ConfigurePin(pins->pixel_clock_pin);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    color_groups[0] = pins->red_pins;
    color_groups[1] = pins->green_pins;
    color_groups[2] = pins->blue_pins;

    for (group_index = 0U; group_index < 3U; group_index++)
    {
        for (pin_index = 0U; pin_index < 8U; pin_index++)
        {
            result = DisplayController_ConfigurePin(color_groups[group_index][pin_index]);

            if (result != DISPLAY_CONTROLLER_RESULT_OK)
            {
                return result;
            }
        }
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Timing and pixel-format helpers                                             */
/* -------------------------------------------------------------------------- */

static uint32_t DisplayController_CalculatePixelClock(const DisplayController_Timing *timing)
{
    uint64_t horizontal_total;
    uint64_t vertical_total;
    uint64_t pixel_clock_hz;

    if ((timing == NULL) || (timing->refresh_rate_millihz == 0U))
    {
        return 0U;
    }

    horizontal_total = (uint64_t)timing->horizontal_sync_width + (uint64_t)timing->horizontal_back_porch + (uint64_t)timing->active_width + (uint64_t)timing->horizontal_front_porch;

    vertical_total = (uint64_t)timing->vertical_sync_height + (uint64_t)timing->vertical_back_porch + (uint64_t)timing->active_height + (uint64_t)timing->vertical_front_porch;

    pixel_clock_hz = horizontal_total * vertical_total * (uint64_t)timing->refresh_rate_millihz / 1000ULL;

    if (pixel_clock_hz > UINT32_MAX)
    {
        return 0U;
    }

    return (uint32_t)pixel_clock_hz;
}

static uint32_t DisplayController_GetPixelFormatEncoding(DisplayController_PixelFormat pixel_format)
{
    switch (pixel_format)
    {
        case DISPLAY_CONTROLLER_PIXEL_FORMAT_ARGB8888: return 0U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB888: return 1U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB565: return 2U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT: return 5U;

        default: return UINT32_MAX;
    }
}

static uint32_t DisplayController_GetBytesPerPixel(DisplayController_PixelFormat pixel_format)
{
    switch (pixel_format)
    {
        case DISPLAY_CONTROLLER_PIXEL_FORMAT_ARGB8888: return 4U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB888: return 3U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB565: return 2U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT: return 1U;

        default: return 0U;
    }
}

/* -------------------------------------------------------------------------- */
/* Register configuration                                                     */
/* -------------------------------------------------------------------------- */

static DisplayController_Result DisplayController_ConfigureGlobalRegisters(const DisplayController_Handle *controller)
{
    uint32_t horizontal_sync_width;
    uint32_t accumulated_horizontal_back_porch;
    uint32_t accumulated_active_width;
    uint32_t total_width;
    uint32_t vertical_sync_height;
    uint32_t accumulated_vertical_back_porch;
    uint32_t accumulated_active_height;
    uint32_t total_height;
    uint32_t global_control;

    horizontal_sync_width = (uint32_t)controller->timing.horizontal_sync_width - 1U;
    accumulated_horizontal_back_porch = (uint32_t)controller->timing.horizontal_sync_width + (uint32_t)controller->timing.horizontal_back_porch - 1U;
    accumulated_active_width = (uint32_t)controller->timing.horizontal_sync_width + (uint32_t)controller->timing.horizontal_back_porch + (uint32_t)controller->timing.active_width - 1U;
    total_width = (uint32_t)controller->timing.horizontal_sync_width + (uint32_t)controller->timing.horizontal_back_porch + (uint32_t)controller->timing.active_width + (uint32_t)controller->timing.horizontal_front_porch - 1U;

    vertical_sync_height = (uint32_t)controller->timing.vertical_sync_height - 1U;
    accumulated_vertical_back_porch = (uint32_t)controller->timing.vertical_sync_height + (uint32_t)controller->timing.vertical_back_porch - 1U;
    accumulated_active_height = (uint32_t)controller->timing.vertical_sync_height + (uint32_t)controller->timing.vertical_back_porch + (uint32_t)controller->timing.active_height - 1U;
    total_height = (uint32_t)controller->timing.vertical_sync_height + (uint32_t)controller->timing.vertical_back_porch + (uint32_t)controller->timing.active_height + (uint32_t)controller->timing.vertical_front_porch - 1U;

    if ((horizontal_sync_width > 0x0FFFU) || (accumulated_horizontal_back_porch > 0x0FFFU) || (accumulated_active_width > 0x0FFFU) || (total_width > 0x0FFFU) || (vertical_sync_height > 0x07FFU) || (accumulated_vertical_back_porch > 0x07FFU) || (accumulated_active_height > 0x07FFU) || (total_height > 0x07FFU))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    LTDC->SSCR = (horizontal_sync_width << LTDC_SSCR_HSW_Pos) | (vertical_sync_height << LTDC_SSCR_VSH_Pos);
    LTDC->BPCR = (accumulated_horizontal_back_porch << LTDC_BPCR_AHBP_Pos) | (accumulated_vertical_back_porch << LTDC_BPCR_AVBP_Pos);
    LTDC->AWCR = (accumulated_active_width << LTDC_AWCR_AAW_Pos) | (accumulated_active_height << LTDC_AWCR_AAH_Pos);
    LTDC->TWCR = (total_width << LTDC_TWCR_TOTALW_Pos) | (total_height << LTDC_TWCR_TOTALH_Pos);

    global_control = 0U;

    if (controller->signals.horizontal_sync == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        global_control |= LTDC_GCR_HSPOL;
    }

    if (controller->signals.vertical_sync == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        global_control |= LTDC_GCR_VSPOL;
    }

    if (controller->signals.data_enable == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        global_control |= LTDC_GCR_DEPOL;
    }

    if (controller->signals.pixel_clock_edge == DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE)
    {
        global_control |= LTDC_GCR_PCPOL;
    }

    LTDC->GCR = global_control;

    LTDC->BCCR = ((uint32_t)controller->background_color.red << LTDC_BCCR_BCRED_Pos) | ((uint32_t)controller->background_color.green << LTDC_BCCR_BCGREEN_Pos) | ((uint32_t)controller->background_color.blue << LTDC_BCCR_BCBLUE_Pos);

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_Result DisplayController_ConfigureLayerRegisters(const DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer)
{
    uint32_t pixel_format;
    uint32_t window_start_x;
    uint32_t window_stop_x;
    uint32_t window_start_y;
    uint32_t window_stop_y;
    uint32_t line_length_bytes;
    uint32_t line_pitch_bytes;

    pixel_format = DisplayController_GetPixelFormatEncoding(layer->pixel_format);

    if (pixel_format == UINT32_MAX)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    window_start_x = (uint32_t)controller->timing.horizontal_sync_width + (uint32_t)controller->timing.horizontal_back_porch;
    window_stop_x = window_start_x + (uint32_t)layer->width - 1U;

    window_start_y = (uint32_t)controller->timing.vertical_sync_height + (uint32_t)controller->timing.vertical_back_porch;
    window_stop_y = window_start_y + (uint32_t)layer->height - 1U;

    line_length_bytes = (uint32_t)layer->width * DisplayController_GetBytesPerPixel(layer->pixel_format);
    line_pitch_bytes = (uint32_t)layer->stride_bytes;

    if ((line_length_bytes > 0x1FFCU) || (line_pitch_bytes > 0x1FFFU))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    LTDC_Layer1->CR = 0U;

    LTDC_Layer1->WHPCR = ((window_stop_x & 0x0FFFU) << LTDC_LxWHPCR_WHSPPOS_Pos) | ((window_start_x & 0x0FFFU) << LTDC_LxWHPCR_WHSTPOS_Pos);

    LTDC_Layer1->WVPCR = ((window_stop_y & 0x07FFU) << LTDC_LxWVPCR_WVSPPOS_Pos) | ((window_start_y & 0x07FFU) << LTDC_LxWVPCR_WVSTPOS_Pos);

    LTDC_Layer1->PFCR = pixel_format;
    LTDC_Layer1->CACR = 0xFFU;
    LTDC_Layer1->DCCR = 0U;

    LTDC_Layer1->BFCR = (DISPLAY_CONTROLLER_BLEND_FACTOR_1_CA << LTDC_LxBFCR_BF1_Pos) | (DISPLAY_CONTROLLER_BLEND_FACTOR_2_CA << LTDC_LxBFCR_BF2_Pos);

    LTDC_Layer1->CFBAR = (uint32_t)(uintptr_t)layer->framebuffer;

    LTDC_Layer1->CFBLR = (((line_length_bytes + 3U) & 0x1FFFU) << LTDC_LxCFBLR_CFBLL_Pos) | ((line_pitch_bytes & 0x1FFFU) << LTDC_LxCFBLR_CFBP_Pos);

    LTDC_Layer1->CFBLNR = (uint32_t)layer->height;

    if (layer->pixel_format == DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT)
    {
        LTDC_Layer1->CR |= LTDC_LxCR_CLUTEN;
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_Result DisplayController_ReloadImmediate(void)
{
    LTDC->SRCR = LTDC_SRCR_IMR;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_Result DisplayController_ReloadVerticalBlanking(DisplayController_State *state)
{
    if(state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Multiple shadow-register updates may be staged during one frame. If a
     * vertical-blank reload is already pending, the existing request will commit
     * all shadow-register writes made before the blanking interval.
     */
    if(state->reload_pending || ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U))
    {
        state->reload_pending = true;
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    state->reload_complete = false;
    state->reload_pending = true;

    __DMB();
    LTDC->SRCR = LTDC_SRCR_VBR;
    __DSB();

    return DISPLAY_CONTROLLER_RESULT_OK;
}

/**
 * @brief Wait for the start of the next vertical-front-porch interval.
 *
 * LTDC palette-entry writes are not shadow-register writes. They take effect
 * as CLUTWR is written, so performing them during active scanout can expose a
 * partially updated palette for one scan line. The LTDC line interrupt marks
 * the beginning of vertical blanking, which provides a safe interval for the
 * short CLUT update sequence.
 *
 * @param state Controller state with an enabled LTDC instance.
 */
static void DisplayController_WaitForVerticalBlank(const DisplayController_State *state)
{
    const uint32_t observed_vertical_blank_count = state->vertical_blank_count;

    while(state->vertical_blank_count == observed_vertical_blank_count)
    {
        __WFI();
    }
}

/**
 * @brief Configure the LTDC line and reload interrupts.
 *
 * The line interrupt is placed on the first line following the active image,
 * which is the beginning of the vertical-front-porch blanking interval.
 */
static void DisplayController_ConfigureInterrupts(const DisplayController_Handle *controller)
{
    uint32_t first_vertical_blank_line;

    first_vertical_blank_line = (uint32_t)controller->timing.vertical_sync_height + (uint32_t)controller->timing.vertical_back_porch + (uint32_t)controller->timing.active_height;

    LTDC->LIPCR = first_vertical_blank_line;

    /*
     * Clear all stale LTDC status before enabling either NVIC vector. Line and
     * reload-complete events use LTDC_IRQn, while FIFO-underrun and transfer
     * errors use the separate LTDC_ER_IRQn vector on STM32H7A3.
     */
    LTDC->ICR = LTDC_ICR_CLIF | LTDC_ICR_CRRIF | LTDC_ICR_CFUIF | LTDC_ICR_CTERRIF;
    LTDC->IER |= LTDC_IER_LIE | LTDC_IER_RRIE | LTDC_IER_FUIE | LTDC_IER_TERRIE;

    NVIC_ClearPendingIRQ(LTDC_IRQn);
    NVIC_SetPriority(LTDC_IRQn, DISPLAY_CONTROLLER_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(LTDC_IRQn);

    NVIC_ClearPendingIRQ(LTDC_ER_IRQn);
    NVIC_SetPriority(LTDC_ER_IRQn, DISPLAY_CONTROLLER_ERROR_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(LTDC_ER_IRQn);
}

/**
 * @brief Disable LTDC frame-related interrupts.
 */
static void DisplayController_DisableInterrupts(void)
{
    NVIC_DisableIRQ(LTDC_IRQn);
    NVIC_DisableIRQ(LTDC_ER_IRQn);

    NVIC_ClearPendingIRQ(LTDC_IRQn);
    NVIC_ClearPendingIRQ(LTDC_ER_IRQn);

    LTDC->IER &= ~(LTDC_IER_LIE | LTDC_IER_RRIE | LTDC_IER_FUIE | LTDC_IER_TERRIE);
    LTDC->ICR = LTDC_ICR_CLIF | LTDC_ICR_CRRIF | LTDC_ICR_CFUIF | LTDC_ICR_CTERRIF;

    __DSB();
    __ISB();
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

DisplayController_Result DisplayController_Init(DisplayController_Handle *controller)
{
    DisplayController_State *state;
    DisplayController_Result result;

    result = DisplayController_ValidateConfiguration(controller);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    if (DisplayController_FindState(controller) != NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    state = DisplayController_AllocateState(controller);

    if (state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_BUSY;
    }

    if (RCC_EnablePeripheralClock(LTDC) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    if (RCC_ResetPeripheral(LTDC) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    result = DisplayController_ConfigurePins(&controller->pins);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ConfigureGlobalRegisters(controller);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    LTDC_Layer1->CR &= ~LTDC_LxCR_LEN;
    LTDC->GCR &= ~LTDC_GCR_LTDCEN;

    result = DisplayController_ReloadImmediate();

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    state->vertical_blank_count = 0U;
    state->reload_pending = false;
    state->reload_complete = false;

    DisplayController_FifoUnderrunCount = 0U;
    DisplayController_TransferErrorCount = 0U;

    state->initialized = true;
    state->layer_configured = false;
    state->enabled = false;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

DisplayController_Result DisplayController_ConfigureLayer(DisplayController_Handle *controller, const DisplayController_LayerConfiguration *layer)
{
    DisplayController_State *state;
    DisplayController_Result result;

    if ((controller == NULL) || (layer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    state = DisplayController_FindState(controller);

    if (state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if (state->enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_BUSY;
    }

    result = DisplayController_ValidateLayer(controller, layer);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    result = DisplayController_ConfigureLayerRegisters(controller, layer);

    if (result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    state->layer = *layer;
    state->layer_configured = true;

    return DisplayController_ReloadImmediate();
}

DisplayController_Result DisplayController_SetFramebuffer(DisplayController_Handle *controller, void *framebuffer)
{
    DisplayController_State *state;
    DisplayController_Result result;

    if((controller == NULL) || (framebuffer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    state = DisplayController_FindState(controller);

    if(state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(!state->layer_configured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    LTDC_Layer1->CFBAR = (uint32_t)(uintptr_t)framebuffer;

    if(state->enabled)
    {
        result = DisplayController_ReloadVerticalBlanking(state);
    }
    else
    {
        result = DisplayController_ReloadImmediate();
    }

    if(result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return result;
    }

    state->layer.framebuffer = framebuffer;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

DisplayController_Result DisplayController_SetPalette(DisplayController_Handle *controller, const uint32_t *palette, size_t count)
{
    DisplayController_State *state;
    size_t index;
    uint32_t color;

    if ((controller == NULL) || (palette == NULL) || (count == 0U) || (count > DISPLAY_CONTROLLER_MAX_PALETTE_ENTRIES))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    state = DisplayController_FindState(controller);

    if (state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if (!state->layer_configured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if (state->layer.pixel_format != DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    /*
     * CLUTWR updates are immediate, unlike the framebuffer-address shadow
     * register.  Wait for vertical blank before disabling and reprogramming
     * the CLUT so a scanline can never see a partially updated palette.
     */
    if(state->enabled)
    {
        DisplayController_WaitForVerticalBlank(state);
    }

    LTDC_Layer1->CR &= ~LTDC_LxCR_CLUTEN;

    for (index = 0U; index < count; index++)
    {
        color = palette[index] & 0x00FFFFFFUL;

        LTDC_Layer1->CLUTWR = ((uint32_t)index << DISPLAY_CONTROLLER_CLUT_INDEX_POSITION) | (((color >> 16U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_RED_POSITION) | (((color >> 8U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_GREEN_POSITION) | (((color >> 0U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_BLUE_POSITION);
    }

    LTDC_Layer1->CR |= LTDC_LxCR_CLUTEN;

    if (state->enabled)
    {
        return DisplayController_ReloadVerticalBlanking(state);
    }

    return DisplayController_ReloadImmediate();
}

DisplayController_Result DisplayController_Enable(DisplayController_Handle *controller)
{
    DisplayController_State *state;

    if (controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    state = DisplayController_FindState(controller);

    if (state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if (state->enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    if (!state->layer_configured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    state->vertical_blank_count = 0U;
    state->reload_pending = false;
    state->reload_complete = false;

    DisplayController_ConfigureInterrupts(controller);

    LTDC_Layer1->CR |= LTDC_LxCR_LEN;
    LTDC->GCR |= LTDC_GCR_LTDCEN;

    state->enabled = true;

    return DisplayController_ReloadImmediate();
}

DisplayController_Result DisplayController_Disable(DisplayController_Handle *controller)
{
    DisplayController_State *state;

    if (controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    state = DisplayController_FindState(controller);

    if (state == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if (!state->enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    DisplayController_DisableInterrupts();

    LTDC_Layer1->CR &= ~LTDC_LxCR_LEN;
    LTDC->GCR &= ~LTDC_GCR_LTDCEN;

    state->reload_pending = false;
    state->reload_complete = false;
    state->enabled = false;

    return DisplayController_ReloadImmediate();
}

uint32_t DisplayController_GetVerticalBlankCount(const DisplayController_Handle *controller)
{
    const DisplayController_State *state = DisplayController_FindState(controller);

    if(state == NULL)
    {
        return 0U;
    }

    return state->vertical_blank_count;
}

bool DisplayController_IsReloadPending(const DisplayController_Handle *controller)
{
    const DisplayController_State *state = DisplayController_FindState(controller);

    if(state == NULL)
    {
        return false;
    }

    return state->reload_pending;
}

bool DisplayController_ConsumeReloadComplete(DisplayController_Handle *controller)
{
    DisplayController_State *state = DisplayController_FindState(controller);
    bool reload_complete;

    if(state == NULL)
    {
        return false;
    }

    reload_complete = state->reload_complete;
    state->reload_complete = false;

    return reload_complete;
}

/**
 * @brief Handle LTDC line and shadow-register reload interrupts.
 *
 * This function is called by LTDC_IRQHandler() in the target interrupt-vector
 * file. It intentionally contains all LTDC interrupt-register handling so the
 * vector file remains a thin forwarding layer.
 */
void DisplayController_IRQHandler(void)
{
    uint32_t interrupt_status;
    size_t index;

    interrupt_status = LTDC->ISR;

    if((interrupt_status & LTDC_ISR_FUIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CFUIF;
        DisplayController_FifoUnderrunCount++;
    }

    if((interrupt_status & LTDC_ISR_TERRIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CTERRIF;
        DisplayController_TransferErrorCount++;
    }

    if((interrupt_status & LTDC_ISR_LIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CLIF;

        for(index = 0U; index < DISPLAY_CONTROLLER_REGISTRY_SIZE; index++)
        {
            DisplayController_State *state = &DisplayController_Registry[index];

            if(state->initialized && state->enabled)
            {
                state->vertical_blank_count++;
            }
        }
    }

    if((interrupt_status & LTDC_ISR_RRIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CRRIF;

        for(index = 0U; index < DISPLAY_CONTROLLER_REGISTRY_SIZE; index++)
        {
            DisplayController_State *state = &DisplayController_Registry[index];

            if(state->initialized && state->enabled)
            {
                state->reload_pending = false;
                state->reload_complete = true;
            }
        }
    }

    /*
     * Ensure interrupt-flag clears reach LTDC before exception return. This
     * prevents immediate retriggering when this handler is entered through
     * either LTDC_IRQn or LTDC_ER_IRQn.
     */
    __DSB();
}

void DisplayController_WaitForEvent(DisplayController_Handle *Controller)
{
    (void)Controller;
    __WFI();
}