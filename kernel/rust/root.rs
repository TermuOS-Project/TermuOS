#![no_std]
#![no_main]

mod panic;

#[path = "../drivers/rtc_rust/rtc.rs"]
mod rtc_rust;

#[path = "../lib/crc32.rs"]
mod crc32;

#[path = "../lib/rdrand.rs"]
mod rdrand;

#[path = "../drivers/gpu/virtio_gpu.rs"]
mod virtio_gpu;

#[path = "../drivers/gpu/virtio_gpu_phase5.rs"]
mod virtio_gpu_phase5;
