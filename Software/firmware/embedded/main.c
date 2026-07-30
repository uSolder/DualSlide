/**
 * @file main.c
 * @brief DualSlide embedded firmware entry point.
 */

#include "board.h"
#include "system.h"

int main(void)
{
    Board_Init();
    System_Run();

    Board_PowerOff();
    for (;;)
    {
    }
}