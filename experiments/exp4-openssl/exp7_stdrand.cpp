/*
 * exp7_stdrand.cpp -- test whether std::random_device uses RDRAND
 *
 * On libstdc++ (GCC 15+), std::random_device{"default"} checks
 * CPUID at construction time: if the CPU supports RDRAND, the
 * default token selects RDRAND as the entropy source.  This means
 * every C++ program using std::random_device{} on a system where
 * libstdc++ was compiled with -march=native (e.g., Gentoo) will
 * execute RDRAND on modern Intel/AMD hardware.
 *
 * Build:
 *   g++ -std=c++17 -O2 -o exp7_stdrand exp7_stdrand.cpp
 *
 * Run directly:
 *   ./exp7_stdrand
 *
 * Run under rr:
 *   rr record ./exp7_stdrand
 *   rr replay 2>&1 | grep val
 *
 * Expected: values differ between direct run and replay if rr
 * does not intercept the RDRAND path.  If rr suppresses the
 * CPUID RDRAND bit, libstdc++ falls back to arc4random/getentropy
 * and replay is faithful (but uses a different entropy source
 * than the original recording).
 */

#include <cstdio>
#include <random>

int main()
{
    /*
     * Default constructor: libstdc++ _M_init checks CPUID and
     * selects RDRAND if available, otherwise arc4random/getentropy.
     */
    std::random_device rd;

    fprintf(stderr, "token: (default)\n");
    for (int i = 0; i < 8; i++) {
        unsigned int v = rd();
        fprintf(stderr, "val[%d] = 0x%08x\n", i, v);
    }

    /*
     * Explicit "rdrand" token: forces RDRAND, throws if
     * CPUID says unsupported.
     */
    try {
        std::random_device rd_hw("rdrand");
        fprintf(stderr, "\ntoken: rdrand\n");
        for (int i = 0; i < 4; i++) {
            unsigned int v = rd_hw();
            fprintf(stderr, "val[%d] = 0x%08x\n", i, v);
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "\ntoken: rdrand -> exception: %s\n",
                e.what());
    }

    /*
     * Explicit "rdseed" token: forces RDSEED, throws if
     * CPUID says unsupported.
     */
    try {
        std::random_device rd_seed("rdseed");
        fprintf(stderr, "\ntoken: rdseed\n");
        for (int i = 0; i < 4; i++) {
            unsigned int v = rd_seed();
            fprintf(stderr, "val[%d] = 0x%08x\n", i, v);
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "\ntoken: rdseed -> exception: %s\n",
                e.what());
    }

    return 0;
}
