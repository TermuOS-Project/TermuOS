#![allow(dead_code)]

const VIRTIO_VENDOR: u16 = 0x1AF4;
const VIRTIO_GPU_DEVICE: u16 = 0x1050;
const PCI_CAP_ID_VNDR: u8 = 0x09;

const VIRTIO_PCI_CAP_COMMON_CFG: u8 = 1;
const VIRTIO_PCI_CAP_NOTIFY_CFG: u8 = 2;
const VIRTIO_PCI_CAP_ISR_CFG: u8 = 3;
const VIRTIO_PCI_CAP_DEVICE_CFG: u8 = 4;

const VIRTIO_STATUS_ACKNOWLEDGE: u8 = 1;
const VIRTIO_STATUS_DRIVER: u8 = 2;

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

extern "C" {
    fn pci_find(vendor: u16, device: u16, out: *mut PciDevice) -> i32;
    fn pci_read(bus: u8, slot: u8, func: u8, offset: u8) -> u32;
    fn pci_write(bus: u8, slot: u8, func: u8, offset: u8, val: u32);
    fn kprintf(fmt: *const u8, ...);
    fn termuos_hhdm_base() -> u64;
    fn virtio_gpu_vq_init_and_get_display(
        common: *mut u8,
        notify: *mut u8,
        notify_mult: u32,
    ) -> i32;
    fn virtio_gpu_phase5_rust() -> i32;
    fn driver_register(d: *const GpuDriver);
}

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
        unsafe {
            kprintf(b"virtio-gpu: not found\n\0".as_ptr());
        }
        return;
    }

    unsafe {
        kprintf(b"virtio-gpu: pci found\n\0".as_ptr());
        pci_enable_mem_bm(dev.bus, dev.slot, dev.func);
    }

    let mut common_loc: Option<CapLoc> = None;
    let mut notify_loc: Option<CapLoc> = None;
    let mut notify_mult: u32 = 1;
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
                        notify_loc = Some(loc);
                        let mult = pci_read(dev.bus, dev.slot, dev.func, cap.wrapping_add(16));
                        notify_mult = if mult == 0 { 1 } else { mult };
                    }
                    VIRTIO_PCI_CAP_ISR_CFG => {
                        kprintf(b"virtio-gpu: cap isr\n\0".as_ptr());
                    }
                    VIRTIO_PCI_CAP_DEVICE_CFG => {
                        kprintf(b"virtio-gpu: cap device\n\0".as_ptr());
                    }
                    _ => {
                        kprintf(b"virtio-gpu: cap other\n\0".as_ptr());
                    }
                }
            }
        }
        cap = unsafe { pci_read8(dev.bus, dev.slot, dev.func, cap.wrapping_add(1)) };
    }

    let Some(common_loc) = common_loc else {
        unsafe {
            kprintf(b"virtio-gpu: no common cfg\n\0".as_ptr());
        }
        return;
    };

    let Some(notify_loc) = notify_loc else {
        unsafe {
            kprintf(b"virtio-gpu: no notify cfg\n\0".as_ptr());
        }
        return;
    };

    let cbi = common_loc.bar as usize;
    let nbi = notify_loc.bar as usize;
    if cbi >= 6 || nbi >= 6 {
        unsafe {
            kprintf(b"virtio-gpu: bad bar idx\n\0".as_ptr());
        }
        return;
    }

    let common_bar = unsafe { *dev.bar.as_ptr().add(cbi) };
    let notify_bar = unsafe { *dev.bar.as_ptr().add(nbi) };

    let common_base = unsafe { map_bar(common_bar) };
    let notify_base = unsafe { map_bar(notify_bar) };
    if common_base.is_null() || notify_base.is_null() {
        unsafe {
            kprintf(b"virtio-gpu: bar map failed\n\0".as_ptr());
        }
        return;
    }

    let common = unsafe { common_base.add(common_loc.offset as usize) };
    let notify = unsafe { notify_base.add(notify_loc.offset as usize) };

    unsafe {
        mmio_w8(common, 0x14, 0);
        mmio_w8(common, 0x14, VIRTIO_STATUS_ACKNOWLEDGE);
        mmio_w8(
            common,
            0x14,
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER,
        );
        kprintf(b"virtio-gpu: phase2 ok (ACK|DRIVER)\n\0".as_ptr());

        let rc = virtio_gpu_vq_init_and_get_display(common, notify, notify_mult);
        if rc != 0 {
            kprintf(b"virtio-gpu: vq/display failed\n\0".as_ptr());
        } else {
            let _ = virtio_gpu_phase5_rust();
        }
    }
}

#[repr(C)]
struct GpuDriver {
    name: *const u8,
    init: Option<extern "C" fn() -> i32>,
    priority: i32,
}

unsafe impl Sync for GpuDriver {}

extern "C" fn virtio_gpu_driver_init() -> i32 {
    unsafe {
        virtio_gpu_rust_probe();
    }
    0
}

static GPU_DRIVER: GpuDriver = GpuDriver {
    name: b"virtio-gpu\0".as_ptr(),
    init: Some(virtio_gpu_driver_init),
    priority: 40,
};

#[no_mangle]
pub unsafe extern "C" fn virtio_gpu_driver_register() {
    driver_register(&GPU_DRIVER as *const GpuDriver as *const _);
}
