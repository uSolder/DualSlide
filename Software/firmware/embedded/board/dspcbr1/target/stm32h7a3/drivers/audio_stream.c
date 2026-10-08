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
#define AUDIO_STREAM_RAMP_STEP_US           1000U
#define AUDIO_STREAM_RAMP_STEP_CODES        16U

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

static Audio_StreamConfig Audio_Stream_Config;
static bool Audio_Stream_Initialized;
static bool Audio_Stream_Running;

static int16_t Audio_Stream_PCM[AUDIO_STREAM_FRAMES_PER_HALF * AUDIO_STREAM_MAX_CHANNELS];

/*
 * DMA source buffer. It lives in cacheable AXI SRAM, so each half is cleaned
 * from the data cache after it is written; the size is a whole number of
 * cache lines.
 */
static uint16_t Audio_Stream_DMABuffer[AUDIO_STREAM_FRAMES_PER_HALF * 2U] __attribute__((aligned(32)));

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static bool Audio_Stream_DisableDMA(void);
static void Audio_Stream_RampToMidscale(void);
static void Audio_Stream_SetTriggered(bool triggered);
static void Audio_Stream_FillHalf(uint32_t half);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/*
 * Raises the untriggered output from 0 V to mid-scale slowly enough that the
 * AC-coupled amplifier input sees a small, slow edge instead of a step.
 */
static void Audio_Stream_RampToMidscale(void)
{
    uint32_t code;

    DAC1->DHR12R1 = 0U;
    DAC1->CR |= DAC_CR_EN1;

    for (code = AUDIO_STREAM_RAMP_STEP_CODES; code <= AUDIO_STREAM_DAC_MIDSCALE; code += AUDIO_STREAM_RAMP_STEP_CODES)
    {
        Delay_us(AUDIO_STREAM_RAMP_STEP_US);
        DAC1->DHR12R1 = code;
    }

    DAC1->CR &= ~DAC_CR_EN1;
}

/*
 * TEN1 may only change while the channel is disabled. Untriggered, the
 * output is parked at mid-scale to avoid a step into the amplifier.
 */
static void Audio_Stream_SetTriggered(bool triggered)
{
    DAC1->CR &= ~DAC_CR_EN1;

    if (triggered)
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

static bool Audio_Stream_DisableDMA(void)
{
    uint32_t timeout = AUDIO_STREAM_DISABLE_TIMEOUT;

    AUDIO_STREAM_DMA_STREAM->CR &= ~DMA_SxCR_EN;

    while (((AUDIO_STREAM_DMA_STREAM->CR & DMA_SxCR_EN) != 0U) && (timeout != 0U))
    {
        timeout--;
    }

    AUDIO_STREAM_DMA->LIFCR = AUDIO_STREAM_DMA_CLEAR_ALL;

    return timeout != 0U;
}

static void Audio_Stream_FillHalf(uint32_t half)
{
    uint16_t *output = &Audio_Stream_DMABuffer[half * AUDIO_STREAM_FRAMES_PER_HALF];
    uint32_t frame;

    Audio_Stream_Config.FillCallback(Audio_Stream_PCM, AUDIO_STREAM_FRAMES_PER_HALF, Audio_Stream_Config.CallbackContext);

    for (frame = 0U; frame < AUDIO_STREAM_FRAMES_PER_HALF; frame++)
    {
        int32_t sample;

        if (Audio_Stream_Config.ChannelCount == 2U)
        {
            sample = ((int32_t)Audio_Stream_PCM[frame * 2U] + (int32_t)Audio_Stream_PCM[(frame * 2U) + 1U]) / 2;
        }
        else
        {
            sample = Audio_Stream_PCM[frame];
        }

        /* Signed 16-bit PCM to unsigned 12-bit right-aligned DAC code. */
        output[frame] = (uint16_t)((uint32_t)(sample + 32768) >> 4U);
    }

    SCB_CleanDCache_by_Addr((uint32_t *)output, (int32_t)(AUDIO_STREAM_FRAMES_PER_HALF * sizeof(uint16_t)));
}

/* -------------------------------------------------------------------------- */
/* Stream control                                                             */
/* -------------------------------------------------------------------------- */

bool Audio_Stream_Init(const Audio_StreamConfig *Config)
{
    uint32_t timer_clock_hz;
    uint32_t period;
    uint32_t index;
    bool dac_was_enabled;

    if ((Config == NULL) || (Config->FillCallback == NULL) || (Config->SampleRate == 0U) ||
        (Config->ChannelCount == 0U) || (Config->ChannelCount > AUDIO_STREAM_MAX_CHANNELS))
    {
        return false;
    }

    if (Audio_Stream_Running)
    {
        return false;
    }

    timer_clock_hz = RCC_GetKernelFrequency(AUDIO_STREAM_TIMER);

    if (timer_clock_hz < Config->SampleRate)
    {
        return false;
    }

    period = (timer_clock_hz + (Config->SampleRate / 2U)) / Config->SampleRate;

    if ((period < 2U) || (period > 0x10000UL))
    {
        return false;
    }

    if ((RCC_EnablePeripheralClock(GPIOA) != RCC_RESULT_OK) ||
        (RCC_EnablePeripheralClock(DAC1) != RCC_RESULT_OK) ||
        (RCC_EnablePeripheralClock(AUDIO_STREAM_TIMER) != RCC_RESULT_OK) ||
        (RCC_EnablePeripheralClock(AUDIO_STREAM_DMA) != RCC_RESULT_OK))
    {
        return false;
    }

    /* Keep the stream running while the CPU sleeps (WFI). */
    RCC->AHB1LPENR |= RCC_AHB1LPENR_DMA1LPEN;
    RCC->APB1LLPENR |= RCC_APB1LLPENR_DAC12LPEN | RCC_APB1LLPENR_TIM6LPEN;

    Audio_Stream_Config = *Config;

    /* PA4 in analog mode carries DAC1_OUT1 to the amplifier. */
    GPIOA->PUPDR &= ~(0x3UL << (AUDIO_STREAM_DAC_PIN * 2U));
    GPIOA->MODER |= AUDIO_STREAM_GPIO_MODE_ANALOG << (AUDIO_STREAM_DAC_PIN * 2U);

    /*
     * DAC1 channel 1: buffered pin output, selected to convert on TIM6 TRGO.
     * The trigger stays off until the stream starts so the output idles at
     * mid-scale. On first power-up the output is ramped to mid-scale.
     */
    dac_was_enabled = (DAC1->CR & DAC_CR_EN1) != 0U;
    DAC1->CR &= ~(DAC_CR_EN1 | DAC_CR_TEN1 | DAC_CR_TSEL1 | DAC_CR_DMAEN1);
    DAC1->MCR &= ~DAC_MCR_MODE1;

    if (!dac_was_enabled)
    {
        Audio_Stream_RampToMidscale();
    }

    DAC1->CR |= (AUDIO_STREAM_DAC_TRIGGER_TIM6 << DAC_CR_TSEL1_Pos) | DAC_CR_DMAEN1;
    Audio_Stream_SetTriggered(false);

    /* TIM6 update event at the sample rate drives TRGO. */
    AUDIO_STREAM_TIMER->CR1 = TIM_CR1_ARPE;
    AUDIO_STREAM_TIMER->CR2 = TIM_CR2_MMS_1;
    AUDIO_STREAM_TIMER->PSC = 0U;
    AUDIO_STREAM_TIMER->ARR = period - 1U;
    AUDIO_STREAM_TIMER->CNT = 0U;

    /* DMA1 stream 0: circular 16-bit memory-to-peripheral transfers. */
    if (!Audio_Stream_DisableDMA())
    {
        return false;
    }

    AUDIO_STREAM_DMAMUX_CHANNEL->CCR = AUDIO_STREAM_DMAMUX_REQUEST_DAC1 << DMAMUX_CxCR_DMAREQ_ID_Pos;
    AUDIO_STREAM_DMA_STREAM->PAR = (uint32_t)&DAC1->DHR12R1;
    AUDIO_STREAM_DMA_STREAM->M0AR = (uint32_t)Audio_Stream_DMABuffer;
    AUDIO_STREAM_DMA_STREAM->NDTR = AUDIO_STREAM_FRAMES_PER_HALF * 2U;
    AUDIO_STREAM_DMA_STREAM->FCR = 0U;
    AUDIO_STREAM_DMA_STREAM->CR = DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0 |
                                  DMA_SxCR_CIRC | DMA_SxCR_PL_1 |
                                  DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE;

    for (index = 0U; index < (AUDIO_STREAM_FRAMES_PER_HALF * 2U); index++)
    {
        Audio_Stream_DMABuffer[index] = AUDIO_STREAM_DAC_MIDSCALE;
    }

    NVIC_SetPriority(AUDIO_STREAM_DMA_IRQN, AUDIO_STREAM_IRQ_PRIORITY);
    NVIC_EnableIRQ(AUDIO_STREAM_DMA_IRQN);

    Audio_Stream_Initialized = true;

    return true;
}

bool Audio_Stream_Start(void)
{
    if (!Audio_Stream_Initialized)
    {
        return false;
    }

    if (Audio_Stream_Running)
    {
        return true;
    }

    Audio_Stream_FillHalf(0U);
    Audio_Stream_FillHalf(1U);

    AUDIO_STREAM_DMA->LIFCR = AUDIO_STREAM_DMA_CLEAR_ALL;
    AUDIO_STREAM_DMA_STREAM->NDTR = AUDIO_STREAM_FRAMES_PER_HALF * 2U;
    AUDIO_STREAM_DMA_STREAM->CR |= DMA_SxCR_EN;

    Audio_Stream_SetTriggered(true);

    AUDIO_STREAM_TIMER->CNT = 0U;
    AUDIO_STREAM_TIMER->CR1 |= TIM_CR1_CEN;

    Audio_Stream_Running = true;

    return true;
}

void Audio_Stream_Stop(void)
{
    if (!Audio_Stream_Initialized)
    {
        return;
    }

    AUDIO_STREAM_TIMER->CR1 &= ~TIM_CR1_CEN;
    (void)Audio_Stream_DisableDMA();

    Audio_Stream_SetTriggered(false);

    Audio_Stream_Running = false;
}

bool Audio_Stream_IsRunning(void)
{
    return Audio_Stream_Running;
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */

void Audio_Stream_IRQHandler(void)
{
    uint32_t status = AUDIO_STREAM_DMA->LISR;

    if ((status & DMA_LISR_TEIF0) != 0U)
    {
        Audio_Stream_Stop();
        return;
    }

    AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;

    if ((status & DMA_LISR_HTIF0) != 0U)
    {
        AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CHTIF0;
        Audio_Stream_FillHalf(0U);
    }

    if ((status & DMA_LISR_TCIF0) != 0U)
    {
        AUDIO_STREAM_DMA->LIFCR = DMA_LIFCR_CTCIF0;
        Audio_Stream_FillHalf(1U);
    }
}
