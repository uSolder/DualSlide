/**
 * @file power.h
 * @brief Target-agnostic battery charging, voltage monitoring, and charge-indicator driver.
 */

#ifndef POWER_H
#define POWER_H

#include "gpio.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    POWER_RESULT_OK,
    POWER_RESULT_ERROR
} Power_ResultTypeDef;

typedef uint16_t (*Power_GetMillivoltsFunctionTypeDef)(void);

typedef struct
{
    const GPIO_PinTypeDef *ChargerCurrentLimitPin;
    const GPIO_PinTypeDef *ChargerStatusPin;
    Timer_PWMChannelTypeDef *ChargeLEDChannel;
    Power_GetMillivoltsFunctionTypeDef GetCC1Millivolts;
    Power_GetMillivoltsFunctionTypeDef GetCC2Millivolts;
    Power_GetMillivoltsFunctionTypeDef GetBatteryMillivolts;
} Power_HandleTypeDef;

/**
 * @brief Initializes the board-provided charging and voltage-monitoring interfaces.
 *
 * @param Handle Initialized board power-hardware handle.
 *
 * @return POWER_RESULT_OK on success; otherwise POWER_RESULT_ERROR.
 */
Power_ResultTypeDef Power_Init(const Power_HandleTypeDef *Handle);

/**
 * @brief Periodic timer callback for voltage monitoring and charge indication.
 *
 * @param Context Unused callback context.
 */
void Power_TimerUpdate(void *Context);

/**
 * @brief Returns whether the charger reports that the battery is charging.
 */
bool Power_IsCharging(void);

/**
 * @brief Returns the filtered battery voltage in millivolts.
 */
uint16_t Power_GetBatteryVoltageMillivolts(void);

/**
 * @brief Returns whether the battery voltage has remained below the depletion threshold.
 */
bool Power_IsBatteryDepleted(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_H */
