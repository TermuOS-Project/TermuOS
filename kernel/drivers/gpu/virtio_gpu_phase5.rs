const VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: u32 = 0x101;
const VIRTIO_GPU_CMD_SET_SCANOUT: u32 = 0x103;
const VIRTIO_GPU_CMD_RESOURCE_FLUSH: u32 = 0x104;
const VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: u32 = 0x105;
const VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: u32 = 0x106;
const VIRTIO_GPU_RESP_OK_NODATA: u32 = 0x1100;
const VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM: u32 = 1;

const FB_W: usize = 640;
const FB_H: usize = 480;
const FB_ID: u32 = 1;

#[repr(C)]
struct CtrlHdr {
    type_: u32,
    flags: u32,
    fence_id: u64,
    ctx_id: u32,
    ring_idx: u8,
    padding: [u8; 3],
}

#[repr(C)]
struct Rect {
    x: u32,
    y: u32,
    width: u32,
    height: u32,
}

#[repr(C)]
struct ResourceCreate2d {
    hdr: CtrlHdr,
    resource_id: u32,
    format: u32,
    width: u32,
    height: u32,
}

#[repr(C)]
struct MemEntry {
    addr: u64,
    length: u32,
    padding: u32,
}

#[repr(C)]
struct ResourceAttachBacking {
    hdr: CtrlHdr,
    resource_id: u32,
    nr_entries: u32,
}

#[repr(C)]
struct AttachBackingMsg {
    a: ResourceAttachBacking,
    e: MemEntry,
}

#[repr(C)]
struct SetScanout {
    hdr: CtrlHdr,
    r: Rect,
    scanout_id: u32,
    resource_id: u32,
}

#[repr(C)]
struct TransferToHost2d {
    hdr: CtrlHdr,
    r: Rect,
    offset: u64,
    resource_id: u32,
    padding: u32,
}

#[repr(C)]
struct ResourceFlush {
    hdr: CtrlHdr,
    r: Rect,
    resource_id: u32,
    padding: u32,
}

#[repr(C)]
struct RespHdr {
    type_: u32,
    _rest: [u8; 24],
}

extern "C" {
    fn virtio_gpu_submit(out: *mut u8, out_len: u32, in_: *mut u8, in_len: u32) -> i32;
    fn kprintf(fmt: *const u8, ...);
    fn kvirt_to_phys(v: *mut u8) -> u64;
}

#[repr(C, align(4096))]
struct Fb([u32; FB_W * FB_H]);

static mut FB: Fb = Fb([0; FB_W * FB_H]);
static mut RESP: RespHdr = RespHdr {
    type_: 0,
    _rest: [0; 24],
};

fn zero_hdr(h: &mut CtrlHdr) {
    h.type_ = 0;
    h.flags = 0;
    h.fence_id = 0;
    h.ctx_id = 0;
    h.ring_idx = 0;
    h.padding = [0; 3];
}

unsafe fn submit_ok(out: *mut u8, out_len: usize) -> bool {
    core::ptr::write_bytes(
        core::ptr::addr_of_mut!(RESP) as *mut u8,
        0,
        core::mem::size_of::<RespHdr>(),
    );
    let t = virtio_gpu_submit(
        out,
        out_len as u32,
        core::ptr::addr_of_mut!(RESP) as *mut u8,
        core::mem::size_of::<RespHdr>() as u32,
    );
    if t < 0 {
        kprintf(b"virtio-gpu: submit timeout\n\0".as_ptr());
        return false;
    }
    if (t as u32) != VIRTIO_GPU_RESP_OK_NODATA {
        kprintf(b"virtio-gpu: bad resp\n\0".as_ptr());
        return false;
    }
    true
}

fn px(r: u8, g: u8, b: u8) -> u32 {
    (0xFFu32 << 24) | ((r as u32) << 16) | ((g as u32) << 8) | (b as u32)
}

unsafe fn fb_ptr() -> *mut u32 {
    core::ptr::addr_of_mut!(FB) as *mut u32
}

unsafe fn put_pixel(x: usize, y: usize, colour: u32) {
    if x >= FB_W || y >= FB_H {
        return;
    }
    *fb_ptr().add(y * FB_W + x) = colour;
}

unsafe fn fill_rect(x0: usize, y0: usize, w: usize, h: usize, colour: u32) {
    let mut y = y0;
    while y < y0 + h && y < FB_H {
        let mut x = x0;
        while x < x0 + w && w < FB_W {
            put_pixel(x, y, colour);
            x += 1;
        }
        y += 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn gpu_fb_ptr() -> *mut u8 {
    fb_ptr() as *mut u8
}

#[no_mangle]
pub extern "C" fn gpu_fb_width() -> u32 {
    FB_W as u32
}

#[no_mangle]
pub extern "C" fn gpu_fb_height() -> u32 {
    FB_H as u32
}

#[no_mangle]
pub unsafe extern "C" fn gpu_fb_present() -> i32 {
    let mut t = TransferToHost2d {
        hdr: CtrlHdr {
            type_: 0,
            flags: 0,
            fence_id: 0,
            ctx_id: 0,
            ring_idx: 0,
            padding: [0; 3],
        },
        r: Rect {
            x: 0,
            y: 0,
            width: FB_W as u32,
            height: FB_H as u32,
        },
        offset: 0,
        resource_id: FB_ID,
        padding: 0,
    };
    zero_hdr(&mut t.hdr);
    t.hdr.type_ = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    if !submit_ok(
        &mut t as *mut _ as *mut u8,
        core::mem::size_of::<TransferToHost2d>(),
    ) {
        return -1;
    }

    let mut f = ResourceFlush {
        hdr: CtrlHdr {
            type_: 0,
            flags: 0,
            fence_id: 0,
            ctx_id: 0,
            ring_idx: 0,
            padding: [0; 3],
        },
        r: Rect {
            x: 0,
            y: 0,
            width: FB_W as u32,
            height: FB_H as u32,
        },
        resource_id: FB_ID,
        padding: 0,
    };
    zero_hdr(&mut f.hdr);
    f.hdr.type_ = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    if !submit_ok(
        &mut f as *mut _ as *mut u8,
        core::mem::size_of::<ResourceFlush>(),
    ) {
        return -1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn gpu_fb_init() -> i32 {
    fill_rect(0, 0, FB_W, FB_H, px(0x1a, 0x1a, 0x2e));

    // CREATE_2D
    let mut c = ResourceCreate2d {
        hdr: CtrlHdr {
            type_: 0,
            flags: 0,
            fence_id: 0,
            ctx_id: 0,
            ring_idx: 0,
            padding: [0; 3],
        },
        resource_id: FB_ID,
        format: VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
        width: FB_W as u32,
        height: FB_H as u32,
    };
    zero_hdr(&mut c.hdr);
    c.hdr.type_ = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    kprintf(b"virtio-gpu: CREATE\n\0".as_ptr());
    if !submit_ok(
        &mut c as *mut _ as *mut u8,
        core::mem::size_of::<ResourceCreate2d>(),
    ) {
        return -1;
    }

    // ATTACH_BACKING
    let mut ab = AttachBackingMsg {
        a: ResourceAttachBacking {
            hdr: CtrlHdr {
                type_: 0,
                flags: 0,
                fence_id: 0,
                ctx_id: 0,
                ring_idx: 0,
                padding: [0; 3],
            },
            resource_id: FB_ID,
            nr_entries: 1,
        },
        e: MemEntry {
            addr: kvirt_to_phys(fb_ptr() as *mut u8) & 0x000f_ffff_ffff_ffff,
            length: (FB_W * FB_H * 4) as u32,
            padding: 0,
        },
    };
    zero_hdr(&mut ab.a.hdr);
    ab.a.hdr.type_ = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    kprintf(b"virtio-gpu: ATTACK\n\0".as_ptr());
    if !submit_ok(
        &mut ab as *mut _ as *mut u8,
        core::mem::size_of::<AttachBackingMsg>(),
    ) {
        return -1;
    }

    // SET_SCANOUT
    let mut s = SetScanout {
        hdr: CtrlHdr {
            type_: 0,
            flags: 0,
            fence_id: 0,
            ctx_id: 0,
            ring_idx: 0,
            padding: [0; 3],
        },
        r: Rect {
            x: 0,
            y: 0,
            width: FB_W as u32,
            height: FB_H as u32,
        },
        scanout_id: 0,
        resource_id: FB_ID,
    };
    zero_hdr(&mut s.hdr);
    s.hdr.type_ = VIRTIO_GPU_CMD_SET_SCANOUT;
    kprintf(b"virtio-gpu: SET_SCANOUT\n\0".as_ptr());
    if !submit_ok(
        &mut s as *mut _ as *mut u8,
        core::mem::size_of::<SetScanout>(),
    ) {
        return -1;
    }

    kprintf(b"virtio-gpu: gpu_fb_init ok\n\0".as_ptr());
    0
}

fn draw_demo() {
    unsafe {
        fill_rect(0, 0, FB_W, FB_H, px(0x1a, 0x1a, 0x2e));

        let mut x = 0usize;
        while x < FB_W {
            let t = (x * 255 / FB_W) as u8;
            fill_rect(x, 40, 1, 80, px(t, 80, 255 - t));
            x += 1;
        }

        fill_rect(120, 180, 200, 120, px(0x00, 0xc8, 0x80));

        let mut cy = 320usize;
        while cy < 440 {
            let mut cx = 40usize;
            while cx < 200 {
                let on = ((cx / 16) + (cy / 16)) % 2 == 0;
                put_pixel(
                    cx,
                    cy,
                    if on {
                        px(0xff, 0xff, 0xff)
                    } else {
                        px(0x30, 0x30, 0x30)
                    },
                );
                cx += 1;
            }
            cy += 1;
        }
    }    
}

#[no_mangle]
pub unsafe extern "C" fn virtio_gpu_phase5_rust() -> i32 {
    if gpu_fb_init() != 0 {
        return -1;
    }
    draw_demo();
    if gpu_fb_present() != 0 {
        return -1;
    }
    kprintf(b"virtio-gpu: phase5 ok\n\0".as_ptr());
    0
}
