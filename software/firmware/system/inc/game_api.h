/**
 * @file game_api.h
 * @brief The interface between the firmware and games loaded from game files.
 *
 * A game loaded from a game file (game_file.h) is built separately from the
 * firmware, so it cannot call firmware functions directly. Instead its entry
 * point receives a GameAPI_TypeDef: tables of pointers to the firmware's
 * render, controls, sound, music, save and display functions, plus the
 * shared fonts. The entry point returns the game's AppManager_AppTypeDef.
 *
 * Game authors never use these tables themselves. The game SDK provides the
 * usual functions (Render_Box(), Controls_WasPressed() and so on), which call
 * through the tables, so a game's source code is the same whether it is built
 * into the firmware or into a game file.
 *
 * VERSIONING
 *
 *   The API carries a MAJOR.MINOR version, separate from the firmware version.
 *
 *   MINOR  is raised when something is added. Functions are only ever added
 *          at the end of a table, and tables at the end of GameAPI_TypeDef,
 *          so a game built for an earlier minor version finds everything
 *          where it expects. The firmware runs any game whose minor version
 *          is not newer than its own.
 *
 *   MAJOR  is raised when anything changes or is removed: a function's
 *          parameters, any structure games share with the firmware (render
 *          targets, images, sounds, synths, songs, AppManager_AppTypeDef),
 *          or any behaviour games rely on. The firmware only runs games of
 *          its own major version.
 *
 *   When the API version changes, GAME_API_MINIMUM_FIRMWARE_* is set to the
 *   firmware release that introduces it. Every game file records it, so a
 *   DualSlide that is too old can tell the player which update it needs.
 *
 *   The firmware reads AppManager_AppTypeDef as it was defined in the game's
 *   own API version; members added later are treated as absent.
 *
 * RULES FOR LOADED GAMES
 *
 *   - Mixer channels 0 to MIXER_APPLICATION_CHANNEL_COUNT - 1 are the game's.
 *   - Everything the game started (sounds, music, mixer channels) is stopped
 *     before the game is unloaded, so no callback can reach unloaded code.
 *   - Save names are private to each game.
 */

#ifndef GAME_API_H
#define GAME_API_H

#include "app_manager.h"
#include "controls.h"
#include "display.h"
#include "font.h"
#include "mixer.h"
#include "music.h"
#include "render.h"
#include "sound.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Version                                                                    */
/* -------------------------------------------------------------------------- */

/** Version of the API described in this file. */
#define GAME_API_VERSION_MAJOR              (1U)
#define GAME_API_VERSION_MINOR              (0U)

/** First firmware release that provides this API version. */
#define GAME_API_MINIMUM_FIRMWARE_MAJOR     (1U)
#define GAME_API_MINIMUM_FIRMWARE_MINOR     (0U)
#define GAME_API_MINIMUM_FIRMWARE_PATCH     (0U)

/* -------------------------------------------------------------------------- */
/* Tables                                                                     */
/* -------------------------------------------------------------------------- */

/** Drawing (render.h). The printf-style functions are built by the SDK on FormatTextList. */
typedef struct
{
    void (*SetClipRect)(const Render_RectTypeDef *Rect);
    void (*ResetClipRect)(void);
    void (*Clear)(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour);
    void (*FillRect)(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour);
    bool (*DrawPolygon)(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour);
    void (*DrawImage)(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y);
    void (*DrawImageRegion)(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y);
    void (*DrawImageRotated)(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle);
    void (*DrawText)(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour);
    void (*Box)(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour);
    void (*DrawRect)(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Thickness, Render_ColourIndexTypeDef Colour);
    void (*FillRoundRect)(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Radius, Render_ColourIndexTypeDef Colour);
    void (*FillCircle)(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, Render_ColourIndexTypeDef Colour);
    void (*DrawCircle)(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, uint16_t Thickness, Render_ColourIndexTypeDef Colour);
    void (*DrawLine)(Render_TargetTypeDef *Target, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint16_t Thickness, Render_ColourIndexTypeDef Colour);
    uint16_t (*TextWidth)(const Font *FontAsset, const char *Text);
    void (*DrawTextAligned)(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_AlignTypeDef Align, Render_ColourIndexTypeDef Colour);
    char *(*FormatTextList)(char *Buffer, uint32_t Size, const char *Format, va_list Arguments);
} GameAPI_RenderTypeDef;

/** Buttons and sliders (controls.h). */
typedef struct
{
    bool (*IsDown)(Controls_ButtonTypeDef Button);
    bool (*WasPressed)(Controls_ButtonTypeDef Button);
    bool (*WasReleased)(Controls_ButtonTypeDef Button);
    uint32_t (*HeldMilliseconds)(Controls_ButtonTypeDef Button);
    void (*IgnoreHeldButtons)(void);
    float (*Slider)(Controls_SliderTypeDef Slider);
    int32_t (*SliderBetween)(Controls_SliderTypeDef Slider, int32_t AtBottom, int32_t AtTop);
    int32_t (*SliderRaw)(Controls_SliderTypeDef Slider);
    bool (*SliderMoved)(Controls_SliderTypeDef Slider);
    void (*ResetSliderMoved)(Controls_SliderTypeDef Slider);
} GameAPI_ControlsTypeDef;

/** Sound effects (sound.h). */
typedef struct
{
    Sound_HandleTypeDef (*Play)(const Sound_TypeDef *Sound);
    Sound_HandleTypeDef (*PlayWith)(const Sound_TypeDef *Sound, float Volume, float Pitch);
    void (*Stop)(Sound_HandleTypeDef Handle);
    void (*StopAll)(void);
    void (*SetVolume)(Sound_HandleTypeDef Handle, float Volume);
    void (*SetPitch)(Sound_HandleTypeDef Handle, float Pitch);
    void (*SetControl)(Sound_HandleTypeDef Handle, float Value);
    bool (*IsPlaying)(Sound_HandleTypeDef Handle);
} GameAPI_SoundTypeDef;

/** Mixer channels for a game's own synths and samples (mixer.h). */
typedef struct
{
    bool (*PlaySound)(Mixer_ChannelTypeDef Channel, const Mixer_SoundTypeDef *Sound, bool Loop);
    bool (*PlayGenerator)(Mixer_ChannelTypeDef Channel, Mixer_GeneratorTypeDef Generator, void *Context);
    bool (*PlaySynth)(Mixer_ChannelTypeDef Channel, const Mixer_SynthTypeDef *Synth, void *Context);
    bool (*Send)(Mixer_ChannelTypeDef Channel, const void *Message, uint32_t Size);
    bool (*Stop)(Mixer_ChannelTypeDef Channel);
    bool (*SetVolume)(Mixer_ChannelTypeDef Channel, uint16_t Volume);
    bool (*IsPlaying)(Mixer_ChannelTypeDef Channel);
} GameAPI_MixerTypeDef;

/** Music (music.h). */
typedef struct
{
    bool (*Play)(Mixer_ChannelTypeDef Channel, const Music_SongTypeDef *Song);
    void (*Stop)(void);
    bool (*IsPlaying)(void);
} GameAPI_MusicTypeDef;

/** Saved data (save.h). Names are private to the game. */
typedef struct
{
    bool (*Load)(const char *Name, void *Data, uint32_t Size);
    bool (*Store)(const char *Name, const void *Data, uint32_t Size);
    bool (*Erase)(const char *Name);
} GameAPI_SaveTypeDef;

/** Display palette (display.h). */
typedef struct
{
    bool (*SetPalette)(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount);
} GameAPI_DisplayTypeDef;

/** Fonts built into the firmware (open_sans.h, open_sans_bold.h). */
typedef struct
{
    const Font *OpenSans12;
    const Font *OpenSans16;
    const Font *OpenSans20;
    const Font *OpenSans28;
    const Font *OpenSans36;
    const Font *OpenSansBold12;
    const Font *OpenSansBold16;
    const Font *OpenSansBold20;
    const Font *OpenSansBold28;
    const Font *OpenSansBold36;
} GameAPI_FontsTypeDef;

/** The device itself (system.h, random.h). */
typedef struct
{
    /** Milliseconds since the device started; wraps after about 49 days. */
    uint32_t (*GetMilliseconds)(void);

    /** A random 32-bit value, seeded from hardware randomness. */
    uint32_t (*Random)(void);

    /** A random whole number from Minimum to Maximum, both included, each equally likely. */
    int32_t (*RandomBetween)(int32_t Minimum, int32_t Maximum);

    /** Battery voltage in millivolts, or 0 before it is known. Varies with load and charging. */
    uint16_t (*GetBatteryMillivolts)(void);

    /** Whether external (USB) power is connected. */
    bool (*IsExternalPowerPresent)(void);
} GameAPI_SystemTypeDef;

/**
 * @brief Everything the firmware offers a game.
 *
 * Version members describe the firmware the game is running on, which may
 * be newer than the one it was built for.
 */
typedef struct
{
    uint16_t VersionMajor;                  /* API version, GAME_API_VERSION_* */
    uint16_t VersionMinor;
    uint16_t FirmwareMajor;                 /* Firmware version, FIRMWARE_VERSION_* */
    uint16_t FirmwareMinor;
    uint16_t FirmwarePatch;
    uint16_t Reserved;

    const GameAPI_RenderTypeDef *Render;
    const GameAPI_ControlsTypeDef *Controls;
    const GameAPI_SoundTypeDef *Sound;
    const GameAPI_MixerTypeDef *Mixer;
    const GameAPI_MusicTypeDef *Music;
    const GameAPI_SaveTypeDef *Save;
    const GameAPI_DisplayTypeDef *Display;
    const GameAPI_FontsTypeDef *Fonts;
    const GameAPI_SystemTypeDef *System;
} GameAPI_TypeDef;

/**
 * @brief A game file's entry point.
 *
 * Called once after the game is loaded and before anything else. The game
 * keeps API for its whole run and returns its application, which must stay
 * valid until it is unloaded. Returning NULL means the game cannot run.
 */
typedef const AppManager_AppTypeDef *(*GameAPI_EntryTypeDef)(const GameAPI_TypeDef *API);

/** The firmware's API, handed to every loaded game. */
extern const GameAPI_TypeDef GameAPI_Firmware;

#ifdef __cplusplus
}
#endif

#endif /* GAME_API_H */
