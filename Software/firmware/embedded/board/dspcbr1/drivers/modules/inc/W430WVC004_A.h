/**
 * @file w430wvc004_a.h
 * @brief W430WVC004-A LCD panel driver interface.
 */

#ifndef W430WVC004_A_H
#define W430WVC004_A_H

#include "st7701s.h"

#ifdef __cplusplus
extern "C" {
#endif

#define W430WVC004_A_WIDTH  480U
#define W430WVC004_A_HEIGHT 800U

typedef enum
{
    W430WVC004_A_RESULT_OK = 0,
    W430WVC004_A_RESULT_INVALID_ARGUMENT,
    W430WVC004_A_RESULT_TIMEOUT,
    W430WVC004_A_RESULT_BUSY,
    W430WVC004_A_RESULT_UNSUPPORTED,
    W430WVC004_A_RESULT_IO_ERROR
} W430WVC004_A_Result;

typedef struct
{
    ST7701S_Handle *controller;
} W430WVC004_A_Handle;

W430WVC004_A_Result W430WVC004_A_Init(W430WVC004_A_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* W430WVC004_A_H */