/*
 * Experiment 1: Library-path divergence
 *
 * Demonstrates that std::random_device (which may use RDRAND
 * internally in libstdc++) produces values during recording
 * that cannot be faithfully replayed.
 *
 * Build:
 *   g++ -O2 -o exp1_random_device exp1_random_device.cpp
 *
 * Run under rr:
 *   rr record ./exp1_random_device 2>record.log
 *   rr replay -a 2>replay.log
 *   diff record.log replay.log
 *
 * Expected: values diverge between record and replay if
 * RDRAND is used internally and rr cannot intercept it.
 */

#include <cstdint>
#include <cstdio>
#include <random>

int main(void)
{
    std::random_device rd;

    fprintf(stderr, "=== random_device values ===\n");
    for (int i = 0; i < 10; i++) {
        uint32_t val = rd();
        fprintf(stderr, "rd[%d] = 0x%08x\n", i, val);
    }

    /*
     * Use the values in a way that affects program behavior,
     * so divergence is not merely cosmetic.
     */
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++)
        sum += rd();

    if (sum & 1)
        fprintf(stderr, "branch: odd\n");
    else
        fprintf(stderr, "branch: even\n");

    return 0;
}
