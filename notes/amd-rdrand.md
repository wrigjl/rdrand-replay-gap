# RDSEED Retry-Count Non-Determinism on AMD Zen Under rr

## Summary

On AMD Zen hardware without CPUID faulting support (Linux < 6.17), rr's
replay of statically-linked C++ programs using `std::random_device` fails
with a tick-count mismatch rather than silent data divergence. The
mismatch traces to a second, distinct form of hardware entropy
non-determinism: **RDSEED retry-count non-determinism**, which is
architecturally separate from the RDRAND value non-determinism that is
the paper's central subject.

---

## Background: Two Forms of Entropy Non-Determinism

RDRAND and RDSEED differ in their failure behavior:

- **RDRAND** draws from an internal DRNG that is continuously reseeded.
  On modern AMD Zen hardware it almost never returns CF=0 (failure);
  the principal non-determinism is in the *value* returned.

- **RDSEED** draws from the CPU's conditioned hardware entropy pool
  (the "Conditioning Component" in SP 800-90B terms). That pool has a
  finite regeneration rate. Under high entropy demand -- concurrent
  processes calling RDSEED, OS entropy pool initialization, etc. --
  RDSEED *can* return CF=0 (no seed available), requiring a retry.
  The number of retries is non-deterministic across runs.

Both forms are invisible to rr's syscall-recording model: neither
RDRAND nor RDSEED is a syscall, and on AMD without CPUID faulting rr
cannot suppress the CPUID RDRAND feature bit to redirect them.

---

## Experimental Evidence

### Environment

- CPU: AMD Ryzen 9 7940HS (Zen 4, Family 25 Model 116)
- Kernel: Linux 6.12.74jlw (no AMD CPUID faulting; `arch_prctl(ARCH_GET_CPUID)` returns ENOSYS)
- rr: 5.9.0
- Binary: `exp4_stdrand.cpp` compiled with `g++ -O2 -static`

### Observed Failure

```
rr record ./exp4_stdrand_static   # succeeds, records ciphertext
rr replay -a                      # FATAL: tick-count mismatch
```

rr error:
```
[FATAL ./src/ReplaySession.cc:1265:check_ticks_consistency()]
  -> Assertion `ticks_now == trace_ticks' failed to hold.
     ticks mismatch for 'SYSCALL: write'; expected 45692, got 45677
```

Tick difference: **45692 − 45677 = 15**.

### The RDSEED Retry Loop

libstdc++ (statically linked) provides `__x86_rdseedEPv` at 0x4038b0:

```asm
4038b8:  rdseed %edx             ; first attempt
4038bf:  jae    4038d0           ; CF=0 (FAILED) → jump to retry loop
4038c1:  mov    0xc(%rsp),%eax
4038c5:  add    $0x18,%rsp
4038c9:  ret                     ; success path

; --- retry loop ---
4038d0:  mov    %rdi,%rax
4038d3:  mov    $0x63,%esi       ; max 99 retries
4038d8:  lea    0xc(%rsp),%rcx
4038dd:  pause                   ; yield
4038df:  rdseed %edx             ; retry
4038e2:  mov    %edx,(%rcx)
4038e4:  jb     4038c1           ; CF=1 (SUCCESS) → return
4038e6:  sub    $0x1,%esi        ; decrement counter
4038e9:  jne    4038dd           ; loop if not exhausted
```

### Branch Counting

rr counts **retired conditional branches** as ticks (via the AMD PMU
event equivalent of Intel's `BR_INST_RETIRED.CONDITIONAL`).

**Happy path** (RDSEED succeeds on first try):
- `jae` at 4038bf: not taken (CF=1) — 1 branch

**Failure path** (first attempt fails, then k retries before success):
- `jae` at 4038bf: taken (CF=0 fail) — 1 branch
- Per failing retry iteration: `jb` not-taken + `jne` taken — 2 branches
- Final successful retry: `jb` taken — 1 branch
- Total: 1 + 2·(k−1) + 1 = **2k branches**

Difference from happy path: **2k − 1 extra branches**.

Solving for the observed 15-tick discrepancy:

    2k − 1 = 15  →  k = 8 retry iterations

So the recording run needed approximately **8 RDSEED retries** (or some
combination of multiple RDSEED calls each needing retries) that the
replay run did not — or vice versa.

### Why RDSEED Fails Intermittently on AMD Zen

AMD's RDSEED instruction draws from the CPU's hardware conditioned
entropy source. The entropy pool has a finite fill rate dictated by
the hardware noise source (thermal noise, ring oscillator jitter, etc.).
Under concurrent entropy demand — from the OS entropy subsystem,
other processes, or rapid sequential RDSEED calls — the pool can be
temporarily exhausted, causing RDSEED to return CF=0.

This behavior is documented: AMD's architecture manuals specify that
software must handle CF=0 returns. The libstdc++ implementation does
so correctly (up to 99 retries with `pause` between each). But the
*number* of retries is a function of the hardware entropy pool state
at the moment of execution — state that rr cannot record because it
is not exposed through any syscall interface.

---

## Two Distinct Non-Determinism Classes

This experiment reveals that the RDRAND/RDSEED problem for replay
systems has **two distinct non-determinism axes**:

| Class | Instruction | What varies | rr detects? | Forensic impact |
|-------|-------------|-------------|-------------|-----------------|
| Value | RDRAND, RDSEED | The value returned (when CF=1) | No (silent) | Wrong data in replay |
| Retry count | RDSEED | Number of CF=0 failures before CF=1 | Yes (tick mismatch) | Replay aborts entirely |

The **value** non-determinism is what exp3_silent.c demonstrates:
RDRAND always succeeds (CF=1) but returns a different value each call,
affecting only data — rr cannot detect this.

The **retry-count** non-determinism is what the static build reveals:
RDSEED sometimes needs retries, and each retry adds conditional
branches to the execution stream — rr detects this via tick-count
consistency checks, but the detection causes the replay to abort
rather than produce wrong output.

Both forms are forensically damaging:
- Silent value divergence: analyst receives a faithfully-terminated
  replay that shows the wrong program state.
- Detected retry-count divergence: analyst cannot complete replay at
  all, losing access to the recorded execution entirely.

---

## Implications for the Paper

The tick-count mismatch in the AMD static-build experiment is not
merely an artifact of rr's implementation — it is evidence of a
second, deeper non-determinism. A replay system that did not check
tick counts (or used a different consistency check) might still fail
to faithfully replay, because RDSEED-derived values would differ
between recording and replay on any run where the retry count
differed.

The distinction matters for paper framing: the paper currently
discusses RDRAND value non-determinism as the primary concern. The
RDSEED retry-count non-determinism is an additional, independent
gap that has received less attention in the literature.

---

## Reproduction

```bash
# Build
g++ -O2 -static -o exp4_stdrand_static exp4_stdrand.cpp

# Confirm RDSEED is in the binary
objdump -d exp4_stdrand_static | grep -c "rdseed"

# Record (succeeds)
rr record ./exp4_stdrand_static > rec.txt 2>rec.log
echo "exit: $?"; cat rec.txt | head -1

# Replay (crashes with tick mismatch)
rr replay -a > rep.txt 2>rep.log
echo "exit: $?"; head -3 rep.log
```

Expected: record exits 0 with ciphertext output; replay crashes with
`ticks mismatch for 'SYSCALL: write'`.

The exact tick delta (15 in our run) will vary across machines and
system load, but will be a small positive integer consistent with
1–10 RDSEED retry iterations.
