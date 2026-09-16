#![allow(dead_code)]

const VIRTIO_VENDOR: u16 = 0x1AF4;
const VIRTIO_GPU_DEVICE: u16 = 0x1050;
const PCI_CAP_ID_VNDR: u8 = 0x09;

const VIRTIO_PCI_CAP_COMMON_CFG: u8 = 1;
const VIRTIO_PCI_CAP_NOTIFY_CFG: u8 = 2;
const VIRTIO_PCI_CAP_ISR_CFG: u8 = 3;
const VIRTIO_PCI_CAP_DEVICE_CFG: u8 = 4;

const VIRTIO_STATUS_ACKOWNLEDGE: u8 = 1;
const VIRTIO_STATUS_DRIVER: u8 = 2;
const VIRTIO_STATUS_FEATURES_OK: u8 = 8;
const VIRTIO_STATUS_DRIVER_OK: u8 = 4;

const VIRTIO_GPU_CMD_GET_DISPLAY_INFO: u32 = 0x100;
const VIRTIO_GPU_RESP_OK_DISPLAY_INFO: u32 = 0x101;
const VRING_DESC_F_NEXT: u16 = 1;
const VRING_DESC_F_WRITE: u16 = 2;

#[repr(C)]
struct PciDevice {
    vendor: u16,
    device: u16,
    bus: u8,
    slot: u8,
    func: u8,
    bar: [u32; 6],
    irq: u8,
}

struct CapLoc {
    bar: u8,
    offset: u32,
    length: u32,
}

#[repr(C)]
struct VirtioGpuCtrlHdr {
    type_: u32,
    flags: u32,
    fence_id: u64,
    ctx_id: u32,
    ring_idx: u32,
    padding: u32,
}

#[repr(C)]
struct VirtioGpuRect {
    x: u32,
    y: u32,
    width: u32,
    height: u32,
}

#[repr(C)]
struct VirtioGpuDisplayOne {
    r: VirtioGpuRect,
    enabled: u32,
    flags: u32,
}

// 16 scanouts in the spec
#[repr(C)]
struct VirtioGpuRespDisplayInfo {
    hdr: VirtioGpuCtrlHdr,
    pmodes: [VirtioGpuDisplayOne; 16],
}

extern "C" {
    fn pci_find(vendor: u16, device: u16, out: *mut PciDevice) -> i32;
    fn pci_read(bus: u8, slot: u8, func: u8, offset: u8) -> u32;
    fn pci_write(bus: u8, slot: u8, func: u8, offset: u8, val: u32);
    fn kprintf(fmt: *const u8, ...);
    fn termuos_hhdm_base() -> u64;
    fn kvirt_to_phys(v: *mut u8) -> u64;
}

#[repr(C, align(4096))]
struct QueueMem([u8; 64 * 1024]);

static mut QUEUE_MEM: QueueMem = QueueMem([0; 64 * 1024]);
static mut GPU_COMMON: *mut u8 = core::ptr::null_mut();
static mut GPU_QSZ: usize = 0;
static mut GPU_DESC_OFF: usize = 0;
static mut GPU_AVAIL_OFF: usize = 0;
static mut GPU_USED_OFF: usize = 0;
static mut GPU_NOTIFY: *mut u8 = core::ptr::null_mut();
static mut GPU_NOTIFY_MULT: u32 = 0;

#[repr(C, align(16))]
struct CmdBuf {
    req: VirtioGpuCtrlHdr,
    resp: VirtioGpuRespDisplayInfo,
}
static mut CMD: CmdBuf = unsafe { core::mem::zeroed() };

unsafe fn pci_read8(bus: u8, slot: u8, func: u8, off: u8) -> u8 {
    let v = pci_read(bus, slot, func, off & !3);
    ((v >> ((off & 3) * 8)) & 0xff) as u8
}

unsafe fn pci_enable_mem_bm(bus: u8, slot: u8, func: u8) {
    let mut cmd = pci_read(bus, slot, func, 0x04);
    cmd |= (1 << 1) | (1 << 2);
    pci_write(bus, slot, func, 0x04, cmd);
}

unsafe fn read_vndr_cap(bus: u8, slot: u8, func: u8, cap_off: u8) -> (u8, CapLoc) {
    let cfg_type = pci_read8(bus, slot, func, cap_off.wrapping_add(3));
    let bar = pci_read8(bus, slot, func, cap_off.wrapping_add(4));
    let offset = pci_read(bus, slot, func, cap_off.wrapping_add(8));
    let length = pci_read(bus, slot, func, cap_off.wrapping_add(12));
    (
        cfg_type,
        CapLoc {
            bar,
            offset,
            length,
        },
    )
}

fn bar_phys(bar: u32) -> u64 {
    (bar & !0xF) as u64
}

unsafe fn map_bar(bar_raw: u32) -> *mut u8 {
    let phys = bar_phys(bar_raw);
    if phys == 0 {
        return core::ptr::null_mut();
    }
    (termuos_hhdm_base() + phys) as *mut u8
}

unsafe fn mmio_w8(p: *mut u8, off: usize, v: u8) {
    core::ptr::write_volatile(p.add(off), v);
}
unsafe fn mmio_r8(p: *mut u8, off: usize) -> u8 {
    core::ptr::read_volatile(p.add(off))
}
unsafe fn mmio_w16(p: *mut u8, off: usize, v: u16) {
    core::ptr::write_volatile(p.add(off) as *mut u16, v);
}
unsafe fn mmio_r16(p: *mut u8, off: usize) -> u16 {
    core::ptr::read_volatile(p.add(off) as *mut u16)
}
unsafe fn mmio_w64(p: *mut u8, off: usize, v: u64) {
    core::ptr::write_volatile(p.add(off) as *mut u64, v);
}

unsafe fn setup_controlq(common: *mut u8) -> i32 {
    unsafe fn mmio_w32(p: *mut u8, off: usize, v: u32) {
        core::ptr::write_volatile(p.add(off) as *mut u32, v);
    }

    mmio_w32(common, 0x08, 1);
    mmio_w32(common, 0x0c, 1);
    mmio_w32(common, 0x08, 0);
    mmio_w32(common, 0x0c, 0);

    let st = mmio_r8(common, 0x14);
    mmio_w8(common, 0x14, st | VIRTIO_STATUS_FEATURES_OK);
    if mmio_r8(common, 0x14) & VIRTIO_STATUS_FEATURES_OK == 0 {
        kprintf(b"virtio-gpu: FEATURES_OK rejected\n\0".as_ptr());
        return -1;
    }

    mmio_w16(common, 0x16, 0);
    let mut qsz = mmio_r16(common, 0x18) as usize;
    if qsz == 0 {
        kprintf(b"virtio-gpu: queue0 size 0\n\0".as_ptr());
        return -1;
    }
    if qsz > 64 {
        qsz = 64;
        mmio_w16(common, 0x18, qsz as u16);
    }

    let mem = QUEUE_MEM.0.as_mut_ptr();
    core::ptr::write_bytes(mem, 0, QUEUE_MEM.0.len());

    let desc_bytes = 16 * qsz;
    let avail_bytes = 4 + 2 * qsz;
    let used_bytes = 4 + 8 * qsz;
    let used_off = (desc_bytes + avail_bytes + 0xfff) & !0xfff;

    let desc_p = kvirt_to_phys(mem);
    let avail_p = kvirt_to_phys(mem.add(desc_bytes));
    let used_p = kvirt_to_phys(mem.add(used_off));

    if desc_p == 0 || avail_p == 0 || used_p == 0 {
        kprintf(b"virtio-gpu: bad queue phys\n\0".as_ptr());
        return -1;
    }

    mmio_w64(common, 0x20, desc_p);
    mmio_w64(common, 0x28, avail_p);
    mmio_w64(common, 0x30, used_p);
    mmio_w16(common, 0x1c, 1);

    GPU_COMMON = common;
    GPU_QSZ = qsz;
    GPU_DESC_OFF = 0;
    GPU_AVAIL_OFF = desc_bytes;
    GPU_USED_OFF = used_off;

    let st = mmio_r8(common, 0x14);
    mmio_w8(common, 0x14, st | VIRTIO_STATUS_DRIVER_OK);

    kprintf(b"virtio-gpu: phase3 ok (controlq enabled)\n\0".as_ptr());
    0
}

unsafe fn queue_kick() {
    if GPU_COMMON.is_null() {
        return;
    }
    mmio_w16(GPU_COMMON, 0x16, 0);
    let off = mmio_r16(GPU_COMMON, 0x1e) as u32;
    if GPU_NOTIFY.is_null() {
        kprintf(b"virtio-gpu: kick: no notify\n\0".as_ptr());
        return;
    }
    kprintf(b"virtio-gpu: kick ok\n\0".as_ptr());
    let addr = GPU_NOTIFY.add((off.wrapping_mul(GPU_NOTIFY_MULT)) as usize);
    // Modern: write queue index
    core::ptr::write_volatile(addr as *mut u32, 0u32);
}

unsafe fn phase4_get_display_info() -> i32 {
    if GPU_COMMON.is_null() || GPU_QSZ < 2 {
        kprintf(b"virtio-gpu: phase4 no queue\n\0".as_ptr());
        return -1;
    }

    const CMD_OFF: usize = 32 * 1024;
    
    let mem = core::ptr::addr_of_mut!(QUEUE_MEM) as *mut u8;
    let desc = mem.add(GPU_DESC_OFF);
    let avail = mem.add(GPU_AVAIL_OFF);
    let used = mem.add(GPU_USED_OFF);

    let req_ptr = mem.add(CMD_OFF);
    let resp_ptr = mem.add(CMD_OFF + 64);

    core::ptr::write_bytes(req_ptr, 0, 64);
    core::ptr::write_bytes(
        resp_ptr,
        0,
        core::mem::size_of::<VirtioGpuRespDisplayInfo>(),
    );
    // type_ is the first field of VirtioGpuCtrlHdr
    core::ptr::write_volatile(req_ptr as *mut u32, VIRTIO_GPU_CMD_GET_DISPLAY_INFO);

    let req_phys = kvirt_to_phys(req_ptr);
    let resp_phys = kvirt_to_phys(resp_ptr);
    if req_phys == 0 || resp_phys == 0 {
        kprintf(b"virtio-gpu: bad cmd phys\n\0".as_ptr());
        return -1;
    }

    let write_desc = |i: usize, addr: u64, len: u32, flags: u16, next: u16| {
        let p = desc.add(i * 16);
        core::ptr::write_volatile(p as *mut u64, addr);
        core::ptr::write_volatile(p.add(8) as *mut u32, len);
        core::ptr::write_volatile(p.add(12) as *mut u16, flags);
        core::ptr::write_volatile(p.add(14) as *mut u16, next);
    };

    write_desc(
        0,
        req_phys,
        core::mem::size_of::<VirtioGpuCtrlHdr>() as u32,
        VRING_DESC_F_NEXT,
        1,
    );
    write_desc(
        1,
        resp_phys,
        core::mem::size_of::<VirtioGpuRespDisplayInfo>() as u32,
        VRING_DESC_F_WRITE,
        0,
    );

    let used_idx_before = core::ptr::read_volatile(used.add(2) as *const u16);

    let avail_idx = core::ptr::read_volatile(avail.add(2) as *const u16);
    let mut slot = avail_idx as usize;
    if GPU_QSZ == 0 {
        slot = 0;
    } else {
        while slot >= GPU_QSZ {
            slot -= GPU_QSZ;
        }
    }

    core::ptr::write_volatile(avail.add(4 + slot * 2) as *mut u16, 0u16);
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    core::ptr::write_volatile(avail.add(2) as *mut u16, avail_idx.wrapping_add(1));
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    queue_kick();

    let mut spins = 0u32;
    loop {
        let used_idx = core::ptr::read_volatile(used.add(2) as *const u16);
        if used_idx != used_idx_before {
            break;
        }
        spins = spins.wrapping_add(1);
        if spins > 10_000_000 {
            kprintf(b"virtio-gpu: phase4 timeout\n\0".as_ptr());
            return -1;
        }
        core::hint::spin_loop();
    }

    let t = core::ptr::read_volatile(resp_ptr as *const u32);
    if t != VIRTIO_GPU_RESP_OK_DISPLAY_INFO {
        kprintf(b"virtio-gpu: phase4 bad resp type\n\0".as_ptr());
        return -1;
    }

    let mode0 =
        resp_ptr.add(core::mem::size_of::<VirtioGpuCtrlHdr>()) as *const VirtioGpuDisplayOne;
    let w = (*mode0).r.width;
    let h = (*mode0).r.height;
    if w > 0 && h > 0 {
        kprintf(b"virtio-gpu: phase4 ok (got display info)\n\0".as_ptr());
    } else {
        kprintf(b"virtio-gpu: phase4 ok (empty mode0)\n\0".as_ptr());
    }
    let _ = (w, h);
    0
}

#[no_mangle]
pub extern "C" fn virtio_gpu_rust_probe() {
    let mut dev = PciDevice {
        vendor: 0,
        device: 0,
        bus: 0,
        slot: 0,
        func: 0,
        bar: [0; 6],
        irq: 0,
    };

    if unsafe { pci_find(VIRTIO_VENDOR, VIRTIO_GPU_DEVICE, &mut dev) } != 0 {
        unsafe { kprintf(b"virtio-gpu: not found\n\0".as_ptr()) };
        return;
    }
    unsafe {
        kprintf(b"virtio-gpu: pci found\n\0".as_ptr());
        pci_enable_mem_bm(dev.bus, dev.slot, dev.func);
    }

    let mut common_loc: Option<CapLoc> = None;
    let mut cap = unsafe { pci_read8(dev.bus, dev.slot, dev.func, 0x34) };

    while cap != 0 && cap != 0xff {
        let id = unsafe { pci_read8(dev.bus, dev.slot, dev.func, cap) };
        if id == PCI_CAP_ID_VNDR {
            let (typ, loc) = unsafe { read_vndr_cap(dev.bus, dev.slot, dev.func, cap) };
            unsafe {
                match typ {
                    VIRTIO_PCI_CAP_COMMON_CFG => {
                        kprintf(b"virtio-gpu: cap common\n\0".as_ptr());
                        common_loc = Some(loc);
                    }
                    VIRTIO_PCI_CAP_NOTIFY_CFG => {
                        kprintf(b"virtio-gpu: cap notify\n\0".as_ptr());
                        let bi = loc.bar as usize;
                        if bi < 6 {
                            let bar_raw = *dev.bar.as_ptr().add(bi);
                            let nb = map_bar(bar_raw);
                            if !nb.is_null() {
                                GPU_NOTIFY = nb.add(loc.offset as usize);
                            }
                        }
                        // virtio_pci_notify_cap.notify_off_multiplier at +16 from cap header
                        let mult = pci_read(dev.bus, dev.slot, dev.func, cap.wrapping_add(16));
                        GPU_NOTIFY_MULT = if mult == 0 { 1 } else { mult };
                    }
                    VIRTIO_PCI_CAP_ISR_CFG => kprintf(b"virtio-gpu: cap isr\n\0".as_ptr()),
                    VIRTIO_PCI_CAP_DEVICE_CFG => kprintf(b"virtio-gpu: cap device\n\0".as_ptr()),
                    _ => kprintf(b"virtio-gpu: cap other\n\0".as_ptr()),
                }
            }
        }
        cap = unsafe { pci_read8(dev.bus, dev.slot, dev.func, cap.wrapping_add(1)) };
    }

    let Some(common_loc) = common_loc else {
        unsafe { kprintf(b"virtio-gpu: no common cfg\n\0".as_ptr()) };
        return;
    };

    if (common_loc.bar as usize) >= 6 {
        unsafe { kprintf(b"virtio-gpu: bad bar idx\n\0".as_ptr()) };
        return;
    }

    let bi = common_loc.bar as usize;
    let bar_raw = unsafe { *dev.bar.as_ptr().add(bi) };
    let base = unsafe { map_bar(bar_raw) };
    if base.is_null() {
        unsafe { kprintf(b"virtio-gpu: bar map failed\n\0".as_ptr()) };
        return;
    }

    let common = unsafe { base.add(common_loc.offset as usize) };

    unsafe {
        mmio_w8(common, 0x14, 0);
        mmio_w8(common, 0x14, VIRTIO_STATUS_ACKOWNLEDGE);
        mmio_w8(
            common,
            0x14,
            VIRTIO_STATUS_ACKOWNLEDGE | VIRTIO_STATUS_DRIVER,
        );
        kprintf(b"virtio-gpu: phase2 ok (ACK|DRIVER)\n\0".as_ptr());
    }

    unsafe {
        let _ = setup_controlq(common);
    }
    unsafe {
        let _ = phase4_get_display_info();
    }
}
