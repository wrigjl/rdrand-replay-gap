/*
 * Experiment 3: Silent divergence demonstration
 *
 * Demonstrates the forensic danger of unintercepted RDRAND:
 * replay succeeds with exit code 0 and no rr errors, but
 * produces completely different output because RDRAND
 * returned different values during replay.
 *
 * The program uses RDRAND to generate a 256-bit key, then
 * XOR-encrypts a fixed plaintext message. The ciphertext is
 * written to stdout. On faithful replay the ciphertext would
 * be identical; under rr without RDRAND interception, the
 * key differs and the ciphertext is completely different.
 *
 * IMPORTANT: The program is carefully written so that the
 * RDRAND values affect only *data*, not *control flow*.
 * This ensures the retired instruction count (ticks) is
 * identical between recording and replay, preventing rr
 * from detecting the divergence via its tick-count check
 * at syscall boundaries.  The result is a truly silent
 * divergence: rr exits 0, reports no errors, but the
 * replayed execution produced different data.
 *
 * Build:
 *   gcc -O2 -mrdrnd -o exp3_silent exp3_silent.c
 *
 * Run:
 *   rr record ./exp3_silent > record_output.bin 2>record.log
 *   rr replay -a > replay_output.bin 2>replay.log
 *   diff record_output.bin replay_output.bin
 *
 * Expected: exit code 0 both times, no rr errors, but
 * record_output.bin and replay_output.bin differ completely.
 * An analyst examining the replay would have no indication
 * that the trace was unfaithful.
 */

#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Forensically plausible plaintext: a simulated log entry
 * that an investigator might expect to see encrypted in a
 * network capture or disk artifact.
 */
static const char plaintext[] =
    "2026-03-25T14:30:00Z session_id=a3f7 "
    "user=admin action=export_database "
    "src=10.0.1.50 dst=203.0.113.42 "
    "status=success bytes=15728640";

/*
 * Generate a 256-bit key from RDRAND.  No CPUID check
 * because we compile with -mrdrnd.
 *
 * Unconditionally stores four 64-bit values.  Does not
 * branch on the RDRAND carry flag -- this is critical
 * to keep the instruction count identical across runs.
 * (In real code you would check CF; here we deliberately
 * do not, to ensure the divergence is purely in data.)
 */
static void generate_key(uint64_t key[4])
{
    unsigned long long v;
    int i;

    for (i = 0; i < 4; i++) {
        _rdrand64_step(&v);   /* ignore CF */
        key[i] = (uint64_t)v;
    }
}

/*
 * XOR-encrypt buf in place using key material.  Simple
 * repeating-key XOR -- not real crypto, but sufficient to
 * demonstrate that different keys produce different output.
 */
static void xor_encrypt(uint8_t *buf, size_t len,
                         const uint64_t key[4])
{
    const uint8_t *kb = (const uint8_t *)key;
    size_t i;

    for (i = 0; i < len; i++)
        buf[i] ^= kb[i % 32];
}

/*
 * Print a hex dump to fd without using fprintf (which may
 * have data-dependent buffering behavior).  Fixed-length
 * output ensures identical instruction count regardless
 * of data values.
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
    uint64_t key[4];
    uint8_t buf[sizeof(plaintext) - 1];
    size_t len = sizeof(plaintext) - 1;

    generate_key(key);

    /* Log key as hex to stderr */
    write(STDERR_FILENO, "key: ", 5);
    write_hex(STDERR_FILENO, (const uint8_t *)key, 32);

    /* Encrypt and write ciphertext hex to stdout */
    memcpy(buf, plaintext, len);
    xor_encrypt(buf, len, key);

    write(STDOUT_FILENO, "ciphertext: ", 12);
    write_hex(STDOUT_FILENO, buf, len);

    return 0;
}
