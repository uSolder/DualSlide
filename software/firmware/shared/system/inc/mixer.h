/**
 * @file mixer.h
 * @brief Eight-channel audio mixer for applications and the system UI.
 *
 * The mixer owns the system audio stream. Each mixer channel plays one source
 * at a time: a PCM sound clip, a synth, or a raw generator callback. The
 * mixer sums the active channels, applies each channel's volume, and delivers
 * mono PCM at MIXER_SAMPLE_RATE_HZ through Mixer_FillAudioBuffer(), so
 * applications never mix audio themselves.
 *
 * A synth is the usual way to make sound in code. It renders floating-point
 * samples, and the game loop talks to it with Mixer_Send(): each message is
 * copied and handed to the synth's Receive function in the audio context,
 * in order with every other request, so the synth owns all of its state and
 * nothing is shared between the game loop and the audio. See synth.h for
 * building blocks.
 *
 * Mixer channels 0 to MIXER_APPLICATION_CHANNEL_COUNT - 1 belong to the
 * running application and are stopped whenever the active application
 * changes. MIXER_SOUND_CHANNEL carries the sound effects engine (sound.h),
 * and MIXER_SYSTEM_CHANNEL is reserved for system sounds.
 *
 * All functions except Mixer_FillAudioBuffer() must be called from the main
 * loop. Requests take effect when the audio output next asks for samples,
 * within one audio buffer.
 */

#ifndef MIXER_H
#define MIXER_H

#include "audio.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Format and channels                                                        */
/* -------------------------------------------------------------------------- */

/** Mono output sample rate; every sound clip uses this rate. */
#define MIXER_SAMPLE_RATE_HZ                (24000U)

/** Total number of mixer channels. */
#define MIXER_CHANNEL_COUNT                 (8U)

/** Mixer channels 0 to this value - 1 belong to the running application. */
#define MIXER_APPLICATION_CHANNEL_COUNT     (6U)

/** Mixer channel the sound effects engine plays on. */
#define MIXER_SOUND_CHANNEL                 ((Mixer_ChannelTypeDef)6U)

/** Mixer channel reserved for system sounds. */
#define MIXER_SYSTEM_CHANNEL                ((Mixer_ChannelTypeDef)7U)

/** Channel volume that plays a source at its original level. */
#define MIXER_VOLUME_MAX                    (256U)

/** Largest message Mixer_Send() carries, in bytes. */
#define MIXER_MESSAGE_SIZE                  (24U)

/** Mixer channel number, 0 to MIXER_CHANNEL_COUNT - 1. */
typedef uint8_t Mixer_ChannelTypeDef;

/**
 * @brief Mono 16-bit PCM sound clip at MIXER_SAMPLE_RATE_HZ.
 *
 * The sample data must remain valid while any mixer channel plays the clip,
 * which is normally achieved by storing it as a const array.
 */
typedef struct
{
    const Audio_SampleTypeDef *Samples;
    uint32_t SampleCount;
} Mixer_SoundTypeDef;

/**
 * @brief Produce the next mono samples for a mixer channel.
 *
 * Runs in the audio output context, like Audio_FillCallbackTypeDef: it must
 * write every requested sample and must not block, allocate memory, perform
 * file I/O, or wait on a mutex.
 */
typedef void (*Mixer_GeneratorTypeDef)(Audio_SampleTypeDef *Samples, uint32_t SampleCount, void *Context);

/**
 * @brief A synth: the functions the mixer calls for one synth source.
 *
 * Every function runs in the audio output context, with the Context given to
 * Mixer_PlaySynth(), and follows the rules of Mixer_GeneratorTypeDef. Keep the
 * structure itself in const storage; the mixer holds a pointer to it.
 */
typedef struct
{
    /**
     * Called once when Mixer_PlaySynth() takes effect, before any message or
     * sample: reset the synth's state here. May be NULL.
     */
    void (*Start)(void *Context);

    /**
     * Called for each message sent with Mixer_Send(), in the order sent,
     * before the next samples are rendered. Message points to a copy that is
     * valid only during the call. May be NULL.
     */
    void (*Receive)(const void *Message, uint32_t Size, void *Context);

    /**
     * Write SampleCount samples, nominally -1.0 to 1.0. The mixer clips
     * anything outside that range.
     */
    void (*Render)(float *Samples, uint32_t SampleCount, void *Context);
} Mixer_SynthTypeDef;

/* -------------------------------------------------------------------------- */
/* Mixer control                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Reset every mixer channel to silent with full volume.
 *
 * Call before the audio output starts.
 */
void Mixer_Init(void);

/**
 * @brief Play a sound clip on a mixer channel, replacing its current source.
 *
 * @param Channel Mixer channel to use.
 * @param Sound   Clip to play. The structure itself is copied.
 * @param Loop    true to repeat the clip until stopped; false to play it once.
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_PlaySound(Mixer_ChannelTypeDef Channel, const Mixer_SoundTypeDef *Sound, bool Loop);

/**
 * @brief Play a generator on a mixer channel, replacing its current source.
 *
 * The generator runs until the channel is stopped or given another source.
 *
 * @param Channel   Mixer channel to use.
 * @param Generator Sample generator.
 * @param Context   Value passed to every generator call.
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_PlayGenerator(Mixer_ChannelTypeDef Channel, Mixer_GeneratorTypeDef Generator, void *Context);

/**
 * @brief Play a synth on a mixer channel, replacing its current source.
 *
 * The synth's Start function runs first, so playing a synth again restarts
 * it. It then renders until the channel is stopped or given another source.
 *
 * @param Channel Mixer channel to use.
 * @param Synth   The synth's functions; must remain valid while it plays.
 * @param Context Value passed to every synth function call.
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_PlaySynth(Mixer_ChannelTypeDef Channel, const Mixer_SynthTypeDef *Synth, void *Context);

/**
 * @brief Send a message to the synth playing on a mixer channel.
 *
 * The message is copied, so it may be a local variable. It is delivered after
 * every request made before it, including Mixer_PlaySynth(), and is dropped if
 * the channel is not playing a synth with a Receive function by then.
 *
 * @param Channel Mixer channel whose synth receives the message.
 * @param Message Message bytes.
 * @param Size    Message size, at most MIXER_MESSAGE_SIZE.
 *
 * @return true if the message was queued; otherwise false.
 */
bool Mixer_Send(Mixer_ChannelTypeDef Channel, const void *Message, uint32_t Size);

/**
 * @brief Silence a mixer channel.
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_Stop(Mixer_ChannelTypeDef Channel);

/**
 * @brief Silence every application mixer channel.
 *
 * The system channel keeps playing.
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_StopApplicationChannels(void);

/**
 * @brief Set a mixer channel's volume.
 *
 * The volume persists across sources played on the channel.
 *
 * @param Channel Mixer channel to adjust.
 * @param Volume  0 (silent) to MIXER_VOLUME_MAX (original level).
 *
 * @return true if the request was queued; otherwise false.
 */
bool Mixer_SetVolume(Mixer_ChannelTypeDef Channel, uint16_t Volume);

/**
 * @brief Set the master volume, applied to the mix of every channel.
 *
 * @param Volume 0 (silent) to MIXER_VOLUME_MAX (original level).
 *
 * @return true if the volume was accepted; otherwise false.
 */
bool Mixer_SetMasterVolume(uint16_t Volume);

/**
 * @brief Return whether a mixer channel is producing sound.
 *
 * A clip played once stops being reported shortly after its last sample.
 * Requests made since the last audio buffer are not yet reflected.
 */
bool Mixer_IsPlaying(Mixer_ChannelTypeDef Channel);

/* -------------------------------------------------------------------------- */
/* Audio output                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Mix every active channel into a mono audio buffer.
 *
 * Use as the Audio_FillCallbackTypeDef of a mono stream at
 * MIXER_SAMPLE_RATE_HZ.
 */
void Mixer_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context);

#ifdef __cplusplus
}
#endif

#endif /* MIXER_H */
