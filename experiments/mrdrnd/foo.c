#include <stdint.h>
static void generate_key(uint64_t key[4])
{
    unsigned long long v;
    int i;

    for (i = 0; i < 4; i++) {
        _rdrand64_step(&v);   /* ignore CF */
        key[i] = (uint64_t)v;
    }
}

