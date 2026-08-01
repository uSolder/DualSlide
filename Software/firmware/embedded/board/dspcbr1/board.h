/**
 * @file board.h
 * @brief DSPCBR1 board initialization interface.
 */

#ifndef DSPCBR1_BOARD_H
#define DSPCBR1_BOARD_H

#include "adc.h"
#include "display_controller.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOARD_WAKE_REASON_POWER_BUTTON,
    BOARD_WAKE_REASON_EXTERNAL_POWER
} Board_WakeReasonTypeDef;

/**
 * @brief Initializes the board hardware required for system operation.
 */
void Board_Init(void);

/**
 * @brief Returns the reason that caused the board to wake.
 */
Board_WakeReasonTypeDef Board_GetWakeReason(void);

/**
 * @brief Returns the initialized board display-controller handle.
 */
DisplayController_Handle *Board_GetDisplayController(void);

/**
 * @brief Returns the board-assigned ADC input for potentiometer A.
 */
const ADC_InputTypeDef *Board_GetPOTAInput(void);

/**
 * @brief Returns the board-assigned ADC input for potentiometer B.
 */
const ADC_InputTypeDef *Board_GetPOTBInput(void);

/**
 * @brief Returns the board-assigned GPIO input for the primary button.
 */
const GPIO_PinTypeDef *Board_GetPrimaryButtonInput(void);

/**
 * @brief Returns the board-assigned GPIO input for the secondary button.
 */
const GPIO_PinTypeDef *Board_GetSecondaryButtonInput(void);

/**
 * @brief Returns the board-assigned GPIO input for 5V input.
 */
const GPIO_PinTypeDef *Board_GetUSBPowerInput(void);

/**
 * @brief Commands the board to turn off its main power rail.
 */
void Board_PowerOff(void);

#ifdef __cplusplus
}
#endif

#endif