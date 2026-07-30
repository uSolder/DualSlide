/**
 * @file board.h
 * @brief DSPCBR1 board initialization interface.
 */

#ifndef DSPCBR1_BOARD_H
#define DSPCBR1_BOARD_H

#include "display_controller.h"
#include "adc.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    BOARD_WAKE_REASON_POWER_BUTTON,
    BOARD_WAKE_REASON_EXTERNAL_POWER
} Board_WakeReasonTypeDef;

Board_WakeReasonTypeDef Board_GetWakeReason(void);

/**
 * @brief Initialize the board's hardware.
 *
 * Initializes the selected MCU target, peripherals, devices, and interfaces needed for
 * system operation to begin.
 */
void Board_Init(void);


/**
 * @brief Get the board display-controller handle.
 *
 * The returned handle remains owned by the board implementation.
 *
 * @return Pointer to the initialized display-controller handle.
 */
DisplayController_Handle *Board_GetDisplayController(void);

/**
 * @brief Get the board-assigned ADC input for potentiometer A.
 *
 * @return Pointer to the potentiometer A ADC input descriptor.
 */
const ADC_InputTypeDef *Board_GetPOTAInput(void);

/**
 * @brief Get the board-assigned ADC input for potentiometer B.
 *
 * @return Pointer to the potentiometer B ADC input descriptor.
 */
const ADC_InputTypeDef *Board_GetPOTBInput(void);

/**
 * @brief Get the board-assigned GPIO input for the primary button.
 *
 * @return Pointer to the primary button GPIO descriptor.
 */
const GPIO_PinTypeDef *Board_GetPrimaryButtonInput(void);

/**
 * @brief Get the board-assigned GPIO input for the secondary button.
 *
 * @return Pointer to the secondary button GPIO descriptor.
 */
const GPIO_PinTypeDef *Board_GetSecondaryButtonInput(void);


/**
 * @brief Command to power off the board
 *
 */
void Board_PowerOff(void);

#ifdef __cplusplus
}
#endif

#endif /* DSPCBR1_BOARD_H */