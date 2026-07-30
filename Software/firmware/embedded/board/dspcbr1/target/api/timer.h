/**
 * @file timer.h
 * @brief Hardware-independent timer and PWM interface.
 */

#ifndef TARGET_INTERFACE_TIMER_H
#define TARGET_INTERFACE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Target-defined timer and channel identifier. */
typedef uint16_t Timer_Identifier;

/** @brief Target-defined timer output-channel identifier. */
typedef uint16_t Timer_OutputIdentifier;

/** @brief Target-defined GPIO pin identifier used by a timer output. */
typedef uint8_t Timer_Pin;

#define TIMER_PIN_UNUSED ((Timer_Pin)0xFFU)

typedef enum
{
    TIMER_PWM_POLARITY_ACTIVE_HIGH = 0,
    TIMER_PWM_POLARITY_ACTIVE_LOW
} Timer_PWMPolarity;

typedef enum
{
    TIMER_RESULT_OK = 0,
    TIMER_RESULT_INVALID_ARGUMENT,
    TIMER_RESULT_NOT_INITIALIZED,
    TIMER_RESULT_UNSUPPORTED,
    TIMER_RESULT_BUSY,
    TIMER_RESULT_HARDWARE_ERROR
} Timer_Result;

/**
 * @brief Callback invoked from the timer update interrupt.
 *
 * The callback executes in interrupt context and must not block.
 */
typedef void (*Timer_UpdateCallback)(void *context);

/**
 * @brief Configuration and state of one timer peripheral.
 *
 * The selected frequency is the timer update frequency and the PWM carrier
 * frequency of every PWM channel attached to this timer.
 */
typedef struct
{
    Timer_Identifier timer;
    uint32_t frequency_hz;
    Timer_UpdateCallback update_callback;
    void *callback_context;

    bool initialized;
    bool running;
} Timer_Handle;

/**
 * @brief Configuration and state of one PWM output channel.
 */
typedef struct
{
    Timer_Handle *timer;
    Timer_OutputIdentifier output;
    Timer_Pin pin;
    Timer_PWMPolarity polarity;
    uint16_t duty_permille;

    bool initialized;
    bool output_enabled;
} Timer_PWMChannel_Handle;

/**
 * @brief Initialize a timer peripheral.
 *
 * The driver configures the update frequency and, when update_callback is not
 * NULL, enables the timer's update interrupt. The timer is left stopped.
 */
Timer_Result Timer_Init(Timer_Handle *timer);

/**
 * @brief Initialize a PWM output channel belonging to an initialized timer.
 *
 * The target validates the timer/output/pin combination and configures the
 * required alternate-function routing. The channel output is left disabled
 * until Timer_OutputEnable() is called.
 */
Timer_Result Timer_PWMChannelInit(Timer_PWMChannel_Handle *channel);

/**
 * @brief Start a configured timer.
 */
Timer_Result Timer_Start(Timer_Handle *timer);

/**
 * @brief Stop a configured timer.
 */
Timer_Result Timer_Stop(Timer_Handle *timer);

/**
 * @brief Enable a timer channel's physical output.
 *
 * Enabling the output does not start the timer or alter the configured duty
 * cycle.
 */
Timer_Result Timer_OutputEnable(Timer_PWMChannel_Handle *channel);

/**
 * @brief Disable a timer channel's physical output.
 *
 * Disabling the output does not stop the timer or alter the configured duty
 * cycle. This permits a timer update callback to gate an LED safely.
 */
Timer_Result Timer_OutputDisable(Timer_PWMChannel_Handle *channel);

/**
 * @brief Set a PWM channel duty cycle in permille.
 *
 * Values greater than 1000 are clamped to 1000.
 */
Timer_Result Timer_SetPWMDutyPermille(Timer_PWMChannel_Handle *channel, uint16_t duty_permille);

/**
 * @brief Process an update interrupt for a target-defined timer.
 *
 * The target interrupt-vector file must call this function from the matching
 * timer IRQ handler.
 */
void Timer_IRQHandler(Timer_Identifier timer);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_TIMER_H */