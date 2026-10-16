/**
 * @file audio_stream.c
 * @brief STM32H7A3 DAC audio output stream implementation.
 *
 * This driver implements the hardware-independent audio stream contract using
 * DAC1 channel 1 on PA4. TIM6 triggers one DAC conversion per sample, and
 * DMA1 stream 0 feeds the DAC from a circular double buffer. Each half of the
 * buffer is refilled from the stream callback when the DMA finishes with it.
 *
 * Samples are output at 12-bit resolution through DAC_DHR12R1. DSPCBR1 has a
 * single analog output, so stereo streams are mixed to mono.
 */

#include "audio_stream.h"

#include "delay.h"
#include "rcc.h"
#include "stm32h7a3xxq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

#define AUDIO_STREAM_FRAMES_PER_HALF        256U
#define AUDIO_STREAM_MAX_CHANNELS           2U
#define AUDIO_STREAM_IRQ_PRIORITY           4U
#define AUDIO_STREAM_DISABLE_TIMEOUT        100000UL

#define AUDIO_STREAM_DAC_PIN                4U
#define AUDIO_STREAM_GPIO_MODE_ANALOG       3U
#define AUDIO_STREAM_DAC_MIDSCALE           2048U
#define AUDIO_STREAM_RAMP_STEPS             1600U
#define AUDIO_STREAM_RAMP_STEP_US           100U

/* DAC channel 1 trigger selection: tim6_trgo. */
#define AUDIO_STREAM_DAC_TRIGGER_TIM6       5U

/* DMAMUX1 request line: dac1_ch1_dma. */
#define AUDIO_STREAM_DMAMUX_REQUEST_DAC1    67U

#define AUDIO_STREAM_DMA                    DMA1
#define AUDIO_STREAM_DMA_STREAM             DMA1_Stream0
#define AUDIO_STREAM_DMAMUX_CHANNEL         DMAMUX1_Channel0
#define AUDIO_STREAM_DMA_IRQN               DMA1_Stream0_IRQn
#define AUDIO_STREAM_TIMER                  TIM6

#define AUDIO_STREAM_DMA_CLEAR_ALL          (DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 | \
                                             DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0)

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static AudioStream_ConfigTypeDef AudioStream_Config;
static bool AudioStream_Initialized;
static bool AudioStream_Running;

static int16_t AudioStream_PCM[AUDIO_STREAM_FRAMES_PER_HALF * AUDIO_STREAM_MAX_CHANNELS];

/*
 * DMA source buffer. It lives in cacheable AXI SRAM, so each half is cleaned
 * from the data cache after it is written; the size is a whole number of
 * cache lines.
 */
static uint16_t AudioStream_DMABuffer[AUDIO_STREAM_FRAMES_PER_HALF * 2U] __attribute__((aligned(32)));

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool AudioStream_DisableDMA(void);
static void AudioStream_RampOutput(uint32_t StartCode, uint32_t EndCode);
static void AudioStream_SetTriggered(bool Triggered);
static void AudioStream_FillHalf(uint32_t Half);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/*
 * Moves the untriggered output from StartCode to EndCode over about 160 ms.
 * Each step is at most two codes (about 1.6 mV) every 100 us, and the S-curve
 * starts and ends with zero slope, so the AC-coupled amplifier input sees
 * neither a step nor an audible staircase; coarse 1 ms steps were heard as a
 * 1 kHz buzz. The channel is left disabled.
 */
static void AudioStream_RampOutput(uint32_t StartCode, uint32_t EndCode)
{
    const float Distance = (float)EndCode - (float)StartCode;
    uint32_t Step;

    DAC1->DHR12R1 = StartCode;
    DAC1->CR |= DAC_CR_EN1;

    if(StartCode != EndCode)
    {
        for(Step = 1U; Step <= AUDIO_STREAM_RAMP_STEPS; Step++)
        {
            const float Progress = (float)Step / (float)AUDIO_STREAM_RAMP_STEPS;
            const float Shape = Progress * Progress * (3.0f - (2.0f * Progress));

            Delay_Microseconds(AUDIO_STREAM_RAMP_STEP_US);
            DAC1->DHR12R1 = (uint32_t)(((float)StartCode + (Distance * Shape)) + 0.5f);
        }
    }

    DAC1->CR &= ~DAC_CR_EN1;
}

/*
 * TEN1 may only change while the channel is disabled. Untriggered, the
 * output is parked at mid-scale to avoid a step into the amplifier.
 */
static void AudioStream_SetTriggered(bool Triggered)
{
    DAC1->CR &= ~DAC_CR_EN1;

    if(Triggered)
    {
        DAC1->CR |= DAC_CR_TEN1;
    }
    else
    {
        DAC1->CR &= ~DAC_CR_TEN1;
        DAC1->DHR12R1 = AUDIO_STREAM_DAC_MIDSCALE;
    }

    DAC1->CR |= DAC_CR_EN1;
}

static bool AudioStream_DisableDMA(void)
{
    uint32_t Timeout = AUDIO_STREAM_DISABLE_TIMEOUT;

    AUDIO_STREAM_DMA_STREAM->CR &= ~DMA_SxCR_EN;

    while(((AUDIO_STREAM_DMA_STREAM->CR & DMA_SxCR_EN) != 0U) && (Timeout != 0U))
    {
        Timeout--;
    }

    AUDIO_STREAM_DMA->LIFCR = AUDIO_STREAM_DMA_CLEAR_ALL;

    return Timeout != 0U;
}

static void AudioStream_FillHalf(uint32_t Half)
{
    uint16_t *Output = &AudioStream_DMABuffer[Half * AUDIO_STREAM_FRAMES_PER_HALF];
    uint32_t Frame;

    AudioStream_Config.FillCallback(AudioStream_PCM, AUDIO_STREAM_FRAMES_PER_HALF, AudioStream_Config.CallbackContext);

    for(Frame = 0U; Frame < AUDIO_STREAM_FRAMES_PER_HALF; Frame++)
    {
        int32_t Sample;

        if(AudioStream_Config.ChannelCount == 2U)
        {
            Sample = ((int32_t)AudioStream_PCM[Frame * 2U] + (int32_t)AudioStream_PCM[(Frame * 2U) + 1U]) / 2;
        }
        else
        {
            Sample = AudioStream_PCM[Frame];
        }

        /* Signed 16-bit PCM to unsigned 12-bit right-aligned DAC code. */
        Output[Frame] = (uint16_t)((uint32_t)(Sample + 32768) >> 4U);
    }

    SCB_CleanDCache_by_Addr((uint32_t *)Output, (int32_t)(AUDIO_STREAM_FRAMES_PER_HALF * sizeof(uint16_t)));
}

/* -------------------------------------------------------------------------- */
/* Stream control                                                             */
/* -------------------------------------------------------------------------- */

void AudioStream_HoldOutput(void)
{
    if(AudioStream_Initialized || ((DAC1->CR & DAC_CR_EN1) != 0U))
    {
        return;
    }

    if((RCC_EnablePeripheralClock(GPIOA) != RCC_RESULT_OK) ||
       (RCC_EnablePeripheralClock(DAC1) != RCC_RESULT_OK))
    {
        return;
    }

    /* PA4 in analog mode, driven by the buffered DAC output at 0 V. */
    GPIOA->PUPDR &= ~(0x3UL << (AUDIO_STREAM_DAC_PIN * 2U));
    GPIOA->MODER |= AUDIO_STREAM_GPIO_MODE_ANALOG << (AUDIO_STREAM_DAC_PIN * 2U);

    DAC1->CR &= ~(DAC_CR_TEN1 | DAC_CR_TSEL1 | DAC_CR_DMAEN1);
    DAC1->MCR &= ~DAC_MCR_MODE1;
    DAC1->DHR12R1 = 0U;
    DAC1->CR |= DAC_CR_EN1;
}

bool AudioStream_Init(const AudioStream_ConfigTypeDef *Config)
{
    uint32_t TimerClockHz;
    uint32_t Period;
    uint32_t Index;
    uint32_t StartCode;

    if((Config == NULL) || (Config->FillCallback == NULL) || (Config->SampleRate == 0U) ||
       (Config->ChannelCount == 0U) || (Config->ChannelCount > AUDIO_STREAM_MAX_CHANNELS))
    {
        return false;
    }

    if(AudioStream_Running)
    {
        return false;
    }

    TimerClockHz = RCC_GetKernelFrequency(AUDIO_STREAM_TIMER);

    if(TimerClockHz < Config->SampleRate)
    {
        return false;
    }

    Period = (TimerClockHz + (Config->SampleRate / 2U)) / Config->SampleRate;

    if((Period < 2U) || (Period > 0x10000UL))
    {
        return false;
    }

    if((RCC_EnablePeripheralClock(GPIOA) != RCC_RESULT_OK) ||
       (RCC_EnablePeripheralClock(DAC1) != RCC_RESULT_OK) ||
       (RCC_EnablePeripheralClock(AUDIO_STREAM_TIMER) != RCC_RESULT_OK) ||
       (RCC_EnablePeripheralClock(AUDIO_STREAM_DMA) != RCC_RESULT_OK))
    {
        return false;
    }

    /* Keep the stream running while the CPU sleeps (WFI). */
    RCC->AHB1LPENR |= RCC_AHB1LPENR_DMA1LPEN;
    RCC->APB1LLPENR |= RCC_APB1LLPENR_DAC12LPEN | RCC_APB1LLPENR_TIM6LPEN;

    AudioStream_Config = *Config;

    /* PA4 in analog mode carries DAC1_OUT1 to the amplifier. */
    GPIOA->PUPDR &= ~(0x3UL << (AUDIO_STREAM_DAC_PIN * 2U));
    GPIOA->MODER |= AUDIO_STREAM_GPIO_MODE_ANALOG << (AUDIO_STREAM_DAC_PIN * 2U);

    /*
     * DAC1 channel 1: buffered pin output, selected to convert on TIM6 TRGO.
     * The trigger stays off until the stream starts so the output idles at
     * mid-scale. The output ramps there from wherever it is held: 0 V after
     * power-up, or already mid-scale when the stream is configured again.
     */
    StartCode = ((DAC1->CR & DAC_CR_EN1) != 0U) ? (DAC1->DOR1 & DAC_DOR1_DACC1DOR) : 0U;
    DAC1->CR &= ~(DAC_CR_EN1 | DAC_CR_TEN1 | DAC_CR_TSEL1 | DAC_CR_DMAEN1);
    DAC1->MCR &= ~DAC_MCR_MODE1;
    AudioStream_RampOutput(StartCode, AUDIO_STREAM_DAC_MIDSCALE);

    DAC1->CR |= (AUDIO_STREAM_DAC_TRIGGER_TIM6 << DAC_CR_TSEL1_Pos) | DAC_CR_DMAEN1;
    AudioStream_SetTriggered(false);

    /* TIM6 update event at the sample rate drives TRGO. */
    AUDIO_STREAM_TIMER->CR1 = TIM_CR1_ARPE;
    AUDIO_STREAM_TIMER->CR2 = TIM_CR2_MMS_1;
    AUDIO_STREAM_TIMER->PSC = 0U;
    AUDIO_STREAM_TIMER->ARR = Period - 1U;
    AUDIO_STREAM_TIMER->CNT = 0U;

    /* DMA1 stream 0: circular 16-bit memory-to-peripheral transfers. */
    if(!AudioStream_DisableDMA())
    {
        return false;
    }

    AUDIO_STREAM_DMAMUX_CHANNEL->CCR = AUDIO_STREAM_DMAMUX_REQUEST_DAC1 << DMAMUX_CxCR_DMAREQ_ID_Pos;
    AUDIO_STREAM_DMA_STREAM->PAR = (uint32_t)&DAC1->DHR12R1;
    AUDIO_STREAM_DMA_STREAM->M0AR = (uint32_t)AudioStream_DMABuffer;
    AUDIO_STREAM_DMA_STREAM->NDTR = AUDIO_STREAM_FRAMES_PER_HALF * 2U;
    AUDIO_STREAM_DMA_STREAM->FCR = 0U;
    AUDIO_STREAM_DMA_STREAM->CR = DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0 |
                                  DMA_SxCR_CIRC | DMA_SxCR_PL_1 |
                                  DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE;

    for(Index = 0U; Index < (AUDIO_STREAM_FRAMES_PER_HALF * 2U); Index++)
    {
        AudioStream_DMABuffer[Index] = AUDIO_STREAM_DAC_MIDSCALE;
    }

    NVIC_SetPriority(AUDIO_STREAM_DMA_IRQN, AUDIO_STREAM_IRQ_PRIORITY);
    NVIC_EnableIRQ(AUDIO_STREAM_DMA_IRQN);

    AudioStream_Initialized = true;

    return true;
}

bool AudioStream_Start(void)
{
    if(!AudioStream_Initialized)
    {
        return false;
    }

    if(AudioStream_Running)
    {
        return true;
    }

    AudioStream_FillHalf(0U);
    AudioStream_FillHalf(1U);

    AUDIO_STREAM_DMA->LIFCR = AUDIO_STREAM_DMA_CLEAR_ALL;
    AUDIO_STREAM_DMA_STREAM->NDTR = AUDIO_STREAM_FRAMES_PER_HALF * 2U;
    AUDIO_STREAM_DMA_STREAM->CR |= DMA_SxCR_EN;

    AudioStream_SetTriggered(true);

    AUDIO_STREAM_TIMER->CNT = 0U;
    AUDIO_STREAM_TIMER->CR1 |= TIM_CR1_CEN;

    AudioStream_Running = true;

    return true;
}

void AudioStream_Stop(void)
{
    if(!AudioStream_Initialized)
    {
        return;
    }

    AUDIO_STREAM_TIMER->CR1 &= ~TIM_CR1_CEN;
    (void)AudioStream_DisableDMA();

    AudioStream_SetTriggered(false);

    AudioStream_Running = false;
}

bool AudioStream_IsRunning(void)
{
    return AudioStream_Running;
}

void AudioStream_PowerDown(void)
{
    uint32_t StartCode;

    if((DAC1->CR & DAC_CR_EN1) == 0U)
    {
        return;
    }

    /* Stopping parks the output at mid-scale; ramp down from wherever it is. */
    AudioStream_Stop();
    StartCode = DAC1->DOR1 & DAC_DOR1_DACC1DOR;

    DAC1->CR &= ~DAC_CR_EN1;
    DAC1->CR &= ~(DAC_CR_TEN1 | DAC_CR_DMAEN1);
    AudioStream_RampOutput(StartCode, 0U);

    /* Hold 0 V until power is removed; the stream must be configured again to restart. */
    DAC1->DHR12R1 = 0U;
    DAC1->CR |= DAC_CR_EN1;
    AudioStream_Initialized = false;
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

void AudioStream_IRQHandler(void)
{
    uint32_t Status = AUDIO_STREAM_DMA->LISR;

    if((Status & DMA_LISR_TEIF0) != 0U)
    {
        AudioStream_Stop();
        return;
    }

    AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;

    if((Status & DMA_LISR_HTIF0) != 0U)
    {
        AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CHTIF0;
        AudioStream_FillHalf(0U);
    }

    if((Status & DMA_LISR_TCIF0) != 0U)
    {
        AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CTCIF0;
        AudioStream_FillHalf(1U);
    }
}
