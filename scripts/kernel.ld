ENTRY(kernel_start)
SECTIONS
{
  . = 0x10000;
  __kernel_start = .;
  __text_start = .;
  .text ALIGN(0x1000) : { *(.text.boot*) *(.text*) *(.ltext*) }
  __text_end = .;
  __rodata_start = .;
  .rodata ALIGN(0x1000) : { *(.rodata*) *(.lrodata*) }
  __rodata_end = .;
  __data_start = .;
  .data ALIGN(0x1000) : { *(.data*) *(.ldata*) }
  __data_end = .;
  .bss ALIGN(0x1000) : {
    __bss_start = .;
    *(.bss*) *(.lbss*) *(COMMON)
    __bss_end = .;
  }
  . = ALIGN(0x1000);
  __kernel_end = .;
}
