/**
 * @file input.h
 * @brief Generic input-control contract.
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Input_NumberTypeDef;

typedef enum
{
    INPUT_TYPE_ANALOG = 0,
    INPUT_TYPE_DIGITAL
} Input_TypeTypeDef;

/**
 * @brief Describes one physical control exposed by the current hardware.
 *
 * Analog inputs may return any value from Minimum to Maximum.
 * Digital inputs return Minimum when inactive and Maximum when active.
 */
typedef struct
{
    Input_NumberTypeDef Number;
    int32_t Minimum;
    int32_t Maximum;
    Input_TypeTypeDef Type;
} Input_InfoTypeDef;

/**
 * @brief Initialise the platform input backend.
 *
 * @return true when the backend is ready; otherwise false.
 */
bool Input_Init(void);

/**
 * @brief Get the number of input controls exposed by this platform.
 */
uint8_t Input_Get_Count(void);

/**
 * @brief Get one control's description.
 *
 * @param Index Zero-based index, less than Input_Get_Count().
 * @param Info Destination for the control description.
 * @return true when successful; otherwise false.
 */
bool Input_Get_Info(uint8_t Index, Input_InfoTypeDef *Info);

/**
 * @brief Get the latest value from one input control.
 *
 * @param Number Physical control number from Input_InfoTypeDef.
 * @param Value Destination for the current control value.
 * @return true when successful; otherwise false.
 */
bool Input_Get_Value(Input_NumberTypeDef Number, int32_t *Value);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */