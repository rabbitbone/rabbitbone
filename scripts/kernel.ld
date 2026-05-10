ENTRY(kernel_start)
SECTIONS
{
  . = 0x10000;
  __kernel_start = .;
  .text ALIGN(0x1000) : { *(.text.boot*) *(.text*) *(.ltext*) }
  .rodata ALIGN(0x1000) : { *(.rodata*) *(.lrodata*) }
  .data ALIGN(0x1000) : { *(.data*) *(.ldata*) }
  .bss ALIGN(0x1000) : {
    __bss_start = .;
    *(.bss*) *(.lbss*) *(COMMON)
    __bss_end = .;
  }
  . = ALIGN(0x1000);
  __kernel_end = .;
}
