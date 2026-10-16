/**
 * @file dualslide_sdk.c
 * @brief The DualSlide game SDK: firmware functions, reached through the game API.
 *
 * Each function here has the name and parameters of the firmware function a
 * game would call if it were built into the firmware, and forwards to the
 * firmware through DualSlide_API. The printf-style functions format with the
 * firmware's FormatTextList, since a table cannot forward "...".
 *
 * The build defines DUALSLIDE_GAME_APP as the name of the game's
 * AppManager_AppTypeDef, which the entry point returns.
 */

#include "dualslide_sdk.h"

#include "controls.h"
#include "display.h"
#include "mixer.h"
#include "music.h"
#include "random.h"
#include "render.h"
#include "save.h"
#include "sound.h"
#include "system.h"
#include "system_time.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DUALSLIDE_GAME_APP
#error "Define DUALSLIDE_GAME_APP as the game's AppManager_AppTypeDef (dualslide_add_game does this)"
#endif

/* Longest line Render_DrawTextf() draws, as in the firmware. */
#define DUALSLIDE_SDK_TEXT_BUFFER_SIZE      (128U)

const GameAPI_TypeDef *DualSlide_API;

extern const AppManager_AppTypeDef DUALSLIDE_GAME_APP;

/* -------------------------------------------------------------------------- */
/* Entry point                                                                */
/* -------------------------------------------------------------------------- */

const AppManager_AppTypeDef *DualSlideGame_Entry(const GameAPI_TypeDef *API);

/* Called by the firmware once the game is loaded. */
const AppManager_AppTypeDef *DualSlideGame_Entry(const GameAPI_TypeDef *API)
{
    /* The firmware checks this too; refusing here keeps a mismatch from going further. */
    static const uint16_t RequiredMinor = GAME_API_VERSION_MINOR;

    if((API == NULL) || (API->VersionMajor != GAME_API_VERSION_MAJOR) || (API->VersionMinor < RequiredMinor))
    {
        return NULL;
    }

    DualSlide_API = API;

    return &DUALSLIDE_GAME_APP;
}

/* -------------------------------------------------------------------------- */
/* render.h                                                                   */
/* -------------------------------------------------------------------------- */

void Render_SetClipRect(const Render_RectTypeDef *Rect)
{
    DualSlide_API->Render->SetClipRect(Rect);
}

void Render_ResetClipRect(void)
{
    DualSlide_API->Render->ResetClipRect();
}

void Render_Clear(Render_TargetTypeDef *Target, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->Clear(Target, Colour);
}

void Render_FillRect(Render_TargetTypeDef *Target, const Render_RectTypeDef *Rect, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->FillRect(Target, Rect, Colour);
}

bool Render_DrawPolygon(Render_TargetTypeDef *Target, const Render_PointTypeDef *Points, uint8_t PointCount, Render_ColourIndexTypeDef Colour)
{
    return DualSlide_API->Render->DrawPolygon(Target, Points, PointCount, Colour);
}

void Render_DrawImage(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t X, int16_t Y)
{
    DualSlide_API->Render->DrawImage(Target, Image, X, Y);
}

void Render_DrawImageRegion(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, const Render_ImageRegionTypeDef *SourceRegion, int16_t X, int16_t Y)
{
    DualSlide_API->Render->DrawImageRegion(Target, Image, SourceRegion, X, Y);
}

void Render_DrawImageRotated(Render_TargetTypeDef *Target, const Render_ImageTypeDef *Image, int16_t CentreX, int16_t CentreY, Render_AngleTypeDef Angle)
{
    DualSlide_API->Render->DrawImageRotated(Target, Image, CentreX, CentreY, Angle);
}

void Render_DrawText(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->DrawText(Target, FontAsset, Text, X, Y, Colour);
}

void Render_Box(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->Box(Target, X, Y, Width, Height, Colour);
}

void Render_DrawRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->DrawRect(Target, X, Y, Width, Height, Thickness, Colour);
}

void Render_FillRoundRect(Render_TargetTypeDef *Target, int16_t X, int16_t Y, uint16_t Width, uint16_t Height, uint16_t Radius, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->FillRoundRect(Target, X, Y, Width, Height, Radius, Colour);
}

void Render_FillCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->FillCircle(Target, CentreX, CentreY, Radius, Colour);
}

void Render_DrawCircle(Render_TargetTypeDef *Target, int16_t CentreX, int16_t CentreY, uint16_t Radius, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->DrawCircle(Target, CentreX, CentreY, Radius, Thickness, Colour);
}

void Render_DrawLine(Render_TargetTypeDef *Target, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint16_t Thickness, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->DrawLine(Target, X1, Y1, X2, Y2, Thickness, Colour);
}

uint16_t Render_TextWidth(const Font *FontAsset, const char *Text)
{
    return DualSlide_API->Render->TextWidth(FontAsset, Text);
}

void Render_DrawTextAligned(Render_TargetTypeDef *Target, const Font *FontAsset, const char *Text, int16_t X, int16_t Y, Render_AlignTypeDef Align, Render_ColourIndexTypeDef Colour)
{
    DualSlide_API->Render->DrawTextAligned(Target, FontAsset, Text, X, Y, Align, Colour);
}

char *Render_FormatTextList(char *Buffer, uint32_t Size, const char *Format, va_list Arguments)
{
    return DualSlide_API->Render->FormatTextList(Buffer, Size, Format, Arguments);
}

char *Render_FormatText(char *Buffer, uint32_t Size, const char *Format, ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    (void)DualSlide_API->Render->FormatTextList(Buffer, Size, Format, Arguments);
    va_end(Arguments);

    return Buffer;
}

void Render_DrawTextf(Render_TargetTypeDef *Target, const Font *FontAsset, int16_t X, int16_t Y, Render_ColourIndexTypeDef Colour, const char *Format, ...)
{
    char Text[DUALSLIDE_SDK_TEXT_BUFFER_SIZE];
    va_list Arguments;

    va_start(Arguments, Format);
    (void)DualSlide_API->Render->FormatTextList(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    DualSlide_API->Render->DrawText(Target, FontAsset, Text, X, Y, Colour);
}

/* -------------------------------------------------------------------------- */
/* controls.h                                                                 */
/* -------------------------------------------------------------------------- */

bool Controls_IsDown(Controls_ButtonTypeDef Button)
{
    return DualSlide_API->Controls->IsDown(Button);
}

bool Controls_WasPressed(Controls_ButtonTypeDef Button)
{
    return DualSlide_API->Controls->WasPressed(Button);
}

bool Controls_WasReleased(Controls_ButtonTypeDef Button)
{
    return DualSlide_API->Controls->WasReleased(Button);
}

uint32_t Controls_HeldMilliseconds(Controls_ButtonTypeDef Button)
{
    return DualSlide_API->Controls->HeldMilliseconds(Button);
}

void Controls_IgnoreHeldButtons(void)
{
    DualSlide_API->Controls->IgnoreHeldButtons();
}

float Controls_Slider(Controls_SliderTypeDef Slider)
{
    return DualSlide_API->Controls->Slider(Slider);
}

int32_t Controls_SliderBetween(Controls_SliderTypeDef Slider, int32_t AtBottom, int32_t AtTop)
{
    return DualSlide_API->Controls->SliderBetween(Slider, AtBottom, AtTop);
}

int32_t Controls_SliderRaw(Controls_SliderTypeDef Slider)
{
    return DualSlide_API->Controls->SliderRaw(Slider);
}

bool Controls_SliderMoved(Controls_SliderTypeDef Slider)
{
    return DualSlide_API->Controls->SliderMoved(Slider);
}

void Controls_ResetSliderMoved(Controls_SliderTypeDef Slider)
{
    DualSlide_API->Controls->ResetSliderMoved(Slider);
}

/* -------------------------------------------------------------------------- */
/* sound.h                                                                    */
/* -------------------------------------------------------------------------- */

Sound_HandleTypeDef Sound_Play(const Sound_TypeDef *Sound)
{
    return DualSlide_API->Sound->Play(Sound);
}

Sound_HandleTypeDef Sound_PlayWith(const Sound_TypeDef *Sound, float Volume, float Pitch)
{
    return DualSlide_API->Sound->PlayWith(Sound, Volume, Pitch);
}

void Sound_Stop(Sound_HandleTypeDef Handle)
{
    DualSlide_API->Sound->Stop(Handle);
}

void Sound_StopAll(void)
{
    DualSlide_API->Sound->StopAll();
}

void Sound_SetVolume(Sound_HandleTypeDef Handle, float Volume)
{
    DualSlide_API->Sound->SetVolume(Handle, Volume);
}

void Sound_SetPitch(Sound_HandleTypeDef Handle, float Pitch)
{
    DualSlide_API->Sound->SetPitch(Handle, Pitch);
}

void Sound_SetControl(Sound_HandleTypeDef Handle, float Value)
{
    DualSlide_API->Sound->SetControl(Handle, Value);
}

bool Sound_IsPlaying(Sound_HandleTypeDef Handle)
{
    return DualSlide_API->Sound->IsPlaying(Handle);
}

/* -------------------------------------------------------------------------- */
/* mixer.h                                                                    */
/* -------------------------------------------------------------------------- */

bool Mixer_PlaySound(Mixer_ChannelTypeDef Channel, const Mixer_SoundTypeDef *Sound, bool Loop)
{
    return DualSlide_API->Mixer->PlaySound(Channel, Sound, Loop);
}

bool Mixer_PlayGenerator(Mixer_ChannelTypeDef Channel, Mixer_GeneratorTypeDef Generator, void *Context)
{
    return DualSlide_API->Mixer->PlayGenerator(Channel, Generator, Context);
}

bool Mixer_PlaySynth(Mixer_ChannelTypeDef Channel, const Mixer_SynthTypeDef *Synth, void *Context)
{
    return DualSlide_API->Mixer->PlaySynth(Channel, Synth, Context);
}

bool Mixer_Send(Mixer_ChannelTypeDef Channel, const void *Message, uint32_t Size)
{
    return DualSlide_API->Mixer->Send(Channel, Message, Size);
}

bool Mixer_Stop(Mixer_ChannelTypeDef Channel)
{
    return DualSlide_API->Mixer->Stop(Channel);
}

bool Mixer_SetVolume(Mixer_ChannelTypeDef Channel, uint16_t Volume)
{
    return DualSlide_API->Mixer->SetVolume(Channel, Volume);
}

bool Mixer_IsPlaying(Mixer_ChannelTypeDef Channel)
{
    return DualSlide_API->Mixer->IsPlaying(Channel);
}

/* -------------------------------------------------------------------------- */
/* music.h                                                                    */
/* -------------------------------------------------------------------------- */

bool Music_Play(Mixer_ChannelTypeDef Channel, const Music_SongTypeDef *Song)
{
    return DualSlide_API->Music->Play(Channel, Song);
}

void Music_Stop(void)
{
    DualSlide_API->Music->Stop();
}

bool Music_IsPlaying(void)
{
    return DualSlide_API->Music->IsPlaying();
}

/* -------------------------------------------------------------------------- */
/* save.h                                                                     */
/* -------------------------------------------------------------------------- */

bool Save_Load(const char *Name, void *Data, uint32_t Size)
{
    return DualSlide_API->Save->Load(Name, Data, Size);
}

bool Save_Store(const char *Name, const void *Data, uint32_t Size)
{
    return DualSlide_API->Save->Store(Name, Data, Size);
}

bool Save_Erase(const char *Name)
{
    return DualSlide_API->Save->Erase(Name);
}

/* -------------------------------------------------------------------------- */
/* display.h                                                                  */
/* -------------------------------------------------------------------------- */

bool Display_SetPalette(uint16_t FirstEntry, const Display_ColourTypeDef *Colours, uint16_t EntryCount)
{
    return DualSlide_API->Display->SetPalette(FirstEntry, Colours, EntryCount);
}

/* -------------------------------------------------------------------------- */
/* system_time.h, random.h, system.h                                          */
/* -------------------------------------------------------------------------- */

uint32_t SystemTime_GetMilliseconds(void)
{
    return DualSlide_API->System->GetMilliseconds();
}

uint32_t Random_Next(void)
{
    return DualSlide_API->System->Random();
}

int32_t Random_Between(int32_t Minimum, int32_t Maximum)
{
    return DualSlide_API->System->RandomBetween(Minimum, Maximum);
}

uint16_t System_GetBatteryMillivolts(void)
{
    return DualSlide_API->System->GetBatteryMillivolts();
}

bool System_IsExternalPowerPresent(void)
{
    return DualSlide_API->System->IsExternalPowerPresent();
}

uint8_t System_GetBrightness(void)
{
    return DualSlide_API->Settings->GetBrightness();
}

void System_SetBrightness(uint8_t Percent)
{
    DualSlide_API->Settings->SetBrightness(Percent);
}

uint8_t System_GetVolume(void)
{
    return DualSlide_API->Settings->GetVolume();
}

void System_SetVolume(uint8_t Percent)
{
    DualSlide_API->Settings->SetVolume(Percent);
}

bool System_SaveSettings(void)
{
    return DualSlide_API->Settings->SaveSettings();
}

bool System_EraseSavedData(void)
{
    return DualSlide_API->Settings->EraseSavedData();
}
