/**
 * @file audio.c
 * @brief Embedded implementation of the platform-neutral audio contract.
 *
 * This backend forwards the application's PCM stream to the target audio
 * output stream (audio_stream.h), which owns the DAC, timer, and DMA.
 *
 * DSPCBR1 output chain: PA4 (DAC1_OUT1, 12-bit) -> 220 nF -> PAM8302A class-D
 * amplifier (24 dB, battery supplied) -> 6 ohm, 1 W speaker.
 *
 * Every sample passes through the same chain, whatever the application
 * supplies:
 *
 * - A makeup gain raises typical program loudness toward the speaker rating.
 * - A soft clipper rounds off peaks so the speaker voltage never exceeds a
 *   limit just below the amplifier's clipping point.
 * - A power limiter measures the average speaker power and lowers the gain
 *   whenever it would exceed the speaker's continuous rating.
 */

#include "audio.h"

#include "audio_stream.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Output chain                                                               */
/* -------------------------------------------------------------------------- */

/*
 * Largest speaker peak voltage. The bridge-tied output swings up to about the
 * battery voltage, so 3.3 V stays unclipped down to a nearly empty cell. A
 * full-scale sine at this peak delivers 3.3^2 / (2 x 6) = 0.91 W.
 */
#define AUDIO_SPEAKER_PEAK_LIMIT_MV       3300U

/* PAM8302A gain with no external input resistors: 24 dB = 15.85 V/V. */
#define AUDIO_AMPLIFIER_GAIN_X100         1585U

/* DAC reference; the AC-coupled swing is +/- half of it around mid-scale. */
#define AUDIO_DAC_REFERENCE_MV            3300U

/* Largest permitted PA4 swing about mid-scale: ~208 mV. */
#define AUDIO_DAC_PEAK_LIMIT_MV           ((AUDIO_SPEAKER_PEAK_LIMIT_MV * 100U) / AUDIO_AMPLIFIER_GAIN_X100)

/* The same limit in 12-bit DAC codes about mid-scale, rounded down: 258 codes. */
#define AUDIO_DAC_CODE_LIMIT              ((AUDIO_DAC_PEAK_LIMIT_MV * 4096U) / AUDIO_DAC_REFERENCE_MV)

/*
 * Q15 gain mapping full-scale PCM onto +/- AUDIO_DAC_CODE_LIMIT codes. The
 * stream keeps the top 12 bits of each sample, so one code is 16 PCM steps.
 */
#define AUDIO_OUTPUT_GAIN_Q15             (AUDIO_DAC_CODE_LIMIT * 16)

_Static_assert(AUDIO_DAC_CODE_LIMIT >= 1U, "Speaker limit is below one DAC code");
_Static_assert(AUDIO_OUTPUT_GAIN_Q15 < 32768, "Output gain must attenuate");

/* -------------------------------------------------------------------------- */
/* Power limiter                                                              */
/* -------------------------------------------------------------------------- */

#define AUDIO_SPEAKER_IMPEDANCE_OHMS      (6.0f)
#define AUDIO_SPEAKER_POWER_LIMIT_W       (1.0f)

/* Speaker power of a full-scale square wave at the peak limit: ~1.8 W. */
#define AUDIO_FULL_SCALE_POWER_W          ((((float)AUDIO_SPEAKER_PEAK_LIMIT_MV / 1000.0f) * \
                                            ((float)AUDIO_SPEAKER_PEAK_LIMIT_MV / 1000.0f)) / \
                                            AUDIO_SPEAKER_IMPEDANCE_OHMS)

/*
 * Averaging time for the measured speaker power, well inside the thermal time
 * constant of a small voice coil, and the times for the limiter gain to fall
 * while the average is too high and to recover once it drops.
 */
#define AUDIO_POWER_AVERAGE_SECONDS       (0.2f)
#define AUDIO_GAIN_ATTACK_SECONDS         (0.05f)
#define AUDIO_GAIN_RELEASE_SECONDS        (1.0f)

/* -------------------------------------------------------------------------- */
/* Loudness                                                                   */
/* -------------------------------------------------------------------------- */

/*
 * Makeup gain applied before the soft clipper: 4.0 = +12 dB. Program material
 * averages well below its peaks, so this raises typical loudness toward the
 * speaker rating; the clipper rounds off the peaks it pushes over the limit.
 * Lower it for cleaner, quieter output.
 */
#define AUDIO_LOUDNESS_GAIN               (4.0f)

/* Fraction of full scale passed linearly; above it the clipper compresses. */
#define AUDIO_SOFT_CLIP_KNEE              (0.7f)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static Audio_FillCallbackTypeDef Audio_ApplicationCallback;
static void *Audio_ApplicationContext;
static uint8_t Audio_ChannelCount;
static uint32_t Audio_SampleRateHz;

static float Audio_AveragePowerW;
static float Audio_LimiterGain;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/*
 * Linear below the knee, then a tanh curve that approaches full scale
 * smoothly, so the output never exceeds +/-1.0 (the speaker peak limit).
 */
static float Audio_SoftClip(float Sample)
{
    float Magnitude = fabsf(Sample);
    float Clipped;

    if(Magnitude <= AUDIO_SOFT_CLIP_KNEE)
    {
        return Sample;
    }

    Clipped = AUDIO_SOFT_CLIP_KNEE +
              ((1.0f - AUDIO_SOFT_CLIP_KNEE) * tanhf((Magnitude - AUDIO_SOFT_CLIP_KNEE) / (1.0f - AUDIO_SOFT_CLIP_KNEE)));

    return (Sample < 0.0f) ? -Clipped : Clipped;
}

static void Audio_FillProtected(int16_t *Buffer, uint32_t FrameCount, void *Context)
{
    uint32_t SampleCount = FrameCount * Audio_ChannelCount;
    float BlockSeconds = (float)FrameCount / (float)Audio_SampleRateHz;
    float Gain = AUDIO_LOUDNESS_GAIN * Audio_LimiterGain * (1.0f / 32768.0f);
    float SumOfSquares = 0.0f;
    float BlockPowerW;
    float TargetGain = 1.0f;
    float TimeConstant;
    uint32_t Index;

    (void)Context;

    Audio_ApplicationCallback(Buffer, FrameCount, Audio_ApplicationContext);

    if(SampleCount == 0U)
    {
        return;
    }

    /* Makeup and limiter gain, soft clip, then scale to the speaker peak limit. */
    for(Index = 0U; Index < SampleCount; Index++)
    {
        float Output = Audio_SoftClip((float)Buffer[Index] * Gain);

        SumOfSquares += Output * Output;
        Buffer[Index] = (int16_t)(Output * (float)AUDIO_OUTPUT_GAIN_Q15);
    }

    /*
     * Average speaker power actually produced. Treating stereo channels as
     * summed power overestimates the mixed-down output, which errs on the
     * safe side.
     */
    BlockPowerW = (SumOfSquares / (float)SampleCount) * AUDIO_FULL_SCALE_POWER_W;
    Audio_AveragePowerW += (BlockPowerW - Audio_AveragePowerW) * (BlockSeconds / AUDIO_POWER_AVERAGE_SECONDS);

    if(Audio_AveragePowerW > AUDIO_SPEAKER_POWER_LIMIT_W)
    {
        TargetGain = Audio_LimiterGain * sqrtf(AUDIO_SPEAKER_POWER_LIMIT_W / Audio_AveragePowerW);
    }

    TimeConstant = (TargetGain < Audio_LimiterGain) ? AUDIO_GAIN_ATTACK_SECONDS : AUDIO_GAIN_RELEASE_SECONDS;
    Audio_LimiterGain += (TargetGain - Audio_LimiterGain) * (BlockSeconds / TimeConstant);

    if(Audio_LimiterGain > 1.0f)
    {
        Audio_LimiterGain = 1.0f;
    }

}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool Audio_Init(const Audio_ConfigTypeDef *Config)
{
    AudioStream_ConfigTypeDef StreamConfig;

    if((Config == NULL) || (Config->FillCallback == NULL) || (Config->SampleRateHz == 0U) ||
       AudioStream_IsRunning())
    {
        return false;
    }

    Audio_ApplicationCallback = Config->FillCallback;
    Audio_ApplicationContext = Config->CallbackContext;
    Audio_ChannelCount = Config->ChannelCount;
    Audio_SampleRateHz = Config->SampleRateHz;
    Audio_AveragePowerW = 0.0f;
    Audio_LimiterGain = 1.0f;

    StreamConfig.SampleRate = Config->SampleRateHz;
    StreamConfig.ChannelCount = Config->ChannelCount;
    StreamConfig.FillCallback = Audio_FillProtected;
    StreamConfig.CallbackContext = NULL;

    return AudioStream_Init(&StreamConfig);
}

bool Audio_Start(void)
{
    return AudioStream_Start();
}

void Audio_Stop(void)
{
    AudioStream_Stop();
}

bool Audio_IsRunning(void)
{
    return AudioStream_IsRunning();
}
