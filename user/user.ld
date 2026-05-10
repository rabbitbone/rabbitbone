ENTRY(_start)
SECTIONS
{
  . = 0x0000010000000000;
  .text : ALIGN(0x1000) { *(.text*) *(.rodata*) }
  .data : ALIGN(0x1000) { *(.data*) }
  .bss : ALIGN(0x1000) { *(.bss*) *(COMMON) }
}
