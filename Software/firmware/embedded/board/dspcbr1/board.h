/**
 * @file board.h
 * @brief DSPCBR1 board initialization interface.
 */

#ifndef DSPCBR1_BOARD_H
#define DSPCBR1_BOARD_H

#include "display_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the DSPCBR1 board.
 *
 * Initializes the selected MCU target and all board-level hardware required
 * before the shared firmware system starts.
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

#ifdef __cplusplus
}
#endif

#endif /* DSPCBR1_BOARD_H */