/**
 * @file game_api.c
 * @brief The firmware's game API tables, handed to every loaded game.
 */

#include "game_api.h"

#include "firmware_version.h"
#include "open_sans.h"
#include "open_sans_bold.h"
#include "random.h"
#include "save.h"
#include "system.h"
#include "system_time.h"

static const GameAPI_RenderTypeDef GameAPI_Render =
{
    .SetClipRect = Render_SetClipRect,
    .ResetClipRect = Render_ResetClipRect,
    .Clear = Render_Clear,
    .FillRect = Render_FillRect,
    .DrawPolygon = Render_DrawPolygon,
    .DrawImage = Render_DrawImage,
    .DrawImageRegion = Render_DrawImageRegion,
    .DrawImageRotated = Render_DrawImageRotated,
    .DrawText = Render_DrawText,
    .Box = Render_Box,
    .DrawRect = Render_DrawRect,
    .FillRoundRect = Render_FillRoundRect,
    .FillCircle = Render_FillCircle,
    .DrawCircle = Render_DrawCircle,
    .DrawLine = Render_DrawLine,
    .TextWidth = Render_TextWidth,
    .DrawTextAligned = Render_DrawTextAligned,
    .FormatTextList = Render_FormatTextList
};

static const GameAPI_ControlsTypeDef GameAPI_Controls =
{
    .IsDown = Controls_IsDown,
    .WasPressed = Controls_WasPressed,
    .WasReleased = Controls_WasReleased,
    .HeldMilliseconds = Controls_HeldMilliseconds,
    .IgnoreHeldButtons = Controls_IgnoreHeldButtons,
    .Slider = Controls_Slider,
    .SliderBetween = Controls_SliderBetween,
    .SliderRaw = Controls_SliderRaw,
    .SliderMoved = Controls_SliderMoved,
    .ResetSliderMoved = Controls_ResetSliderMoved
};

static const GameAPI_SoundTypeDef GameAPI_Sound =
{
    .Play = Sound_Play,
    .PlayWith = Sound_PlayWith,
    .Stop = Sound_Stop,
    .StopAll = Sound_StopAll,
    .SetVolume = Sound_SetVolume,
    .SetPitch = Sound_SetPitch,
    .SetControl = Sound_SetControl,
    .IsPlaying = Sound_IsPlaying
};

static const GameAPI_MixerTypeDef GameAPI_Mixer =
{
    .PlaySound = Mixer_PlaySound,
    .PlayGenerator = Mixer_PlayGenerator,
    .PlaySynth = Mixer_PlaySynth,
    .Send = Mixer_Send,
    .Stop = Mixer_Stop,
    .SetVolume = Mixer_SetVolume,
    .IsPlaying = Mixer_IsPlaying
};

static const GameAPI_MusicTypeDef GameAPI_Music =
{
    .Play = Music_Play,
    .Stop = Music_Stop,
    .IsPlaying = Music_IsPlaying
};

static const GameAPI_SaveTypeDef GameAPI_Save =
{
    .Load = Save_Load,
    .Store = Save_Store,
    .Erase = Save_Erase
};

static const GameAPI_DisplayTypeDef GameAPI_Display =
{
    .SetPalette = Display_SetPalette
};

static const GameAPI_FontsTypeDef GameAPI_Fonts =
{
    .OpenSans12 = &OpenSans12,
    .OpenSans16 = &OpenSans16,
    .OpenSans20 = &OpenSans20,
    .OpenSans28 = &OpenSans28,
    .OpenSans36 = &OpenSans36,
    .OpenSansBold12 = &OpenSansBold12,
    .OpenSansBold16 = &OpenSansBold16,
    .OpenSansBold20 = &OpenSansBold20,
    .OpenSansBold28 = &OpenSansBold28,
    .OpenSansBold36 = &OpenSansBold36
};

static const GameAPI_SystemTypeDef GameAPI_System =
{
    .GetMilliseconds = SystemTime_GetMilliseconds,
    .Random = Random_Next,
    .RandomBetween = Random_Between,
    .GetBatteryMillivolts = System_GetBatteryMillivolts,
    .IsExternalPowerPresent = System_IsExternalPowerPresent
};

static const GameAPI_SettingsTypeDef GameAPI_Settings =
{
    .GetBrightness = System_GetBrightness,
    .SetBrightness = System_SetBrightness,
    .GetVolume = System_GetVolume,
    .SetVolume = System_SetVolume,
    .SaveSettings = System_SaveSettings,
    .EraseSavedData = System_EraseSavedData
};

const GameAPI_TypeDef GameAPI_Firmware =
{
    .VersionMajor = GAME_API_VERSION_MAJOR,
    .VersionMinor = GAME_API_VERSION_MINOR,
    .FirmwareMajor = FIRMWARE_VERSION_MAJOR,
    .FirmwareMinor = FIRMWARE_VERSION_MINOR,
    .FirmwarePatch = FIRMWARE_VERSION_PATCH,
    .Reserved = 0U,

    .Render = &GameAPI_Render,
    .Controls = &GameAPI_Controls,
    .Sound = &GameAPI_Sound,
    .Mixer = &GameAPI_Mixer,
    .Music = &GameAPI_Music,
    .Save = &GameAPI_Save,
    .Display = &GameAPI_Display,
    .Fonts = &GameAPI_Fonts,
    .System = &GameAPI_System,
    .Settings = &GameAPI_Settings
};
