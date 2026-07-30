/**
 * @file system_tasks.c
 * @brief Embedded target system task processing implementation.
 *
 * The current bare-metal target has no background platform services to process.
 * This implementation therefore keeps the application running indefinitely.
 */

#include "system_tasks.h"
#include <stdbool.h>

bool SystemTasks_Process(void)
{
    return true;
}

void SystemTasks_PowerOff(void)
{  
    // nothing to be done here
}