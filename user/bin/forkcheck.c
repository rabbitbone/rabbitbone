#include <aurora_sys.h>

static volatile int cow_value = 11;
static volatile int cow_shadow = 5;

static int check_waited_child(unsigned int pid, int expected_exit) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait(pid, &info) != 0) return 1;
    if (info.pid != pid) return 2;
    if (info.state != 2u || info.faulted || info.exit_code != expected_exit) return 3;
    if (info.address_space_generation == 0) return 4;
    au_procinfo_t self;
    au_memset(&self, 0, sizeof(self));
    if (au_procinfo((unsigned int)au_getpid(), &self) != 0) return 5;
    if (self.address_space_generation == 0 || self.address_space_generation == info.address_space_generation) return 6;
    return 0;
}

static int run_cow_isolation_check(void) {
    cow_value = 11;
    cow_shadow = 5;
    au_i64 child = au_fork();
    if (child < 0) return 80;
    if (child == 0) {
        for (unsigned i = 0; i < 4; ++i) {
            if (au_yield() != 0) return 81;
        }
        if (cow_value != 11 || cow_shadow != 5) return 82;
        cow_value = 31;
        cow_shadow = 41;
        if (cow_value != 31 || cow_shadow != 41) return 83;
        return 17;
    }

    cow_value = 23;
    cow_shadow = 29;
    for (unsigned i = 0; i < 4; ++i) {
        if (au_yield() != 0) return 84;
    }
    int wr = check_waited_child((unsigned int)child, 17);
    if (wr != 0) return 90 + wr;
    if (cow_value != 23 || cow_shadow != 29) return 96;

    cow_value = 43;
    cow_shadow = 47;
    if (cow_value != 43 || cow_shadow != 47) return 97;
    return 0;
}

static int run_fork_exec_decref_check(void) {
    cow_value = 101;
    au_i64 child = au_fork();
    if (child < 0) return 100;
    if (child == 0) {
        const char *args[] = { "/bin/hello" };
        au_i64 r = au_execv("/bin/hello", 1, args);
        return r < 0 ? 101 : 102;
    }
    cow_value = 103;
    int wr = check_waited_child((unsigned int)child, 7);
    if (wr != 0) return 110 + wr;
    if (cow_value != 103) return 116;
    cow_value = 107;
    if (cow_value != 107) return 117;
    return 0;
}

static int run_text_write_fault_check(void) {
    au_i64 child = au_fork();
    if (child < 0) return 120;
    if (child == 0) {
        volatile unsigned char *text = (volatile unsigned char *)(void *)&run_text_write_fault_check;
        *text = (unsigned char)*text;
        return 121;
    }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return 122;
    if (info.pid != (unsigned int)child) return 123;
    if (info.state != 3u || !info.faulted || info.fault_vector != 14u) return 124;
    cow_value = 131;
    if (cow_value != 131) return 125;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int r = run_cow_isolation_check();
    if (r != 0) return r;
    r = run_fork_exec_decref_check();
    if (r != 0) return r;
    r = run_text_write_fault_check();
    if (r != 0) return r;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)au_getpid(), &info) >= 0) return 126;
    return 0;
}
