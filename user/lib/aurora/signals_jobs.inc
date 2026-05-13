__asm__(".intel_syntax noprefix\n"
        ".global __aurora_sigreturn_trampoline\n"
        "__aurora_sigreturn_trampoline:\n"
        "    lea rdi, [rsp - 8]\n"
        "    mov rax, 86\n"
        "    int 0x80\n"
        "1:  hlt\n"
        "    jmp 1b\n"
        ".att_syntax prefix\n");

extern void __aurora_sigreturn_trampoline(void);

au_i64 au_sigaction(unsigned int sig, const au_sigaction_t *act, au_sigaction_t *oldact) {
    au_result_t r = au_syscall3(AU_SYS_SIGACTION, (au_u64)sig, (au_u64)(unsigned long)act, (au_u64)(unsigned long)oldact);
    return au_result_code(r);
}

au_sighandler_t au_signal(unsigned int sig, au_sighandler_t handler) {
    au_sigaction_t act;
    au_sigaction_t old;
    au_memset(&act, 0, sizeof(act));
    au_memset(&old, 0, sizeof(old));
    act.handler = (au_u64)(unsigned long)handler;
    act.restorer = (au_u64)(unsigned long)__aurora_sigreturn_trampoline;
    act.mask = 0;
    act.flags = 0;
    if (au_sigaction(sig, &act, &old) != 0) return (au_sighandler_t)(unsigned long)AURORA_SIG_ERR;
    return (au_sighandler_t)(unsigned long)old.handler;
}

au_i64 au_sigprocmask(unsigned int how, const au_u64 *set, au_u64 *oldset) {
    au_result_t r = au_syscall3(AU_SYS_SIGPROCMASK, (au_u64)how, (au_u64)(unsigned long)set, (au_u64)(unsigned long)oldset);
    return au_result_code(r);
}

au_i64 au_sigpending(au_u64 *out) {
    au_result_t r = au_syscall3(AU_SYS_SIGPENDING, (au_u64)(unsigned long)out, 0, 0);
    return au_result_code(r);
}

au_i64 au_kill(unsigned int pid, unsigned int sig) {
    au_result_t r = au_syscall3(AU_SYS_KILL, (au_u64)pid, (au_u64)sig, 0);
    return au_result_code(r);
}

au_i64 au_raise(unsigned int sig) {
    au_result_t r = au_syscall3(AU_SYS_RAISE, (au_u64)sig, 0, 0);
    return au_result_code(r);
}

au_i64 au_getpgrp(void) {
    au_result_t r = au_syscall3(AU_SYS_GETPGRP, 0, 0, 0);
    return au_result_code(r);
}

au_i64 au_setpgid(unsigned int pid, unsigned int pgid) {
    au_result_t r = au_syscall3(AU_SYS_SETPGID, (au_u64)pid, (au_u64)pgid, 0);
    return au_result_code(r);
}

au_i64 au_getpgid(unsigned int pid) {
    au_result_t r = au_syscall3(AU_SYS_GETPGID, (au_u64)pid, 0, 0);
    return au_result_code(r);
}

au_i64 au_setsid(void) {
    au_result_t r = au_syscall3(AU_SYS_SETSID, 0, 0, 0);
    return au_result_code(r);
}

au_i64 au_getsid(unsigned int pid) {
    au_result_t r = au_syscall3(AU_SYS_GETSID, (au_u64)pid, 0, 0);
    return au_result_code(r);
}

au_i64 au_tcgetpgrp(void) {
    au_result_t r = au_syscall3(AU_SYS_TCGETPGRP, 0, 0, 0);
    return au_result_code(r);
}

au_i64 au_tcsetpgrp(unsigned int pgid) {
    au_result_t r = au_syscall3(AU_SYS_TCSETPGRP, (au_u64)pgid, 0, 0);
    return au_result_code(r);
}
