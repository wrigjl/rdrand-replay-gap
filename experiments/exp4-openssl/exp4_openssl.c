/*
 * Experiment 4: Silent divergence via a real-world library
 *
 * Demonstrates that rr fails to replay faithfully when a
 * production cryptographic library (OpenSSL libcrypto) uses
 * RDRAND internally.  On AMD hardware without CPUID faulting
 * (Linux < 6.17), rr cannot suppress OpenSSL's CPUID feature
 * detection during library initialization.  OPENSSL_ia32_cpuid
 * runs unconstrained, sets the RDRAND capability bit, and
 * subsequent RAND_bytes() calls draw entropy from RDRAND
 * directly.  rr has no mechanism to intercept RDRAND; the
 * recorded and replayed outputs therefore differ silently.
 *
 * Unlike exp3_silent.c, this program contains no RDRAND
 * inline assembly and requires no special compiler flags.
 * All RDRAND instructions reside inside the production
 * libcrypto.so.  The divergence arises from an unmodified
 * real-world library, not a synthetic test harness.
 *
 * Build:
 *   gcc -O2 -o exp4_openssl exp4_openssl.c -lcrypto
 *
 * Run on AMD (Linux < 6.17, no CPUID faulting):
 *   rr record ./exp4_openssl > rec.txt 2>rec.log
 *   rr replay -a > rep.txt 2>rep.log
 *   diff rec.txt rep.txt
 *
 * Expected: exit code 0 both times, no rr errors, but rec.txt
 * and rep.txt differ because RAND_bytes() drew different RDRAND
 * values on recording vs. replay.  An analyst examining the
 * replay would observe a successful encryption operation with
 * no anomalies, yet the key and ciphertext bear no relation to
 * the original execution.
 */

#include <openssl/rand.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/*
 * Same forensically plausible plaintext as exp3_silent.c so
 * results are directly comparable between the two experiments.
 */
static const char plaintext[] =
    "2026-03-25T14:30:00Z session_id=a3f7 "
    "user=admin action=export_database "
    "src=10.0.1.50 dst=203.0.113.42 "
    "status=success bytes=15728640";

/*
 * XOR-encrypt buf in place using a repeating key.  No branching
 * on data values: instruction count is identical regardless of
 * the key material, keeping the divergence silent from rr's
 * tick-count consistency check.
 */
static void xor_encrypt(uint8_t *buf, size_t len,
                         const uint8_t *key, size_t keylen)
{
    size_t i;

    for (i = 0; i < len; i++)
        buf[i] ^= key[i % keylen];
}

/*
 * Fixed-length hex output via raw write().  Using printf or
 * fprintf here would introduce data-dependent buffering
 * behavior; raw write() with a fixed stride keeps the
 * instruction count invariant across key values.
 */
static void write_hex(int fd, const uint8_t *buf, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    char out[3];
    size_t i;

    out[2] = ' ';
    for (i = 0; i < len; i++) {
        out[0] = hex[buf[i] >> 4];
        out[1] = hex[buf[i] & 0x0f];
        write(fd, out, 3);
    }
    write(fd, "\n", 1);
}

int main(void)
{
    uint8_t key[32];
    uint8_t buf[sizeof(plaintext) - 1];
    size_t len = sizeof(plaintext) - 1;

    /*
     * RAND_bytes() is the standard entry point for
     * cryptographic randomness in OpenSSL-linked programs.
     * On AMD without CPUID faulting, libcrypto's internal
     * RDRAND provider is active and this call draws entropy
     * directly from RDRAND -- no syscall, no interception.
     */
    if (RAND_bytes(key, sizeof(key)) != 1)
        return 1;

    write(STDERR_FILENO, "key: ", 5);
    write_hex(STDERR_FILENO, key, sizeof(key));

    memcpy(buf, plaintext, len);
    xor_encrypt(buf, len, key, sizeof(key));

    write(STDOUT_FILENO, "ciphertext: ", 12);
    write_hex(STDOUT_FILENO, buf, len);

    return 0;
}
