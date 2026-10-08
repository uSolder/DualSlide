/**
 * @file pong_audio.h
 * @brief Generated sound effects for Pong.
 *
 * Every sound is synthesized while it plays, so the game stores no audio
 * data. Each one is a short sequence of soft FM tones:
 *
 * - Paddle: a clean "pok", higher pitched as the ball speeds up; the right
 *   paddle sits a tone above the left.
 * - Wall: a softer, shorter "tik".
 * - Shield: a buzzing energy-field "thwum" when a shield returns the ball.
 * - Miss: a calm, falling "da-dum".
 * - Power-ups: one sound per type, such as a rising arpeggio for expand and
 *   a falling one for shrink.
 * - Expire: a soft, subtle "bloop" when a power-up wears off.
 */

#ifndef PONG_AUDIO_H
#define PONG_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power-up sounds.
 */
typedef enum
{
    PONG_AUDIO_POWER_UP_EXPAND,
    PONG_AUDIO_POWER_UP_SHIELD,
    PONG_AUDIO_POWER_UP_SHRINK,
    PONG_AUDIO_POWER_UP_POWER,
    PONG_AUDIO_POWER_UP_INVERT
} PongAudio_PowerUpTypeDef;

/**
 * @brief Play a paddle hit.
 *
 * @param Left  True for the left paddle, false for the right.
 * @param Speed Ball speed as a fraction of the normal maximum; a power shot
 *              goes up to 1.5.
 */
void PongAudio_PlayPaddle(bool Left, float Speed);

/**
 * @brief Play the ball bouncing off the top or bottom wall.
 *
 * @param Speed Ball speed as a fraction of the normal maximum.
 */
void PongAudio_PlayWall(float Speed);

/**
 * @brief Play a shield returning the ball.
 */
void PongAudio_PlayShield(void);

/**
 * @brief Play a missed ball.
 */
void PongAudio_PlayMiss(void);

/**
 * @brief Play a collected power-up.
 */
void PongAudio_PlayPowerUp(PongAudio_PowerUpTypeDef PowerUp);

/**
 * @brief Play the subtle cue for a power-up wearing off.
 */
void PongAudio_PlayExpire(void);

/**
 * @brief Silence every Pong sound, for pause and shutdown.
 */
void PongAudio_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PONG_AUDIO_H */
