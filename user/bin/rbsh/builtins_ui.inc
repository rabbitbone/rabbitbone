static void sh_help(void) {
    sh_puts("rbsh commands:\n");
    sh_puts("  help clear uname info path which type theme exit echo pid pwd cd\n");
    sh_puts("  dir ll ls stat cat read put touch mk mkdir del rm ren mv link ln\n");
    sh_puts("  run spawn wait jobs ps proc last procctl sync fsync\n");
    sh_puts("  mem heap vmm ktest logs boot disks blk pci acpi apic hpet timer smp ext4\n");
    sh_puts("  bone <mem|heap|logs|disks|ext4|boot|reboot|halt|panic>\n");
    sh_puts("keys: Tab complete, Up/Down history, Ctrl+L clear, Ctrl+D exit.\n");
}


static int sh_builtin_clear(void) {
    if (au_tty_clear() != 0) { sh_err("clear: tty clear failed\n"); return 1; }
    return 0;
}

static int sh_builtin_path(int argc, char **argv) {
    if (argc == 1) {
        sh_puts("PATH="); sh_puts(sh_path_value()); sh_puts("\n");
        return 0;
    }
    if (argc == 2) {
        if (!sh_set_path_value(argv[1])) { sh_err("path: invalid value\n"); return 1; }
        sh_puts("PATH="); sh_puts(sh_path_value()); sh_puts("\n");
        return 0;
    }
    sh_err("usage: path [VALUE]\n");
    return 1;
}

