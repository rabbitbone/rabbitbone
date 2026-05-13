#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/arch/cpu.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/libc.h>
#include <aurora/version.h>
#include <aurora/block.h>
#include <aurora/pci.h>
#include <aurora/vfs.h>
#include <aurora/acpi.h>
#include <aurora/apic.h>
#include <aurora/hpet.h>
#include <aurora/smp.h>
#include <aurora/timer.h>
#include <aurora/process.h>
#include <aurora/bootinfo.h>

#ifdef AURORA_DEBUG_SHELL
#define DEBUG_SHELL_LINE_MAX 96u

static void debug_log_writer(const char *line) { kprintf("%s", line); }

static void debug_execute(char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (strcmp(line, "help") == 0) {
        kprintf("debug shell commands: help logs boot acpi apic hpet timer smp pci lspci disks blk mounts signals jobs panic reboot halt\n");
    } else if (strcmp(line, "logs") == 0) {
        log_dump_ring(debug_log_writer);
    } else if (strcmp(line, "boot") == 0) {
        char out[1024];
        bootinfo_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "acpi") == 0) {
        char out[4096];
        acpi_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "apic") == 0) {
        char out[2048];
        apic_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "hpet") == 0) {
        char out[1024];
        hpet_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "timer") == 0) {
        char out[1024];
        timer_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "smp") == 0) {
        char out[2048];
        smp_format_status(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "pci") == 0 || strcmp(line, "lspci") == 0) {
        char out[4096];
        pci_format_devices(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "disks") == 0 || strcmp(line, "blk") == 0) {
        block_log_devices();
    } else if (strcmp(line, "mounts") == 0) {
        vfs_dump_mounts();
    } else if (strcmp(line, "signals") == 0) {
        char out[1024];
        process_format_signals(out, sizeof(out));
        kprintf("%s", out);
    } else if (strcmp(line, "jobs") == 0) {
        char out[2048];
        process_format_jobs(out, sizeof(out));
        kprintf("%s", out);
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
    kprintf("\n%s emergency debug shell. Type 'help'.\n# ", AURORA_CLI_BANNER);
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
