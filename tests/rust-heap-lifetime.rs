// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
use std::thread;
#[cfg(ferail_allocator)]
#[global_allocator]
static ALLOCATOR: aros_alloc::ArosAlloc = aros_alloc::ArosAlloc::new();
unsafe extern "C" { fn kprintf(format: *const u8, ...); }
fn main() {
    for cycle in 1..=32_u8 {
      for bytes in [64, 8192] {
        let value = thread::spawn(move || vec![cycle; bytes]).join().unwrap();
        let churn = vec![0x5a_u8; 16384];
        assert!(value.iter().all(|&byte| byte == cycle));
        drop(value);
        drop(churn);
        let value = vec![0xa5_u8; bytes];
        thread::spawn(move || {
            assert!(value.iter().all(|&byte| byte == 0xa5));
            drop(value);
        }).join().unwrap();
    }
      }
    unsafe { kprintf(c"[VM] Rust heap survives worker exit PASS cycles=32\n".as_ptr().cast()); }
}
