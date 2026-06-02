# Debian 13 (Trixie) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Twenty binaries from a Debian 13 (Trixie) container, scanned via
the artifact's `prevalence/debian13/Dockerfile` (which installs
`task-gnome-desktop` plus `libbotan-2-19` so that all four
cryptographic libraries enumerated in the paper are present).
Notably absent (continuing the trend from D12): the systemd
family, since systemd 252+ replaced `rdrand()` with
`getrandom()`.  New additions over D12: a broader GCC 14
toolchain footprint (collect2, lto-wrapper, gcov-tool-14,
gcov-dump-14, cpp-14, g++-14, gcc-14, and gcov-14 all carry the
statically-linked libstdc++ dispatch) and libbotan-2.

| Path | Package |
|---|---|
| `usr/bin/x86_64-linux-gnu-gcov-tool-14` | gcc-14 |
| `usr/bin/x86_64-linux-gnu-gcov-dump-14` | gcc-14 |
| `usr/bin/x86_64-linux-gnu-cpp-14` | cpp-14 |
| `usr/bin/x86_64-linux-gnu-g++-14` | g++-14 |
| `usr/bin/x86_64-linux-gnu-lto-dump-14` | gcc-14 |
| `usr/bin/x86_64-linux-gnu-gcc-14` | gcc-14 |
| `usr/bin/x86_64-linux-gnu-gcov-14` | gcc-14 |
| `usr/sbin/apparmor_parser` | apparmor |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.3` | libssl3 |
| `usr/lib/x86_64-linux-gnu/ossl-modules/legacy.so` | openssl |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.33` | libstdc++6 |
| `usr/lib/x86_64-linux-gnu/libsodium.so.23.3.0` | libsodium23 |
| `usr/lib/x86_64-linux-gnu/libgcrypt.so.20.5.0` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libbotan-2.so.19.19.5` | libbotan-2-19 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/cc1plus` | g++-14 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/lto-wrapper` | gcc-14 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/lto1` | gcc-14 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/cc1` | gcc-14 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/g++-mapper-server` | g++-14 |
| `usr/libexec/gcc/x86_64-linux-gnu/14/collect2` | gcc-14 |

## Gating Results (check_rdrand_gating.sh)

```
GATED   gcov-tool-14                 (1 function)
GATED   gcov-dump-14                 (1 function)
GATED   cpp-14                       (1 function)
GATED   g++-14                       (1 function)
UNGATED lto-dump-14                  (1 function)
GATED   gcc-14                       (1 function)
GATED   gcov-14                      (1 function)
GATED   apparmor_parser              (1 function)
UNGATED libcrypto.so.3               (1 function)
GATED   ossl-modules/legacy.so       (1 function)
UNGATED libstdc++.so.6.0.33          (1 function)
UNGATED libsodium.so.23.3.0          (1 function)
UNGATED libbotan-2.so.19.19.5        (2 functions)
GATED   libgcrypt.so.20.5.0          (1 function)
UNGATED cc1plus                      (1 function)
GATED   lto-wrapper                  (1 function)
UNGATED lto1                         (1 function)
UNGATED cc1                          (1 function)
GATED   g++-mapper-server            (1 function)
GATED   collect2                     (1 function)
```

Twelve GATED, eight UNGATED.  As in every prior release,
the UNGATED labels are heuristic artifacts of stripped-symbol
layout; manual disassembly confirms all 20 binaries use
cached or caller-level CPUID checks.

---

## Per-Binary Analysis

### libgcrypt.so.20.5.0 — GATED

`gcry_is_secure` at `0x13370`; co-located CPUID in the same
function.  Unchanged from all prior releases.

---

### libcrypto.so.3 — UNGATED (stripped symbol artifact)

Same `CRYPTO_memcmp@@OPENSSL_3.0.0` stripped-symbol artifact as
D9–D12.  Gating is via the cached `OPENSSL_ia32cap_P[]` array
populated at library init.  RDRAND retry-loop stub patterns are
identical to libcrypto 1.0.2, 1.1, and 3 in earlier releases.

---

### ossl-modules/legacy.so — GATED

Same `OSSL_provider_init@@Base-0xb50` stripped-symbol region as
D12.  Embeds its own CPUID init and RDRAND asm stubs; the
heuristic correctly labels the binary GATED because both the
cached `ia32cap` init and the RDRAND stubs fall within the same
stripped `.text` region.

---

### libsodium.so.23.3.0 — UNGATED (cached global flag)

Identical to D10/D11/D12.  Library initializer runs CPUID,
`sodium_runtime_has_rdrand()` returns the cached flag, RDRAND
callers test it before use.  Symbol attribution is
`crypto_aead_aes256gcm_keygen@@Base` due to layout.

---

### libbotan-2.so.19.19.5 — UNGATED (caller-side singleton)

Two RDRAND-containing functions:

1. `Botan::Processor_RNG::reseed(Botan::Entropy_Sources&, ...)`
   at `0x3960d0` — contains a `rdrand %rdx` retry loop with no
   in-function CPUID:
   ```
   3960fa:  mov    $0xa,%ecx          # 10 retries
   396104:  rdrand %rdx
   396108:  adc    $0x0,%eax
   39610b:  cmp    $0x1,%eax
   39610e:  je     396140             # success
   396110:  sub    $0x1,%rcx
   396114:  jne    3960ff             # retry
   ```
2. A second RDRAND site attributed by the stripped symbol table
   to `std::deque<...>::_M_push_back_aux` — almost certainly the
   `Botan::Processor_RNG::randomize` body (at `0x396170`,
   immediately adjacent) reusing the same RDRAND asm pattern.

Both sites are reached only via `Botan::Processor_RNG` objects,
which are constructed only after a successful
`Botan::CPUID::has_rdrand()` check (the cached CPUID singleton
described in the paper).  This is the "CPUID::state() singleton"
pattern Table 2 of the paper attributes to libbotan.  Under rr
with CPUID faulting, `Botan::CPUID::has_rdrand()` returns false
and `Processor_RNG` is never instantiated; the RDRAND stubs are
never reached.

---

### libstdc++.so.6.0.33 — UNGATED (caller-gated; Intel and AMD)

RDRAND at `__once_proxy@@GLIBCXX_3.4.11` (`0xdfc60`).  CPUID
dispatch in `_M_init`.  Structurally identical to libstdc++
6.0.28 (D11) and 6.0.30 (D12): vendor check ("AuthenticAMD" or
"GenuineIntel"), leaf 7 RDSEED test, leaf 1 RDRAND test,
function-pointer dispatch via `cmove`.

---

### GCC 14 compiler binaries — UNGATED heuristic / caller-gated actual

`lto-dump-14`, `cc1plus`, `lto1`, and `cc1` are classified
UNGATED because the RDRAND instruction sits within the
statically-linked libstdc++ `_M_init_pretr1` symbol region
while the CPUID dispatch lives in `_M_initERKNSt`, the same
split documented for D12.  Actual structure is identical to
the cc1 disassembly in `debian12.md`: vendor check, leaf 7
RDSEED bit test, leaf 1 RDRAND bit test, function-pointer
selection via `cmove`.

`gcov-tool-14`, `gcov-dump-14`, `cpp-14`, `g++-14`, `gcc-14`,
`gcov-14`, `lto-wrapper`, `g++-mapper-server`, and `collect2`
are classified GATED.  All carry the same statically-linked
libstdc++ dispatch; the heuristic GATED/UNGATED split depends
only on whether stripping happened to place the CPUID
initialization code in the same `_obstack_newchunk@@Base`
region as the RDRAND stubs.  GCC 14 ships notably more
binaries with this footprint than GCC 12 did (D12), so the
toolchain-derived count grows from 8 (D12) to 13 (D13).

---

### apparmor\_parser — GATED

RDRAND/RDSEED stubs and CPUID initialization fall within the
same stripped `.text` region; statically-linked libstdc++
copy.  Same caller-gated pattern as D11/D12.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.5.0 | GATED | Co-located CPUID in `gcry_is_secure` |
| libcrypto.so.3 | UNGATED | Cached `OPENSSL_ia32cap_P` |
| ossl-modules/legacy.so | GATED | Cached `ia32cap` init (co-located by symbol layout) |
| libsodium.so.23.3.0 | UNGATED | Cached global flag from `_init` CPUID |
| libbotan-2.so.19.19.5 | UNGATED | Caller-side singleton via `Botan::CPUID::has_rdrand()` |
| libstdc++.so.6.0.33 | UNGATED | Caller-gated `_M_init`; Intel and AMD |
| cc1, cc1plus, lto1, lto-dump-14 | UNGATED | Statically-linked libstdc++; CPUID in adjacent symbol |
| gcov-tool-14, gcov-dump-14, cpp-14, g++-14, gcc-14, gcov-14, lto-wrapper, g++-mapper-server, collect2 | GATED | Statically-linked libstdc++; CPUID co-located by symbol layout |
| apparmor\_parser | GATED | Statically-linked libstdc++; CPUID co-located by symbol layout |

All 20 binaries are effectively gated.

## Notable Findings

**All four crypto libraries reproduce in-container**: with
`libbotan-2-19` added to the Dockerfile alongside `libssl3`,
`libgcrypt20`, and `libsodium23`, all four crypto libraries
enumerated in Table 2 of the paper appear in the scan
without needing a host install.  The libbotan dispatch
("singleton CPUID lookup", caller-side) matches the row
attributed to it in the paper.

**GCC 14 toolchain breadth**: D13 carries 13 RDRAND-bearing
GCC binaries to D12's 8.  All trace back to the same
statically-linked libstdc++ dispatch; the count grows because
more GCC frontend/utility binaries now link the full C++
runtime.

**Systemd remains absent**: continuing the D12 pattern,
systemd 257 (Trixie) does not use `rdrand()`.  The systemd
family does not appear in the scan on D12 or D13.

**libstdc++ 6.0.33** retains the unchanged Intel+AMD
detection introduced in 6.0.28 (D11); no behavioral change
in 6.0.30 → 6.0.33.

**Container scan totals 4,447 ELF binaries**, of which 20
contain RDRAND/RDSEED.  This is the headline number the
paper now reports (replacing the 6,771 / 30 figures derived
from a host install that additionally included
Electron-based applications; the artifact intentionally does
not replicate those, which are install-dependent and not
load-bearing for any claim in the paper).
