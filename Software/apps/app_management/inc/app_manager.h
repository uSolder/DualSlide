/**
 * @file app_manager.h
 * @brief Controls the currently active DualSlide application.
 */

#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "audio.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the application manager and initial application.
 *
 * @return true when the application was initialised successfully; otherwise
 * false.
 */
bool AppManager_Init(void);

/**
 * @brief Update the active application.
 *
 * @param DeltaTimeMilliseconds Time elapsed since the previous update.
 */
void AppManager_Update(uint32_t DeltaTimeMilliseconds);

/**
 * @brief Render the active application.
 */
void AppManager_Render(void);

/**
 * @brief Fill an audio output buffer for the active application.
 *
 * This function matches Audio_FillCallbackTypeDef exactly and can be passed
 * directly to Audio_Init().
 *
 * Window Washer currently produces no audio, so the requested mono frames are
 * filled with silence.
 *
 * @param Samples Destination PCM sample buffer.
 * @param FrameCount Number of mono audio frames to write.
 * @param Context Optional backend callback context; currently unused.
 */
void AppManager_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context);

/**
 * @brief Pause the active application.
 */
void AppManager_Pause(void);

/**
 * @brief Resume the active application.
 */
void AppManager_Resume(void);

/**
 * @brief Shut down the active application.
 */
void AppManager_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MANAGER_H */