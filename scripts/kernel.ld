ENTRY(kernel_start)
SECTIONS
{
  . = 0x10000;
  . = ALIGN(0x1000);
  __kernel_start = .;

  __text_start = .;
  .text : { *(.text.boot*) *(.text*) *(.ltext*) }
  . = ALIGN(0x1000);
  __text_end = .;

  __rodata_start = .;
  .rodata : { *(.rodata*) *(.lrodata*) }
  . = ALIGN(0x1000);
  __rodata_end = .;

  __data_start = .;
  .data : { *(.data*) *(.ldata*) }
  . = ALIGN(0x1000);
  __data_end = .;

  __bss_start = .;
  .bss : {
    *(.bss*) *(.lbss*) *(COMMON)
  }
  . = ALIGN(0x1000);
  __bss_end = .;

  __kernel_end = .;
}
