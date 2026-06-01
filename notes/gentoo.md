# Gentoo RDRAND Prevalence Analysis

## Setup

- **Distribution:** Gentoo Linux (Docker container)
- **Architecture:** x86_64, Haswell target
- **Build flags:** `-march=native` via `emerge -e @world` (full world rebuild)
- **Tools:** `scan_rdrand.sh` and `check_rdrand_gating.sh` (same as Debian analysis)

## Scan Results

- **Binaries scanned:** 1,940
- **Containing RDRAND/RDSEED:** 22 (1.13%)

### Comparison with Debian 13

| Metric                     | Debian 13    | Gentoo (Haswell) |
|----------------------------|--------------|------------------|
| Binaries scanned           | 6,771        | 1,940            |
| Containing RDRAND/RDSEED   | 30 (0.44%)   | 22 (1.13%)       |
| GATED (co-located CPUID)   | 16           | 13               |
| UNGATED (caller-gated)     | 14           | 9                |

Prevalence rate is 2.5x higher on the source distribution compiled
with `-march=native`.

## Binaries Found

### Crypto Libraries (3)
| Binary                  | Gating   | Mechanism                         |
|-------------------------|----------|-----------------------------------|
| libcrypto.so.3          | UNGATED  | `OPENSSL_ia32_cpuid` cached setup |
| libgcrypt.so.20.6.0     | GATED    | Co-located CPUID                  |
| libbotan-3.so.10.10.0   | UNGATED  | `CPUID::state()` singleton        |

Note: libsodium not present in this Gentoo install.

### OpenSSL Module (1)
| Binary                        | Gating | Mechanism          |
|-------------------------------|--------|--------------------|
| ossl-modules/legacy.so        | GATED  | Co-located CPUID   |

### GCC Toolchain — Statically Linked libstdc++ (16)
All contain the same libstdc++ `_M_init` random-device dispatch pattern
(1 RDRAND + 4 RDSEED + many CPUID).

| Binary                                | Gating   |
|----------------------------------------|----------|
| libstdc++.so.6.0.34 (32-bit)          | UNGATED  |
| libstdc++.so.6.0.34 (64-bit)          | UNGATED  |
| cc1                                    | UNGATED  |
| cc1plus                               | UNGATED  |
| f951 (Fortran)                         | UNGATED  |
| lto1                                   | UNGATED  |
| lto-dump                              | UNGATED  |
| lto-wrapper                           | GATED    |
| g++-mapper-server                     | GATED    |
| collect2                               | GATED    |
| x86_64-pc-linux-gnu-g++               | GATED    |
| x86_64-pc-linux-gnu-gcc               | GATED    |
| x86_64-pc-linux-gnu-gfortran          | GATED    |
| x86_64-pc-linux-gnu-c++               | GATED    |
| x86_64-pc-linux-gnu-cpp               | GATED    |
| gcov-dump                             | GATED    |
| x86_64-pc-linux-gnu-gcov              | GATED    |
| gcov-tool                             | GATED    |

### libstdc++ Shared Library (2)
The 32-bit and 64-bit shared libstdc++ are listed above under GCC toolchain.

## Gating Analysis

### GATED (13 binaries)
Co-located CPUID in the same function as RDRAND/RDSEED. These are either:
- GCC driver/tool binaries where the statically linked libstdc++ code
  happens to include CPUID in the same function
- libgcrypt with its own co-located CPUID
- OpenSSL legacy module with co-located CPUID

### UNGATED (9 binaries)
All nine use caller-level CPUID gating — same patterns as Debian.
Verified by disassembly of the extracted binaries in
`position/gentoo/usr/`.

1. **libstdc++ `_M_init_pretr1`** (7 binaries: libstdc++ 32/64,
   cc1, cc1plus, f951, lto1, lto-dump):
   - Shared library: RDRAND in unnamed function near `__once_proxy`
     (offset `e5d90`). RDSEED at `e5de8`.
   - Statically linked GCC binaries: RDRAND in
     `_ZNSt13random_device14_M_init_pretr1ERKSs` (demangled:
     `std::random_device::_M_init_pretr1(std::string const&)`).
     Confirmed in cc1, cc1plus, f951, lto1, lto-dump — all
     the same function, all at different link addresses.
   - None of the statically linked GCC binaries export
     `GLIBCXX` dynamic symbols (verified with `objdump -T`),
     confirming static linkage.
   - **Gating:** The caller `_M_init` (at `e6250`) performs an
     **inline CPUID check** (not cached) at each `random_device`
     construction. See "libstdc++ Default Token Dispatch" below.

2. **OpenSSL `OPENSSL_ia32_rdrand_bytes`** (1 binary:
   libcrypto.so.3):
   - RDRAND at `1b6514` (`rdrand %r10`), RDSEED at `1b6584`
     (`rdseed %r10`). Both in asm stubs that objdump labels
     as `CRYPTO_memcmp+0x1a0` / `+0x224` due to stripped symbols.
   - Stubs are called from the RAND provider (at `146b1f`),
     which checks `OPENSSL_ia32cap_P` cached flags before calling.
   - **Gating:** `OPENSSL_ia32_cpuid` (asm probe near
     `OPENSSL_issetugid`) runs at init, stores results in
     `OPENSSL_ia32cap_P[]`. Provider-model cached init.

3. **Botan `CPUID::state()` singleton** (1 binary:
   libbotan-3.so):
   - RDSEED at `24bf9b` (`rdseed %ebx`) in
     `Botan::Entropy_Sources` destructor area. References
     `g_cpuid` singleton
     (`_ZGVZN5Botan5CPUID5stateEvE7g_cpuid`).
   - RDRAND at `54ba19` (`rdrand %rdx`) in
     `Botan::Processor_RNG::reseed`. Also references `g_cpuid`.
   - **Gating:** `CPUID::state()` is a singleton initialized
     once via `CPUID::initialize()` (at `60da60`), which calls
     `CPUID_Data::detect_cpu_features()`. Callers check cached
     feature bits before invoking RDRAND/RDSEED wrappers.

## libstdc++ Default Token Dispatch

Disassembly of `_M_init` (`_ZNSt13random_device7_M_initERKN
St7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE`, at `e6250`)
reveals the dispatch logic for the `"default"` token (length 7):

1. Check CPUID leaf 0 for vendor string ("Genu"=Intel or
   "Auth"=AMD).
2. If recognized vendor, execute CPUID leaf 1, test ECX bit 30
   (RDRAND feature flag).
3. If RDRAND supported: set the generator function pointer to the
   RDRAND function (`e5d70`, which calls `rdrand %eax`).
4. If RDRAND not supported: set the function pointer to
   `arc4random` (`e5d60`).

**This means every C++ program using `std::random_device{}` (the
default constructor) on this libstdc++ will execute RDRAND on
modern Intel/AMD hardware.** The application code does not
explicitly request RDRAND — libstdc++ selects it automatically
based on CPUID.

The explicit tokens also perform inline CPUID checks:
- `"rdrand"` (length 6): checks CPUID leaf 1 ECX bit 30; throws
  `std::runtime_error` if unsupported.
- `"rdseed"` (length 6): checks CPUID leaf 7 EBX bit 18; throws
  if unsupported.
- `"hardware"` (length 8): same as `"rdrand"`.

### Implications for rr

rr intercepts CPUID and clears the RDRAND feature bit. This causes
`_M_init` to take the `arc4random`/`getentropy` fallback path,
so RDRAND is never executed during recording or replay. The program
silently uses a completely different entropy source under rr than
it would natively.

This is correct for replay fidelity (the fallback path is
deterministic under rr's syscall interception), but it means the
recorded execution does not reflect the program's native behavior.
An analyst examining a program's entropy usage under rr would see
`getentropy` syscalls where the native execution used RDRAND — a
behavioral difference that could matter for forensic analysis of
cryptographic operations.

### Test Program

`exp7_stdrand.cpp` exercises all three paths:
1. Default constructor — uses RDRAND natively, `arc4random` under rr
2. `"rdrand"` token — uses RDRAND natively, throws under rr
3. `"rdseed"` token — uses RDSEED natively, throws under rr

```
g++ -std=c++17 -O2 -o exp7_stdrand exp7_stdrand.cpp
./exp7_stdrand              # native: all three succeed
rr record ./exp7_stdrand    # rr: default succeeds (fallback),
                            #     rdrand/rdseed throw
```

## Key Observations

1. **Higher prevalence on source distributions:** 1.13% vs 0.44%,
   driven by GCC toolchain binaries statically linking libstdc++.

2. **Same gating patterns:** All UNGATED binaries use identical
   caller-gated mechanisms found in the Debian scan. Verified by
   disassembly: libstdc++ `_M_init_pretr1`, OpenSSL
   `OPENSSL_ia32_rdrand_bytes`, Botan `CPUID::state()` singleton.

3. **libstdc++ default token selects RDRAND automatically:** On
   GCC 15 libstdc++, `std::random_device{}` checks CPUID at
   construction and uses RDRAND if the CPU supports it. Every C++
   program using the default `random_device` on modern hardware
   executes RDRAND — without the programmer requesting it.

4. **No new RDRAND from `-march=native` compiler flags:** Despite
   enabling `-mrdrnd` at the compiler level, no application code
   gained RDRAND through compiler intrinsic use. All RDRAND comes
   through library code (libstdc++ random_device, crypto libraries).

5. **Convention consistency:** The "convention not guarantee" argument
   holds — all RDRAND is gated by voluntary CPUID checks, not
   architectural enforcement. rr handles all of these correctly
   today by intercepting CPUID, but this safety is fragile.
