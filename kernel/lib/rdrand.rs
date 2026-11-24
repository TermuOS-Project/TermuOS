#[inline]
fn has_rdrand() -> bool {
    #[cfg(target_arch = "x86_64")]
    {
        let r =  core::arch::x86_64::__cpuid(1);
        (r.ecx & (1 << 30)) != 0
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        false
    }
}

#[inline]
unsafe fn rdrand64(out: *mut u64) -> bool {
    let mut val: u64;
    let mut ok: u8;
    // CF=1 on success; setc captures that
    core::arch::asm!(
        "rdrand {val}",
        "setc {ok}",
        val = out(reg) val,
        ok = out(reg_byte) ok,
        options(nostack, preserves_flags),
    );
    if ok != 0 {
        *out = val;
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn rdrand_fill(buf: *mut u8, len: usize) -> i32 {
    if buf.is_null() || len == 0 {
        return -1;
    }

    let mut filled = 0usize;

    if has_rdrand() {
        while filled < len {
            let mut q: u64 = 0;
            let mut tries = 0;
            while tries < 10 && !rdrand64(&mut q) {
                tries += 1;
            }
            if tries == 10 {
                break;
            }
            let bytes = q.to_le_bytes();
            let n = core::cmp::min(8, len - filled);
            for i in 0..n {
                *buf.add(filled + i) = bytes[i];
            }
            filled += n;
        }
    }

    if filled < len {
        let mut state = 0xA5A5_1234_C3C3_u64.wrapping_mul(len as u64 | 1);
        while filled < len {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            *buf.add(filled) = (state >> 33) as u8;
            filled += 1;
        }
        return 0;
    }
    1
}

extern "C" {
    fn kprintf(fmt: *const u8, ...);
}

#[no_mangle]
pub extern "C" fn rdrand_selftest() {
    let mut buf = [0u8; 16];
    let rc = unsafe { rdrand_fill(buf.as_mut_ptr(), buf.len()) };
    unsafe {
        kprintf(
            b"rdrand: fill rc=%d bytes %x %x %x %x\n\0".as_ptr(),
            rc as i32,
            buf[0] as u32,
            buf[1] as u32,
            buf[2] as u32,
            buf[3] as u32,
        );
    }
}
