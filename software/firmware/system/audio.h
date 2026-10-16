/**
 * @file audio.h
 * @brief PCM audio output through the board's speaker.
 *
 * The mixer supplies signed 16-bit PCM frames through a callback; audio.c
 * handles the DAC, DMA, buffering and timing.
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
 * The callback may execute in a time-critical interrupt context.  It must not
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
 * @brief Initialise audio output.
 *
 * @p Config must describe signed 16-bit PCM with one or more interleaved
 * channels.  Reinitialise only after calling Audio_Stop().
 *
 * @return true when audio output is ready to start playback; otherwise false.
 */
bool Audio_Init(const Audio_ConfigTypeDef *Config);

/**
 * @brief Start continuous PCM playback.
 *
 * Audio output invokes the configured fill callback whenever it needs more
 * audio frames.
 *
 * @return true when playback started; otherwise false.
 */
bool Audio_Start(void);

/**
 * @brief Stop playback and release the output stream.
 */
void Audio_Stop(void);

/**
 * @brief Return whether audio output is currently playing.
 */
bool Audio_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
