/**
 * @file window_washer_audio.h
 * @brief Generated sound effects for the Window Washer game.
 *
 * Every sound is synthesized while it plays, so the game stores no audio
 * data:
 *
 * - Cart: rotary rumble with a metallic grind and one soft thud per wheel
 *   turn, louder and faster as the cart moves faster.
 * - Wind: gusting wind that builds slowly through a round.
 * - City: traffic, a soft city roar, two car horns, and one or two bicycle
 *   bells at the start of a round, holding for 3 seconds and then fading out
 *   over 6 seconds.
 * - Squeegee: a rubber-on-glass squeak for every window washed.
 * - Crash: a thud when the cart hits the end of its track, harder at speed.
 * - Yell: the worker's "Aaaah!" as he falls, fading away until the reset.
 */

#ifndef WINDOW_WASHER_AUDIO_H
#define WINDOW_WASHER_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the sounds of a new round: cart, rising wind, and city.
 */
void WindowWasherAudio_StartRound(void);

/**
 * @brief Update the sounds once per game step.
 *
 * @param CartSpeed                Cart speed from 0.0 (stopped) to 1.0
 *                                 (full speed); larger values are clamped.
 * @param RoundElapsedMilliseconds Time since the round started.
 */
void WindowWasherAudio_Update(float CartSpeed, uint64_t RoundElapsedMilliseconds);

/**
 * @brief Play the squeegee squeak for a washed window.
 */
void WindowWasherAudio_PlaySqueegee(void);

/**
 * @brief Play the crash thud and the worker's falling yell.
 *
 * @param ImpactSpeed Cart speed at impact, 0.0 to 1.0; harder hits thud louder.
 */
void WindowWasherAudio_PlayCrash(float ImpactSpeed);

/**
 * @brief Silence every Window Washer sound, for pause and shutdown.
 */
void WindowWasherAudio_Stop(void);

/**
 * @brief Restart the cart and wind after a pause. The city does not return.
 */
void WindowWasherAudio_Resume(void);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_WASHER_AUDIO_H */
