CC   = clang
CXX  = clang++
LD   = ld.lld
NASM = nasm
RUSTC ?= rustc
RUSTFLAGS = --emit=obj \
	-C panic=abort \
	-C opt-level=2 \
	-C relocation-model=static \
	-C code-model=kernel \
	-C target-feature=-sse,-sse2 \
	--target x86_64-unknown-none

quiet_RS = $(Q)printf "  [RS]    %s\n" "$<";

BUILD_DIR = kbuild

V ?= $(VERBOSE)
ifeq ($(V),1)
	Q =
else
	Q = @
endif

# Colours
ifeq ($(shell [ -t 1 ] && echo 1),1)
	C_C = \033[1;32m
	C_CXX = \033[1;36m
	C_ASM = \033[1;33m
	C_LD = \033[1;35m
	C_RESET = \033[0m
endif

quiet_C   = $(Q)printf "  ${C_C}[C]${C_RESET}     %s\n" "$<";
quiet_CXX = $(Q)printf "  ${C_CXX}[C++]${C_RESET}   %s\n" "$<";
quiet_ASM = $(Q)printf "  ${C_ASM}[ASM]${C_RESET}   %s\n" "$<";
quiet_LD  = $(Q)printf "  ${C_LD}[LD]${C_RESET}   %s\n" "$@";

# ---------------------------------------------------------------------------
# Default goal FIRST
# ---------------------------------------------------------------------------
.PHONY: all iso run clean tsys tsys-clean tsys-install usr usr-clean luna test test-img

all: iso

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------
SRCS :=
CPPSRCS :=
RUSTSRCS :=

CFLAGS = -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic \
         -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel \
         -O2 -Wall -Wextra -Ikernel -Ilimine

CXXFLAGS = -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic \
           -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel \
           -O2 -Wall -Wextra -Ikernel -Ilimine \
           -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
           -nostdinc++ -std=c++20

SRCS += \
       kernel/main.c \
       kernel/arch/x86_64/gdt.c \
       kernel/arch/x86_64/idt.c \
       kernel/arch/x86_64/pic.c \
       kernel/arch/x86_64/pit.c \
       kernel/arch/x86_64/fpu.c \
       kernel/drivers/rtc/rtc.c

SRCS += \
       kernel/mm/pmm.c \
       kernel/mm/vmm.c \
       kernel/mm/heap.c

SRCS += \
       kernel/lib/printf.c \
       kernel/lib/string.c

CPPSRCS += kernel/lib/cxxabi.cpp

SRCS += \
       kernel/drivers/input/keyboard.c \
       kernel/drivers/input/mouse.c \
       kernel/sched/scheduler.c

SRCS += kernel/proc/process.c
SRCS += kernel/ob/object.c
SRCS += kernel/io/ioman.c
SRCS += kernel/drivers/storage/ata_ioman.c
SRCS += kernel/drivers/input/keyboard_ioman.c
SRCS += kernel/ipc/port.c
SRCS += \
       kernel/proc/launch.c \
       kernel/proc/exec.c

SRCS += \
       kernel/drivers/video/fb.c \
       kernel/drivers/video/terminal.c \
       kernel/drivers/serial/serial.c

SRCS += \
       kernel/drivers/net/pci.c \
       kernel/drivers/net/virtio_net.c \
	   kernel/drivers/gpu/virtio_gpu_vq.c \
       kernel/net/net.c \
	   kernel/drivers/driver.c

SRCS += \
       kernel/drivers/storage/ata.c \
       kernel/drivers/storage/disk.c

SRCS += \
       kernel/fs/vfs.c \
       kernel/fs/ramfs.c \
       kernel/fs/tfs.c \
       kernel/fs/devfs.c \
	   kernel/fs/install.c

SRCS += kernel/shell/shell.c
SRCS += \
       kernel/user/syscall.c \
       kernel/user/uaccess.c \
       kernel/user/userspace.c

RUSTSRCS += kernel/rust/root.rs

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS)) \
       $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPPSRCS)) \
	$(patsubst %.rs,$(BUILD_DIR)/%.o,$(RUSTSRCS)) \
       $(BUILD_DIR)/kernel/arch/x86_64/entry.o \
       $(BUILD_DIR)/kernel/arch/x86_64/gdt_asm.o \
       $(BUILD_DIR)/kernel/arch/x86_64/isr.o \
       $(BUILD_DIR)/kernel/sched/context_switch.o \
       $(BUILD_DIR)/kernel/user/syscall_asm.o \
       $(BUILD_DIR)/kernel/user/userspace_asm.o

KERNEL = kernel.elf

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(quiet_C) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(quiet_CXX) $(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(quiet_ASM) $(NASM) -f elf64 $< -o $@

$(BUILD_DIR)/%.o: %.rs
	@mkdir -p $(dir $@)
	$(quiet_RS) $(RUSTC) $(RUSTFLAGS) -o $@ $<

$(KERNEL): $(OBJS)
	$(quiet_LD) $(LD) -T kernel/arch/x86_64/linker.ld -nostdlib -m elf_x86_64 -o $@ $(OBJS)

iso: $(KERNEL) tsys luna
	@rm -rf iso
	@mkdir -p iso/boot/bin
	@cp $(KERNEL) iso/boot/kernel.elf
	@cp limine/limine-bios.sys iso/boot/
	@cp limine/limine-bios-cd.bin iso/boot/
	@cp limine/limine-uefi-cd.bin iso/boot/
	@cp limine/BOOTX64.EFI iso/boot/
	@cp limine.conf iso/limine.conf
	@mkdir -p iso/boot/icons
	@cp assets/icons/*.rgba iso/boot/icons/ 2>/dev/null || true
	@cp assets/logo.png iso/boot/ 2>/dev/null || true
	@# userspace payload (install media)
	$(Q)for b in $(TSYS_BINS); do \
		printf "  [ISO]   boot/bin/$$(basename $$b)\n"; \
		cp $$b iso/boot/bin/; \
	done
	$(Q)if [ -f $(LUNA_BIN) ]; then \
		printf "  [ISO]   boot/bin/luna.tsys\n"; \
		cp $(LUNA_BIN) iso/boot/bin/; \
	fi
	@xorriso -as mkisofs \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		-graft-points \
		boot/=iso/boot \
		limine.conf=iso/limine.conf \
		-o termuos.iso

# ---------------------------------------------------------------------------
# Host tools
# ---------------------------------------------------------------------------
tools/mkfs_tfs: tools/mkfs_tfs.c
	@cc -O2 -o tools/mkfs_tfs tools/mkfs_tfs.c

tools/tfs_write: tools/tfs_write.c
	@cc -O2 -o tools/tfs_write tools/tfs_write.c

disk.img: tools/mkfs_tfs
	@./tools/mkfs_tfs disk.img 64

# ---------------------------------------------------------------------------
# usr/ — userspace projects (Luna submodule, etc.)
# ---------------------------------------------------------------------------
LUNA_DIR := usr/src/luna
LUNA_BIN := $(LUNA_DIR)/luna.tsys

.PHONY: luna
luna: $(TSYS_LIB_A)
	$(Q)printf "  [USR]   luna\n"
	$(Q)$(MAKE) -C $(LUNA_DIR) TERMUOS_ROOT=$(CURDIR)

# Future: other usr packages go here and as dependencies of `usr`
usr: tsys luna

usr-clean:
	$(Q)$(MAKE) -C $(LUNA_DIR) clean || true

# ---------------------------------------------------------------------------
# tsys userspace + libtsys
# ---------------------------------------------------------------------------
TSYS_CC      := gcc
TSYS_CFLAGS  := -static -nostdlib -no-pie -ffreestanding \
                -fno-stack-protector -fno-asynchronous-unwind-tables \
                -fcf-protection=none -O2 -Wall \
                -Itsys/lib/include

TSYS_CRT0    := tsys/lib/crt0.S
TSYS_OUT     := $(BUILD_DIR)/tsys
TSYS_LIB_A   := $(TSYS_OUT)/libtsys.a
DISK_IMG     ?= disk.img
TFS_WRITE    := ./tools/tfs_write

TSYS_HDRS := $(wildcard tsys/lib/include/*.h)

TSYS_LIB_SRCS := \
	tsys/lib/src/syscall.c \
	tsys/lib/src/unistd.c \
	tsys/lib/src/string.c \
	tsys/lib/src/fb.c \
	tsys/lib/src/input.c \
	tsys/lib/src/rtc.c \
	tsys/lib/src/dirent.c

TSYS_LIB_OBJS := $(patsubst tsys/lib/src/%.c,$(TSYS_OUT)/lib/%.o,$(TSYS_LIB_SRCS))

$(TSYS_OUT) $(TSYS_OUT)/lib:
	mkdir -p $@

$(TSYS_OUT)/lib/%.o: tsys/lib/src/%.c $(TSYS_HDRS) | $(TSYS_OUT)/lib
	$(Q)printf "  [TSYS]  %s\n" "$<"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -c $< -o $@

$(TSYS_LIB_A): $(TSYS_LIB_OBJS)
	$(Q)printf "  [AR]    libtsys.a\n"
	$(Q)ar rcs $@ $^

$(TSYS_OUT)/echo.tsys: tsys/echo/echo.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  echo.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/echo/echo.c $(TSYS_LIB_A)

$(TSYS_OUT)/uname.tsys: tsys/uname/uname.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  uname.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/uname/uname.c $(TSYS_LIB_A)

$(TSYS_OUT)/cat.tsys: tsys/cat/cat.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  cat.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/cat/cat.c $(TSYS_LIB_A)

$(TSYS_OUT)/edit.tsys: tsys/edit/edit.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  edit.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/edit/edit.c $(TSYS_LIB_A)

$(TSYS_OUT)/lsdrv.tsys: tsys/lsdrv/lsdrv.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  lsdrv.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/lsdrv/lsdrv.c $(TSYS_LIB_A)

$(TSYS_OUT)/help.tsys: tsys/help/help.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
	$(Q)printf "  [TSYS]  help.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/help/help.c $(TSYS_LIB_A)

# Add more apps here, e.g.:
# $(TSYS_OUT)/hello.tsys: tsys/hello/hello.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TSYS_OUT)
#	$(Q)printf "  [TSYS]  hello.tsys\n"
#	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) tsys/hello/hello.c $(TSYS_LIB_A)

TSYS_BINS := $(TSYS_OUT)/echo.tsys $(TSYS_OUT)/uname.tsys $(TSYS_OUT)/cat.tsys $(TSYS_OUT)/edit.tsys $(TSYS_OUT)/lsdrv.tsys $(TSYS_OUT)/help.tsys

tsys: $(TSYS_BINS)

tsys-clean:
	rm -rf $(TSYS_OUT)

# Requires existing TFS disk.img (make disk.img once after mkfs)
tsys-install: tsys luna tools/tfs_write $(DISK_IMG)
	$(Q)printf "  [INST]  /bin/echo.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/echo.tsys /bin/echo.tsys
	$(Q)printf "  [INST]  /bin/uname.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/uname.tsys /bin/uname.tsys
	$(Q)printf "  [INST]  /bin/cat.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/cat.tsys /bin/cat.tsys
	$(Q)printf "  [INST]  /bin/edit.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/edit.tsys /bin/edit.tsys
	$(Q)printf "  [INST]  /bin/lsdrv.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/lsdrv.tsys /bin/lsdrv.tsys
	$(Q)printf "  [INST]  /bin/luna.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(LUNA_BIN) /bin/luna.tsys
	$(Q)printf "  [INST]  /bin/help.tsys\n"
	$(Q)$(TFS_WRITE) $(DISK_IMG) $(TSYS_OUT)/help.tsys /bin/help.tsys

# ---------------------------------------------------------------------------
# Tests: boot under QEMU and check the userland from the shell
# ---------------------------------------------------------------------------
TEST_IMG   := test.img
TEST_OUT   := $(BUILD_DIR)/tests
TEST_SRCS  := $(wildcard tests/tsys/*.c)
TEST_BINS  := $(patsubst tests/tsys/%.c,$(TEST_OUT)/%.tsys,$(TEST_SRCS))
TEST_FIXTURES := motd big.txt block.txt empty.txt scratch.txt

$(TEST_OUT):
	mkdir -p $@

$(TEST_OUT)/%.tsys: tests/tsys/%.c $(TSYS_CRT0) $(TSYS_LIB_A) $(TSYS_HDRS) | $(TEST_OUT)
	$(Q)printf "  [TEST]  $*.tsys\n"
	$(Q)$(TSYS_CC) $(TSYS_CFLAGS) -o $@ $(TSYS_CRT0) $< $(TSYS_LIB_A)

# A throwaway image, rebuilt every run: tfs_write never overwrites, so the
# tests must not reuse an image that already holds older copies.
.PHONY: test-img
test-img: tools/mkfs_tfs tools/tfs_write tsys $(TEST_BINS)
	$(Q)rm -f $(TEST_IMG)
	$(Q)./tools/mkfs_tfs $(TEST_IMG) 64 >/dev/null
	$(Q)for b in $(TSYS_BINS); do \
		printf "  [INST]  /bin/$$(basename $$b)\n"; \
		$(TFS_WRITE) $(TEST_IMG) $$b /bin/$$(basename $$b) >/dev/null; \
	done
	$(Q)for b in $(TEST_BINS); do \
		printf "  [INST]  /bin/$$(basename $$b)\n"; \
		$(TFS_WRITE) $(TEST_IMG) $$b /bin/$$(basename $$b) >/dev/null; \
	done
	$(Q)for f in $(TEST_FIXTURES); do \
		printf "  [INST]  /etc/$$f\n"; \
		$(TFS_WRITE) $(TEST_IMG) tests/fixtures/$$f /etc/$$f >/dev/null; \
	done

test: iso test-img
	$(Q)python3 tests/run_tests.py --iso termuos.iso --img $(TEST_IMG)

# ---------------------------------------------------------------------------
# Run: ISO + disk + install tsys into the image
# ---------------------------------------------------------------------------
run: iso $(DISK_IMG)
	@qemu-system-x86_64 -cdrom termuos.iso -cpu qemu64,+syscall -m 125M \
		-netdev user,id=net0 \
		-device virtio-net-pci,netdev=net0,disable-modern=on \
		-device virtio-gpu-pci \
		-drive file=$(DISK_IMG),format=raw,if=ide \
		-serial stdio 

limine:
	git clone https://github.com/limine-bootloader/limine.git \
		--branch=v8.x-binary --depth=1

clean:
	@rm -rf $(BUILD_DIR) $(KERNEL) termuos.iso iso/ disk.img test.img tests-serial.log
	@$(MAKE) -C $(LUNA_DIR) clean 2>/dev/null || true
