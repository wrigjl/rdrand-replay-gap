# Debian 11 (Bullseye) RDRAND/RDSEED Gating Analysis

## Binaries Scanned

Twenty-one binaries extracted from a Debian 11 (Bullseye) system.  The set
includes systemd 247 components, NetworkManager, three crypto libraries,
apparmor\_parser, and gcov-10.

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

## Gating Results (check_rdrand_gating.sh)

```
GATED   systemctl                    (1 function)
GATED   systemd-hwdb                 (1 function)
GATED   udevadm                      (1 function)
GATED   libsystemd-shared-247.so     (1 function)
GATED   systemd-networkd             (1 function)
GATED   cdrom_id                     (1 function)
GATED   fido_id                      (1 function)
GATED   scsi_id                      (1 function)
GATED   pam_systemd.so               (1 function)
GATED   apparmor_parser              (1 function)
GATED   x86_64-linux-gnu-gcov-10     (1 function)
GATED   nm-iface-helper              (1 function)
UNGATED libcrypto.so.1.1             (1 function)
GATED   libgcrypt.so.20.2.8          (1 function)
GATED   libnss_myhostname.so.2       (1 function)
GATED   libnss_systemd.so.2          (1 function)
UNGATED libsodium.so.23.3.0          (1 function)
UNGATED libstdc++.so.6.0.28          (1 function)
GATED   libsystemd.so.0.30.0         (1 function)
GATED   libudev.so.1.7.0             (1 function)
GATED   NetworkManager               (1 function)
```

All three UNGATED results are heuristic artifacts.  Manual disassembly
confirms all 21 binaries use cached or caller-level CPUID checks.

---

## Per-Binary Analysis

### libgcrypt.so.20.2.8 — GATED

`gcry_is_secure` at `0xe6c0`; co-located CPUID.  Unchanged from all prior
releases.

---

### systemd 247 family — GATED (lazy-init CPUID, env-var override added)

All systemd-family binaries (libsystemd-shared-247.so, libsystemd.so,
libnss\_systemd.so, libnss\_myhostname.so, libudev.so, pam\_systemd.so,
systemd-networkd, systemctl, systemd-hwdb, udevadm, cdrom\_id, fido\_id,
scsi\_id) share the same `rdrand()` lazy-init function (verified in
libsystemd-shared-247.so at `0x13c1e0`):

```
13c1e9:  mov   0x155f39(%rip),%eax   # load cached flag
13c1ef:  test  %eax,%eax
13c1f1:  js    13c228                # negative: run CPUID (first call)
13c1f3:  test  %eax,%eax
13c1f5:  je    13c2e0                # zero: no RDRAND, return error
13c1fb:  rdrand %rbx                 # positive: use RDRAND
...
13c228:  xor   %eax,%eax
13c22a:  cpuid                       # leaf 0
13c234:  mov   $0x1,%eax
13c239:  cpuid                       # leaf 1
13c23d:  and   $0x40000000,%ecx      # test RDRAND bit
13c248:  setne %dl
13c24b:  mov   %edx,0x155ed7(%rip)  # cache: 0=no, 1=yes
13c253:  lea   "SYSTEMD_RDRAND",%rdi
13c25a:  call  getenv_bool_secure    # honour env-var override
```

The lazy-init pattern is identical to systemd 241.  The notable addition in
247 is the `getenv_bool_secure("SYSTEMD_RDRAND")` call after the CPUID
result is cached; a negative return disables RDRAND even when the CPU
supports it.  This is the env-var escape hatch mentioned in the rr bug
report.  rr's CPUID suppression still handles this correctly when CPUID
faulting is available.

---

### libcrypto.so.1.1 — UNGATED (stripped symbol artifact)

Same stripped `CRYPTO_memcmp` artifact as D9 and D10; cached
`OPENSSL_ia32cap_P[]` gating.

---

### libsodium.so.23.3.0 — UNGATED (cached global flag)

Same pattern as libsodium 23.2.0 from D10: library initializer runs CPUID,
caches result in a global, RDRAND callers check the cached flag before use.

---

### libstdc++.so.6.0.28 — UNGATED (caller-gated; **AMD detection added**)

RDRAND at `__once_proxy+0x45` (`0xcdfc0+0x45`).  CPUID gate in `_M_init`
at `0xce170`.

This version introduces AMD vendor detection.  Prior libstdc++ versions
(6.0.20 through 6.0.25, Debian 8–10) checked only `"Genu"` (Intel).
libstdc++ 6.0.28 adds `"Auth"` (AMD) checks in every token path:

```
ce260:  cmp   $0x68747541,%ebx      # "Auth" (AMD)?
ce266:  je    ce274                  # yes: proceed to feature check
ce268:  cmp   $0x756e6547,%ebx      # "Genu" (Intel)?
ce26e:  jne   ce1af                  # neither: fallback
```

The function now handles three token paths ("default", "rdrand", "rdseed"),
each checking both vendors.  The default token path additionally tests
CPUID leaf 7 bit 18 (RDSEED support) before leaf 1 bit 30 (RDRAND),
selecting the appropriate entropy function via a `cmove` at `ce255`.

**This is the version where `std::random_device{}` begins using RDRAND on
AMD hardware.**  Debian 8, 9, and 10 were not affected on AMD; Debian 11
is the first release where the AMD RDRAND path is active for the default
constructor.

---

### apparmor\_parser and gcov-10 — GATED (statically-linked libstdc++)

Both binaries statically link a copy of libstdc++ 6.0.28, including the
`_M_init` dispatch.  The code is labeled under stripped symbol names
(`reallocarray@@Base` for apparmor\_parser, `_obstack_newchunk@@Base` for
gcov-10).  The CPUID, RDRAND/RDSEED stubs, and Intel/AMD vendor checks are
identical to the shared libstdc++ 6.0.28, including the "Auth" AMD check at
`c4bbb`/`c4bc3` in apparmor\_parser.

The heuristic correctly flags these as GATED because the CPUID instructions
fall within the same large stripped-symbol region as the RDRAND instructions.

---

### NetworkManager / nm-iface-helper — GATED

Co-located CPUID; same pattern as D10.

---

## Summary

| Binary | Heuristic | Actual gating mechanism |
|---|---|---|
| libgcrypt.so.20.2.8 | GATED | Co-located CPUID in `gcry_is_secure` |
| systemd family (13 binaries) | GATED | Lazy-init CPUID in `rdrand()` |
| apparmor\_parser | GATED | Statically-linked libstdc++ 6.0.28 `_M_init` |
| gcov-10 | GATED | Statically-linked libstdc++ 6.0.28 `_M_init` |
| NetworkManager (2 binaries) | GATED | Co-located CPUID |
| libcrypto.so.1.1 | UNGATED | Cached `OPENSSL_ia32cap_P` |
| libsodium.so.23.3.0 | UNGATED | Cached global flag from `_init` CPUID |
| libstdc++.so.6.0.28 | UNGATED | Caller-gated `_M_init`; Intel **and AMD** |

All 21 binaries are effectively gated.

## Notable Findings

**AMD vendor detection added in libstdc++ 6.0.28.**  This is the most
significant change from prior Debian releases.  Starting with Debian 11,
`std::random_device{}` dispatches to RDRAND on AMD hardware as well as
Intel.  Under rr on AMD before Linux~6.17 (no CPUID faulting), this means
any C++ program using the default `std::random_device` constructor would
silently use RDRAND unintercepted.

**Systemd 247 adds `SYSTEMD_RDRAND` env-var override**, providing a
documented escape hatch for environments where RDRAND is problematic.  rr
would still suppress RDRAND via CPUID interception on supported hardware,
but the env-var provides a fallback for AMD systems lacking CPUID faulting.

**Two new categories** appear vs. Debian 10: `apparmor_parser` (libgcrypt
user, statically-linked libstdc++) and `gcov-10` (statically-linked
libstdc++).  Both correctly gated; statically-linked libstdc++ copies
propagate the AMD-aware dispatch code.
