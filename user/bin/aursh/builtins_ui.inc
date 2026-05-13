static void sh_help(void) {
    sh_puts("Aurora userland shell (/bin/sh). prompt: <cwd>$\n");
    sh_puts("PATH="); sh_puts(sh_path_value()); sh_puts(". Use 'path [VALUE]', 'which NAME', 'type NAME', 'userbins'.\n");
    sh_puts("builtins:\n");
    sh_puts("  help clear uname path which type theme exit crash echo pid pwd cd id whoami users login su sudo\n");
    sh_puts("  ls [-l] [PATH] stat chmod chown cat write touch mkdir rm mv truncate link ln symlink readlink\n");
    sh_puts("  run spawn qspawn wait runq procs ps proc lastproc sched schedtest preempt ticks sleep yield count\n");
    sh_puts("  fdprobe fdinfo tty statvfs mounts sync fsync syscall elf userbins log\n");
    sh_puts("  mem heap vmm ktest logs disks ext4 panic reboot halt\n");
    sh_puts("syntax: cmd [args] | cmd, redirects: < file, > file, 2> file, quotes: '...' or \"...\"\n");
    sh_puts("editing: Left/Right/Home/End move cursor, Backspace/Delete edit, Tab completes, Up/Down history, PageUp/PageDown scroll output.\n");
    sh_puts("keys: Ctrl+C cancels input, Ctrl+D exits on an empty line or deletes under cursor, Ctrl+L clears and redraws. Long input wraps across rows.\nexternal commands are discovered from PATH directories; bare commands are never forced into /bin.\n");
    sh_puts("themes: theme, theme show, theme legacy, theme black\nusers: login USER, su [USER], sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]\n");
    sh_puts("kernel ABI: info commands are direct syscalls; panic/reboot/halt require sudo/root.\n");
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

