/**
 * @file tug_audio.h
 * @brief Sound effects for Canal Tug.
 */

#ifndef TUG_AUDIO_H
#define TUG_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* The engines: a steady chug that follows how hard they're working (0 to 1). */
void TugAudio_StartEngines(void);
void TugAudio_SetEngines(float Load);
void TugAudio_StopEngines(void);

/* Bumping into something; Strength 0 for a nudge, 1 for a crash. */
void TugAudio_PlayKnock(float Strength);

void TugAudio_PlayHorn(void);
void TugAudio_PlayDoubleHorn(void);
void TugAudio_PlayHook(void);
void TugAudio_PlayPaid(void);
void TugAudio_PlayWrecked(void);
void TugAudio_PlayPurchase(void);
void TugAudio_PlaySelect(void);

#ifdef __cplusplus
}
#endif

#endif /* TUG_AUDIO_H */
