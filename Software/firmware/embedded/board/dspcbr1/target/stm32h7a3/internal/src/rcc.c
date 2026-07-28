/**
 * @file rcc.c
 * @brief Internal STM32H7A3 reset and clock-control implementation.
 */

#include "rcc.h"

#include "stm32h7a3xxq.h"
#include "system_stm32h7xx.h"

#include <stddef.h>
#include <stdint.h>

/*
 * SPI2 is required by this target. The STM32H7A3 CMSIS header exposes the
 * SPI1/2/3 kernel-clock selector through CDCCIP1R.
 */
#if !defined(RCC_CDCCIP1R_SPI123SEL_Msk) || \
    !defined(RCC_CDCCIP1R_SPI123SEL_Pos)
#error "STM32H7A3 CMSIS header does not provide CDCCIP1R SPI123SEL."
#endif

/* -------------------------------------------------------------------------- */
/* Clock configuration                                                        */
/* -------------------------------------------------------------------------- */

/*
 * External and internal oscillator frequencies.
 */
#define RCC_HSE_FREQUENCY_HZ       8000000UL
#define RCC_HSI_FREQUENCY_HZ       64000000UL
#define RCC_CSI_FREQUENCY_HZ       4000000UL
#define RCC_HSI48_FREQUENCY_HZ     48000000UL

/*
 * Set to 1 when HSE is supplied by an external clock source rather than a
 * crystal or resonator.
 */
#define RCC_HSE_BYPASS_ENABLE      0U

/*
 * Maximum register polling iterations.
 */
#define RCC_TIMEOUT_ITERATIONS     1000000UL

/*
 * PLL1:
 *
 * HSE / 1 × 70 = 560 MHz VCO
 *
 * P = 280 MHz system clock
 * Q = 140 MHz peripheral clock
 * R = 280 MHz
 */
#define RCC_PLL1_M                 1U
#define RCC_PLL1_N                 70U
#define RCC_PLL1_P                 2U
#define RCC_PLL1_Q                 4U
#define RCC_PLL1_R                 2U
#define RCC_PLL1_FRACN             0U

/*
 * PLL2:
 *
 * HSE / 8 × 200 = 200 MHz VCO
 *
 * P = 5 MHz ADC clock
 *
 * The 1 MHz PLL input and 200 MHz VCO use the medium VCO range. The reduced
 * ADC clock extends the acquisition time for the 50 kΩ potentiometer inputs.
 */
#define RCC_PLL2_M                 8U
#define RCC_PLL2_N                 200U
#define RCC_PLL2_P                 40U
#define RCC_PLL2_Q                 2U
#define RCC_PLL2_R                 2U
#define RCC_PLL2_FRACN             0U

/*
 * PLL3:
 *
 * HSE / 1 × 20 = 160 MHz VCO
 *
 * R = approximately 26.667 MHz LTDC pixel clock
 */
#define RCC_PLL3_M                 1U
#define RCC_PLL3_N                 20U
#define RCC_PLL3_P                 2U
#define RCC_PLL3_Q                 2U
#define RCC_PLL3_R                 6U
#define RCC_PLL3_FRACN             0U

/*
 * Bus frequencies.
 */
#define RCC_SYSTEM_CLOCK_HZ        280000000UL
#define RCC_CPU_CLOCK_HZ           280000000UL
#define RCC_AHB_CLOCK_HZ           280000000UL
#define RCC_APB1_CLOCK_HZ          140000000UL
#define RCC_APB2_CLOCK_HZ          140000000UL
#define RCC_APB3_CLOCK_HZ          140000000UL
#define RCC_APB4_CLOCK_HZ          140000000UL

/*
 * Peripheral kernel frequencies.
 */
#define RCC_SPI123_CLOCK_HZ        140000000UL
#define RCC_SPI45_CLOCK_HZ         140000000UL
#define RCC_SPI6_CLOCK_HZ          140000000UL

#define RCC_I2C123_CLOCK_HZ        140000000UL
#define RCC_I2C4_CLOCK_HZ          140000000UL

#define RCC_USART16910_CLOCK_HZ    140000000UL
#define RCC_USART234578_CLOCK_HZ   140000000UL
#define RCC_LPUART1_CLOCK_HZ       140000000UL

#define RCC_ADC_CLOCK_HZ           5000000UL
#define RCC_LTDC_CLOCK_HZ          26666667UL
#define RCC_USB_CLOCK_HZ           48000000UL

/*
 * Bus prescalers.
 */
#define RCC_CPU_DIVIDER            1U
#define RCC_AHB_DIVIDER            1U
#define RCC_APB1_DIVIDER           2U
#define RCC_APB2_DIVIDER           2U
#define RCC_APB3_DIVIDER           2U
#define RCC_APB4_DIVIDER           2U

/*
 * Peripheral clock-source selections.
 */
#define RCC_SPI123_SOURCE_PLL1_Q   0U
#define RCC_SPI45_SOURCE_PCLK2     0U
#define RCC_SPI6_SOURCE_PCLK4      0U

#define RCC_I2C123_SOURCE_PCLK1    0U
#define RCC_I2C4_SOURCE_PCLK4      0U

#define RCC_ADC_SOURCE_PLL2_P      0U
#define RCC_LTDC_SOURCE_PLL3_R     0U
#define RCC_USB_SOURCE_HSI48       2U

/*
 * Flash wait states required at 280 MHz.
 */
#define RCC_FLASH_LATENCY          6U

/* -------------------------------------------------------------------------- */
/* Private function declarations                                              */
/* -------------------------------------------------------------------------- */

static RCC_Result RCC_WaitForSet(volatile uint32_t *reg, uint32_t mask);
static RCC_Result RCC_WaitForClear(volatile uint32_t *reg, uint32_t mask);
static RCC_Result RCC_WaitForValue(volatile uint32_t *reg, uint32_t mask, uint32_t value);
static RCC_Result RCC_ConfigurePower(void);
static RCC_Result RCC_ConfigureFlash(void);
static RCC_Result RCC_EnableOscillators(void);
static RCC_Result RCC_ConfigurePLL1(void);
static RCC_Result RCC_ConfigurePLL2(void);
static RCC_Result RCC_ConfigurePLL3(void);
static RCC_Result RCC_ConfigureBusDividers(void);
static RCC_Result RCC_ConfigurePeripheralClocks(void);
static RCC_Result RCC_SwitchSystemClock(void);
static uint32_t RCC_EncodeAHBPrescaler(uint32_t divider);
static uint32_t RCC_EncodeAPBPrescaler(uint32_t divider);
static uint32_t RCC_EncodePLLInputRange(uint32_t frequency_hz);
static void RCC_WriteField(volatile uint32_t *reg, uint32_t mask, uint32_t position, uint32_t value);

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void RCC_WriteField(volatile uint32_t *reg, uint32_t mask, uint32_t position, uint32_t value)
{
    *reg = (*reg & ~mask) | ((value << position) & mask);
}

static RCC_Result RCC_WaitForSet(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = RCC_TIMEOUT_ITERATIONS;

    while (((*reg & mask) == 0U) && (timeout > 0U))
    {
        timeout--;
    }

    return (timeout > 0U) ? RCC_RESULT_OK : RCC_RESULT_TIMEOUT;
}

static RCC_Result RCC_WaitForClear(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = RCC_TIMEOUT_ITERATIONS;

    while (((*reg & mask) != 0U) && (timeout > 0U))
    {
        timeout--;
    }

    return (timeout > 0U) ? RCC_RESULT_OK : RCC_RESULT_TIMEOUT;
}

static RCC_Result RCC_WaitForValue(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    uint32_t timeout = RCC_TIMEOUT_ITERATIONS;

    while (((*reg & mask) != value) && (timeout > 0U))
    {
        timeout--;
    }

    return (timeout > 0U) ? RCC_RESULT_OK : RCC_RESULT_TIMEOUT;
}

static uint32_t RCC_EncodeAHBPrescaler(uint32_t divider)
{
    switch (divider)
    {
        case 1U:
            return 0x0U;

        case 2U:
            return 0x8U;

        case 4U:
            return 0x9U;

        case 8U:
            return 0xAU;

        case 16U:
            return 0xBU;

        case 64U:
            return 0xCU;

        case 128U:
            return 0xDU;

        case 256U:
            return 0xEU;

        case 512U:
            return 0xFU;

        default:
            return 0x0U;
    }
}

static uint32_t RCC_EncodeAPBPrescaler(uint32_t divider)
{
    switch (divider)
    {
        case 1U:
            return 0x0U;

        case 2U:
            return 0x4U;

        case 4U:
            return 0x5U;

        case 8U:
            return 0x6U;

        case 16U:
            return 0x7U;

        default:
            return 0x0U;
    }
}

static uint32_t RCC_EncodePLLInputRange(uint32_t frequency_hz)
{
    if (frequency_hz < 2000000UL)
    {
        return 0U;
    }

    if (frequency_hz < 4000000UL)
    {
        return 1U;
    }

    if (frequency_hz < 8000000UL)
    {
        return 2U;
    }

    return 3U;
}

static RCC_Result RCC_ConfigurePower(void)
{
    RCC_Result result;

    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;

    /*
     * Select the internal LDO supply.
     */
    PWR->CR3 &= ~PWR_CR3_SMPSEN;
    PWR->CR3 |= PWR_CR3_LDOEN;

    /*
     * VOS0 is required for the 280 MHz system clock.
     */
    PWR->SRDCR &= ~PWR_SRDCR_VOS_Msk;
    PWR->SRDCR |= 3UL << PWR_SRDCR_VOS_Pos;

    result = RCC_WaitForSet(&PWR->SRDCR, PWR_SRDCR_VOSRDY);

    return result;
}

static RCC_Result RCC_ConfigureFlash(void)
{
    uint32_t expected_latency;

    expected_latency = (RCC_FLASH_LATENCY << FLASH_ACR_LATENCY_Pos) & FLASH_ACR_LATENCY_Msk;

    RCC_WriteField(&FLASH->ACR, FLASH_ACR_LATENCY_Msk, FLASH_ACR_LATENCY_Pos, RCC_FLASH_LATENCY);

    return RCC_WaitForValue(&FLASH->ACR, FLASH_ACR_LATENCY_Msk, expected_latency);
}

static RCC_Result RCC_EnableOscillators(void)
{
    RCC_Result result;

#if RCC_HSE_BYPASS_ENABLE
    RCC->CR |= RCC_CR_HSEBYP;

#ifdef RCC_CR_HSEEXT
    RCC->CR |= RCC_CR_HSEEXT;
#endif

#else
    RCC->CR &= ~RCC_CR_HSEBYP;
#endif

    RCC->CR |= RCC_CR_HSEON;

    result = RCC_WaitForSet(&RCC->CR, RCC_CR_HSERDY);

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    RCC->CR |= RCC_CR_HSI48ON;

    result = RCC_WaitForSet(&RCC->CR, RCC_CR_HSI48RDY);

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    RCC->CSR |= RCC_CSR_LSION;

    return RCC_WaitForSet(&RCC->CSR, RCC_CSR_LSIRDY);
}

static RCC_Result RCC_ConfigurePLL1(void)
{
    RCC_Result result;

    RCC->CR &= ~RCC_CR_PLL1ON;

    result = RCC_WaitForClear(&RCC->CR, RCC_CR_PLL1RDY);

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    RCC_WriteField(&RCC->PLLCKSELR, RCC_PLLCKSELR_PLLSRC_Msk, RCC_PLLCKSELR_PLLSRC_Pos, 2U);
    RCC_WriteField(&RCC->PLLCKSELR, RCC_PLLCKSELR_DIVM1_Msk, RCC_PLLCKSELR_DIVM1_Pos, RCC_PLL1_M);
    RCC_WriteField(&RCC->PLLCFGR, RCC_PLLCFGR_PLL1RGE_Msk, RCC_PLLCFGR_PLL1RGE_Pos, RCC_EncodePLLInputRange(RCC_HSE_FREQUENCY_HZ / RCC_PLL1_M));

    /*
     * Select the wide VCO range.
     */
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL1VCOSEL;

    RCC->PLLCFGR |= RCC_PLLCFGR_DIVP1EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVQ1EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVR1EN;
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL1FRACEN;

    RCC->PLL1DIVR =
        ((RCC_PLL1_N - 1U) << RCC_PLL1DIVR_N1_Pos) |
        ((RCC_PLL1_P - 1U) << RCC_PLL1DIVR_P1_Pos) |
        ((RCC_PLL1_Q - 1U) << RCC_PLL1DIVR_Q1_Pos) |
        ((RCC_PLL1_R - 1U) << RCC_PLL1DIVR_R1_Pos);

    RCC->PLL1FRACR = RCC_PLL1_FRACN << RCC_PLL1FRACR_FRACN1_Pos;

    RCC->CR |= RCC_CR_PLL1ON;

    return RCC_WaitForSet(&RCC->CR, RCC_CR_PLL1RDY);
}

static RCC_Result RCC_ConfigurePLL2(void)
{
    RCC_Result result;

    RCC->CR &= ~RCC_CR_PLL2ON;

    result = RCC_WaitForClear(&RCC->CR, RCC_CR_PLL2RDY);

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    RCC_WriteField(&RCC->PLLCKSELR, RCC_PLLCKSELR_DIVM2_Msk, RCC_PLLCKSELR_DIVM2_Pos, RCC_PLL2_M);
    RCC_WriteField(&RCC->PLLCFGR, RCC_PLLCFGR_PLL2RGE_Msk, RCC_PLLCFGR_PLL2RGE_Pos, RCC_EncodePLLInputRange(RCC_HSE_FREQUENCY_HZ / RCC_PLL2_M));

    /*
     * Select the medium VCO range.
     */
    RCC->PLLCFGR |= RCC_PLLCFGR_PLL2VCOSEL;

    RCC->PLLCFGR |= RCC_PLLCFGR_DIVP2EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVQ2EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVR2EN;
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL2FRACEN;

    RCC->PLL2DIVR =
        ((RCC_PLL2_N - 1U) << RCC_PLL2DIVR_N2_Pos) |
        ((RCC_PLL2_P - 1U) << RCC_PLL2DIVR_P2_Pos) |
        ((RCC_PLL2_Q - 1U) << RCC_PLL2DIVR_Q2_Pos) |
        ((RCC_PLL2_R - 1U) << RCC_PLL2DIVR_R2_Pos);

    RCC->PLL2FRACR = RCC_PLL2_FRACN << RCC_PLL2FRACR_FRACN2_Pos;

    RCC->CR |= RCC_CR_PLL2ON;

    return RCC_WaitForSet(&RCC->CR, RCC_CR_PLL2RDY);
}

static RCC_Result RCC_ConfigurePLL3(void)
{
    RCC_Result result;

    RCC->CR &= ~RCC_CR_PLL3ON;

    result = RCC_WaitForClear(&RCC->CR, RCC_CR_PLL3RDY);

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    RCC_WriteField(&RCC->PLLCKSELR, RCC_PLLCKSELR_DIVM3_Msk, RCC_PLLCKSELR_DIVM3_Pos, RCC_PLL3_M);
    RCC_WriteField(&RCC->PLLCFGR, RCC_PLLCFGR_PLL3RGE_Msk, RCC_PLLCFGR_PLL3RGE_Pos, RCC_EncodePLLInputRange(RCC_HSE_FREQUENCY_HZ / RCC_PLL3_M));

    /*
     * Select the wide VCO range.
     */
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL3VCOSEL;

    RCC->PLLCFGR |= RCC_PLLCFGR_DIVP3EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVQ3EN;
    RCC->PLLCFGR |= RCC_PLLCFGR_DIVR3EN;
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLL3FRACEN;

    RCC->PLL3DIVR =
        ((RCC_PLL3_N - 1U) << RCC_PLL3DIVR_N3_Pos) |
        ((RCC_PLL3_P - 1U) << RCC_PLL3DIVR_P3_Pos) |
        ((RCC_PLL3_Q - 1U) << RCC_PLL3DIVR_Q3_Pos) |
        ((RCC_PLL3_R - 1U) << RCC_PLL3DIVR_R3_Pos);

    RCC->PLL3FRACR = RCC_PLL3_FRACN << RCC_PLL3FRACR_FRACN3_Pos;

    RCC->CR |= RCC_CR_PLL3ON;

    return RCC_WaitForSet(&RCC->CR, RCC_CR_PLL3RDY);
}

static RCC_Result RCC_ConfigureBusDividers(void)
{
    RCC_WriteField(&RCC->CDCFGR1, RCC_CDCFGR1_CDCPRE_Msk, RCC_CDCFGR1_CDCPRE_Pos, RCC_EncodeAHBPrescaler(RCC_CPU_DIVIDER));
    RCC_WriteField(&RCC->CDCFGR1, RCC_CDCFGR1_HPRE_Msk, RCC_CDCFGR1_HPRE_Pos, RCC_EncodeAHBPrescaler(RCC_AHB_DIVIDER));
    RCC_WriteField(&RCC->CDCFGR1, RCC_CDCFGR1_CDPPRE_Msk, RCC_CDCFGR1_CDPPRE_Pos, RCC_EncodeAPBPrescaler(RCC_APB3_DIVIDER));
    RCC_WriteField(&RCC->CDCFGR2, RCC_CDCFGR2_CDPPRE1_Msk, RCC_CDCFGR2_CDPPRE1_Pos, RCC_EncodeAPBPrescaler(RCC_APB1_DIVIDER));
    RCC_WriteField(&RCC->CDCFGR2, RCC_CDCFGR2_CDPPRE2_Msk, RCC_CDCFGR2_CDPPRE2_Pos, RCC_EncodeAPBPrescaler(RCC_APB2_DIVIDER));
    RCC_WriteField(&RCC->SRDCFGR, RCC_SRDCFGR_SRDPPRE_Msk, RCC_SRDCFGR_SRDPPRE_Pos, RCC_EncodeAPBPrescaler(RCC_APB4_DIVIDER));

    return RCC_RESULT_OK;
}

static RCC_Result RCC_ConfigurePeripheralClocks(void)
{
    /*
     * SPI1/2/3 use PLL1-Q. SPI2 is required by this target, so this
     * configuration is mandatory.
     */
    RCC_WriteField(&RCC->CDCCIP1R, RCC_CDCCIP1R_SPI123SEL_Msk, RCC_CDCCIP1R_SPI123SEL_Pos, RCC_SPI123_SOURCE_PLL1_Q);

    if ((RCC->CDCCIP1R & RCC_CDCCIP1R_SPI123SEL_Msk) != ((RCC_SPI123_SOURCE_PLL1_Q << RCC_CDCCIP1R_SPI123SEL_Pos) & RCC_CDCCIP1R_SPI123SEL_Msk))
    {
        return RCC_RESULT_CLOCK_FAILURE;
    }

#ifdef RCC_D2CCIP1R_SPI45SEL_Msk
    RCC_WriteField(&RCC->D2CCIP1R, RCC_D2CCIP1R_SPI45SEL_Msk, RCC_D2CCIP1R_SPI45SEL_Pos, RCC_SPI45_SOURCE_PCLK2);
#endif

#ifdef RCC_D3CCIPR_SPI6SEL_Msk
    RCC_WriteField(&RCC->D3CCIPR, RCC_D3CCIPR_SPI6SEL_Msk, RCC_D3CCIPR_SPI6SEL_Pos, RCC_SPI6_SOURCE_PCLK4);
#endif

#ifdef RCC_D2CCIP2R_I2C123SEL_Msk
    RCC_WriteField(&RCC->D2CCIP2R, RCC_D2CCIP2R_I2C123SEL_Msk, RCC_D2CCIP2R_I2C123SEL_Pos, RCC_I2C123_SOURCE_PCLK1);
#endif

#ifdef RCC_D3CCIPR_I2C4SEL_Msk
    RCC_WriteField(&RCC->D3CCIPR, RCC_D3CCIPR_I2C4SEL_Msk, RCC_D3CCIPR_I2C4SEL_Pos, RCC_I2C4_SOURCE_PCLK4);
#endif

#ifdef RCC_D3CCIPR_ADCSEL_Msk
    RCC_WriteField(&RCC->D3CCIPR, RCC_D3CCIPR_ADCSEL_Msk, RCC_D3CCIPR_ADCSEL_Pos, RCC_ADC_SOURCE_PLL2_P);
#endif

#ifdef RCC_D1CCIPR_LTDCSEL_Msk
    RCC_WriteField(&RCC->D1CCIPR, RCC_D1CCIPR_LTDCSEL_Msk, RCC_D1CCIPR_LTDCSEL_Pos, RCC_LTDC_SOURCE_PLL3_R);
#endif

#ifdef RCC_D2CCIP2R_USBSEL_Msk
    RCC_WriteField(&RCC->D2CCIP2R, RCC_D2CCIP2R_USBSEL_Msk, RCC_D2CCIP2R_USBSEL_Pos, RCC_USB_SOURCE_HSI48);
#endif

    return RCC_RESULT_OK;
}

static RCC_Result RCC_SwitchSystemClock(void)
{
    RCC_WriteField(&RCC->CFGR, RCC_CFGR_SW_Msk, RCC_CFGR_SW_Pos, 3U);

    return RCC_WaitForValue(&RCC->CFGR, RCC_CFGR_SWS_Msk, 3U << RCC_CFGR_SWS_Pos);
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

RCC_Result RCC_Init(void)
{
    RCC_Result result;

    /*
     * Enable all peripheral kernel-clock gates. Individual peripheral bus
     * clocks remain controlled by RCC_EnablePeripheralClock().
     */
    RCC->CKGAENR = 0xFFFFFFFFUL;
    (void)RCC->CKGAENR;

    result = RCC_ConfigurePower();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigureFlash();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_EnableOscillators();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigurePLL1();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigurePLL2();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigurePLL3();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigureBusDividers();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_ConfigurePeripheralClocks();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    result = RCC_SwitchSystemClock();

    if (result != RCC_RESULT_OK)
    {
        return result;
    }

    SystemCoreClock = RCC_CPU_CLOCK_HZ;

    return RCC_RESULT_OK;
}

uint32_t RCC_GetKernelFrequency(const void *peripheral)
{
    if (peripheral == NULL)
    {
        return 0U;
    }

    /*
     * SPI.
     */
    if ((peripheral == SPI1) || (peripheral == SPI2) || (peripheral == SPI3))
    {
        return RCC_SPI123_CLOCK_HZ;
    }

    if ((peripheral == SPI4) || (peripheral == SPI5))
    {
        return RCC_SPI45_CLOCK_HZ;
    }

#ifdef SPI6
    if (peripheral == SPI6)
    {
        return RCC_SPI6_CLOCK_HZ;
    }
#endif

    /*
     * I2C.
     */
    if ((peripheral == I2C1) || (peripheral == I2C2) || (peripheral == I2C3))
    {
        return RCC_I2C123_CLOCK_HZ;
    }

#ifdef I2C4
    if (peripheral == I2C4)
    {
        return RCC_I2C4_CLOCK_HZ;
    }
#endif

    /*
     * Timers on APB1.
     *
     * Timer clocks run at twice PCLK when the APB prescaler is greater than
     * one.
     */
    if ((peripheral == TIM2) || (peripheral == TIM3) || (peripheral == TIM4) || (peripheral == TIM5) || (peripheral == TIM6) || (peripheral == TIM7) || (peripheral == TIM12) || (peripheral == TIM13) || (peripheral == TIM14))
    {
        return RCC_APB1_CLOCK_HZ * 2U;
    }

    /*
     * Timers on APB2.
     */
    if ((peripheral == TIM1) || (peripheral == TIM8) || (peripheral == TIM15) || (peripheral == TIM16) || (peripheral == TIM17))
    {
        return RCC_APB2_CLOCK_HZ * 2U;
    }

    /*
     * USART and UART.
     */
    if ((peripheral == USART1) || (peripheral == USART6))
    {
        return RCC_USART16910_CLOCK_HZ;
    }

    if ((peripheral == USART2) || (peripheral == USART3) || (peripheral == UART4) || (peripheral == UART5) || (peripheral == UART7) || (peripheral == UART8))
    {
        return RCC_USART234578_CLOCK_HZ;
    }

#ifdef LPUART1
    if (peripheral == LPUART1)
    {
        return RCC_LPUART1_CLOCK_HZ;
    }
#endif

    /*
     * GPIO.
     */
    if ((peripheral == GPIOA) || (peripheral == GPIOB) || (peripheral == GPIOC) || (peripheral == GPIOD) || (peripheral == GPIOE) || (peripheral == GPIOF) || (peripheral == GPIOG) || (peripheral == GPIOH) || (peripheral == GPIOI) || (peripheral == GPIOJ) || (peripheral == GPIOK))
    {
        return RCC_AHB_CLOCK_HZ;
    }

#ifdef ADC1
    if ((peripheral == ADC1) || (peripheral == ADC2))
    {
        return RCC_ADC_CLOCK_HZ;
    }
#endif

#ifdef LTDC
    if (peripheral == LTDC)
    {
        return RCC_LTDC_CLOCK_HZ;
    }
#endif

#ifdef USB_OTG_FS
    if (peripheral == USB_OTG_FS)
    {
        return RCC_USB_CLOCK_HZ;
    }
#endif

#ifdef USB_OTG_HS
    if (peripheral == USB_OTG_HS)
    {
        return RCC_USB_CLOCK_HZ;
    }
#endif

    return 0U;
}

uint32_t RCC_GetSystemClockFrequency(void)
{
    return RCC_SYSTEM_CLOCK_HZ;
}

uint32_t RCC_GetCPUClockFrequency(void)
{
    return RCC_CPU_CLOCK_HZ;
}

uint32_t RCC_GetAHBClockFrequency(void)
{
    return RCC_AHB_CLOCK_HZ;
}

RCC_Result RCC_EnablePeripheralClock(const void *peripheral)
{
#define RCC_ENABLE_IF(PERIPHERAL, REGISTER, MASK) \
    do                                            \
    {                                             \
        if (peripheral == (const void *)(PERIPHERAL)) \
        {                                         \
            RCC->REGISTER |= (MASK);              \
            (void)RCC->REGISTER;                  \
            return RCC_RESULT_OK;                 \
        }                                         \
    } while (0)

    RCC_ENABLE_IF(GPIOA, AHB4ENR, RCC_AHB4ENR_GPIOAEN);
    RCC_ENABLE_IF(GPIOB, AHB4ENR, RCC_AHB4ENR_GPIOBEN);
    RCC_ENABLE_IF(GPIOC, AHB4ENR, RCC_AHB4ENR_GPIOCEN);
    RCC_ENABLE_IF(GPIOD, AHB4ENR, RCC_AHB4ENR_GPIODEN);
    RCC_ENABLE_IF(GPIOE, AHB4ENR, RCC_AHB4ENR_GPIOEEN);
    RCC_ENABLE_IF(GPIOF, AHB4ENR, RCC_AHB4ENR_GPIOFEN);
    RCC_ENABLE_IF(GPIOG, AHB4ENR, RCC_AHB4ENR_GPIOGEN);
    RCC_ENABLE_IF(GPIOH, AHB4ENR, RCC_AHB4ENR_GPIOHEN);
    RCC_ENABLE_IF(GPIOI, AHB4ENR, RCC_AHB4ENR_GPIOIEN);
    RCC_ENABLE_IF(GPIOJ, AHB4ENR, RCC_AHB4ENR_GPIOJEN);
    RCC_ENABLE_IF(GPIOK, AHB4ENR, RCC_AHB4ENR_GPIOKEN);

    RCC_ENABLE_IF(SPI1, APB2ENR, RCC_APB2ENR_SPI1EN);
    RCC_ENABLE_IF(SPI2, APB1LENR, RCC_APB1LENR_SPI2EN);
    RCC_ENABLE_IF(SPI3, APB1LENR, RCC_APB1LENR_SPI3EN);
    RCC_ENABLE_IF(SPI4, APB2ENR, RCC_APB2ENR_SPI4EN);
    RCC_ENABLE_IF(SPI5, APB2ENR, RCC_APB2ENR_SPI5EN);
    RCC_ENABLE_IF(SPI6, APB4ENR, RCC_APB4ENR_SPI6EN);

    RCC_ENABLE_IF(I2C1, APB1LENR, RCC_APB1LENR_I2C1EN);
    RCC_ENABLE_IF(I2C2, APB1LENR, RCC_APB1LENR_I2C2EN);
    RCC_ENABLE_IF(I2C3, APB1LENR, RCC_APB1LENR_I2C3EN);
    RCC_ENABLE_IF(I2C4, APB4ENR, RCC_APB4ENR_I2C4EN);

    RCC_ENABLE_IF(TIM1, APB2ENR, RCC_APB2ENR_TIM1EN);
    RCC_ENABLE_IF(TIM2, APB1LENR, RCC_APB1LENR_TIM2EN);
    RCC_ENABLE_IF(TIM3, APB1LENR, RCC_APB1LENR_TIM3EN);
    RCC_ENABLE_IF(TIM4, APB1LENR, RCC_APB1LENR_TIM4EN);
    RCC_ENABLE_IF(TIM5, APB1LENR, RCC_APB1LENR_TIM5EN);
    RCC_ENABLE_IF(TIM6, APB1LENR, RCC_APB1LENR_TIM6EN);
    RCC_ENABLE_IF(TIM7, APB1LENR, RCC_APB1LENR_TIM7EN);
    RCC_ENABLE_IF(TIM8, APB2ENR, RCC_APB2ENR_TIM8EN);

    RCC_ENABLE_IF(DMA1, AHB1ENR, RCC_AHB1ENR_DMA1EN);
    RCC_ENABLE_IF(DMA2, AHB1ENR, RCC_AHB1ENR_DMA2EN);

    RCC_ENABLE_IF(ADC1, AHB1ENR, RCC_AHB1ENR_ADC12EN);
    RCC_ENABLE_IF(ADC2, AHB1ENR, RCC_AHB1ENR_ADC12EN);

    RCC_ENABLE_IF(DAC1, APB1LENR, RCC_APB1LENR_DAC12EN);
    RCC_ENABLE_IF(LTDC, APB3ENR, RCC_APB3ENR_LTDCEN);

#undef RCC_ENABLE_IF

    return RCC_RESULT_UNSUPPORTED;
}

RCC_Result RCC_DisablePeripheralClock(const void *peripheral)
{
#define RCC_DISABLE_IF(PERIPHERAL, REGISTER, MASK) \
    do                                             \
    {                                              \
        if (peripheral == (const void *)(PERIPHERAL)) \
        {                                          \
            RCC->REGISTER &= ~(MASK);              \
            (void)RCC->REGISTER;                   \
            return RCC_RESULT_OK;                  \
        }                                          \
    } while (0)

    RCC_DISABLE_IF(GPIOA, AHB4ENR, RCC_AHB4ENR_GPIOAEN);
    RCC_DISABLE_IF(GPIOB, AHB4ENR, RCC_AHB4ENR_GPIOBEN);
    RCC_DISABLE_IF(GPIOC, AHB4ENR, RCC_AHB4ENR_GPIOCEN);
    RCC_DISABLE_IF(GPIOD, AHB4ENR, RCC_AHB4ENR_GPIODEN);
    RCC_DISABLE_IF(GPIOE, AHB4ENR, RCC_AHB4ENR_GPIOEEN);
    RCC_DISABLE_IF(GPIOF, AHB4ENR, RCC_AHB4ENR_GPIOFEN);
    RCC_DISABLE_IF(GPIOG, AHB4ENR, RCC_AHB4ENR_GPIOGEN);
    RCC_DISABLE_IF(GPIOH, AHB4ENR, RCC_AHB4ENR_GPIOHEN);
    RCC_DISABLE_IF(GPIOI, AHB4ENR, RCC_AHB4ENR_GPIOIEN);
    RCC_DISABLE_IF(GPIOJ, AHB4ENR, RCC_AHB4ENR_GPIOJEN);
    RCC_DISABLE_IF(GPIOK, AHB4ENR, RCC_AHB4ENR_GPIOKEN);

    RCC_DISABLE_IF(SPI1, APB2ENR, RCC_APB2ENR_SPI1EN);
    RCC_DISABLE_IF(SPI2, APB1LENR, RCC_APB1LENR_SPI2EN);
    RCC_DISABLE_IF(SPI3, APB1LENR, RCC_APB1LENR_SPI3EN);
    RCC_DISABLE_IF(SPI4, APB2ENR, RCC_APB2ENR_SPI4EN);
    RCC_DISABLE_IF(SPI5, APB2ENR, RCC_APB2ENR_SPI5EN);
    RCC_DISABLE_IF(SPI6, APB4ENR, RCC_APB4ENR_SPI6EN);

    RCC_DISABLE_IF(I2C1, APB1LENR, RCC_APB1LENR_I2C1EN);
    RCC_DISABLE_IF(I2C2, APB1LENR, RCC_APB1LENR_I2C2EN);
    RCC_DISABLE_IF(I2C3, APB1LENR, RCC_APB1LENR_I2C3EN);
    RCC_DISABLE_IF(I2C4, APB4ENR, RCC_APB4ENR_I2C4EN);

    RCC_DISABLE_IF(DMA1, AHB1ENR, RCC_AHB1ENR_DMA1EN);
    RCC_DISABLE_IF(DMA2, AHB1ENR, RCC_AHB1ENR_DMA2EN);

    RCC_DISABLE_IF(ADC1, AHB1ENR, RCC_AHB1ENR_ADC12EN);
    RCC_DISABLE_IF(ADC2, AHB1ENR, RCC_AHB1ENR_ADC12EN);

    RCC_DISABLE_IF(DAC1, APB1LENR, RCC_APB1LENR_DAC12EN);
    RCC_DISABLE_IF(LTDC, APB3ENR, RCC_APB3ENR_LTDCEN);

#undef RCC_DISABLE_IF

    return RCC_RESULT_UNSUPPORTED;
}

RCC_Result RCC_ResetPeripheral(const void *peripheral)
{
#define RCC_RESET_IF(PERIPHERAL, REGISTER, MASK) \
    do                                           \
    {                                            \
        if (peripheral == (const void *)(PERIPHERAL)) \
        {                                        \
            RCC->REGISTER |= (MASK);             \
            (void)RCC->REGISTER;                 \
            RCC->REGISTER &= ~(MASK);            \
            (void)RCC->REGISTER;                 \
            return RCC_RESULT_OK;                \
        }                                        \
    } while (0)

    RCC_RESET_IF(GPIOA, AHB4RSTR, RCC_AHB4RSTR_GPIOARST);
    RCC_RESET_IF(GPIOB, AHB4RSTR, RCC_AHB4RSTR_GPIOBRST);
    RCC_RESET_IF(GPIOC, AHB4RSTR, RCC_AHB4RSTR_GPIOCRST);
    RCC_RESET_IF(GPIOD, AHB4RSTR, RCC_AHB4RSTR_GPIODRST);
    RCC_RESET_IF(GPIOE, AHB4RSTR, RCC_AHB4RSTR_GPIOERST);
    RCC_RESET_IF(GPIOF, AHB4RSTR, RCC_AHB4RSTR_GPIOFRST);
    RCC_RESET_IF(GPIOG, AHB4RSTR, RCC_AHB4RSTR_GPIOGRST);
    RCC_RESET_IF(GPIOH, AHB4RSTR, RCC_AHB4RSTR_GPIOHRST);

    RCC_RESET_IF(SPI1, APB2RSTR, RCC_APB2RSTR_SPI1RST);
    RCC_RESET_IF(SPI2, APB1LRSTR, RCC_APB1LRSTR_SPI2RST);
    RCC_RESET_IF(SPI3, APB1LRSTR, RCC_APB1LRSTR_SPI3RST);
    RCC_RESET_IF(SPI4, APB2RSTR, RCC_APB2RSTR_SPI4RST);
    RCC_RESET_IF(SPI5, APB2RSTR, RCC_APB2RSTR_SPI5RST);
    RCC_RESET_IF(SPI6, APB4RSTR, RCC_APB4RSTR_SPI6RST);

    RCC_RESET_IF(I2C1, APB1LRSTR, RCC_APB1LRSTR_I2C1RST);
    RCC_RESET_IF(I2C2, APB1LRSTR, RCC_APB1LRSTR_I2C2RST);
    RCC_RESET_IF(I2C3, APB1LRSTR, RCC_APB1LRSTR_I2C3RST);
    RCC_RESET_IF(I2C4, APB4RSTR, RCC_APB4RSTR_I2C4RST);

    RCC_RESET_IF(ADC1, AHB1RSTR, RCC_AHB1RSTR_ADC12RST);
    RCC_RESET_IF(ADC2, AHB1RSTR, RCC_AHB1RSTR_ADC12RST);

    RCC_RESET_IF(DAC1, APB1LRSTR, RCC_APB1LRSTR_DAC12RST);
    RCC_RESET_IF(LTDC, APB3RSTR, RCC_APB3RSTR_LTDCRST);

#undef RCC_RESET_IF

    return RCC_RESULT_UNSUPPORTED;
}