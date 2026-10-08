/**
 * @file usb_audio.h
 * @brief USB Audio Class 1.0 speaker service.
 *
 * The service enumerates as a standard USB speaker accepting 48 kHz, 16-bit,
 * stereo PCM, so any host can use it without a custom driver. Received audio
 * is converted to 24 kHz mono and queued in a jitter buffer that the audio
 * output drains through USBAudio_FillAudioBuffer().
 *
 * The device runs from its own sample clock, so the jitter buffer absorbs the
 * small rate difference to the host by occasionally dropping or repeating a
 * sample.
 */

#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include "audio.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sample rate advertised to the USB host. */
#define USB_AUDIO_HOST_SAMPLE_RATE_HZ       (48000U)

/** Interleaved channel count advertised to the USB host (left, right). */
#define USB_AUDIO_HOST_CHANNEL_COUNT        (2U)

/** Sample rate produced by USBAudio_FillAudioBuffer(). */
#define USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ     (24000U)

/** Channel count produced by USBAudio_FillAudioBuffer(). */
#define USB_AUDIO_OUTPUT_CHANNEL_COUNT      (1U)

/**
 * @brief Register the USB speaker and attach it to the bus.
 *
 * The platform USB device controller must already be initialized.
 *
 * @return true on success; otherwise false.
 */
bool USBAudio_Init(void);

/**
 * @brief Detach the USB speaker from the bus.
 */
void USBAudio_Deinit(void);

/**
 * @brief Return whether the host has selected the streaming interface.
 */
bool USBAudio_IsStreaming(void);

/**
 * @brief Fill an audio output buffer from the USB jitter buffer.
 *
 * Writes FrameCount mono samples at USB_AUDIO_OUTPUT_SAMPLE_RATE_HZ. Silence
 * is written while the host is not streaming or the jitter buffer is
 * refilling. Matches Audio_FillCallbackTypeDef.
 */
void USBAudio_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_H */
