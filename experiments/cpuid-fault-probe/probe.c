/*
 * probe.c -- Detect CPUID-faulting support on Intel and AMD CPUs.
 *
 * CPUID-faulting is what lets rr (and any other process-level
 * replay tool) intercept guest CPUID and substitute a deterministic
 * answer.  Intel has supported it since Ivy Bridge (2012);
 * AMD added it in firmware/kernel in 2024-2025 and Linux 6.17
 * was the first stable kernel to enable it generically on AMD.
 *
 * Three layers to test, top-down:
 *
 *   1. /proc/cpuinfo  -- the kernel's normalized verdict.  If the
 *                        "cpuid_fault" flag appears in the flags
 *                        line, the kernel both knows how to detect
 *                        CPUID-faulting on this CPU *and* has
 *                        accepted it as supported.
 *
 *   2. arch_prctl()   -- runtime per-process check.  Both
 *                        ARCH_GET_CPUID (current state) and a
 *                        no-op ARCH_SET_CPUID (capability probe).
 *                        ARCH_SET_CPUID returning -ENODEV means
 *                        the kernel believes this CPU does not
 *                        support faulting -- regardless of what
 *                        /proc/cpuinfo claims.
 *
 *   3. Hardware-direct -- bypasses the kernel entirely.
 *                        Intel: MSR_PLATFORM_INFO (0xCE) bit 31
 *                               is the "CPUID Faulting Capable"
 *                               bit; reading requires the msr
 *                               module and root.
 *                        AMD:   CPUID.0x80000021.EAX has the
 *                               extended-feature bits in the
 *                               range where AMD added their
 *                               CPUID-faulting indicator.  Bit
 *                               position has shifted between
 *                               revisions, so the probe prints
 *                               the full leaf and points at the
 *                               Linux kernel arch/x86/kernel/cpu/
 *                               amd.c for the canonical decode.
 *
 * Build:
 *     gcc -O2 -o probe probe.c
 *
 * Usage:
 *     ./probe            # most useful information
 *     sudo ./probe       # also reads MSR_PLATFORM_INFO on Intel
 *                          (requires `modprobe msr` first)
 *
 * Exit status:
 *     0 = ran to completion (regardless of what was found)
 *     1 = could not determine vendor or other fatal error
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <asm/prctl.h>

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static int read_msr(int cpu, uint32_t msr, uint64_t *val)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int ok = (pread(fd, val, 8, msr) == 8) ? 0 : -1;
    int saved = errno;
    close(fd);
    errno = saved;
    return ok;
}

static int check_proc_cpuinfo(void)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        printf("  [could not open /proc/cpuinfo: %s]\n", strerror(errno));
        return -1;
    }
    char line[8192];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "flags", 5) != 0)
            continue;
        /* Match " cpuid_fault " or " cpuid_fault\n" -- avoid
         * spurious substring hits.
         */
        char *p = strstr(line, "cpuid_fault");
        while (p) {
            char before = (p == line) ? ' ' : *(p - 1);
            char after  = *(p + 11);
            if (before == ' ' && (after == ' ' || after == '\n' || after == 0)) {
                found = 1;
                break;
            }
            p = strstr(p + 1, "cpuid_fault");
        }
        break;
    }
    fclose(f);
    return found;
}

int main(void)
{
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = 0;
    int is_intel = (strcmp(vendor, "GenuineIntel") == 0);
    int is_amd   = (strcmp(vendor, "AuthenticAMD") == 0);

    /* Brand string */
    uint32_t brand[12] = {0};
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_ext_leaf = eax;
    if (max_ext_leaf >= 0x80000004) {
        cpuid(0x80000002, 0, &brand[0], &brand[1], &brand[2],  &brand[3]);
        cpuid(0x80000003, 0, &brand[4], &brand[5], &brand[6],  &brand[7]);
        cpuid(0x80000004, 0, &brand[8], &brand[9], &brand[10], &brand[11]);
    }

    printf("vendor:          %s\n", vendor);
    printf("brand:           %.48s\n", (char *)brand);
    printf("max ext leaf:    0x%08x\n", max_ext_leaf);
    printf("\n");

    if (!is_intel && !is_amd) {
        fprintf(stderr, "Unrecognized CPU vendor %s; bailing.\n", vendor);
        return 1;
    }

    /* === Layer 1: /proc/cpuinfo === */
    printf("Layer 1: /proc/cpuinfo flags\n");
    int proc_flag = check_proc_cpuinfo();
    if (proc_flag < 0) {
        printf("  cpuid_fault: unknown (proc read failed)\n");
    } else {
        printf("  cpuid_fault: %s\n",
               proc_flag ? "PRESENT (kernel reports CPUID-faulting)"
                         : "absent  (kernel does NOT report CPUID-faulting)");
    }
    printf("\n");

    /* === Layer 2: arch_prctl === */
    printf("Layer 2: arch_prctl(ARCH_GET_CPUID / ARCH_SET_CPUID)\n");
    long get_ret = syscall(SYS_arch_prctl, ARCH_GET_CPUID);
    if (get_ret < 0) {
        printf("  ARCH_GET_CPUID: -1, errno=%d (%s)\n",
               errno, strerror(errno));
        printf("    -> kernel/CPU does not expose CPUID-faulting at all\n");
    } else {
        printf("  ARCH_GET_CPUID returned %ld\n", get_ret);
        printf("    -> CPUID is currently %s for this process\n",
               get_ret ? "ALLOWED (not faulted)" : "FAULTED");
    }

    /* No-op SET: try to set CPUID to its current value.  If the
     * kernel knows about CPUID-faulting on this CPU it returns 0;
     * if not it returns -ENODEV.  This distinguishes "kernel
     * accepts the syscall but the CPU has no support" from "the
     * CPU has full support."
     */
    long set_ret = syscall(SYS_arch_prctl, ARCH_SET_CPUID,
                           (unsigned long)(get_ret < 0 ? 1 : get_ret));
    if (set_ret == 0) {
        printf("  ARCH_SET_CPUID (no-op): success\n");
        printf("    -> kernel believes this CPU supports CPUID-faulting\n");
    } else {
        printf("  ARCH_SET_CPUID (no-op): -1, errno=%d (%s)\n",
               errno, strerror(errno));
        if (errno == ENODEV) {
            printf("    -> kernel says this CPU has no CPUID-faulting support\n");
        } else if (errno == ENOSYS || errno == EINVAL) {
            printf("    -> kernel predates the cpuid-mode syscall interface\n");
        }
    }
    printf("\n");

    /* === Layer 3: hardware-direct === */
    if (is_intel) {
        printf("Layer 3: Intel MSR_PLATFORM_INFO (0xCE)\n");
        uint64_t msr;
        if (read_msr(0, 0xCE, &msr) == 0) {
            printf("  MSR_PLATFORM_INFO = 0x%016lx\n", (unsigned long)msr);
            int cap = (msr >> 31) & 1;
            printf("  Bit 31 (CPUID Faulting Capable): %s\n",
                   cap ? "SET (hardware supports CPUID-faulting)"
                       : "CLEAR (hardware does NOT support CPUID-faulting)");
        } else {
            printf("  cannot read /dev/cpu/0/msr: %s\n", strerror(errno));
            printf("  Try: sudo modprobe msr; sudo ./probe\n");
        }
    }

    if (is_amd) {
        printf("Layer 3: AMD CPUID 0x80000021\n");
        if (max_ext_leaf < 0x80000021) {
            printf("  Max extended leaf = 0x%08x; 0x80000021 not present.\n",
                   max_ext_leaf);
            printf("  -> CPU predates the AMD revision that added the\n");
            printf("     CPUID-faulting indicator.\n");
        } else {
            cpuid(0x80000021, 0, &eax, &ebx, &ecx, &edx);
            printf("  EAX = 0x%08x\n", eax);
            printf("  EBX = 0x%08x\n", ebx);
            printf("  ECX = 0x%08x\n", ecx);
            printf("  EDX = 0x%08x\n", edx);
            printf("\n");
            printf("  AMD documents this leaf as \"Extended Feature\n");
            printf("  Identification 2\".  The exact bit position of the\n");
            printf("  CPUID-faulting indicator has shifted between\n");
            printf("  revisions.  The authoritative decode lives in\n");
            printf("  Linux kernel arch/x86/kernel/cpu/amd.c; the kernel's\n");
            printf("  consolidated answer is the /proc/cpuinfo flag above.\n");
        }
    }
    printf("\n");

    /* === Summary === */
    printf("Summary\n");
    int kernel_supports = (proc_flag == 1) || (set_ret == 0);
    int kernel_excludes = (proc_flag == 0) && (set_ret != 0 && errno == ENODEV);
    if (kernel_supports) {
        printf("  This kernel + CPU combination DOES support CPUID-faulting.\n");
        printf("  rr's CPUID-suppression mitigation can run here.\n");
    } else if (kernel_excludes) {
        printf("  This kernel + CPU combination does NOT support CPUID-faulting.\n");
        printf("  rr cannot intercept CPUID; any RDRAND callsite that depends\n");
        printf("  on a runtime CPUID check will execute the hardware instruction\n");
        printf("  unconstrained.  See paper section 4.3.\n");
    } else {
        printf("  Mixed signals (see layers above); inspect manually.\n");
    }
    return 0;
}
