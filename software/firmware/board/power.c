/**
 * @file power.c
 * @brief Target-agnostic battery charging, voltage-monitoring, and charge-indicator implementation.
 */

#include "power.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS        660U
#define CHARGE_LED_MINIMUM_DUTY_PERMILLE                500U
#define CHARGE_LED_MAXIMUM_DUTY_PERMILLE                1000U
#define CHARGE_LED_DISABLE_VOLTAGE_MILLIVOLTS           4150U
#define BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT            1024U
#define BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS          10U
#define BATTERY_DEPLETED_VOLTAGE_MILLIVOLTS             3300U
#define BATTERY_RECOVERY_VOLTAGE_MILLIVOLTS             3400U
#define BATTERY_DEPLETED_CONFIRMATION_AVERAGES          5U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Power_IsFastUSBCurrentAvailable(void);
static void Power_UpdateBatteryChargeCurrentLimit(bool FastCurrentAvailable);
static void Power_UpdateBatteryVoltage(void);
static void Power_ApplyBatteryVoltageAverage(uint32_t BatteryVoltageMillivolts);
static void Power_UpdateBatteryDepletedState(uint32_t BatteryVoltageMillivolts);
static uint16_t Power_RoundBatteryVoltageToOutputStep(uint32_t BatteryVoltageMillivolts);

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const Power_HandleTypeDef *Power_Handle;
static bool Power_Charging;
static bool Power_FastUSBCurrent;
static uint16_t Power_ChargeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
static bool Power_ChargeLEDDutyIncreasing = true;
static bool Power_ChargeLEDSlowUpdateToggle;
static uint32_t Power_BatteryVoltageSampleAccumulator;
static uint16_t Power_BatteryVoltageSampleCount;
static uint16_t Power_FilteredBatteryVoltageMillivolts;
static uint8_t Power_BatteryDepletedAverageCount;
static bool Power_BatteryVoltageValid;
static bool Power_BatteryDepleted;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

Power_ResultTypeDef Power_Init(const Power_HandleTypeDef *Handle)
{
    if((Handle == NULL) ||
       (Handle->ChargerCurrentLimitPin == NULL) ||
       (Handle->ChargerStatusPin == NULL) ||
       (Handle->ChargeLEDChannel == NULL) ||
       (Handle->GetCC1Millivolts == NULL) ||
       (Handle->GetCC2Millivolts == NULL) ||
       (Handle->GetBatteryMillivolts == NULL))
    {
        return POWER_RESULT_ERROR;
    }

    Power_Handle = Handle;
    Power_Charging = GPIO_IsLow(Power_Handle->ChargerStatusPin);
    Power_FastUSBCurrent = false;
    Power_ChargeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
    Power_ChargeLEDDutyIncreasing = true;
    Power_ChargeLEDSlowUpdateToggle = false;
    Power_BatteryVoltageSampleAccumulator = 0U;
    Power_BatteryVoltageSampleCount = 0U;
    Power_FilteredBatteryVoltageMillivolts = 0U;
    Power_BatteryDepletedAverageCount = 0U;
    Power_BatteryVoltageValid = false;
    Power_BatteryDepleted = false;

    if(GPIO_Set(Power_Handle->ChargerCurrentLimitPin) != GPIO_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    if(Timer_SetPWMDutyPermille(Power_Handle->ChargeLEDChannel, Power_ChargeLEDDutyPermille) != TIMER_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    if(Timer_OutputDisable(Power_Handle->ChargeLEDChannel) != TIMER_RESULT_OK)
    {
        return POWER_RESULT_ERROR;
    }

    return POWER_RESULT_OK;
}

void Power_TimerUpdate(void *Context)
{
    bool FastCurrentAvailable;
    bool UpdateDuty;

    (void)Context;

    if(Power_Handle == NULL)
    {
        return;
    }

    Power_Charging = GPIO_IsLow(Power_Handle->ChargerStatusPin);
    Power_UpdateBatteryVoltage();

    FastCurrentAvailable = Power_IsFastUSBCurrentAvailable();
    Power_UpdateBatteryChargeCurrentLimit(FastCurrentAvailable);

    if(!Power_Charging ||
       (Power_GetBatteryVoltageMillivolts() >= CHARGE_LED_DISABLE_VOLTAGE_MILLIVOLTS))
    {
        (void)Timer_OutputDisable(Power_Handle->ChargeLEDChannel);
        return;
    }

    (void)Timer_OutputEnable(Power_Handle->ChargeLEDChannel);

    UpdateDuty = FastCurrentAvailable;

    if(!UpdateDuty)
    {
        Power_ChargeLEDSlowUpdateToggle = !Power_ChargeLEDSlowUpdateToggle;
        UpdateDuty = Power_ChargeLEDSlowUpdateToggle;
    }

    if(!UpdateDuty)
    {
        return;
    }

    if(Power_ChargeLEDDutyIncreasing)
    {
        Power_ChargeLEDDutyPermille++;

        if(Power_ChargeLEDDutyPermille >= CHARGE_LED_MAXIMUM_DUTY_PERMILLE)
        {
            Power_ChargeLEDDutyPermille = CHARGE_LED_MAXIMUM_DUTY_PERMILLE;
            Power_ChargeLEDDutyIncreasing = false;
        }
    }
    else
    {
        Power_ChargeLEDDutyPermille--;

        if(Power_ChargeLEDDutyPermille <= CHARGE_LED_MINIMUM_DUTY_PERMILLE)
        {
            Power_ChargeLEDDutyPermille = CHARGE_LED_MINIMUM_DUTY_PERMILLE;
            Power_ChargeLEDDutyIncreasing = true;
        }
    }

    (void)Timer_SetPWMDutyPermille(Power_Handle->ChargeLEDChannel, Power_ChargeLEDDutyPermille);
}

bool Power_IsCharging(void)
{
    return Power_Charging;
}

uint16_t Power_GetBatteryVoltageMillivolts(void)
{
    return Power_FilteredBatteryVoltageMillivolts;
}

bool Power_IsBatteryDepleted(void)
{
    return Power_BatteryDepleted && !Power_Charging;
}

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool Power_IsFastUSBCurrentAvailable(void)
{
    return (Power_Handle->GetCC1Millivolts() >= USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS) ||
           (Power_Handle->GetCC2Millivolts() >= USB_CC_FAST_CURRENT_THRESHOLD_MILLIVOLTS);
}

static void Power_UpdateBatteryChargeCurrentLimit(bool FastCurrentAvailable)
{
    if(FastCurrentAvailable == Power_FastUSBCurrent)
    {
        return;
    }

    if(FastCurrentAvailable)
    {
        (void)GPIO_Clear(Power_Handle->ChargerCurrentLimitPin);
    }
    else
    {
        (void)GPIO_Set(Power_Handle->ChargerCurrentLimitPin);
    }

    Power_FastUSBCurrent = FastCurrentAvailable;
}

static void Power_UpdateBatteryVoltage(void)
{
    uint32_t BatteryVoltageMillivolts;
    uint32_t AverageBatteryVoltageMillivolts;

    BatteryVoltageMillivolts = Power_Handle->GetBatteryMillivolts();

    if(BatteryVoltageMillivolts == 0U)
    {
        return;
    }

    if(!Power_BatteryVoltageValid)
    {
        Power_FilteredBatteryVoltageMillivolts = Power_RoundBatteryVoltageToOutputStep(BatteryVoltageMillivolts);
        Power_BatteryVoltageValid = true;
    }

    Power_BatteryVoltageSampleAccumulator += BatteryVoltageMillivolts;
    Power_BatteryVoltageSampleCount++;

    if(Power_BatteryVoltageSampleCount < BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT)
    {
        return;
    }

    AverageBatteryVoltageMillivolts =
        (Power_BatteryVoltageSampleAccumulator + (BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT / 2U)) /
        BATTERY_VOLTAGE_AVERAGE_SAMPLE_COUNT;

    Power_BatteryVoltageSampleAccumulator = 0U;
    Power_BatteryVoltageSampleCount = 0U;

    Power_ApplyBatteryVoltageAverage(AverageBatteryVoltageMillivolts);
    Power_UpdateBatteryDepletedState(AverageBatteryVoltageMillivolts);
}

static void Power_ApplyBatteryVoltageAverage(uint32_t BatteryVoltageMillivolts)
{
    if((BatteryVoltageMillivolts >= ((uint32_t)Power_FilteredBatteryVoltageMillivolts + BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS)) ||
       ((BatteryVoltageMillivolts + BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS) <= Power_FilteredBatteryVoltageMillivolts))
    {
        Power_FilteredBatteryVoltageMillivolts = Power_RoundBatteryVoltageToOutputStep(BatteryVoltageMillivolts);
    }
}

static void Power_UpdateBatteryDepletedState(uint32_t BatteryVoltageMillivolts)
{
    if(Power_Charging)
    {
        Power_BatteryDepletedAverageCount = 0U;
        Power_BatteryDepleted = false;
        return;
    }

    if(BatteryVoltageMillivolts <= BATTERY_DEPLETED_VOLTAGE_MILLIVOLTS)
    {
        if(Power_BatteryDepletedAverageCount < BATTERY_DEPLETED_CONFIRMATION_AVERAGES)
        {
            Power_BatteryDepletedAverageCount++;
        }

        if(Power_BatteryDepletedAverageCount >= BATTERY_DEPLETED_CONFIRMATION_AVERAGES)
        {
            Power_BatteryDepleted = true;
        }

        return;
    }

    Power_BatteryDepletedAverageCount = 0U;

    if(BatteryVoltageMillivolts >= BATTERY_RECOVERY_VOLTAGE_MILLIVOLTS)
    {
        Power_BatteryDepleted = false;
    }
}

static uint16_t Power_RoundBatteryVoltageToOutputStep(uint32_t BatteryVoltageMillivolts)
{
    uint32_t RoundedBatteryVoltageMillivolts;

    RoundedBatteryVoltageMillivolts =
        ((BatteryVoltageMillivolts + (BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS / 2U)) /
         BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS) *
        BATTERY_VOLTAGE_OUTPUT_STEP_MILLIVOLTS;

    return (RoundedBatteryVoltageMillivolts > UINT16_MAX) ?
        UINT16_MAX : (uint16_t)RoundedBatteryVoltageMillivolts;
}
