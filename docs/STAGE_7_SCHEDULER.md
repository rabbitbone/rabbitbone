# AuroraOS Stage 7

Stage 7 adds a process scheduler layer above the existing single-active-process executor.

Implemented:

- heap-backed scheduler run queue
- scheduler job ids and job history
- queued process dispatch
- scheduler wait over queued/completed jobs
- scheduler accounting for yield, sleep, enqueue and dispatch
- syscalls: yield, sleep, schedinfo
- userlib wrappers for scheduler syscalls
- `/bin/schedcheck`
- shell commands: `qspawn`, `runq`, `sched`
- ktest suite: `scheduler/runqueue/contracts`

Limitations:

- no preemptive user context switch yet
- queued jobs dispatch one full process at a time
- spawn/wait remain synchronous compatibility syscalls

Next target:

- saved user contexts and timer-driven preemptive switching.
