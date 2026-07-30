/**
 * @file gpio.h
 * @brief Hardware-independent digital GPIO interface.
 *
 * This interface provides configuration, logical control, and interrupt
 * registration for board-level digital GPIO signals.
 *
 * It intentionally does not expose alternate functions, analog modes, drive
 * strength, slew rate, or other target-specific configuration.
 */

#ifndef TARGET_INTERFACE_GPIO_H
#define TARGET_INTERFACE_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Value representing an unassigned GPIO pin.
 */
#define GPIO_PIN_NONE UINT32_MAX

/* -------------------------------------------------------------------------- */
/* Pin types                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Target-defined GPIO pin identifier.
 *
 * The target implementation determines how this value is encoded.
 *
 * An STM32 target may encode the port in the upper nibble and pin number in
 * the lower nibble:
 *
 * @code
 * PA0  = 0x00
 * PA15 = 0x0F
 * PB0  = 0x10
 * PC7  = 0x27
 * @endcode
 */
typedef uint32_t GPIO_PinIdTypeDef;

/**
 * @brief GPIO pin descriptor.
 *
 * Board-level code creates one descriptor for each digital signal used by the
 * board. Device drivers reference this descriptor instead of target-specific
 * port and pin definitions.
 */
typedef struct
{
    GPIO_PinIdTypeDef Pin;
} GPIO_PinTypeDef;

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Digital GPIO operating mode.
 */
typedef enum
{
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT
} GPIO_ModeTypeDef;

/**
 * @brief Electrical output-driver type.
 *
 * This setting applies only when the pin is configured as an output.
 */
typedef enum
{
    GPIO_OUTPUT_PUSH_PULL = 0,
    GPIO_OUTPUT_OPEN_DRAIN
} GPIO_OutputTypeDef;

/**
 * @brief Internal pull-resistor configuration.
 */
typedef enum
{
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN
} GPIO_PullTypeDef;

/**
 * @brief Logical digital GPIO level.
 */
typedef enum
{
    GPIO_LEVEL_LOW = 0,
    GPIO_LEVEL_HIGH
} GPIO_LevelTypeDef;

/**
 * @brief Digital GPIO configuration.
 *
 * For input pins, OutputType and InitialLevel are ignored.
 *
 * For output pins, InitialLevel is applied before the pin is switched into
 * output mode where the target permits this. This avoids unintended output
 * pulses during initialization.
 */
typedef struct
{
    GPIO_ModeTypeDef Mode;
    GPIO_OutputTypeDef OutputType;
    GPIO_PullTypeDef Pull;
    GPIO_LevelTypeDef InitialLevel;
} GPIO_ConfigTypeDef;

/**
 * @brief GPIO interrupt edge selection.
 */
typedef enum
{
    GPIO_INTERRUPT_RISING_EDGE = 0,
    GPIO_INTERRUPT_FALLING_EDGE,
    GPIO_INTERRUPT_BOTH_EDGES
} GPIO_InterruptModeTypeDef;

/**
 * @brief GPIO interrupt callback.
 *
 * The callback executes in interrupt context. It must be short, must not
 * block, and must not call functions that wait for an interrupt or otherwise
 * depend on normal application scheduling.
 *
 * @param Context Caller-provided context supplied during registration.
 */
typedef void (*GPIO_InterruptCallbackTypeDef)(void *Context);

/**
 * @brief GPIO interrupt registration parameters.
 */
typedef struct
{
    GPIO_InterruptModeTypeDef Mode;
    GPIO_InterruptCallbackTypeDef Callback;
    void *Context;
} GPIO_InterruptConfigTypeDef;

/**
 * @brief Result returned by GPIO operations.
 */
typedef enum
{
    GPIO_RESULT_OK = 0,
    GPIO_RESULT_INVALID_ARGUMENT,
    GPIO_RESULT_INVALID_PIN,
    GPIO_RESULT_NOT_INITIALIZED,
    GPIO_RESULT_BUSY,
    GPIO_RESULT_UNSUPPORTED,
    GPIO_RESULT_HARDWARE_ERROR
} GPIO_ResultTypeDef;

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure a GPIO pin for digital input or output operation.
 *
 * The target implementation enables the required GPIO peripheral clock and
 * applies the requested digital configuration.
 *
 * This function does not support alternate-function or analog configuration.
 *
 * @param Pin    GPIO pin descriptor.
 * @param Config Digital pin configuration.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Init(const GPIO_PinTypeDef *Pin, const GPIO_ConfigTypeDef *Config);

/**
 * @brief Return a GPIO pin to its target-defined reset configuration.
 *
 * Any interrupt registered for the pin is unregistered before the pin is
 * deinitialized.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Deinit(const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Interrupts                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register a GPIO interrupt callback.
 *
 * The pin must already be configured as a digital input. The target
 * implementation configures the required interrupt routing and enables the
 * interrupt source.
 *
 * Only one callback may be registered for a pin at a time. Calling this
 * function for an already-registered pin returns GPIO_RESULT_BUSY.
 *
 * The target implementation clears any pending interrupt condition before
 * enabling the interrupt.
 *
 * @param Pin    GPIO pin descriptor.
 * @param Config Interrupt edge and callback configuration.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_RegisterInterrupt(const GPIO_PinTypeDef *Pin, const GPIO_InterruptConfigTypeDef *Config);

/**
 * @brief Disable and unregister a GPIO interrupt callback.
 *
 * This function disables interrupt generation for the pin, clears any pending
 * interrupt condition, and discards the registered callback and context.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_UnregisterInterrupt(const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Digital output                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Set the logical output level of a GPIO pin.
 *
 * The pin must already be configured as a digital output.
 *
 * @param Pin   GPIO pin descriptor.
 * @param Level Desired logical output level.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Write(const GPIO_PinTypeDef *Pin, GPIO_LevelTypeDef Level);

/**
 * @brief Set a GPIO output to the logical high level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Set(const GPIO_PinTypeDef *Pin);

/**
 * @brief Set a GPIO output to the logical low level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Clear(const GPIO_PinTypeDef *Pin);

/**
 * @brief Toggle the current GPIO output level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Toggle(const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Digital input                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read the current logical level of a GPIO pin.
 *
 * The pin must already be configured as a digital input or output.
 *
 * @param Pin   GPIO pin descriptor.
 * @param Level Receives the current logical pin level.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Read(const GPIO_PinTypeDef *Pin, GPIO_LevelTypeDef *Level);

/**
 * @brief Determine whether a GPIO pin is logically high.
 *
 * If the pin cannot be read, this function returns false. Use GPIO_Read() when
 * the caller needs explicit error information.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the pin is logically high.
 */
bool GPIO_IsHigh(const GPIO_PinTypeDef *Pin);

/**
 * @brief Determine whether a GPIO pin is logically low.
 *
 * If the pin cannot be read, this function returns false. Use GPIO_Read() when
 * the caller needs explicit error information.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the descriptor contains an assigned pin.
 */
bool GPIO_IsLow(const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether a GPIO descriptor contains an assigned pin.
 *
 * This function checks only that the descriptor is non-null and that its pin
 * identifier is not GPIO_PIN_NONE. It does not verify that the pin physically
 * exists on the current target.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the descriptor contains an assigned pin.
 */
bool GPIO_IsAssigned(const GPIO_PinTypeDef *Pin);

void GPIO_InterruptHandler(uint32_t Line);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_GPIO_H */