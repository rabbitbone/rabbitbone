#include <rabbitbone/console.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/log.h>
#include <rabbitbone/panic.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/version.h>
#include <rabbitbone/block.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/hpet.h>
#include <rabbitbone/smp.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/process.h>
#include <rabbitbone/bootinfo.h>
#include <rabbitbone/kmem.h>

#ifdef RABBITBONE_DEBUG_SHELL
#define DEBUG_SHELL_LINE_MAX 96u

static void debug_log_writer(const char *line) { kprintf("%s", line); }

typedef void (*debug_format_fn_t)(char *out, usize out_size);

static void debug_print_formatted(debug_format_fn_t format, usize out_size) {
    if (!format || out_size == 0) return;
    char *out = (char *)kmalloc(out_size);
    if (!out) {
        kprintf("debug shell: output buffer allocation failed\n");
        return;
    }
    memset(out, 0, out_size);
    format(out, out_size);
    kprintf("%s", out);
    kfree(out);
}

static void debug_execute(char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (strcmp(line, "help") == 0) {
        kprintf("debug shell commands: help logs boot acpi apic hpet timer smp pci lspci disks blk mounts signals jobs panic reboot halt\n");
    } else if (strcmp(line, "logs") == 0) {
        log_dump_ring(debug_log_writer);
    } else if (strcmp(line, "boot") == 0) {
        debug_print_formatted(bootinfo_format_status, 1024u);
    } else if (strcmp(line, "acpi") == 0) {
        debug_print_formatted(acpi_format_status, 4096u);
    } else if (strcmp(line, "apic") == 0) {
        debug_print_formatted(apic_format_status, 2048u);
    } else if (strcmp(line, "hpet") == 0) {
        debug_print_formatted(hpet_format_status, 1024u);
    } else if (strcmp(line, "timer") == 0) {
        debug_print_formatted(timer_format_status, 1024u);
    } else if (strcmp(line, "smp") == 0) {
        debug_print_formatted(smp_format_status, 8192u);
    } else if (strcmp(line, "pci") == 0 || strcmp(line, "lspci") == 0) {
        debug_print_formatted(pci_format_devices, 4096u);
    } else if (strcmp(line, "disks") == 0 || strcmp(line, "blk") == 0) {
        block_log_devices();
    } else if (strcmp(line, "mounts") == 0) {
        vfs_dump_mounts();
    } else if (strcmp(line, "signals") == 0) {
        debug_print_formatted(process_format_signals, 1024u);
    } else if (strcmp(line, "jobs") == 0) {
        debug_print_formatted(process_format_jobs, 2048u);
    } else if (strcmp(line, "panic") == 0) {
        PANIC("manual panic requested from debug shell");
    } else if (strcmp(line, "reboot") == 0) {
        cpu_reboot();
    } else if (strcmp(line, "halt") == 0) {
        cpu_halt_forever();
    } else if (*line) {
        kprintf("debug shell: unknown command: %s\n", line);
    }
}

void shell_run(void) {
    char line[DEBUG_SHELL_LINE_MAX];
    usize len = 0;
    bool discarding = false;
    kprintf("\n%s emergency debug shell. Type 'help'.\n# ", RABBITBONE_CLI_BANNER);
    for (;;) {
        char c;
        if (!keyboard_getc(&c)) {
            cpu_sti();
            cpu_hlt();
            continue;
        }
        if (c == '\n') {
            console_putc('\n');
            if (discarding) {
                kprintf("debug shell: line too long\n");
                discarding = false;
            } else {
                line[len] = 0;
                debug_execute(line);
            }
            len = 0;
            kprintf("# ");
        } else if (c == '\b') {
            if (!discarding && len > 0) { --len; console_putc('\b'); }
        } else if (c >= 32 && c <= 126) {
            if (discarding) continue;
            if (len + 1u < DEBUG_SHELL_LINE_MAX) {
                line[len++] = c;
                console_putc(c);
            } else {
                discarding = true;
            }
        }
    }
}
#endif
