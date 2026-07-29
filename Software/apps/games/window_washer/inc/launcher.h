/**
 * @file launcher.h
 * @brief DualSlide launcher application interface.
 */

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the launcher.
 *
 * @return true if initialization succeeded; otherwise false.
 */
bool Launcher_Init(void);

/**
 * @brief Samples launcher input and updates launcher state.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void Launcher_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Renders one launcher frame.
 */
void Launcher_Render(void);

/**
 * @brief Pauses the launcher.
 */
void Launcher_Pause(void);

/**
 * @brief Resumes the launcher.
 */
void Launcher_Resume(void);

/**
 * @brief Shuts down the launcher.
 */
void Launcher_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_H */