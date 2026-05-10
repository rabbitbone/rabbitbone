ENTRY(stage2_start)
SECTIONS
{
  . = 0x8000;
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  ASSERT(. <= 0x10000, "stage2 exceeds the reserved 32 KiB real-mode load window")
}
