/*
 * Experiment 2: Compile-time RDRAND emission bypass
 *
 * Uses the _rdrand64_step() intrinsic directly. When compiled
 * with -mrdrnd, the compiler defines __RDRND__ and emits
 * RDRAND without any preceding CPUID check, bypassing rr's
 * CPUID suppression mitigation entirely.
 *
 * Build (intrinsic path -- no CPUID check):
 *   gcc -O2 -mrdrnd -o exp2_intrinsic exp2_intrinsic.c
 *
 * Build (control -- runtime CPUID check):
 *   gcc -O2 -o exp2_control exp2_intrinsic.c
 *
 * Verify no CPUID before RDRAND in the intrinsic build:
 *   objdump -d exp2_intrinsic | grep -E 'rdrand|cpuid'
 *
 * Run under rr:
 *   rr record ./exp2_intrinsic 2>record.log
 *   rr replay -a 2>replay.log
 *   diff record.log replay.log
 *
 * Expected: the intrinsic build diverges on replay; the
 * control build (if CPUID faulting is available) does not.
 */

#include <stdint.h>
#include <stdio.h>

#ifdef __RDRND__

/*
 * Compiled with -mrdrnd: the intrinsic emits RDRAND directly
 * with no runtime feature check.
 */
#include <immintrin.h>

static int get_hw_random(uint64_t *val)
{
    return _rdrand64_step((unsigned long long *)val);
}

#else

/*
 * Control path: check CPUID at runtime before using RDRAND.
 * This is the "well-behaved" pattern that rr can mitigate
 * by suppressing the RDRAND CPUID feature flag.
 */
#include <cpuid.h>

static int get_hw_random(uint64_t *val)
{
    unsigned int eax, ebx, ecx, edx;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return 0;

    /* ECX bit 30 = RDRAND support */
    if (!(ecx & (1u << 30))) {
        fprintf(stderr, "RDRAND not available (CPUID)\n");
        return 0;
    }

    /*
     * Use inline asm since we don't have -mrdrnd and
     * cannot use the intrinsic.
     */
    unsigned char ok;
    __asm__ __volatile__(
        "rdrand %0\n\t"
        "setc   %1\n\t"
        : "=r"(*val), "=qm"(ok)
        :
        : "cc"
    );
    return ok;
}

#endif /* __RDRND__ */

int main(void)
{
    uint64_t vals[10];
    int i;

#ifdef __RDRND__
    fprintf(stderr, "mode: intrinsic (no CPUID check)\n");
#else
    fprintf(stderr, "mode: control (CPUID check first)\n");
#endif

    fprintf(stderr, "=== RDRAND values ===\n");
    for (i = 0; i < 10; i++) {
        if (get_hw_random(&vals[i]))
            fprintf(stderr, "val[%d] = 0x%016lx\n",
                    i, (unsigned long)vals[i]);
        else
            fprintf(stderr, "val[%d] = FAILED\n", i);
    }

    /*
     * Use the values to influence control flow, so
     * divergence has behavioral consequences.
     */
    uint64_t sum = 0;
    for (i = 0; i < 10; i++)
        sum ^= vals[i];

    fprintf(stderr, "xor = 0x%016lx\n", (unsigned long)sum);
    fprintf(stderr, "branch: %s\n", (sum & 1) ? "odd" : "even");

    return 0;
}
