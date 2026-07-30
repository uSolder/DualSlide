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

#define TIMER_HANDLE_COUNT                14U
#define TIMER_MAX_PRESCALER               0xFFFFU
#define TIMER_MAX_16_BIT_PERIOD           0xFFFFUL
#define TIMER_MAX_32_BIT_PERIOD           0xFFFFFFFFUL

#define TIMER_GPIO_MODE_ALTERNATE         0x02UL
#define TIMER_GPIO_FIELD_MASK             0x03UL
#define TIMER_GPIO_ALTERNATE_FIELD_MASK   0x0FUL
typedef struct
{
    Timer_OutputIdentifier output;
    Timer_Pin pin;
    uint8_t alternate_function;
} Timer_PinMappingTypeDef;

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

static Timer_Handle *Timer_Handles[TIMER_HANDLE_COUNT];

static TIM_TypeDef *Timer_GetInstance(Timer_Identifier timer);
static uint32_t Timer_GetHandleIndex(Timer_Identifier timer);
static IRQn_Type Timer_GetIRQNumber(Timer_Identifier timer);
static bool Timer_Is32Bit(TIM_TypeDef *timer);
static bool Timer_IsAdvanced(TIM_TypeDef *timer);
static uint8_t Timer_GetChannelNumber(Timer_OutputIdentifier output);
static Timer_Identifier Timer_GetOutputTimer(Timer_OutputIdentifier output);
static volatile uint32_t *Timer_GetCompareRegister(TIM_TypeDef *timer, uint8_t channel);
static volatile uint32_t *Timer_GetCaptureCompareModeRegister(TIM_TypeDef *timer, uint8_t channel);
static uint32_t Timer_GetCaptureCompareModeShift(uint8_t channel);
static uint32_t Timer_GetCaptureCompareEnableShift(uint8_t channel);
static Timer_Result Timer_ComputePeriod(TIM_TypeDef *timer, uint32_t clock_hz, uint32_t frequency_hz, uint16_t *prescaler, uint32_t *period);
static const Timer_PinMappingTypeDef *Timer_GetPinMapping(Timer_OutputIdentifier output, Timer_Pin pin);
static GPIO_TypeDef *Timer_GetGPIOPort(Timer_Pin pin);
static Timer_Result Timer_ConfigurePWMPin(const Timer_PWMChannel_Handle *channel);
static Timer_Result Timer_ValidateHandle(const Timer_Handle *timer);
static Timer_Result Timer_ValidateChannel(const Timer_PWMChannel_Handle *channel);

static TIM_TypeDef *Timer_GetInstance(Timer_Identifier timer)
{
    switch(Timer_GetOutputTimer(timer))
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

static uint32_t Timer_GetHandleIndex(Timer_Identifier timer)
{
    switch(Timer_GetOutputTimer(timer))
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

static IRQn_Type Timer_GetIRQNumber(Timer_Identifier timer)
{
    switch(Timer_GetOutputTimer(timer))
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

static bool Timer_Is32Bit(TIM_TypeDef *timer)
{
    return (timer == TIM2) || (timer == TIM5);
}

static bool Timer_IsAdvanced(TIM_TypeDef *timer)
{
    return (timer == TIM1) || (timer == TIM8);
}

static uint8_t Timer_GetChannelNumber(Timer_OutputIdentifier output)
{
    return (uint8_t)(output & 0x000FU);
}

static Timer_Identifier Timer_GetOutputTimer(Timer_OutputIdentifier output)
{
    return (Timer_Identifier)(output >> 4U);
}

static volatile uint32_t *Timer_GetCompareRegister(TIM_TypeDef *timer, uint8_t channel)
{
    switch(channel)
    {
        case 1U: return &timer->CCR1;
        case 2U: return &timer->CCR2;
        case 3U: return &timer->CCR3;
        case 4U: return &timer->CCR4;
        default: return NULL;
    }
}

static volatile uint32_t *Timer_GetCaptureCompareModeRegister(TIM_TypeDef *timer, uint8_t channel)
{
    return ((channel == 1U) || (channel == 2U)) ? &timer->CCMR1 : &timer->CCMR2;
}

static uint32_t Timer_GetCaptureCompareModeShift(uint8_t channel)
{
    return ((channel == 1U) || (channel == 3U)) ? 0U : 8U;
}

static uint32_t Timer_GetCaptureCompareEnableShift(uint8_t channel)
{
    return ((uint32_t)channel - 1U) * 4U;
}

static Timer_Result Timer_ComputePeriod(TIM_TypeDef *timer, uint32_t clock_hz, uint32_t frequency_hz, uint16_t *prescaler, uint32_t *period)
{
    const uint32_t maximum_period = Timer_Is32Bit(timer) ? TIMER_MAX_32_BIT_PERIOD : TIMER_MAX_16_BIT_PERIOD;

    if((timer == NULL) || (clock_hz == 0U) || (frequency_hz == 0U) || (prescaler == NULL) || (period == NULL))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    for(uint32_t value = 0U; value <= TIMER_MAX_PRESCALER; value++)
    {
        const uint64_t divider = ((uint64_t)value + 1ULL) * frequency_hz;
        const uint32_t ticks = (uint32_t)((uint64_t)clock_hz / divider);

        if((ticks != 0U) && ((ticks - 1U) <= maximum_period))
        {
            *prescaler = (uint16_t)value;
            *period = ticks - 1U;

            return TIMER_RESULT_OK;
        }
    }

    return TIMER_RESULT_UNSUPPORTED;
}

static const Timer_PinMappingTypeDef *Timer_GetPinMapping(Timer_OutputIdentifier output, Timer_Pin pin)
{
    for(uint32_t index = 0U; index < (sizeof(Timer_PinMappings) / sizeof(Timer_PinMappings[0])); index++)
    {
        if((Timer_PinMappings[index].output == output) && (Timer_PinMappings[index].pin == pin))
        {
            return &Timer_PinMappings[index];
        }
    }

    return NULL;
}

static GPIO_TypeDef *Timer_GetGPIOPort(Timer_Pin pin)
{
    switch(pin >> 4U)
    {
        case 0U: return GPIOA;
        case 1U: return GPIOB;
        case 2U: return GPIOC;
        case 3U: return GPIOD;
        default: return NULL;
    }
}

static Timer_Result Timer_ConfigurePWMPin(const Timer_PWMChannel_Handle *channel)
{
    const Timer_PinMappingTypeDef *mapping;
    GPIO_TypeDef *port;
    uint32_t pin_number;
    uint32_t position;
    uint32_t alternate_register;
    uint32_t alternate_position;

    mapping = Timer_GetPinMapping(channel->output, channel->pin);

    if(mapping == NULL)
    {
        return TIMER_RESULT_UNSUPPORTED;
    }

    port = Timer_GetGPIOPort(channel->pin);

    if(port == NULL)
    {
        return TIMER_RESULT_UNSUPPORTED;
    }

    pin_number = (uint32_t)channel->pin & 0x0FU;
    position = pin_number * 2U;
    alternate_register = pin_number / 8U;
    alternate_position = (pin_number % 8U) * 4U;

    if(RCC_EnablePeripheralClock(port) != RCC_RESULT_OK)
    {
        return TIMER_RESULT_HARDWARE_ERROR;
    }

    port->MODER &= ~(TIMER_GPIO_FIELD_MASK << position);
    port->MODER |= TIMER_GPIO_MODE_ALTERNATE << position;
    port->OTYPER &= ~(1UL << pin_number);
    port->OSPEEDR &= ~(TIMER_GPIO_FIELD_MASK << position);
    port->PUPDR &= ~(TIMER_GPIO_FIELD_MASK << position);
    port->AFR[alternate_register] &= ~(TIMER_GPIO_ALTERNATE_FIELD_MASK << alternate_position);
    port->AFR[alternate_register] |= (uint32_t)mapping->alternate_function << alternate_position;

    return TIMER_RESULT_OK;
}

static Timer_Result Timer_ValidateHandle(const Timer_Handle *timer)
{
    if((timer == NULL) || (Timer_GetInstance(timer->timer) == NULL) || (timer->frequency_hz == 0U))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    return TIMER_RESULT_OK;
}

static Timer_Result Timer_ValidateChannel(const Timer_PWMChannel_Handle *channel)
{
    const uint8_t channel_number = (channel == NULL) ? 0U : Timer_GetChannelNumber(channel->output);

    if((channel == NULL) || (channel->timer == NULL) || !channel->timer->initialized ||
       (channel->pin == TIMER_PIN_UNUSED) ||
       (Timer_GetOutputTimer(channel->output) != Timer_GetOutputTimer(channel->timer->timer)) ||
       (channel_number < 1U) || (channel_number > 4U) ||
       (channel->polarity > TIMER_PWM_POLARITY_ACTIVE_LOW))
    {
        return TIMER_RESULT_INVALID_ARGUMENT;
    }

    return TIMER_RESULT_OK;
}

Timer_Result Timer_Init(Timer_Handle *timer)
{
    TIM_TypeDef *instance;
    uint32_t handle_index;
    uint32_t clock_hz;
    uint16_t prescaler;
    uint32_t period;
    Timer_Result result;

    result = Timer_ValidateHandle(timer);

    if(result != TIMER_RESULT_OK)
    {
        return result;
    }

    handle_index = Timer_GetHandleIndex(timer->timer);

    if(timer->initialized || (Timer_Handles[handle_index] != NULL))
    {
        return TIMER_RESULT_BUSY;
    }

    instance = Timer_GetInstance(timer->timer);
    clock_hz = RCC_GetKernelFrequency(instance);
    result = Timer_ComputePeriod(instance, clock_hz, timer->frequency_hz, &prescaler, &period);

    if(result != TIMER_RESULT_OK)
    {
        return result;
    }

    if(RCC_EnablePeripheralClock(instance) != RCC_RESULT_OK)
    {
        return TIMER_RESULT_HARDWARE_ERROR;
    }

    instance->CR1 &= ~TIM_CR1_CEN;
    instance->PSC = prescaler;
    instance->ARR = period;
    instance->CNT = 0U;
    instance->CR1 = TIM_CR1_ARPE;
    instance->DIER = 0U;
    instance->SR = 0U;
    instance->EGR = TIM_EGR_UG;
    instance->SR = 0U;

    if(timer->update_callback != NULL)
    {
        instance->DIER |= TIM_DIER_UIE;
        NVIC_ClearPendingIRQ(Timer_GetIRQNumber(timer->timer));
        NVIC_EnableIRQ(Timer_GetIRQNumber(timer->timer));
    }

    Timer_Handles[handle_index] = timer;
    timer->initialized = true;
    timer->running = false;

    return TIMER_RESULT_OK;
}

Timer_Result Timer_PWMChannelInit(Timer_PWMChannel_Handle *channel)
{
    TIM_TypeDef *timer;
    volatile uint32_t *capture_compare_mode_register;
    uint32_t mode_shift;
    uint32_t enable_shift;
    uint32_t mode_mask;
    uint8_t channel_number;
    Timer_Result result = Timer_ValidateChannel(channel);

    if(result != TIMER_RESULT_OK)
    {
        return result;
    }

    if(channel->initialized)
    {
        return TIMER_RESULT_BUSY;
    }

    result = Timer_ConfigurePWMPin(channel);

    if(result != TIMER_RESULT_OK)
    {
        return result;
    }

    timer = Timer_GetInstance(channel->timer->timer);
    channel_number = Timer_GetChannelNumber(channel->output);
    capture_compare_mode_register = Timer_GetCaptureCompareModeRegister(timer, channel_number);
    mode_shift = Timer_GetCaptureCompareModeShift(channel_number);
    enable_shift = Timer_GetCaptureCompareEnableShift(channel_number);
    mode_mask = (TIM_CCMR1_CC1S | TIM_CCMR1_OC1FE | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M) << mode_shift;

    *capture_compare_mode_register &= ~mode_mask;
    *capture_compare_mode_register |= (TIM_CCMR1_OC1PE | (6UL << TIM_CCMR1_OC1M_Pos)) << mode_shift;
    timer->CCER &= ~((TIM_CCER_CC1E | TIM_CCER_CC1P) << enable_shift);

    if(channel->polarity == TIMER_PWM_POLARITY_ACTIVE_LOW)
    {
        timer->CCER |= TIM_CCER_CC1P << enable_shift;
    }

    if(Timer_IsAdvanced(timer))
    {
        timer->BDTR |= TIM_BDTR_MOE;
    }

    channel->initialized = true;
    channel->output_enabled = false;

    return Timer_SetPWMDutyPermille(channel, channel->duty_permille);
}

Timer_Result Timer_Start(Timer_Handle *timer)
{
    if((timer == NULL) || !timer->initialized)
    {
        return TIMER_RESULT_NOT_INITIALIZED;
    }

    Timer_GetInstance(timer->timer)->CR1 |= TIM_CR1_CEN;
    timer->running = true;

    return TIMER_RESULT_OK;
}

Timer_Result Timer_Stop(Timer_Handle *timer)
{
    if((timer == NULL) || !timer->initialized)
    {
        return TIMER_RESULT_NOT_INITIALIZED;
    }

    Timer_GetInstance(timer->timer)->CR1 &= ~TIM_CR1_CEN;
    timer->running = false;

    return TIMER_RESULT_OK;
}

Timer_Result Timer_OutputEnable(Timer_PWMChannel_Handle *channel)
{
    uint8_t channel_number;
    Timer_Result result = Timer_ValidateChannel(channel);

    if((result != TIMER_RESULT_OK) || !channel->initialized)
    {
        return (result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : result;
    }

    channel_number = Timer_GetChannelNumber(channel->output);
    Timer_GetInstance(channel->timer->timer)->CCER |= TIM_CCER_CC1E << Timer_GetCaptureCompareEnableShift(channel_number);
    channel->output_enabled = true;

    return TIMER_RESULT_OK;
}

Timer_Result Timer_OutputDisable(Timer_PWMChannel_Handle *channel)
{
    uint8_t channel_number;
    Timer_Result result = Timer_ValidateChannel(channel);

    if((result != TIMER_RESULT_OK) || !channel->initialized)
    {
        return (result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : result;
    }

    channel_number = Timer_GetChannelNumber(channel->output);
    Timer_GetInstance(channel->timer->timer)->CCER &= ~(TIM_CCER_CC1E << Timer_GetCaptureCompareEnableShift(channel_number));
    channel->output_enabled = false;

    return TIMER_RESULT_OK;
}

Timer_Result Timer_SetPWMDutyPermille(Timer_PWMChannel_Handle *channel, uint16_t duty_permille)
{
    TIM_TypeDef *timer;
    volatile uint32_t *compare_register;
    uint8_t channel_number;
    Timer_Result result = Timer_ValidateChannel(channel);

    if((result != TIMER_RESULT_OK) || !channel->initialized)
    {
        return (result == TIMER_RESULT_OK) ? TIMER_RESULT_NOT_INITIALIZED : result;
    }

    if(duty_permille > 1000U)
    {
        duty_permille = 1000U;
    }

    timer = Timer_GetInstance(channel->timer->timer);
    channel_number = Timer_GetChannelNumber(channel->output);
    compare_register = Timer_GetCompareRegister(timer, channel_number);
    *compare_register = (uint32_t)(((uint64_t)(timer->ARR + 1U) * duty_permille) / 1000U);
    channel->duty_permille = duty_permille;

    return TIMER_RESULT_OK;
}

void Timer_IRQHandler(Timer_Identifier timer_identifier)
{
    uint32_t handle_index = Timer_GetHandleIndex(timer_identifier);
    Timer_Handle *timer;
    TIM_TypeDef *instance;

    if(handle_index >= TIMER_HANDLE_COUNT)
    {
        return;
    }

    timer = Timer_Handles[handle_index];

    if(timer == NULL)
    {
        return;
    }

    instance = Timer_GetInstance(timer_identifier);

    if((instance->SR & TIM_SR_UIF) == 0U)
    {
        return;
    }

    instance->SR &= ~TIM_SR_UIF;

    if(timer->update_callback != NULL)
    {
        timer->update_callback(timer->callback_context);
    }
}