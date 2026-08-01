/**
 * @file power.h
 * @brief Target-agnostic battery charging, monitoring, and charge-indicator driver.
 */

#ifndef POWER_H
#define POWER_H

#include "gpio.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    POWER_RESULT_OK,
    POWER_RESULT_ERROR
} Power_ResultTypeDef;

typedef uint16_t (*Power_GetMillivoltsFunctionTypeDef)(void);

typedef struct
{
    const GPIO_PinTypeDef *charger_current_limit_pin;
    const GPIO_PinTypeDef *charger_status_pin;
    Timer_PWMChannel_Handle *charge_led_channel;
    Power_GetMillivoltsFunctionTypeDef get_cc1_millivolts;
    Power_GetMillivoltsFunctionTypeDef get_cc2_millivolts;
    Power_GetMillivoltsFunctionTypeDef get_battery_millivolts;
} Power_Handle;

/**
 * @brief Initializes the board-provided charging and monitoring interfaces.
 *
 * @param handle Initialized board power-hardware handle.
 *
 * @return POWER_RESULT_OK on success; otherwise POWER_RESULT_ERROR.
 */
Power_ResultTypeDef Power_Init(const Power_Handle *handle);

/**
 * @brief Periodic timer callback for power monitoring and charge indication.
 *
 * @param context Unused callback context.
 */
void Power_TimerUpdate(void *context);

/**
 * @brief Returns whether the charger reports that the battery is charging.
 */
bool Power_IsCharging(void);

/**
 * @brief Returns the filtered battery voltage in millivolts.
 */
uint16_t Power_GetBatteryVoltageMillivolts(void);

/**
 * @brief Returns the estimated battery charge from 0 to 1000 permille.
 */
uint16_t Power_GetBatteryChargePermille(void);

#endif