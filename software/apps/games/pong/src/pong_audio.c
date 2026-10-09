/**
 * @file pong_audio.c
 * @brief Sound effects for Pong.
 *
 * Every sound is a table of layers played by the sound engine (sound.h).
 * Most layers are soft FM tones: a sine whose brightness fades faster than
 * its volume, so each starts with a gentle click and settles into a clean
 * tone. Some glide in pitch, and a few add a breath of filtered noise.
 */

#include "pong_audio.h"

#include "sound.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Overall level of every Pong sound. */
#define PA_MASTER_LEVEL                     (0.35f)

/* Paddle */
#define PA_PADDLE_LEVEL                     (0.5f)
#define PA_PADDLE_SLOW_PITCH                (0.85f)
#define PA_PADDLE_FAST_PITCH                (1.25f)
#define PA_PADDLE_RIGHT_PITCH               (1.122f)
#define PA_PADDLE_SLOW_LOUDNESS             (0.7f)

/* Wall */
#define PA_WALL_LEVEL                       (0.3f)
#define PA_WALL_SLOW_PITCH                  (0.9f)
#define PA_WALL_FAST_PITCH                  (1.1f)

/* Shield, miss and power-ups */
#define PA_SHIELD_LEVEL                     (0.4f)
#define PA_MISS_LEVEL                       (0.4f)
#define PA_POWER_UP_LEVEL                   (0.3f)
#define PA_EXPIRE_LEVEL                     (0.15f)

/* -------------------------------------------------------------------------- */
/* Sounds                                                                     */
/* -------------------------------------------------------------------------- */

/* Paddle: a clean "pok" with a short tock on top. */
static const Sound_LayerTypeDef PongAudio_PaddleLayers[] = {
    { .Hz = 740.0f, .EndHz = 620.0f, .SlideSeconds = 0.045f, .Attack = 0.002f, .Decay = 0.41f, .FmDepth = 1.2f, .FmRatio = 2.0f, .FmDecay = 0.14f },
    { .Hz = 2960.0f, .Attack = 0.001f, .Decay = 0.055f, .Volume = 0.15f },
};

/* Wall: a softer, shorter "tik". */
static const Sound_LayerTypeDef PongAudio_WallLayers[] = {
    { .Hz = 520.0f, .EndHz = 470.0f, .SlideSeconds = 0.03f, .Attack = 0.002f, .Decay = 0.24f, .FmDepth = 0.6f, .FmDecay = 0.1f },
};

/* Shield: the ball striking an energy field, a buzzing "thwum" that wavers, over the wall tik. */
static const Sound_LayerTypeDef PongAudio_ShieldLayers[] = {
    { .Hz = 880.0f, .EndHz = 440.0f, .SlideSeconds = 0.09f, .Attack = 0.002f, .Decay = 0.83f, .Volume = 0.6f, .FmDepth = 2.5f, .FmRatio = 2.0f, .FmDecay = 0.35f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 12500.0f, .Resonance = 0.5f, .Attack = 0.002f, .Decay = 0.83f, .Volume = 0.36f },
    { .Hz = 893.0f, .EndHz = 447.0f, .SlideSeconds = 0.09f, .Attack = 0.002f, .Decay = 0.83f, .Volume = 0.45f, .FmDepth = 2.5f, .FmRatio = 2.0f, .FmDecay = 0.35f },
    { .Hz = 520.0f, .EndHz = 470.0f, .SlideSeconds = 0.03f, .Attack = 0.002f, .Decay = 0.24f, .Volume = 0.5f, .FmDepth = 0.6f, .FmDecay = 0.1f },
};

/* Miss: a calm falling "da-dum" with a soft breath of air. */
static const Sound_LayerTypeDef PongAudio_MissLayers[] = {
    { .Hz = 659.0f, .EndHz = 640.0f, .SlideSeconds = 0.3f, .Attack = 0.005f, .Decay = 1.0f, .Volume = 0.8f, .FmDepth = 0.8f, .FmDecay = 0.69f },
    { .Delay = 0.14f, .Hz = 494.0f, .EndHz = 392.0f, .SlideSeconds = 0.75f, .Attack = 0.01f, .Decay = 2.4f, .FmDepth = 0.8f, .FmDecay = 2.1f },
    { .Delay = 0.14f, .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 3750.0f, .Resonance = 0.5f, .Attack = 0.01f, .Decay = 2.4f, .Volume = 0.5f },
};

/* Expand: inflating, a swelling octave slide that fills out and lands on a bright ting. */
static const Sound_LayerTypeDef PongAudio_ExpandLayers[] = {
    { .Hz = 392.0f, .EndHz = 784.0f, .SlideSeconds = 0.3f, .Attack = 0.3f, .Decay = 2.1f, .Volume = 0.7f, .FmDepth = 1.5f, .FmDecay = 2.1f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 6250.0f, .Resonance = 0.5f, .Attack = 0.3f, .Decay = 2.1f, .Volume = 0.14f },
    { .Hz = 396.0f, .EndHz = 792.0f, .SlideSeconds = 0.3f, .Attack = 0.3f, .Decay = 2.1f, .Volume = 0.6f, .FmDepth = 1.5f, .FmDecay = 2.1f },
    { .Delay = 0.15f, .Hz = 784.0f, .EndHz = 1568.0f, .SlideSeconds = 0.24f, .Attack = 0.15f, .Decay = 1.7f, .Volume = 0.4f, .FmDepth = 1.0f, .FmRatio = 2.0f, .FmDecay = 1.4f },
    { .Delay = 0.3f, .Hz = 1568.0f, .Attack = 0.003f, .Decay = 1.7f, .Volume = 0.3f, .FmDepth = 1.2f, .FmRatio = 3.5f, .FmDecay = 0.69f },
};

/* Shield: a force field powering up, a wavering hum that rises and crackles. */
static const Sound_LayerTypeDef PongAudio_ShieldPowerUpLayers[] = {
    { .Hz = 330.0f, .EndHz = 660.0f, .SlideSeconds = 0.24f, .Attack = 0.12f, .Decay = 2.4f, .Volume = 0.7f, .FmDepth = 2.0f, .FmRatio = 2.0f, .FmDecay = 2.8f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 10000.0f, .Resonance = 0.5f, .Attack = 0.12f, .Decay = 2.4f, .Volume = 0.21f },
    { .Hz = 336.0f, .EndHz = 672.0f, .SlideSeconds = 0.24f, .Attack = 0.12f, .Decay = 2.4f, .Volume = 0.6f, .FmDepth = 2.0f, .FmRatio = 2.0f, .FmDecay = 2.8f },
    { .Delay = 0.1f, .Hz = 660.0f, .EndHz = 1320.0f, .SlideSeconds = 0.24f, .Attack = 0.1f, .Decay = 2.1f, .Volume = 0.35f, .FmDepth = 1.5f, .FmRatio = 2.0f, .FmDecay = 2.1f },
    { .Delay = 0.1f, .Hz = 671.0f, .EndHz = 1342.0f, .SlideSeconds = 0.24f, .Attack = 0.1f, .Decay = 2.1f, .Volume = 0.3f, .FmDepth = 1.5f, .FmRatio = 2.0f, .FmDecay = 2.1f },
};

/* Shrink: deflating, a zoom down with escaping air, then a tiny "pip-pip". */
static const Sound_LayerTypeDef PongAudio_ShrinkLayers[] = {
    { .Hz = 1047.0f, .EndHz = 392.0f, .SlideSeconds = 0.3f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.8f, .FmDepth = 1.5f, .FmDecay = 0.69f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 6250.0f, .Resonance = 0.5f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.32f },
    { .Hz = 1057.0f, .EndHz = 396.0f, .SlideSeconds = 0.3f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.6f, .FmDepth = 1.5f, .FmDecay = 0.69f },
    { .Delay = 0.28f, .Hz = 1760.0f, .Attack = 0.002f, .Decay = 0.28f, .Volume = 0.5f, .FmDepth = 0.5f, .FmRatio = 2.0f, .FmDecay = 0.14f },
    { .Delay = 0.36f, .Hz = 1760.0f, .Attack = 0.002f, .Decay = 0.28f, .Volume = 0.35f, .FmDepth = 0.5f, .FmRatio = 2.0f, .FmDecay = 0.14f },
};

/* Power: a fast rising zap, then a ringing high note. */
static const Sound_LayerTypeDef PongAudio_PowerLayers[] = {
    { .Hz = 330.0f, .EndHz = 1320.0f, .SlideSeconds = 0.15f, .Attack = 0.003f, .Decay = 1.4f, .FmDepth = 2.5f, .FmDecay = 0.55f },
    { .Wave = SOUND_NOISE, .Filter = SOUND_LOWPASS, .FilterHz = 7500.0f, .Resonance = 0.5f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.3f },
    { .Delay = 0.08f, .Hz = 1320.0f, .Attack = 0.003f, .Decay = 1.7f, .Volume = 0.6f, .FmDepth = 1.0f, .FmRatio = 2.0f, .FmDecay = 0.69f },
};

/* Invert: a swoop up, then its mirror image back down; detuned pairs beat for a dizzy feel. */
static const Sound_LayerTypeDef PongAudio_InvertLayers[] = {
    { .Hz = 600.0f, .EndHz = 1200.0f, .SlideSeconds = 0.12f, .Attack = 0.003f, .Decay = 0.83f, .Volume = 0.6f, .FmDepth = 1.2f, .FmRatio = 2.0f, .FmDecay = 0.55f },
    { .Hz = 609.0f, .EndHz = 1218.0f, .SlideSeconds = 0.12f, .Attack = 0.003f, .Decay = 0.83f, .Volume = 0.5f, .FmDepth = 1.2f, .FmRatio = 2.0f, .FmDecay = 0.55f },
    { .Delay = 0.13f, .Hz = 1200.0f, .EndHz = 600.0f, .SlideSeconds = 0.12f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.7f, .FmDepth = 1.2f, .FmRatio = 2.0f, .FmDecay = 0.69f },
    { .Delay = 0.13f, .Hz = 1218.0f, .EndHz = 609.0f, .SlideSeconds = 0.12f, .Attack = 0.003f, .Decay = 1.4f, .Volume = 0.6f, .FmDepth = 1.2f, .FmRatio = 2.0f, .FmDecay = 0.69f },
};

/* Expire: a soft two-note "bloop" falling a fourth, as a power-up wears off. */
static const Sound_LayerTypeDef PongAudio_ExpireLayers[] = {
    { .Hz = 784.0f, .Attack = 0.003f, .Decay = 0.48f, .FmDepth = 0.4f, .FmDecay = 0.21f },
    { .Delay = 0.07f, .Hz = 587.0f, .Attack = 0.003f, .Decay = 0.62f, .Volume = 0.8f, .FmDepth = 0.3f, .FmDecay = 0.21f },
};

/* Playing a sound again cuts off the previous one, as a single speaker would. */
static const Sound_TypeDef PongAudio_PaddleSound = { SOUND_LAYERS(PongAudio_PaddleLayers), .Single = true };
static const Sound_TypeDef PongAudio_WallSound = { SOUND_LAYERS(PongAudio_WallLayers), .Single = true };
static const Sound_TypeDef PongAudio_ShieldSound = { SOUND_LAYERS(PongAudio_ShieldLayers), .Single = true };
static const Sound_TypeDef PongAudio_MissSound = { SOUND_LAYERS(PongAudio_MissLayers), .Single = true };
static const Sound_TypeDef PongAudio_ExpireSound = { SOUND_LAYERS(PongAudio_ExpireLayers), .Single = true };
static const Sound_TypeDef PongAudio_PowerUpSounds[] = {
    { SOUND_LAYERS(PongAudio_ExpandLayers), .Single = true },
    { SOUND_LAYERS(PongAudio_ShieldPowerUpLayers), .Single = true },
    { SOUND_LAYERS(PongAudio_ShrinkLayers), .Single = true },
    { SOUND_LAYERS(PongAudio_PowerLayers), .Single = true },
    { SOUND_LAYERS(PongAudio_InvertLayers), .Single = true },
};

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static float PongAudio_Clamp(float Value, float Minimum, float Maximum)
{
    return (Value < Minimum) ? Minimum : ((Value > Maximum) ? Maximum : Value);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void PongAudio_PlayPaddle(bool Left, float Speed)
{
    const float ClampedSpeed = PongAudio_Clamp(Speed, 0.0f, 1.5f);
    float Pitch = PA_PADDLE_SLOW_PITCH + ((PA_PADDLE_FAST_PITCH - PA_PADDLE_SLOW_PITCH) * ClampedSpeed);
    const float Loudness = PA_PADDLE_SLOW_LOUDNESS + ((1.0f - PA_PADDLE_SLOW_LOUDNESS) * PongAudio_Clamp(Speed, 0.0f, 1.0f));

    Pitch *= Left ? 1.0f : PA_PADDLE_RIGHT_PITCH;
    (void)Sound_PlayWith(&PongAudio_PaddleSound, PA_MASTER_LEVEL * PA_PADDLE_LEVEL * Loudness, Pitch);
}

void PongAudio_PlayWall(float Speed)
{
    const float Pitch = PA_WALL_SLOW_PITCH + ((PA_WALL_FAST_PITCH - PA_WALL_SLOW_PITCH) * PongAudio_Clamp(Speed, 0.0f, 1.0f));

    (void)Sound_PlayWith(&PongAudio_WallSound, PA_MASTER_LEVEL * PA_WALL_LEVEL, Pitch);
}

void PongAudio_PlayShield(void)
{
    (void)Sound_PlayWith(&PongAudio_ShieldSound, PA_MASTER_LEVEL * PA_SHIELD_LEVEL, 1.0f);
}

void PongAudio_PlayMiss(void)
{
    (void)Sound_PlayWith(&PongAudio_MissSound, PA_MASTER_LEVEL * PA_MISS_LEVEL, 1.0f);
}

void PongAudio_PlayPowerUp(PongAudio_PowerUpTypeDef PowerUp)
{
    if((uint32_t)PowerUp >= (uint32_t)(sizeof(PongAudio_PowerUpSounds) / sizeof(PongAudio_PowerUpSounds[0])))
    {
        return;
    }

    (void)Sound_PlayWith(&PongAudio_PowerUpSounds[PowerUp], PA_MASTER_LEVEL * PA_POWER_UP_LEVEL, 1.0f);
}

void PongAudio_PlayExpire(void)
{
    (void)Sound_PlayWith(&PongAudio_ExpireSound, PA_MASTER_LEVEL * PA_EXPIRE_LEVEL, 1.0f);
}

void PongAudio_Stop(void)
{
    Sound_StopAll();
}
