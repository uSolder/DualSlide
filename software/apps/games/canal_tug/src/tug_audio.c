/**
 * @file tug_audio.c
 * @brief Sound effects for Canal Tug.
 *
 * Every sound is a table of layers played by the sound engine (sound.h). The
 * engines are one looping sound, a muffled saw and a breath of noise pulsing
 * like a diesel, whose pitch and volume follow the throttles.
 */

#include "tug_audio.h"

#include "sound.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Overall level of every Canal Tug sound. */
#define TA_MASTER_LEVEL                     (0.4f)

/*
 * Engines: idle and flat-out pitch and volume. The chug is filtered to the
 * middle of the range: deep bass only pushes a small speaker to its limits.
 */
#define TA_ENGINE_IDLE_PITCH                (0.8f)
#define TA_ENGINE_FULL_PITCH                (1.5f)
#define TA_ENGINE_IDLE_LEVEL                (0.12f)
#define TA_ENGINE_FULL_LEVEL                (0.4f)

/* The rope's clunk when hooking on, quieter than the other sounds. */
#define TA_HOOK_LEVEL                       (0.4f)

/* The horn, quieter than the other sounds and pitched clear of the speaker's bass. */
#define TA_HORN_LEVEL                       (0.2f)

/* -------------------------------------------------------------------------- */
/* Sounds                                                                     */
/* -------------------------------------------------------------------------- */

/* Engines: a low diesel chug. */
static const Sound_LayerTypeDef TugAudio_EngineLayers[] = {
    { .Wave = SOUND_SAW, .Hz = 72.0f, .Filter = SOUND_BANDPASS, .FilterHz = 340.0f, .Resonance = 0.8f, .Attack = 0.25f, .Decay = 0.4f, .Volume = 0.7f, .Tremolo = 0.6f, .TremoloHz = 10.0f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_BANDPASS, .FilterHz = 600.0f, .Resonance = 0.6f, .Attack = 0.25f, .Decay = 0.4f, .Volume = 0.3f, .Tremolo = 0.45f, .TremoloHz = 10.0f },
};

/* Knock: a hollow thud against the hull. */
static const Sound_LayerTypeDef TugAudio_KnockLayers[] = {
    { .Hz = 190.0f, .EndHz = 120.0f, .SlideSeconds = 0.12f, .Attack = 0.002f, .Decay = 0.3f, .FmDepth = 1.5f, .FmDecay = 0.08f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .FilterEndHz = 400.0f, .Resonance = 0.8f, .Attack = 0.002f, .Decay = 0.25f, .Volume = 0.6f },
};

/* Horn: two low reedy notes together, a tug's toot. */
static const Sound_LayerTypeDef TugAudio_HornLayers[] = {
    { .Wave = SOUND_SQUARE, .Hz = 247.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.04f, .Hold = 0.35f, .Decay = 0.25f, .Volume = 0.45f, .Vibrato = 0.004f, .VibratoHz = 5.0f },
    { .Wave = SOUND_SQUARE, .Hz = 311.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.04f, .Hold = 0.35f, .Decay = 0.25f, .Volume = 0.4f, .Vibrato = 0.004f, .VibratoHz = 5.3f },
};

/* Double horn: "honk honk", setting off. */
static const Sound_LayerTypeDef TugAudio_DoubleHornLayers[] = {
    { .Wave = SOUND_SQUARE, .Hz = 247.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.03f, .Hold = 0.18f, .Decay = 0.12f, .Volume = 0.45f },
    { .Wave = SOUND_SQUARE, .Hz = 311.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.03f, .Hold = 0.18f, .Decay = 0.12f, .Volume = 0.4f },
    { .Delay = 0.38f, .Wave = SOUND_SQUARE, .Hz = 247.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.03f, .Hold = 0.3f, .Decay = 0.2f, .Volume = 0.45f },
    { .Delay = 0.38f, .Wave = SOUND_SQUARE, .Hz = 311.0f, .Filter = SOUND_BANDPASS, .FilterHz = 900.0f, .Resonance = 0.7f, .Attack = 0.03f, .Hold = 0.3f, .Decay = 0.2f, .Volume = 0.4f },
};

/* Hook: the rope snatching tight, a woody clunk and a creak, pitched clear of the speaker's bass. */
static const Sound_LayerTypeDef TugAudio_HookLayers[] = {
    { .Hz = 520.0f, .EndHz = 380.0f, .SlideSeconds = 0.05f, .Attack = 0.002f, .Decay = 0.16f, .Volume = 0.7f, .FmDepth = 1.5f, .FmRatio = 1.5f, .FmDecay = 0.05f },
    { .Delay = 0.06f, .Wave = SOUND_NOISE, .Filter = SOUND_BANDPASS, .FilterHz = 1400.0f, .FilterEndHz = 900.0f, .Resonance = 6.0f, .Attack = 0.02f, .Decay = 0.35f, .Volume = 0.35f },
};

/* Paid: the till rings, two bright bell notes and a jingle of coins. */
static const Sound_LayerTypeDef TugAudio_PaidLayers[] = {
    { .Hz = 1319.0f, .Attack = 0.002f, .Decay = 0.9f, .Volume = 0.5f, .FmDepth = 1.4f, .FmRatio = 3.5f, .FmDecay = 0.3f },
    { .Delay = 0.11f, .Hz = 1760.0f, .Attack = 0.002f, .Decay = 1.3f, .Volume = 0.55f, .FmDepth = 1.4f, .FmRatio = 3.5f, .FmDecay = 0.4f },
    { .Delay = 0.05f, .Wave = SOUND_NOISE, .Filter = SOUND_HIGHPASS, .FilterHz = 6000.0f, .Attack = 0.002f, .Decay = 0.3f, .Volume = 0.2f, .Tremolo = 0.9f, .TremoloHz = 24.0f },
};

/* Wrecked: a splintering crunch, then a slow glug. */
static const Sound_LayerTypeDef TugAudio_WreckedLayers[] = {
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 2400.0f, .FilterEndHz = 300.0f, .Resonance = 1.0f, .Attack = 0.002f, .Decay = 0.6f, .Volume = 0.8f, .Drive = 3.0f },
    { .Hz = 150.0f, .EndHz = 100.0f, .SlideSeconds = 0.3f, .Attack = 0.002f, .Decay = 0.5f, .Volume = 0.5f, .FmDepth = 2.0f, .FmDecay = 0.1f },
    { .Delay = 0.35f, .Hz = 300.0f, .EndHz = 70.0f, .SlideSeconds = 1.2f, .Attack = 0.01f, .Decay = 1.5f, .Volume = 0.5f, .Vibrato = 0.06f, .VibratoHz = 7.0f },
};

/* Purchase: the bell rising through a major chord. */
static const Sound_LayerTypeDef TugAudio_PurchaseLayers[] = {
    { .Hz = 523.0f, .Attack = 0.002f, .Decay = 1.0f, .Volume = 0.5f, .FmDepth = 1.2f, .FmRatio = 3.5f, .FmDecay = 0.4f },
    { .Delay = 0.12f, .Hz = 659.0f, .Attack = 0.002f, .Decay = 1.0f, .Volume = 0.5f, .FmDepth = 1.2f, .FmRatio = 3.5f, .FmDecay = 0.4f },
    { .Delay = 0.24f, .Hz = 784.0f, .Attack = 0.002f, .Decay = 1.2f, .Volume = 0.5f, .FmDepth = 1.2f, .FmRatio = 3.5f, .FmDecay = 0.4f },
    { .Delay = 0.36f, .Hz = 1047.0f, .Attack = 0.002f, .Decay = 1.8f, .Volume = 0.6f, .FmDepth = 1.4f, .FmRatio = 3.5f, .FmDecay = 0.6f },
};

/* Select: a light tick on the board. */
static const Sound_LayerTypeDef TugAudio_SelectLayers[] = {
    { .Hz = 1200.0f, .Attack = 0.001f, .Decay = 0.06f, .Volume = 0.4f, .FmDepth = 0.5f, .FmRatio = 2.0f, .FmDecay = 0.03f },
};

static const Sound_TypeDef TugAudio_Engine = { SOUND_LAYERS(TugAudio_EngineLayers), .Loop = true };
static const Sound_TypeDef TugAudio_Knock = { SOUND_LAYERS(TugAudio_KnockLayers), .RandomPitch = 0.08f };
static const Sound_TypeDef TugAudio_Horn = { SOUND_LAYERS(TugAudio_HornLayers) };
static const Sound_TypeDef TugAudio_DoubleHorn = { SOUND_LAYERS(TugAudio_DoubleHornLayers) };
static const Sound_TypeDef TugAudio_Hook = { SOUND_LAYERS(TugAudio_HookLayers) };
static const Sound_TypeDef TugAudio_Paid = { SOUND_LAYERS(TugAudio_PaidLayers) };
static const Sound_TypeDef TugAudio_Wrecked = { SOUND_LAYERS(TugAudio_WreckedLayers) };
static const Sound_TypeDef TugAudio_Purchase = { SOUND_LAYERS(TugAudio_PurchaseLayers) };
static const Sound_TypeDef TugAudio_Select = { SOUND_LAYERS(TugAudio_SelectLayers) };

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Sound_HandleTypeDef TugAudio_EngineHandle;
static bool TugAudio_EngineRunning = false;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void TugAudio_StartEngines(void)
{
    /* Sounds stop when the game is left, so a handle from an earlier game may be long gone. */
    if(!TugAudio_EngineRunning || !Sound_IsPlaying(TugAudio_EngineHandle))
    {
        TugAudio_EngineHandle = Sound_PlayWith(&TugAudio_Engine, TA_MASTER_LEVEL * TA_ENGINE_IDLE_LEVEL, TA_ENGINE_IDLE_PITCH);
        TugAudio_EngineRunning = true;
    }
}

void TugAudio_SetEngines(float Load)
{
    if(TugAudio_EngineRunning)
    {
        Load = (Load < 0.0f) ? 0.0f : ((Load > 1.0f) ? 1.0f : Load);
        Sound_SetPitch(TugAudio_EngineHandle, TA_ENGINE_IDLE_PITCH + ((TA_ENGINE_FULL_PITCH - TA_ENGINE_IDLE_PITCH) * Load));
        Sound_SetVolume(TugAudio_EngineHandle, TA_MASTER_LEVEL * (TA_ENGINE_IDLE_LEVEL + ((TA_ENGINE_FULL_LEVEL - TA_ENGINE_IDLE_LEVEL) * Load)));
    }
}

void TugAudio_StopEngines(void)
{
    if(TugAudio_EngineRunning)
    {
        Sound_Stop(TugAudio_EngineHandle);
        TugAudio_EngineRunning = false;
    }
}

void TugAudio_PlayKnock(float Strength)
{
    Strength = (Strength < 0.0f) ? 0.0f : ((Strength > 1.0f) ? 1.0f : Strength);
    (void)Sound_PlayWith(&TugAudio_Knock, TA_MASTER_LEVEL * (0.3f + (0.7f * Strength)), 1.15f - (0.3f * Strength));
}

void TugAudio_PlayHorn(void)
{
    (void)Sound_PlayWith(&TugAudio_Horn, TA_MASTER_LEVEL * TA_HORN_LEVEL, 1.0f);
}

void TugAudio_PlayDoubleHorn(void)
{
    (void)Sound_PlayWith(&TugAudio_DoubleHorn, TA_MASTER_LEVEL * TA_HORN_LEVEL, 1.0f);
}

void TugAudio_PlayHook(void)
{
    (void)Sound_PlayWith(&TugAudio_Hook, TA_MASTER_LEVEL * TA_HOOK_LEVEL, 1.0f);
}

void TugAudio_PlayPaid(void)
{
    (void)Sound_PlayWith(&TugAudio_Paid, TA_MASTER_LEVEL, 1.0f);
}

void TugAudio_PlayWrecked(void)
{
    (void)Sound_PlayWith(&TugAudio_Wrecked, TA_MASTER_LEVEL, 1.0f);
}

void TugAudio_PlayPurchase(void)
{
    (void)Sound_PlayWith(&TugAudio_Purchase, TA_MASTER_LEVEL, 1.0f);
}

void TugAudio_PlaySelect(void)
{
    (void)Sound_PlayWith(&TugAudio_Select, TA_MASTER_LEVEL, 1.0f);
}
