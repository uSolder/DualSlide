/**
 * @file audio_stream.h
 * @brief Hardware-independent audio output stream contract.
 *
 * The stream outputs signed 16-bit PCM frames continuously.
 *
 * A frame contains one sample for each channel:
 * - Mono:   FrameCount int16_t values
 * - Stereo: FrameCount * 2 int16_t values, interleaved L/R
 *
 * The implementation owns its hardware buffer, DMA, timer, cache handling,
 * DAC/I2S/etc. The callback supplies the next PCM frames when needed. A
 * target with fewer physical outputs than requested channels mixes them down.
 */

#ifndef TARGET_API_AUDIO_STREAM_H
#define TARGET_API_AUDIO_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

typedef void (*AudioStream_FillCallbackTypeDef)(
    int16_t *Buffer,
    uint32_t FrameCount,
    void *Context);

typedef struct AudioStream_ConfigTypeDef
{
    uint32_t SampleRate;
    uint8_t ChannelCount;

    AudioStream_FillCallbackTypeDef FillCallback;
    void *CallbackContext;
} AudioStream_ConfigTypeDef;

/* -------------------------------------------------------------------------- */
/* Stream control                                                             */
/* -------------------------------------------------------------------------- */

/*
 * Holds the output at its resting level so the amplifier input is never left
 * floating, where it picks up noise and buzzes. Call as early as possible at
 * power-up; AudioStream_Init later moves the output smoothly from here to its
 * idle level.
 */
void AudioStream_HoldOutput(void);

/*
 * Configures the target's sole audio output stream.
 *
 * The callback must fill every requested frame before returning.
 * It must not block, allocate memory, perform file access, or call
 * non-interrupt-safe RTOS functions.
 */
bool AudioStream_Init(const AudioStream_ConfigTypeDef *Config);

/* Starts continuous PCM output. */
bool AudioStream_Start(void);

/* Stops PCM output and releases/turns off the active hardware stream. */
void AudioStream_Stop(void);

/* Returns true while the stream is actively producing PCM output. */
bool AudioStream_IsRunning(void);

/*
 * Stops the stream and moves the output smoothly down to its resting level,
 * where it holds, so removing power or resetting does not pop the speaker.
 * Call before powering down; AudioStream_Init is needed to play again.
 */
void AudioStream_PowerDown(void);

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

/*
 * Handles the stream's buffer interrupt and requests more frames from the
 * fill callback. Intended to be called directly by the interrupt handler of
 * the stream's DMA channel in the target interrupt-vector file.
 */
void AudioStream_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_AUDIO_STREAM_H */
