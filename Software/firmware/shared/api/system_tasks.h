/**
 * @file system_tasks.h
 * @brief Target-specific system task processing contract.
 */

#ifndef SYSTEM_TASKS_H
#define SYSTEM_TASKS_H

#include <stdbool.h>

/**
 * @brief Processes pending target-specific system tasks.
 *
 * On Windows, this processes SDL events and returns false when the
 * application should terminate.
 *
 * On embedded targets, this may process platform services and normally
 * returns true while the system should continue running.
 *
 * @return true if execution should continue.
 * @return false if the system should stop.
 */
bool SystemTasks_Process(void);

#endif