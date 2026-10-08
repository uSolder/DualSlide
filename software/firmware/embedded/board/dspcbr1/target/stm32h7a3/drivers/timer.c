/**
 * @file timer.c
 * @brief STM32H7A3 timer and PWM implementation.
 */

#include "timer.h"

#include "rcc.h"
#include "stm32h7a3_defs.h"
#include "stm32h7a3xxq.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define TIMER_HANDLE_COUNT                14U
#define TIMER_MAX_PRESCALER               0xFFFFU
#define TIMER_MAX_16_BIT_PERIOD           0xFFFFUL
#define TIMER_MAX_32_BIT_PERIOD           0xFFFFFFFFUL

#define TIMER_GPIO_MODE_ALTERNATE         0x02UL
#define TIMER_GPIO_FIELD_MASK             0x03UL
#define TIMER_GPIO_ALTERNATE_FIELD_MASK   0x0FUL

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    Timer_OutputIdentifierTypeDef Output;
    Timer_PinTypeDef Pin;
    uint8_t AlternateFunction;
} Timer_PinMappingTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const Timer_PinMappingTypeDef Timer_PinMappings[] =
{
    {TIM1_CH1, PA8, 1U}, {TIM1_CH2, PA9, 1U}, {TIM1_CH3, PA10, 1U}, {TIM1_CH4, PA11, 1U},
    {TIM2_CH1, PA0, 1U}, {TIM2_CH1, PA5, 1U}, {TIM2_CH1, PA15, 1U},
    {TIM2_CH2, PB3, 1U}, {TIM2_CH3, PA2, 1U}, {TIM2_CH3, PB10, 1U}, {TIM2_CH4, PA3, 1U},
    {TIM3_CH1, PA6, 2U}, {TIM3_CH1, PB4, 2U}, {TIM3_CH1, PC6, 2U},
    {TIM3_CH2, PA7, 2U}, {TIM3_CH2, PB5, 2U}, {TIM3_CH2, PC7, 2U},
    {TIM3_CH3, PB0, 2U}, {TIM3_CH4, PB1, 2U}, {TIM3_CH4, PC9, 2U},
    {TIM4_CH1, PB6, 2U}, {TIM4_CH2, PB7, 2U}, {TIM4_CH3, PB8, 2U}, {TIM4_CH4, PB9, 2U},
    {TIM5_CH1, PA0, 2U}, {TIM5_CH2, PA1, 2U}, {TIM5_CH3, PA2, 2U}, {TIM5_CH4, PA3, 2U},
    {TIM8_CH1, PC6, 3U}, {TIM8_CH2, PC7, 3U}, {TIM8_CH4, PC9, 3U},
    {TIM12_CH1, PB14, 2U}, {TIM12_CH2, PB15, 2U},
    {TIM13_CH1, PA6, 9U}, {TIM14_CH1, PA7, 9U},
    {TIM15_CH1, PA2, 3U}, {TIM15_CH1, PC12, 2U}, {TIM15_CH2, PA3, 3U},
    {TIM16_CH1, PB8, 1U}, {TIM17_CH1, PB9, 1U}
};

static Timer_HandleTypeDef *Timer_Handles[TIMER_HANDLE_COUNT];

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static TIM_TypeDef *Timer_GetInstance(Timer_IdentifierTypeDef Timer);
static uint32_t Timer_GetHandleIndex(Timer_IdentifierTypeDef Timer);
static IRQn_Type Timer_GetIRQNumber(Timer_IdentifierTypeDef Timer);
static bool Timer_Is32Bit(TIM_TypeDef *Timer);
static bool Timer_IsAdvanced(TIM_TypeDef *Timer);
static uint8_t Timer_GetChannelNumber(Timer_OutputIdentifierTypeDef Output);
static Timer_IdentifierTypeDef Timer_GetOutputTimer(Timer_OutputIdentifierTypeDef Output);
static volatile uint32_t *Timer_GetCompareRegister(TIM_TypeDef *Timer, uint8_t Channel);
static volatile uint32_t *Timer_GetCaptureCompareModeRegister(TIM_TypeDef *Timer, uint8_t Channel);
static uint32_t Timer_GetCaptureCompareModeShift(uint8_t Channel);
static uint32_t Timer_GetCaptureCompareEnableShift(uint8_t Channel);
static Timer_ResultTypeDef Timer_ComputePeriod(TIM_TypeDef *Timer, uint32_t ClockHz, uint32_t FrequencyHz, uint16_t *Prescaler, uint32_t *Period);
static const Timer_PinMappingTypeDef *Timer_GetPinMapping(Timer_OutputIdentifierTypeDef Output, Timer_PinTypeDef Pin);
static GPIO_TypeDef *Timer_GetGPIOPort(Timer_PinTypeDef Pin);
static Timer_ResultTypeDef Timer_ConfigurePWMPin(const Timer_PWMChannelTypeDef *Channel);
static Timer_ResultTypeDef Timer_ValidateHandle(const Timer_HandleTypeDef *Timer);
static Timer_ResultTypeDef Timer_ValidateChannel(const Timer_PWMChannelTypeDef *Channel);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static TIM_TypeDef *Timer_GetInstance(Timer_IdentifierTypeDef Timer)
{
    switch(Timer_GetOutputTimer(Timer))
    {
        case 1U: return TIM1;
        case 2U: return TIM2;
        case 3U: return TIM3;
        case 4U: return TIM4;
        case 5U: return TIM5;
        case 6U: return TIM6;
        case 7U: return TIM7;
        case 8U: return TIM8;
        case 12U: return TIM12;
        case 13U: return TIM13;
        case 14U: return TIM14;
        case 15U: return TIM15;
        case 16U: return TIM16;
        case 17U: return TIM17;
        default: return NULL;
    }
}

static uint32_t Timer_GetHandleIndex(Timer_IdentifierTypeDef Timer)
{
    switch(Timer_GetOutputTimer(Timer))
    {
        case 1U: return 0U;
        case 2U: return 1U;
        case 3U: return 2U;
        case 4U: return 3U;
        case 5U: return 4U;
        case 6U: return 5U;
        case 7U: return 6U;
        case 8U: return 7U;
        case 12U: return 8U;
        case 13U: return 9U;
        case 14U: return 10U;
        case 15U: return 11U;
        case 16U: return 12U;
        case 17U: return 13U;
        default: return TIMER_HANDLE_COUNT;
    }
}

static IRQn_Type Timer_GetIRQNumber(Timer_IdentifierTypeDef Timer)
{
    switch(Timer_GetOutputTimer(Timer))
    {
        case 1U: return TIM1_UP_IRQn;
        case 2U: return TIM2_IRQn;
        case 3U: return TIM3_IRQn;
        case 4U: return TIM4_IRQn;
        case 5U: return TIM5_IRQn;
        case 6U: return TIM6_DAC_IRQn;
        case 7U: return TIM7_IRQn;
        case 8U: return TIM8_UP_TIM13_IRQn;
        case 12U: return TIM8_BRK_TIM12_IRQn;
        case 13U: return TIM8_UP_TIM13_IRQn;
        case 14U: return TIM8_TRG_COM_TIM14_IRQn;
        case 15U: return TIM15_IRQn;
        case 16U: return TIM16_IRQn;
        case 17U: return TIM17_IRQn;
        default: return NonMaskableInt_IRQn;
    }
}

static bool Timer_Is32Bit(TIM_TypeDef *Timer)
{
    return (Timer == TIM2) || (Timer == TIM5);
}

static bool Timer_IsAdvanced(TIM_TypeDef *Timer)
{
    return (Timer == TIM1) || (Timer == TIM8);
}

static uint8_t Timer_GetChannelNumber(Timer_OutputIdentifierTypeDef Output)
{
    return (uint8_t)(Output & 0x000FU);
}

static Timer_IdentifierTypeDef Timer_GetOutputTimer(Timer_OutputIdentifierTypeDef Output)
{
    return (Timer_IdentifierTypeDef)(Output >> 4U);
}

static volatile uint32_t *Timer_GetCompareRegister(TIM_TypeDef *Timer, uint8_t Channel)
{
    switch(Channel)
    {
        case 1U: return &Timer->CCR1;
        case 2U: return &Timer->CCR2;
        case 3U: return &Timer->CCR3;
        case 4U: return &Timer->CCR4;
        default: return NULL;
    }
}

static volatile uint32_t *Timer_GetCaptureCompareModeRegister(TIM_TypeDef *Timer, uint8_t Channel)
{
    return ((Channel == 1U) || (Channel == 2U)) ? &Timer->CCMR1 : &Timer->CCMR2;
}

static uint32_t Timer_GetCaptureCompareModeShift(uint8_t Channel)
{
    return ((Channel == 1U) || (Channel == 3U)) ? 0U : 8U;
}

static uint32_t Timer_GetCaptureCompareEnableShift(uint8_t Channel)
{
    return ((uint32_t)Channel - 1U) * 4U;
}

static Timer_ResultTypeDef Timer_ComputePeriod(TIM_TypeDef *Timer, uint32_t ClockHz, uint32_t FrequencyHz, uint16_t *Prescaler, uint32_t *Period)
{
    const uint32_t MaximumPeriod = Timer_Is32Bit(Timer) ? TIMER_MAX_32_BIT_PERIOD : TIMER_MAX_16_BIT_PERIOD;

    if((Timer == NULL) || (ClockHz == 0U) || (FrequencyHz == 0U) || (Prescaler == NULL) || (Period == NULL))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    for(uint32_t Value = 0U; Value <= TIMER_MAX_PRESCALER; Value++)
    {
        const uint64_t Divider = ((uint64_t)Value + 1ULL) * FrequencyHz;
        const uint32_t Ticks = (uint32_t)((uint64_t)ClockHz / Divider);

        if((Ticks != 0U) && ((Ticks - 1U) <= MaximumPeriod))
        {
            *Prescaler = (uint16_t)Value;
            *Period = Ticks - 1U;

            return TIMER_RESULT_OK;
        }
    }

    return TIMER_RESULT_UNSUPPORTED;
}

static const Timer_PinMappingTypeDef *Timer_GetPinMapping(Timer_OutputIdentifierTypeDef Output, Timer_PinTypeDef Pin)
{
    for(uint32_t Index = 0U; Index < (sizeof(Timer_PinMappings) / sizeof(Timer_PinMappings[0])); Index++)
    {
        if((Timer_PinMappings[Index].Output == Output) && (Timer_PinMappings[Index].Pin == Pin))
        {
            return &Timer_PinMappings[Index];
        }
    }

    return NULL;
}

static GPIO_TypeDef *Timer_GetGPIOPort(Timer_PinTypeDef Pin)
{
    switch(Pin >> 4U)
    {
        case 0U: return GPIOA;
        case 1U: return GPIOB;
        case 2U: return GPIOC;
        case 3U: return GPIOD;
        default: return NULL;
    }
}

static Timer_ResultTypeDef Timer_ConfigurePWMPin(const Timer_PWMChannelTypeDef *Channel)
{
    const Timer_PinMappingTypeDef *Mapping;
    GPIO_TypeDef *Port;
    uint32_t PinNumber;
    uint32_t Position;
    uint32_t AlternateRegister;
    uint32_t AlternatePosition;

    Mapping = Timer_GetPinMapping(Channel->Output, Channel->Pin);

    if(Mapping == NULL)
    {
        return TIMER_RESULT_UNSUPPORTED;
    }

    Port = Timer_GetGPIOPort(Channel->Pin);

    if(Port == NULL)
    {
        return TIMER_RESULT_UNSUPPORTED;
    }

    PinNumber = (uint32_t)Channel->Pin & 0x0FU;
    Position = PinNumber * 2U;
    AlternateRegister = PinNumber / 8U;
    AlternatePosition = (PinNumber % 8U) * 4U;

    if(RCC_EnablePeripheralClock(Port) != RCC_RESULT_OK)
    {
        return TIMER_RESULT_HARDWARE_ERROR;
    }

    Port->MODER &= ~(TIMER_GPIO_FIELD_MASK << Position);
    Port->MODER |= TIMER_GPIO_MODE_ALTERNATE << Position;
    Port->OTYPER &= ~(1UL << PinNumber);
    Port->OSPEEDR &= ~(TIMER_GPIO_FIELD_MASK << Position);
    Port->PUPDR &= ~(TIMER_GPIO_FIELD_MASK << Position);
    Port->AFR[AlternateRegister] &= ~(TIMER_GPIO_ALTERNATE_FIELD_MASK << AlternatePosition);
    Port->AFR[AlternateRegister] |= (uint32_t)Mapping->AlternateFunction << AlternatePosition;

    return TIMER_RESULT_OK;
}

static Timer_ResultTypeDef Timer_ValidateHandle(const Timer_HandleTypeDef *Timer)
{
    if((Timer == NULL) || (Timer_GetInstance(Timer->Timer) == NULL) || (Timer->FrequencyHz == 0U))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    return TIMER_RESULT_OK;
}

static Timer_ResultTypeDef Timer_ValidateChannel(const Timer_PWMChannelTypeDef *Channel)
{
    const uint8_t ChannelNumber = (Channel == NULL) ? 0U : Timer_GetChannelNumber(Channel->Output);

    if((Channel == NULL) || (Channel->Timer == NULL) || !Channel->Timer->Initialized ||
       (Channel->Pin == TIMER_PIN_UNUSED) ||
       (Timer_GetOutputTimer(Channel->Output) != Timer_GetOutputTimer(Channel->Timer->Timer)) ||
       (ChannelNumber < 1U) || (ChannelNumber > 4U) ||
       (Channel->Polarity > TIMER_PWM_POLARITY_ACTIVE_LOW))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    return TIMER_RESULT_OK;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

Timer_ResultTypeDef Timer_Init(Timer_HandleTypeDef *Timer)
{
    TIM_TypeDef *Instance;
    uint32_t HandleIndex;
    uint32_t ClockHz;
    uint16_t Prescaler;
    uint32_t Period;
    Timer_ResultTypeDef Result;

    Result = Timer_ValidateHandle(Timer);

    if(Result != TIMER_RESULT_OK)
    {
        return Result;
    }

    HandleIndex = Timer_GetHandleIndex(Timer->Timer);

    if(Timer->Initialized || (Timer_Handles[HandleIndex] != NULL))
    {
        return TIMER_RESULT_BUSY;
    }

    Instance = Timer_GetInstance(Timer->Timer);
    ClockHz = RCC_GetKernelFrequency(Instance);
    Result = Timer_ComputePeriod(Instance, ClockHz, Timer->FrequencyHz, &Prescaler, &Period);

    if(Result != TIMER_RESULT_OK)
    {
        return Result;
    }

    if(RCC_EnablePeripheralClock(Instance) != RCC_RESULT_OK)
    {
        return TIMER_RESULT_HARDWARE_ERROR;
    }

    Instance->CR1 &= ~TIM_CR1_CEN;
    Instance->PSC = Prescaler;
    Instance->ARR = Period;
    Instance->CNT = 0U;
    Instance->CR1 = TIM_CR1_ARPE;
    Instance->DIER = 0U;
    Instance->SR = 0U;
    Instance->EGR = TIM_EGR_UG;
    Instance->SR = 0U;

    if(Timer->UpdateCallback != NULL)
    {
        Instance->DIER |= TIM_DIER_UIE;
        NVIC_ClearPendingIRQ(Timer_GetIRQNumber(Timer->Timer));
        NVIC_EnableIRQ(Timer_GetIRQNumber(Timer->Timer));
    }

    Timer_Handles[HandleIndex] = Timer;
    Timer->Initialized = true;
    Timer->Running = false;

    return TIMER_RESULT_OK;
}

Timer_ResultTypeDef Timer_PWMChannelInit(Timer_PWMChannelTypeDef *Channel)
{
    TIM_TypeDef *Timer;
    volatile uint32_t *CaptureCompareModeRegister;
    uint32_t ModeShift;
    uint32_t EnableShift;
    uint32_t ModeMask;
    uint8_t ChannelNumber;
    Timer_ResultTypeDef Result = Timer_ValidateChannel(Channel);

    if(Result != TIMER_RESULT_OK)
    {
        return Result;
    }

    if(Channel->Initialized)
    {
        return TIMER_RESULT_BUSY;
    }

    Result = Timer_ConfigurePWMPin(Channel);

    if(Result != TIMER_RESULT_OK)
    {
        return Result;
    }

    Timer = Timer_GetInstance(Channel->Timer->Timer);
    ChannelNumber = Timer_GetChannelNumber(Channel->Output);
    CaptureCompareModeRegister = Timer_GetCaptureCompareModeRegister(Timer, ChannelNumber);
    ModeShift = Timer_GetCaptureCompareModeShift(ChannelNumber);
    EnableShift = Timer_GetCaptureCompareEnableShift(ChannelNumber);
    ModeMask = (TIM_CCMR1_CC1S | TIM_CCMR1_OC1FE | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M) << ModeShift;

    *CaptureCompareModeRegister &= ~ModeMask;
    *CaptureCompareModeRegister |= (TIM_CCMR1_OC1PE | (6UL << TIM_CCMR1_OC1M_Pos)) << ModeShift;
    Timer->CCER &= ~((TIM_CCER_CC1E | TIM_CCER_CC1P) << EnableShift);

    if(Channel->Polarity == TIMER_PWM_POLARITY_ACTIVE_LOW)
    {
        Timer->CCER |= TIM_CCER_CC1P << EnableShift;
    }

    if(Timer_IsAdvanced(Timer))
    {
        Timer->BDTR |= TIM_BDTR_MOE;
    }

    Channel->Initialized = true;
    Channel->OutputEnabled = false;

    return Timer_SetPWMDutyPermille(Channel, Channel->DutyPermille);
}

Timer_ResultTypeDef Timer_Start(Timer_HandleTypeDef *Timer)
{
    if((Timer == NULL) || !Timer->Initialized)
    {
        return TIMER_RESULT_NOT_INITIALIZED;
    }

    Timer_GetInstance(Timer->Timer)->CR1 |= TIM_CR1_CEN;
    Timer->Running = true;

    return TIMER_RESULT_OK;
}

Timer_ResultTypeDef Timer_Stop(Timer_HandleTypeDef *Timer)
{
    if((Timer == NULL) || !Timer->Initialized)
    {
        return TIMER_RESULT_NOT_INITIALIZED;
    }

    Timer_GetInstance(Timer->Timer)->CR1 &= ~TIM_CR1_CEN;
    Timer->Running = false;

    return TIMER_RESULT_OK;
}

Timer_ResultTypeDef Timer_OutputEnable(Timer_PWMChannelTypeDef *Channel)
{
    uint8_t ChannelNumber;
    Timer_ResultTypeDef Result = Timer_ValidateChannel(Channel);

    if((Result != TIMER_RESULT_OK) || !Channel->Initialized)
    {
        return (Result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : Result;
    }

    ChannelNumber = Timer_GetChannelNumber(Channel->Output);
    Timer_GetInstance(Channel->Timer->Timer)->CCER |= TIM_CCER_CC1E << Timer_GetCaptureCompareEnableShift(ChannelNumber);
    Channel->OutputEnabled = true;

    return TIMER_RESULT_OK;
}

Timer_ResultTypeDef Timer_OutputDisable(Timer_PWMChannelTypeDef *Channel)
{
    uint8_t ChannelNumber;
    Timer_ResultTypeDef Result = Timer_ValidateChannel(Channel);

    if((Result != TIMER_RESULT_OK) || !Channel->Initialized)
    {
        return (Result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : Result;
    }

    ChannelNumber = Timer_GetChannelNumber(Channel->Output);
    Timer_GetInstance(Channel->Timer->Timer)->CCER &= ~(TIM_CCER_CC1E << Timer_GetCaptureCompareEnableShift(ChannelNumber));
    Channel->OutputEnabled = false;

    return TIMER_RESULT_OK;
}

Timer_ResultTypeDef Timer_SetPWMDutyPermille(Timer_PWMChannelTypeDef *Channel, uint16_t DutyPermille)
{
    TIM_TypeDef *Timer;
    volatile uint32_t *CompareRegister;
    uint8_t ChannelNumber;
    Timer_ResultTypeDef Result = Timer_ValidateChannel(Channel);

    if((Result != TIMER_RESULT_OK) || !Channel->Initialized)
    {
        return (Result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : Result;
    }

    if(DutyPermille > 1000U)
    {
        DutyPermille = 1000U;
    }

    Timer = Timer_GetInstance(Channel->Timer->Timer);
    ChannelNumber = Timer_GetChannelNumber(Channel->Output);
    CompareRegister = Timer_GetCompareRegister(Timer, ChannelNumber);
    *CompareRegister = (uint32_t)(((uint64_t)(Timer->ARR + 1U) * DutyPermille) / 1000U);
    Channel->DutyPermille = DutyPermille;

    return TIMER_RESULT_OK;
}

void Timer_IRQHandler(Timer_IdentifierTypeDef TimerIdentifier)
{
    uint32_t HandleIndex = Timer_GetHandleIndex(TimerIdentifier);
    Timer_HandleTypeDef *Timer;
    TIM_TypeDef *Instance;

    if(HandleIndex >= TIMER_HANDLE_COUNT)
    {
        return;
    }

    Timer = Timer_Handles[HandleIndex];

    if(Timer == NULL)
    {
        return;
    }

    Instance = Timer_GetInstance(TimerIdentifier);

    if((Instance->SR & TIM_SR_UIF) == 0U)
    {
        return;
    }

    Instance->SR &= ~TIM_SR_UIF;

    if(Timer->UpdateCallback != NULL)
    {
        Timer->UpdateCallback(Timer->CallbackContext);
    }
}
