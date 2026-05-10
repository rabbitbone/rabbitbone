ENTRY(_start)
SECTIONS
{
  . = 0x0000010000000000;
  .text : ALIGN(0x1000) { *(.text*) *(.ltext*) *(.rodata*) *(.lrodata*) }
  . = ALIGN(0x1000);
  .data : ALIGN(0x1000) { *(.data*) *(.ldata*) }
  . = ALIGN(0x1000);
  .bss : ALIGN(0x1000) { *(.bss*) *(.lbss*) *(COMMON) }
}
