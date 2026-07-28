/*
******************************************************************************
**
**  File        : STM32H7A3RGT6_FLASH.ld
**
**  Author      : STM32CubeIDE / modified for DualSlide
**
**  Abstract    : Linker script for STM32H7A3.
**
**                The 1 MB AXI SRAM is divided into its three physical banks:
**
**                  AXI SRAM1: 256 KB at 0x24000000
**                  AXI SRAM2: 384 KB at 0x24040000
**                  AXI SRAM3: 384 KB at 0x240A0000
**
**                Normal program data, BSS, heap, and stack use AXI SRAM1.
**                LTDC framebuffer 0 uses AXI SRAM2.
**                LTDC framebuffer 1 uses AXI SRAM3.
**
******************************************************************************
*/

/* Entry point */
ENTRY(Reset_Handler)

/* Heap and stack requirements */
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

/* Memory areas */
MEMORY
{
  ITCMRAM   (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH     (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  DTCMRAM1  (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
  DTCMRAM2  (xrw) : ORIGIN = 0x20010000, LENGTH = 64K

  AXI_SRAM1 (xrw) : ORIGIN = 0x24000000, LENGTH = 256K
  AXI_SRAM2 (xrw) : ORIGIN = 0x24040000, LENGTH = 384K
  AXI_SRAM3 (xrw) : ORIGIN = 0x240A0000, LENGTH = 384K

  RAM_CD    (xrw) : ORIGIN = 0x30000000, LENGTH = 128K
  RAM_SRD   (xrw) : ORIGIN = 0x38000000, LENGTH = 32K
}

/* Highest address of the user-mode stack */
_estack = ORIGIN(AXI_SRAM1) + LENGTH(AXI_SRAM1);

/* Output sections */
SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* Initialized data copied from flash into AXI SRAM1 at startup. */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;

    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)

    . = ALIGN(4);
    _edata = .;
  } >AXI_SRAM1 AT>FLASH

  /* Uninitialized normal program data in AXI SRAM1. */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;

    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >AXI_SRAM1

  /*
   * LTDC framebuffer 0.
   *
   * This section is NOLOAD because display initialization explicitly clears the
   * framebuffer. Its start is aligned for cache lines and LTDC AXI bursts.
   */
  .ltdc_framebuffer0 (NOLOAD) :
  {
    . = ALIGN(128);
    __ltdc_framebuffer0_start__ = .;

    KEEP(*(.ltdc_framebuffer0))
    KEEP(*(.ltdc_framebuffer0.*))

    . = ALIGN(128);
    __ltdc_framebuffer0_end__ = .;
  } >AXI_SRAM2

  /*
   * LTDC framebuffer 1.
   *
   * Keeping the second framebuffer in a different physical AXI SRAM bank
   * prevents the CPU and LTDC from competing for the same SRAM slave while
   * rendering and scanning concurrently.
   */
  .ltdc_framebuffer1 (NOLOAD) :
  {
    . = ALIGN(128);
    __ltdc_framebuffer1_start__ = .;

    KEEP(*(.ltdc_framebuffer1))
    KEEP(*(.ltdc_framebuffer1.*))

    . = ALIGN(128);
    __ltdc_framebuffer1_end__ = .;
  } >AXI_SRAM3

  /* Heap and stack reservation in AXI SRAM1. */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);

    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;

    . = ALIGN(8);
  } >AXI_SRAM1

  .ARM.attributes 0 :
  {
    *(.ARM.attributes)
  }

  ASSERT((__ltdc_framebuffer0_end__ - __ltdc_framebuffer0_start__) <= LENGTH(AXI_SRAM2),
         "Framebuffer 0 exceeds AXI SRAM2")

  ASSERT((__ltdc_framebuffer1_end__ - __ltdc_framebuffer1_start__) <= LENGTH(AXI_SRAM3),
         "Framebuffer 1 exceeds AXI SRAM3")

  ASSERT(_end <= (ORIGIN(AXI_SRAM1) + LENGTH(AXI_SRAM1)),
         "Program data, BSS, heap, and stack exceed AXI SRAM1")
}