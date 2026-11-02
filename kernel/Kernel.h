#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Arch
#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/fpu.h"

// Lib
#include "lib/printf.h"
#include "lib/string.h"
#include "lib/cxxabi.h"
#include "lib/rdrand.h"
#include "lib/crc32.h"

// Memory
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"

// Object manager
#include "ob/object.h"
#include "ob/event.h"

// Process / scheduler
#include "proc/process.h"
#include "proc/exec.h"
#include "proc/launch.h"
#include "sched/scheduler.h"

// I/O manager
#include "io/ioman.h"

// VFS
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "fs/tfs.h"
#include "fs/devfs.h"
#include "fs/install.h"

// IPC
#include "ipc/port.h"

// Net
#include "net/net.h"
#include "net/tcp.h"

// Userland interface
#include "user/syscall.h"
#include "user/uaccess.h"
#include "user/userspace.h"

// Shell
#include "shell/shell.h"

// Drivers - registry
#include "drivers/driver.h"

// Drivers - PCI
#include "drivers/net/pci.h"

// Drivers - video / console
#include "drivers/video/fb.h"
#include "drivers/video/terminal.h"
#include "drivers/serial/serial.h"

// Drivers - input
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"

// Drivers - storage
#include "drivers/storage/ata.h"
#include "drivers/storage/disk.h"

// Drivers - net / gpu / sound
#include "drivers/net/virtio_net.h"
#include "drivers/gpu/virtio_gpu_vq.h"
#include "drivers/gpu/gpu_fb.h"
#include "drivers/sound/virtio_snd.h"
#include "drivers/speaker/speaker.h"

// Drivers - RTC
#include "drivers/rtc/rtc.h"
