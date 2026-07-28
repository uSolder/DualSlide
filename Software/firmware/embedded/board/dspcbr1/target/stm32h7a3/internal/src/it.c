/**
 * @file it.c
 * @brief STM32H7A3 exception and peripheral interrupt handlers.
 */

#include "STM32H7xx.h"

//#include "STM32H7A3_DMA.h"
#include "display_controller.h"

/**
 * @brief Halt execution when an unexpected interrupt occurs.
 */
static void IT_UnhandledInterrupt(void)
{
    for(;;)
    {
        __BKPT(0);
    }
}

void NMI_Handler(void)
{
    IT_UnhandledInterrupt();
}

void HardFault_Handler(void)
{
    IT_UnhandledInterrupt();
}

void MemManage_Handler(void)
{
    IT_UnhandledInterrupt();
}

void BusFault_Handler(void)
{
    IT_UnhandledInterrupt();
}

void UsageFault_Handler(void)
{
    IT_UnhandledInterrupt();
}

void SVC_Handler(void)
{
    IT_UnhandledInterrupt();
}

void DebugMon_Handler(void)
{
    IT_UnhandledInterrupt();
}

void PendSV_Handler(void)
{
    IT_UnhandledInterrupt();
}
/*
void SysTick_Handler(void)
{
    IT_UnhandledInterrupt();
}
*/
void WWDG_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void PVD_PVM_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void RTC_TAMP_STAMP_CSS_LSE_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void RTC_WKUP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void FLASH_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void RCC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMA_STR0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 0);
}

void DMA_STR1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //MA_IRQHandler(DMA1, 1);
}

void DMA_STR2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 2);
}

void DMA_STR3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 3);
}

void DMA_STR4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 4);
}

void DMA_STR5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 5);
}

void DMA_STR6_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 6);
}

void ADC1_2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void FDCAN1_IT0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void FDCAN2_IT0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void FDCAN1_IT1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void FDCAN2_IT1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI9_5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM1_BRK_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM1_UP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM1_TRG_COM_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM1_CC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C1_EV_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C1_ER_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C2_EV_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C2_ER_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void USART1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void USART2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void USART3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void EXTI15_10_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void RTC_ALARM_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM8_BRK_TIM12_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM8_UP_TIM13_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM8_CC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMA1_STR7_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA1, 7);
}

void FMC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SDMMC1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void UART4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void UART5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMA2_STR0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 0);
}

void DMA2_STR1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 1);
}

void DMA2_STR2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 2);
}

void DMA2_STR3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 3);
}

void DMA2_STR4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 4);
}

void ETH_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void ETH_WKUP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMA2_STR5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 5);
}

void DMA2_STR6_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 6);
}

void DMA2_STR7_IRQHandler(void)
{
    IT_UnhandledInterrupt();
    //DMA_IRQHandler(DMA2, 7);
}

void USART6_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C3_EV_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C3_ER_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void OTG_HS_EP1_OUT_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void OTG_HS_EP1_IN_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void OTG_HS_WKUP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void OTG_HS_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DCMI_PSSI_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void CRYP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void UART7_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void UART8_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SPI6_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void LTDC_IRQHandler(void)
{
    DisplayController_IRQHandler();
}

void LTDC_ER_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMA2D_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SAI2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void QUADSPI_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void LPTIM1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void CEC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C4_EV_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void I2C4_ER_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMAMUX1_OV_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DFSDM1_FLT0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DFSDM1_FLT1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DFSDM1_FLT2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DFSDM1_FLT3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SWPMI1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM15_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM16_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void TIM17_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void MDIOS_WKUP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void MDIOS_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void JPEG_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void MDMA_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void SDMMC_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void HSEM0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DAC2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void DMAMUX2_OVR_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH0_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH4_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA2_CH5_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA_CH6_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA1_CH7_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void COMP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void LPTIM2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void LPTIM3_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void LPUART_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void WWDG1_RST_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void CRS_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void WKUP_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void OCTOSPI2_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}

void BDMA1_IRQHandler(void)
{
    IT_UnhandledInterrupt();
}