ENTRY(stage1_start)
SECTIONS
{
  . = 0x7c00;
  .text : { *(.text*) *(.rodata*) }
  ASSERT(. <= 0x7c00 + 446, "stage1 exceeds MBR boot-code area and would overlap the partition table")
  . = 0x7c00 + 510;
  .sig : { SHORT(0xaa55) }
}
