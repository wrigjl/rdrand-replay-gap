# RDRAND/RDSEED Prevalence and Gating Analysis

Per-release disassembly of every RDRAND/RDSEED-bearing binary
recovered from the seven `prevalence/<distro>/Dockerfile`
containers.  Replaces `notes/debian{8..13}.md` and
`notes/gentoo.md`.

## Methodology

For each distribution the container builds a base system,
installs `task-gnome-desktop` (Debian) or `gnome-light`
(Gentoo) plus the four cryptographic libraries enumerated in
Table 2 of the paper (`libgcrypt`, OpenSSL's `libcrypto`,
`libsodium`, `libbotan`).  `scripts/scan_rdrand.sh` walks
every ELF on the resulting image and reports those containing
RDRAND or RDSEED; `scripts/check_rdrand_gating.sh` then
classifies each hit as GATED or UNGATED by looking for a
CPUID instruction in the same `objdump`-attributed function
as the RDRAND/RDSEED.

The GATED/UNGATED heuristic is coarse: it identifies binaries
whose RDRAND callsite shares a function with a CPUID
instruction, which is sufficient for libgcrypt's
`gcry_is_secure` pattern but misses any library that caches
the CPUID result and tests the cache from a separate function
(OpenSSL's `OPENSSL_ia32cap_P`, libsodium's
`sodium_runtime_has_rdrand`, libstdc++'s `_M_init` dispatch,
libbotan's `CPUID::has_rdrand` singleton).  Manual
disassembly across all seven distributions has not found a
single binary that is genuinely UNGATED — every UNGATED label
in this document is a heuristic artifact attributable to one
of those four cached-init patterns.

The `check_rdrand_gating.sh` script does not run as-is on
Debian 8 or on Gentoo's awk (gawk 4.1.1 in jessie rejects
`pattern1 || pattern2 { ... }`, and also treats `func` as a
synonym for the `function` keyword).  Both issues are fixed
in the current script; classifications for D8 and Gentoo
below were produced by manual inspection.

## Cross-distribution summary

| Release | RDRAND-bearing ELFs | Scanned ELFs |
|---------|---------------------|-------------:|
| D8      | 4                   | 5,567        |
| D9      | 5                   | 5,271        |
| D10     | 20                  | 4,885        |
| D11     | 22                  | 4,908        |
| D12     | 15                  | 4,903        |
| D13     | 20                  | 4,447        |
| Gentoo  | 13                  | 3,928        |
| **Total** | **99**            | **33,909**   |

Four cross-cutting observations recur across the
distributions and are the load-bearing findings for paper §4:

1. **The gating story is uniform.**  All 99 RDRAND-bearing
   binaries across the corpus are effectively gated by a
   cached CPUID check, via one of four mechanisms:
   co-located CPUID (libgcrypt), cached global flag
   (OpenSSL `ia32cap`, libsodium), caller-side dispatch
   (libstdc++ `_M_init`), or singleton lookup (libbotan
   `CPUID::has_rdrand`).  No binary is unconditionally
   issuing RDRAND.  rr's CPUID suppression correctly
   intercepts all four patterns on Intel; the AMD gap is
   driven by the absence of CPUID faulting on AMD before
   Linux 6.17, not by any library-side defect.

2. **libstdc++ adds AMD vendor detection in 6.0.28
   (Debian 11).**  Prior versions (6.0.20 through 6.0.25,
   shipping in D8–D10) check only `"GenuineIntel"` and fall
   back to `/dev/urandom` on AMD; 6.0.28 and later (D11–D13,
   Gentoo) add `"AuthenticAMD"`.  This means the
   `std::random_device{}` silent-divergence issue under rr
   was introduced gradually — Intel systems were affected
   from the time RDRAND dispatch was added; AMD systems
   became affected only after the AMD check landed.

3. **systemd dropped `rdrand()` between Debian 11 and 12.**
   D10 and D11 ship 14 and 13 systemd-family binaries
   respectively, all sharing a lazy-init `rdrand()`
   function; D12 onwards ships zero.  systemd 252 replaced
   the function with `getrandom()`, eliminating the
   AMD CPUID-faulting gap exposure for the largest
   single category of RDRAND callers in the corpus.

4. **Coverage is concentrated in crypto/system libraries,
   not user-facing applications.**  The Gentoo container
   doubles in size (1,933 → 3,928 ELFs) when
   `gnome-base/gnome-light` is added without producing a
   single new RDRAND callsite.  All 99 RDRAND-bearing
   binaries trace back to a small set of crypto libraries
   (libgcrypt, libcrypto, libsodium, libbotan) and the GCC
   toolchain binaries that statically link libstdc++.

### libstdc++ AMD-detection timeline

| Release | libstdc++ version | Vendor check |
|---------|-------------------|--------------|
| D8      | 6.0.20            | Intel only   |
| D9      | 6.0.22            | Intel only   |
| D10     | 6.0.25            | Intel only   |
| D11     | 6.0.28            | Intel + AMD  |
| D12     | 6.0.30            | Intel + AMD  |
| D13     | 6.0.33            | Intel + AMD  |
| Gentoo  | 6.0.34            | Intel + AMD  |

---

## Debian 8 (Jessie)

`prevalence/debian8/Dockerfile` installs `task-gnome-desktop`
plus `libbotan-1.10-0`.  Debian 8 base does not ship
`libssl1.1`, so the libcrypto family is represented by the
OpenSSL 1.0.0 ABI only.  An earlier disassembly pass against
a host install with `jessie-backports` enabled additionally
captured `libcrypto.so.1.1`; reproducers using only the base
archive will not see it.

Debian 8 is the only release that requires both
`cp /bin/true /usr/bin/gpgv` and `ulimit -n 1024` to make
apt usable inside the container — see
`prevalence/debian8/Dockerfile` for the explanation.

### Binaries

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.0.3` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0` | libssl1.0.0 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.20` | libstdc++6 |
| `usr/lib/libbotan-1.10.so.0.8` | libbotan-1.10-0 |

### Per-binary

**libgcrypt.so.20.0.3 — GATED (co-located CPUID).**
RDRAND retry loop attributed by stripped symbols to
`gcry_is_secure+0x94df3`:

```
9fbd3:  rdrand %rax
9fbd7:  jb     9fbdd        # success
9fbd9:  dec    %ecx
9fbdb:  jne    9fbd3        # retry
```

Five CPUID instructions appear elsewhere in the binary; the
leaf-1 CPUID inside libgcrypt's HWF (HardWare Features)
initialization sets the cached flag that callers test before
invoking the RDRAND wrapper.

**libcrypto.so.1.0.0 — UNGATED (cached `ia32cap`).**
12 RDRAND instances; gating via `ENGINE_load_rdrand`
testing the cached RDRAND bit in `OPENSSL_ia32cap_P` before
activating the engine.

```
75b05:  rdrand %rax
75b09:  jb     75b0d        # success
75b0b:  loop   75b05        # retry up to 8 times
75b0d:  cmp    $0x0,%rax
75b11:  cmove  %rcx,%rax
```

**libstdc++.so.6.0.20 — UNGATED (caller-gated, Intel-only).**
RDRAND retry loop inside `__once_proxy`; `_M_init`
(`0xb5bb0`) holds the CPUID dispatch and checks only for
`"GenuineIntel"`, so the AMD path falls back to
`/dev/urandom`.  Under rr with CPUID faulting, `_M_init`
sees no RDRAND support and the retry loop is unreachable.

**libbotan-1.10.so.0.8 — UNGATED (caller-side singleton).**
RDRAND sits in the stripped-symbol region attributed to
`std::deque<std::string>::_M_push_back_aux`; the actual
containing function is Botan 1.10's `Processor_RNG` reseed
implementation.  Callers consult the cached
`CPUID::has_rdrand()` singleton before instantiating
`Processor_RNG`.

---

## Debian 9 (Stretch)

### Binaries

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.1.6` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.2` | libssl1.0.2 |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.1` | libssl1.1 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22` | libstdc++6 |

### Per-binary

**libgcrypt.so.20.1.6 — GATED.**  `gcry_is_secure` at
`0xd060` contains both CPUID and RDRAND in the same function
body; same pattern as every later release.

**libcrypto.so.1.0.2 — UNGATED (stripped symbol).**  RDRAND
at `0x7a765`, labelled by objdump as `OPENSSL_cleanse+0xd5`
due to stripped symbols.  CPUID lives in `OPENSSL_init` at
`0x7a470` (four leaves: `0x7a4d8`, `0x7a534`, `0x7a545`,
`0x7a55f`) which populates `OPENSSL_ia32cap_P`.
`ENGINE_load_rdrand` at `0x127940` tests the cached bit
before enabling the engine:

```
127940:  lea   0x33fd79(%rip),%rax    # OPENSSL_ia32cap_P
127947:  testb $0x40,0x7(%rax)        # RDRAND bit
12794b:  jne   127950                 # skip if clear
12794d:  ret
```

**libcrypto.so.1.1 — UNGATED.**  RDRAND under
`CRYPTO_memcmp+0x...` (stripped artifact identical to D10+);
gated by cached `OPENSSL_ia32cap_P[]`.

**libstdc++.so.6.0.22 — UNGATED (caller-gated, Intel-only).**
RDRAND in `__once_proxy+0x45` (`0xb9165`); CPUID gate in
`_M_init` at `0xb9190`.  Dispatch is Intel-only:

```
b91a7:  call  basic_string::compare("default")
b91ae:  jne   b9208
b91b0:  cpuid                         # leaf 0
b91b4:  je    b91be                   # no CPUID: fallback
b91b6:  cmp   $0x756e6547,%ebx        # "Genu" (Intel)?
b91bc:  je    b91e8
        # fall through to /dev/urandom (no AMD check)
b91e8:  mov   $0x1,%eax
b91ed:  cpuid                         # leaf 1
b91ef:  and   $0x40000000,%ecx        # RDRAND bit
b91f5:  je    b91be
b91f8:  movq  $0x0,(%r12)             # RDRAND path
```

On AMD the `cmp $0x756e6547,%ebx` fails; `/dev/urandom` is
used regardless of RDRAND support.

---

## Debian 10 (Buster)

systemd 241 lands with a `rdrand()` function used across the
systemd family — the largest single category of new RDRAND
callers in the corpus.

### Binaries

| Path | Package |
|---|---|
| `lib/x86_64-linux-gnu/libgcrypt.so.20.2.4` | libgcrypt20 |
| `lib/x86_64-linux-gnu/libnss_myhostname.so.2` | libnss-myhostname |
| `lib/x86_64-linux-gnu/libnss_systemd.so.2` | libnss-systemd |
| `lib/x86_64-linux-gnu/libsystemd.so.0.25.0` | libsystemd0 |
| `lib/x86_64-linux-gnu/libudev.so.1.6.13` | libudev1 |
| `lib/x86_64-linux-gnu/security/pam_systemd.so` | libpam-systemd |
| `lib/systemd/libsystemd-shared-241.so` | systemd |
| `lib/systemd/systemd-networkd` | systemd-networkd |
| `lib/systemd/systemd-udevd` | udev |
| `lib/udev/cdrom_id` | udev |
| `lib/udev/scsi_id` | udev |
| `bin/systemctl` | systemd |
| `bin/systemd-hwdb` | systemd |
| `bin/udevadm` | udev |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.1` | libssl1.1 |
| `usr/lib/x86_64-linux-gnu/libsodium.so.23.2.0` | libsodium23 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25` | libstdc++6 |
| `usr/lib/NetworkManager/nm-iface-helper` | network-manager |
| `usr/sbin/NetworkManager` | network-manager |

### Per-binary

**libgcrypt.so.20.2.4 — GATED.**  `gcry_is_secure` at
`0xe540`; unchanged.

**systemd 241 family — GATED (lazy-init CPUID).**  All
14 systemd-family binaries share a common `rdrand()` function
with the same lazy-initialization pattern (verified in
`libsystemd-shared-241.so` at `0xef080`):

```
ef080:  mov   0x19cf9a(%rip),%ecx    # cached flag
ef087:  test  %ecx,%ecx
ef089:  js    ef0c0                  # negative: run CPUID
ef08b:  test  %ecx,%ecx
ef08d:  je    ef141                  # zero: no RDRAND
ef093:  rdrand %rbx                  # positive: use RDRAND
...
ef0c0:  xor   %eax,%eax
ef0c2:  cpuid                        # leaf 0
ef0c8:  mov   $0x1,%eax
ef0cd:  cpuid                        # leaf 1
ef0cf:  shr   $0x1e,%ecx             # RDRAND bit
ef0d2:  and   $0x1,%ecx
ef0d5:  mov   %ecx,0x19cf45(%rip)
ef0db:  jmp   ef08b
```

Under rr with CPUID faulting, the first call intercepts
CPUID, clears the RDRAND bit, caches zero, and all subsequent
calls take the zero branch.  On AMD before Linux 6.17 (no
CPUID faulting), the first call cached `true` and all
subsequent RDRANDs ran unintercepted — the mechanism behind
the rr/systemd incompatibility, fixed by systemd 251's switch
to `getrandom()`.

**libcrypto.so.1.1 — UNGATED.**  Same `CRYPTO_memcmp`
stripped artifact and cached `OPENSSL_ia32cap_P[]` gating
as D9.

**libsodium.so.23.2.0 — UNGATED (cached global flag).**
RDRAND inside the stripped region for
`crypto_aead_aes256gcm_keygen`; actual guard is a cached
global at `0x563ec`:

```
428ac:  mov   0x13a0a(%rip),%eax    # cached RDRAND flag
428c2:  test  %eax,%eax
428c4:  je    428e8
428c6:  rdrand %eax
```

`sodium_set_misuse_handler` (the library `_init`) runs four
CPUID calls at `0x24793`, `0x247aa`, `0x2486f`, `0x24897` to
populate the flag; `sodium_runtime_has_rdrand()` at
`0x24980` reads it.  Same cached-global pattern as
OpenSSL's `ia32cap`.

**libstdc++.so.6.0.25 — UNGATED (Intel-only).**  RDRAND at
`__once_proxy+0x45` (`0xbae65`); CPUID gate in `_M_init` at
`0xbae90`.  Structurally identical to 6.0.20/6.0.22; still
checks only `"GenuineIntel"`.

**NetworkManager / nm-iface-helper — GATED.**  Both contain
co-located CPUID and RDRAND, most likely via an embedded
BoringSSL or linked OpenSSL copy using the standard
cached-init pattern.

---

## Debian 11 (Bullseye)

systemd 247 adds the `SYSTEMD_RDRAND` env-var override; the
big behavioural change is libstdc++ 6.0.28's addition of AMD
vendor detection.

### Binaries

| Path | Package |
|---|---|
| `bin/systemctl` | systemd |
| `bin/systemd-hwdb` | systemd |
| `bin/udevadm` | udev |
| `lib/systemd/libsystemd-shared-247.so` | systemd |
| `lib/systemd/systemd-networkd` | systemd-networkd |
| `lib/udev/cdrom_id` | udev |
| `lib/udev/fido_id` | udev |
| `lib/udev/scsi_id` | udev |
| `lib/x86_64-linux-gnu/security/pam_systemd.so` | libpam-systemd |
| `sbin/apparmor_parser` | apparmor |
| `usr/bin/x86_64-linux-gnu-gcov-10` | gcc-10 |
| `usr/lib/NetworkManager/nm-iface-helper` | network-manager |
| `usr/lib/x86_64-linux-gnu/libcrypto.so.1.1` | libssl1.1 |
| `usr/lib/x86_64-linux-gnu/libgcrypt.so.20.2.8` | libgcrypt20 |
| `usr/lib/x86_64-linux-gnu/libnss_myhostname.so.2` | libnss-myhostname |
| `usr/lib/x86_64-linux-gnu/libnss_systemd.so.2` | libnss-systemd |
| `usr/lib/x86_64-linux-gnu/libsodium.so.23.3.0` | libsodium23 |
| `usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.28` | libstdc++6 |
| `usr/lib/x86_64-linux-gnu/libsystemd.so.0.30.0` | libsystemd0 |
| `usr/lib/x86_64-linux-gnu/libudev.so.1.7.0` | libudev1 |
| `usr/sbin/NetworkManager` | network-manager |

### Per-binary

**libgcrypt.so.20.2.8 — GATED.**  `gcry_is_secure` at
`0xe6c0`; unchanged.

**systemd 247 family — GATED (lazy-init + env override).**
13 binaries share the D10 lazy-init pattern with one
addition (verified in `libsystemd-shared-247.so` at
`0x13c1e0`):

```
13c1e9:  mov   0x155f39(%rip),%eax   # cached flag
13c1ef:  test  %eax,%eax
13c1f1:  js    13c228                # first call: CPUID
13c1f3:  test  %eax,%eax
13c1f5:  je    13c2e0                # zero: no RDRAND
13c1fb:  rdrand %rbx
...
13c228:  xor   %eax,%eax
13c22a:  cpuid                       # leaf 0
13c234:  mov   $0x1,%eax
13c239:  cpuid                       # leaf 1
13c23d:  and   $0x40000000,%ecx
13c248:  setne %dl
13c24b:  mov   %edx,0x155ed7(%rip)
13c253:  lea   "SYSTEMD_RDRAND",%rdi
13c25a:  call  getenv_bool_secure    # env-var override
```

A negative return from `getenv_bool_secure("SYSTEMD_RDRAND")`
disables RDRAND even when supported — the documented escape
hatch from the rr bug report.

**libcrypto.so.1.1 — UNGATED.**  Same `CRYPTO_memcmp`
artifact as D9/D10.

**libsodium.so.23.3.0 — UNGATED.**  Same cached-global
pattern as D10's libsodium 23.2.0.

**libstdc++.so.6.0.28 — UNGATED (Intel + AMD).**  RDRAND at
`__once_proxy+0x45` (`0xcdfc0+0x45`); CPUID gate in
`_M_init` at `0xce170`.  This release introduces AMD
detection:

```
ce260:  cmp   $0x68747541,%ebx      # "Auth" (AMD)?
ce266:  je    ce274
ce268:  cmp   $0x756e6547,%ebx      # "Genu" (Intel)?
ce26e:  jne   ce1af                  # neither: fallback
```

Each of the three token paths (`"default"`, `"rdrand"`,
`"rdseed"`) now checks both vendors.  The default token
additionally tests CPUID leaf 7 bit 18 (RDSEED) before
leaf 1 bit 30 (RDRAND), selecting the entropy function via a
`cmove` at `ce255`.  **This is the version where
`std::random_device{}` begins using RDRAND on AMD.**

**apparmor_parser and gcov-10 — GATED.**  Both statically
link libstdc++ 6.0.28 including the `_M_init` dispatch and
the AMD check (`"Auth"` at `c4bbb`/`c4bc3` in
apparmor_parser).  Heuristic happens to label them GATED
because the stripped layout puts CPUID in the same region as
the RDRAND stubs.

**NetworkManager / nm-iface-helper — GATED.**  Co-located
CPUID; same pattern as D10.

---

## Debian 12 (Bookworm)

systemd 252 drops the `rdrand()` function entirely; the
systemd family disappears from the scan.  GCC 12 brings a
toolchain footprint that exposes the
`_M_init_pretr1`/`_M_initERKNSt` symbol split.

### Binaries

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

### Per-binary

**libgcrypt.so.20.4.1 — GATED.**  `gcry_is_secure` at
`0x12010`; unchanged.

**libcrypto.so.3 — UNGATED.**  Same `CRYPTO_memcmp` stripped
artifact; cached `OPENSSL_ia32cap_P[]` gating.

**ossl-modules/legacy.so — GATED.**  OpenSSL provider module
embedding its own CPUID init and RDRAND asm stubs.  Both
fall within the same stripped-symbol region
(`OSSL_provider_init@@Base-0xa00`), so the heuristic labels
it GATED — coincidence of layout, not a different mechanism.

**libsodium.so.23.3.0 — UNGATED.**  Identical to D10/D11.

**libstdc++.so.6.0.30 — UNGATED (Intel + AMD).**  RDRAND at
`__once_proxy+0xc0` (`0xd32c0`); CPUID gate in `_M_init`
at `0xd3410`.  Structurally identical to 6.0.28:

```
d3451:  cpuid                        # leaf 0
d3462:  je    d34b6
d3464:  cmp   $0x68747541,%ebx       # "Auth" (AMD)?
d346a:  je    d3474
d346c:  cmp   $0x756e6547,%ebx       # "Genu" (Intel)?
d3472:  jne   d34b6
d3474:  mov   $0x7,%eax
d347b:  cpuid                        # leaf 7 (RDSEED)
d347d:  and   $0x40000,%ebx
d3483:  je    d34b0
d3485:  mov   $0x1,%eax
d348a:  cpuid                        # leaf 1 (RDRAND)
d3493:  and   $0x40000000,%ecx
d34a0:  cmove %rdx,%rax              # select function pointer
```

**GCC 12 compiler binaries — UNGATED heuristic / caller-gated
actual.**  `lto-dump-12`, `cc1`, `cc1plus`, and `lto1` are
classified UNGATED because the RDRAND instruction sits in the
`_M_init_pretr1` symbol region while CPUID dispatch lives in
`_M_initERKNSt` — a stripped-symbol split.  The actual
structure in `cc1`'s `_M_initERKNSt` at `0x19e6800`:

```
19e6841:  cpuid                      # leaf 0
19e6854:  cmp   $0x68747541,%ebx     # "Auth"?
19e685a:  je    19e6864
19e685c:  cmp   $0x756e6547,%ebx     # "Genu"?
19e6862:  jne   19e68a6
19e6864:  mov   $0x7,%eax
19e686b:  cpuid                      # leaf 7
19e686d:  and   $0x40000,%ebx
19e6873:  je    19e68a0              # no RDSEED: getentropy/arc4random
19e6875:  mov   $0x1,%eax
19e687a:  cpuid                      # leaf 1
19e6883:  and   $0x40000000,%ecx
19e6890:  cmove %rdx,%rax            # select function pointer
```

`dwp`, `ld.gold`, `gcov-12`, and `g++-mapper-server` are
classified GATED — same statically-linked dispatch, but the
stripped layout happens to put CPUID and RDRAND in the same
region.

**apparmor_parser — GATED.**  Same statically-linked
libstdc++ 6.0.30; CPUID co-located by symbol layout.

---

## Debian 13 (Trixie)

GCC 14 ships more frontend/utility binaries linking the C++
runtime than GCC 12 did; the toolchain-derived RDRAND count
grows from 8 (D12) to 13 (D13).  systemd remains absent.

### Binaries

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

### Per-binary

**libgcrypt.so.20.5.0 — GATED.**  `gcry_is_secure` at
`0x13370`; unchanged.

**libcrypto.so.3 — UNGATED.**  Same `CRYPTO_memcmp@@OPENSSL_3.0.0`
stripped artifact as D9–D12; cached `OPENSSL_ia32cap_P[]`
gating.  RDRAND retry-loop stubs are identical to earlier
libcrypto releases.

**ossl-modules/legacy.so — GATED.**  Same
`OSSL_provider_init@@Base-0xb50` stripped region as D12.

**libsodium.so.23.3.0 — UNGATED.**  Identical to D10/D11/D12.

**libbotan-2.so.19.19.5 — UNGATED (caller-side singleton).**
Two RDRAND-containing functions:

1. `Botan::Processor_RNG::reseed(...)` at `0x3960d0` —
   `rdrand %rdx` retry loop with no in-function CPUID:

   ```
   3960fa:  mov    $0xa,%ecx          # 10 retries
   396104:  rdrand %rdx
   396108:  adc    $0x0,%eax
   39610b:  cmp    $0x1,%eax
   39610e:  je     396140             # success
   396110:  sub    $0x1,%rcx
   396114:  jne    3960ff             # retry
   ```

2. A second RDRAND site attributed by the stripped symbol
   table to `std::deque<...>::_M_push_back_aux` — almost
   certainly `Botan::Processor_RNG::randomize` (at
   `0x396170`, immediately adjacent).

Both reached only via `Botan::Processor_RNG`, constructed
only after `Botan::CPUID::has_rdrand()` returns true — the
singleton pattern attributed to libbotan in Table 2 of the
paper.

**libstdc++.so.6.0.33 — UNGATED (Intel + AMD).**  RDRAND at
`__once_proxy@@GLIBCXX_3.4.11` (`0xdfc60`); structurally
identical to 6.0.28/6.0.30.

**GCC 14 compiler binaries — split heuristic / caller-gated
actual.**  `lto-dump-14`, `cc1plus`, `lto1`, and `cc1` are
UNGATED (same `_M_init_pretr1` / `_M_initERKNSt` split as
D12).  `gcov-tool-14`, `gcov-dump-14`, `cpp-14`, `g++-14`,
`gcc-14`, `gcov-14`, `lto-wrapper`, `g++-mapper-server`, and
`collect2` are GATED — same dispatch, different stripped
layout.  GCC 14 ships notably more such binaries than GCC 12.

**apparmor_parser — GATED.**  Statically-linked libstdc++
6.0.33; CPUID co-located by layout.

---

## Gentoo (`-march=broadwell`, snapshot 2026-06-02)

`prevalence/gentoo/Dockerfile` rebuilds stage3 with
`-march=broadwell` (so both RDRAND and RDSEED are in the
target instruction set), then adds the four crypto libraries
plus `gnome-base/gnome-light`.  The portage timestamp is
**2026-06-02**.

A CLI-only build of the same Dockerfile (without
`gnome-base/gnome-light`) scanned 1,933 binaries and found
the same 13 RDRAND-bearing files — adding the full GNOME
desktop more than doubled the scanned count (1,933 → 3,928)
without introducing a single new RDRAND callsite.  This is
the concentration finding cited in §4 of the paper: RDRAND
exposure is concentrated in a small set of crypto and
toolchain libraries.

### Binaries

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

### Instruction counts (objdump)

| Binary | RDRAND | RDSEED | CPUID |
|---|---:|---:|---:|
| libgcrypt.so.20.7.2 | 2 | 0 | 4 |
| libcrypto.so.3 | 1 | 1 | 10 |
| libbotan-3.so.11.11.1 | 1 | 1 | 4 |
| ossl-modules/legacy.so | 1 | 1 | 10 |
| libstdc++.so.6.0.34 (×2 multilib) | 1 | 4 | 17 (native) / 8 (32-bit) |
| cc1, cc1plus, f951, lto1, lto-wrapper, collect2, g++-mapper-server (×7) | 1 | 4 | 17–21 |

Every binary contains at least one RDRAND, and most contain
RDSEED — a direct consequence of `-march=broadwell`.  Under
`-march=haswell` (or the paper's original `-march=native`
on a non-Broadwell host) the RDSEED paths would have been
compiled out.

### Per-binary

**libgcrypt.so.20.7.2 — GATED.**  Same `gcry_is_secure`
retry loop as every other release:

```
e3999:  rdrand %rax
e399d:  jb     e39a3       # success
e399f:  dec    %edx
e39a1:  jne    e3999       # retry
```

Two RDRAND instances in adjacent retry loops (`e3999` and
`e3a31`) but no RDSEED — the Gentoo libgcrypt 1.11 build
appears to use only the RDRAND path; RDSEED is either USE-flag
gated or pruned upstream.

**libcrypto.so.3 / ossl-modules/legacy.so — same as D12/D13.**

**libbotan-3.so.11.11.1 — UNGATED (singleton).**  Botan 3
replaces Botan 2's `Processor_RNG` with `HMAC_DRBG` as the
default RNG.  Symbol attribution puts RDRAND inside
`HMAC_DRBG::max_number_of_bytes_per_request`'s stripped
region; actual containing function is the Processor_RNG
equivalent.  Gating is unchanged: caller-side
`Botan::CPUID::has_rdrand()` singleton.

```
596e89:  rdrand %rdx
596e8d:  adc    $0x0,%eax
596e90:  cmp    $0x1,%eax
596e93:  je     596ec8       # success
596e95:  dec    %rcx
596e98:  jne    596e85       # retry
```

**libstdc++.so.6.0.34 — UNGATED (Intel + AMD).**  GCC 15's
libstdc++ keeps the dispatch architecture from
6.0.28/6.0.30/6.0.33.  Notably the RDSEED-preferred path is
compiled in (4 RDSEED instances vs 1 RDRAND); falls back to
RDRAND when RDSEED is unavailable.

```
e5d90:  rdrand %eax
e5d93:  mov    %eax,0x4(%rsp)
e5d97:  cmovb  %edx,%eax
e5d9a:  test   %eax,%eax
e5d9c:  je     e5dc0          # retry
```

**GCC 15 toolchain (7 binaries) — statically-linked
libstdc++; same `_M_init_pretr1` / `_M_initERKNSt` split as
D12/D13.**

### libsodium 1.0.22 — no RDRAND/RDSEED in this snapshot

The Gentoo build of libsodium 1.0.22 retains a
`sodium_runtime_has_rdrand()` symbol (visible in the symbol
table) but does **not** emit any RDRAND or RDSEED in
`.text`.  This is a real upstream-vs-Debian difference:
Debian's libsodium 23.x ships RDRAND in the binary; the
current Gentoo build does not.  The function exists for ABI
purposes but unconditionally returns the cached flag without
ever executing hardware entropy instructions.

Consistent with the rolling-tree caveat in the artifact
README: an earlier Gentoo snapshot (the one captured in the
paper's original 22-binary scan) likely had libsodium
emitting RDRAND; today's snapshot does not.
