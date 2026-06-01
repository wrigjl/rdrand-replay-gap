# Debian 12 (Bookworm) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Fourteen binaries extracted from a Debian 12 (Bookworm) system.  Notably
absent: systemd family (systemd 252+ removed the `rdrand()` function,
replacing it with `getrandom()`).  New additions: GCC 12 compiler binaries
with exposed libstdc++ symbols, and the OpenSSL legacy provider module.

| Path | Package |
|---|---|
| `usr/bin/x86_64-linux-gnu-dwp` | binutils-dev |
| `usr/bin/x86_64-linux-gnu-gcov-12` | gcc-12 |
| `usr/bin/x86_64-linux-gnu-ld.gold` | binutils |
| `usr/bin/x86_64-linux-gnu-lto-dump-12` | gcc-12 |
| `usr/lib/gcc/x86_64-linux-gnu/12/cc1` | gcc-12 |
| `usr/lib/gcc/x86_64-linux-gnu/12/cc1plus` | g++-12 |
| `usr/lib/gcc/x86_64-linux-gnu/12/g++-mapper-server` | g++-12 |
| `usr/lib/gcc/x86_64-linux-gnu/12/lto1` | gcc-12 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.3` | libssl3 |
| `usr/lib/x86_64-linux-gnu/libgcrypt.so.20.4.1` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libsodium.so.23.3.0` | libsodium23 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30` | libstdc++6 |
| `usr/lib/x86_64-linux-gnu/ossl-modules/legacy.so` | openssl |
| `usr/sbin/apparmor_parser` | apparmor |

## Gating Results (check_rdrand_gating.sh)

```
GATED   x86_64-linux-gnu-dwp         (1 function)
GATED   x86_64-linux-gnu-gcov-12     (1 function)
GATED   x86_64-linux-gnu-ld.gold     (1 function)
UNGATED x86_64-linux-gnu-lto-dump-12 (1 function)
UNGATED cc1                          (1 function)
UNGATED cc1plus                      (1 function)
GATED   g++-mapper-server            (1 function)
UNGATED lto1                         (1 function)
UNGATED libcrypto.so.3               (1 function)
GATED   libgcrypt.so.20.4.1          (1 function)
UNGATED libsodium.so.23.3.0          (1 function)
UNGATED libstdc++.so.6.0.30          (1 function)
GATED   ossl-modules/legacy.so       (1 function)
GATED   usr/sbin/apparmor_parser     (1 function)
```

Seven UNGATED, seven GATED.  All UNGATED results are heuristic artifacts;
manual disassembly confirms all 14 binaries use cached or caller-level
CPUID checks.

---

## Per-Binary Analysis

### libgcrypt.so.20.4.1 — GATED

`gcry_is_secure` at `0x12010`; co-located CPUID.  Unchanged from all prior
releases.

---

### libcrypto.so.3 — UNGATED (stripped symbol artifact)

Same `CRYPTO_memcmp` stripped-symbol artifact as D9, D10, D11.  Gating is
via the cached `OPENSSL_ia32cap_P[]` array populated at library init.

---

### ossl-modules/legacy.so — GATED (same stripped-symbol region)

The OpenSSL legacy provider module embeds its own copy of the OpenSSL
CPUID init and RDRAND asm stubs.  Because symbol stripping leaves both the
CPUID initialization code and the RDRAND stubs under the same large
stripped-symbol region (`OSSL_provider_init@@Base-0xa00`), the heuristic
correctly labels it GATED.  The pattern is identical to libcrypto's cached
`ia32cap` init; the co-location is a coincidence of how this module's
symbol table was stripped relative to libcrypto's.

---

### libsodium.so.23.3.0 — UNGATED (cached global flag)

Identical pattern to D10 and D11: library initializer runs CPUID,
`sodium_runtime_has_rdrand()` returns the cached flag, and RDRAND callers
test it before use.

---

### libstdc++.so.6.0.30 — UNGATED (caller-gated; Intel and AMD)

RDRAND at `__once_proxy+0xc0` (`0xd32c0`).  CPUID gate in `_M_init` at
`0xd3410`.

The dispatch logic is structurally identical to libstdc++ 6.0.28 (D11):

```
d3451:  cpuid                        # leaf 0
d3462:  je    d34b6                  # EAX==0: no CPUID, fallback
d3464:  cmp   $0x68747541,%ebx       # "Auth" (AMD)?
d346a:  je    d3474                  # yes
d346c:  cmp   $0x756e6547,%ebx       # "Genu" (Intel)?
d3472:  jne   d34b6                  # neither: fallback
d3474:  mov   $0x7,%eax
d347b:  cpuid                        # leaf 7 subleaf 0
d347d:  and   $0x40000,%ebx          # test RDSEED bit (EBX bit 18)
d3483:  je    d34b0                  # no RDSEED: fallback
d3485:  mov   $0x1,%eax
d348a:  cpuid                        # leaf 1
d3493:  and   $0x40000000,%ecx       # test RDRAND bit
d34a0:  cmove %rdx,%rax              # select entropy function
```

Both Intel and AMD are checked, unchanged from 6.0.28.

---

### GCC compiler binaries — UNGATED heuristic / caller-gated actual

`lto-dump-12`, `cc1`, `cc1plus`, and `lto1` are all classified UNGATED
because the RDRAND instruction sits within the `_M_init_pretr1` symbol
region while the CPUID dispatch lives in `_M_initERKNSt` (a different
symbol).  The two functions share the `_M_init_pretr1` address space due to
the stripped symbol layout of the statically-linked libstdc++.

The actual structure in cc1 (`_M_initERKNSt` at `0x19e6800`):

```
19e6841:  cpuid                      # leaf 0
19e6854:  cmp   $0x68747541,%ebx     # "Auth" (AMD)?
19e685a:  je    19e6864
19e685c:  cmp   $0x756e6547,%ebx     # "Genu" (Intel)?
19e6862:  jne   19e68a6              # neither: fallback
19e6864:  mov   $0x7,%eax
19e686b:  cpuid                      # leaf 7 (RDSEED check)
19e686d:  and   $0x40000,%ebx
19e6873:  je    19e68a0              # no RDSEED: check getentropy/arc4random
19e6875:  mov   $0x1,%eax
19e687a:  cpuid                      # leaf 1 (RDRAND check)
19e6883:  and   $0x40000000,%ecx
19e6890:  cmove %rdx,%rax            # select entropy function pointer
```

The RDRAND stubs at `0x19e6660` and RDSEED stubs at `0x19e66b0` are
selected by setting a function pointer.  Under rr with CPUID faulting,
`_M_initERKNSt` intercepts CPUID, finds no RDRAND/RDSEED support, and
selects the `getentropy` or `arc4random` stub instead.  The RDRAND stubs
are never called.

`dwp`, `ld.gold`, `gcov-12`, and `g++-mapper-server` are classified GATED
because in those binaries the stripped symbol layout happens to group the
CPUID init code in the same region as the RDRAND stubs.

This is the same pattern documented in the Gentoo scan for GCC toolchain
binaries with statically-linked libstdc++.

---

### apparmor\_parser — GATED

RDRAND/RDSEED stubs and CPUID initialization fall within the same stripped
`.text` region.  Statically-linked libstdc++ copy; same caller-gated
pattern as D11, but symbol layout produces a GATED classification here.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.4.1 | GATED | Co-located CPUID in `gcry_is_secure` |
| libcrypto.so.3 | UNGATED | Cached `OPENSSL_ia32cap_P` |
| ossl-modules/legacy.so | GATED | Cached `ia32cap` init (co-located by symbol layout) |
| libsodium.so.23.3.0 | UNGATED | Cached global flag from `_init` CPUID |
| libstdc++.so.6.0.30 | UNGATED | Caller-gated `_M_init`; Intel and AMD |
| cc1, cc1plus, lto1, lto-dump-12 | UNGATED | Caller-gated `_M_initERKNSt`; RDRAND stubs in adjacent symbol |
| dwp, ld.gold, gcov-12, g++-mapper-server | GATED | Statically-linked libstdc++; CPUID co-located by symbol layout |
| apparmor\_parser | GATED | Statically-linked libstdc++; CPUID co-located by symbol layout |

All 14 binaries are effectively gated.

## Notable Findings

**Systemd absent**: systemd 252 (Debian 12) removed the `rdrand()` function
entirely, replacing hardware entropy calls with `getrandom()`.  This
eliminates the AMD CPUID-faulting gap issue for the systemd family.

**GCC compiler binaries expose `_M_init_pretr1`**: In prior Debian releases
the statically-linked libstdc++ dispatch was hidden under opaque stripped
symbol names.  In Debian 12 the `_M_init_pretr1` and `_M_initERKNSt`
symbols are retained, making the CPUID/RDRAND split visible.  The gating is
identical to prior releases but the symbol layout causes the heuristic to
split GATED and UNGATED classifications across the GCC binaries depending
on whether CPUID and RDRAND happen to share the same symbol region.

**libstdc++ 6.0.30** retains full Intel and AMD detection; no change from
6.0.28 in approach or behavior.

**ossl-modules/legacy.so** is a new binary category: an OpenSSL provider
module that embeds its own CPUID+RDRAND code, similar to a statically-linked
libcrypto.  The cached `ia32cap` pattern is identical to libcrypto.so.3.
