# rbsh production shell

`rbsh` is the Rabbitbone userland shell. It stays small enough for the freestanding OS environment, but the interactive and scripted surface now covers the command line features needed for daily work on the live system.

## Shell invocation

```text
sh [-il] [--login USER] [--no-banner] [-c COMMAND]
```

- `-c COMMAND` runs a command string and exits with its status.
- `-i` forces interactive mode.
- `-l` marks the session as a login shell.
- `--login USER` switches to a Rabbitbone user and starts in that user home.
- `--no-banner` suppresses the startup banner.
- `-h`, `--help`, `-V`, and `--version` are supported.

## Command grammar

- command sequences with `;`
- conditional execution with `&&` and `||`
- foreground and background jobs with `&`
- pipelines with `|`
- single quotes, double quotes, and backslash escaping
- comments starting with `#` at token boundaries
- variable assignment and expansion: `NAME=value`, `$NAME`, `${NAME}`, `$?`, `$$`
- tilde expansion for `~` and `~/...`
- redirection: `<`, `>`, `>>`, `1>`, `1>>`, `2>`, `2>>`, `2>&1`, `1>&2`, `>&2`, `&>`, `&>>`

## CLI builtins

Core shell and environment:

- `help [COMMAND...]`
- `clear`
- `uname`, `info`
- `path [VALUE]`
- `which [-a] NAME...`
- `type [-a] NAME...`
- `set [NAME=VALUE...]`
- `export [-p] [NAME|NAME=VALUE...]`
- `unset NAME...`
- `env [-0]`
- `exit [STATUS]`
- `true`, `false`, `test`, `[`

Text and filesystem:

- `echo [-n] [-e|-E] [ARG...]`
- `printf FORMAT [ARG...]` with `%s`, `%q`, `%d`, `%u`, and `%%`
- `pwd [-L|-P]`
- `cd [PATH|-]`
- `ls [-aAldi1F] [PATH...]`, `ll [PATH]`
- `stat [-L] PATH...`
- `cat [-nbsE] [FILE...]`
- `write [-a] PATH TEXT...`, `put [-a] PATH TEXT...`
- `touch [-cv] PATH...`
- `mkdir [-pv] PATH...`
- `rm [-frv] PATH...`
- `mv [-fnv] OLD NEW`
- `cp [-fnv] SRC DST`
- `link OLD NEW`, `ln OLD NEW`
- `symlink TARGET LINKPATH`
- `readlink PATH`
- `truncate PATH SIZE`

Process and system diagnostics:

- `run`, `spawn`, `wait`, `jobs`, `ps`, `proc`, `last`
- `sched`, `preempt`, `ticks`, `sleep`, `yield`
- `fdprobe`, `fdinfo`, `tty`, `statvfs`, `mounts`, `sync`, `fsync`
- `syscall`, `elf`, `userbins`, `log`
- `mem`, `heap`, `vmm`, `ktest`, `logs`, `boot`, `disks`, `blk`, `pci`, `acpi`, `apic`, `hpet`, `timer`, `smp`, `signals`, `ext4`
- `bone <mem|heap|logs|disks|ext4|boot|reboot|halt|panic>`

Authentication and privileges:

- `id`
- `whoami`
- `users`
- `login USER`
- `su [USER]`
- `sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]`

## Interactive editor

- framebuffer TTY redraw that handles long lines
- prompt with current user and directory
- history navigation with Up/Down
- completion for builtins, PATH executables, and filesystem paths
- Home/End/Delete/Left/Right
- PageUp/PageDown scrollback
- Ctrl+L clear, Ctrl+D exit, Ctrl+C cancel line

## Compatibility notes

GNU Bash was used as a behavior reference only. No GPL Bash source code is copied or vendored into Rabbitbone's MIT-licensed tree.
