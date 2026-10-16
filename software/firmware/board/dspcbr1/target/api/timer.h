/**
 * @file timer.h
 * @brief Hardware-independent timer and PWM interface.
 */

#ifndef TARGET_API_TIMER_H
#define TARGET_API_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Target-defined timer and channel identifier. */
typedef uint16_t Timer_IdentifierTypeDef;

/** @brief Target-defined timer output-channel identifier. */
typedef uint16_t Timer_OutputIdentifierTypeDef;

/** @brief Target-defined GPIO pin identifier used by a timer output. */
typedef uint8_t Timer_PinTypeDef;

#define TIMER_PIN_UNUSED ((Timer_PinTypeDef)0xFFU)

typedef enum
{
    TIMER_PWM_POLARITY_ACTIVE_HIGH = 0,
    TIMER_PWM_POLARITY_ACTIVE_LOW
} Timer_PWMPolarityTypeDef;

typedef enum
{
    TIMER_RESULT_OK = 0,
    TIMER_RESULT_INVALID_ARGUMENT,
    TIMER_RESULT_NOT_INITIALIZED,
    TIMER_RESULT_UNSUPPORTED,
    TIMER_RESULT_BUSY,
    TIMER_RESULT_HARDWARE_ERROR
} Timer_ResultTypeDef;

/**
 * @brief Callback invoked from the timer update interrupt.
 *
 * The callback executes in interrupt context and must not block.
 */
typedef void (*Timer_UpdateCallbackTypeDef)(void *Context);

/**
 * @brief Configuration and state of one timer peripheral.
 *
 * The selected frequency is the timer update frequency and the PWM carrier
 * frequency of every PWM channel attached to this timer.
 */
typedef struct
{
    Timer_IdentifierTypeDef Timer;
    uint32_t FrequencyHz;
    Timer_UpdateCallbackTypeDef UpdateCallback;
    void *CallbackContext;

    bool Initialized;
    bool Running;
} Timer_HandleTypeDef;

/**
 * @brief Configuration and state of one PWM output channel.
 */
typedef struct
{
    Timer_HandleTypeDef *Timer;
    Timer_OutputIdentifierTypeDef Output;
    Timer_PinTypeDef Pin;
    Timer_PWMPolarityTypeDef Polarity;
    uint16_t DutyPermille;

    bool Initialized;
    bool OutputEnabled;
} Timer_PWMChannelTypeDef;

/**
 * @brief Initialize a timer peripheral.
 *
 * The driver configures the update frequency and, when update_callback is not
 * NULL, enables the timer's update interrupt. The timer is left stopped.
 */
Timer_ResultTypeDef Timer_Init(Timer_HandleTypeDef *Timer);

/**
 * @brief Initialize a PWM output channel belonging to an initialized timer.
 *
 * The target validates the timer/output/pin combination and configures the
 * required alternate-function routing. The channel output is left disabled
 * until Timer_OutputEnable() is called.
 */
Timer_ResultTypeDef Timer_PWMChannelInit(Timer_PWMChannelTypeDef *Channel);

/**
 * @brief Start a configured timer.
 */
Timer_ResultTypeDef Timer_Start(Timer_HandleTypeDef *Timer);

/**
 * @brief Stop a configured timer.
 */
Timer_ResultTypeDef Timer_Stop(Timer_HandleTypeDef *Timer);

/**
 * @brief Enable a timer channel's physical output.
 *
 * Enabling the output does not start the timer or alter the configured duty
 * cycle.
 */
Timer_ResultTypeDef Timer_OutputEnable(Timer_PWMChannelTypeDef *Channel);

/**
 * @brief Disable a timer channel's physical output.
 *
 * Disabling the output does not stop the timer or alter the configured duty
 * cycle. This permits a timer update callback to gate an LED safely.
 */
Timer_ResultTypeDef Timer_OutputDisable(Timer_PWMChannelTypeDef *Channel);

/**
 * @brief Set a PWM channel duty cycle in permille.
 *
 * Values greater than 1000 are clamped to 1000.
 */
Timer_ResultTypeDef Timer_SetPWMDutyPermille(Timer_PWMChannelTypeDef *Channel, uint16_t DutyPermille);

/**
 * @brief Process an update interrupt for a target-defined timer.
 *
 * The target interrupt-vector file must call this function from the matching
 * timer IRQ handler.
 */
void Timer_IRQHandler(Timer_IdentifierTypeDef Timer);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_TIMER_H */
