/*
 * Experiment 4: Silent divergence via std::random_device
 *
 * Demonstrates that rr fails to replay faithfully when the
 * C++ standard library's std::random_device uses RDRAND or
 * RDSEED internally.  On AMD hardware without CPUID faulting
 * (Linux < 6.17), rr cannot suppress libstdc++'s CPUID
 * feature detection during std::random_device construction.
 * The library's _M_init dispatch sets a function pointer to
 * the RDSEED or RDRAND path; each call to operator()() draws
 * entropy from hardware directly with no syscall.  rr has no
 * mechanism to intercept RDRAND or RDSEED; the recorded and
 * replayed outputs therefore differ silently.
 *
 * Unlike exp3_silent.c (which uses inline RDRAND assembly
 * with -mrdrnd), this program contains no RDRAND intrinsics
 * and requires no special compiler flags.  The hardware
 * instruction is reached through the standard library's
 * normal runtime dispatch.
 *
 * Note: OpenSSL 3.x RAND_bytes() does NOT exhibit this
 * divergence because its DRBG is seeded via getrandom(2),
 * a syscall that rr records and replays identically.  The
 * std::random_device path is different: it exposes RDRAND
 * directly as a random number source without DRBG buffering.
 *
 * Build:
 *   g++ -O2 -o exp4_stdrand exp4_stdrand.cpp
 *
 * Run on AMD (Linux < 6.17, no CPUID faulting):
 *   rr record ./exp4_stdrand > rec.txt 2>rec.log
 *   rr replay -a > rep.txt 2>rep.log
 *   diff rec.txt rep.txt
 *
 * Expected: exit code 0 both times, no rr errors, but rec.txt
 * and rep.txt differ because std::random_device drew different
 * RDRAND/RDSEED values on recording vs. replay.
 */

#include <cstdint>
#include <cstring>
#include <random>
#include <unistd.h>

static const char plaintext[] =
    "2026-03-25T14:30:00Z session_id=a3f7 "
    "user=admin action=export_database "
    "src=10.0.1.50 dst=203.0.113.42 "
    "status=success bytes=15728640";

/*
 * Fixed-length hex output via raw write().  No data-dependent
 * branching: instruction count is identical regardless of
 * the key material, keeping the divergence silent from rr's
 * tick-count consistency check.
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
    std::random_device rd;
    uint8_t key[32];
    uint8_t buf[sizeof(plaintext) - 1];
    size_t len = sizeof(plaintext) - 1;
    int i;

    /*
     * Collect 32 bytes of key material from std::random_device.
     * operator()() returns uint_fast32_t; eight calls give 32
     * bytes.  On AMD without CPUID faulting, libstdc++ routes
     * each call directly to RDSEED or RDRAND with no syscall.
     */
    for (i = 0; i < 8; i++) {
        uint32_t v = rd();
        key[i * 4 + 0] = (uint8_t)(v >>  0);
        key[i * 4 + 1] = (uint8_t)(v >>  8);
        key[i * 4 + 2] = (uint8_t)(v >> 16);
        key[i * 4 + 3] = (uint8_t)(v >> 24);
    }

    write(STDERR_FILENO, "key: ", 5);
    write_hex(STDERR_FILENO, key, sizeof(key));

    memcpy(buf, plaintext, len);
    for (i = 0; i < (int)len; i++)
        buf[i] ^= key[i % 32];

    write(STDOUT_FILENO, "ciphertext: ", 12);
    write_hex(STDOUT_FILENO, buf, len);

    return 0;
}
