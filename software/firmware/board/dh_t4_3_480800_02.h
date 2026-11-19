/**
 * @file dh_t4_3_480800_02.h
 * @brief DH_T4-3_480800-02 LCD panel driver interface.
 *
 * A 4.3" 480x800 ST7701S panel with the same interface as W430WVC004-A;
 * board.c chooses between the two with BOARD_LCD_DH_T4_3_480800_02.
 */

#ifndef DH_T4_3_480800_02_H
#define DH_T4_3_480800_02_H

#include "st7701s.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DH_T4_3_480800_02_WIDTH  480U
#define DH_T4_3_480800_02_HEIGHT 800U

typedef enum
{
    DH_T4_3_480800_02_RESULT_OK = 0,
    DH_T4_3_480800_02_RESULT_INVALID_ARGUMENT,
    DH_T4_3_480800_02_RESULT_TIMEOUT,
    DH_T4_3_480800_02_RESULT_BUSY,
    DH_T4_3_480800_02_RESULT_UNSUPPORTED,
    DH_T4_3_480800_02_RESULT_IO_ERROR
} DH_T4_3_480800_02_ResultTypeDef;

typedef struct
{
    ST7701S_HandleTypeDef *Controller;
} DH_T4_3_480800_02_HandleTypeDef;

DH_T4_3_480800_02_ResultTypeDef DH_T4_3_480800_02_Init(DH_T4_3_480800_02_HandleTypeDef *Handle);

#ifdef __cplusplus
}
#endif

#endif /* DH_T4_3_480800_02_H */
