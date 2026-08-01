/**
 * @file power.c
 * @brief Target-agnostic battery charging and charge-indicator implementation.
 */

#include "power.h"

#include <stddef.h>
#include <stdint.h>

#define USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS        660U
#define CHARGE_LED_MINIMUM_DUTY_PERMILLE                500U
#define CHARGE_LED_MAXIMUM_DUTY_PERMILLE                1000U
#define BATTERY_FILTER_SHIFT                            8U
#define BATTERY_CHARGE_FULL_PERMILLE                    1000U

/*
 * TIM4 calls Power_TimerUpdate() at 1 kHz. Applying one permille correction
 * every three seconds limits the displayed charge change to 1% every 30 s.
 */
#define BATTERY_CHARGE_CORRECTION_INTERVAL_UPDATES      3000U

typedef struct
{
    uint16_t voltage_millivolts;
    uint16_t charge_permille;
} Power_BatteryChargePointTypeDef;

static bool Power_IsFastUSBCurrentAvailable(void);
static void Power_UpdateBatteryChargeCurrentLimit(bool fast_current_available);
static void Power_UpdateBatteryVoltage(void);
static void Power_UpdateBatteryChargeEstimate(void);
static uint16_t Power_GetBatteryChargeFromVoltage(uint16_t voltage_millivolts);

static const Power_Handle *PowerHandle;
static bool IsCharging;
static bool IsFastUSBCurrent;
static uint16_t OrangeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
static bool OrangeLEDDutyIncreasing = true;
static bool OrangeLEDSlowUpdateToggle;
static uint32_t FilteredBatteryVoltageMillivolts;
static bool IsBatteryVoltageValid;
static uint16_t EstimatedBatteryChargePermille;
static uint16_t BatteryChargeCorrectionUpdateCount;
static bool IsBatteryChargeEstimateValid;

/* Typical 1-cell Li-ion open-circuit discharge curve. */
static const Power_BatteryChargePointTypeDef BatteryChargeCurve[] =
{
    { 3300U,    0U },
    { 3600U,   50U },
    { 3700U,  100U },
    { 3740U,  200U },
    { 3770U,  300U },
    { 3800U,  400U },
    { 3830U,  500U },
    { 3860U,  600U },
    { 3900U,  700U },
    { 3950U,  800U },
    { 4000U,  900U },
    { 4100U,  975U },
    { 4150U, 1000U }
};

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
    IsBatteryVoltageValid = false;
    EstimatedBatteryChargePermille = 0U;
    BatteryChargeCorrectionUpdateCount = 0U;
    IsBatteryChargeEstimateValid = false;

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

    if(!IsCharging || (Power_GetBatteryChargePermille() >= BATTERY_CHARGE_FULL_PERMILLE))
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
    return (uint16_t)FilteredBatteryVoltageMillivolts;
}

uint16_t Power_GetBatteryChargePermille(void)
{
    if(IsBatteryChargeEstimateValid)
    {
        return EstimatedBatteryChargePermille;
    }

    return Power_GetBatteryChargeFromVoltage(Power_GetBatteryVoltageMillivolts());
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

    battery_voltage_millivolts = PowerHandle->get_battery_millivolts();

    if(battery_voltage_millivolts == 0U)
    {
        return;
    }

    if(!IsBatteryVoltageValid)
    {
        FilteredBatteryVoltageMillivolts = battery_voltage_millivolts;
        IsBatteryVoltageValid = true;
        Power_UpdateBatteryChargeEstimate();
        return;
    }

    if(battery_voltage_millivolts > FilteredBatteryVoltageMillivolts)
    {
        uint32_t difference = battery_voltage_millivolts - FilteredBatteryVoltageMillivolts;
        uint32_t adjustment = difference >> BATTERY_FILTER_SHIFT;

        FilteredBatteryVoltageMillivolts += (adjustment != 0U) ? adjustment : 1U;
    }
    else if(battery_voltage_millivolts < FilteredBatteryVoltageMillivolts)
    {
        uint32_t difference = FilteredBatteryVoltageMillivolts - battery_voltage_millivolts;
        uint32_t adjustment = difference >> BATTERY_FILTER_SHIFT;

        FilteredBatteryVoltageMillivolts -= (adjustment != 0U) ? adjustment : 1U;
    }

    Power_UpdateBatteryChargeEstimate();
}

static void Power_UpdateBatteryChargeEstimate(void)
{
    uint16_t voltage_charge_permille;

    voltage_charge_permille = Power_GetBatteryChargeFromVoltage(Power_GetBatteryVoltageMillivolts());

    if(!IsBatteryChargeEstimateValid)
    {
        EstimatedBatteryChargePermille = voltage_charge_permille;
        IsBatteryChargeEstimateValid = true;
        BatteryChargeCorrectionUpdateCount = 0U;
        return;
    }

    /*
     * Board load causes an immediate voltage drop when the main rail starts.
     * While the charger is actively charging, that voltage sag must not be
     * interpreted as battery capacity being removed.
     */
    if(IsCharging && (voltage_charge_permille < EstimatedBatteryChargePermille))
    {
        BatteryChargeCorrectionUpdateCount = 0U;
        return;
    }

    if(EstimatedBatteryChargePermille == voltage_charge_permille)
    {
        BatteryChargeCorrectionUpdateCount = 0U;
        return;
    }

    BatteryChargeCorrectionUpdateCount++;

    if(BatteryChargeCorrectionUpdateCount < BATTERY_CHARGE_CORRECTION_INTERVAL_UPDATES)
    {
        return;
    }

    BatteryChargeCorrectionUpdateCount = 0U;

    if(EstimatedBatteryChargePermille < voltage_charge_permille)
    {
        EstimatedBatteryChargePermille++;
    }
    else
    {
        EstimatedBatteryChargePermille--;
    }
}

static uint16_t Power_GetBatteryChargeFromVoltage(uint16_t voltage_millivolts)
{
    uint32_t index;

    if(voltage_millivolts <= BatteryChargeCurve[0].voltage_millivolts)
    {
        return BatteryChargeCurve[0].charge_permille;
    }

    for(index = 1U; index < (sizeof(BatteryChargeCurve) / sizeof(BatteryChargeCurve[0])); index++)
    {
        const Power_BatteryChargePointTypeDef *lower_point = &BatteryChargeCurve[index - 1U];
        const Power_BatteryChargePointTypeDef *upper_point = &BatteryChargeCurve[index];

        if(voltage_millivolts <= upper_point->voltage_millivolts)
        {
            uint32_t voltage_range = upper_point->voltage_millivolts - lower_point->voltage_millivolts;
            uint32_t voltage_offset = voltage_millivolts - lower_point->voltage_millivolts;
            uint32_t charge_range = upper_point->charge_permille - lower_point->charge_permille;

            return (uint16_t)(lower_point->charge_permille + ((voltage_offset * charge_range) / voltage_range));
        }
    }

    return BatteryChargeCurve[(sizeof(BatteryChargeCurve) / sizeof(BatteryChargeCurve[0])) - 1U].charge_permille;
}