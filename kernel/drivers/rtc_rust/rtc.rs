#[repr(C)]
struct Driver {
    name: *const u8,
    init: Option<extern "C" fn() -> i32>,
    priority: i32,
}

unsafe impl Sync for Driver {}

#[inline(always)]
unsafe fn outb(port: u16, val: u8) {
    core::arch::asm!(
        "out dx, al",
        in("dx") port,
        in("al") val,
        options(nostack, preserves_flags)
    );
}

#[inline(always)]
unsafe fn inb(port: u16) -> u8 {
    let val: u8;
    core::arch::asm!(
        "in al, dx",
        out("al") val,
        in("dx") port,
        options(nostack, preserves_flags)
    );
    val
}

unsafe fn cmos_read(reg: u8) -> u8 {
    outb(0x70, reg | 0x80);
    inb(0x71)
}

fn bcd_to_bin(v: u8) -> u8 {
    ((v >> 4) * 10) + (v & 0x0F)
}

#[no_mangle]
pub unsafe extern "C" fn rtc_rust_read(hour: *mut u8, min: *mut u8, sec: *mut u8) {
    while cmos_read(0x0A) & 0x80 != 0 {
        core::hint::spin_loop();
    }

    let mut s = cmos_read(0x00);
    let mut m = cmos_read(0x02);
    let mut h = cmos_read(0x04);
    let b = cmos_read(0x0B);

    if b & 0x04 == 0 {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h & 0x7F);
        if b & 0x02 == 0 {
            let pm = cmos_read(0x04) & 0x80 != 0;
            if h == 12 {
                h = 0;
            }
            if pm {
                h += 12;
            }
        }
    } else {
        h &= 0x7F;
    }

    if !hour.is_null() {
        *hour = h;
    };
    if !min.is_null() {
        *min = m;
    }
    if !sec.is_null() {
        *sec = s;
    }
}

extern "C" {
    fn kprintf(fmt: *const u8, ...);
    fn driver_register(d: *const Driver);
}

#[no_mangle]
pub extern "C" fn rtc_rust_init() {
    let mut h = 0u8;
    let mut m = 0u8;
    let mut s = 0u8;
    unsafe {
        rtc_rust_read(&mut h, &mut m, &mut s);
        kprintf(
            b"rtc_rust: %u:%u:%u (UTC/CMOS)\n\0".as_ptr(),
            h as u32,
            m as u32,
            s as u32,
        );
    }
}

extern "C" fn rtc_driver_init() -> i32 {
    rtc_rust_init();
    0
}

static DRIVER: Driver = Driver {
    name: b"rtc-rust\0".as_ptr(),
    init: Some(rtc_driver_init),
    priority: 20,
};

#[no_mangle]
pub unsafe extern "C" fn rtc_driver_register() {
    driver_register(&DRIVER);
}
