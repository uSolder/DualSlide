/**
 * @file adc.h
 * @brief Hardware-independent analog-to-digital converter interface.
 *
 * This interface provides configuration and sampled-value access for
 * board-assigned analog input signals.
 *
 * The target implementation determines how pin identifiers map to ADC
 * peripherals and channels.
 */

#ifndef TARGET_INTERFACE_ADC_H
#define TARGET_INTERFACE_ADC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Value representing an unassigned ADC input pin.
 */
#define ADC_PIN_NONE UINT32_MAX
#define ADC_PIN_VREFINT (UINT32_MAX - 1U)

/* -------------------------------------------------------------------------- */
/* ADC input types                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Target-defined ADC-capable pin identifier.
 *
 * The target implementation determines how this value is encoded.
 *
 * Targets may use the same pin identifiers used by the GPIO interface.
 */
typedef uint32_t ADC_PinIdTypeDef;

/**
 * @brief ADC input descriptor.
 *
 * Board-level code creates one descriptor for each analog signal used by the
 * board. The target implementation maps the pin to the corresponding ADC
 * peripheral and channel.
 */
typedef struct
{
    ADC_PinIdTypeDef Pin;
} ADC_InputTypeDef;

/**
 * @brief Raw ADC conversion value.
 */
typedef uint16_t ADC_ValueTypeDef;

/**
 * @brief Result returned by ADC operations.
 */
typedef enum
{
    ADC_RESULT_OK = 0,
    ADC_RESULT_INVALID_ARGUMENT,
    ADC_RESULT_INVALID_PIN,
    ADC_RESULT_NOT_INITIALIZED,
    ADC_RESULT_ALREADY_INITIALIZED,
    ADC_RESULT_BUSY,
    ADC_RESULT_UNSUPPORTED,
    ADC_RESULT_HARDWARE_ERROR
} ADC_ResultTypeDef;

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the ADC for a fixed set of analog inputs.
 *
 * The target implementation configures the assigned pins, creates the ADC
 * conversion sequence, and prepares interrupt-driven sampling.
 *
 * The Values array must contain at least InputCount entries and must remain
 * valid until ADC_Stop() is called.
 *
 * @param Inputs     Array of ADC input descriptors.
 * @param Values     Destination array for the latest conversion values.
 * @param InputCount Number of entries in Inputs and Values.
 *
 * @return ADC_RESULT_OK on success.
 */
ADC_ResultTypeDef ADC_Init(const ADC_InputTypeDef *Inputs, ADC_ValueTypeDef *Values, uint8_t InputCount);

/**
 * @brief Start continuous ADC sampling.
 *
 * @return ADC_RESULT_OK on success.
 */
ADC_ResultTypeDef ADC_Start(void);

/**
 * @brief Stop ADC sampling.
 *
 * @return ADC_RESULT_OK on success.
 */
ADC_ResultTypeDef ADC_Stop(void);

/* -------------------------------------------------------------------------- */
/* Sample access                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read the latest conversion value for an ADC input.
 *
 * @param Input ADC input descriptor passed to ADC_Init().
 * @param Value Receives the latest raw ADC conversion value.
 *
 * @return ADC_RESULT_OK on success.
 */
ADC_ResultTypeDef ADC_Read(const ADC_InputTypeDef *Input, ADC_ValueTypeDef *Value);

/**
 * @brief Read the latest conversion value for an ADC input.
 *
 * This convenience function returns zero if the input is invalid or has not
 * been initialized. Use ADC_Read() when explicit error information is needed.
 *
 * @param Input ADC input descriptor passed to ADC_Init().
 *
 * @return Latest raw ADC conversion value, or zero on failure.
 */
ADC_ValueTypeDef ADC_GetValue(const ADC_InputTypeDef *Input);

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether an ADC input descriptor contains an assigned pin.
 *
 * This function checks only that the descriptor is non-null and that its pin
 * identifier is not ADC_PIN_NONE.
 *
 * @param Input ADC input descriptor.
 *
 * @return true when the descriptor contains an assigned pin.
 */
bool ADC_IsAssigned(const ADC_InputTypeDef *Input);

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handle ADC conversion interrupts.
 *
 * The target interrupt-vector file must call this function from the relevant
 * ADC interrupt handler.
 */
void ADC_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_ADC_H */