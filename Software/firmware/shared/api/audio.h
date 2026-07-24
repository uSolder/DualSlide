/**
 * @file audio.h
 * @brief Platform-neutral PCM audio-output contract.
 *
 * The application supplies signed 16-bit PCM frames through a callback.  The
 * platform backend owns the device, DAC, DMA, buffering, and timing details.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A single signed 16-bit PCM sample. */
typedef int16_t Audio_SampleTypeDef;

/**
 * @brief Fill an interleaved PCM output buffer.
 *
 * @p FrameCount is a count of audio frames, not individual samples.  For a
 * stereo stream, each frame contains left then right samples.  The callback
 * must write every requested frame.
 *
 * The callback may execute in a time-critical backend context.  It must not
 * block, allocate memory, perform file I/O, or wait on a mutex.
 */
typedef void (*Audio_FillCallbackTypeDef)(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context);

/** Audio stream settings fixed for the lifetime of one initialisation. */
typedef struct
{
    uint32_t SampleRateHz;
    uint8_t ChannelCount;
    Audio_FillCallbackTypeDef FillCallback;
    void *CallbackContext;
} Audio_ConfigTypeDef;

/**
 * @brief Initialise the platform audio backend.
 *
 * @p Config must describe signed 16-bit PCM with one or more interleaved
 * channels.  Reinitialise only after calling Audio_Stop().
 *
 * @return true when the backend is ready to start playback; otherwise false.
 */
bool Audio_Init(const Audio_ConfigTypeDef *Config);

/**
 * @brief Start continuous PCM playback.
 *
 * The backend invokes the configured fill callback whenever it needs more
 * audio frames.
 *
 * @return true when playback started; otherwise false.
 */
bool Audio_Start(void);

/**
 * @brief Stop playback and release any active backend stream resources.
 */
void Audio_Stop(void);

/**
 * @brief Return whether the audio backend is currently playing.
 */
bool Audio_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */