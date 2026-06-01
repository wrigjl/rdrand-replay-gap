# Debian 8 (Jessie) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Three binaries extracted from a Debian 8 (Jessie) system:

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.0.3` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0` | libssl1.0.0 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.20` | libstdc++6 |

Note: no libssl1.1 on Debian 8 (OpenSSL 1.1 was not packaged until Stretch).

## Gating Results (check_rdrand_gating.sh)

```
GATED   libgcrypt.so.20.0.3   (1 function)
UNGATED libcrypto.so.1.0.0    (1 function)
UNGATED libstdc++.so.6.0.20   (1 function)
```

Both UNGATED results are heuristic artifacts.  Manual disassembly confirms
all three libraries use cached or caller-level CPUID checks.

---

## Per-Binary Analysis

### libgcrypt.so.20.0.3 — GATED

Function `gcry_is_secure` at `0xade0` contains both CPUID and RDRAND in the
same function body.  Co-located gate; consistent with D9 and D13.

---

### libcrypto.so.1.0.0 — UNGATED (stripped symbol artifact)

RDRAND at `0x75b05`, labeled by objdump as `OPENSSL_cleanse+0xd5` due to
stripped symbols.  The actual function is the same RDRAND asm stub pattern
seen in libcrypto 1.0.2 (D9) and libcrypto 3 (D13): a retry loop using the
CF flag.

CPUID lives in `OPENSSL_init` at `0x75870` (four CPUID calls at `0x758c5`,
`0x75921`, `0x75932`, `0x7594c`), populating the cached `OPENSSL_ia32cap_P`
array.  `ENGINE_load_rdrand` at `0xf4180` reads the cached word and tests
the RDRAND bit before enabling the engine:

```
f4180:  lea   0x308a59(%rip),%rax    # points into OPENSSL_ia32cap_P
f4187:  testb $0x40,0x7(%rax)        # test RDRAND bit
f418b:  jne   f4190                  # skip (no RDRAND) if bit clear
f418d:  ret
```

Identical pattern to libcrypto 1.0.2 (D9) and libcrypto 3 (D13).

**Verdict**: cached CPUID init via `OPENSSL_init`.

---

### libstdc++.so.6.0.20 — UNGATED (caller-gated, Intel-only)

RDRAND at `__once_proxy+0x45` (`0xb5b85`).  The CPUID gate is in `_M_init`
(`_ZNSt13random_device7_M_initERKSs`) at `0xb5bb0`.

Dispatch logic in `_M_init`:

```
b5bc7:  call  basic_string::compare("default")
b5bce:  jne   b5c08                  # not "default" token, try others
b5bd0:  cpuid                        # leaf 0: get vendor string
b5bd4:  je    b5bde                  # EAX==0: no CPUID, fallback
b5bd6:  cmp   $0x756e6547,%ebx       # "Genu" (Intel)?
b5bdc:  je    b5c40                  # yes: check RDRAND bit
        # fall through to /dev/urandom (AMD not checked)
b5bef:  call  fopen                  # open /dev/urandom
...
b5c40:  mov   $0x1,%eax
b5c45:  cpuid                        # leaf 1: feature flags
b5c47:  and   $0x40000000,%ecx       # ECX bit 30 = RDRAND
b5c4d:  je    b5bde                  # not supported: /dev/urandom
        # RDRAND supported: set pointer to null (RDRAND path)
b5c4f:  movq  $0x0,0x0(%rbp)
b5c5b:  ret
```

Identical Intel-only check to libstdc++ 6.0.22 (D9): `cmp $0x756e6547,%ebx`
with no AMD ("Auth") branch.  On AMD hardware `std::random_device{}`
falls back to `/dev/urandom`.

Also present: `_M_init_pretr1` at `0xb5c60`.  This is a software Mersenne
Twister seeder (pre-TR1 PRNG path); it contains no RDRAND and is not
relevant to the gating analysis.

**Verdict**: caller-gated via `_M_init` CPUID; Intel-only on this version.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.0.3 | GATED | Co-located CPUID in `gcry_is_secure` |
| libcrypto.so.1.0.0 | UNGATED | Cached `OPENSSL_ia32cap_P` (same as D9, D13) |
| libstdc++.so.6.0.20 | UNGATED | Caller-gated `_M_init` CPUID; **Intel-only** |

All three libraries are effectively gated.  The gating convention holds on
Debian 8.

## Consistency Across Releases

The Intel-only `std::random_device{}` dispatch is present in both Debian 8
(libstdc++ 6.0.20) and Debian 9 (libstdc++ 6.0.22), confirming this was a
stable characteristic of libstdc++ 6.x rather than a version-specific
quirk.  AMD vendor detection ("Auth") was added in a later libstdc++ release.

OpenSSL's `OPENSSL_init` + cached `ia32cap_P` + `ENGINE_load_rdrand` gate
pattern is identical across libcrypto 1.0.0 (D8), 1.0.2 (D9), and 3 (D13),
confirming the pattern has been stable across the entire OpenSSL 1.x/3.x
lineage covered by these Debian releases.
