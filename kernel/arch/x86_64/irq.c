#include <aurora/arch/io.h>

void irq_enable(void) { cpu_sti(); }
void irq_disable(void) { cpu_cli(); }
