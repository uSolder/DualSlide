/*
 * gpio.h
 *
 * Hardware-independent digital GPIO interface.
 *
 * This interface provides simple logical control of board-level signals.
 * It intentionally does not expose alternate functions, analog modes,
 * drive strength, slew rate, or other target-specific pin configuration.
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
#define GPIO_PIN_NONE    UINT32_MAX

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Target-defined GPIO pin identifier.
 *
 * The target implementation determines how this value is encoded.
 *
 * An STM32 target may encode the port in the upper nibble and the pin number
 * in the lower nibble:
 *
 * @code
 * PA0  = 0x00
 * PA15 = 0x0F
 * PB0  = 0x10
 * PC7  = 0x27
 * @endcode
 *
 * Other targets may use flat pin numbering or another internal representation.
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

/**
 * @brief Logical digital GPIO level.
 */
typedef enum
{
    GPIO_LEVEL_LOW = 0,
    GPIO_LEVEL_HIGH
} GPIO_LevelTypeDef;

/**
 * @brief Result returned by GPIO operations.
 */
typedef enum
{
    GPIO_RESULT_OK = 0,
    GPIO_RESULT_INVALID_ARGUMENT,
    GPIO_RESULT_INVALID_PIN,
    GPIO_RESULT_NOT_INITIALIZED,
    GPIO_RESULT_UNSUPPORTED,
    GPIO_RESULT_HARDWARE_ERROR
} GPIO_ResultTypeDef;

/* -------------------------------------------------------------------------- */
/* Digital output                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Set the logical output level of a GPIO pin.
 *
 * The pin must already be configured as a digital output by the target or
 * board initialization code.
 *
 * @param Pin   GPIO pin descriptor.
 * @param Level Desired logical output level.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Write(
    const GPIO_PinTypeDef *Pin,
    GPIO_LevelTypeDef Level);

/**
 * @brief Set a GPIO output to the logical high level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Set(
    const GPIO_PinTypeDef *Pin);

/**
 * @brief Set a GPIO output to the logical low level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Clear(
    const GPIO_PinTypeDef *Pin);

/**
 * @brief Toggle the current GPIO output level.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Toggle(
    const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Digital input                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read the current logical level of a GPIO pin.
 *
 * The pin must already be configured as a digital input or output by the
 * target or board initialization code.
 *
 * @param Pin   GPIO pin descriptor.
 * @param Level Receives the current logical pin level.
 *
 * @return GPIO_RESULT_OK on success.
 */
GPIO_ResultTypeDef GPIO_Read(
    const GPIO_PinTypeDef *Pin,
    GPIO_LevelTypeDef *Level);

/**
 * @brief Determine whether a GPIO pin is logically high.
 *
 * If the pin cannot be read, this function returns false. Use GPIO_Read()
 * when the caller needs explicit error information.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the pin is logically high.
 */
bool GPIO_IsHigh(
    const GPIO_PinTypeDef *Pin);

/**
 * @brief Determine whether a GPIO pin is logically low.
 *
 * If the pin cannot be read, this function returns false. Use GPIO_Read()
 * when the caller needs explicit error information.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the pin is logically low.
 */
bool GPIO_IsLow(
    const GPIO_PinTypeDef *Pin);

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether a GPIO pin descriptor contains an assigned pin.
 *
 * This function checks only that the descriptor is non-null and that its pin
 * identifier is not GPIO_PIN_NONE. It does not verify that the pin physically
 * exists on the current target.
 *
 * @param Pin GPIO pin descriptor.
 *
 * @return true when the descriptor contains an assigned pin.
 */
bool GPIO_IsAssigned(
    const GPIO_PinTypeDef *Pin);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_GPIO_H */