/// Experiment 6: Rust `target-feature=+rdrand` demonstration
///
/// Demonstrates that `-C target-feature=+rdrand` causes the
/// compiler to emit RDRAND without a runtime CPUID check.
///
/// This mirrors the pattern used by the `getrandom` crate on
/// `no_std` x86 targets and by `ring` on SGX: when the target
/// feature is set, `cfg!(target_feature = "rdrand")` is true
/// at compile time, so no runtime CPUID check is emitted.
///
/// Build and compare disassembly:
///
///   # Default: runtime CPUID check before RDRAND
///   cargo build --release
///   objdump -d target/release/exp_rust | grep -E 'cpuid|rdrand'
///
///   # With target-feature: RDRAND emitted directly, no CPUID
///   RUSTFLAGS="-C target-feature=+rdrand" cargo build --release
///   objdump -d target/release/exp_rust | grep -E 'cpuid|rdrand'
use std::arch::x86_64::{__cpuid, _rdrand64_step};

/// When compiled with `-C target-feature=+rdrand`, this check
/// is always true at compile time and the CPUID call is
/// eliminated by the optimizer.  Without the flag, the
/// compiler emits a runtime CPUID check.
fn has_rdrand() -> bool {
    #[cfg(target_feature = "rdrand")]
    {
        true
    }
    #[cfg(not(target_feature = "rdrand"))]
    {
        let result = unsafe { __cpuid(1) };
        (result.ecx & (1 << 30)) != 0
    }
}

fn get_rdrand() -> Option<u64> {
    if !has_rdrand() {
        return None;
    }
    let mut val: u64 = 0;
    let ok = unsafe { _rdrand64_step(&mut val) };
    if ok == 1 {
        Some(val)
    } else {
        None
    }
}

fn main() {
    if cfg!(target_feature = "rdrand") {
        eprintln!("mode: target-feature=+rdrand (no CPUID check)");
    } else {
        eprintln!("mode: default (runtime CPUID check)");
    }

    for i in 0..4 {
        match get_rdrand() {
            Some(v) => eprintln!("val[{}] = 0x{:016x}", i, v),
            None => eprintln!("val[{}] = UNAVAILABLE", i),
        }
    }
}
