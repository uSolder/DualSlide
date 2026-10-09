/**
 * @file sound.c
 * @brief Sound effects engine: plays table-described sounds on one mixer channel.
 *
 * The engine is a mixer synth on MIXER_SOUND_CHANNEL. The game loop sends it
 * commands (play, stop, set volume...) as mixer messages; everything else
 * happens in the audio context, which owns all playback state.
 *
 * Each play of a sound is an instance. As its layers' delays pass, the
 * instance starts a voice for each layer from a shared pool. Voices render
 * sample by sample; slides, vibrato, tremolo and filter settings change at a
 * control rate of one update per SOUND_CONTROL_SAMPLES samples, which is far
 * cheaper and sounds the same. The mix passes through a soft limiter, so many
 * loud sounds at once squash gently instead of clipping.
 */

#include "sound.h"

#include "mixer.h"
#include "synth.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Samples between control updates. */
#define SOUND_CONTROL_SAMPLES               (16U)
#define SOUND_CONTROL_RATE                  (SYNTH_SAMPLE_RATE / (float)SOUND_CONTROL_SAMPLES)

/* Defaults for fields left at 0. */
#define SOUND_DEFAULT_HZ                    (440.0f)
#define SOUND_DEFAULT_DECAY_SECONDS         (0.2f)
#define SOUND_DEFAULT_DUTY                  (0.5f)
#define SOUND_DEFAULT_FILTER_HZ             (1000.0f)
#define SOUND_DEFAULT_RESONANCE             (0.7f)
#define SOUND_DEFAULT_WOBBLE_HZ             (6.0f)

/* A fade "to silence" falls by ln(1000), 60 dB, over its time; a voice ends below SOUND_SILENT. */
#define SOUND_FADE_DEPTH                    (6.9077553f)
#define SOUND_SILENT                        (0.0005f)

/* A slide covers 95% of the way, e^-3, in its SlideSeconds. */
#define SOUND_SLIDE_DEPTH                   (3.0f)

/* Stopping everything fades over this, quick but without a click. */
#define SOUND_QUICK_FADE_SECONDS            (0.01f)

/* Volume and pitch changes glide over a few milliseconds. */
#define SOUND_SMOOTHING                     (0.1f)

/* Highest oscillator and filter frequency, as a fraction of the sample rate. */
#define SOUND_MAXIMUM_STEP                  (0.45f)
#define SOUND_MINIMUM_FILTER_HZ             (20.0f)

/* The output limiter is linear up to the knee, then bends towards full scale. */
#define SOUND_LIMIT_KNEE                    (0.8f)

#define SOUND_PI                            (3.14159265f)

/* A repeating sound has played its last pass (never, when it repeats until stopped). */
#define SOUND_LAST_PASS(Instance, Sound)    (((Sound)->RepeatCount != 0U) && ((Instance)->Pass >= (Sound)->RepeatCount))

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef enum
{
    SOUND_COMMAND_PLAY = 0,
    SOUND_COMMAND_STOP,
    SOUND_COMMAND_STOP_ALL,
    SOUND_COMMAND_SET_VOLUME,
    SOUND_COMMAND_SET_PITCH,
    SOUND_COMMAND_SET_CONTROL
} Sound_CommandKindTypeDef;

/**
 * @brief A command from the game loop to the engine.
 */
typedef struct
{
    const Sound_TypeDef *Sound;
    float Value;
    float Pitch;
    Sound_HandleTypeDef Handle;
    uint8_t Kind;
} Sound_CommandTypeDef;

_Static_assert(sizeof(Sound_CommandTypeDef) <= MIXER_MESSAGE_SIZE, "Sound commands must fit in a mixer message.");
_Static_assert(SOUND_MAX_LAYERS <= 32U, "Started layers are tracked in a 32-bit mask.");

/**
 * @brief One play of a sound.
 */
typedef struct
{
    const Sound_TypeDef *Sound;
    Sound_HandleTypeDef Handle;
    bool Active;
    bool Releasing;
    uint8_t VoiceCount;
    uint16_t Pass;
    uint32_t Age;
    uint32_t Sample;
    uint32_t RepeatSamples;
    uint32_t StartedLayers;
    float BaseVolume;
    float BasePitch;
    float Volume;
    float TargetVolume;
    float Pitch;
    float TargetPitch;
    float Control;
} Sound_InstanceTypeDef;

typedef enum
{
    SOUND_STAGE_ATTACK = 0,
    SOUND_STAGE_HOLD,
    SOUND_STAGE_SUSTAIN,
    SOUND_STAGE_DECAY,
    SOUND_STAGE_RELEASE
} Sound_StageTypeDef;

/**
 * @brief One layer of an instance, sounding.
 */
typedef struct
{
    bool Active;
    bool Fresh;
    uint8_t Stage;
    uint8_t Instance;
    const Sound_LayerTypeDef *Layer;
    uint32_t Sample;
    uint32_t StageSamples;
    uint32_t HoldSamples;
    float Envelope;
    float AttackStep;
    float DecayCoefficient;
    float ReleaseCoefficient;
    float Gain;
    float Volume;
    float Hz;
    float FilterHz;
    float Glide;
    float Phase;
    float Step;
    float ModulatorPhase;
    float ModulatorStep;
    float FmDepth;
    float FmDecay;
    float Duty;
    float VibratoPhase;
    float TremoloPhase;
    float TremoloGain;
    float NoiseValue;
    float FilterA1;
    float FilterA2;
    float FilterA3;
    float FilterK;
    float FilterIc1;
    float FilterIc2;
    float DriveGain;
    float DriveScale;
    Sound_PatchVoiceTypeDef Patch;
} Sound_VoiceTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

/* Audio context only. */
static Sound_InstanceTypeDef Sound_Instances[SOUND_MAX_PLAYING];
static Sound_VoiceTypeDef Sound_Voices[SOUND_MAX_VOICES];
static uint32_t Sound_Random;
static uint32_t Sound_Age;

/* Written by the audio context, read by the game loop. */
static Sound_HandleTypeDef Sound_PlayingHandles[SOUND_MAX_PLAYING];
static Sound_HandleTypeDef Sound_LastStartedHandle;

/* Game loop only. */
static Sound_HandleTypeDef Sound_NextHandle;

/* -------------------------------------------------------------------------- */
/* Private functions: helpers                                                 */
/* -------------------------------------------------------------------------- */

/* A field's value, or its default when left at 0. */
static float Sound_OrDefault(float Value, float Default)
{
    return (Value > 0.0f) ? Value : Default;
}

/* Per-sample multiplier that fades to silence over Seconds. */
static float Sound_FadeCoefficient(float Seconds, float Rate)
{
    return expf(-SOUND_FADE_DEPTH / (Seconds * Rate));
}

static void Sound_PublishHandle(uint32_t Instance, Sound_HandleTypeDef Handle)
{
    __atomic_store_n(&Sound_PlayingHandles[Instance], Handle, __ATOMIC_RELEASE);
}

/* -------------------------------------------------------------------------- */
/* Private functions: voices                                                  */
/* -------------------------------------------------------------------------- */

/* A free voice, or else the quietest one, taken from whichever sound had it. */
static Sound_VoiceTypeDef *Sound_AllocateVoice(void)
{
    Sound_VoiceTypeDef *Quietest = &Sound_Voices[0];

    for(uint32_t Index = 0U; Index < SOUND_MAX_VOICES; Index++)
    {
        Sound_VoiceTypeDef *Voice = &Sound_Voices[Index];

        if(!Voice->Active)
        {
            return Voice;
        }

        if((Voice->Envelope * Voice->Gain) < (Quietest->Envelope * Quietest->Gain))
        {
            Quietest = Voice;
        }
    }

    Quietest->Active = false;
    Sound_Instances[Quietest->Instance].VoiceCount--;

    return Quietest;
}

/* Recompute pitch, filter and wobble; runs once per SOUND_CONTROL_SAMPLES samples. */
static void Sound_UpdateVoiceControl(Sound_VoiceTypeDef *Voice)
{
    const Sound_LayerTypeDef *Layer = Voice->Layer;
    const Sound_InstanceTypeDef *Instance = &Sound_Instances[Voice->Instance];
    float Hz = Voice->Hz * Instance->Pitch;

    if(Layer->Vibrato > 0.0f)
    {
        Hz *= 1.0f + (Layer->Vibrato * Synth_Sine(Voice->VibratoPhase));
        Voice->VibratoPhase += Sound_OrDefault(Layer->VibratoHz, SOUND_DEFAULT_WOBBLE_HZ) / SOUND_CONTROL_RATE;
        Voice->VibratoPhase -= floorf(Voice->VibratoPhase);
    }

    Voice->Step = Hz * SYNTH_SAMPLE_PERIOD;
    Voice->Step = (Voice->Step > SOUND_MAXIMUM_STEP) ? SOUND_MAXIMUM_STEP : Voice->Step;
    Voice->ModulatorStep = Voice->Step * Sound_OrDefault(Layer->FmRatio, 1.0f);

    Voice->TremoloGain = 1.0f;

    if(Layer->Tremolo > 0.0f)
    {
        Voice->TremoloGain = 1.0f - (Layer->Tremolo * (0.5f + (0.5f * Synth_Sine(Voice->TremoloPhase))));
        Voice->TremoloPhase += Sound_OrDefault(Layer->TremoloHz, SOUND_DEFAULT_WOBBLE_HZ) / SOUND_CONTROL_RATE;
        Voice->TremoloPhase -= floorf(Voice->TremoloPhase);
    }

    Voice->Gain = Voice->Volume * Voice->TremoloGain * Instance->Volume;

    /* Topology-preserving state-variable filter (Zavalishin): stable at any setting. */
    if(Layer->Filter != SOUND_NO_FILTER)
    {
        float FilterHz = Voice->FilterHz;
        float G;

        FilterHz = (FilterHz < SOUND_MINIMUM_FILTER_HZ) ? SOUND_MINIMUM_FILTER_HZ : FilterHz;
        FilterHz = (FilterHz > (SOUND_MAXIMUM_STEP * SYNTH_SAMPLE_RATE)) ? (SOUND_MAXIMUM_STEP * SYNTH_SAMPLE_RATE) : FilterHz;
        G = tanf(SOUND_PI * FilterHz * SYNTH_SAMPLE_PERIOD);
        Voice->FilterK = 1.0f / Sound_OrDefault(Layer->Resonance, SOUND_DEFAULT_RESONANCE);
        Voice->FilterA1 = 1.0f / (1.0f + (G * (G + Voice->FilterK)));
        Voice->FilterA2 = G * Voice->FilterA1;
        Voice->FilterA3 = G * Voice->FilterA2;
    }

    Voice->Patch.Hz = Hz;
    Voice->Patch.Step = Voice->Step;
    Voice->Patch.Control = Instance->Control;
    Voice->Patch.Seconds = (float)Voice->Sample * SYNTH_SAMPLE_PERIOD;

    /* Slides ease towards their end values, and the FM fades, ready for the next update. */
    if(Layer->EndHz > 0.0f)
    {
        Voice->Hz += Voice->Glide * (Layer->EndHz - Voice->Hz);
    }

    if(Layer->FilterEndHz > 0.0f)
    {
        Voice->FilterHz += Voice->Glide * (Layer->FilterEndHz - Voice->FilterHz);
    }

    Voice->FmDepth *= Voice->FmDecay;
}

static void Sound_StartVoice(uint32_t InstanceIndex, const Sound_LayerTypeDef *Layer)
{
    Sound_InstanceTypeDef *Instance = &Sound_Instances[InstanceIndex];
    Sound_VoiceTypeDef *Voice = Sound_AllocateVoice();
    const bool Noise = (Layer->Patch == NULL) && (Layer->Wave == SOUND_NOISE);
    const float Decay = Sound_OrDefault(Layer->Decay, SOUND_DEFAULT_DECAY_SECONDS);
    const uint32_t AttackSamples = Synth_Seconds(Layer->Attack);
    const float SlideSeconds = (Layer->SlideSeconds > 0.0f) ? Layer->SlideSeconds : (Layer->Attack + Layer->Hold + Decay);

    *Voice = (Sound_VoiceTypeDef){ 0 };
    Voice->Active = true;
    Voice->Instance = (uint8_t)InstanceIndex;
    Voice->Layer = Layer;
    Voice->Volume = Sound_OrDefault(Layer->Volume, 1.0f);
    Voice->Hz = Noise ? Layer->Hz : Sound_OrDefault(Layer->Hz, SOUND_DEFAULT_HZ);
    Voice->Duty = Sound_OrDefault(Layer->Duty, SOUND_DEFAULT_DUTY);
    Voice->FilterHz = Sound_OrDefault(Layer->FilterHz, SOUND_DEFAULT_FILTER_HZ);
    Voice->HoldSamples = Synth_Seconds(Layer->Hold);
    Voice->DecayCoefficient = Sound_FadeCoefficient(Decay, SYNTH_SAMPLE_RATE);
    Voice->ReleaseCoefficient = Voice->DecayCoefficient;
    Voice->FmDepth = Layer->FmDepth / SYNTH_TWO_PI;
    Voice->FmDecay = (Layer->FmDecay > 0.0f) ? Sound_FadeCoefficient(Layer->FmDecay, SOUND_CONTROL_RATE) : 1.0f;

    /* Slides ease in: most of the way in the first third of SlideSeconds, settling by the end. */
    Voice->Glide = 1.0f - expf(-SOUND_SLIDE_DEPTH / (SlideSeconds * SOUND_CONTROL_RATE));

    if(Layer->Drive > 0.0f)
    {
        Voice->DriveGain = 1.0f + Layer->Drive;
        Voice->DriveScale = 1.0f / Synth_SoftClip(Voice->DriveGain);
    }

    if(AttackSamples > 0U)
    {
        Voice->Stage = SOUND_STAGE_ATTACK;
        Voice->StageSamples = AttackSamples;
        Voice->AttackStep = 1.0f / (float)AttackSamples;
    }
    else
    {
        Voice->Envelope = 1.0f;
        Voice->Stage = Instance->Sound->Loop ? SOUND_STAGE_SUSTAIN : SOUND_STAGE_HOLD;
        Voice->StageSamples = Voice->HoldSamples;
    }

    Sound_Random = (Sound_Random * 1664525U) + 1013904223U;
    Voice->Patch.Layer = Layer;
    Voice->Patch.Random = Sound_Random | 1U;
    Voice->NoiseValue = Synth_Noise(&Voice->Patch.Random);
    Instance->VoiceCount++;

    /* Ready for the patch's Start; this update counts as the voice's first. */
    Sound_UpdateVoiceControl(Voice);
    Voice->Fresh = true;

    if((Layer->Patch != NULL) && (Layer->Patch->Start != NULL))
    {
        Layer->Patch->Start(&Voice->Patch);
    }
}

/* Fade a voice out from wherever it is. */
static void Sound_ReleaseVoice(Sound_VoiceTypeDef *Voice, float Coefficient)
{
    Voice->Stage = SOUND_STAGE_RELEASE;
    Voice->ReleaseCoefficient = (Coefficient > 0.0f) ? Coefficient : Voice->ReleaseCoefficient;
}

static void Sound_EndVoice(Sound_VoiceTypeDef *Voice)
{
    Voice->Active = false;
    Sound_Instances[Voice->Instance].VoiceCount--;
}

/* One oscillator sample, before filter, drive and envelope. */
static float Sound_Oscillate(Sound_VoiceTypeDef *Voice)
{
    const Sound_LayerTypeDef *Layer = Voice->Layer;
    float Phase = Voice->Phase;
    float Output;

    if(Layer->Patch != NULL)
    {
        return Layer->Patch->Next(&Voice->Patch);
    }

    if(Voice->FmDepth != 0.0f)
    {
        Phase += Voice->FmDepth * Synth_Sine(Voice->ModulatorPhase);
        Phase -= floorf(Phase);
        Voice->ModulatorPhase += Voice->ModulatorStep;
        Voice->ModulatorPhase -= (Voice->ModulatorPhase >= 1.0f) ? 1.0f : 0.0f;
    }

    switch(Layer->Wave)
    {
        case SOUND_TRIANGLE:
            Output = (4.0f * fabsf(Phase - 0.5f)) - 1.0f;
            break;

        case SOUND_SQUARE:
        {
            float Falling = Phase + 1.0f - Voice->Duty;

            Falling -= (Falling >= 1.0f) ? 1.0f : 0.0f;
            Output = (Phase < Voice->Duty) ? 1.0f : -1.0f;
            Output += Synth_PolyBlep(Phase, Voice->Step) - Synth_PolyBlep(Falling, Voice->Step);
            break;
        }

        case SOUND_SAW:
            Output = (2.0f * Phase) - 1.0f - Synth_PolyBlep(Phase, Voice->Step);
            break;

        case SOUND_NOISE:
            /* Plain hiss, or with a pitch, a new random level each cycle. */
            Output = (Voice->Hz > 0.0f) ? Voice->NoiseValue : Synth_Noise(&Voice->Patch.Random);
            break;

        case SOUND_SINE:
        default:
            Output = Synth_Sine(Phase);
            break;
    }

    Voice->Phase += Voice->Step;

    if(Voice->Phase >= 1.0f)
    {
        Voice->Phase -= 1.0f;
        Voice->NoiseValue = Synth_Noise(&Voice->Patch.Random);
    }

    return Output;
}

/* Add Count samples of a voice to Output. */
static void Sound_RenderVoice(Sound_VoiceTypeDef *Voice, float *Output, uint32_t Count)
{
    const Sound_LayerTypeDef *Layer = Voice->Layer;

    for(uint32_t Index = 0U; Index < Count; Index++)
    {
        float Value = Sound_Oscillate(Voice);

        if(Layer->Filter != SOUND_NO_FILTER)
        {
            const float V3 = Value - Voice->FilterIc2;
            const float V1 = (Voice->FilterA1 * Voice->FilterIc1) + (Voice->FilterA2 * V3);
            const float V2 = Voice->FilterIc2 + (Voice->FilterA2 * Voice->FilterIc1) + (Voice->FilterA3 * V3);

            Voice->FilterIc1 = (2.0f * V1) - Voice->FilterIc1;
            Voice->FilterIc2 = (2.0f * V2) - Voice->FilterIc2;

            if(Layer->Filter == SOUND_LOWPASS)
            {
                Value = V2;
            }
            else if(Layer->Filter == SOUND_HIGHPASS)
            {
                Value = Value - (Voice->FilterK * V1) - V2;
            }
            else
            {
                Value = V1 * Voice->FilterK;
            }
        }

        if(Voice->DriveGain > 0.0f)
        {
            Value = Synth_SoftClip(Value * Voice->DriveGain) * Voice->DriveScale;
        }

        switch(Voice->Stage)
        {
            case SOUND_STAGE_ATTACK:
                Voice->Envelope += Voice->AttackStep;

                if(--Voice->StageSamples == 0U)
                {
                    Voice->Envelope = 1.0f;
                    Voice->Stage = Sound_Instances[Voice->Instance].Sound->Loop ? SOUND_STAGE_SUSTAIN : SOUND_STAGE_HOLD;
                    Voice->StageSamples = Voice->HoldSamples;
                }
                break;

            case SOUND_STAGE_HOLD:
                if(Voice->StageSamples == 0U)
                {
                    Voice->Stage = SOUND_STAGE_DECAY;
                }
                else
                {
                    Voice->StageSamples--;
                }
                break;

            case SOUND_STAGE_DECAY:
                Voice->Envelope *= Voice->DecayCoefficient;
                break;

            case SOUND_STAGE_RELEASE:
                Voice->Envelope *= Voice->ReleaseCoefficient;
                break;

            default:
                break;
        }

        Output[Index] += Value * Voice->Envelope * Voice->Gain;
        Voice->Sample++;

        if(((Voice->Stage == SOUND_STAGE_DECAY) || (Voice->Stage == SOUND_STAGE_RELEASE)) && (Voice->Envelope < SOUND_SILENT))
        {
            Sound_EndVoice(Voice);
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Private functions: instances                                               */
/* -------------------------------------------------------------------------- */

static void Sound_EndInstance(uint32_t Index)
{
    Sound_Instances[Index].Active = false;
    Sound_PublishHandle(Index, 0U);
}

/* Stop an instance starting layers, and fade its voices (Coefficient 0: over each layer's Decay). */
static void Sound_StopInstance(uint32_t Index, float Coefficient)
{
    Sound_Instances[Index].Releasing = true;

    for(uint32_t Voice = 0U; Voice < SOUND_MAX_VOICES; Voice++)
    {
        if(Sound_Voices[Voice].Active && (Sound_Voices[Voice].Instance == Index))
        {
            Sound_ReleaseVoice(&Sound_Voices[Voice], Coefficient);
        }
    }
}

static int32_t Sound_FindInstance(Sound_HandleTypeDef Handle)
{
    for(uint32_t Index = 0U; Index < SOUND_MAX_PLAYING; Index++)
    {
        if(Sound_Instances[Index].Active && (Sound_Instances[Index].Handle == Handle))
        {
            return (int32_t)Index;
        }
    }

    return -1;
}

static void Sound_StartInstance(const Sound_CommandTypeDef *Command)
{
    const Sound_TypeDef *Sound = Command->Sound;
    const float QuickFade = Sound_FadeCoefficient(SOUND_QUICK_FADE_SECONDS, SYNTH_SAMPLE_RATE);
    uint32_t Free = 0U;
    Sound_InstanceTypeDef *Instance;

    __atomic_store_n(&Sound_LastStartedHandle, Command->Handle, __ATOMIC_RELEASE);

    if((Sound == NULL) || (Sound->Layers == NULL) || (Sound->LayerCount == 0U))
    {
        return;
    }

    if(Sound->Single)
    {
        for(uint32_t Index = 0U; Index < SOUND_MAX_PLAYING; Index++)
        {
            if(Sound_Instances[Index].Active && (Sound_Instances[Index].Sound == Sound))
            {
                Sound_StopInstance(Index, QuickFade);
            }
        }
    }

    /* A free slot, or else the oldest play. */
    for(uint32_t Index = 0U; Index < SOUND_MAX_PLAYING; Index++)
    {
        if(!Sound_Instances[Index].Active)
        {
            Free = Index;
            break;
        }

        if(Sound_Instances[Index].Age < Sound_Instances[Free].Age)
        {
            Free = Index;
        }
    }

    if(Sound_Instances[Free].Active)
    {
        for(uint32_t Voice = 0U; Voice < SOUND_MAX_VOICES; Voice++)
        {
            if(Sound_Voices[Voice].Active && (Sound_Voices[Voice].Instance == Free))
            {
                Sound_EndVoice(&Sound_Voices[Voice]);
            }
        }
    }

    Instance = &Sound_Instances[Free];
    *Instance = (Sound_InstanceTypeDef){ 0 };
    Instance->Sound = Sound;
    Instance->Handle = Command->Handle;
    Instance->Active = true;
    Instance->Age = ++Sound_Age;
    Instance->RepeatSamples = Synth_Seconds(Sound->RepeatSeconds);
    Instance->BaseVolume = Sound_OrDefault(Sound->Volume, 1.0f) * (1.0f - (Sound->RandomVolume * (0.5f + (0.5f * Synth_Noise(&Sound_Random)))));
    Instance->BasePitch = 1.0f + (Sound->RandomPitch * Synth_Noise(&Sound_Random));
    Instance->TargetVolume = Command->Value * Instance->BaseVolume;
    Instance->TargetPitch = Command->Pitch * Instance->BasePitch;
    Instance->Volume = Instance->TargetVolume;
    Instance->Pitch = Instance->TargetPitch;
    Sound_PublishHandle(Free, Command->Handle);
}

/* Start layers whose time has come, repeat, and finish; runs once per control update. */
static void Sound_UpdateInstance(uint32_t Index, uint32_t Samples)
{
    Sound_InstanceTypeDef *Instance = &Sound_Instances[Index];
    const Sound_TypeDef *Sound = Instance->Sound;
    const uint32_t AllLayers = (Sound->LayerCount >= 32U) ? UINT32_MAX : ((1UL << Sound->LayerCount) - 1UL);
    bool Finished = Instance->Releasing;

    Instance->Volume += SOUND_SMOOTHING * (Instance->TargetVolume - Instance->Volume);
    Instance->Pitch += SOUND_SMOOTHING * (Instance->TargetPitch - Instance->Pitch);

    if(!Instance->Releasing)
    {
        for(uint32_t Layer = 0U; (Layer < Sound->LayerCount) && (Layer < SOUND_MAX_LAYERS); Layer++)
        {
            if(((Instance->StartedLayers & (1UL << Layer)) == 0U) && (Instance->Sample >= Synth_Seconds(Sound->Layers[Layer].Delay)))
            {
                Instance->StartedLayers |= 1UL << Layer;
                Sound_StartVoice(Index, &Sound->Layers[Layer]);
            }
        }

        /* Each pass of a repeating sound starts its layers again. */
        if((Instance->RepeatSamples > 0U) && !SOUND_LAST_PASS(Instance, Sound) && (Instance->Sample >= Instance->RepeatSamples))
        {
            Instance->Pass++;

            if(!SOUND_LAST_PASS(Instance, Sound))
            {
                Instance->Sample = 0U;
                Instance->StartedLayers = 0U;
                return;
            }
        }

        /* A loop plays until stopped; otherwise it ends once every layer has played out. */
        Finished = !Sound->Loop && ((Instance->StartedLayers & AllLayers) == AllLayers) &&
                   ((Instance->RepeatSamples == 0U) || SOUND_LAST_PASS(Instance, Sound));
    }

    Instance->Sample += Samples;

    if(Finished && (Instance->VoiceCount == 0U))
    {
        Sound_EndInstance(Index);
    }
}

/* -------------------------------------------------------------------------- */
/* Private functions: the mixer synth                                         */
/* -------------------------------------------------------------------------- */

static void Sound_Start(void *Context)
{
    (void)Context;

    memset(Sound_Instances, 0, sizeof(Sound_Instances));
    memset(Sound_Voices, 0, sizeof(Sound_Voices));
    Sound_Random = 0x2545F491U;
    Sound_Age = 0U;

    for(uint32_t Index = 0U; Index < SOUND_MAX_PLAYING; Index++)
    {
        Sound_PublishHandle(Index, 0U);
    }
}

static void Sound_Receive(const void *Message, uint32_t Size, void *Context)
{
    Sound_CommandTypeDef Command;
    int32_t Index;

    (void)Context;

    if(Size != sizeof(Command))
    {
        return;
    }

    (void)memcpy(&Command, Message, sizeof(Command));

    if(Command.Kind == (uint8_t)SOUND_COMMAND_PLAY)
    {
        Sound_StartInstance(&Command);
        return;
    }

    if(Command.Kind == (uint8_t)SOUND_COMMAND_STOP_ALL)
    {
        for(uint32_t Instance = 0U; Instance < SOUND_MAX_PLAYING; Instance++)
        {
            if(Sound_Instances[Instance].Active)
            {
                Sound_StopInstance(Instance, Sound_FadeCoefficient(SOUND_QUICK_FADE_SECONDS, SYNTH_SAMPLE_RATE));
            }
        }

        return;
    }

    Index = Sound_FindInstance(Command.Handle);

    if(Index < 0)
    {
        return;
    }

    switch(Command.Kind)
    {
        case SOUND_COMMAND_STOP:
            Sound_StopInstance((uint32_t)Index, 0.0f);
            break;

        case SOUND_COMMAND_SET_VOLUME:
            Sound_Instances[Index].TargetVolume = Command.Value * Sound_Instances[Index].BaseVolume;
            break;

        case SOUND_COMMAND_SET_PITCH:
            Sound_Instances[Index].TargetPitch = Command.Pitch * Sound_Instances[Index].BasePitch;
            break;

        case SOUND_COMMAND_SET_CONTROL:
            Sound_Instances[Index].Control = Command.Value;
            break;

        default:
            break;
    }
}

static void Sound_Render(float *Samples, uint32_t SampleCount, void *Context)
{
    (void)Context;

    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        Samples[Index] = 0.0f;
    }

    for(uint32_t Offset = 0U; Offset < SampleCount; Offset += SOUND_CONTROL_SAMPLES)
    {
        const uint32_t Count = ((SampleCount - Offset) < SOUND_CONTROL_SAMPLES) ? (SampleCount - Offset) : SOUND_CONTROL_SAMPLES;

        for(uint32_t Instance = 0U; Instance < SOUND_MAX_PLAYING; Instance++)
        {
            if(Sound_Instances[Instance].Active)
            {
                Sound_UpdateInstance(Instance, Count);
            }
        }

        for(uint32_t Voice = 0U; Voice < SOUND_MAX_VOICES; Voice++)
        {
            if(Sound_Voices[Voice].Active)
            {
                if(!Sound_Voices[Voice].Fresh)
                {
                    Sound_UpdateVoiceControl(&Sound_Voices[Voice]);
                }

                Sound_Voices[Voice].Fresh = false;
                Sound_RenderVoice(&Sound_Voices[Voice], &Samples[Offset], Count);
            }
        }
    }

    /* Soft limiter: untouched below the knee, then bending smoothly towards full scale. */
    for(uint32_t Index = 0U; Index < SampleCount; Index++)
    {
        const float Magnitude = fabsf(Samples[Index]);

        if(Magnitude > SOUND_LIMIT_KNEE)
        {
            const float Excess = Magnitude - SOUND_LIMIT_KNEE;
            const float Limited = SOUND_LIMIT_KNEE + (((1.0f - SOUND_LIMIT_KNEE) * Excess) / ((1.0f - SOUND_LIMIT_KNEE) + Excess));

            Samples[Index] = (Samples[Index] < 0.0f) ? -Limited : Limited;
        }
    }
}

static const Mixer_SynthTypeDef Sound_Synth =
{
    .Start = Sound_Start,
    .Receive = Sound_Receive,
    .Render = Sound_Render
};

/* -------------------------------------------------------------------------- */
/* Private functions: the game loop side                                      */
/* -------------------------------------------------------------------------- */

static bool Sound_Send(Sound_CommandKindTypeDef Kind, const Sound_TypeDef *Sound, Sound_HandleTypeDef Handle, float Value, float Pitch)
{
    Sound_CommandTypeDef Command;

    memset(&Command, 0, sizeof(Command));
    Command.Kind = (uint8_t)Kind;
    Command.Sound = Sound;
    Command.Handle = Handle;
    Command.Value = Value;
    Command.Pitch = Pitch;

    return Mixer_Send(MIXER_SOUND_CHANNEL, &Command, sizeof(Command));
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Sound_Init(void)
{
    Sound_NextHandle = 0U;
    __atomic_store_n(&Sound_LastStartedHandle, 0U, __ATOMIC_RELAXED);
    (void)Mixer_PlaySynth(MIXER_SOUND_CHANNEL, &Sound_Synth, NULL);
}

Sound_HandleTypeDef Sound_Play(const Sound_TypeDef *Sound)
{
    return Sound_PlayWith(Sound, 1.0f, 1.0f);
}

Sound_HandleTypeDef Sound_PlayWith(const Sound_TypeDef *Sound, float Volume, float Pitch)
{
    Sound_HandleTypeDef Handle;

    if(Sound == NULL)
    {
        return 0U;
    }

    Sound_NextHandle++;
    Sound_NextHandle = (Sound_NextHandle == 0U) ? 1U : Sound_NextHandle;
    Handle = Sound_NextHandle;

    Volume = (Volume < 0.0f) ? 0.0f : Volume;
    Pitch = (Pitch > 0.0f) ? Pitch : 1.0f;

    return Sound_Send(SOUND_COMMAND_PLAY, Sound, Handle, Volume, Pitch) ? Handle : 0U;
}

void Sound_Stop(Sound_HandleTypeDef Handle)
{
    if(Handle != 0U)
    {
        (void)Sound_Send(SOUND_COMMAND_STOP, NULL, Handle, 0.0f, 1.0f);
    }
}

void Sound_StopAll(void)
{
    (void)Sound_Send(SOUND_COMMAND_STOP_ALL, NULL, 0U, 0.0f, 1.0f);
}

void Sound_SetVolume(Sound_HandleTypeDef Handle, float Volume)
{
    if(Handle != 0U)
    {
        (void)Sound_Send(SOUND_COMMAND_SET_VOLUME, NULL, Handle, (Volume < 0.0f) ? 0.0f : Volume, 1.0f);
    }
}

void Sound_SetPitch(Sound_HandleTypeDef Handle, float Pitch)
{
    if(Handle != 0U)
    {
        (void)Sound_Send(SOUND_COMMAND_SET_PITCH, NULL, Handle, 0.0f, (Pitch > 0.0f) ? Pitch : 1.0f);
    }
}

void Sound_SetControl(Sound_HandleTypeDef Handle, float Value)
{
    if(Handle != 0U)
    {
        (void)Sound_Send(SOUND_COMMAND_SET_CONTROL, NULL, Handle, Value, 1.0f);
    }
}

bool Sound_IsPlaying(Sound_HandleTypeDef Handle)
{
    const Sound_HandleTypeDef LastStarted = __atomic_load_n(&Sound_LastStartedHandle, __ATOMIC_ACQUIRE);

    if(Handle == 0U)
    {
        return false;
    }

    /* Sent but not yet reached by the audio: about to start. */
    if((int16_t)(Handle - LastStarted) > 0)
    {
        return true;
    }

    for(uint32_t Index = 0U; Index < SOUND_MAX_PLAYING; Index++)
    {
        if(__atomic_load_n(&Sound_PlayingHandles[Index], __ATOMIC_ACQUIRE) == Handle)
        {
            return true;
        }
    }

    return false;
}
