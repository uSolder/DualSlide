/**
 * @file music.h
 * @brief Sample-based music player.
 *
 * Songs are lists of notes. Each note names an instrument, a pitch, a start
 * time, a duration, and a velocity. Instruments are single recorded notes
 * (generated from WAV files by tools/music_converter/instrument_converter.py)
 * that the player pitch-shifts to every note it plays, so a complete song
 * costs only its note data plus the instruments it uses. Songs are generated
 * from MIDI files by tools/music_converter/song_converter.py.
 *
 * The player renders up to MUSIC_VOICE_COUNT simultaneous notes and plays on
 * a single mixer channel, so the remaining channels stay free for sound
 * effects. One song plays at a time, and it belongs to the running
 * application: the application manager stops it when the application changes.
 *
 * Pitches are MIDI note numbers: 60 is middle C (shown as C5 in FL Studio,
 * C4 in most other software) and each step is one semitone.
 *
 * All functions must be called from the main loop.
 */

#ifndef MUSIC_H
#define MUSIC_H

#include "audio.h"
#include "mixer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/** Maximum number of notes the player renders at once. */
#define MUSIC_VOICE_COUNT                   (8U)

/**
 * @brief One recorded note, mono 16-bit PCM at MIXER_SAMPLE_RATE_HZ.
 *
 * When LoopEnd is greater than LoopStart, a held note repeats the samples
 * LoopStart to LoopEnd - 1 until it is released. Otherwise the note plays to
 * the end of the recording and stops there, even if it is still held.
 */
typedef struct
{
    const Audio_SampleTypeDef *Samples;
    uint32_t SampleCount;
    uint32_t LoopStart;
    uint32_t LoopEnd;
    uint16_t ReleaseMilliseconds;
    uint8_t RootNote;
} Music_InstrumentTypeDef;

/**
 * @brief One note of a song.
 *
 * Times are in ticks; the song defines the tick length.
 */
typedef struct
{
    uint32_t StartTick;
    uint32_t DurationTicks;
    uint8_t Instrument;
    uint8_t Note;
    uint8_t Velocity;
} Music_NoteTypeDef;

/**
 * @brief A complete song.
 *
 * Notes must be sorted by StartTick. Instrument is an index into Instruments.
 * The song ends, or loops back to the start, at LengthTicks.
 */
typedef struct
{
    const Music_InstrumentTypeDef *const *Instruments;
    uint8_t InstrumentCount;
    const Music_NoteTypeDef *Notes;
    uint32_t NoteCount;
    uint16_t TicksPerQuarterNote;
    uint32_t MicrosecondsPerQuarterNote;
    uint32_t LengthTicks;
    bool Loop;
} Music_SongTypeDef;

/* -------------------------------------------------------------------------- */
/* Playback                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Play a song from the start, replacing any song already playing.
 *
 * Use Mixer_SetVolume() on the same channel to set the music volume.
 *
 * @param Channel Mixer channel for the music.
 * @param Song    Song to play; must remain valid while it plays.
 *
 * @return true if playback was requested; otherwise false.
 */
bool Music_Play(Mixer_ChannelTypeDef Channel, const Music_SongTypeDef *Song);

/**
 * @brief Stop the current song immediately.
 */
void Music_Stop(void);

/**
 * @brief Return whether a song is playing.
 *
 * A song that does not loop stops being reported once its last note has
 * faded out.
 */
bool Music_IsPlaying(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_H */
