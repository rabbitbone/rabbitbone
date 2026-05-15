#include <rabbitbone_sys.h>

static volatile int sig_seen;
static void on_sigusr1(int sig) { if (sig == (int)RABBITBONE_SIGUSR1) ++sig_seen; }
static int fail(int code) { return code; }

static int signal_job_tests(void) {
    if (au_signal(RABBITBONE_SIGUSR1, on_sigusr1) == (au_sighandler_t)(unsigned long)RABBITBONE_SIG_ERR) return fail(110);
    if (au_raise(RABBITBONE_SIGUSR1) != 0 || sig_seen != 1) return fail(111);
    au_u64 mask = 1ull << RABBITBONE_SIGUSR1;
    if (au_sigprocmask(RABBITBONE_SIG_BLOCK, &mask, 0) != 0) return fail(112);
    if (au_raise(RABBITBONE_SIGUSR1) != 0 || sig_seen != 1) return fail(113);
    au_u64 pending = 0;
    if (au_sigpending(&pending) != 0 || (pending & mask) == 0) return fail(114);
    if (au_sigprocmask(RABBITBONE_SIG_UNBLOCK, &mask, 0) != 0 || sig_seen != 2) return fail(115);

    au_i64 self = au_getpid();
    au_i64 pg = au_getpgrp();
    au_i64 sid = au_getsid(0);
    if (self <= 0 || pg <= 0 || sid <= 0) return fail(116);
    if (au_getpgid((unsigned int)self) != pg) return fail(117);
    if (au_tcgetpgrp() <= 0) return fail(118);

    au_i64 child = au_fork();
    if (child < 0) return fail(119);
    if (child == 0) { for (;;) (void)au_sleep(10); }
    if (au_setpgid((unsigned int)child, (unsigned int)child) != 0) return fail(120);
    if (au_getpgid((unsigned int)child) != child) return fail(121);
    if (au_tcsetpgrp((unsigned int)child) != 0 || au_tcgetpgrp() != child) return fail(122);
    if (au_kill(-(int)child, RABBITBONE_SIGTERM) != 0) return fail(123);
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return fail(124);
    if (info.exit_code != 128 + (int)RABBITBONE_SIGTERM || info.pgrp != (unsigned int)child) return fail(125);
    if (au_tcsetpgrp((unsigned int)pg) != 0) return fail(126);
    if (au_signal(RABBITBONE_SIGKILL, on_sigusr1) != (au_sighandler_t)(unsigned long)RABBITBONE_SIG_ERR) return fail(127);
    if (au_kill(0x7ffffffe, RABBITBONE_SIGTERM) >= 0) return fail(128);
    if (au_kill(-0x7ffffffe, RABBITBONE_SIGTERM) >= 0) return fail(129);

    unsigned char *data_handler = (unsigned char *)mmap(0, 4096ul, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE, -1, 0);
    if (data_handler == (void *)-1 || !data_handler) return fail(130);
    au_sigaction_t bad_act;
    au_memset(&bad_act, 0, sizeof(bad_act));
    bad_act.handler = (au_u64)(unsigned long)data_handler;
    bad_act.restorer = (au_u64)(unsigned long)data_handler;
    if (au_sigaction(RABBITBONE_SIGUSR2, &bad_act, 0) >= 0) return fail(131);
    if (munmap(data_handler, 4096ul) != 0) return fail(132);
    if (au_sigaction(RABBITBONE_SIGUSR2, 0, 0) >= 0) return fail(133);
    if (au_sigprocmask(RABBITBONE_SIG_SETMASK, 0, 0) >= 0) return fail(134);
    au_result_t raw_bad = au_syscall3(AU_SYS_KILL, 0x80000000ull, RABBITBONE_SIGTERM, 0);
    if (au_result_code(raw_bad) >= 0) return fail(135);
    au_result_t raw_pg = au_syscall3(AU_SYS_KILL, 0xfffffffeull, RABBITBONE_SIGTERM, 0);
    if (raw_pg.error == RABBITBONE_ERR_INVAL) return fail(136);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));

    au_i64 p1 = au_spawn("/bin/hello");
    if (p1 <= 0) return 90;
    if (au_wait((unsigned int)p1, &info) != 0) return 91;
    if (info.pid != (unsigned int)p1 || info.state != 2u || info.exit_code != 7) return 92;

    const char *args[] = { "/bin/fscheck", "/disk0/hello.txt" };
    au_i64 p2 = au_spawnv("/bin/fscheck", 2, args);
    if (p2 <= 0 || p2 == p1) return 93;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)p2, &info) != 0) return 94;
    if (info.pid != (unsigned int)p2 || info.state != 2u || info.exit_code != 0) return 95;

    if (au_spawnv("/bin/hello", 0, args) >= 0) return 96;
    if (au_spawnv("/bin/hello", 1, (const char *const *)0) >= 0) return 97;
    if (au_wait(0xffffffffu, &info) >= 0) return 98;
    return signal_job_tests();
}
