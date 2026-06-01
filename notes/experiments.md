# Proposed Experiments

Organized by reviewer priority. "Must-have" experiments are table
stakes for acceptance; "should-have" strengthen the paper
significantly; "nice-to-have" add depth but aren't expected for a
workshop paper.

---

## Must-have

### Experiment 1: Replay divergence with `-mrdrnd` (Section 4.2)

**Validates:** Core claim that RDRAND breaks `rr` replay when CPUID
check is absent.

Compile `exp2_intrinsic.c` with `-mrdrnd`, record under `rr`, replay,
show the values differ. The control build (without `-mrdrnd`) checks
CPUID first; `rr` suppresses the flag and replay succeeds. This A/B
comparison demonstrates the boundary of `rr`'s current mitigation.

**Procedure:**

```bash
# Intrinsic build (no CPUID check)
gcc -O2 -mrdrnd -o exp2_intrinsic exp2_intrinsic.c
objdump -d exp2_intrinsic | grep -E 'rdrand|cpuid'
rr record ./exp2_intrinsic 2>record_intrinsic.log
rr replay -a 2>replay_intrinsic.log
diff record_intrinsic.log replay_intrinsic.log

# Control build (CPUID check first)
gcc -O2 -o exp2_control exp2_intrinsic.c
objdump -d exp2_control | grep -E 'rdrand|cpuid'
rr record ./exp2_control 2>record_control.log
rr replay -a 2>replay_control.log
diff record_control.log replay_control.log
```

**Expected results:**

| Build | CPUID check? | `rr` mitigation effective? | Replay faithful? |
|-------|-------------|---------------------------|-----------------|
| `-mrdrnd` | No | N/A (never checked) | **No** — values diverge |
| control | Yes | Yes (flag suppressed) | Yes |

**Source:** `exp2_intrinsic.c` (already written)

### Experiment 2: Prevalence scan of distribution binaries (Section 4.2)

**Validates:** "Growing scope" claim — quantifies how many binaries
in a real distribution contain RDRAND/RDSEED.

Scan all ELF binaries on a Debian 13 installation using
`scan_rdrand.sh`. Report:

- N binaries scanned
- M contain RDRAND/RDSEED instructions
- K of those have no preceding CPUID check (i.e., `rr`'s mitigation
  would not help)
- Which packages they belong to

**Procedure:**

```bash
./scan_rdrand.sh /usr/bin /usr/sbin /usr/lib > scan_results.txt 2>&1
```

Post-process `scan_results.txt` to produce a summary table. For each
binary with RDRAND, check whether CPUID appears earlier in the
disassembly (heuristic: CPUID in same function before RDRAND).

**Expected output:** A table suitable for the paper, e.g.:

| Category | Count |
|----------|-------|
| Binaries scanned | N |
| Contain RDRAND/RDSEED | M (X%) |
| RDRAND with CPUID gate | K |
| RDRAND without CPUID gate | M-K |

**Source:** `scan_rdrand.sh` (already written),
`check_rdrand_gating.sh` (checks CPUID gating per binary)

#### Preliminary results (Debian 13 Trixie, 2026-03-25)

Scanned 6771 ELF binaries across `/usr/bin`, `/usr/sbin`, `/usr/lib`,
`/usr/libexec`, `/usr/share`, and `~/.vscode`. 30 binaries (0.44%)
contain RDRAND or RDSEED instructions.

| Category | Binaries | Examples |
|----------|----------|---------|
| GCC toolchain | 14 | cc1, cc1plus, gcc, g++, gcov-*, lto-*, collect2 |
| Crypto libraries | 4 | libcrypto.so (OpenSSL), libgcrypt, libsodium, libbotan |
| libstdc++ | 1 | `std::random_device` path |
| VS Code / extensions | 7 | code, code-tunnel, crash handler, Claude Code x2, cpptools x3 |
| LLDB extension | 1 | libpython312.so |
| OpenSSL modules | 1 | ossl-modules/legacy.so |
| System tools | 1 | apparmor_parser |
| **Total** | **30** | |

**Observations:**

- Crypto libraries (OpenSSL, libgcrypt, libsodium, libbotan) are
  the most forensically relevant hits — any binary performing TLS,
  encryption, or signature verification will link one of these.
- GCC is the largest single group (14 binaries) but all share the
  same statically linked runtime code.
- All VS Code binaries likely use the BoringSSL/`ring` pattern
  with cached CPUID gating (confirmed for Claude Code).
#### CPUID gating analysis

`check_rdrand_gating.sh` checks whether each function containing
RDRAND/RDSEED also contains a CPUID instruction. "UNGATED" means
the RDRAND wrapper function has no co-located CPUID — the gating
(if any) happens in a caller via a cached flag.

| Gating | Count | Binaries |
|--------|-------|----------|
| GATED (CPUID in same function) | 16 | Claude Code x2, libpython312, GCC tools x8, legacy.so, libgcrypt, code-tunnel, apparmor_parser |
| UNGATED (no co-located CPUID) | 14 | cpptools x3, clang-tidy, lto-dump, libsodium, libbotan, libcrypto, libstdc++, chrome_crashpad_handler, code, cc1plus, lto1, cc1 |

**Manual analysis of UNGATED binaries:** All 14 "UNGATED"
binaries were manually analyzed to determine whether they use a
cached CPUID check in a calling function. **All 14 are
caller-gated.** The mechanisms fall into five categories:

| Mechanism | Binaries | Pattern |
|-----------|----------|---------|
| libstdc++ `_M_init` dispatch | cpptools x3, clang-tidy, cc1, cc1plus, lto1, lto-dump, libstdc++ | `std::random_device::_M_init` runs CPUID, stores function pointer to `__x86_rdrandEPv`/`__x86_rdseedEPv` |
| libsodium cached global | libsodium | `sodium_runtime_has_rdrand` returns cached bit set by CPUID during init |
| Botan `CPUID::state()` singleton | libbotan | `__cxa_guard` lazy init populates CPUID state; `Processor_RNG::available` checks cached RDRAND bit |
| OpenSSL provider architecture | libcrypto | `OPENSSL_ia32_cpuid` asm populates capabilities; SEED-SRC provider checks before calling RDRAND |
| BoringSSL `pthread_once` | code, chrome_crashpad_handler | `pthread_once` runs CPUID init, caches in global; caller tests `$0x40000000` bit before RDRAND |

**Note on GCC toolchain:** cc1, cc1plus, lto1, and lto-dump
also contain `gen_rdrand*` functions — these are code
*generators* that emit RDRAND instructions into compiled output,
not functions that execute RDRAND themselves.

**Result: Zero binaries on this Debian 13 system contain truly
ungated RDRAND.** Every instance uses a cached CPUID check
pattern. `rr`'s CPUID suppression handles all of them correctly
(the cached flag is never set, so the RDRAND wrapper is never
called).

However, this finding actually *strengthens* the paper's
argument. The safety of the current ecosystem depends entirely
on a **convention**: that RDRAND is reachable only through
caller-level CPUID checks. This convention is architecturally
fragile:

- If a binary adds a new call site that bypasses the cached
  check, `rr` cannot detect or prevent the divergence.
- If link-time optimization inlines the RDRAND wrapper past the
  check, the gating invariant breaks silently.
- If a library changes its internal structure (as systemd did
  when it added direct RDRAND in v247 and removed it in v251),
  the convention can appear and disappear between versions.
- The `-mrdrnd` compiler flag and Rust's
  `target-feature=+rdrand` bypass the convention entirely at
  compile time.

A truly robust solution requires instruction-level
interception, not caller-level conventions.

**Key finding for the paper:** All four crypto libraries on the
system (libcrypto, libgcrypt, libsodium, libbotan) contain
RDRAND. These are exactly the libraries that forensically
interesting binaries link against.

### Experiment 3: Silent divergence demonstration (Section 5.1)

**Validates:** Forensic argument — divergence is silent (no crash, no
`rr` error, wrong results).

Build a program where RDRAND seeds a forensically meaningful
decision: e.g., RDRAND generates a key, the program encrypts a
message with it, and the ciphertext is written to stdout. Record
under `rr`. On replay, the program completes with exit code 0 and
no errors, but the ciphertext differs because RDRAND returned a
different value.

This is the most important experiment for the WSDF audience. The
danger isn't that replay fails — it's that it **succeeds with wrong
results**.

**Procedure:**

```bash
gcc -O2 -mrdrnd -o exp3_silent exp3_silent.c
rr record ./exp3_silent > record_output.bin 2>record.log
rr replay -a > replay_output.bin 2>replay.log
echo "Exit codes match: $(diff <(echo $?) <(echo 0))"
echo "rr reported no errors: $(grep -c error replay.log)"
echo "Output differs:"
diff record_output.bin replay_output.bin
```

**Expected:** Exit code 0 both times, no `rr` errors, completely
different output. The analyst would have no indication that the
replay was unfaithful.

**Source:** `exp3_silent.c` (written)

#### Design note: data-only vs control-flow divergence

An initial version of `exp3_silent.c` branched on the RDRAND
carry flag (returning an error if CF=0). This caused a
different number of retired instructions between recording and
replay, and `rr` detected the divergence via its tick-count
consistency check at syscall boundaries:

```
ticks mismatch for 'SYSCALL: write'; expected 28175, got 28184
```

This is an important observation for the paper: `rr` can
*indirectly* detect RDRAND divergence when the different values
cause different control flow (and therefore different tick
counts). However, this detection is a side effect, not a
designed safety mechanism — it depends on the divergence
happening to alter the instruction count before the next
syscall. If the RDRAND value affects only data (e.g., used as
an encryption key with no conditional branching), the tick
counts remain identical and the divergence is truly silent.

The revised `exp3_silent.c` is carefully written to avoid all
RDRAND-dependent branching:

- `generate_key()` ignores the carry flag and unconditionally
  stores all four values
- Output uses raw `write()` with fixed-length hex encoding
  instead of `fprintf()` (which has data-dependent branching)
- No conditional logic depends on RDRAND-derived values

This ensures the retired instruction count is identical between
recording and replay, making the divergence undetectable by
`rr`'s tick-count check.

---

## Should-have

### Experiment 4: Gentoo `-march=native` confirmation (Section 4.2)

**Validates:** Claim that `-march=native` implicitly enables
`-mrdrnd` on modern hardware.

Show that `gcc -march=native -dM -E` defines `__RDRND__`, and that
a program compiled with `-march=native` emits RDRAND without CPUID.

**Procedure:**

```bash
echo 'int main(){}' | gcc -march=native -dM -E -x c - | grep RDRND
gcc -O2 -march=native -o exp2_native exp2_intrinsic.c
objdump -d exp2_native | grep -E 'rdrand|cpuid'
```

**Expected:** `__RDRND__` is defined; disassembly shows RDRAND
without preceding CPUID. This confirms the Gentoo paragraph in one
command.

### Experiment 5: Library-path divergence (Section 4.1)

**Validates:** Historical failure mechanism via `std::random_device`
and `libnss_systemd`.

Both libraries have since fixed their RDRAND usage, so reproduction
requires specific environments. The fact that fixes were needed
reinforces the paper's argument.

#### Version-specific notes

- **libstdc++ (`std::random_device`)**: All tested versions (GCC
  7–14) gate RDRAND behind a CPUID check, so `rr`'s CPUID
  suppression works on Intel hardware with CPUID faulting. The
  failure occurs on **AMD hardware without CPUID faulting** (all
  AMD before Linux 6.17, Sep 2025). The priority order changed in
  GCC 10 (May 2019): RDSEED/RDRAND were promoted above
  `/dev/urandom`, increasing exposure on affected systems.

  | Debian | GCC | Default entropy priority | RDRAND risk |
  |--------|-----|--------------------------|-------------|
  | 9 Stretch (2017-06-17) | 6.3 | RDRAND (Intel+CPUID) > `/dev/urandom` | AMD only |
  | 10 Buster (2019-07-06) | 8.3 | RDRAND (Intel+CPUID) > `/dev/urandom` | AMD only |
  | 11 Bullseye (2021-08-14) | 10.2 | RDSEED > RDRAND > `/dev/urandom` | AMD only |
  | 12 Bookworm (2023-06-10) | 12.2 | RDSEED > RDRAND > `getentropy()` > `/dev/urandom` | AMD only |
  | 13 Trixie (2025-08-09) | 14.2 | RDSEED > RDRAND > `getentropy()` > `/dev/urandom` | None (AMD CPUID faulting in 6.17) |

- **systemd (`libnss_systemd`)**: Direct RDRAND usage was removed
  in systemd v251 (2022), replaced with `getrandom(GRND_INSECURE)`.
  Debian 11 (systemd 247) is the last release affected; Debian 12+
  (systemd 252+) are not.

#### Reproduction strategy

To reproduce Failure Mechanism 1, use one of:

1. **Debian 11 container/chroot on AMD hardware** (pre-6.17 kernel)
   — both `std::random_device` (GCC 10.2) and `libnss_systemd`
   (systemd 247) will use RDRAND, and `rr` cannot suppress the
   CPUID flag.
2. **Any Debian on AMD with a pre-6.17 kernel** —
   `std::random_device` will hit RDRAND through the CPUID-gated
   path, but `rr`'s CPUID suppression is inoperative without CPUID
   faulting.

**Source:** `exp1_random_device.cpp` (already written; requires
AMD hardware or older environment)

---

## Nice-to-have

### Experiment 6: Rust `target-feature=+rdrand` demonstration (Section 4.2)

**Validates:** Claim that Rust's `target-feature=+rdrand` causes
RDRAND to be emitted without a runtime CPUID check.

A minimal Rust program uses `core::arch::x86_64` intrinsics with
a `#[cfg(target_feature = "rdrand")]` guard, mirroring the
pattern used by the `getrandom` crate on `no_std`/SGX targets
and by `ring` on SGX.

**Note:** The original plan used the `getrandom` crate directly,
but on Linux it always uses the `getrandom(2)` syscall — the
RDRAND code path only applies to `no_std` and SGX targets. The
revised program uses raw intrinsics with the same `cfg` pattern
to demonstrate the compile-time behavior directly.

**Procedure:**

```bash
cd exp_rust

# Default: runtime CPUID check before RDRAND
cargo build --release
objdump -d target/release/exp_rust | grep -E 'cpuid|rdrand'

# With target-feature: RDRAND only, no CPUID
RUSTFLAGS="-C target-feature=+rdrand" cargo build --release
objdump -d target/release/exp_rust | grep -E 'cpuid|rdrand'
```

**Results (confirmed):**

Default build (1 CPUID + 1 RDRAND call):
```
143d0: rdrand %rcx        # _rdrand64_step function
14458: cpuid               # runtime feature check
14468: call 143d0          # calls RDRAND only if CPUID says OK
```

`+rdrand` build (4 RDRAND, zero CPUID — loop unrolled):
```
143be: rdrand %rax
1442e: rdrand %rax
1449e: rdrand %rax
1450e: rdrand %rax
```

The `+rdrand` build eliminates the CPUID check entirely at
compile time and the optimizer unrolls the loop, emitting four
bare RDRAND instructions. This confirms that
`-C target-feature=+rdrand` bypasses `rr`'s CPUID suppression
mitigation in Rust binaries, just as `-mrdrnd` does in C/C++.

**Source:** `exp_rust/` (Rust project, written)

### Experiment 7: Divergence severity spectrum

**Validates:** Forensic impact varies — some divergences are silent
and misleading, which is worse than a crash.

Build several programs where RDRAND output feeds into progressively
more consequential decisions:

- **Benign**: RDRAND seeds a hash table randomization (ordering
  changes, but correctness is preserved)
- **Moderate**: RDRAND picks a branch in a conditional (different
  code path on replay)
- **Severe**: RDRAND generates a key used to encrypt output (replay
  output is completely different)

Record and replay each under `rr`. Measure divergence as: identical
replay, different output but same control flow, or completely
different execution.

### Experiment 8: Cross-version comparison

**Validates:** Section 4.1's historical claims about library RDRAND
usage across Debian releases.

Using containers for Debian 11 and 13, `strace` the
`std::random_device` program and show that Debian 11's libstdc++
calls RDRAND (via CPUID-gated path) while Debian 13's calls
`getrandom()`. This doesn't require `rr` or AMD hardware — it just
confirms the version-specific behavior.

---

## What NOT to attempt

- **ARM experiments**: The paper's ARM claims are architectural.
  WSDF reviewers won't expect ARM replay infrastructure.
- **Working hypervisor prototype**: That's the dissertation, not
  this paper.
- **Adversarial malware samples**: Interesting but out of scope.
  The Santos-Filho and D'Elia citations carry the argument.
- **Legal/Daubert analysis**: Section 5.3 is speculative and
  reviewers will accept that for a workshop paper.

---

## Summary

| Priority | Experiment | Validates | Status |
|----------|-----------|-----------|--------|
| Must-have | 1. Replay divergence (`-mrdrnd`) | Section 4.2, 4.3 | Code ready (`exp2_intrinsic.c`) |
| Must-have | 2. Prevalence scan | Section 4.2 "growing scope" | Tool ready (`scan_rdrand.sh`) |
| Must-have | 3. Silent divergence | Section 5.1 | Needs `exp3_silent.c` |
| Should-have | 4. `-march=native` confirmation | Section 4.2 (Gentoo) | One-liner |
| Should-have | 5. Library-path divergence | Section 4.1 | Code ready; needs AMD/old env |
| Nice-to-have | 6. Rust `target-feature` demo | Section 4.2 (Rust) | Needs Rust project |
| Nice-to-have | 7. Severity spectrum | Section 5.1 | Needs code |
| Nice-to-have | 8. Cross-version comparison | Section 4.1 | Needs containers |

The three must-have experiments fit in ~2–3 pages of the paper and
keep it within the 18-page LNCS limit. Experiments 1 and 2 can be
run today with existing code.
