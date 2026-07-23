#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Audio stream contract
 *
 * The stream outputs signed 16-bit PCM frames continuously.
 *
 * A frame contains one sample for each channel:
 * - Mono:   FrameCount int16_t values
 * - Stereo: FrameCount * 2 int16_t values, interleaved L/R
 *
 * The implementation owns its hardware buffer, DMA, timer, cache handling,
 * DAC/I2S/etc. The callback supplies the next PCM frames when needed.
 */

typedef void (*Audio_StreamFillCallback)(
    int16_t *Buffer,
    uint32_t FrameCount,
    void *Context);

typedef struct Audio_StreamConfig
{
    uint32_t SampleRate;
    uint8_t ChannelCount;

    Audio_StreamFillCallback FillCallback;
    void *CallbackContext;
} Audio_StreamConfig;

/*
 * Configures the target's sole audio output stream.
 *
 * The callback must fill every requested frame before returning.
 * It must not block, allocate memory, perform file access, or call
 * non-interrupt-safe RTOS functions.
 */
bool Audio_Stream_Init(const Audio_StreamConfig *Config);

/* Starts continuous PCM output. */
bool Audio_Stream_Start(void);

/* Stops PCM output and releases/turns off the active hardware stream. */
void Audio_Stream_Stop(void);

/* Returns true while the stream is actively producing PCM output. */
bool Audio_Stream_IsRunning(void);

#endif /* AUDIO_STREAM_H */