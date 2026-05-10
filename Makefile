ROOT := $(CURDIR)
BUILD := $(ROOT)/build
CLANG := clang
LD := ld.lld
OBJCOPY := llvm-objcopy
HOSTCXX := c++
HOSTCC := cc
RUSTC ?= rustc
RUST_TARGET ?= x86_64-unknown-linux-gnu
RUST_SYSROOT ?=
KERNEL_SECTORS := 1016
KERNEL_BYTES := 520192
ifneq ($(strip $(RUST_SYSROOT)),)
RUST_SYSROOT_FLAG := --sysroot $(RUST_SYSROOT)
endif
USER_CFLAGS := --target=x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=large -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Werror -Iinclude -Iuserlib/include -MMD -MP
USER_ASMFLAGS := --target=x86_64-unknown-none -ffreestanding -Wall -Wextra -Werror -MMD -MP
USER_LDFLAGS := -nostdlib -z max-page-size=0x1000 -T user/user.ld

USER_C_PROGS := hello fscheck writetest badptr badpath statcheck procstat spawncheck schedcheck preemptcheck fdcheck isolate fdleak forkcheck procctl execcheck
USER_ASM_PROGS := regtrash
USER_PROGS := $(USER_C_PROGS) $(USER_ASM_PROGS)
USER_ELFS := $(USER_PROGS:%=$(BUILD)/user/%.elf)

K_CFLAGS := --target=x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=large -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Werror -Iinclude -Ikernel/include -MMD -MP
K_CXXFLAGS := --target=x86_64-unknown-none -std=c++20 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=large -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Werror -Iinclude -Ikernel/include -MMD -MP
ASMFLAGS := --target=x86_64-unknown-none -ffreestanding -Wall -Wextra -Werror -MMD -MP
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T scripts/kernel.ld -Map=$(BUILD)/kernel.map
RUSTFLAGS := $(RUST_SYSROOT_FLAG) --target $(RUST_TARGET) --edition=2021 -C panic=abort -C relocation-model=static -C code-model=large -C no-redzone=yes -C opt-level=2 --crate-type lib --emit obj

K_C_SRCS := \
  kernel/core/bootinfo.c \
  kernel/core/console.c \
  kernel/core/kmain.c \
  kernel/core/ktest.c \
  kernel/core/log.c \
  kernel/core/panic.c \
  kernel/core/printf.c \
  kernel/core/shell.c \
  kernel/core/timer.c \
  kernel/lib/bitmap.c \
  kernel/lib/string.c \
  kernel/lib/ringbuf.c \
  kernel/lib/crc32.c \
  kernel/mm/kmem.c \
  kernel/mm/vmm.c \
  kernel/proc/process.c \
  kernel/sched/task.c \
  kernel/sched/scheduler.c \
  kernel/vfs/path.c \
  kernel/vfs/vfs.c \
  kernel/vfs/ramfs.c \
  kernel/vfs/devfs.c \
  kernel/vfs/tarfs.c \
  kernel/vfs/ext4_vfs.c \
  kernel/sys/syscall.c \
  kernel/exec/elf64.c \
  kernel/arch/x86_64/cpu.c \
  kernel/arch/x86_64/gdt.c \
  kernel/arch/x86_64/idt.c \
  kernel/arch/x86_64/irq.c \
  kernel/arch/x86_64/memory.c \
  kernel/drivers/ata_pio.c \
  kernel/drivers/block.c \
  kernel/drivers/keyboard.c \
  kernel/drivers/mbr.c \
  kernel/drivers/pic.c \
  kernel/drivers/pit.c \
  kernel/drivers/serial.c \
  kernel/drivers/vga.c \
  kernel/fs/ext4.c

K_CXX_SRCS := kernel/api/system.cpp
K_ASM_SRCS := kernel/arch/x86_64/entry.S kernel/arch/x86_64/isr.S kernel/arch/x86_64/user_entry.S
K_RUST_SRCS := kernel/rust/lib.rs kernel/rust/syscall_dispatch.rs kernel/rust/vfs_route.rs kernel/rust/usercopy.rs kernel/rust/path_policy.rs
K_RUST_OBJ := $(BUILD)/kernel/rust/lib.o

K_OBJS := $(K_C_SRCS:%.c=$(BUILD)/%.o) $(K_CXX_SRCS:%.cpp=$(BUILD)/%.o) $(K_ASM_SRCS:%.S=$(BUILD)/%.o) $(K_RUST_OBJ) $(BUILD)/user_bins.o
DEPS := $(patsubst %.o,%.d,$(filter %.o,$(K_OBJS)))

.PHONY: all clean test image bootcheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck rusttoolcheck
all: image

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot_config.inc: scripts/gen_boot_config.py $(BUILD)
	python3 scripts/gen_boot_config.py --stage2-sectors 64 --kernel-lba 65 --kernel-sectors $(KERNEL_SECTORS) > $@

$(BUILD)/boot/stage1.o: boot/stage1.S $(BUILD)/boot_config.inc
	mkdir -p $(dir $@)
	$(CLANG) --target=i386-unknown-none -c $< -o $@ -I$(BUILD)

$(BUILD)/stage1.elf: $(BUILD)/boot/stage1.o scripts/stage1.ld
	$(LD) -nostdlib -T scripts/stage1.ld -o $@ $<

$(BUILD)/stage1.bin: $(BUILD)/stage1.elf
	$(OBJCOPY) -O binary $< $@
	python3 scripts/check_boot_sector.py $@

$(BUILD)/boot/stage2.o: boot/stage2.S $(BUILD)/boot_config.inc
	mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-unknown-none -c $< -o $@ -I$(BUILD)

$(BUILD)/stage2.elf: $(BUILD)/boot/stage2.o scripts/stage2.ld
	$(LD) -nostdlib -T scripts/stage2.ld -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.elf
	$(OBJCOPY) -O binary $< $@
	python3 scripts/pad_file.py $@ 32768


$(BUILD)/user/crt/start.o: user/crt/start.S
	mkdir -p $(dir $@)
	$(CLANG) $(USER_ASMFLAGS) -c $< -o $@

$(BUILD)/user/lib/aurora.o: user/lib/aurora.c userlib/include/aurora_sys.h
	mkdir -p $(dir $@)
	$(CLANG) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/%.o: user/bin/%.c userlib/include/aurora_sys.h
	mkdir -p $(dir $@)
	$(CLANG) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/%.asm.o: user/bin/%.S
	mkdir -p $(dir $@)
	$(CLANG) $(USER_ASMFLAGS) -c $< -o $@

$(BUILD)/user/%.elf: $(BUILD)/user/crt/start.o $(BUILD)/user/lib/aurora.o $(BUILD)/user/%.o user/user.ld
	mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/crt/start.o $(BUILD)/user/lib/aurora.o $(BUILD)/user/$*.o

$(BUILD)/user/regtrash.elf: $(BUILD)/user/regtrash.asm.o user/user.ld
	mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/regtrash.asm.o

$(BUILD)/user_bins.c: scripts/bin2c.py $(USER_ELFS)
	mkdir -p $(dir $@)
	python3 scripts/bin2c.py --out $@ $(BUILD)/user/hello.elf:/bin/hello $(BUILD)/user/fscheck.elf:/bin/fscheck $(BUILD)/user/writetest.elf:/bin/writetest $(BUILD)/user/regtrash.elf:/bin/regtrash $(BUILD)/user/badptr.elf:/bin/badptr $(BUILD)/user/badpath.elf:/bin/badpath $(BUILD)/user/statcheck.elf:/bin/statcheck $(BUILD)/user/procstat.elf:/bin/procstat $(BUILD)/user/spawncheck.elf:/bin/spawncheck $(BUILD)/user/schedcheck.elf:/bin/schedcheck $(BUILD)/user/preemptcheck.elf:/bin/preemptcheck $(BUILD)/user/fdcheck.elf:/bin/fdcheck $(BUILD)/user/isolate.elf:/bin/isolate $(BUILD)/user/fdleak.elf:/bin/fdleak $(BUILD)/user/forkcheck.elf:/bin/forkcheck $(BUILD)/user/procctl.elf:/bin/procctl $(BUILD)/user/execcheck.elf:/bin/execcheck

$(BUILD)/user_bins.o: $(BUILD)/user_bins.c
	mkdir -p $(dir $@)
	$(CLANG) $(K_CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CLANG) $(K_CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CLANG) $(K_CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	mkdir -p $(dir $@)
	$(CLANG) $(ASMFLAGS) -c $< -o $@

rusttoolcheck:
	@if ! command -v $(RUSTC) >/dev/null 2>&1 && [ ! -x "$(RUSTC)" ]; then \
		echo "error: rustc is required for AuroraOS kernel Rust boundary modules" >&2; \
		echo "hint: install Rust 1.94.1 or run scripts/build_with_uploaded_rust.sh /path/to/rust_toolchain <target>" >&2; \
		exit 127; \
	fi

$(K_RUST_OBJ): $(K_RUST_SRCS) | rusttoolcheck
	mkdir -p $(dir $@)
	$(RUSTC) $(RUSTFLAGS) kernel/rust/lib.rs -o $@

$(BUILD)/kernel.elf: $(K_OBJS) scripts/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(K_OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	python3 scripts/pad_file.py $@ $(KERNEL_BYTES)

$(BUILD)/tools/installer/aurora-install: tools/installer/main.cpp
	mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++17 -O2 -Wall -Wextra -Werror $< -o $@

image: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/tools/installer/aurora-install
	$(BUILD)/tools/installer/aurora-install --out $(BUILD)/aurora.img --stage1 $(BUILD)/stage1.bin --stage2 $(BUILD)/stage2.bin --kernel $(BUILD)/kernel.bin --size-mib 64

bootcheck:
	python3 scripts/check_boot_sources.py boot/stage1.S boot/stage2.S

layoutcheck: $(BUILD)/stage2.elf
	python3 scripts/check_stage2_layout.py $(BUILD)/stage2.elf

kernellayoutcheck: $(BUILD)/kernel.elf
	python3 scripts/check_kernel_layout.py $(BUILD)/kernel.elf $(BUILD)/kernel.map

rustsymbolscheck: $(BUILD)/kernel.elf
	python3 scripts/check_rust_symbols.py $(BUILD)/kernel.elf

rustpaniccheck: $(K_RUST_OBJ)
	python3 scripts/check_rust_no_panic_bounds.py $(K_RUST_OBJ)

usercheck: $(USER_ELFS)
	python3 scripts/check_userland.py $(USER_ELFS)

test: bootcheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck $(BUILD)/host-tests
	$(BUILD)/host-tests

HOST_C_SRCS := kernel/lib/bitmap.c kernel/lib/string.c kernel/lib/ringbuf.c kernel/lib/crc32.c kernel/core/printf.c kernel/drivers/block.c kernel/drivers/mbr.c kernel/fs/ext4.c kernel/mm/kmem.c kernel/vfs/path.c kernel/vfs/tarfs.c
HOST_C_OBJS := $(HOST_C_SRCS:%.c=$(BUILD)/host/%.o)

$(BUILD)/host/%.o: %.c
	mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -fno-builtin -DAURORA_HOST_TEST=1 -Iinclude -Ikernel/include -Wall -Wextra -Werror -c $< -o $@

$(BUILD)/host-tests: tests/test_main.cpp $(HOST_C_OBJS)
	mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++17 -DAURORA_HOST_TEST=1 -Iinclude -Ikernel/include -Wall -Wextra -Werror tests/test_main.cpp $(HOST_C_OBJS) -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)
