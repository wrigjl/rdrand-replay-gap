# Gentoo (-march=broadwell) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Thirteen binaries from a Gentoo container built via the
artifact's `prevalence/gentoo/Dockerfile` (which rebuilds the
stage3 with `-march=broadwell` so both RDRAND and RDSEED are
in the target instruction set, then adds the four crypto
libraries enumerated in Table 2 of the paper plus a small set
of CLI utilities).

The container is **CLI-only** — no desktop install, no GNOME
stack — which is why the absolute binary count (1,933) is
substantially smaller than the Debian containers (4,500–5,600).
A `-march=broadwell` rebuild with `gnome-base/gnome-light` would
be much larger and is left as a follow-up (see
`prevalence/gentoo/Dockerfile` comments).

The portage snapshot used here is **dated 2026-06-02**
(per `emerge --info`'s "Timestamp of repository gentoo");
exact package versions reflect that day's tree.

| Path | Package |
|---|---|
| `usr/lib/gcc/x86_64-pc-linux-gnu/15/libstdc++.so.6.0.34` | sys-devel/gcc |
| `usr/lib/gcc/x86_64-pc-linux-gnu/15/32/libstdc++.so.6.0.34` | sys-devel/gcc (multilib) |
| `usr/lib64/libgcrypt.so.20.7.2` | dev-libs/libgcrypt |
| `usr/lib64/libcrypto.so.3` | dev-libs/openssl |
| `usr/lib64/ossl-modules/legacy.so` | dev-libs/openssl |
| `usr/lib64/libbotan-3.so.11.11.1` | dev-libs/botan |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/cc1` | sys-devel/gcc |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/cc1plus` | sys-devel/gcc |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/f951` | sys-devel/gcc |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/lto1` | sys-devel/gcc |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/lto-wrapper` | sys-devel/gcc |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/collect2` | sys-devel/binutils |
| `usr/libexec/gcc/x86_64-pc-linux-gnu/15/g++-mapper-server` | sys-devel/gcc |

## Instruction Counts (objdump)

| Binary | RDRAND | RDSEED | CPUID |
|---|---:|---:|---:|
| libgcrypt.so.20.7.2 | 2 | 0 | 4 |
| libcrypto.so.3 | 1 | 1 | 10 |
| libbotan-3.so.11.11.1 | 1 | 1 | 4 |
| ossl-modules/legacy.so | 1 | 1 | 10 |
| libstdc++.so.6.0.34 (×2 multilib) | 1 | 4 | 17 (native) / 8 (32-bit) |
| cc1, cc1plus, f951, lto1, lto-wrapper, collect2, g++-mapper-server (×7) | 1 | 4 | 17–21 |

Every binary contains at least one RDRAND, and most contain
RDSEED — the latter is a direct consequence of building with
`-march=broadwell`.  Under `-march=haswell` (or the paper's
original `-march=native` on a non-Broadwell host), the RDSEED
paths would have been compiled out.

## Gating Results (manual disassembly)

The `check_rdrand_gating.sh` heuristic does not run cleanly
on Gentoo's awk (same gawk-extension portability issue
documented for D8).  Classifications below are from manual
disassembly of the disassemblies above and known caller-side
CPUID-gating patterns.

| Binary | Gating mechanism |
|---|---|
| libgcrypt.so.20.7.2 | Co-located CPUID in HWF init region; same `gcry_is_secure` retry-loop pattern as every other release |
| libcrypto.so.3 | Cached `OPENSSL_ia32cap_P[]`; ENGINE_load_rdrand gates activation (same as D12/D13) |
| ossl-modules/legacy.so | Same cached `ia32cap` init (provider module embeds OpenSSL's CPUID dispatch) |
| libbotan-3.so.11.11.1 | Caller-side `Botan::CPUID::has_rdrand()` singleton; RDRAND sits inside `HMAC_DRBG::max_number_of_bytes_per_request@@Base+0x15` (stripped-symbol artifact — the actual containing function is `Processor_RNG` reseed/randomize, immediately preceding) |
| libstdc++.so.6.0.34 (both copies) | Caller-side `_M_init` Intel+AMD dispatch (same as 6.0.28/6.0.30/6.0.33; the larger CPUID count reflects the statically-linked-with-Botan/gcc-runtime build pattern) |
| GCC 15 toolchain (7 binaries) | Statically-linked libstdc++; `_M_init_pretr1` + `_M_initERKNSt` split — same pattern documented for D12 and D13 |

All 13 binaries are effectively gated.

## Per-Binary Highlights

### libgcrypt.so.20.7.2 — same `gcry_is_secure` retry loop

```
e3999:  rdrand %rax
e399d:  jb     e39a3       # success
e399f:  dec    %edx
e39a1:  jne    e3999       # retry
```

Two RDRAND instances appear in adjacent retry-loop bodies
(e3999 and e3a31) but no RDSEED, which differs from the
OpenSSL/Botan/libstdc++ pattern.  Gentoo's libgcrypt 1.11 build
appears to use only the RDRAND path; the RDSEED fallback is
either gated behind a USE flag or pruned by upstream.

### libbotan-3.so.11.11.1 — HMAC_DRBG instead of Processor_RNG

```
596e89:  rdrand %rdx
596e8d:  adc    $0x0,%eax
596e90:  cmp    $0x1,%eax
596e93:  je     596ec8       # success
596e95:  dec    %rcx
596e98:  jne    596e85       # retry
```

The Botan 3 release line replaces Botan 2's `Processor_RNG`
class with `HMAC_DRBG` as the default RNG.  Symbol attribution
shows the RDRAND inside `HMAC_DRBG::max_number_of_bytes_per_request`'s
stripped region; the actual containing function is the
Processor_RNG-equivalent in Botan 3.  Gating remains caller-side
via `Botan::CPUID::has_rdrand()` (the singleton pattern Table 2
of the paper attributes to libbotan; unchanged across Botan 2
and 3).

### libstdc++.so.6.0.34 — 1 RDRAND, 4 RDSEED

```
e5d90:  rdrand %eax
e5d93:  mov    %eax,0x4(%rsp)
e5d97:  cmovb  %edx,%eax
e5d9a:  test   %eax,%eax
e5d9c:  je     e5dc0          # retry
```

GCC 15's libstdc++ keeps the dispatch architecture from
6.0.28/6.0.30/6.0.33 — `_M_init` selects a function pointer
based on cached CPUID results.  Notably the RDSEED-preferred
path is now compiled in (4 RDSEED instances vs 1 RDRAND);
the binary still falls back to RDRAND when RDSEED is
unavailable.

## libsodium 1.0.22 — *no* RDRAND/RDSEED on this snapshot

The Gentoo build of libsodium 1.0.22 retains a
`sodium_runtime_has_rdrand()` symbol (visible in the symbol
table) but does **not** emit any RDRAND or RDSEED instructions
in `.text`.  This is a real upstream-vs-Debian difference:
Debian's libsodium 23.x ships with RDRAND in the binary; the
current Gentoo build does not.  The function exists for ABI
purposes but unconditionally returns the cached flag without
ever executing hardware entropy instructions.

This is consistent with the rolling-tree caveat in the artifact
README: an earlier Gentoo snapshot (the one captured in the
paper's original 22-binary scan) likely had libsodium emitting
RDRAND; today's snapshot doesn't.

## Notable Findings

**1,933 ELF binaries scanned, 13 contain RDRAND/RDSEED.**  Of
those, 9 are GCC 15 toolchain binaries (all the same
statically-linked libstdc++ dispatch), 4 are runtime libraries.

**Botan 3 instead of Botan 2.**  Gentoo's portage tree has
moved to Botan 3 (`dev-libs/botan` slot 3); Debian still ships
Botan 2.  Both expose the same caller-side singleton pattern;
only the symbol attribution differs.

**RDSEED emitted everywhere it can be.**  Switching from
`-march=haswell` to `-march=broadwell` brings RDSEED into the
target instruction set, and several libraries (libstdc++,
libcrypto, libbotan-3) now contain RDSEED instructions that
would not have appeared in the paper's original
`-march=native` scan on a Haswell-target host.

**libsodium has no RDRAND on this snapshot.**  Documented
above; representative of the rolling-tree drift the README
caveats describe.

**CLI-only image.**  This Gentoo container does not include a
desktop environment; the absolute binary count is therefore
smaller than the Debian containers' `task-gnome-desktop`-based
counts.  Adding `gnome-base/gnome-light` to the Dockerfile's
explicit emerge list would expand the corpus and could surface
additional RDRAND/RDSEED-bearing libraries (gnome-keyring uses
libgcrypt, etc.).
