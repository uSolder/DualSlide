/**
 * @file display_controller.c
 * @brief STM32H7A3 raster display-controller implementation.
 *
 * This driver implements the hardware-independent display-controller contract
 * using the STM32H7A3 LTDC peripheral and LTDC layer 1.
 */

#include "display_controller.h"

#include "stm32h7a3_defs.h"
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
    DisplayController_HandleTypeDef *Handle;
    DisplayController_LayerConfigurationTypeDef Layer;
    volatile uint32_t VerticalBlankCount;
    volatile bool ReloadPending;
    volatile bool ReloadComplete;

    bool Initialized;
    bool LayerConfigured;
    bool Enabled;
} DisplayController_StateTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static DisplayController_StateTypeDef DisplayController_Registry[DISPLAY_CONTROLLER_REGISTRY_SIZE];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static DisplayController_StateTypeDef *DisplayController_FindState(const DisplayController_HandleTypeDef *Controller);
static DisplayController_StateTypeDef *DisplayController_AllocateState(DisplayController_HandleTypeDef *Controller);
static DisplayController_ResultTypeDef DisplayController_ValidateConfiguration(const DisplayController_HandleTypeDef *Controller);
static DisplayController_ResultTypeDef DisplayController_ValidateLayer(const DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer);
static DisplayController_ResultTypeDef DisplayController_ValidatePins(const DisplayController_PinConfigurationTypeDef *Pins);
static DisplayController_ResultTypeDef DisplayController_ValidateRequiredPin(DisplayController_PinTypeDef Pin);
static DisplayController_ResultTypeDef DisplayController_ValidateColorPins(const DisplayController_PinTypeDef Pins[8]);
static GPIO_TypeDef *DisplayController_GetGPIOPort(DisplayController_PinTypeDef Pin);
static uint32_t DisplayController_GetGPIOPinNumber(DisplayController_PinTypeDef Pin);
static DisplayController_ResultTypeDef DisplayController_GetAlternateFunction(DisplayController_PinTypeDef Pin, uint32_t *AlternateFunction);
static DisplayController_ResultTypeDef DisplayController_ConfigurePin(DisplayController_PinTypeDef Pin);
static DisplayController_ResultTypeDef DisplayController_ConfigurePins(const DisplayController_PinConfigurationTypeDef *Pins);
static DisplayController_ResultTypeDef DisplayController_ConfigureGlobalRegisters(const DisplayController_HandleTypeDef *Controller);
static DisplayController_ResultTypeDef DisplayController_ConfigureLayerRegisters(const DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer);
static uint32_t DisplayController_CalculatePixelClock(const DisplayController_TimingTypeDef *Timing);
static uint32_t DisplayController_GetPixelFormatEncoding(DisplayController_PixelFormatTypeDef PixelFormat);
static uint32_t DisplayController_GetBytesPerPixel(DisplayController_PixelFormatTypeDef PixelFormat);
static DisplayController_ResultTypeDef DisplayController_ReloadImmediate(void);
static DisplayController_ResultTypeDef DisplayController_ReloadVerticalBlanking(DisplayController_StateTypeDef *State);
static void DisplayController_WaitForVerticalBlank(const DisplayController_StateTypeDef *State);
static void DisplayController_ConfigureInterrupts(const DisplayController_HandleTypeDef *Controller);
static void DisplayController_DisableInterrupts(void);

/* -------------------------------------------------------------------------- */
/* State registry                                                             */
/* -------------------------------------------------------------------------- */

static DisplayController_StateTypeDef *DisplayController_FindState(const DisplayController_HandleTypeDef *Controller)
{
    size_t Index;

    if(Controller == NULL)
    {
        return NULL;
    }

    for(Index = 0U; Index < DISPLAY_CONTROLLER_REGISTRY_SIZE; Index++)
    {
        if(DisplayController_Registry[Index].Initialized && (DisplayController_Registry[Index].Handle == Controller))
        {
            return &DisplayController_Registry[Index];
        }
    }

    return NULL;
}

static DisplayController_StateTypeDef *DisplayController_AllocateState(DisplayController_HandleTypeDef *Controller)
{
    size_t Index;

    for(Index = 0U; Index < DISPLAY_CONTROLLER_REGISTRY_SIZE; Index++)
    {
        if(!DisplayController_Registry[Index].Initialized)
        {
            DisplayController_Registry[Index].Handle = Controller;
            DisplayController_Registry[Index].VerticalBlankCount = 0U;
            DisplayController_Registry[Index].ReloadPending = false;
            DisplayController_Registry[Index].ReloadComplete = false;
            DisplayController_Registry[Index].LayerConfigured = false;
            DisplayController_Registry[Index].Enabled = false;

            return &DisplayController_Registry[Index];
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
/* GPIO helpers                                                               */
/* -------------------------------------------------------------------------- */

static GPIO_TypeDef *DisplayController_GetGPIOPort(DisplayController_PinTypeDef Pin)
{
    uint32_t PortIndex;

    if(Pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return NULL;
    }

    PortIndex = ((uint32_t)Pin >> 4U) & 0x0FU;

    switch(PortIndex)
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

static uint32_t DisplayController_GetGPIOPinNumber(DisplayController_PinTypeDef Pin)
{
    return (uint32_t)Pin & 0x0FU;
}

/* -------------------------------------------------------------------------- */
/* Alternate-function mapping                                                 */
/* -------------------------------------------------------------------------- */

static DisplayController_ResultTypeDef DisplayController_GetAlternateFunction(DisplayController_PinTypeDef Pin, uint32_t *AlternateFunction)
{
    if(AlternateFunction == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    switch(Pin)
    {
        case PC0: case PC1: case PA5: case PA6: case PA7: case PC4: case PC5: case PB10: case PB14: case PB15: case PC6: case PC7: case PC10: case PC11: case PD2: case PB8: case PB9:
            *AlternateFunction = 14U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PA8:
            *AlternateFunction = 13U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PB5:
            *AlternateFunction = 11U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PC9:
            *AlternateFunction = 10U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        case PB0: case PB1:
            *AlternateFunction = 9U;
            return DISPLAY_CONTROLLER_RESULT_OK;

        default: return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                 */
/* -------------------------------------------------------------------------- */

static DisplayController_ResultTypeDef DisplayController_ValidateRequiredPin(DisplayController_PinTypeDef Pin)
{
    uint32_t AlternateFunction;

    if(Pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if(DisplayController_GetGPIOPort(Pin) == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    return DisplayController_GetAlternateFunction(Pin, &AlternateFunction);
}

static DisplayController_ResultTypeDef DisplayController_ValidateColorPins(const DisplayController_PinTypeDef Pins[8])
{
    size_t Index;
    uint32_t AlternateFunction;

    for(Index = 0U; Index < 8U; Index++)
    {
        if(Pins[Index] == DISPLAY_CONTROLLER_PIN_UNUSED)
        {
            continue;
        }

        if(DisplayController_GetGPIOPort(Pins[Index]) == NULL)
        {
            return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
        }

        if(DisplayController_GetAlternateFunction(Pins[Index], &AlternateFunction) != DISPLAY_CONTROLLER_RESULT_OK)
        {
            return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
        }
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_ResultTypeDef DisplayController_ValidatePins(const DisplayController_PinConfigurationTypeDef *Pins)
{
    DisplayController_ResultTypeDef Result;

    if(Pins == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    Result = DisplayController_ValidateRequiredPin(Pins->HorizontalSyncPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ValidateRequiredPin(Pins->VerticalSyncPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ValidateRequiredPin(Pins->DataEnablePin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ValidateRequiredPin(Pins->PixelClockPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ValidateColorPins(Pins->RedPins);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ValidateColorPins(Pins->GreenPins);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    return DisplayController_ValidateColorPins(Pins->BluePins);
}

static DisplayController_ResultTypeDef DisplayController_ValidateConfiguration(const DisplayController_HandleTypeDef *Controller)
{
    uint32_t RequestedPixelClockHz;
    uint32_t ActualPixelClockHz;
    uint32_t DifferenceHz;
    uint32_t ToleranceHz;

    if(Controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Timing.ActiveWidth == 0U) || (Controller->Timing.ActiveHeight == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Timing.HorizontalSyncWidth == 0U) || (Controller->Timing.VerticalSyncHeight == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if(Controller->Timing.RefreshRateMilliHz == 0U)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Signals.HorizontalSync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (Controller->Signals.HorizontalSync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Signals.VerticalSync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (Controller->Signals.VerticalSync != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Signals.DataEnable != DISPLAY_CONTROLLER_POLARITY_ACTIVE_LOW) && (Controller->Signals.DataEnable != DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Controller->Signals.PixelClockEdge != DISPLAY_CONTROLLER_PIXEL_CLOCK_RISING_EDGE) && (Controller->Signals.PixelClockEdge != DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    RequestedPixelClockHz = DisplayController_CalculatePixelClock(&Controller->Timing);
    ActualPixelClockHz = RCC_GetKernelFrequency(LTDC);

    if((RequestedPixelClockHz == 0U) || (ActualPixelClockHz == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    DifferenceHz = (RequestedPixelClockHz > ActualPixelClockHz)
        ? RequestedPixelClockHz - ActualPixelClockHz
        : ActualPixelClockHz - RequestedPixelClockHz;

    ToleranceHz = (RequestedPixelClockHz / 100U) * DISPLAY_CONTROLLER_PIXEL_CLOCK_TOLERANCE_PCT;

    if(DifferenceHz > ToleranceHz)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    return DisplayController_ValidatePins(&Controller->Pins);
}

static DisplayController_ResultTypeDef DisplayController_ValidateLayer(const DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer)
{
    uint32_t BytesPerPixel;
    size_t MinimumStride;

    if((Controller == NULL) || (Layer == NULL) || (Layer->Framebuffer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Layer->Width == 0U) || (Layer->Height == 0U))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    if((Layer->Width != Controller->Timing.ActiveWidth) || (Layer->Height != Controller->Timing.ActiveHeight))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    BytesPerPixel = DisplayController_GetBytesPerPixel(Layer->PixelFormat);

    if(BytesPerPixel == 0U)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    MinimumStride = (size_t)Layer->Width * (size_t)BytesPerPixel;

    if(Layer->StrideBytes < MinimumStride)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Pin configuration                                                          */
/* -------------------------------------------------------------------------- */

static DisplayController_ResultTypeDef DisplayController_ConfigurePin(DisplayController_PinTypeDef Pin)
{
    GPIO_TypeDef *GPIO;
    uint32_t PinNumber;
    uint32_t AlternateFunction;
    uint32_t AFRIndex;
    uint32_t AFRPosition;
    DisplayController_ResultTypeDef Result;

    if(Pin == DISPLAY_CONTROLLER_PIN_UNUSED)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    GPIO = DisplayController_GetGPIOPort(Pin);

    if(GPIO == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    Result = DisplayController_GetAlternateFunction(Pin, &AlternateFunction);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    if(RCC_EnablePeripheralClock(GPIO) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    PinNumber = DisplayController_GetGPIOPinNumber(Pin);
    AFRIndex = PinNumber / 8U;
    AFRPosition = (PinNumber % 8U) * 4U;

    GPIO->MODER &= ~(0x3UL << (PinNumber * 2U));
    GPIO->MODER |= DISPLAY_CONTROLLER_GPIO_MODE_ALTERNATE << (PinNumber * 2U);

    GPIO->OTYPER &= ~(1UL << PinNumber);

    GPIO->OSPEEDR &= ~(0x3UL << (PinNumber * 2U));
    GPIO->OSPEEDR |= DISPLAY_CONTROLLER_GPIO_SPEED_VERY_HIGH << (PinNumber * 2U);

    GPIO->PUPDR &= ~(0x3UL << (PinNumber * 2U));

    GPIO->AFR[AFRIndex] &= ~(0xFUL << AFRPosition);
    GPIO->AFR[AFRIndex] |= AlternateFunction << AFRPosition;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_ResultTypeDef DisplayController_ConfigurePins(const DisplayController_PinConfigurationTypeDef *Pins)
{
    const DisplayController_PinTypeDef *ColourGroups[3];
    DisplayController_ResultTypeDef Result;
    size_t GroupIndex;
    size_t PinIndex;

    Result = DisplayController_ConfigurePin(Pins->HorizontalSyncPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ConfigurePin(Pins->VerticalSyncPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ConfigurePin(Pins->DataEnablePin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ConfigurePin(Pins->PixelClockPin);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    ColourGroups[0] = Pins->RedPins;
    ColourGroups[1] = Pins->GreenPins;
    ColourGroups[2] = Pins->BluePins;

    for(GroupIndex = 0U; GroupIndex < 3U; GroupIndex++)
    {
        for(PinIndex = 0U; PinIndex < 8U; PinIndex++)
        {
            Result = DisplayController_ConfigurePin(ColourGroups[GroupIndex][PinIndex]);

            if(Result != DISPLAY_CONTROLLER_RESULT_OK)
            {
                return Result;
            }
        }
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Timing and pixel-format helpers                                             */
/* -------------------------------------------------------------------------- */

static uint32_t DisplayController_CalculatePixelClock(const DisplayController_TimingTypeDef *Timing)
{
    uint64_t HorizontalTotal;
    uint64_t VerticalTotal;
    uint64_t PixelClockHz;

    if((Timing == NULL) || (Timing->RefreshRateMilliHz == 0U))
    {
        return 0U;
    }

    HorizontalTotal = (uint64_t)Timing->HorizontalSyncWidth + (uint64_t)Timing->HorizontalBackPorch + (uint64_t)Timing->ActiveWidth + (uint64_t)Timing->HorizontalFrontPorch;

    VerticalTotal = (uint64_t)Timing->VerticalSyncHeight + (uint64_t)Timing->VerticalBackPorch + (uint64_t)Timing->ActiveHeight + (uint64_t)Timing->VerticalFrontPorch;

    PixelClockHz = HorizontalTotal * VerticalTotal * (uint64_t)Timing->RefreshRateMilliHz / 1000ULL;

    if(PixelClockHz > UINT32_MAX)
    {
        return 0U;
    }

    return (uint32_t)PixelClockHz;
}

static uint32_t DisplayController_GetPixelFormatEncoding(DisplayController_PixelFormatTypeDef PixelFormat)
{
    switch(PixelFormat)
    {
        case DISPLAY_CONTROLLER_PIXEL_FORMAT_ARGB8888: return 0U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB888: return 1U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_RGB565: return 2U;

        case DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT: return 5U;

        default: return UINT32_MAX;
    }
}

static uint32_t DisplayController_GetBytesPerPixel(DisplayController_PixelFormatTypeDef PixelFormat)
{
    switch(PixelFormat)
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

static DisplayController_ResultTypeDef DisplayController_ConfigureGlobalRegisters(const DisplayController_HandleTypeDef *Controller)
{
    uint32_t HorizontalSyncWidth;
    uint32_t AccumulatedHorizontalBackPorch;
    uint32_t AccumulatedActiveWidth;
    uint32_t TotalWidth;
    uint32_t VerticalSyncHeight;
    uint32_t AccumulatedVerticalBackPorch;
    uint32_t AccumulatedActiveHeight;
    uint32_t TotalHeight;
    uint32_t GlobalControl;

    HorizontalSyncWidth = (uint32_t)Controller->Timing.HorizontalSyncWidth - 1U;
    AccumulatedHorizontalBackPorch = (uint32_t)Controller->Timing.HorizontalSyncWidth + (uint32_t)Controller->Timing.HorizontalBackPorch - 1U;
    AccumulatedActiveWidth = (uint32_t)Controller->Timing.HorizontalSyncWidth + (uint32_t)Controller->Timing.HorizontalBackPorch + (uint32_t)Controller->Timing.ActiveWidth - 1U;
    TotalWidth = (uint32_t)Controller->Timing.HorizontalSyncWidth + (uint32_t)Controller->Timing.HorizontalBackPorch + (uint32_t)Controller->Timing.ActiveWidth + (uint32_t)Controller->Timing.HorizontalFrontPorch - 1U;

    VerticalSyncHeight = (uint32_t)Controller->Timing.VerticalSyncHeight - 1U;
    AccumulatedVerticalBackPorch = (uint32_t)Controller->Timing.VerticalSyncHeight + (uint32_t)Controller->Timing.VerticalBackPorch - 1U;
    AccumulatedActiveHeight = (uint32_t)Controller->Timing.VerticalSyncHeight + (uint32_t)Controller->Timing.VerticalBackPorch + (uint32_t)Controller->Timing.ActiveHeight - 1U;
    TotalHeight = (uint32_t)Controller->Timing.VerticalSyncHeight + (uint32_t)Controller->Timing.VerticalBackPorch + (uint32_t)Controller->Timing.ActiveHeight + (uint32_t)Controller->Timing.VerticalFrontPorch - 1U;

    if((HorizontalSyncWidth > 0x0FFFU) || (AccumulatedHorizontalBackPorch > 0x0FFFU) || (AccumulatedActiveWidth > 0x0FFFU) || (TotalWidth > 0x0FFFU) || (VerticalSyncHeight > 0x07FFU) || (AccumulatedVerticalBackPorch > 0x07FFU) || (AccumulatedActiveHeight > 0x07FFU) || (TotalHeight > 0x07FFU))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    LTDC->SSCR = (HorizontalSyncWidth << LTDC_SSCR_HSW_Pos) | (VerticalSyncHeight << LTDC_SSCR_VSH_Pos);
    LTDC->BPCR = (AccumulatedHorizontalBackPorch << LTDC_BPCR_AHBP_Pos) | (AccumulatedVerticalBackPorch << LTDC_BPCR_AVBP_Pos);
    LTDC->AWCR = (AccumulatedActiveWidth << LTDC_AWCR_AAW_Pos) | (AccumulatedActiveHeight << LTDC_AWCR_AAH_Pos);
    LTDC->TWCR = (TotalWidth << LTDC_TWCR_TOTALW_Pos) | (TotalHeight << LTDC_TWCR_TOTALH_Pos);

    GlobalControl = 0U;

    if(Controller->Signals.HorizontalSync == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        GlobalControl |= LTDC_GCR_HSPOL;
    }

    if(Controller->Signals.VerticalSync == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        GlobalControl |= LTDC_GCR_VSPOL;
    }

    if(Controller->Signals.DataEnable == DISPLAY_CONTROLLER_POLARITY_ACTIVE_HIGH)
    {
        GlobalControl |= LTDC_GCR_DEPOL;
    }

    if(Controller->Signals.PixelClockEdge == DISPLAY_CONTROLLER_PIXEL_CLOCK_FALLING_EDGE)
    {
        GlobalControl |= LTDC_GCR_PCPOL;
    }

    LTDC->GCR = GlobalControl;

    LTDC->BCCR = ((uint32_t)Controller->BackgroundColour.Red << LTDC_BCCR_BCRED_Pos) | ((uint32_t)Controller->BackgroundColour.Green << LTDC_BCCR_BCGREEN_Pos) | ((uint32_t)Controller->BackgroundColour.Blue << LTDC_BCCR_BCBLUE_Pos);

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_ResultTypeDef DisplayController_ConfigureLayerRegisters(const DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer)
{
    uint32_t PixelFormat;
    uint32_t WindowStartX;
    uint32_t WindowStopX;
    uint32_t WindowStartY;
    uint32_t WindowStopY;
    uint32_t LineLengthBytes;
    uint32_t LinePitchBytes;

    PixelFormat = DisplayController_GetPixelFormatEncoding(Layer->PixelFormat);

    if(PixelFormat == UINT32_MAX)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    WindowStartX = (uint32_t)Controller->Timing.HorizontalSyncWidth + (uint32_t)Controller->Timing.HorizontalBackPorch;
    WindowStopX = WindowStartX + (uint32_t)Layer->Width - 1U;

    WindowStartY = (uint32_t)Controller->Timing.VerticalSyncHeight + (uint32_t)Controller->Timing.VerticalBackPorch;
    WindowStopY = WindowStartY + (uint32_t)Layer->Height - 1U;

    LineLengthBytes = (uint32_t)Layer->Width * DisplayController_GetBytesPerPixel(Layer->PixelFormat);
    LinePitchBytes = (uint32_t)Layer->StrideBytes;

    if((LineLengthBytes > 0x1FFCU) || (LinePitchBytes > 0x1FFFU))
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    LTDC_Layer1->CR = 0U;

    LTDC_Layer1->WHPCR = ((WindowStopX & 0x0FFFU) << LTDC_LxWHPCR_WHSPPOS_Pos) | ((WindowStartX & 0x0FFFU) << LTDC_LxWHPCR_WHSTPOS_Pos);

    LTDC_Layer1->WVPCR = ((WindowStopY & 0x07FFU) << LTDC_LxWVPCR_WVSPPOS_Pos) | ((WindowStartY & 0x07FFU) << LTDC_LxWVPCR_WVSTPOS_Pos);

    LTDC_Layer1->PFCR = PixelFormat;
    LTDC_Layer1->CACR = 0xFFU;
    LTDC_Layer1->DCCR = 0U;

    LTDC_Layer1->BFCR = (DISPLAY_CONTROLLER_BLEND_FACTOR_1_CA << LTDC_LxBFCR_BF1_Pos) | (DISPLAY_CONTROLLER_BLEND_FACTOR_2_CA << LTDC_LxBFCR_BF2_Pos);

    LTDC_Layer1->CFBAR = (uint32_t)(uintptr_t)Layer->Framebuffer;

    LTDC_Layer1->CFBLR = (((LineLengthBytes + 3U) & 0x1FFFU) << LTDC_LxCFBLR_CFBLL_Pos) | ((LinePitchBytes & 0x1FFFU) << LTDC_LxCFBLR_CFBP_Pos);

    LTDC_Layer1->CFBLNR = (uint32_t)Layer->Height;

    if(Layer->PixelFormat == DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT)
    {
        LTDC_Layer1->CR |= LTDC_LxCR_CLUTEN;
    }

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_ResultTypeDef DisplayController_ReloadImmediate(void)
{
    LTDC->SRCR = LTDC_SRCR_IMR;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

static DisplayController_ResultTypeDef DisplayController_ReloadVerticalBlanking(DisplayController_StateTypeDef *State)
{
    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    /*
     * Multiple shadow-register updates may be staged during one frame. If a
     * vertical-blank reload is already pending, the existing request will commit
     * all shadow-register writes made before the blanking interval.
     */
    if(State->ReloadPending || ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U))
    {
        State->ReloadPending = true;
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    State->ReloadComplete = false;
    State->ReloadPending = true;

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
 * @param State Controller state with an enabled LTDC instance.
 */
static void DisplayController_WaitForVerticalBlank(const DisplayController_StateTypeDef *State)
{
    const uint32_t ObservedVerticalBlankCount = State->VerticalBlankCount;

    while(State->VerticalBlankCount == ObservedVerticalBlankCount)
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
static void DisplayController_ConfigureInterrupts(const DisplayController_HandleTypeDef *Controller)
{
    uint32_t FirstVerticalBlankLine;

    FirstVerticalBlankLine = (uint32_t)Controller->Timing.VerticalSyncHeight + (uint32_t)Controller->Timing.VerticalBackPorch + (uint32_t)Controller->Timing.ActiveHeight;

    LTDC->LIPCR = FirstVerticalBlankLine;

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

DisplayController_ResultTypeDef DisplayController_Init(DisplayController_HandleTypeDef *Controller)
{
    DisplayController_StateTypeDef *State;
    DisplayController_ResultTypeDef Result;

    Result = DisplayController_ValidateConfiguration(Controller);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    if(DisplayController_FindState(Controller) != NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    State = DisplayController_AllocateState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_BUSY;
    }

    if(RCC_EnablePeripheralClock(LTDC) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    if(RCC_ResetPeripheral(LTDC) != RCC_RESULT_OK)
    {
        return DISPLAY_CONTROLLER_RESULT_IO_ERROR;
    }

    Result = DisplayController_ConfigurePins(&Controller->Pins);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ConfigureGlobalRegisters(Controller);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    LTDC_Layer1->CR &= ~LTDC_LxCR_LEN;
    LTDC->GCR &= ~LTDC_GCR_LTDCEN;

    Result = DisplayController_ReloadImmediate();

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    State->VerticalBlankCount = 0U;
    State->ReloadPending = false;
    State->ReloadComplete = false;

    State->Initialized = true;
    State->LayerConfigured = false;
    State->Enabled = false;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

DisplayController_ResultTypeDef DisplayController_ConfigureLayer(DisplayController_HandleTypeDef *Controller, const DisplayController_LayerConfigurationTypeDef *Layer)
{
    DisplayController_StateTypeDef *State;
    DisplayController_ResultTypeDef Result;

    if((Controller == NULL) || (Layer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(State->Enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_BUSY;
    }

    Result = DisplayController_ValidateLayer(Controller, Layer);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    Result = DisplayController_ConfigureLayerRegisters(Controller, Layer);

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    State->Layer = *Layer;
    State->LayerConfigured = true;

    return DisplayController_ReloadImmediate();
}

DisplayController_ResultTypeDef DisplayController_SetFramebuffer(DisplayController_HandleTypeDef *Controller, void *Framebuffer)
{
    DisplayController_StateTypeDef *State;
    DisplayController_ResultTypeDef Result;

    if((Controller == NULL) || (Framebuffer == NULL))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(!State->LayerConfigured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    LTDC_Layer1->CFBAR = (uint32_t)(uintptr_t)Framebuffer;

    if(State->Enabled)
    {
        Result = DisplayController_ReloadVerticalBlanking(State);
    }
    else
    {
        Result = DisplayController_ReloadImmediate();
    }

    if(Result != DISPLAY_CONTROLLER_RESULT_OK)
    {
        return Result;
    }

    State->Layer.Framebuffer = Framebuffer;

    return DISPLAY_CONTROLLER_RESULT_OK;
}

DisplayController_ResultTypeDef DisplayController_SetPalette(DisplayController_HandleTypeDef *Controller, const uint32_t *Palette, size_t Count)
{
    DisplayController_StateTypeDef *State;
    size_t Index;
    uint32_t Colour;

    if((Controller == NULL) || (Palette == NULL) || (Count == 0U) || (Count > DISPLAY_CONTROLLER_MAX_PALETTE_ENTRIES))
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(!State->LayerConfigured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(State->Layer.PixelFormat != DISPLAY_CONTROLLER_PIXEL_FORMAT_INDEXED_8_BIT)
    {
        return DISPLAY_CONTROLLER_RESULT_UNSUPPORTED;
    }

    /*
     * CLUTWR updates are immediate, unlike the framebuffer-address shadow
     * register.  Wait for vertical blank before disabling and reprogramming
     * the CLUT so a scanline can never see a partially updated palette.
     */
    if(State->Enabled)
    {
        DisplayController_WaitForVerticalBlank(State);
    }

    LTDC_Layer1->CR &= ~LTDC_LxCR_CLUTEN;

    for(Index = 0U; Index < Count; Index++)
    {
        Colour = Palette[Index] & 0x00FFFFFFUL;

        LTDC_Layer1->CLUTWR = ((uint32_t)Index << DISPLAY_CONTROLLER_CLUT_INDEX_POSITION) | (((Colour >> 16U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_RED_POSITION) | (((Colour >> 8U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_GREEN_POSITION) | (((Colour >> 0U) & 0xFFU) << DISPLAY_CONTROLLER_CLUT_BLUE_POSITION);
    }

    LTDC_Layer1->CR |= LTDC_LxCR_CLUTEN;

    if(State->Enabled)
    {
        return DisplayController_ReloadVerticalBlanking(State);
    }

    return DisplayController_ReloadImmediate();
}

DisplayController_ResultTypeDef DisplayController_Enable(DisplayController_HandleTypeDef *Controller)
{
    DisplayController_StateTypeDef *State;

    if(Controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(State->Enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    if(!State->LayerConfigured)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    State->VerticalBlankCount = 0U;
    State->ReloadPending = false;
    State->ReloadComplete = false;

    DisplayController_ConfigureInterrupts(Controller);

    LTDC_Layer1->CR |= LTDC_LxCR_LEN;
    LTDC->GCR |= LTDC_GCR_LTDCEN;

    State->Enabled = true;

    return DisplayController_ReloadImmediate();
}

DisplayController_ResultTypeDef DisplayController_Disable(DisplayController_HandleTypeDef *Controller)
{
    DisplayController_StateTypeDef *State;

    if(Controller == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_INVALID_ARGUMENT;
    }

    State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return DISPLAY_CONTROLLER_RESULT_NOT_INITIALIZED;
    }

    if(!State->Enabled)
    {
        return DISPLAY_CONTROLLER_RESULT_OK;
    }

    DisplayController_DisableInterrupts();

    LTDC_Layer1->CR &= ~LTDC_LxCR_LEN;
    LTDC->GCR &= ~LTDC_GCR_LTDCEN;

    State->ReloadPending = false;
    State->ReloadComplete = false;
    State->Enabled = false;

    return DisplayController_ReloadImmediate();
}

uint32_t DisplayController_GetVerticalBlankCount(const DisplayController_HandleTypeDef *Controller)
{
    const DisplayController_StateTypeDef *State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return 0U;
    }

    return State->VerticalBlankCount;
}

bool DisplayController_IsReloadPending(const DisplayController_HandleTypeDef *Controller)
{
    const DisplayController_StateTypeDef *State = DisplayController_FindState(Controller);

    if(State == NULL)
    {
        return false;
    }

    return State->ReloadPending;
}

bool DisplayController_ConsumeReloadComplete(DisplayController_HandleTypeDef *Controller)
{
    DisplayController_StateTypeDef *State = DisplayController_FindState(Controller);
    bool ReloadComplete;

    if(State == NULL)
    {
        return false;
    }

    ReloadComplete = State->ReloadComplete;
    State->ReloadComplete = false;

    return ReloadComplete;
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
    uint32_t InterruptStatus;
    size_t Index;

    InterruptStatus = LTDC->ISR;

    if((InterruptStatus & LTDC_ISR_FUIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CFUIF;
    }

    if((InterruptStatus & LTDC_ISR_TERRIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CTERRIF;
    }

    if((InterruptStatus & LTDC_ISR_LIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CLIF;

        for(Index = 0U; Index < DISPLAY_CONTROLLER_REGISTRY_SIZE; Index++)
        {
            DisplayController_StateTypeDef *State = &DisplayController_Registry[Index];

            if(State->Initialized && State->Enabled)
            {
                State->VerticalBlankCount++;
            }
        }
    }

    if((InterruptStatus & LTDC_ISR_RRIF) != 0U)
    {
        LTDC->ICR = LTDC_ICR_CRRIF;

        for(Index = 0U; Index < DISPLAY_CONTROLLER_REGISTRY_SIZE; Index++)
        {
            DisplayController_StateTypeDef *State = &DisplayController_Registry[Index];

            if(State->Initialized && State->Enabled)
            {
                State->ReloadPending = false;
                State->ReloadComplete = true;
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

void DisplayController_WaitForEvent(DisplayController_HandleTypeDef *Controller)
{
    (void)Controller;
    __WFI();
}
