# Debian 8 (Jessie) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Four binaries from a Debian 8 (Jessie) container, scanned via
the artifact's `prevalence/debian8/Dockerfile` (which installs
`task-gnome-desktop` plus `libbotan-1.10-0` so that all four
cryptographic libraries enumerated in the paper are present
where available — Debian 8 base does not ship `libssl1.1`, so
the libcrypto family is represented by the OpenSSL 1.0.0 ABI
only).

This file documents the **container-reproducible** corpus.  An
earlier disassembly pass against a fortuitous host install
(captured in this repo's history as the original
`notes/debian8.md`) found `libcrypto.so.1.1` from
`jessie-backports`; reproducers using only the base archive
will not see that binary.

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.0.3` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0` | libssl1.0.0 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.20` | libstdc++6 |
| `usr/lib/libbotan-1.10.so.0.8` | libbotan-1.10-0 |

## Gating Results (manual disassembly)

```
GATED   libgcrypt.so.20.0.3   (co-located CPUID in gcry_is_secure)
UNGATED libcrypto.so.1.0.0    (cached OPENSSL_ia32cap_P)
UNGATED libstdc++.so.6.0.20   (caller-side _M_init; Intel-only on D8)
UNGATED libbotan-1.10.so.0.8  (caller-side Botan::CPUID singleton)
```

The three UNGATED results are heuristic artifacts in the sense
documented for D9–D13: each binary uses a cached CPUID flag
checked before the RDRAND wrapper is reached, but the CPUID
check is in a different function from the RDRAND instruction
and so does not satisfy the `check_rdrand_gating.sh`
co-location heuristic.  All four binaries are effectively
gated under manual disassembly.

The static `check_rdrand_gating.sh` script does not run as-is on
Debian 8: the script uses gawk extensions (`pattern1 ||
pattern2 { ... }`) that gawk 4.1.1 in jessie rejects despite
later gawk versions accepting them.  Gating classifications
above were produced by manual inspection of the disassembly.

---

## Per-Binary Analysis

### libgcrypt.so.20.0.3 — GATED (co-located CPUID)

RDRAND retry loop attributed by stripped symbols to
`gcry_is_secure+0x94df3`.  The same retry-loop pattern that
appears in every later libgcrypt version analyzed in this
repo:

```
9fbd3:  rdrand %rax
9fbd7:  jb     9fbdd        # success
9fbd9:  dec    %ecx
9fbdb:  jne    9fbd3        # retry
```

5 separate CPUID instructions appear elsewhere in the binary,
the relevant one being the leaf-1 CPUID inside libgcrypt's
HWF (HardWare Features) initialization that sets the cached
flag callers test before invoking the RDRAND wrapper.

---

### libcrypto.so.1.0.0 — UNGATED (cached `ia32cap`)

12 RDRAND instances across the binary (a count consistent with
OpenSSL 1.0.x's per-architecture retry-loop stubs:
`OPENSSL_ia32_cpuid` writes the cached CPUID word, and the
RDRAND stub at `OPENSSL_cleanse+0xd5` is reached via an
ENGINE_load_rdrand registration that tests the cached
RDRAND bit before activating the engine).

```
75b05:  rdrand %rax
75b09:  jb     75b0d        # success
75b0b:  loop   75b05        # retry up to 8 times
75b0d:  cmp    $0x0,%rax
75b11:  cmove  %rcx,%rax
```

Same `ENGINE_load_rdrand` gating pattern as libcrypto.so.1.0.2
on D9.  Symbol-stripping attribution shows the RDRAND stub as
sitting "inside" `OPENSSL_cleanse` only because of how the
RDRAND asm stub falls within `OPENSSL_cleanse`'s stripped
text region.

---

### libstdc++.so.6.0.20 — UNGATED (Intel-only dispatch)

RDRAND retry-loop inside `__once_proxy`:

```
b5b85:  rdrand %eax
b5b88:  mov    %eax,(%rsi)
b5b8a:  cmovb  %ecx,%eax        # retry if not successful
b5b8d:  test   %eax,%eax
b5b8f:  je     b5b80            # retry
```

`std::random_device::_M_init` (`b5bb0`) holds the CPUID
dispatch.  As noted in the paper (§4.2), the 6.0.20–6.0.25
generation checks only for Intel vendor strings before
invoking RDRAND; the AMD path falls back to `/dev/urandom`.
This is the period before 6.0.28 (D11) added AMD vendor
detection to extend RDRAND dispatch onto AMD hardware.

Under rr with CPUID faulting, `_M_init` sees no RDRAND
support and the retry-loop is not reached.

---

### libbotan-1.10.so.0.8 — UNGATED (caller-side singleton)

RDRAND instruction sits within the stripped-symbol region
attributed to `std::deque<std::string>::_M_push_back_aux` —
the classic libbotan symbol-layout artifact also seen on
D13's Botan 2.  The actual containing function is
Botan 1.10's `Processor_RNG` reseed implementation.

```
136da2:  rdrand %eax
136da5:  adc    $0x0,%edx
136da8:  cmp    $0x1,%edx
136dab:  mov    %eax,0xc(%rsp)
136daf:  jne    136d98          # retry
```

Two CPUID instances appear elsewhere in the binary, in
Botan 1.10's `CPUID::initialize()` (the analogue of Botan 2's
`CPUID::state()`).  Callers consult the cached
`CPUID::has_rdrand()` singleton before instantiating
`Processor_RNG`, matching the "CPUID singleton" entry in
Table 2 of the paper.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.0.3 | GATED | Co-located CPUID in `gcry_is_secure`-attributed region |
| libcrypto.so.1.0.0 | UNGATED | Cached `OPENSSL_ia32cap_P`; ENGINE_load_rdrand gates activation |
| libstdc++.so.6.0.20 | UNGATED | Caller-gated `_M_init`; **Intel-only** (no AMD detection until 6.0.28) |
| libbotan-1.10.so.0.8 | UNGATED | Caller-side singleton via `Botan::CPUID::has_rdrand()` |

All four binaries are effectively gated.

## Notable Findings

**Smallest corpus in the Debian set**: 4 binaries / 5,567
scanned, the lowest absolute count of any Debian release
analyzed.  The growth that follows (5 in D9 because
`libssl1.1` lands in stretch main, 19+ in D10+ because
systemd starts using RDRAND directly) is the trajectory the
paper documents as "the scope of RDRAND exposure expanded
silently with routine library updates."

**libstdc++ 6.0.20 is the pre-AMD-detection generation**:
matches the paper's "checked only for Intel vendor strings"
claim.  Reproducers running the container on AMD hardware
will see the libstdc++ binary skip RDRAND under
CPUID faulting — but the binary still contains the
instruction (the dispatch decision is at runtime, not link
time).

**libcrypto.so.1.1 is absent**: only `libcrypto.so.1.0.0` is
in the container.  The paper's earlier scan on a host install
captured `libcrypto.so.1.1` from `jessie-backports`; the
reproducible container scan does not enable backports, so
this binary is not present.  This is the only categorical
difference between the original-notes corpus and the
container-reproducible corpus.

**Build hacks required**: D8 is the only release in the
artifact that requires both `cp /bin/true /usr/bin/gpgv`
and `ulimit -n 1024` to make apt usable.  See
`prevalence/debian8/Dockerfile` for the explanation.
