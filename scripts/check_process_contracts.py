from pathlib import Path
checks = {
    Path('include/aurora/abi.h'): ['aurora_procinfo_t', 'aurora_schedinfo_t', 'AURORA_ABI_STATIC_ASSERT', 'AURORA_SYS_GETPID 15u', 'AURORA_SYS_PROCINFO 16u', 'AURORA_SYS_SPAWN 17u', 'AURORA_SYS_WAIT 18u', 'AURORA_SYS_YIELD 19u', 'AURORA_SYS_SLEEP 20u', 'AURORA_SYS_SCHEDINFO 21u', 'AURORA_SYS_DUP 22u', 'AURORA_SYS_TELL 23u', 'AURORA_SYS_FSTAT 24u', 'AURORA_SYS_FDINFO 25u', 'AURORA_SYS_READDIR 26u', 'AURORA_SYS_SPAWNV 27u', 'AURORA_SYS_PREEMPTINFO 28u', 'AURORA_SYS_FORK 29u', 'AURORA_SYS_EXEC 30u', 'AURORA_SYS_EXECV 31u', 'AURORA_SYS_MAX 32u'],
    Path('kernel/include/aurora/syscall.h'): ['AURORA_SYSCALL_GETPID = AURORA_SYS_GETPID', 'AURORA_SYSCALL_SPAWN = AURORA_SYS_SPAWN', 'AURORA_SYSCALL_WAIT = AURORA_SYS_WAIT', 'AURORA_SYSCALL_DUP = AURORA_SYS_DUP', 'AURORA_SYSCALL_READDIR = AURORA_SYS_READDIR', 'AURORA_SYSCALL_SPAWNV = AURORA_SYS_SPAWNV', 'AURORA_SYSCALL_PREEMPTINFO = AURORA_SYS_PREEMPTINFO', 'AURORA_SYSCALL_FORK = AURORA_SYS_FORK', 'AURORA_SYSCALL_EXEC = AURORA_SYS_EXEC', 'AURORA_SYSCALL_EXECV = AURORA_SYS_EXECV'],
    Path('userlib/include/aurora_sys.h'): ['typedef aurora_procinfo_t au_procinfo_t', 'typedef aurora_schedinfo_t au_schedinfo_t', 'AU_SYS_GETPID = AURORA_SYS_GETPID', 'AU_SYS_SPAWN = AURORA_SYS_SPAWN', 'AU_SYS_WAIT = AURORA_SYS_WAIT', 'AU_SYS_YIELD = AURORA_SYS_YIELD', 'AU_SYS_DUP = AURORA_SYS_DUP', 'AU_SYS_SPAWNV = AURORA_SYS_SPAWNV', 'AU_SYS_PREEMPTINFO = AURORA_SYS_PREEMPTINFO', 'AU_SYS_FORK = AURORA_SYS_FORK', 'AU_SYS_EXEC = AURORA_SYS_EXEC', 'AU_SYS_EXECV = AURORA_SYS_EXECV', 'au_procinfo_t', 'au_schedinfo_t', 'au_fdinfo_t', 'au_dirent_t', 'au_preemptinfo_t'],
    Path('kernel/core/ktest.c'): ['process/registry/contracts', 'scheduler/runqueue/contracts', '/bin/procstat', '/bin/spawncheck', '/bin/schedcheck', '/bin/preemptcheck', '/bin/fdcheck', 'process_wait', 'AURORA_SYSCALL_ABI_VERSION', 'spawnv', 'preemptinfo', 'fork', 'execv', '/bin/execcheck'],
    Path('kernel/proc/process.c'): ['wait_out_ptr', 'copy_to_process_user_space', 'resolve_current_async_slot', 'process_exit_current_from_syscall', 'leave_user_scheduler', 'extern void arch_user_resume(const cpu_regs_t *regs);', 'continue;', 'prepare_exec_replacement', 'commit_exec_replacement', 'process_request_exec'],
    Path('kernel/arch/x86_64/idt.c'): ['AURORA_SYS_EXIT', 'process_exit_current_from_syscall'],
    Path('kernel/mm/kmem.c'): ['blocks_physically_adjacent', 'block_end', 'merge_forward'],
}
for path, needles in checks.items():
    text = path.read_text()
    missing = [n for n in needles if n not in text]
    if missing:
        raise SystemExit(f'{path}: missing contract markers: {missing}')
print('process syscall/registry/fd contract checks passed')
