#!/bin/bash
# Smoke test for mrdrnd: minimal RDRAND emission probe.
#
# foo.c is a single-function probe that calls _rdrand64_step()
# four times.  We wrap it with a main() that prints the values
# so the binary is actually runnable, build with -mrdrnd, and
# disassemble to confirm the RDRAND instruction was emitted with
# no surrounding CPUID gate.  Then we record/replay under rr.

set -u
cd "$(dirname "$0")"
. "$(dirname "$0")/../lib.sh"

echo "=== mrdrnd ==="

# Stitch foo.c (defines generate_key) with a tiny main.
cat > _main.c <<'EOF'
#include <stdint.h>
#include <stdio.h>

void generate_key(uint64_t key[4]);

int main(void)
{
    uint64_t key[4];
    int i;
    generate_key(key);
    for (i = 0; i < 4; i++)
        fprintf(stderr, "key[%d] = 0x%016lx\n", i,
                (unsigned long)key[i]);
    return 0;
}
EOF

gcc -O2 -mrdrnd -o mrdrnd foo.c _main.c

echo "--- objdump: rdrand / cpuid sites ---"
objdump -d mrdrnd | grep -E 'rdrand|cpuid' || echo "(none)"

rr record ./mrdrnd 2> record.log
rec=$?
rr replay -a 2> replay.log
rep=$?
diverged=$(diverged_pair record.log replay.log)
errs=$(rr_error_count replay.log)

report_result mrdrnd "$rec" "$rep" "$diverged" "$errs" diverged
