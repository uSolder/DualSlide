/**
 * @file window_washer.h
 * @brief Window Washer game application interface.
 */

#ifndef WINDOW_WASHER_H
#define WINDOW_WASHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Window Washer game.
 *
 * @return true if initialization succeeded; otherwise false.
 */
bool WindowWasher_Init(void);

/**
 * @brief Updates game input and elapsed-time state.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void WindowWasher_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Updates game simulation and renders the next frame.
 */
void WindowWasher_Render(void);

/**
 * @brief Pauses the game.
 */
void WindowWasher_Pause(void);

/**
 * @brief Resumes the game.
 */
void WindowWasher_Resume(void);

/**
 * @brief Shuts down the game.
 */
void WindowWasher_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_WASHER_H */