/*
 * Experiment 4: std::random_device behavior under rr
 *
 * Originally written to demonstrate silent replay divergence
 * via libstdc++'s std::random_device RDRAND/RDSEED fast-path.
 * Live re-runs on Debian 13 / GCC 14 / libstdc++.so.6 show
 * that the *default-token* constructor (used here) actually
 * routes through getrandom(2) rather than the RDRAND/RDSEED
 * function-pointer dispatch documented in older libstdc++.
 * RDRAND/RDSEED instructions are still present in
 * libstdc++.so.6, but reaching them requires an *explicit*
 * "rdrand" / "rdseed" token (see exp7_stdrand.cpp), and those
 * callsites are caught by rr's instruction patching of known
 * library entry points -- the program replays faithfully on
 * both Intel and AMD with this libstdc++.
 *
 * The default-token behavior is therefore now a calibration
 * point ("this dispatch goes through a syscall, so rr replays
 * it") rather than a divergence demo.  The actual silent-
 * divergence demonstrations are exp3_silent.c (inline RDRAND
 * with -mrdrnd in user code) and exp-rust with
 * target-feature=+rdrand.
 *
 * Note: OpenSSL 3.x RAND_bytes() also does NOT exhibit
 * divergence because its DRBG is seeded via getrandom(2).
 *
 * Build:
 *   g++ -O2 -o exp4_stdrand exp4_stdrand.cpp
 *
 * Run:
 *   rr record ./exp4_stdrand > rec.txt 2>rec.log
 *   rr replay -a > rep.txt 2>rep.log
 *   diff rec.txt rep.txt
 *
 * Expected on Debian 13 / GCC 14: rec.txt and rep.txt match
 * (faithful) because the default-token dispatch goes through
 * getrandom(2), which rr records and replays identically.
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
