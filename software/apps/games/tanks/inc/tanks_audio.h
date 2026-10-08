/**
 * @file tanks_audio.h
 * @brief Generated sound effects and jingles for TANKS.
 *
 * Everything is synthesized while it plays, so the game stores no audio
 * data. Three mixer channels are used:
 *
 * - Ambience: the treads of every moving tank (the player's up close, the
 *   enemies' quieter with distance) and the hiss of rockets in flight.
 * - Effects: shots, rocket launches, ricochets, explosions and mines, mixed
 *   from a small pool of voices so they overlap naturally.
 * - Jingles: short fanfares for a new room, a cleared arena, a lost tank,
 *   game over and victory.
 *
 * Sounds further from the player are quieter. All functions are called
 * from the game loop.
 */

#ifndef TANKS_AUDIO_H
#define TANKS_AUDIO_H

#include "tanks_internal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Jingles, one playing at a time.
 */
typedef enum
{
    TANKS_AUDIO_JINGLE_WAVE_INTRO,
    TANKS_AUDIO_JINGLE_ARENA_CLEAR,
    TANKS_AUDIO_JINGLE_TANK_LOST,
    TANKS_AUDIO_JINGLE_GAME_OVER,
    TANKS_AUDIO_JINGLE_VICTORY,
    TANKS_AUDIO_JINGLE_NONE
} TanksAudio_JingleTypeDef;

/**
 * @brief Start the sound generators, for init and resume.
 */
void TanksAudio_Start(void);

/**
 * @brief Silence every TANKS sound, for pause and shutdown.
 */
void TanksAudio_Stop(void);

/**
 * @brief Follow the game once per frame: tread sounds for moving tanks,
 *        rockets in flight, and beeping mines.
 */
void TanksAudio_Update(uint32_t DeltaMilliseconds);

/**
 * @brief Play a tank firing.
 *
 * @param Position Muzzle position.
 * @param Player   True for the player's tank.
 * @param Rocket   True for a rocket launch.
 */
void TanksAudio_PlayFire(Tanks_VectorTypeDef Position, bool Player, bool Rocket);

/**
 * @brief Play a bullet ricocheting off a wall.
 */
void TanksAudio_PlayRicochet(Tanks_VectorTypeDef Position);

/**
 * @brief Play a bullet fizzling out after its last bounce.
 */
void TanksAudio_PlayBulletSpent(Tanks_VectorTypeDef Position);

/**
 * @brief Play an explosion.
 *
 * @param Position Centre of the blast.
 * @param Strength The game's explosion strength: 7 for a shot-down rocket
 *                 or mine, 11 for a tank, 14 for a mine, 16 for a rocket,
 *                 and 19 for the player's tank.
 */
void TanksAudio_PlayExplosion(Tanks_VectorTypeDef Position, uint8_t Strength);

/**
 * @brief Play a mine being dropped.
 */
void TanksAudio_PlayMineDropped(Tanks_VectorTypeDef Position);

/**
 * @brief Play a mine arming itself.
 */
void TanksAudio_PlayMineArmed(Tanks_VectorTypeDef Position);

/**
 * @brief Play a mine fizzling out harmlessly at the end of its life.
 */
void TanksAudio_PlayMineFizzle(Tanks_VectorTypeDef Position);

/**
 * @brief Play a jingle, replacing any jingle still playing.
 */
void TanksAudio_PlayJingle(TanksAudio_JingleTypeDef Jingle);

#ifdef __cplusplus
}
#endif

#endif /* TANKS_AUDIO_H */
