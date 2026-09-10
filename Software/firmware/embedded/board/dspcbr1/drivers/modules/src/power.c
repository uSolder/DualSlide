/**
 * @file power.c
 * @brief Target-agnostic battery charging, voltage-monitoring, and charge-indicator implementation.
 */

#include "power.h"

#include <stddef.h>
#include <stdint.h>

#define USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS        660U
#define CHARGE_LED_MINIMUM_DUTY_PERMILLE                500U
#define CHARGE_LED_MAXIMUM_DUTY_PERMILLE                1000U
#define CHARGE_LED_DISABLE_VOLTAGE_MILLIVOLTS           4150U
#define BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT            1024U
#define BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS          10U
#define BATTERY_DEPLETED_VOLTAGE_MILLIVOLTS             3300U
#define BATTERY_RECOVERY_VOLTAGE_MILLIVOLTS             3400U
#define BATTERY_DEPLETED_CONFIRMATION_AVERAGES          5U

static bool Power_IsFastUSBCurrentAvailable(void);
static void Power_UpdateBatteryChargeCurrentLimit(bool fast_current_available);
static void Power_UpdateBatteryVoltage(void);
static void Power_ApplyBatteryVoltageAverage(uint32_t battery_voltage_millivolts);
static void Power_UpdateBatteryDepletedState(uint32_t battery_voltage_millivolts);
static uint16_t Power_RoundBatteryVoltageToOutputStep(uint32_t battery_voltage_millivolts);

static const Power_Handle *PowerHandle;
static bool IsCharging;
static bool IsFastUSBCurrent;
static uint16_t OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
static bool OrangeLEDDutyIncreasing = true;
static bool OrangeLEDSlowUpdateToggle;
static uint32_t BatteryVoltageSampleAccumulator;
static uint16_t BatteryVoltageSampleCount;
static uint16_t FilteredBatteryVoltageMillivolts;
static uint8_t BatteryDepletedAverageCount;
static bool IsBatteryVoltageValid;
static bool IsBatteryDepleted;

Power_ResultTypeDef Power_Init(const Power_Handle *handle)
{
    if((handle == NULL) ||
       (handle->charger_current_limit_pin == NULL) ||
       (handle->charger_status_pin == NULL) ||
       (handle->charge_led_channel == NULL) ||
       (handle->get_cc1_millivolts == NULL) ||
       (handle->get_cc2_millivolts == NULL) ||
       (handle->get_battery_millivolts == NULL))
    {
        return POWER_RESULT_ERROR;
    }

    PowerHandle = handle;
    IsCharging = GPIO_IsLow(PowerHandle->charger_status_pin);
    IsFastUSBCurrent = false;
    OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
    OrangeLEDDutyIncreasing = true;
    OrangeLEDSlowUpdateToggle = false;
    BatteryVoltageSampleAccumulator = 0U;
    BatteryVoltageSampleCount = 0U;
    FilteredBatteryVoltageMillivolts = 0U;
    BatteryDepletedAverageCount = 0U;
    IsBatteryVoltageValid = false;
    IsBatteryDepleted = false;

    if(GPIO_Set(PowerHandle->charger_current_limit_pin) != GPIO_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    if(Timer_SetPWMDutyPermille(PowerHandle->charge_led_channel, OrangeLEDDutyPermille) != TIMER_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    if(Timer_OutputDisable(PowerHandle->charge_led_channel) != TIMER_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    return POWER_RESULT_OK;
}

void Power_TimerUpdate(void *context)
{
    bool fast_current_available;
    bool update_duty;

    (void)context;

    if(PowerHandle == NULL)
    {
        return;
    }

    IsCharging = GPIO_IsLow(PowerHandle->charger_status_pin);
    Power_UpdateBatteryVoltage();

    fast_current_available = Power_IsFastUSBCurrentAvailable();
    Power_UpdateBatteryChargeCurrentLimit(fast_current_available);

    if(!IsCharging ||
       (Power_GetBatteryVoltageMillivolts() >= CHARGE_LED_DISABLE_VOLTAGE_MILLIVOLTS))
    {
        (void)Timer_OutputDisable(PowerHandle->charge_led_channel);
        return;
    }

    (void)Timer_OutputEnable(PowerHandle->charge_led_channel);

    update_duty = fast_current_available;

    if(!update_duty)
    {
        OrangeLEDSlowUpdateToggle = !OrangeLEDSlowUpdateToggle;
        update_duty = OrangeLEDSlowUpdateToggle;
    }

    if(!update_duty)
    {
        return;
    }

    if(OrangeLEDDutyIncreasing)
    {
        OrangeLEDDutyPermille++;

        if(OrangeLEDDutyPermille >= CHARGE_LED_MAXIMUM_DUTY_PERMILLE)
        {
            OrangeLEDDutyPermille = CHARGE_LED_MAXIMUM_DUTY_PERMILLE;
            OrangeLEDDutyIncreasing = false;
        }
    }
    else
    {
        OrangeLEDDutyPermille--;

        if(OrangeLEDDutyPermille <= CHARGE_LED_MINIMUM_DUTY_PERMILLE)
        {
            OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
            OrangeLEDDutyIncreasing = true;
        }
    }

    (void)Timer_SetPWMDutyPermille(PowerHandle->charge_led_channel, OrangeLEDDutyPermille);
}

bool Power_IsCharging(void)
{
    return IsCharging;
}

uint16_t Power_GetBatteryVoltageMillivolts(void)
{
    return FilteredBatteryVoltageMillivolts;
}

bool Power_IsBatteryDepleted(void)
{
    return IsBatteryDepleted && !IsCharging;
}

static bool Power_IsFastUSBCurrentAvailable(void)
{
    return (PowerHandle->get_cc1_millivolts() >= USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS) ||
           (PowerHandle->get_cc2_millivolts() >= USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS);
}

static void Power_UpdateBatteryChargeCurrentLimit(bool fast_current_available)
{
    if(fast_current_available == IsFastUSBCurrent)
    {
        return;
    }

    if(fast_current_available)
    {
        (void)GPIO_Clear(PowerHandle->charger_current_limit_pin);
    }
    else
    {
        (void)GPIO_Set(PowerHandle->charger_current_limit_pin);
    }

    IsFastUSBCurrent = fast_current_available;
}

static void Power_UpdateBatteryVoltage(void)
{
    uint32_t battery_voltage_millivolts;
    uint32_t average_battery_voltage_millivolts;

    battery_voltage_millivolts = PowerHandle->get_battery_millivolts();

    if(battery_voltage_millivolts == 0U)
    {
        return;
    }

    if(!IsBatteryVoltageValid)
    {
        FilteredBatteryVoltageMillivolts = Power_RoundBatteryVoltageToOutputStep(battery_voltage_millivolts);
        IsBatteryVoltageValid = true;
    }

    BatteryVoltageSampleAccumulator += battery_voltage_millivolts;
    BatteryVoltageSampleCount++;

    if(BatteryVoltageSampleCount < BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT)
    {
        return;
    }

    average_battery_voltage_millivolts =
        (BatteryVoltageSampleAccumulator + (BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT / 2U)) /
        BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT;

    BatteryVoltageSampleAccumulator = 0U;
    BatteryVoltageSampleCount = 0U;

    Power_ApplyBatteryVoltageAverage(average_battery_voltage_millivolts);
    Power_UpdateBatteryDepletedState(average_battery_voltage_millivolts);
}

static void Power_ApplyBatteryVoltageAverage(uint32_t battery_voltage_millivolts)
{
    if((battery_voltage_millivolts >= ((uint32_t)FilteredBatteryVoltageMillivolts + BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS)) ||
       ((battery_voltage_millivolts + BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS) <= FilteredBatteryVoltageMillivolts))
    {
        FilteredBatteryVoltageMillivolts = Power_RoundBatteryVoltageToOutputStep(battery_voltage_millivolts);
    }
}

static void Power_UpdateBatteryDepletedState(uint32_t battery_voltage_millivolts)
{
    if(IsCharging)
    {
        BatteryDepletedAverageCount = 0U;
        IsBatteryDepleted = false;
        return;
    }

    if(battery_voltage_millivolts <= BATTERY_DEPLETED_VOLTAGE_MILLIVOLTS)
    {
        if(BatteryDepletedAverageCount < BATTERY_DEPLETED_CONFIRMATION_AVERAGES)
        {
            BatteryDepletedAverageCount++;
        }

        if(BatteryDepletedAverageCount >= BATTERY_DEPLETED_CONFIRMATION_AVERAGES)
        {
            IsBatteryDepleted = true;
        }

        return;
    }

    BatteryDepletedAverageCount = 0U;

    if(battery_voltage_millivolts >= BATTERY_RECOVERY_VOLTAGE_MILLIVOLTS)
    {
        IsBatteryDepleted = false;
    }
}

static uint16_t Power_RoundBatteryVoltageToOutputStep(uint32_t battery_voltage_millivolts)
{
    uint32_t rounded_battery_voltage_millivolts;

    rounded_battery_voltage_millivolts =
        ((battery_voltage_millivolts + (BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS / 2U)) /
         BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS) *
        BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS;

    return (rounded_battery_voltage_millivolts > UINT16_MAX) ?
        UINT16_MAX : (uint16_t)rounded_battery_voltage_millivolts;
}