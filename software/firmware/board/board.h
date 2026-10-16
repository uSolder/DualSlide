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
DisplayController_HandleTypeDef *Board_GetDisplayController(void);

/**
 * @brief Returns the board-assigned ADC input for potentiometer A.
 */
const ADC_InputTypeDef *Board_GetPOTAInput(void);

/**
 * @brief Returns the board-assigned ADC input for potentiometer B.
 */
const ADC_InputTypeDef *Board_GetPOTBInput(void);

/**
 * @brief Return whether the primary button is pressed right now.
 */
bool Board_IsPrimaryButtonDown(void);

/**
 * @brief Return whether the secondary button is pressed right now.
 */
bool Board_IsSecondaryButtonDown(void);

/**
 * @brief Sets the LCD backlight level.
 *
 * Levels above off map onto the duty range the backlight driver can follow,
 * so even the lowest level stays lit.
 *
 * @param Permille 0 (off), or 1 (dimmest) to 1000 (full); larger values are clamped.
 */
void Board_SetBacklightPermille(uint16_t Permille);

/**
 * @brief Commands the board to turn off its main power rail.
 */
void Board_PowerOff(void);

#ifdef __cplusplus
}
#endif

#endif /* DSPCBR1_BOARD_H */
