# Debian 9 (Stretch) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Four binaries extracted from a Debian 9 (Stretch) system:

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.1.6` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.2` | libssl1.0.2 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.1` | libssl1.1 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22` | libstdc++6 |

## Gating Results (check_rdrand_gating.sh)

```
GATED   libgcrypt.so.20.1.6   (1 function)
UNGATED libcrypto.so.1.0.2    (1 function)
UNGATED libcrypto.so.1.1      (1 function)
UNGATED libstdc++.so.6.0.22   (1 function)
```

The three UNGATED results are all heuristic artifacts — the script classifies
a function as UNGATED when it contains RDRAND but not CPUID.  Manual
disassembly confirms all three use cached or caller-level CPUID checks.

---

## Per-Binary Analysis

### libgcrypt.so.20.1.6 — GATED

Function `gcry_is_secure` at `0xd060` contains both CPUID and RDRAND in the
same function body.  Straightforward co-located gate; same pattern seen on
Debian 13.

---

### libcrypto.so.1.0.2 — UNGATED (stripped symbol artifact)

The RDRAND instruction is at `0x7a765`, labeled by objdump as
`OPENSSL_cleanse+0xd5` due to stripped symbols.  The actual function is an
RDRAND asm stub (the RDSEED stub follows at `0x7a785`).

The CPUID check lives in `OPENSSL_init` at `0x7a470` (four CPUID calls at
`0x7a4d8`, `0x7a534`, `0x7a545`, `0x7a55f`), which is the library
initializer that populates `OPENSSL_ia32cap_P`.

The `ENGINE_load_rdrand` function at `0x127940` reads the cached capability
word and tests bit 6 of byte 7 before enabling the RDRAND engine:

```
127940:  lea   0x33fd79(%rip),%rax    # points into OPENSSL_ia32cap_P
127947:  testb $0x40,0x7(%rax)        # test RDRAND bit
12794b:  jne   127950                 # skip (no RDRAND) if bit clear
12794d:  ret
```

**Verdict**: cached CPUID init, same pattern as Debian 13's libcrypto.

---

### libcrypto.so.1.1 — UNGATED (stripped symbol artifact)

RDRAND labeled under `CRYPTO_memcmp+0x...` — same stripped-symbol artifact
seen on Debian 13's libcrypto.so.3.  Gating is through the cached
`OPENSSL_ia32cap_P[]` array populated at library init time.

**Verdict**: cached CPUID init, identical to Debian 13.

---

### libstdc++.so.6.0.22 — UNGATED (caller-gated, Intel-only)

RDRAND appears in `__once_proxy+0x45` (`0xb9165`).  The gating CPUID is in
`_M_init` (`_ZNSt13random_device7_M_initE...`) at `0xb9190`.

The dispatch logic in `_M_init`:

```
b91a7:  call  basic_string::compare("default")
b91ae:  jne   b9208                   # not "default" token, try others
b91b0:  cpuid                         # leaf 0: get vendor string
b91b4:  je    b91be                   # EAX==0: no CPUID support, fallback
b91b6:  cmp   $0x756e6547,%ebx        # "Genu" (Intel)?
b91bc:  je    b91e8                   # yes: check RDRAND bit
        # fall through to /dev/urandom open (AMD not checked)
b91c5:  lea   "/dev/urandom",%rsi
b91cf:  call  fopen
...
b91e8:  mov   $0x1,%eax
b91ed:  cpuid                         # leaf 1: feature flags
b91ef:  and   $0x40000000,%ecx        # ECX bit 30 = RDRAND
b91f5:  je    b91be                   # not supported: /dev/urandom
        # RDRAND supported: set internal pointer to null (RDRAND path)
b91f8:  movq  $0x0,(%r12)
b9203:  ret
```

**Key finding**: Debian 9's libstdc++ 6.0.22 checks only for Intel ("Genu").
On AMD hardware the `cmp $0x756e6547,%ebx` fails and the code falls through
to open `/dev/urandom` — RDRAND is never used.  Later libstdc++ versions
added "Auth" (AMD) detection, making `std::random_device{}` use RDRAND on
both vendors.

This means the rr CPUID interception issue with `std::random_device{}` was
Intel-only on Debian 9 / libstdc++ 6.x.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.1.6 | GATED | Co-located CPUID in `gcry_is_secure` |
| libcrypto.so.1.0.2 | UNGATED | Cached `OPENSSL_ia32cap_P` (same as D13) |
| libcrypto.so.1.1 | UNGATED | Cached `OPENSSL_ia32cap_P` (same as D13) |
| libstdc++.so.6.0.22 | UNGATED | Caller-gated `_M_init` CPUID; **Intel-only** |

All four libraries are effectively gated.  The gating convention holds on
Debian 9, consistent with Debian 13 and Gentoo findings.

## Notable Difference from Debian 13

libstdc++ 6.0.22 (Debian 9) only dispatches to RDRAND on Intel CPUs.  AMD
systems fall back to `/dev/urandom` regardless of RDRAND support.  The
Intel-only restriction was lifted in a later libstdc++ release that added
AMD vendor string detection.  This means the `std::random_device{}`
silent-divergence issue under rr was introduced gradually: Intel systems
were affected from the time RDRAND dispatch was added; AMD systems only
became affected after the AMD check was added.
