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
    if (au_kill((unsigned int)child, RABBITBONE_SIGTERM) != 0) return fail(123);
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return fail(124);
    if (info.exit_code != 128 + (int)RABBITBONE_SIGTERM || info.pgrp != (unsigned int)child) return fail(125);
    if (au_tcsetpgrp((unsigned int)pg) != 0) return fail(126);
    if (au_signal(RABBITBONE_SIGKILL, on_sigusr1) != (au_sighandler_t)(unsigned long)RABBITBONE_SIG_ERR) return fail(127);
    if (au_kill(0x7ffffffeu, RABBITBONE_SIGTERM) >= 0) return fail(128);
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
