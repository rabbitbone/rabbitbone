static int sh_path_is_elf(const char *path) {
    unsigned char head[4];
    au_i64 fd = au_open(path);
    if (fd < 0) return 0;
    au_i64 got = au_read(fd, head, sizeof(head));
    (void)au_close(fd);
    if (got >= 4 && head[0] == 0x7fu && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') return 1;
    return got >= 2 && head[0] == '#' && head[1] == '!';
}

static int sh_kctl_op_for(const char *cmd, unsigned int *op) {
    if (!cmd || !op) return 0;
    if (sh_streq(cmd, "mem")) *op = AURORA_KCTL_OP_MEM;
    else if (sh_streq(cmd, "heap")) *op = AURORA_KCTL_OP_HEAP;
    else if (sh_streq(cmd, "vmm")) *op = AURORA_KCTL_OP_VMM;
    else if (sh_streq(cmd, "ktest")) *op = AURORA_KCTL_OP_KTEST;
    else if (sh_streq(cmd, "logs")) *op = AURORA_KCTL_OP_LOGS;
    else if (sh_streq(cmd, "disks")) *op = AURORA_KCTL_OP_DISKS;
    else if (sh_streq(cmd, "acpi")) *op = AURORA_KCTL_OP_ACPI;
    else if (sh_streq(cmd, "apic")) *op = AURORA_KCTL_OP_APIC;
    else if (sh_streq(cmd, "hpet")) *op = AURORA_KCTL_OP_HPET;
    else if (sh_streq(cmd, "timer")) *op = AURORA_KCTL_OP_TIMER;
    else if (sh_streq(cmd, "smp")) *op = AURORA_KCTL_OP_SMP;
    else if (sh_streq(cmd, "blk")) *op = AURORA_KCTL_OP_BLK;
    else if (sh_streq(cmd, "pci") || sh_streq(cmd, "lspci")) *op = AURORA_KCTL_OP_PCI;
    else if (sh_streq(cmd, "ext4")) *op = AURORA_KCTL_OP_EXT4;
    else if (sh_streq(cmd, "panic")) *op = AURORA_KCTL_OP_PANIC;
    else if (sh_streq(cmd, "reboot")) *op = AURORA_KCTL_OP_REBOOT;
    else if (sh_streq(cmd, "halt")) *op = AURORA_KCTL_OP_HALT;
    else return 0;
    return 1;
}

static int sh_builtin_kctl(int argc, char **argv) {
    if (argc <= 0 || !argv || !argv[0]) return 1;
    unsigned int op = 0;
    if (!sh_kctl_op_for(argv[0], &op)) return 127;
    const char *arg = 0;
    if (op == AURORA_KCTL_OP_PANIC) arg = argc > 1 ? argv[1] : "shell panic command";
    if ((op == AURORA_KCTL_OP_REBOOT || op == AURORA_KCTL_OP_HALT) && argc > 1) {
        sh_err(argv[0]);
        sh_err(": no arguments expected\n");
        return 2;
    }
    char out[AURORA_KCTL_OUT_MAX];
    au_memset(out, 0, sizeof(out));
    au_i64 r = au_kctl(op, out, sizeof(out), arg);
    if (r < 0) {
        sh_err(argv[0]);
        if (r == -8) sh_err(": permission denied; use sudo\n");
        else if (r == -11) sh_err(": unsupported by kernel\n");
        else sh_err(": syscall failed\n");
        return r == -8 ? 126 : 1;
    }
    if (out[0]) sh_puts(out);
    return 0;
}
